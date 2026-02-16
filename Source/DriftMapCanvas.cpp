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

// ---------------------------------------------------------------------------------------------------------------
StreamSnapshotView::StreamSnapshotView (DriftMap* processor_, DriftMapCanvas* owner_, uint16 streamId_)
    : processor (processor_), owner (owner_), streamId (streamId_)
{
    if (processor != nullptr)
        processor->addChangeListener (this);

    updateImageFromSnapshot();
}

StreamSnapshotView::~StreamSnapshotView()
{
    if (processor != nullptr)
        processor->removeChangeListener (this);
}

void StreamSnapshotView::changeListenerCallback (ChangeBroadcaster* source)
{

    LOGD ("Change received in StreamSnapshotView for stream ", streamId);

    if (processor == nullptr)
        return;

    if (! processor->isSnapshotReady (streamId))
        return;

    LOGD ("Updating image from snapshot for stream ", streamId);

    updateImageFromSnapshot();
    repaint();
}

void StreamSnapshotView::updateImageFromSnapshot()
{
    if (processor == nullptr || owner == nullptr)
        return;

    AudioBuffer<float>* snapshot = processor->getSnapshot (streamId);
    if (snapshot == nullptr || snapshot->getNumSamples() <= 0 || snapshot->getNumChannels() <= 0)
    {
        image = Image (Image::RGB, 1, 1, true, SoftwareImageType());
        return;
    }

    if (image.getWidth() != snapshot->getNumSamples() || image.getHeight() != snapshot->getNumChannels())
    {
        image = Image (Image::RGB, snapshot->getNumSamples(), snapshot->getNumChannels(), true, SoftwareImageType());
    }

    const int numChannels = snapshot->getNumChannels();
    const float range = owner->range;

    for (int i = 0; i < numChannels; i++)
    {
        for (int j = 0; j < snapshot->getNumSamples(); j++)
        {
            float value = snapshot->getSample (i, j);
            value = (value + range) / (2 * range);

            Colour colour = ColorMap::getColorForNormalizedValue (value);

            image.setPixelAt (j, numChannels - i - 1, colour);
        }
    }
}

void StreamSnapshotView::paint (Graphics& g)
{
    g.fillAll (findColour (ThemeColours::componentParentBackground));

    if (image.isValid())
    {
        g.drawImageWithin (image, 20, 20, getWidth() - 40, getHeight() - 40, RectanglePlacement::stretchToFit, false);
    }
}

void StreamSnapshotView::saveImage (File& file)
{
    FileOutputStream stream (file);
    PNGImageFormat pngWriter;
    pngWriter.writeImageToStream (image, stream);
}

void StreamSnapshotView::refreshFromSnapshot()
{
    updateImageFromSnapshot();
    repaint();
}

OptionsBar::OptionsBar (DriftMapCanvas* canvas_, DriftMap* processor)
    : canvas (canvas_), ParameterEditorOwner (this)
{
    saveButton = std::make_unique<UtilityButton> ("SAVE");
    saveButton->addListener (this);
    saveButton->setRadius (3.0f);
    saveButton->setClickingTogglesState (false);
    addAndMakeVisible (saveButton.get());

    ComboBoxParameterEditor* rangeSelector = new ComboBoxParameterEditor (canvas->getParameter ("voltage_range"), 25, 220);
    rangeSelector->setLayout (ParameterEditor::Layout::nameOnLeft);
    addParameterEditor (rangeSelector, 20, 12);

    TextBoxParameterEditor* pEditor = new TextBoxParameterEditor (processor->getParameter ("window"), 25, 160);
    pEditor->setLayout (ParameterEditor::Layout::nameOnLeft);
    addParameterEditor (pEditor, 260, 12);

    ComboBoxParameterEditor* colorMapEditor = new ComboBoxParameterEditor (canvas->getParameter ("color_map"), 25, 160);
    colorMapEditor->setLayout (ParameterEditor::Layout::nameOnLeft);
    addParameterEditor (colorMapEditor, 450, 12);
}

void OptionsBar::buttonClicked (Button* button)
{
    if (button == saveButton.get())
    {
        FileChooser chooser ("Save snapshot to file...",
                             File(),
                             "*.png");

        if (chooser.browseForFileToSave (true))
        {
            File file = chooser.getResult();

            if (file.exists())
                file.deleteFile();

            canvas->saveImage (file);
        }
    }
}

void OptionsBar::resized()
{
    saveButton->setBounds (getWidth() - 100, 12, 70, 25);
}

void OptionsBar::paint (Graphics& g)
{
    g.fillAll (findColour (ThemeColours::componentBackground));
}

// ---------------------------------------------------------------------------------------------------------------
DriftMapCanvas::DriftMapCanvas (DriftMap* processor_)
    : Visualizer (processor_),
      processor (processor_)
{

    Array<String> colorMaps;
    colorMaps.add ("Greys");
    colorMaps.add ("Cividis");
    colorMaps.add ("Viridis");
    colorMaps.add ("RdGy");
    colorMaps.add ("RdBu");
    addCategoricalParameter ("color_map", "Color Map", "Color map for drift map", colorMaps, 0);

    Array<String> ranges;
    ranges.add ("+/- 25 uV");
    ranges.add ("+/- 50 uV");
    ranges.add ("+/- 75 uV");
    ranges.add ("+/- 100 uV");
    ranges.add ("+/- 250 uV");
    ranges.add ("+/- 500 uV");
    ranges.add ("+/- 1000 uV");
    ranges.add ("+/- 2000 uV");

    for (auto vRange : ranges)
    {
        voltageRanges[vRange] = vRange.substring (4, vRange.length() - 3).getIntValue();
    }

    addCategoricalParameter ("voltage_range", "Voltage Range", "Voltage Range for drift map", ranges, 1);

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

void DriftMapCanvas::setRange (int rangeMicrovolts)
{
    range = (float) rangeMicrovolts;
}

void DriftMapCanvas::parameterValueChanged (Parameter* param)
{
    //LOGD("Changing parameter: ", param->getName());

    if (param->getName().equalsIgnoreCase ("color_map"))
    {
        int colormapIndex = (int) param->getValue() + 1;
        ColorMap::setColorMap ((ColorMapId) colormapIndex);
    }
    else if (param->getName().equalsIgnoreCase ("voltage_range"))
    {
        String rangeValue = param->getValueAsString();
        range = voltageRanges[rangeValue];
    }

    for (int i = 0; i < streamTabs->getNumTabs(); i++)
    {
        auto* view = dynamic_cast<StreamSnapshotView*> (streamTabs->getTabContentComponent (i));
        if (view != nullptr)
        {
            view->refreshFromSnapshot();
        }
    }
}

void DriftMapCanvas::setColorMap (int colormapIndex)
{
    // set colormap
    ColorMap::setColorMap ((ColorMapId) colormapIndex);
}

void DriftMapCanvas::saveImage (File& file)
{
    auto* view = getCurrentView();
    if (view != nullptr)
        view->saveImage (file);
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

        auto* view = new StreamSnapshotView (processor, this, streamId);
        streamTabs->addTab (label, findColour (ThemeColours::componentParentBackground), view, true);
    }
}

StreamSnapshotView* DriftMapCanvas::getCurrentView() const
{
    if (streamTabs == nullptr)
        return nullptr;

    return dynamic_cast<StreamSnapshotView*> (streamTabs->getCurrentContentComponent());
}
