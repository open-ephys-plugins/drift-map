/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2026 Open Ephys

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "DriftMap.h"

#include "DriftMapEditor.h"
#include <algorithm>
#include <cmath>
#include <numeric>

DriftMap::DriftMap()
    : GenericProcessor ("Drift Map")
{
}

DriftMap::~DriftMap()
{
}

void DriftMap::registerParameters()
{
    thresholdUvParam.store (defaultThresholdUv, std::memory_order_relaxed);
    refractoryMsParam.store (defaultRefractoryMs, std::memory_order_relaxed);

    addFloatParameter (Parameter::PROCESSOR_SCOPE,
                       "threshold_uv",
                       "Threshold (uV)",
                       "Negative peak threshold in microvolts",
                       "uV",
                       defaultThresholdUv,
                       -500,
                       -50,
                       1.0);

    addFloatParameter (Parameter::PROCESSOR_SCOPE,
                       "refractory_ms",
                       "Refractory",
                       "Minimum separation between peaks on a channel",
                       "ms",
                       defaultRefractoryMs,
                       0.5,
                       20,
                       0.5);
}

AudioProcessorEditor* DriftMap::createEditor()
{
    editor = std::make_unique<DriftMapEditor> (this);
    return editor.get();
}

void DriftMap::updateSettings()
{
    auto previousStates = std::move (streamPeaks);
    std::unordered_map<uint16, StreamPeaks> updatedStates;

    for (auto stream : getDataStreams())
    {
        StreamPeaks streamState;
        streamState.numChannels = stream->getChannelCount();
        streamState.sampleRate = stream->getSampleRate();
        streamState.channelStates.resize ((size_t) streamState.numChannels);
        streamState.globalChannelIndices.resize ((size_t) streamState.numChannels);
        streamState.displayChannelOrder.resize ((size_t) streamState.numChannels);

        const uint16 streamId = stream->getStreamId();
        for (int localChannel = 0; localChannel < streamState.numChannels; ++localChannel)
        {
            streamState.globalChannelIndices[(size_t) localChannel] = getGlobalChannelIndex (streamId, localChannel);
            streamState.displayChannelOrder[(size_t) localChannel] = localChannel;
        }

        auto continuousChannels = stream->getContinuousChannels();
        if (continuousChannels.size() == streamState.numChannels && streamState.numChannels > 0)
        {
            std::vector<float> depths ((size_t) streamState.numChannels, 0.0f);
            std::vector<float> xposValues ((size_t) streamState.numChannels, 0.0f);
            std::vector<int> groups ((size_t) streamState.numChannels, 0);
            std::vector<bool> hasYposMetadata ((size_t) streamState.numChannels, false);
            std::vector<bool> hasXposMetadata ((size_t) streamState.numChannels, false);
            std::vector<bool> hasGroupMetadata ((size_t) streamState.numChannels, false);

            bool allSame = true;
            bool anyYposMetadata = false;
            bool anyXposMetadata = false;
            bool anyGroupMetadata = false;
            float last = 0.0f;

            for (int i = 0; i < streamState.numChannels; ++i)
            {
                const auto* channel = continuousChannels[i];
                if (channel == nullptr)
                    continue;

                float ypos = channel->position.y;
                const int yposMetadataIndex = channel->findMetadata (MetadataDescriptor::MetadataType::FLOAT, 1, "channel.ypos");
                if (yposMetadataIndex >= 0)
                {
                    if (const auto* yposValue = channel->getMetadataValue (yposMetadataIndex))
                    {
                        yposValue->getValue (ypos);
                        hasYposMetadata[(size_t) i] = true;
                        hasXposMetadata[(size_t) i] = true;
                    }
                }

                depths[(size_t) i] = ypos;
                xposValues[(size_t) i] = channel->position.x;
                groups[(size_t) i] = channel->group.number;
                hasGroupMetadata[(size_t) i] = ! channel->group.name.equalsIgnoreCase ("default");

                anyYposMetadata = anyYposMetadata || hasYposMetadata[(size_t) i];
                anyXposMetadata = anyXposMetadata || hasXposMetadata[(size_t) i];
                anyGroupMetadata = anyGroupMetadata || hasGroupMetadata[(size_t) i];

                if (i == 0)
                    last = depths[(size_t) i];
                else if (depths[(size_t) i] != last)
                    allSame = false;
            }

            const bool positionMetadataAvailable = anyYposMetadata && anyXposMetadata;
            const bool groupMetadataAvailable = anyGroupMetadata;

            if (groupMetadataAvailable || ! allSame || anyYposMetadata)
            {
                std::vector<int> order ((size_t) streamState.numChannels);
                std::iota (order.begin(), order.end(), 0);

                std::sort (order.begin(), order.end(), [&] (int i, int j)
                           {
                    const float depthDiff = depths[(size_t) i] - depths[(size_t) j];
                    const float depthEpsilon = 1.0e-3f;

                    if (groupMetadataAvailable && groups[(size_t) i] != groups[(size_t) j])
                        return groups[(size_t) i] < groups[(size_t) j];

                    if (std::abs (depthDiff) >= depthEpsilon)
                        return depths[(size_t) i] < depths[(size_t) j];

                    if (positionMetadataAvailable)
                        return xposValues[(size_t) i] < xposValues[(size_t) j];

                    return i < j; });

                for (int orderedIndex = 0; orderedIndex < streamState.numChannels; ++orderedIndex)
                    streamState.displayChannelOrder[(size_t) order[(size_t) orderedIndex]] = orderedIndex;
            }
        }

        streamState.pendingPeaks.reserve (maxPendingPeaksPerStream);
        streamState.pendingLock = std::make_unique<CriticalSection>();

        auto previousStateIt = previousStates.find (streamId);
        if (previousStateIt != previousStates.end())
        {
            StreamPeaks& previousState = previousStateIt->second;
            streamState.lastCumulativeBlockEndSample = previousState.lastCumulativeBlockEndSample;

            if (previousState.pendingLock != nullptr)
            {
                const ScopedLock lock (*previousState.pendingLock);
                streamState.pendingPeaks = std::move (previousState.pendingPeaks);
            }
        }

        if (streamState.pendingPeaks.capacity() < maxPendingPeaksPerStream)
            streamState.pendingPeaks.reserve (maxPendingPeaksPerStream);

        updatedStates[streamId] = std::move (streamState);
    }

    streamPeaks = std::move (updatedStates);
}

