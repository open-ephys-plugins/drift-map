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

#include "DriftMapEditor.h"

#include "DriftMap.h"
#include "DriftMapCanvas.h"

DriftMapEditor::DriftMapEditor (GenericProcessor* p)
    : VisualizerEditor (p, "Drift Map", 180)
{

    // Snapshot button
    takeSnapshotButton = std::make_unique<UtilityButton> ("SNAP");
    takeSnapshotButton->addListener (this);
    takeSnapshotButton->setRadius (3.0f);
    takeSnapshotButton->setBounds (58, 55, 70, 30);
    addAndMakeVisible (takeSnapshotButton.get());

    ChangeBroadcaster* snap = dynamic_cast<ChangeBroadcaster*> (p);
    snap->addChangeListener (this);
}

Visualizer* DriftMapEditor::createNewCanvas()
{
    return new DriftMapCanvas ((DriftMap*) getProcessor());
    ;
}

void DriftMapEditor::buttonClicked (Button* button)
{
    if (button == takeSnapshotButton.get() && CoreServices::getAcquisitionStatus())
    {
        NotificationParameter* snapParam = (NotificationParameter*) getProcessor()->getParameter ("snap");
        snapParam->triggerNotification();
        takeSnapshotButton->setEnabledState (false);
    }
}

void DriftMapEditor::changeListenerCallback (ChangeBroadcaster* source)
{
    takeSnapshotButton->setEnabledState (true);
}
