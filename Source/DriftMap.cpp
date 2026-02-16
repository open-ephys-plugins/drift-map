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
    : GenericProcessor ("Drift Map"), currentStream (0)
{
}

DriftMap::~DriftMap()
{
}

void DriftMap::registerParameters()
{
    addNotificationParameter (Parameter::PROCESSOR_SCOPE,
                              "snap",
                              "Snap",
                              "Used to trigger snapshots",
                              false);

    addIntParameter (Parameter::PROCESSOR_SCOPE,
                     "window",
                     "Window",
                     "Duration of snapshot window in milliseconds",
                     defaultWindowMs,
                     50,
                     500);
}

AudioProcessorEditor* DriftMap::createEditor()
{
    editor = std::make_unique<DriftMapEditor> (this);
    return editor.get();
}

void DriftMap::updateSettings()
{
    std::unordered_map<uint16, StreamSnapshot> updatedSnapshots;

    auto streams = getDataStreams();
    int windowMs = (int) getParameter ("window")->getValue();

    for (auto stream : streams)
    {
        StreamSnapshot snapshot;
        snapshot.numChannels = stream->getChannelCount();
        snapshot.numSamples = int (stream->getSampleRate() * ((float) windowMs / 1000));
        snapshot.writePos = 0;
        snapshot.pendingSnap = false;

        updatedSnapshots[stream->getStreamId()] = std::move (snapshot);
    }

    streamSnapshots = std::move (updatedSnapshots);

    if (streams.size() > 0)
        currentStream = streams[0]->getStreamId();
    else
        currentStream = 0;
}

bool DriftMap::streamExists (uint16 streamId)
{
    for (auto stream : getDataStreams())
    {
        if (stream->getStreamId() == streamId)
            return true;
    }

    return false;
}

void DriftMap::parameterValueChanged (Parameter* parameter)
{
    if (parameter->getName().equalsIgnoreCase ("snap") && CoreServices::getAcquisitionStatus())
    {
        LOGD ("Snapshot triggered");

        for (auto& entry : streamSnapshots)
        {
            entry.second.pendingSnap = true;
            entry.second.snapshotReady = false;
            entry.second.writePos = 0;
        }
        return;
    }

    if (parameter->getName().equalsIgnoreCase ("window"))
    {
        LOGD ("Window parameter changed, updating snapshot settings");

        int windowMs = (int) parameter->getValue();
        for (auto& entry : streamSnapshots)
        {
            auto stream = getDataStream (entry.first);
            if (stream != nullptr)
            {
                entry.second.numSamples = int (stream->getSampleRate() * ((float) windowMs / 1000));
                entry.second.writePos = 0;
                entry.second.pendingSnap = false;
            }
        }
    }
}

void DriftMap::process (AudioBuffer<float>& buffer)
{
    for (auto& entry : streamSnapshots)
    {
        const uint16 streamId = entry.first;
        StreamSnapshot& snapshot = entry.second;

        if (! snapshot.pendingSnap)
            continue;

        int samplesPerBlock = getNumSamplesInBlock (streamId);
        DataStream* stream = getDataStream (streamId);
        if (stream == nullptr || snapshot.numChannels == 0 || snapshot.numSamples == 0)
            continue;

        ensureBufferForStream (snapshot);
        writeBlockToSnapshot (snapshot, stream, buffer, samplesPerBlock);

        // Check if buffer is now full
        if (snapshot.writePos >= snapshot.numSamples)
        {
            LOGD ("Finishing snapshot for stream ", streamId);

            snapshot.pendingSnap = false;
            snapshot.snapshotReady = true;
            sendChangeMessage();
        }
    }
}

void DriftMap::handleBroadcastMessage (const String& message, const int64 systemTimeMillis)
{
}

AudioBuffer<float>* DriftMap::getSnapshot()
{
    return getSnapshot (currentStream);
}

AudioBuffer<float>* DriftMap::getSnapshot (uint16 streamId)
{
    auto it = streamSnapshots.find (streamId);
    if (it == streamSnapshots.end())
        return &emptySnapshotBuffer;

    it->second.snapshotReady = false;
    return &it->second.snapshotBuffer;
}

bool DriftMap::isSnapshotReady (uint16 streamId) const
{
    auto it = streamSnapshots.find (streamId);
    if (it == streamSnapshots.end())
        return false;
    return it->second.snapshotReady;
}

void DriftMap::ensureBufferForStream (StreamSnapshot& snapshot)
{
    if (snapshot.snapshotBuffer.getNumChannels() != snapshot.numChannels
        || snapshot.snapshotBuffer.getNumSamples() != snapshot.numSamples)
    {
        snapshot.snapshotBuffer.setSize (snapshot.numChannels, snapshot.numSamples);
        snapshot.snapshotBuffer.clear();
        snapshot.writePos = 0;
    }
}

void DriftMap::writeBlockToSnapshot (StreamSnapshot& snapshot, DataStream* stream, AudioBuffer<float>& buffer, int samplesPerBlock)
{
    if (snapshot.numSamples <= 0)
        return;

    LOGD ("Writing block to snapshot: streamId=", stream->getStreamId(), ", writePos=", snapshot.writePos, ", samplesPerBlock=", samplesPerBlock);

    // Calculate how many samples we can write (don't exceed buffer size)
    const int spaceRemaining = snapshot.numSamples - snapshot.writePos;
    const int samplesToWrite = jmin (samplesPerBlock, spaceRemaining);

    if (samplesToWrite <= 0)
        return;

    for (int localChannelIndex = 0; localChannelIndex < snapshot.numChannels; localChannelIndex++)
    {
        int globalChannelIndex = getGlobalChannelIndex (stream->getStreamId(), localChannelIndex);
        snapshot.snapshotBuffer.copyFrom (localChannelIndex, // destChannel
                                          snapshot.writePos, // destSample
                                          buffer, // source
                                          globalChannelIndex, // sourceChannel
                                          0, // source start sample
                                          samplesToWrite); // num samples
    }

    snapshot.writePos += samplesToWrite;
}

void DriftMap::saveCustomParametersToXml (XmlElement* parentElement)
{
}

void DriftMap::loadCustomParametersFromXml (XmlElement* parentElement)
{
}