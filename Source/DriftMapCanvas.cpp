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

#include "DriftMapCanvas.h"

#include "DriftMap.h"

#include <algorithm>
#include <cmath>

StreamScatterView::StreamScatterView (DriftMap* processor_, DriftMapCanvas* owner_, uint16 streamId_)
    : processor (processor_), owner (owner_), streamId (streamId_)
{
}

void DriftMapCanvas::refreshTabColours()
{
    if (streamTabs == nullptr)
        return;

    const Colour tabColour = findColour (ThemeColours::componentParentBackground);
    for (int i = 0; i < streamTabs->getNumTabs(); ++i)
        streamTabs->setTabBackgroundColour (i, tabColour);
}

void StreamScatterView::resetSweepState()
{
    sessionStartTimeMinutes = -1.0;
    minutesPerPixel = 0.0;
    latestDrawnX = -1;
    droppedSourcePixels = 0;
    viewStartX = 0.0;
    viewZoom = 1.0;
    followLatest = true;
    isPanning = false;
    lastDragX = 0;
    cachedPlotWidth = 0;
    cachedPlotHeight = 0;
}

void StreamScatterView::ensureSessionImage (Rectangle<int> plotBounds)
{
    if (plotBounds.getWidth() <= 1 || plotBounds.getHeight() <= 1)
        return;

    cachedPlotWidth = plotBounds.getWidth();
    cachedPlotHeight = plotBounds.getHeight();

    if (sessionImage.isValid())
        return;

    const int initialWidth = jmax (4096 * 4, plotBounds.getWidth() * 12);
    const int initialHeight = jmax (1024, plotBounds.getHeight() * 2);

    sessionImage = Image (Image::ARGB, initialWidth, initialHeight, true, SoftwareImageType());
    sessionImage.clear (sessionImage.getBounds(), clearColour);
    invertedSessionImage = Image();
    invertedDirty = true;
}

void StreamScatterView::extendSessionImageWidth (int requiredX)
{
    if (! sessionImage.isValid())
        return;
    const int maxWidth = getMaxSessionImageWidth();

    if (maxWidth <= 1)
        return;

    int targetWidth = sessionImage.getWidth();
    while (requiredX >= targetWidth && targetWidth < maxWidth)
        targetWidth = jmin (targetWidth * 2, maxWidth);

    if (targetWidth != sessionImage.getWidth())
    {
        Image grown (Image::ARGB, targetWidth, sessionImage.getHeight(), true, SoftwareImageType());
        grown.clear (grown.getBounds(), clearColour);

        Graphics g (grown);
        g.drawImageAt (sessionImage, 0, 0, false);
        sessionImage = std::move (grown);
        invertedSessionImage = Image();
        invertedDirty = true;
    }

    if (requiredX < sessionImage.getWidth())
        return;

    const int shiftPixels = requiredX - (sessionImage.getWidth() - 1);
    if (shiftPixels <= 0)
        return;

    Image shifted (Image::ARGB, sessionImage.getWidth(), sessionImage.getHeight(), true, SoftwareImageType());
    shifted.clear (shifted.getBounds(), clearColour);

    Graphics g (shifted);
    g.drawImageAt (sessionImage, -shiftPixels, 0, false);
    sessionImage = std::move (shifted);
    invertedSessionImage = Image();
    invertedDirty = true;

    droppedSourcePixels += (int64) shiftPixels;
    latestDrawnX = jmax (-1, latestDrawnX - shiftPixels);
    viewStartX = jmax (0.0, viewStartX - (double) shiftPixels);

}

int StreamScatterView::getMaxSessionImageWidth() const
{
    if (owner == nullptr || minutesPerPixel <= 0.0)
        return 4096 * 4;
    const int baseVisibleWidth = jmax (1, (int) std::round (owner->getDisplayWindowMinutes() / minutesPerPixel));
    return jmax (baseVisibleWidth + 1, baseVisibleWidth * 50);
}

