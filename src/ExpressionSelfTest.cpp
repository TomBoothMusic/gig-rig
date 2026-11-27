#include "AudioEngine.h"
#include "ExpressionPedal.h"

#include <functional>

// Checks for the expression pedal's MIDI input (27 Nov 2026), run by the automatic builds:
// how a controller value becomes a position, that Learn finds the pedal and its ends and never
// takes a footswitch for one, that only the learned controller moves anything, that fast moves
// are merged, and that a move reaches the volume pedals (the blend) and, with the toe switch
// on, the wah, through the same door the on-screen sliders use.

namespace gigrig
{

void runExpressionSelfTest (const std::function<void (bool, const juce::String&)>& check)
{
    using Setup = ExpressionInput::Setup;
    const auto cc = [] (int channel, int number, int value) { return juce::MidiMessage::controllerEvent (channel, number, value); };

    // ---- a value becomes a position ----
    {
        Setup s;
        s.cc = 11;
        check (ExpressionInput::positionFor (s, 0) == 0.0f && ExpressionInput::positionFor (s, 127) == 100.0f
               && ExpressionInput::positionFor (s, 2) == 0.0f && ExpressionInput::positionFor (s, 125) == 100.0f,
               "expression pedal: the last couple of steps at each end count as the heel and the toe");
        const float mid = ExpressionInput::positionFor (s, 64);
        check (mid > 49.0f && mid < 52.0f, "expression pedal: half way is half way (" + juce::String (mid, 1) + ")");

        s.heel = 20; s.toe = 100;
        check (ExpressionInput::positionFor (s, 5) == 0.0f && ExpressionInput::positionFor (s, 120) == 100.0f && std::abs (ExpressionInput::positionFor (s, 60) - 50.0f) < 1.0f,
               "expression pedal: a pedal that only reaches 20 to 100 still covers heel to toe");

        s.heel = 127; s.toe = 0;
        check (ExpressionInput::positionFor (s, 127) == 0.0f && ExpressionInput::positionFor (s, 0) == 100.0f && ExpressionInput::positionFor (s, 100) < 30.0f,
               "expression pedal: heel and toe swapped turns the pedal round");

        bool rising = true;
        s.heel = 0; s.toe = 127;
        for (int v = 1; v < 128; ++v)
            rising = rising && ExpressionInput::positionFor (s, v) >= ExpressionInput::positionFor (s, v - 1);
        check (rising, "expression pedal: the position never goes backwards as the pedal goes forwards");
    }

    // ---- Learn ----
    {
        ExpressionInput in;
        in.setDeliverByItself (false);
        std::vector<float> moves;
        in.onMove = [&] (float p) { moves.push_back (p); };

        in.handleMidi ("RC-600", cc (1, 11, 64));
        in.deliver();
        check (moves.empty() && ! in.isSetUp(), "expression pedal: before a pedal is learned, nothing moves anything");

        in.startLearning();
        const auto nothing = in.finishLearning();
        check (! in.isSetUp() && nothing.contains ("Nothing arrived"), "expression pedal: Learn with no pedal moved says so and changes nothing");

        in.startLearning();
        for (int i = 0; i < 6; ++i)
        {
            in.handleMidi ("RC-600", cc (1, 22, 127));      // a footswitch pressed and let go while learning
            in.handleMidi ("RC-600", cc (1, 22, 0));
        }
        const auto onlySwitch = in.finishLearning();
        check (! in.isSetUp() && onlySwitch.contains ("footswitch"), "expression pedal: a footswitch is never taken for a pedal");

        in.startLearning();
        in.handleMidi ("RC-600", cc (1, 22, 127));
        in.handleMidi ("RC-600", cc (1, 22, 0));
        for (int v = 3; v <= 124; v += 2)
            in.handleMidi ("RC-600", cc (2, 31, v));
        for (int v = 124; v >= 3; v -= 3)
            in.handleMidi ("RC-600", cc (2, 31, v));
        const bool listening = in.learningLine().contains ("controller 31");
        const auto found = in.finishLearning();
        const auto s = in.setup();
        check (listening && s.cc == 31 && s.channel == 2 && s.device == "RC-600" && s.heel == 3 && s.toe == 124 && found.contains ("Learned"),
               "expression pedal: Learn picks the controller that swept, with its channel, its device and its two ends (" + found + ")");
        check (moves.empty(), "expression pedal: nothing moves while learning");

        // ---- only the learned controller counts, and fast moves are merged ----
        in.handleMidi ("RC-600", cc (2, 30, 100));
        in.handleMidi ("RC-600", cc (1, 31, 100));
        in.handleMidi ("Other device", cc (2, 31, 100));
        in.deliver();
        check (moves.empty(), "expression pedal: another controller, channel or device moves nothing");

        in.handleMidi ("RC-600", cc (2, 31, 10));
        in.handleMidi ("RC-600", cc (2, 31, 60));
        in.handleMidi ("RC-600", cc (2, 31, 124));
        in.deliver();
        in.deliver();
        check (moves.size() == 1 && moves[0] == 100.0f && in.messages() == 3, "expression pedal: moves that arrive in a rush are merged, and the newest one is what counts");

        // ---- the settings file ----
        ExpressionInput other;
        other.loadStateText (in.stateText());
        check (other.setup() == in.setup() && other.stateText() == in.stateText(), "expression pedal: the learned pedal comes back from the app's memory file");
        check (in.reportText().contains ("Controller 31"), "expression pedal: the report has its part");
    }

    // ---- a move reaches the blend, and the wah once the toe switch is on ----
    {
        AudioEngine engine;
        engine.openSimulated (48000.0, 64, 16, 16);
        auto& rack = engine.effects();
        rack.setEnabled (true);
        rack.chain (0).setModel (fx::ChainModel::fromText ("slot=volume\nslot=wah\non=0\n"));
        rack.chain (1).setModel (fx::ChainModel::fromText ("slot=volume\nreverse=1\n"));

        ExpressionInput in;
        in.setDeliverByItself (false);
        in.onMove = [&] (float p) { rack.moveExpressionPedal (p); };
        Setup s;
        s.cc = 31;
        in.setSetup (s);

        const auto pedalOf = [&] (int chain, int slot) { return rack.chain (chain).model().slots[(size_t) slot].param ("pedal", -1.0f); };
        in.handleMidi ("Any", cc (1, 31, 64));
        in.deliver();
        const float blendA = pedalOf (0, 0), blendB = pedalOf (1, 0);
        check (std::abs (blendA - ExpressionInput::positionFor (s, 64)) < 0.01f && std::abs (blendB - blendA) < 0.01f,
               "expression pedal: a move sets every volume pedal (the blend) to the pedal's position (" + juce::String (blendA, 1) + ")");

        rack.toeSwitch (0);
        in.handleMidi ("Any", cc (1, 31, 20));
        in.deliver();
        const float wah = pedalOf (0, 1);
        check (std::abs (wah - ExpressionInput::positionFor (s, 20)) < 0.01f && pedalOf (0, 0) == 100.0f,
               "expression pedal: with the toe switch on the pedal rocks the wah and the electric stays at full (wah at " + juce::String (wah, 1) + ")");
        rack.toeSwitch (0);
    }
}

} // namespace gigrig
