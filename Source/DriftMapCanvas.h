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
    void mouseDown (const MouseEvent& event) override;
    void mouseDrag (const MouseEvent& event) override;
    void mouseUp (const MouseEvent& event) override;
    void mouseWheelMove (const MouseEvent& event, const MouseWheelDetails& wheel) override;
    void mouseDoubleClick (const MouseEvent& event) override;
    void refreshFromProcessor();
    void clearHistory();
    void setLightMode (bool enabled);
    void setTimebaseSeconds (double timebaseSeconds);
    uint16 getStreamId() const { return streamId; }

private:
    void appendPeaksFromProcessor();
    void ensureSessionImage (Rectangle<int> plotBounds);
    void drawPeakOnSessionImage (const DriftMap::PeakEvent& peak, int numChannels);
    void extendSessionImageWidth (int requiredX);
    void resetViewToLatest();
    void updateThemeCacheIfNeeded();
    void resetSweepState();

    DriftMap* processor;
    DriftMapCanvas* owner;
    uint16 streamId;
    Image sessionImage;
    Image invertedSessionImage;
    bool invertedDirty = true;
    bool lightModeEnabled = false;
    double sessionStartTimeSeconds = -1.0;
    double secondsPerPixel = 0.0;
    int latestDrawnX = -1;
    double viewStartX = 0.0;
    double viewZoom = 1.0;
    bool followLatest = true;
    bool isPanning = false;
    int lastDragX = 0;
    int cachedPlotWidth = 0;
    int cachedPlotHeight = 0;
    Colour clearColour = Colour(40, 40, 40);

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
    void lookAndFeelChanged() override;
    void parameterValueChanged (Parameter*) override;
    void saveCustomParametersToXml (XmlElement* xml) override;
    void loadCustomParametersFromXml (XmlElement* xml) override;

    int getDisplayWindowSeconds() const;
    void clearAllViews();

private:
    void rebuildTabs();
    void refreshTabColours();
    StreamScatterView* getCurrentView() const;

    DriftMap* processor;
    std::unique_ptr<OptionsBar> optionsBar;
    std::unique_ptr<Viewport> optionsViewport;
    std::unique_ptr<TabbedComponent> streamTabs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DriftMapCanvas);
};

#endif // DRIFTMAPCANVAS_H_INCLUDED