void StreamScatterView::drawPeakOnSessionImage (const DriftMap::PeakEvent& peak, int numChannels)
{
    if (! sessionImage.isValid() || numChannels <= 0 || minutesPerPixel <= 0.0)
        return;
    const double peakTimeMinutes = peak.timestamp / 60.0;
    if (sessionStartTimeMinutes < 0.0)
        sessionStartTimeMinutes = peakTimeMinutes;
    const int absoluteX = (int) std::floor ((peakTimeMinutes - sessionStartTimeMinutes) / minutesPerPixel);
    if (absoluteX < 0)
        return;
    int x = absoluteX - (int) droppedSourcePixels;
    if (x < 0)
        return;

    extendSessionImageWidth (x);
    x = absoluteX - (int) droppedSourcePixels;
    if (x < 0 || x >= sessionImage.getWidth())
        return;

    int yPixel = sessionImage.getHeight() - 1;
    if (numChannels > 1)
    {
        const int displayChannel = jlimit (0,
                                           numChannels - 1,
                                           processor->getDisplayChannelForStream (streamId, (int) peak.channel));
        const double yNorm = (double) displayChannel / (double) (numChannels - 1);
        yPixel = sessionImage.getHeight() - 1 - (int) (yNorm * (sessionImage.getHeight() - 1));
    }

    if (yPixel < 0 || yPixel >= sessionImage.getHeight())
        return;

    constexpr float maxAmpUv = 150.0f;
    float thresholdUv = 50.0f;

    const float normalized = jlimit (0.0f,
                                     1.0f,
                                     (peak.amplitude - thresholdUv) / jmax (1.0f, maxAmpUv - thresholdUv));

    constexpr float gamma = 0.7f;
    constexpr int minIntensity = 45;
    const float shaped = std::pow (normalized, gamma);
    const uint8 intensity = (uint8) jlimit (0,
                                            255,
                                            minIntensity + (int) std::round (shaped * (255 - minIntensity)));
    const Colour pointColour (intensity, intensity, intensity, (uint8) 100);

    const float circleDiameter = jlimit (2.0f, 14.0f, (float) sessionImage.getHeight() * 0.006f);
    const float radius = circleDiameter * 0.5f;

    Graphics imageGraphics (sessionImage);
    imageGraphics.setColour (pointColour);
    imageGraphics.fillEllipse ((float) x - radius,
                               (float) yPixel - radius,
                               circleDiameter / 2.0f,
                               circleDiameter);

    latestDrawnX = jmax (latestDrawnX, x);
    invertedDirty = true;
}

void StreamScatterView::appendPeaksFromProcessor()
{
    if (processor == nullptr || owner == nullptr)
        return;

    const int numChannels = processor->getNumChannelsForStream (streamId);
    if (numChannels <= 0)
        return;

    Rectangle<int> plotBounds = getLocalBounds().reduced (16);
    ensureSessionImage (plotBounds);
    if (! sessionImage.isValid())
        return;

    if (minutesPerPixel <= 0.0)
        minutesPerPixel = owner->getDisplayWindowMinutes() / (double) jmax (1, cachedPlotWidth);

    bool wasAtLatestEdge = false;
    if (! followLatest && latestDrawnX >= 0)
    {
        const double baseVisibleWidth = owner->getDisplayWindowMinutes() / minutesPerPixel;
        const int visibleSourceWidth = jmax (1, (int) std::round (baseVisibleWidth / viewZoom));
        const int maxStartBefore = jmax (0, latestDrawnX - visibleSourceWidth + 1);
        wasAtLatestEdge = std::abs (viewStartX - (double) maxStartBefore) <= 1.0;
    }

    std::vector<DriftMap::PeakEvent> newPeaks;
    if (! processor->drainPeaks (streamId, newPeaks) || newPeaks.empty())
        return;

    std::sort (newPeaks.begin(),
               newPeaks.end(),
               [] (const DriftMap::PeakEvent& a, const DriftMap::PeakEvent& b) { return a.timestamp < b.timestamp; });

    for (const auto& peak : newPeaks)
        drawPeakOnSessionImage (peak, numChannels);

    if (followLatest || wasAtLatestEdge)
        followLatest = true;

    if (followLatest)
        resetViewToLatest();
}

