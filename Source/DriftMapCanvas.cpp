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

#include "DriftMapCanvas.h"

#include "ColorMap.h"
#include "DriftMap.h"

#include <algorithm>

StreamScatterView::StreamScatterView (DriftMap* processor_, DriftMapCanvas* owner_, uint16 streamId_)
    : processor (processor_), owner (owner_), streamId (streamId_)
{
}

void StreamScatterView::resetSweepState()
{
    windowPeaks.clear();
    latestSample = -1;
    windowStartSample = -1;
    heatmapImage = Image();
    heatmapDirty = true;
    cachedWindowSamples = 0;
}

void StreamScatterView::drawPeakOnImage (const DriftMap::PeakEvent& peak, Rectangle<int> plotBounds, int64 windowSamples, int numChannels)
{
    if (! heatmapImage.isValid() || windowSamples <= 0)
        return;

    const int64 dx = peak.sampleNumber - windowStartSample;
    if (dx < 0 || dx >= windowSamples)
        return;

    const int x = (int) ((dx * (plotBounds.getWidth() - 1)) / windowSamples);
    if (x < 0 || x >= plotBounds.getWidth())
        return;

    int yPixel = plotBounds.getBottom() - 1;
    if (numChannels > 1)
    {
        const double yNorm = (double) peak.channel / (double) (numChannels - 1);
        yPixel = plotBounds.getBottom() - 1 - (int) (yNorm * (plotBounds.getHeight() - 1));
    }

    const int relativeY = yPixel - plotBounds.getY();
    if (relativeY < 0 || relativeY >= plotBounds.getHeight())
        return;

    const float maxAmpUv = (float) owner->getMaxAmplitudeUv();
    const float normalized = jlimit (0.0f, 1.0f, jmax (0.0f, peak.amplitude) / jmax (1.0f, maxAmpUv));
    const Colour pointColour = ColorMap::getColorForNormalizedValue (normalized).withAlpha (0.92f);

    Graphics imageGraphics (heatmapImage);
    imageGraphics.setColour (pointColour);

    constexpr float circleDiameter = 3.5f;
    constexpr float radius = circleDiameter * 0.5f;
    imageGraphics.fillEllipse ((float) x - radius,
                               (float) relativeY - radius,
                               circleDiameter,
                               circleDiameter);
}

void StreamScatterView::rebuildHeatmapImage (Rectangle<int> plotBounds, int64 windowSamples, int numChannels)
{
    if (plotBounds.getWidth() <= 1 || plotBounds.getHeight() <= 1)
        return;

    heatmapImage = Image (Image::ARGB,
                          plotBounds.getWidth(),
                          plotBounds.getHeight(),
                          true,
                          SoftwareImageType());
    Graphics imageGraphics (heatmapImage);
    imageGraphics.fillAll (Colours::transparentBlack);

    for (const auto& peak : windowPeaks)
        drawPeakOnImage (peak, plotBounds, windowSamples, numChannels);

    heatmapDirty = false;
    cachedPlotWidth = plotBounds.getWidth();
    cachedPlotHeight = plotBounds.getHeight();
    cachedWindowSamples = windowSamples;
}

void StreamScatterView::appendPeaksFromProcessor()
{
    if (processor == nullptr || owner == nullptr)
        return;

    const int numChannels = processor->getNumChannelsForStream (streamId);
    const double sampleRate = processor->getSampleRateForStream (streamId);
    if (numChannels <= 0 || sampleRate <= 0.0)
        return;

    const int64 windowSamples = (int64) (sampleRate * owner->getDisplayWindowSeconds());
    if (windowSamples <= 0)
        return;

    std::vector<DriftMap::PeakEvent> newPeaks;
    if (! processor->drainPeaks (streamId, newPeaks) || newPeaks.empty())
        return;

    std::sort (newPeaks.begin(),
               newPeaks.end(),
               [] (const DriftMap::PeakEvent& a, const DriftMap::PeakEvent& b) { return a.sampleNumber < b.sampleNumber; });

    latestSample = jmax (latestSample, newPeaks.back().sampleNumber);

    Rectangle<int> plotBounds = getLocalBounds().reduced (16);
    const bool needResetForGeometry = ! heatmapImage.isValid()
                                      || plotBounds.getWidth() != cachedPlotWidth
                                      || plotBounds.getHeight() != cachedPlotHeight
                                      || windowSamples != cachedWindowSamples;

    if (needResetForGeometry)
    {
        heatmapImage = Image (Image::ARGB,
                              jmax (2, plotBounds.getWidth()),
                              jmax (2, plotBounds.getHeight()),
                              true,
                              SoftwareImageType());
        Graphics imageGraphics (heatmapImage);
        imageGraphics.fillAll (Colours::transparentBlack);
        cachedPlotWidth = plotBounds.getWidth();
        cachedPlotHeight = plotBounds.getHeight();
        cachedWindowSamples = windowSamples;
        heatmapDirty = false;
    }

    if (windowStartSample < 0)
    {
        const int64 firstSample = newPeaks.front().sampleNumber;
        windowStartSample = (firstSample / windowSamples) * windowSamples;
    }

    for (const auto& peak : newPeaks)
    {
        while (peak.sampleNumber >= windowStartSample + windowSamples)
        {
            windowStartSample += windowSamples;
            windowPeaks.clear();

            if (heatmapImage.isValid())
            {
                Graphics imageGraphics (heatmapImage);
                imageGraphics.fillAll (Colours::transparentBlack);
            }
        }

        if (peak.sampleNumber < windowStartSample)
            continue;

        windowPeaks.push_back (peak);
        drawPeakOnImage (peak, plotBounds, windowSamples, numChannels);
    }

    constexpr size_t maxWindowPeaks = 300000;
    if (windowPeaks.size() > maxWindowPeaks)
    {
        const size_t removeCount = windowPeaks.size() - maxWindowPeaks;
        windowPeaks.erase (windowPeaks.begin(), windowPeaks.begin() + (int64) removeCount);
        heatmapDirty = true;
    }
}

