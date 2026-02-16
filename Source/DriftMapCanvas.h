/*
------------------------------------------------------------------

This file is part of a plugin for the Open Ephys GUI
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

#ifndef DRIFTMAPCANVAS_H_INCLUDED
#define DRIFTMAPCANVAS_H_INCLUDED
#include "DriftMap.h"

#include <VisualizerWindowHeaders.h>
#include <vector>

class DriftMap;
class DriftMapCanvas;
class StreamScatterView;

class OptionsBar : public Component,
                   public ParameterEditorOwner,
                   public Button::Listener
{
public:
    OptionsBar (DriftMapCanvas* canvas, DriftMap* processor);
    ~OptionsBar() {}
    void buttonClicked (Button* button) override;
    void resized() override;
    void paint (Graphics& g) override;

private:
    std::unique_ptr<UtilityButton> clearButton;
    DriftMapCanvas* canvas;
    DriftMap* processor;
};

class StreamScatterView : public Component
{
public:
    StreamScatterView (DriftMap* processor, DriftMapCanvas* owner, uint16 streamId);
    ~StreamScatterView() override {}
    void paint (Graphics& g) override;
    void resized() override {}
    void refreshFromProcessor();
    void clearHistory();
    uint16 getStreamId() const { return streamId; }

private:
    void appendPeaksFromProcessor();
    void pruneHistory();

    DriftMap* processor;
    DriftMapCanvas* owner;
    uint16 streamId;
    std::vector<DriftMap::PeakEvent> displayPeaks;
    int64 latestSample = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StreamScatterView);
};

class DriftMapCanvas : public Visualizer
{
public:
    DriftMapCanvas (DriftMap* processor);
    ~DriftMapCanvas() {}

    void resized() override;
    void refreshState() override {}
    void updateSettings() override;
    void refresh() override;
    void paint (Graphics& g) override;
    void parameterValueChanged (Parameter*) override;
    void saveCustomParametersToXml (XmlElement* xml) override;
    void loadCustomParametersFromXml (XmlElement* xml) override;

    int getDisplayWindowSeconds() const;
    void clearAllViews();

private:
    void rebuildTabs();
    StreamScatterView* getCurrentView() const;

    DriftMap* processor;
    OptionsBar* optionsBar;
    std::unique_ptr<TabbedComponent> streamTabs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DriftMapCanvas);
};

#endif // DRIFTMAPCANVAS_H_INCLUDED