void StreamScatterView::refreshFromProcessor()
{
    updateFromProcessor (true);
}

void StreamScatterView::updateFromProcessor (bool shouldRepaint)
{
    appendPeaksFromProcessor();
    if (shouldRepaint)
        repaint();
}

void StreamScatterView::clearHistory()
{
    if (sessionImage.isValid())
    {
        sessionImage.clear (sessionImage.getBounds(), clearColour);
        invertedSessionImage = Image();
        invertedDirty = true;
    }

    resetSweepState();
    repaint();
}

void StreamScatterView::setLightMode (bool enabled)
{
    lightModeEnabled = enabled;
    repaint();
}

void StreamScatterView::setTimebaseMinutes (double timebaseMinutes)
{
    if (! sessionImage.isValid() || minutesPerPixel <= 0.0 || timebaseMinutes <= 0.0)
        return;
    if (latestDrawnX < 0)
        return;

    viewZoom = 1.0;
    followLatest = true;
    resetViewToLatest();

    repaint();
}

void StreamScatterView::updateThemeCacheIfNeeded()
{
    if (! lightModeEnabled || ! sessionImage.isValid())
        return;

    if (! invertedDirty
        && invertedSessionImage.isValid()
        && invertedSessionImage.getWidth() == sessionImage.getWidth()
        && invertedSessionImage.getHeight() == sessionImage.getHeight())
    {
        return;
    }

    invertedSessionImage = Image (Image::ARGB,
                                  sessionImage.getWidth(),
                                  sessionImage.getHeight(),
                                  true,
                                  SoftwareImageType());

    Image::BitmapData src (sessionImage, Image::BitmapData::readOnly);
    Image::BitmapData dst (invertedSessionImage, Image::BitmapData::writeOnly);

    for (int y = 0; y < src.height; ++y)
    {
        const uint8* srcLine = src.getLinePointer (y);
        uint8* dstLine = dst.getLinePointer (y);

        for (int x = 0; x < src.width; ++x)
        {
            const int offset = x * 4;
            dstLine[offset + 0] = (uint8) (255 - srcLine[offset + 0]);
            dstLine[offset + 1] = (uint8) (255 - srcLine[offset + 1]);
            dstLine[offset + 2] = (uint8) (255 - srcLine[offset + 2]);
            dstLine[offset + 3] = srcLine[offset + 3];
        }
    }

    invertedDirty = false;
}

void StreamScatterView::resetViewToLatest()
{
    if (! sessionImage.isValid() || latestDrawnX < 0)
        return;
    const double baseVisibleWidth = owner->getDisplayWindowMinutes() / minutesPerPixel;
    const int visibleSourceWidth = jmax (1, (int) std::round (baseVisibleWidth / viewZoom));
    const int maxStart = jmax (0, latestDrawnX - visibleSourceWidth + 1);
    viewStartX = (double) maxStart;
}

void StreamScatterView::mouseDown (const MouseEvent& event)
{
    auto contentBounds = getLocalBounds().reduced (16);
    Rectangle<int> plotBounds = contentBounds.withTrimmedBottom (22);
    if (! plotBounds.contains (event.getPosition()))
        return;

    isPanning = true;
    followLatest = false;
    lastDragX = event.x;
}

void StreamScatterView::mouseDrag (const MouseEvent& event)
{
    if (! isPanning || ! sessionImage.isValid() || cachedPlotWidth <= 1 || latestDrawnX < 0)
        return;

    const int dx = event.x - lastDragX;
    lastDragX = event.x;

    const double baseVisibleWidth = owner->getDisplayWindowMinutes() / minutesPerPixel;
    const double visibleSourceWidth = jmax (1.0, baseVisibleWidth / viewZoom);
    const int maxStart = jmax (0, latestDrawnX - (int) std::round (visibleSourceWidth) + 1);
    const double sourceDelta = -((double) dx * (visibleSourceWidth / (double) cachedPlotWidth));
    viewStartX = jlimit (0.0,
                         (double) maxStart,
                         viewStartX + sourceDelta);
    followLatest = std::abs (viewStartX - (double) maxStart) <= 1.0;

    repaint();
}