void StreamScatterView::refreshFromProcessor()
{
    appendPeaksFromProcessor();
    repaint();
}

void StreamScatterView::clearHistory()
{
    resetSweepState();
    repaint();
}

void StreamScatterView::invalidateHeatmap()
{
    heatmapDirty = true;
    repaint();
}

void StreamScatterView::paint (Graphics& g)
{
    g.fillAll (findColour (ThemeColours::componentParentBackground));

    Rectangle<int> plotBounds = getLocalBounds().reduced (16);
    g.setColour (Colours::darkgrey);
    g.drawRect (plotBounds);

    if (processor == nullptr || owner == nullptr || plotBounds.getWidth() <= 1 || plotBounds.getHeight() <= 1)
        return;

    const double sampleRate = processor->getSampleRateForStream (streamId);
    const int numChannels = processor->getNumChannelsForStream (streamId);
    const int64 windowSamples = (int64) (sampleRate * owner->getDisplayWindowSeconds());
    if (sampleRate <= 0.0 || numChannels <= 0 || windowSamples <= 0)
        return;

    if (! heatmapImage.isValid()
        || plotBounds.getWidth() != cachedPlotWidth
        || plotBounds.getHeight() != cachedPlotHeight
        || windowSamples != cachedWindowSamples)
    {
        heatmapDirty = true;
    }

    if (heatmapDirty)
        rebuildHeatmapImage (plotBounds, windowSamples, numChannels);

    if (heatmapImage.isValid())
        g.drawImageAt (heatmapImage, plotBounds.getX(), plotBounds.getY(), false);
}

OptionsBar::OptionsBar (DriftMapCanvas* canvas_, DriftMap* processor_)
    : canvas (canvas_),
      processor (processor_),
      ParameterEditorOwner (this)
{
    clearButton = std::make_unique<UtilityButton> ("CLEAR");
    clearButton->addListener (this);
    clearButton->setRadius (3.0f);
    clearButton->setClickingTogglesState (false);
    addAndMakeVisible (clearButton.get());

    auto* thresholdEditor = new TextBoxParameterEditor (processor->getParameter ("threshold_uv"), 25, 210);
    thresholdEditor->setLayout (ParameterEditor::Layout::nameOnLeft);
    addParameterEditor (thresholdEditor, 20, 12);

    auto* refractoryEditor = new TextBoxParameterEditor (processor->getParameter ("refractory_ms"), 25, 200);
    refractoryEditor->setLayout (ParameterEditor::Layout::nameOnLeft);
    addParameterEditor (refractoryEditor, 250, 12);

    auto* windowEditor = new ComboBoxParameterEditor (canvas->getParameter ("display_window_s"), 25, 200);
    windowEditor->setLayout (ParameterEditor::Layout::nameOnLeft);
    addParameterEditor (windowEditor, 470, 12);

    auto* maxAmpEditor = new ComboBoxParameterEditor (canvas->getParameter ("max_amplitude_uv"), 25, 180);
    maxAmpEditor->setLayout (ParameterEditor::Layout::nameOnLeft);
    addParameterEditor (maxAmpEditor, 640, 12);

    auto* colorMapEditor = new ComboBoxParameterEditor (canvas->getParameter ("color_map"), 25, 170);
    colorMapEditor->setLayout (ParameterEditor::Layout::nameOnLeft);
    addParameterEditor (colorMapEditor, 810, 12);
}

void OptionsBar::buttonClicked (Button* button)
{
    if (button == clearButton.get())
    {
        if (processor != nullptr)
            processor->clearDriftData();

        if (canvas != nullptr)
            canvas->clearAllViews();
    }
}

void OptionsBar::resized()
{
    clearButton->setBounds (getWidth() - 100, 12, 70, 25);
}

void OptionsBar::paint (Graphics& g)
{
    g.fillAll (findColour (ThemeColours::componentBackground));
}

