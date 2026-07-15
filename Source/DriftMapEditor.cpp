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

#include "DriftMapEditor.h"

#include "DriftMap.h"
#include "DriftMapCanvas.h"

DriftMapEditor::DriftMapEditor (GenericProcessor* p)
    : VisualizerEditor (p, "Drift Map", 180)
{

    addTextBoxParameterEditor (Parameter::PROCESSOR_SCOPE, "threshold_uv", 15, 45);
    addBoundedValueParameterEditor (Parameter::PROCESSOR_SCOPE, "refractory_ms", 15, 85);

    for (auto* ed : parameterEditors)
    {
        ed->setBounds (ed->getX(), ed->getY(), 160, 18);
        ed->getEditor()->setSize (70, 18);
        ed->getLabel()->setBounds (72, 0, 88, 18);
    }
}

Visualizer* DriftMapEditor::createNewCanvas()
{
    return new DriftMapCanvas ((DriftMap*) getProcessor());
}
