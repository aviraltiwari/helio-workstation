/*
    This file is part of Helio Workstation.

    Helio is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Helio is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Helio. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

class Lasso;
class ProjectTreeItem;

#include "CommandPanel.h"

class PianoRollSelectionCommandPanel : public CommandPanel
{
public:
    
    PianoRollSelectionCommandPanel(WeakReference<Lasso> lasso, const ProjectTreeItem &project);
    void handleCommandMessage(int commandId) override;
    
private:

    void dismiss() const;

    WeakReference<Lasso> lasso;
    const ProjectTreeItem &project;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollSelectionCommandPanel)
};
