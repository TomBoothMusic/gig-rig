#include "Responses.h"
#include "effects/ir/IrTools.h"
#include "effects/ir/ResponsePlayer.h"

#include "BinaryData.h"

namespace gigrig::responses
{

juce::File folder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("GigRig").getChildFile ("Responses");
}

void setUp()
{
    folder().createDirectory();
    fx::ResponsePlayer::setFolder (folder().getFullPathName().toStdString());
}

Made makeFromCapture (const juce::File& capture, const juce::File& target)
{
    Made made;
    made.file = target;

    fx::ir::Wav wav;
    std::string error;

    if (! fx::ir::readWav (capture.getFullPathName().toStdString(), wav, error))
    {
        made.text = "Could not read " + capture.getFileName() + ": " + juce::String (error) + ".";
        return made;
    }

    if (wav.channels.size() < 2)
    {
        made.text = capture.getFileName() + " is not an Amp capture (it needs two channels: the test sound, and what came back).";
        return made;
    }

    const auto r = fx::ir::makeResponse (wav.channels[0], wav.channels[1], wav.rate);

    if (r.taps.empty())
    {
        made.text = "No response file made from " + capture.getFileName() + ": " + juce::String (r.error) + ".";
        return made;
    }

    fx::ir::Wav out;
    out.rate = r.rate;
    out.channels.push_back (r.taps);
    target.getParentDirectory().createDirectory();

    if (! fx::ir::writeWav (target.getFullPathName().toStdString(), out, error))
    {
        made.text = "Could not write " + target.getFullPathName() + ": " + juce::String (error) + ".";
        return made;
    }

    made.ok = true;
    made.text = "Made the response file \"" + target.getFileName() + "\" from " + capture.getFileName() + ". "
              + "The pedal's sound came back " + juce::String (r.delayMs, 2) + " ms late and " + juce::String (std::abs (r.levelDb), 1) + " dB "
              + (r.levelDb < 0.0 ? "quieter" : "louder") + "; both are taken out, so the file changes the tone and not the level. "
              + "A response file can explain " + juce::String (juce::roundToInt (r.matchPercent)) + "% of what the pedal did"
              + (r.matchPercent >= 95.0 ? ": it is tone-only at this setting, so the file is a close copy."
               : r.matchPercent >= 85.0 ? ": close, but the pedal also squashes or moves the sound a little, which a file cannot hold."
                                        : ": the pedal does a lot besides tone (squashing, distortion or movement), so the file is only part of its sound.")
              + " To use it: Effects tab, add a \"Response file\" effect and choose " + target.getFileName() + ".";

    juce::String curve;
    for (double hz : { 80.0, 110.0, 160.0, 220.0, 320.0, 450.0, 640.0, 900.0, 1300.0, 1800.0, 2500.0, 3500.0, 5000.0, 7000.0, 10000.0 })
        curve << (hz < 1000.0 ? juce::String (hz, 0) + " Hz" : juce::String (hz / 1000.0, 1) + " kHz") << ": "
              << juce::String (fx::ir::responseDb (r.taps, r.rate, hz), 1) << " dB\n";

    target.withFileExtension ("txt").replaceWithText ("# GigRig response file, made by build " + juce::String (GIGRIG_BUILD_LABEL) + "\n"
                                                      + made.text + "\n\n# What the file does to the tone:\n" + curve);
    return made;
}

} // namespace gigrig::responses

namespace gigrig::starterpresets
{

std::vector<std::pair<juce::String, juce::String>> all()
{
    // One line per preset built into the app (the files are in resources/presets).
    static const std::pair<const char*, const char*> builtIn[] {
        { "starteracoustic_txt", "Starter - Acoustic" },
        { "startervocal_txt",    "Starter - Vocal" },
        { "starterbass_txt",     "Starter - Bass (Bass9 copy)" },
        { "vocalforthemixer_txt", "Vocal - for the mixer" },
    };

    std::vector<std::pair<juce::String, juce::String>> list;

    for (const auto& b : builtIn)
    {
        int size = 0;
        if (const char* data = BinaryData::getNamedResource (b.first, size); data != nullptr && size > 0)
            list.emplace_back (b.second, juce::String::fromUTF8 (data, size));
    }

    return list;
}

int setUp (const juce::File& presetFolder)
{
    int wrote = 0;
    presetFolder.createDirectory();

    for (const auto& [name, text] : all())
    {
        const auto file = presetFolder.getChildFile (name + ".txt");

        // A starter that 'Tidy unused presets' moved into a "Not used" folder stays tidied away.
        if (! presetFolder.findChildFiles (juce::File::findFiles, true, name + ".txt").isEmpty())
            continue;

        if (! file.existsAsFile() && file.replaceWithText (text))
            ++wrote;
    }

    return wrote;
}

} // namespace gigrig::starterpresets
