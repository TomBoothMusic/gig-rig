#include "Registry.h"

#include "amp/NamAmp.h"
#include "analogdelay/AnalogDelay.h"
#include "chorus/Chorus.h"
#include "digitaldelay/DigitalDelay.h"
#include "spring/SpringReverb.h"
#include "tapedelay/TapeDelay.h"
#include "tremolo/Tremolo.h"
#include "vibrato/Vibrato.h"
#include "wah/Wah.h"
#include "whammy/Whammy.h"
#include "bass9/Bass9.h"
#include "body/AcousticBody.h"
#include "flanger/Flanger.h"
#include "harmony/Harmony.h"
#include "ir/ResponsePlayer.h"
#include "peq/ChannelEq.h"
#include "plate/PlateReverb.h"
#include "room/RoomReverb.h"
#include "univibe/UniVibe.h"
#include "bluesdriver/BluesDriver.h"
#include "boost/CleanBoost.h"
#include "comp/Comp1176.h"
#include "eq/GraphicEq.h"
#include "fuzz/Fuzz.h"
#include "gate/NoiseGate.h"
#include "klon/Klon.h"
#include "marshall/MarshallBox.h"
#include "tubescreamer/TubeScreamer.h"

namespace gigrig::fx
{

void tubeScreamerChecks (const Check&);
void driveChecks (const std::string& id, const Check&);
void bass9Checks (const Check&);
void fuzzChecks (const Check&);
void ampChecks (const Check&);
void gateChecks (const Check&);
void compChecks (const Check&);
void eqChecks (const Check&);
void delayChecks (const std::string& id, const Check&);
void tremoloChecks (const Check&);
void springChecks (const Check&);
void modulationChecks (const std::string& id, const Check&);
void reverbChecks (const std::string& id, const Check&);
void sweepChecks (const std::string& id, const Check&);
void channelEqChecks (const Check&);
void bodyChecks (const Check&);
void responseChecks (const Check&);
void harmonyChecks (const Check&);
void whammyChecks (const Check&);
void wahChecks (const Check&);

const std::vector<EffectType>& effectTypes()
{
    // One line per effect. The order is the order on screen.
    static const std::vector<EffectType> list {
        { "ts",       "Tube Screamer style", "Drive", "Mid-hump overdrive with a tight low end (TS808 / TS9).",
          [] { return std::make_unique<TubeScreamer>(); }, tubeScreamerChecks },
        { "bd",       "Blues Driver style",  "Drive", "Open, amp-like overdrive that cleans up with the guitar's volume (BD-2).",
          [] { return std::make_unique<BluesDriver>(); }, [] (const Check& c) { driveChecks ("bd", c); } },
        { "klon",     "Klon style",          "Drive", "Clean plus driven blend: a fat boost at low gain, smooth drive higher up (Centaur).",
          [] { return std::make_unique<Klon>(); }, [] (const Check& c) { driveChecks ("klon", c); } },
        { "marshall", "Marshall in a box",   "Drive", "Cranked Plexi / JCM800-style preamp with bass, middle, treble and presence.",
          [] { return std::make_unique<MarshallBox>(); }, [] (const Check& c) { driveChecks ("marshall", c); } },
        { "fuzz",     "Fuzz Face style",     "Drive", "Round, woolly two-transistor fuzz, with a bias knob for the sputtery dying-battery sound.",
          [] { return std::make_unique<Fuzz>(); }, fuzzChecks },
        { "boost",    "Clean boost",         "Drive", "Louder with a little colour, to push a drive or the amp.",
          [] { return std::make_unique<CleanBoost>(); }, {} },
        { "comp",     "1176 style compressor", "Utility", "Studio FET compressor (1176 Rev E blackface): Input pushes it harder, attack and release 1 to 7 (7 fastest), ratio 4 to All.",
          [] { return std::make_unique<Comp1176>(); }, compChecks },
        { "eq",       "Graphic EQ (GE-7 style)", "Utility", "Seven sliders from 100 Hz to 6.4 kHz, up or down 15 dB, and a level: trim the lows or lift the top.",
          [] { return std::make_unique<GraphicEq>(); }, eqChecks },
        { "gate",     "Noise gate",          "Utility", "Turns the hiss and hum down when you stop playing; opens in a couple of milliseconds when you play.",
          [] { return std::make_unique<NoiseGate>(); }, gateChecks },
        { "amp",      "Amp model (NAM)",     "Amp",   "An amp and its cab from a capture: Tom's Dream '65 settings, or any .nam model in Documents\\GigRig\\Amp models.",
          [] { return std::make_unique<NamAmp>(); }, ampChecks },
        { "analogdelay",  "Analog delay",          "Time", "Short, warm bucket-brigade echo for slapback: each repeat darker and a little dirty. Not synced (DM-2 / Carbon Copy).",
          [] { return std::make_unique<AnalogDelay>(); }, [] (const Check& c) { delayChecks ("analogdelay", c); } },
        { "digitaldelay", "Digital delay",         "Time", "Clean echo with a touch of chorus on the repeats; follows the song's tempo in note values, or its own time.",
          [] { return std::make_unique<DigitalDelay>(); }, [] (const Check& c) { delayChecks ("digitaldelay", c); } },
        { "tapedelay",    "Tape delay",            "Time", "Tape echo (Space Echo / Echoplex): wow, flutter and darkening, saturating repeats. Lovely on vocals.",
          [] { return std::make_unique<TapeDelay>(); }, [] (const Check& c) { delayChecks ("tapedelay", c); } },
        { "spring",       "Spring reverb",         "Time", "Two-spring tank like a Fender Deluxe Reverb's, with the drip. Put it after the amp.",
          [] { return std::make_unique<SpringReverb>(); }, springChecks },
        { "tremolo",      "Tremolo",               "Modulation", "The amp's pulsing volume (the Dream calls it vibrato): speed, intensity and a smooth-to-choppy shape. After the amp.",
          [] { return std::make_unique<Tremolo>(); }, tremoloChecks },
        { "vibrato",      "Vibrato",               "Modulation", "True pitch vibrato (VB-2 style): only the wobbled sound comes out.",
          [] { return std::make_unique<Vibrato>(); }, [] (const Check& c) { modulationChecks ("vibrato", c); } },
        { "chorus",       "Chorus",                "Modulation", "Analog-style chorus (CE-2): two swept, slightly dark copies with the dry guitar.",
          [] { return std::make_unique<Chorus>(); }, [] (const Check& c) { modulationChecks ("chorus", c); } },
        { "flanger",      "Flanger",               "Modulation", "Jet-plane sweep (BF-2 style): a very short swept copy fed back on itself. For the vocal in Come As You Are, or a guitar.",
          [] { return std::make_unique<Flanger>(); }, [] (const Check& c) { sweepChecks ("flanger", c); } },
        { "univibe",      "Uni-Vibe",              "Modulation", "The swirling, throbbing phase sound of a Uni-Vibe (rotary-speaker style), with its lopsided lamp pulse. Chorus or Vibrato.",
          [] { return std::make_unique<UniVibe>(); }, [] (const Check& c) { sweepChecks ("univibe", c); } },
        { "plate",        "Plate reverb",          "Time", "Bright, smooth studio plate for the voice. A simple one to try now; its home will be the mixer.",
          [] { return std::make_unique<PlateReverb>(); }, [] (const Check& c) { reverbChecks ("plate", c); } },
        { "room",         "Room reverb",           "Time", "Small room, room or hall around an acoustic guitar or a voice. A simple one to try now; its home will be the mixer.",
          [] { return std::make_unique<RoomReverb>(); }, [] (const Check& c) { reverbChecks ("room", c); } },
        { "peq",          "Channel EQ",            "Utility", "Mixer-style EQ: low cut, high cut, low and high shelves and two sweepable mid bands. For the acoustic's boost-and-EQ sounds and the voice.",
          [] { return std::make_unique<ChannelEq>(); }, channelEqChecks },
        { "body",         "Acoustic body",         "Acoustic", "Makes a pickup sound more like the guitar's wooden body and less like a quack (BodyRez style): one Body knob and a phase flip for feedback.",
          [] { return std::make_unique<AcousticBody>(); }, bodyChecks },
        { "ir",           "Response file",         "Acoustic", "Plays back the tone of a tone-only pedal measured on the Amp capture tab (the BodyRez at your setting), or any short response WAV in Documents\\GigRig\\Responses.",
          [] { return std::make_unique<ResponsePlayer>(); }, responseChecks },
        { "harmony",      "Vocal harmony",         "Vocal", "Sings a second voice with yours, in the song's key: a third above, a fifth, an octave and more. Your own voice passes through untouched.",
          [] { return std::make_unique<Harmony>(); }, harmonyChecks },
        { "whammy",       "Whammy style pedal", "Pitch", "Bends the guitar up or down as far as two octaves (or a dive bomb) with the Pedal slider or a held switch; harmony and detune modes too.",
          [] { return std::make_unique<Whammy>(); }, whammyChecks },
        { "wah",          "Wah pedal",             "Filter", "A Cry Baby style wah. Rock it with the Pedal slider or a held switch; set where the sweep starts and ends, how narrow the peak is (Q) and how deep.",
          [] { return std::make_unique<Wah>(); }, wahChecks },
        { "bass9",    "Bass9 PRECISION copy", "Bass", "Guitar in, P-bass out: the app's copy of the EHX Bass9 PRECISION patch.",
          [] { return std::make_unique<Bass9>(); }, bass9Checks },
    };

    return list;
}

const EffectType* findEffectType (const std::string& id)
{
    for (const auto& t : effectTypes())
        if (t.id == id)
            return &t;

    return nullptr;
}

std::unique_ptr<Effect> makeEffect (const std::string& id)
{
    const auto* t = findEffectType (id);
    return t != nullptr ? t->make() : nullptr;
}

} // namespace gigrig::fx
