/*
------------------------------------------------------------------

This file is part of a plugin for the Open Ephys GUI
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
    /** Creates the shared options bar for the drift-map canvas. */
    OptionsBar (DriftMapCanvas* canvas, DriftMap* processor);

    /** Destroys the options bar. */
    ~OptionsBar() {}

    /** Handles button clicks from controls in the options bar. */
    void buttonClicked (Button* button) override;

    /** Lays out child controls when the component is resized. */
    void resized() override;

    /** Paints the options bar background. */
    void paint (Graphics& g) override;

private:
    std::unique_ptr<UtilityButton> clearButton;
    DriftMapCanvas* canvas;
    DriftMap* processor;
};

class StreamScatterView : public Component
{
public:
    /** Creates a per-stream scatter view backed by a session image. */
    StreamScatterView (DriftMap* processor, DriftMapCanvas* owner, uint16 streamId);

    /** Destroys the stream scatter view. */
    ~StreamScatterView() override {}

    /** Paints the current stream image and timeline overlays. */
    void paint (Graphics& g) override;

    /** Handles resize events for the view. */
    void resized() override {}

    /** Starts panning when the user presses in the plot region. */
    void mouseDown (const MouseEvent& event) override;

    /** Updates pan offset while the user drags. */
    void mouseDrag (const MouseEvent& event) override;

    /** Ends panning interaction. */
    void mouseUp (const MouseEvent& event) override;

    /** Adjusts zoom around the mouse anchor point. */
    void mouseWheelMove (const MouseEvent& event, const MouseWheelDetails& wheel) override;

    /** Resets the viewport to follow the latest data. */
    void mouseDoubleClick (const MouseEvent& event) override;

    /** Pulls newly detected peaks from the processor and repaints. */
    void refreshFromProcessor();
    /** Pulls newly detected peaks from the processor with optional repaint. */
    void updateFromProcessor (bool shouldRepaint);

    /** Clears the accumulated session image and view state. */
    void clearHistory();

    /** Enables or disables light-mode rendering. */
    void setLightMode (bool enabled);

    /** Updates the timebase used for visible-window scaling in minutes. */
    void setTimebaseMinutes (double timebaseMinutes);

    /** Returns the stream id rendered by this view. */
    uint16 getStreamId() const { return streamId; }

private:
    /** Drains available peaks from the processor into the session image. */
    void appendPeaksFromProcessor();

    /** Allocates the backing session image if needed. */
    void ensureSessionImage (Rectangle<int> plotBounds);
    /** Draws a batch of detected peaks into the backing session image. */
    void drawPeaksOnSessionImage (const std::vector<DriftMap::PeakEvent>& peaks, int numChannels);

    /** Grows the session image width to include a required x-coordinate. */
    void extendSessionImageWidth (int requiredX);

    /** Returns the maximum allowed session image width in source pixels. */
    int getMaxSessionImageWidth() const;

    /** Positions the viewport to the newest visible data. */
    void resetViewToLatest();

    /** Rebuilds cached inverted imagery when theme data is stale. */
    void updateThemeCacheIfNeeded();

    /** Resets temporal mapping and pan/zoom state. */
    void resetSweepState();

    DriftMap* processor;
    DriftMapCanvas* owner;
    uint16 streamId;
    Image sessionImage;
    Image invertedSessionImage;
    bool invertedDirty = true;
    bool lightModeEnabled = false;
    double sessionStartTimeMinutes = -1.0;
    double minutesPerPixel = 0.0;
    int latestDrawnX = -1;
    int64 droppedSourcePixels = 0;
    double viewStartX = 0.0;
    double viewZoom = 1.0;
    bool followLatest = true;
    bool isPanning = false;
    int lastDragX = 0;
    int cachedPlotWidth = 0;
    int cachedPlotHeight = 0;
    Colour clearColour = Colour (40, 40, 40);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StreamScatterView);
};

class DriftMapCanvas : public Visualizer
{
public:
    /** Creates the drift-map visualizer canvas. */
    DriftMapCanvas (DriftMap* processor);

    /** Destroys the drift-map canvas. */
    ~DriftMapCanvas() {}
    /** Lays out tabs and the bottom options viewport. */

    void resized() override;

    /** Refreshes transient visualizer state. */
    void refreshState() override {}

    /** Rebuilds UI tabs to match current stream configuration. */
    void updateSettings() override;

    /** Refreshes the currently selected stream view. */
    void refresh() override;

    /** Paints the canvas background. */
    void paint (Graphics& g) override;

    /** Updates tab/theme visuals after LookAndFeel changes. */
    void lookAndFeelChanged() override;

    /** Reacts to canvas parameter changes. */
    void parameterValueChanged (Parameter*) override;

    /** Saves canvas-specific parameters to XML. */
    void saveCustomParametersToXml (XmlElement* xml) override;

    /** Restores canvas-specific parameters from XML. */
    void loadCustomParametersFromXml (XmlElement* xml) override;
    /** Returns the selected display window duration in minutes. */
    double getDisplayWindowMinutes() const;

    /** Clears history for all stream views. */
    void clearAllViews();

private:
    /** Recreates stream tabs for the currently available streams. */
    void rebuildTabs();

    /** Applies current tab colours to all tab buttons. */
    void refreshTabColours();

    /** Returns the currently selected stream view, if any. */
    StreamScatterView* getCurrentView() const;

    DriftMap* processor;
    std::unique_ptr<OptionsBar> optionsBar;
    std::unique_ptr<Viewport> optionsViewport;
    std::unique_ptr<TabbedComponent> streamTabs;
    int hiddenTabRefreshCounter = 0;
    int nextHiddenTabIndex = 0;

    static constexpr int hiddenTabRefreshIntervalTicks = 5;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DriftMapCanvas);
};

#endif // DRIFTMAPCANVAS_H_INCLUDED
