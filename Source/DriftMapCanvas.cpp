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

#include "DriftMap.h"
#include <algorithm>

#include <array>

StreamScatterView::StreamScatterView (DriftMap* processor_, DriftMapCanvas* owner_, uint16 streamId_)
    : processor (processor_), owner (owner_), streamId (streamId_)
{
}

void StreamScatterView::appendPeaksFromProcessor()
{
    if (processor == nullptr)
        return;

    std::vector<DriftMap::PeakEvent> newPeaks;
    if (! processor->drainPeaks (streamId, newPeaks))
        return;

    if (newPeaks.empty())
        return;

    latestSample = jmax (latestSample, newPeaks.back().sampleNumber);

    const size_t oldSize = displayPeaks.size();
    displayPeaks.resize (oldSize + newPeaks.size());
    std::copy (newPeaks.begin(),
               newPeaks.end(),
               displayPeaks.begin() + (int64) oldSize);
}

void StreamScatterView::pruneHistory()
{
    if (processor == nullptr || displayPeaks.empty())
        return;

    const double sampleRate = processor->getSampleRateForStream (streamId);
    if (sampleRate <= 0.0)
        return;

    const int64 windowSamples = (int64) (sampleRate * owner->getDisplayWindowSeconds());
    if (windowSamples <= 0)
        return;

    const int64 keepAfter = jmax ((int64) 0, latestSample - (windowSamples * 2));

    auto it = std::lower_bound (displayPeaks.begin(),
                                displayPeaks.end(),
                                keepAfter,
                                [] (const DriftMap::PeakEvent& peak, int64 sample) { return peak.sampleNumber < sample; });

    if (it != displayPeaks.begin())
        displayPeaks.erase (displayPeaks.begin(), it);

    constexpr size_t maxDisplayPeaks = 1000000;
    if (displayPeaks.size() > maxDisplayPeaks)
    {
        const size_t removeCount = displayPeaks.size() - maxDisplayPeaks;
        displayPeaks.erase (displayPeaks.begin(), displayPeaks.begin() + (int64) removeCount);
    }
}

void StreamScatterView::refreshFromProcessor()
{
    appendPeaksFromProcessor();
    pruneHistory();
    repaint();
}

void StreamScatterView::clearHistory()
{
    displayPeaks.clear();
    latestSample = 0;
    repaint();
}

void StreamScatterView::paint (Graphics& g)
{
    g.fillAll (findColour (ThemeColours::componentParentBackground));

    Rectangle<int> plotBounds = getLocalBounds().reduced (16);
    g.setColour (Colours::darkgrey);
    g.drawRect (plotBounds);

    if (processor == nullptr)
        return;

    const int numChannels = processor->getNumChannelsForStream (streamId);
    const double sampleRate = processor->getSampleRateForStream (streamId);

    if (numChannels <= 0 || sampleRate <= 0.0 || displayPeaks.empty() || plotBounds.getWidth() < 2 || plotBounds.getHeight() < 2)
        return;

    const int windowSeconds = owner->getDisplayWindowSeconds();
    const int64 windowSamples = (int64) (windowSeconds * sampleRate);
    if (windowSamples <= 0)
        return;

    const int64 xMax = jmax ((int64) 1, latestSample);
    const int64 xMin = jmax ((int64) 0, xMax - windowSamples);
    const int64 xSpan = jmax ((int64) 1, xMax - xMin);

    auto startIt = std::lower_bound (displayPeaks.begin(),
                                     displayPeaks.end(),
                                     xMin,
                                     [] (const DriftMap::PeakEvent& peak, int64 sample) { return peak.sampleNumber < sample; });

    struct ColumnPoints
    {
        std::array<int, 4> y {};
        int count = 0;
    };

    std::vector<ColumnPoints> columns ((size_t) plotBounds.getWidth());

    for (auto it = startIt; it != displayPeaks.end(); ++it)
    {
        const DriftMap::PeakEvent& peak = *it;
        if (peak.sampleNumber > xMax)
            break;

        const int x = (int) (((peak.sampleNumber - xMin) * (plotBounds.getWidth() - 1)) / xSpan);
        if (x < 0 || x >= plotBounds.getWidth())
            continue;

        int yPixel = plotBounds.getBottom() - 1;
        if (numChannels > 1)
        {
            const double yNorm = (double) peak.channel / (double) (numChannels - 1);
            yPixel = plotBounds.getBottom() - 1 - (int) (yNorm * (plotBounds.getHeight() - 1));
        }

        ColumnPoints& column = columns[(size_t) x];

        bool alreadyPresent = false;
        for (int i = 0; i < column.count; ++i)
        {
            if (column.y[(size_t) i] == yPixel)
            {
                alreadyPresent = true;
                break;
            }
        }

        if (alreadyPresent)
            continue;

        if (column.count < (int) column.y.size())
        {
            column.y[(size_t) column.count++] = yPixel;
        }
    }

    g.setColour (Colours::white.withAlpha (0.85f));
    for (int x = 0; x < (int) columns.size(); ++x)
    {
        const ColumnPoints& column = columns[(size_t) x];
        for (int i = 0; i < column.count; ++i)
            g.fillRect (plotBounds.getX() + x, column.y[(size_t) i], 1, 1);
    }
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

    addCategoricalParameter ("display_window_s",
                             "Display Window (s)",
                             "Visible time range in seconds",
                             windows,
                             2);

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
    if (param->getName().equalsIgnoreCase ("display_window_s"))
    {
        for (int i = 0; i < streamTabs->getNumTabs(); ++i)
        {
            auto* view = dynamic_cast<StreamScatterView*> (streamTabs->getTabContentComponent (i));
            if (view != nullptr)
                view->repaint();
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
