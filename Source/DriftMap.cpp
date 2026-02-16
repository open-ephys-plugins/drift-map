/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2022 Open Ephys

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
    addIntParameter (Parameter::PROCESSOR_SCOPE,
                     "threshold_uv",
                     "Threshold (uV)",
                     "Negative peak threshold in microvolts",
                     defaultThresholdUv,
                     -500,
                     -5);

    addIntParameter (Parameter::PROCESSOR_SCOPE,
                     "refractory_ms",
                     "Refractory (ms)",
                     "Minimum separation between peaks on a channel",
                     defaultRefractoryMs,
                     0,
                     20);
}

AudioProcessorEditor* DriftMap::createEditor()
{
    editor = std::make_unique<DriftMapEditor> (this);
    return editor.get();
}

void DriftMap::updateSettings()
{
    std::unordered_map<uint16, StreamPeaks> updatedStates;

    for (auto stream : getDataStreams())
    {
        StreamPeaks streamState;
        streamState.numChannels = stream->getChannelCount();
        streamState.sampleRate = stream->getSampleRate();
        streamState.channelStates.resize ((size_t) streamState.numChannels);
        streamState.pendingPeaks.reserve (maxPendingPeaksPerStream);
        streamState.pendingLock = std::make_unique<CriticalSection>();

        updatedStates[stream->getStreamId()] = std::move (streamState);
    }

    streamPeaks = std::move (updatedStates);
}

void DriftMap::parameterValueChanged (Parameter* parameter)
{
    if (parameter->getName().equalsIgnoreCase ("threshold_uv")
        || parameter->getName().equalsIgnoreCase ("refractory_ms"))
    {
        return;
    }
}

bool DriftMap::startAcquisition()
{

    DriftMapEditor* editor = (DriftMapEditor*) getEditor();
    editor->enable();

    clearDriftData();
    return true;
}

bool DriftMap::stopAcquisition()
{
    DriftMapEditor* editor = (DriftMapEditor*) getEditor();
    editor->disable();

    clearDriftData();
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
    const float thresholdUv = (float) getParameter ("threshold_uv")->getValue();
    const int refractoryMs = (int) getParameter ("refractory_ms")->getValue();

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

        if ((int) streamState.channelStates.size() != streamState.numChannels)
            streamState.channelStates.resize ((size_t) streamState.numChannels);

        const int64 blockFirstSample = getFirstSampleNumberForBlock (streamId);
        const int refractorySamples = jmax (0, (int) ((refractoryMs / 1000.0) * streamState.sampleRate));

        std::vector<PeakEvent> detectedPeaks;
        detectedPeaks.reserve ((size_t) (samplesPerBlock * jmax (1, streamState.numChannels / 32)));

        for (int sample = 0; sample < samplesPerBlock; ++sample)
        {
            const int64 sampleNumber = blockFirstSample + sample;

            for (int localChannel = 0; localChannel < streamState.numChannels; ++localChannel)
            {
                ChannelPeakState& channelState = streamState.channelStates[(size_t) localChannel];

                const int globalChannel = getGlobalChannelIndex (streamId, localChannel);
                const float currentSample = buffer.getSample (globalChannel, sample);

                if (channelState.initCount >= 2)
                {
                    const bool localMin = (channelState.prev2 > channelState.prev1)
                                          && (channelState.prev1 <= currentSample);
                    const bool belowThreshold = channelState.prev1 < thresholdUv;
                    const bool outsideRefractory = (channelState.prev1SampleNumber - channelState.lastPeakSampleNumber) > refractorySamples;

                    if (localMin && belowThreshold && outsideRefractory)
                    {
                        PeakEvent peak;
                        peak.sampleNumber = channelState.prev1SampleNumber;
                        peak.channel = (uint16) localChannel;
                        detectedPeaks.push_back (peak);
                        channelState.lastPeakSampleNumber = channelState.prev1SampleNumber;
                    }
                }

                channelState.prev2 = channelState.prev1;
                channelState.prev1 = currentSample;
                channelState.prev1SampleNumber = sampleNumber;
                if (channelState.initCount < 2)
                    channelState.initCount++;
            }
        }

        appendDetectedPeaks (streamState, detectedPeaks);
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
            channelState.initCount = 0;
            channelState.prev2 = 0.0f;
            channelState.prev1 = 0.0f;
            channelState.prev1SampleNumber = -1;
            channelState.lastPeakSampleNumber = std::numeric_limits<int64>::lowest() / 2;
        }
    }
}

void DriftMap::saveCustomParametersToXml (XmlElement* parentElement)
{
}

void DriftMap::loadCustomParametersFromXml (XmlElement* parentElement)
{
}