void StreamScatterView::mouseUp (const MouseEvent& event)
{
    isPanning = false;
}

void StreamScatterView::mouseWheelMove (const MouseEvent& event, const MouseWheelDetails& wheel)
{
    if (! sessionImage.isValid() || cachedPlotWidth <= 1 || latestDrawnX < 0 || std::abs (wheel.deltaY) < 0.0001f)
        return;
    auto contentBounds = getLocalBounds().reduced (16);
    Rectangle<int> plotBounds = contentBounds.withTrimmedBottom (22);
    if (! plotBounds.contains (event.getPosition()))
        return;

    followLatest = false;

    const double baseVisibleWidth = owner->getDisplayWindowMinutes() / minutesPerPixel;
    const double oldVisibleWidth = jmax (1.0, baseVisibleWidth / viewZoom);
    const double zoomFactor = wheel.deltaY > 0.0f ? 1.15 : (1.0 / 1.15);
    viewZoom = jlimit (1.0, 64.0, viewZoom * zoomFactor);
    const double newVisibleWidth = jmax (1.0, baseVisibleWidth / viewZoom);

    const double u = jlimit (0.0, 1.0, (double) (event.x - plotBounds.getX()) / (double) jmax (1, plotBounds.getWidth()));
    const double anchorX = viewStartX + u * oldVisibleWidth;
    viewStartX = anchorX - u * newVisibleWidth;
    const int maxStart = jmax (0, latestDrawnX - (int) std::round (newVisibleWidth) + 1);
    viewStartX = jlimit (0.0, (double) maxStart, viewStartX);
    followLatest = std::abs (viewStartX - (double) maxStart) <= 1.0;

    repaint();
}

void StreamScatterView::mouseDoubleClick (const MouseEvent& event)
{
    followLatest = true;
    resetViewToLatest();
    repaint();
}