void DriftMap::parameterValueChanged (Parameter* parameter)
{
    if (parameter->getName().equalsIgnoreCase ("threshold_uv"))
        thresholdUvParam.store ((float) parameter->getValue(), std::memory_order_relaxed);
    else if (parameter->getName().equalsIgnoreCase ("refractory_ms"))
        refractoryMsParam.store ((float) parameter->getValue(), std::memory_order_relaxed);
}

bool DriftMap::startAcquisition()
{
    auto* driftMapEditor = dynamic_cast<DriftMapEditor*> (getEditor());
    if (driftMapEditor != nullptr)
        driftMapEditor->enable();
    resetChannelDetectionHistory();
    return true;
}

bool DriftMap::stopAcquisition()
{
    auto* driftMapEditor = dynamic_cast<DriftMapEditor*> (getEditor());
    if (driftMapEditor != nullptr)
        driftMapEditor->disable();
    return true;
}

void DriftMap::appendDetectedPeaks (StreamPeaks& streamState, const std::vector<PeakEvent>& detectedPeaks)
{
    if (detectedPeaks.empty())
        return;

    const ScopedLock lock (*streamState.pendingLock);

    const size_t currentSize = streamState.pendingPeaks.size();
    if (currentSize >= maxPendingPeaksPerStream)
        return;

    const size_t remaining = maxPendingPeaksPerStream - currentSize;
    const size_t toCopy = jmin (remaining, detectedPeaks.size());

    streamState.pendingPeaks.insert (streamState.pendingPeaks.end(),
                                     detectedPeaks.begin(),
                                     detectedPeaks.begin() + (int64) toCopy);
}

