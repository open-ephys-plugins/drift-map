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
#include <atomic>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

/** 
	A plugin that includes a canvas for displaying incoming data
*/

class DriftMap : public GenericProcessor
{
public:
    struct PeakEvent
    {
        double timestamp = 0.0f;
        uint16 channel = 0;
        float amplitude = 0.0f;
    };
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
    void parameterValueChanged (Parameter* param) override;

    /** Defines the functionality of the processor.
		The process method is called every time a new data buffer is available.
		Visualizer plugins typically use this method to send data to the canvas for display purposes */
    void process (AudioBuffer<float>& buffer) override;

    bool startAcquisition() override;
    bool stopAcquisition() override;

    /** Handles broadcast messages sent during acquisition
		Called automatically whenever a broadcast message is sent through the signal chain */
    void handleBroadcastMessage (const String& message, const int64 systemTimeMillis) override;

    /** Saving custom settings to XML. This method is not needed to save the state of
		Parameter objects */
    void saveCustomParametersToXml (XmlElement* parentElement) override;

    /** Load custom settings from XML. This method is not needed to load the state of
		Parameter objects*/
    void loadCustomParametersFromXml (XmlElement* parentElement) override;

    /** Drains pending peak events for a stream into an output buffer. */
    bool drainPeaks (uint16 streamId, std::vector<PeakEvent>& outPeaks);

    /** Returns stream metadata used by the canvas. */
    int getNumChannelsForStream (uint16 streamId) const;
    double getSampleRateForStream (uint16 streamId) const;

    /** Clears pending and historical processor-side peak state. */
    void clearDriftData();

private:
    struct ChannelPeakState
    {
        float prev2 = 0.0f;
        float prev1 = 0.0f;
        int64 prev1SampleNumber = -1;
        int64 lastPeakSampleNumber = std::numeric_limits<int64>::lowest() / 2;
    };

    struct StreamPeaks
    {
        int numChannels = 0;
        double sampleRate = 0.0;
        std::vector<ChannelPeakState> channelStates;
        std::vector<int> globalChannelIndices;
        std::vector<PeakEvent> pendingPeaks;
        std::unique_ptr<CriticalSection> pendingLock;
        int64 lastCumulativeBlockEndSample = 0;
    };

    void appendDetectedPeaks (StreamPeaks& streamState, const std::vector<PeakEvent>& detectedPeaks);
    void resetChannelDetectionHistory();


    static constexpr float defaultThresholdUv = -50;
    static constexpr float defaultRefractoryMs = 2;
    static constexpr size_t maxPendingPeaksPerStream = 500000;
    std::unordered_map<uint16, StreamPeaks> streamPeaks;
    std::atomic<float> thresholdUvParam { defaultThresholdUv };
    std::atomic<float> refractoryMsParam { defaultRefractoryMs };

    /** Generates an assertion if this class leaks */
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DriftMap);
};

#endif // DriftMap_H_DEFINED