DriftMapCanvas::DriftMapCanvas (DriftMap* processor_)
    : Visualizer (processor_),
      processor (processor_)
{
    refreshRate = 10;

    Array<String> windows;
    windows.add ("10");
    windows.add ("30");
    windows.add ("60");
    windows.add ("300");

    Array<String> colorMaps;
    colorMaps.add ("Greys");
    colorMaps.add ("Cividis");
    colorMaps.add ("Viridis");
    colorMaps.add ("RdGy");
    colorMaps.add ("RdBu");

    Array<String> maxAmplitudes;
    maxAmplitudes.add ("50");
    maxAmplitudes.add ("100");
    maxAmplitudes.add ("250");
    maxAmplitudes.add ("500");
    maxAmplitudes.add ("1000");
    maxAmplitudes.add ("2000");

    addCategoricalParameter ("display_window_s",
                             "Display Window (s)",
                             "Visible time range in seconds",
                             windows,
                             2);

    addCategoricalParameter ("color_map",
                             "Color Map",
                             "Color map for drift heatmap amplitudes",
                             colorMaps,
                             1);

    addCategoricalParameter ("max_amplitude_uv",
                             "Max Amplitude (uV)",
                             "Fixed amplitude scale for heatmap coloring",
                             maxAmplitudes,
                             2);

    ColorMap::setColorMap (ColorMapId::CIVIDIS);

    optionsBar = new OptionsBar (this, processor);
    addParameterEditorOwner (optionsBar);

    streamTabs = std::make_unique<TabbedComponent> (TabbedButtonBar::TabsAtTop);
    addAndMakeVisible (streamTabs.get());

    rebuildTabs();
}

void DriftMapCanvas::resized()
{
    const int optionsHeight = 50;
    optionsBar->setBounds (0, getHeight() - optionsHeight, getWidth(), optionsHeight);
    streamTabs->setBounds (0, 0, getWidth(), getHeight() - optionsHeight);
}

void DriftMapCanvas::paint (Graphics& g)
{
    g.fillAll (findColour (ThemeColours::componentParentBackground));
}

void DriftMapCanvas::refresh()
{
    auto* view = getCurrentView();
    if (view != nullptr)
        view->refreshFromProcessor();
}

void DriftMapCanvas::parameterValueChanged (Parameter* param)
{
    if (param->getName().equalsIgnoreCase ("color_map"))
    {
        const int colorMapIndex = (int) param->getValue() + 1;
        ColorMap::setColorMap ((ColorMapId) colorMapIndex);
    }

    if (param->getName().equalsIgnoreCase ("display_window_s")
        || param->getName().equalsIgnoreCase ("color_map")
        || param->getName().equalsIgnoreCase ("max_amplitude_uv"))
    {
        for (int i = 0; i < streamTabs->getNumTabs(); ++i)
        {
            auto* view = dynamic_cast<StreamScatterView*> (streamTabs->getTabContentComponent (i));
            if (view != nullptr)
            {
                if (param->getName().equalsIgnoreCase ("display_window_s"))
                    view->clearHistory();
                else
                    view->invalidateHeatmap();
                view->refreshFromProcessor();
            }
        }
    }
}

void DriftMapCanvas::updateSettings()
{
    rebuildTabs();
}

void DriftMapCanvas::saveCustomParametersToXml (XmlElement* xml)
{
}

void DriftMapCanvas::loadCustomParametersFromXml (XmlElement* xml)
{
}

int DriftMapCanvas::getDisplayWindowSeconds() const
{
    auto* windowParam = getParameter ("display_window_s");
    if (windowParam == nullptr)
        return 60;

    const int windowSeconds = windowParam->getValueAsString().getIntValue();
    return windowSeconds > 0 ? windowSeconds : 60;
}

int DriftMapCanvas::getMaxAmplitudeUv() const
{
    auto* maxAmpParam = getParameter ("max_amplitude_uv");
    if (maxAmpParam == nullptr)
        return 250;

    const int maxAmpUv = maxAmpParam->getValueAsString().getIntValue();
    return maxAmpUv > 0 ? maxAmpUv : 250;
}

void DriftMapCanvas::clearAllViews()
{
    for (int i = 0; i < streamTabs->getNumTabs(); ++i)
    {
        auto* view = dynamic_cast<StreamScatterView*> (streamTabs->getTabContentComponent (i));
        if (view != nullptr)
            view->clearHistory();
    }
}

void DriftMapCanvas::rebuildTabs()
{
    streamTabs->clearTabs();

    auto streams = processor->getDataStreams();
    for (auto stream : streams)
    {
        const uint16 streamId = stream->getStreamId();
        String label = stream->getName();
        if (label.isEmpty())
            label = "Stream";
        label << " (" << String (stream->getSourceNodeId()) << ")";

        auto* view = new StreamScatterView (processor, this, streamId);
        streamTabs->addTab (label, findColour (ThemeColours::componentParentBackground), view, true);
    }
}

StreamScatterView* DriftMapCanvas::getCurrentView() const
{
    if (streamTabs == nullptr)
        return nullptr;

    return dynamic_cast<StreamScatterView*> (streamTabs->getCurrentContentComponent());
}
