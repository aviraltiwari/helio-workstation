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

#include "MidiEvent.h"

#define DEFAULT_TRIGGER_AUTOMATION_EVENT_STATE (false)

class AutomationEvent final : public MidiEvent
{
public:

    AutomationEvent() noexcept;
    AutomationEvent(const AutomationEvent &other) noexcept;
    AutomationEvent(WeakReference<MidiSequence> owner,
        const AutomationEvent &parametersToCopy) noexcept;
    explicit AutomationEvent(WeakReference<MidiSequence> owner,
        float beatVal = 0.f,
        float controllerValue = 0.f) noexcept;

    Array<MidiMessage> toMidiMessages() const override;
    
    AutomationEvent copyWithNewId() const noexcept;
    AutomationEvent withBeat(float newBeat) const noexcept;
    AutomationEvent withDeltaBeat(float deltaBeat) const noexcept;
    AutomationEvent withInvertedControllerValue() const noexcept;
    AutomationEvent withParameters(float newBeat, float newControllerValue) const noexcept;
    AutomationEvent withCurvature(float newCurvature) const noexcept;
    AutomationEvent withParameters(const ValueTree &parameters) const noexcept;
    
    //===------------------------------------------------------------------===//
    // Accessors
    //===------------------------------------------------------------------===//

    float getControllerValue() const noexcept;
    float getCurvature() const noexcept;
    
    //===------------------------------------------------------------------===//
    // Pedal helpers
    //===------------------------------------------------------------------===//
    
    bool isPedalDownEvent() const noexcept;
    bool isPedalUpEvent() const noexcept;

    static AutomationEvent pedalUpEvent(MidiSequence *owner, float beatVal = 0.f);
    static AutomationEvent pedalDownEvent(MidiSequence *owner, float beatVal = 0.f);

    //===------------------------------------------------------------------===//
    // Serializable
    //===------------------------------------------------------------------===//

    ValueTree serialize() const noexcept override;
    void deserialize(const ValueTree &tree) noexcept override;
    void reset() noexcept override;

    //===------------------------------------------------------------------===//
    // Helpers
    //===------------------------------------------------------------------===//

    void applyChanges(const AutomationEvent &parameters) noexcept;

protected:

    float controllerValue;
    float curvature;

private:

    JUCE_LEAK_DETECTOR(AutomationEvent);
};
