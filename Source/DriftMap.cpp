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
                     "Refractory (ms)",
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

        const uint16 streamId = stream->getStreamId();
        for (int localChannel = 0; localChannel < streamState.numChannels; ++localChannel)
            streamState.globalChannelIndices[(size_t) localChannel] = getGlobalChannelIndex (streamId, localChannel);

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