void StreamScatterView::paint (Graphics& g)
{
    g.fillAll (lightModeEnabled ? Colour (200, 200, 200) : Colour (50, 50, 50));
    auto contentBounds = getLocalBounds().reduced (16);
    Rectangle<int> timelineBounds = contentBounds.removeFromBottom (20);
    Rectangle<int> plotBounds = contentBounds;
    g.setColour (lightModeEnabled ? Colours::grey : Colours::darkgrey);
    g.drawRect (plotBounds);

    if (processor == nullptr || owner == nullptr || plotBounds.getWidth() <= 1 || plotBounds.getHeight() <= 1)
        return;

    ensureSessionImage (plotBounds);
    if (! sessionImage.isValid())
        return;

    cachedPlotWidth = plotBounds.getWidth();
    cachedPlotHeight = plotBounds.getHeight();

    if (followLatest)
        resetViewToLatest();
    const double baseVisibleWidth = owner->getDisplayWindowMinutes() / minutesPerPixel;
    const int visibleSourceWidth = jmax (1, (int) std::round (baseVisibleWidth / viewZoom));
    const int maxStart = (latestDrawnX >= 0) ? jmax (0, latestDrawnX - visibleSourceWidth + 1) : 0;
    const int sourceX = jlimit (0,
                                maxStart,
                                (int) std::round (viewStartX));

    const Image* sourceImage = &sessionImage;
    if (lightModeEnabled)
    {
        updateThemeCacheIfNeeded();
        if (invertedSessionImage.isValid())
            sourceImage = &invertedSessionImage;
    }

    g.drawImage (*sourceImage,
                 plotBounds.getX(),
                 plotBounds.getY(),
                 plotBounds.getWidth(),
                 plotBounds.getHeight(),
                 sourceX,
                 0,
                 visibleSourceWidth,
                 sourceImage->getHeight(),
                 false);

    g.setColour (lightModeEnabled ? Colours::black.withAlpha (0.2f) : Colours::white.withAlpha (0.2f));
    g.drawLine ((float) timelineBounds.getX(),
                (float) timelineBounds.getY(),
                (float) timelineBounds.getRight(),
                (float) timelineBounds.getY(),
                1.0f);

    const double visibleMinutes = visibleSourceWidth * minutesPerPixel;
    if (visibleMinutes <= 0.0)
        return;
    const double minutesAtLeft = (double) (sourceX + (int) droppedSourcePixels) * minutesPerPixel;
    const double minutesAtRight = minutesAtLeft + visibleMinutes;

    g.setFont (FontOptions().withHeight (13.0f));
    const double roughStep = visibleMinutes / 6.0;
    const double decade = std::pow (10.0, std::floor (std::log10 (jmax (roughStep, 1.0e-9))));
    const double normalized = roughStep / decade;
    double stepNorm = 1.0;
    if (normalized > 5.0)
        stepNorm = 10.0;
    else if (normalized > 2.0)
        stepNorm = 5.0;
    else if (normalized > 1.0)
        stepNorm = 2.0;
    const double tickStepMinutes = stepNorm * decade;

    int decimals = 0;
    if (tickStepMinutes < 1.0)
        decimals = jlimit (0, 3, (int) std::ceil (-std::log10 (tickStepMinutes)));
    const double firstTickMinutes = std::ceil (minutesAtLeft / tickStepMinutes) * tickStepMinutes;
    for (double tickMinutes = firstTickMinutes;
         tickMinutes <= minutesAtRight + (tickStepMinutes * 0.25);
         tickMinutes += tickStepMinutes)
    {
        const double t = (tickMinutes - minutesAtLeft) / visibleMinutes;
        const int x = jlimit (timelineBounds.getX(),
                              timelineBounds.getRight(),
                              timelineBounds.getX() + (int) std::round (t * (double) plotBounds.getWidth()));
        g.drawLine ((float) x, (float) timelineBounds.getY(), (float) x, (float) timelineBounds.getY() + 4.0f, 1.0f);
        String label = (decimals == 0) ? String ((int) std::round (tickMinutes))
                                       : String (tickMinutes, decimals);
        label << " min";
        constexpr int labelWidth = 60;
        const int labelX = jlimit (timelineBounds.getX(),
                                   timelineBounds.getRight() - labelWidth,
                                   x - (labelWidth / 2));

        g.drawText (label,
                    labelX,
                    timelineBounds.getY() + 5,
                    labelWidth,
                    timelineBounds.getHeight() - 5,
                    Justification::centredTop,
                    false);
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

    auto* windowEditor = new ComboBoxParameterEditor (canvas->getParameter ("display_window_min"), 25, 200);
    windowEditor->setLayout (ParameterEditor::Layout::nameOnLeft);
    addParameterEditor (windowEditor, 465, 12);

    auto* themeEditor = new ComboBoxParameterEditor (canvas->getParameter ("theme_mode"), 25, 170);
    themeEditor->setLayout (ParameterEditor::Layout::nameOnLeft);
    addParameterEditor (themeEditor, 660, 12);
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
    windows.add ("1");
    windows.add ("2");
    windows.add ("5");
    windows.add ("10");
    windows.add ("15");
    windows.add ("30");
    windows.add ("60");

    Array<String> themes;
    themes.add ("Dark");
    themes.add ("Light");

    addCategoricalParameter ("display_window_min",
                             "Timebase (min)",
                             "Visible time range in minutes",
                             windows,
                             2);

    addCategoricalParameter ("theme_mode",
                             "Theme",
                             "Display theme for drift image",
                             themes,
                             0);

    streamTabs = std::make_unique<TabbedComponent> (TabbedButtonBar::TabsAtTop);
    addAndMakeVisible (streamTabs.get());
    optionsViewport = std::make_unique<Viewport>();
    optionsViewport->setScrollBarsShown (false, true);
    optionsViewport->setScrollOnDragEnabled (true);
    addAndMakeVisible (optionsViewport.get());
    optionsBar = std::make_unique<OptionsBar> (this, processor);
    optionsViewport->setViewedComponent (optionsBar.get(), false);

    rebuildTabs();
}

void DriftMapCanvas::resized()
{
    const int optionsHeight = 50;
    const int minOptionsWidth = 980;
    streamTabs->setBounds (0, 0, getWidth(), getHeight() - optionsHeight);
    if (optionsViewport != nullptr && optionsBar != nullptr)
    {
        optionsViewport->setBounds (0, getHeight() - optionsHeight, getWidth(), optionsHeight);
        optionsBar->setBounds (0, 0, jmax (minOptionsWidth, optionsViewport->getWidth()), optionsHeight);

        optionsViewport->toFront (false);
    }
}

void DriftMapCanvas::paint (Graphics& g)
{
    g.fillAll (findColour (ThemeColours::componentParentBackground));
}
void DriftMapCanvas::lookAndFeelChanged()
{
    refreshTabColours();
    repaint();
}

void DriftMapCanvas::refresh()
{
    if (streamTabs == nullptr)
        return;

    const int currentTabIndex = streamTabs->getCurrentTabIndex();
    for (int i = 0; i < streamTabs->getNumTabs(); ++i)
    {
        auto* view = dynamic_cast<StreamScatterView*> (streamTabs->getTabContentComponent (i));
        if (view == nullptr)
            continue;

        view->updateFromProcessor (i == currentTabIndex);
    }
}

void DriftMapCanvas::parameterValueChanged (Parameter* param)
{
    const bool isThemeChange = param->getName().equalsIgnoreCase ("theme_mode");
    const bool isWindowChange = param->getName().equalsIgnoreCase ("display_window_min");

    if (! isThemeChange && ! isWindowChange)
        return;

    const bool lightMode = isThemeChange && ((int) param->getValue() == 1);
    const double timebaseMinutes = getDisplayWindowMinutes();

    for (int i = 0; i < streamTabs->getNumTabs(); ++i)
    {
        auto* view = dynamic_cast<StreamScatterView*> (streamTabs->getTabContentComponent (i));
        if (view == nullptr)
            continue;

        if (isThemeChange)
            view->setLightMode (lightMode);
        if (isWindowChange)
            view->setTimebaseMinutes (timebaseMinutes);

        view->refreshFromProcessor();
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

double DriftMapCanvas::getDisplayWindowMinutes() const
{
    auto* windowParam = getParameter ("display_window_min");
    if (windowParam == nullptr)
        return 1.0;
    const double windowMinutes = windowParam->getValueAsString().getDoubleValue();
    return jmax (0.25, windowMinutes);
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
    uint16 previouslySelectedStreamId = 0;
    bool hadSelectedStream = false;
    if (auto* currentView = getCurrentView())
    {
        previouslySelectedStreamId = currentView->getStreamId();
        hadSelectedStream = true;
    }
    streamTabs->clearTabs();
    int tabIndexToSelect = 0;
    int createdTabIndex = 0;

    auto streams = processor->getDataStreams();
    for (auto stream : streams)
    {
        const uint16 streamId = stream->getStreamId();
        String label = stream->getName();
        if (label.isEmpty())
            label = "Stream";
        label << " (" << String (stream->getSourceNodeId()) << ")";

        auto* view = new StreamScatterView (processor, this, streamId);
        auto* themeParam = getParameter ("theme_mode");
        if (themeParam != nullptr)
            view->setLightMode ((int) themeParam->getValue() == 1);
        streamTabs->addTab (label, findColour (ThemeColours::componentParentBackground), view, true);

        if (hadSelectedStream && streamId == previouslySelectedStreamId)
            tabIndexToSelect = createdTabIndex;

        ++createdTabIndex;
    }

    if (streamTabs->getNumTabs() > 0)
        streamTabs->setCurrentTabIndex (jlimit (0, streamTabs->getNumTabs() - 1, tabIndexToSelect));
    refreshTabColours();
}

StreamScatterView* DriftMapCanvas::getCurrentView() const
{
    if (streamTabs == nullptr)
        return nullptr;

    return dynamic_cast<StreamScatterView*> (streamTabs->getCurrentContentComponent());
}
