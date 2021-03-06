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

//[Headers]
#include "IconButton.h"
#include "CommandPanel.h"
//[/Headers]

#include "../Themes/PanelBackgroundC.h"
#include "../Themes/LighterShadowDownwards.h"
#include "../Themes/SeparatorHorizontalReversed.h"

class MobileComboBox  : public Component
{
public:

    MobileComboBox (WeakReference<Component> editor);

    ~MobileComboBox();

    //[UserMethods]

    void initMenu(CommandPanel::Items menu);
    void initText(TextEditor *editor);

    class Trigger final : public IconButton
    {
    public:
        Trigger(WeakReference<Component> listener = nullptr);
        void parentHierarchyChanged() override;
        void parentSizeChanged() override;
        void updateBounds();
    };

    class Primer final : public Component
    {
    public:
        Primer();
        ~Primer() override;
        void handleCommandMessage(int commandId) override;
        void initWith(WeakReference<Component> textEditor, CommandPanel::Items menu);
        void updateMenu(CommandPanel::Items menu);
        void cleanup();
    private:
        ComponentAnimator animator;
        ScopedPointer<MobileComboBox> combo;
        ScopedPointer<MobileComboBox::Trigger> comboTrigger;
        WeakReference<Component> textEditor;
    };
    //[/UserMethods]

    void paint (Graphics& g) override;
    void resized() override;
    void parentHierarchyChanged() override;
    void parentSizeChanged() override;
    void handleCommandMessage (int commandId) override;


private:

    //[UserVariables]
    WeakReference<MobileComboBox::Primer> primer;
    WeakReference<Component> editor;
    ComponentAnimator animator;
    //[/UserVariables]

    ScopedPointer<PanelBackgroundC> background;
    ScopedPointer<CommandPanel> menu;
    ScopedPointer<MobileComboBox::Trigger> triggerButtton;
    ScopedPointer<LighterShadowDownwards> shadow;
    ScopedPointer<SeparatorHorizontalReversed> separator;
    ScopedPointer<Label> currentNameLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MobileComboBox)
};
