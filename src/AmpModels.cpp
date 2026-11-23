#include "AmpModels.h"
#include "effects/amp/NamAmp.h"

#include "BinaryData.h"

namespace gigrig::ampmodels
{

namespace
{
    struct BuiltIn { const char* resource; const char* name; };

    // One line per model built into the app (the files are in resources/amp-models).
    const BuiltIn builtIn[] {
        { "dream65breakup_nam", "Dream 65 - Break Up" },
        { "dream65clean_nam",   "Dream 65 - Clean" },
        { "dream65driven_nam",  "Dream 65 - Driven" },
    };
}

juce::File folder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("GigRig").getChildFile ("Amp models");
}

juce::String defaultName()      { return builtIn[0].name; }

int setUp (const juce::File& dir)
{
    int wrote = 0;
    dir.createDirectory();

    for (const auto& b : builtIn)
    {
        int size = 0;
        const char* data = BinaryData::getNamedResource (b.resource, size);
        if (data == nullptr || size <= 0)
            continue;

        const auto file = dir.getChildFile (juce::String (b.name) + ".nam");
        juce::MemoryBlock existing;

        if (file.existsAsFile() && file.loadFileAsData (existing)
            && existing.getSize() == (size_t) size && std::memcmp (existing.getData(), data, (size_t) size) == 0)
            continue;

        if (file.replaceWithData (data, (size_t) size))
            ++wrote;
    }

    fx::NamAmp::setModelFolder (dir.getFullPathName().toStdString());
    fx::NamAmp::setDefaultModel (defaultName().toStdString());
    return wrote;
}

juce::StringArray names (const juce::File& dir)
{
    juce::StringArray list;
    for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.nam"))
        list.add (f.getFileNameWithoutExtension());
    list.sort (true);
    return list;
}

} // namespace gigrig::ampmodels