void DriftMap::process (AudioBuffer<float>& buffer)
{
    constexpr int sampleStride = 2;
    constexpr int maxPeaksPerChannelPerBuffer = 10;
    const float thresholdUv = (float) thresholdUvParam.load (std::memory_order_relaxed);
    const int refractoryMs = refractoryMsParam.load (std::memory_order_relaxed);

    for (auto& entry : streamPeaks)
    {
        const uint16 streamId = entry.first;
        StreamPeaks& streamState = entry.second;

        DataStream* stream = getDataStream (streamId);
        if (stream == nullptr || streamState.numChannels <= 0)
            continue;

        const int samplesPerBlock = getNumSamplesInBlock (streamId);
        if (samplesPerBlock <= 0)
            continue;

        if ((int) streamState.channelStates.size() != streamState.numChannels
            || (int) streamState.globalChannelIndices.size() != streamState.numChannels)
        {
            streamState.channelStates.resize ((size_t) streamState.numChannels);
            streamState.globalChannelIndices.resize ((size_t) streamState.numChannels);
            for (int localChannel = 0; localChannel < streamState.numChannels; ++localChannel)
                streamState.globalChannelIndices[(size_t) localChannel] = getGlobalChannelIndex (streamId, localChannel);
        }

        const int refractorySamples = jmax (0, (int) ((refractoryMs / 1000.0) * streamState.sampleRate));

        std::vector<PeakEvent> detectedPeaks;
        detectedPeaks.reserve ((size_t) jmax (32, (samplesPerBlock * streamState.numChannels) / 256));

        for (int localChannel = 0; localChannel < streamState.numChannels; ++localChannel)
        {
            ChannelPeakState& channelState = streamState.channelStates[(size_t) localChannel];
            const float* channelData = buffer.getReadPointer (streamState.globalChannelIndices[(size_t) localChannel]);

            float prev2 = channelState.prev2;
            float prev1 = channelState.prev1;
            int64 prev1SampleNumber = channelState.prev1SampleNumber;
            int64 lastPeakSampleNumber = channelState.lastPeakSampleNumber;
            int peaksAddedForChannel = 0;

            for (int sample = 0; sample < samplesPerBlock; sample += sampleStride)
            {
                const float currentSample = channelData[sample];

                const bool localMin = (prev2 > prev1) && (prev1 <= currentSample);
                const bool belowThreshold = prev1 < thresholdUv;
                const bool outsideRefractory = (prev1SampleNumber - lastPeakSampleNumber) > refractorySamples;
                const bool belowPerBufferLimit = peaksAddedForChannel < maxPeaksPerChannelPerBuffer;

                if (localMin && belowThreshold && outsideRefractory && belowPerBufferLimit)
                {
                    PeakEvent peak;
                    peak.timestamp = double (prev1SampleNumber) / streamState.sampleRate;
                    peak.channel = (uint16) localChannel;
                    peak.amplitude = -prev1;
                    detectedPeaks.push_back (peak);
                    ++peaksAddedForChannel;
                    lastPeakSampleNumber = prev1SampleNumber;
                }

                prev2 = prev1;
                prev1 = currentSample;
                prev1SampleNumber = streamState.lastCumulativeBlockEndSample + sample;
            }

            channelState.prev2 = prev2;
            channelState.prev1 = prev1;
            channelState.prev1SampleNumber = prev1SampleNumber;
            channelState.lastPeakSampleNumber = lastPeakSampleNumber;
        }

        streamState.lastCumulativeBlockEndSample += samplesPerBlock;
        appendDetectedPeaks (streamState, detectedPeaks);
    }
}

void DriftMap::resetChannelDetectionHistory()
{
    for (auto& entry : streamPeaks)
    {
        StreamPeaks& streamState = entry.second;
        for (auto& channelState : streamState.channelStates)
        {
            channelState.prev2 = 0.0f;
            channelState.prev1 = 0.0f;
            channelState.prev1SampleNumber = -1;
            channelState.lastPeakSampleNumber = std::numeric_limits<int64>::lowest() / 2;
        }
    }
}

void DriftMap::handleBroadcastMessage (const String& message, const int64 systemTimeMillis)
{
}

bool DriftMap::drainPeaks (uint16 streamId, std::vector<PeakEvent>& outPeaks)
{
    outPeaks.clear();

    auto it = streamPeaks.find (streamId);
    if (it == streamPeaks.end())
        return false;

    StreamPeaks& streamState = it->second;
    const ScopedLock lock (*streamState.pendingLock);
    outPeaks.swap (streamState.pendingPeaks);
    return true;
}

int DriftMap::getNumChannelsForStream (uint16 streamId) const
{
    auto it = streamPeaks.find (streamId);
    if (it == streamPeaks.end())
        return 0;

    return it->second.numChannels;
}

double DriftMap::getSampleRateForStream (uint16 streamId) const
{
    auto it = streamPeaks.find (streamId);
    if (it == streamPeaks.end())
        return 0.0;

    return it->second.sampleRate;
}

int DriftMap::getDisplayChannelForStream (uint16 streamId, int localChannel) const
{
    auto it = streamPeaks.find (streamId);
    if (it == streamPeaks.end())
        return localChannel;

    const auto& order = it->second.displayChannelOrder;
    if (localChannel < 0 || localChannel >= (int) order.size())
        return localChannel;

    return order[(size_t) localChannel];
}

void DriftMap::clearDriftData()
{
    for (auto& entry : streamPeaks)
    {
        StreamPeaks& streamState = entry.second;

        {
            const ScopedLock lock (*streamState.pendingLock);
            streamState.pendingPeaks.clear();
        }

        for (auto& channelState : streamState.channelStates)
        {
            channelState.prev2 = 0.0f;
            channelState.prev1 = 0.0f;
            channelState.prev1SampleNumber = -1;
            channelState.lastPeakSampleNumber = std::numeric_limits<int64>::lowest() / 2;
        }

        streamState.lastCumulativeBlockEndSample = -1;
    }
}

void DriftMap::saveCustomParametersToXml (XmlElement* parentElement)
{
}

void DriftMap::loadCustomParametersFromXml (XmlElement* parentElement)
{
}
