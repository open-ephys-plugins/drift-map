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

//This prevents include loops. We recommend changing the macro to a name suitable for your plugin
#ifndef DRIFTMAP_H_DEFINED
#define DRIFTMAP_H_DEFINED

#include <ProcessorHeaders.h>
#include <unordered_map>

/** 
	A plugin that includes a canvas for displaying incoming data
*/

class DriftMap : public GenericProcessor,
                 public ChangeBroadcaster
{
public:
    /** The class constructor, used to initialize any members.*/
    DriftMap();

    /** The class destructor, used to deallocate memory*/
    ~DriftMap();

    /** If the processor has a custom editor, this method must be defined to instantiate it. */
    AudioProcessorEditor* createEditor() override;

    /** Called every time the settings of an upstream plugin are changed.
		Allows the processor to handle variations in the channel configuration or any other parameter
		passed through signal chain. The processor can use this function to modify channel objects that
		will be passed to downstream plugins. */
    void updateSettings() override;

    void registerParameters() override;

    /** Called when a parameter value is changed*/
    void parameterValueChanged (Parameter* param);

    /** Defines the functionality of the processor.
		The process method is called every time a new data buffer is available.
		Visualizer plugins typically use this method to send data to the canvas for display purposes */
    void process (AudioBuffer<float>& buffer) override;

    /** Handles broadcast messages sent during acquisition
		Called automatically whenever a broadcast message is sent through the signal chain */
    void handleBroadcastMessage (const String& message, const int64 systemTimeMillis) override;

    /** Saving custom settings to XML. This method is not needed to save the state of
		Parameter objects */
    void saveCustomParametersToXml (XmlElement* parentElement) override;

    /** Load custom settings from XML. This method is not needed to load the state of
		Parameter objects*/
    void loadCustomParametersFromXml (XmlElement* parentElement) override;

    /** Returns a pointer to the snapshot buffer */
    AudioBuffer<float>* getSnapshot();
    AudioBuffer<float>* getSnapshot (uint16 streamId);

    /** Returns true if the snapshot for the given stream is ready */
    bool isSnapshotReady (uint16 streamId) const;

private:
    struct StreamSnapshot
    {
        AudioBuffer<float> snapshotBuffer;
        int numSamples = 0;
        int numChannels = 0;
        int writePos = 0;
        bool pendingSnap = false;
        bool snapshotReady = false;
    };
    void ensureBufferForStream (StreamSnapshot& snapshot);
    void writeBlockToSnapshot (StreamSnapshot& snapshot, DataStream* stream, AudioBuffer<float>& buffer, int samplesPerBlock);
    /**Check whether data stream exists */
    bool streamExists (uint16 streamId);
    uint16 currentStream;
    std::unordered_map<uint16, StreamSnapshot> streamSnapshots;
    AudioBuffer<float> emptySnapshotBuffer;

    static constexpr int defaultWindowMs = 3000;

    /** Generates an assertion if this class leaks */
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DriftMap);
};

#endif // DriftMap_H_DEFINED
