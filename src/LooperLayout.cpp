#include "LooperLayout.h"

namespace gigrig::layout
{

namespace
{
    const int sourceBits[] = { rcMic1, rcInst1L, rcInst1R, rcInst2L, rcInst2R, rcMic2, rcRhythm };
    const char* const sourceNames[] = { "vocal", "acoustic", "stomp", "bass", "electric", "mic 2", "rhythm" };

    // THE WORDS BELOW ARE SAVED IN SONG FILES. Never change one; add new ones at the end.
    const char* const kindWords[] = { "record play", "play stop", "stop", "clear", nullptr, "undo redo" };
    const char* const returnWords[] = { "in place", "from its first bar", "from its first note" };          // ReturnMode order
    const char* const stopWords[] = { "stops at once", "fades out", "stops at the end of its loop" };       // StopMode order

    juce::String squash (const juce::String& s)
    {
        return s.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789");
    }

    /** "track 3" -> 2, "current track" -> -1, else -3. */
    int trackFromWords (const juce::String& w)
    {
        const auto s = squash (w);

        if (s == "currenttrack" || s == "current")
            return -1;

        if (s.startsWith ("track") || s.startsWith ("t"))
        {
            const int n = s.retainCharacters ("0123456789").getIntValue();
            return n >= 1 && n <= numLoopTracks ? n - 1 : -3;
        }

        return -3;
    }

    juce::String trackWords (int track)
    {
        return track < 0 ? juce::String ("current track") : "track " + juce::String (track + 1);
    }
}

const std::vector<FunctionChoice>& functionChoices()
{
    static const std::vector<FunctionChoice> choices
    {
        { "nothing",                                        "Nothing",                                                                      -1, 0 },
        { "record play",                                    "Record, play, overdub",                                                         0, 0 },
        { "record play, hold undo",                         "Record, play, overdub. Hold: undo",                                             1, 0 },
        { "record play, hold undo, double stop",            "Record, play, overdub. Hold: undo. Double-press: stop",                         2, 0 },
        { "record play, hold undo or clear, double stop",   "Record, play, overdub. Hold: undo, or clear when stopped. Double-press: stop",  3, 0 },
        { "play stop",                                      "Play or stop",                                                                 26, 0 },    // 26 = the RC-600's TRK PLAY/STOP list (numbers 217 on)
        { "play stop, hold undo",                           "Play or stop. Hold: undo",                                                      5, 0 },
        { "play stop, hold undo or clear",                  "Play or stop. Hold: undo, or clear when stopped",                               6, 0 },
        { "stop",                                           "Stop",                                                                          7, 0 },
        { "stop, hold clear",                               "Stop. Hold: clear",                                                             9, 0 },
        { "stop, double clear",                             "Stop. Double-press: clear",                                                    11, 0 },
        { "clear",                                          "Clear",                                                                        12, 0 },
        { "undo redo",                                      "Undo, then redo",                                                              14, 0 },
        { "all start stop",                                 "All start or stop",                                                            -1, 186 },
        { "all start stop, hold clear all",                 "All start or stop. Hold: clear every track",                                   -1, 187 },
        { "all start stop, double clear all",               "All start or stop. Double-press: clear every track",                           -1, 188 },
        { "clear all",                                      "Clear every track",                                                            -1, 189 },
        { "bank",                                           "Change bank",                                                                  -1, 193 },
    };

    return choices;
}

int codeFor (const FunctionChoice& c, int track) noexcept
{
    if (c.index < 0)
        return c.code;

    if (c.index == 26)
        return track >= 0 ? 217 + track : 157 + 5;      // the current track has no bare play/stop: the nearest is play/stop with undo on hold

    return track >= 0 ? track * 26 + c.index + 1 : 157 + c.index;
}

juce::String functionWords (int code)
{
    if (code <= 0)
        return "nothing";

    int index = -2, track = -2;

    if (code <= 182)
    {
        const bool current = code >= 157;
        index = current ? code - 157 : (code - 1) % 26;
        track = current ? -1 : (code - 1) / 26;

        // Two pairs of RC-600 functions do the same thing in this looper: say the first of each.
        if (index == 8)     index = 7;
        if (index == 10)    index = 9;
    }
    else if (code >= 217 && code <= 222)
    {
        index = 26;
        track = code - 217;
    }
    else if (code == 194)
    {
        code = 193;
    }

    for (const auto& c : functionChoices())
    {
        if (index >= 0 && c.index == index)
            return trackWords (track) + " / " + c.words;

        if (index < 0 && c.index < 0 && c.code == code)
            return c.words;
    }

    return "rc " + juce::String (code);
}

bool functionFromWords (const juce::String& words, int& code)
{
    const auto text = words.trim();

    if (text.toLowerCase().startsWith ("rc "))
    {
        code = text.substring (3).getIntValue();
        return code >= 0 && code < 1000 && text.substring (3).trim().containsOnly ("0123456789");
    }

    const bool hasTrack = text.containsChar ('/');
    const auto what = squash (hasTrack ? text.fromFirstOccurrenceOf ("/", false, false) : text);
    const int track = hasTrack ? trackFromWords (text.upToFirstOccurrenceOf ("/", false, false)) : -2;

    for (const auto& c : functionChoices())
    {
        if (squash (c.words) != what)
            continue;

        if (c.index < 0)
        {
            code = c.code;
            return ! hasTrack;
        }

        if (track < -1)
            return false;

        code = codeFor (c, track);
        return true;
    }

    return false;
}

juce::String linkWords (const LoopAssign& a)
{
    juce::String when;

    switch (a.source)
    {
        case LoopAssign::Source::off:            return {};
        case LoopAssign::Source::pedal:          when = a.sw >= 9 ? juce::String (a.sw == 9 ? "switch CTL3" : "switch CTL4")
                                                                  : "switch " + juce::String (a.bank + 1) + "." + juce::String (a.sw + 1); break;
        case LoopAssign::Source::trackRecDub:    when = "track " + juce::String (a.sourceTrack + 1) + " records"; break;
        case LoopAssign::Source::trackPlayStop:  when = "track " + juce::String (a.sourceTrack + 1) + " plays"; break;
    }

    if (a.action == LoopAction::allStartStop)
        return when + " / all start stop";

    const char* kind = nullptr;

    switch (a.action)
    {
        case LoopAction::recPlay:   kind = kindWords[0]; break;
        case LoopAction::playStop:  kind = kindWords[1]; break;
        case LoopAction::stop:      kind = kindWords[2]; break;
        case LoopAction::clear:     kind = kindWords[3]; break;
        case LoopAction::undoRedo:  kind = kindWords[5]; break;
        case LoopAction::none: case LoopAction::undoOrClear: case LoopAction::allStartStop: case LoopAction::allClear:
        case LoopAction::bankToggle: case LoopAction::unsupported: break;
    }

    if (kind == nullptr)
        return "rc " + juce::String (a.rcSource) + " " + juce::String (a.rcTarget);

    return when + " / " + trackWords (a.targetTrack) + " / " + kind;
}

bool linkFromWords (const juce::String& words, LoopAssign& out)
{
    const auto text = words.trim();

    if (text.toLowerCase().startsWith ("rc "))
    {
        const auto numbers = juce::StringArray::fromTokens (text.substring (3), " ", "");

        if (numbers.size() != 2 || ! (numbers[0] + numbers[1]).containsOnly ("0123456789"))
            return false;

        out = decodeRcAssign (numbers[0].getIntValue(), numbers[1].getIntValue());
        return out.source != LoopAssign::Source::off;
    }

    auto parts = juce::StringArray::fromTokens (text, "/", "");
    parts.trim();

    if (parts.size() < 2 || parts.size() > 3)
        return false;

    int src = -1, tgt = -1;
    const auto when = parts[0].toLowerCase();

    if (when.startsWith ("switch"))
    {
        const auto which = when.substring (6).trim();

        if (which == "ctl3")        src = 42;
        else if (which == "ctl4")   src = 43;
        else
        {
            const int bank = which.upToFirstOccurrenceOf (".", false, false).getIntValue();
            const int sw = which.fromFirstOccurrenceOf (".", false, false).getIntValue();

            if (! which.containsChar ('.') || bank < 1 || bank > numLoopBanks || sw < 1 || sw > 9)
                return false;

            src = 13 + (bank - 1) * 9 + (sw - 1);
        }
    }
    else if (when.startsWith ("track"))
    {
        const int track = trackFromWords (when.upToLastOccurrenceOf (" ", false, false));

        if (track < 0)
            return false;

        if (when.endsWith ("records"))      src = track;
        else if (when.endsWith ("plays"))   src = 6 + track;
        else                                return false;
    }
    else
    {
        return false;
    }

    if (parts.size() == 2)
    {
        if (squash (parts[1]) != "allstartstop")
            return false;

        tgt = 80;
    }
    else
    {
        const int track = trackFromWords (parts[1]);
        int kind = -1;

        for (int k = 0; k < 6; ++k)
            if (kindWords[k] != nullptr && squash (kindWords[k]) == squash (parts[2]))
                kind = k;

        if (track < -1 || kind < 0)
            return false;

        tgt = track >= 0 ? track * 11 + kind : 66 + kind;
    }

    out = decodeRcAssign (src, tgt);
    return true;
}

juce::String sourceWords (int rcInputs)
{
    juce::StringArray parts;

    for (int s = 0; s < 7; ++s)
        if ((rcInputs & sourceBits[s]) != 0)
            parts.add (sourceNames[s]);

    return parts.isEmpty() ? juce::String ("nothing") : parts.joinIntoString (" + ");
}

bool sourcesFromWords (const juce::String& words, int& rcInputs)
{
    int mask = 0;

    if (squash (words) != "nothing")
    {
        auto parts = juce::StringArray::fromTokens (words, "+", "");
        parts.trim();

        for (const auto& p : parts)
        {
            int bit = 0;

            for (int s = 0; s < 7; ++s)
                if (squash (sourceNames[s]) == squash (p))
                    bit = sourceBits[s];

            if (bit == 0)
                return false;

            mask |= bit;
        }
    }

    rcInputs = mask;
    return true;
}

juce::StringArray toLines (const SongSetup& song)
{
    juce::StringArray lines;
    lines.add ("looper beats = " + juce::String (song.beatsPerBar));
    lines.add (juce::String ("looper order = ") + (song.recThenPlay ? "record play overdub" : "record overdub play"));
    lines.add (juce::String ("looper new track = ") + (song.quantise ? "waits for the bar" : "starts at once"));

    for (int t = 0; t < numLoopTracks; ++t)
    {
        const auto& tr = song.track[(size_t) t];
        lines.add ("looper track " + juce::String (t + 1) + " = " + sourceWords (tr.rcInputs)
                     + " / " + (tr.bars > 0 ? juce::String (tr.bars) + (tr.bars == 1 ? " bar" : " bars") : juce::String ("free"))
                     + " / " + returnWords[(int) tr.returnMode]
                     + " / " + stopWords[(int) tr.stopMode]
                     + " / level " + juce::String (tr.level));
    }

    for (int b = 0; b < numLoopBanks; ++b)
        for (int sw = 0; sw < 9; ++sw)
            lines.add ("looper switch " + juce::String (b + 1) + "." + juce::String (sw + 1) + " = " + functionWords (song.bank[b][sw]));

    bool anyLink = false;

    for (const auto& a : song.assign)
        if (a.source != LoopAssign::Source::off)
        {
            lines.add ("looper link = " + linkWords (a));
            anyLink = true;
        }

    // Said out loud, because these lines are laid over the layout the song borrows, whose own links would otherwise stay.
    if (! anyLink)
        lines.add ("looper links = none");

    return lines;
}

bool fromLines (const juce::StringArray& lines, SongSetup& song, juce::StringArray& problems)
{
    bool any = false, linksCleared = false;
    size_t nextLink = 0;

    for (const auto& whole : lines)
    {
        const auto key = whole.upToFirstOccurrenceOf ("=", false, false).trim().toLowerCase();
        const auto value = whole.fromFirstOccurrenceOf ("=", false, false).trim();
        const auto bad = [&] (const juce::String& why) { problems.add (whole.trim() + "  (" + why + ")"); };

        if (! key.startsWith ("looper "))
            continue;

        const auto what = key.substring (7).trim();

        if (what == "beats")
        {
            const int beats = value.getIntValue();
            if (beats < 1 || beats > 16)    { bad ("beats in a bar is 1 to 16"); continue; }
            song.beatsPerBar = beats;
        }
        else if (what == "order")
        {
            const auto v = squash (value);
            if (v != "recordplayoverdub" && v != "recordoverdubplay")   { bad ("the order is: record play overdub, or: record overdub play"); continue; }
            song.recThenPlay = v == "recordplayoverdub";
        }
        else if (what == "new track")
        {
            const auto v = squash (value);
            if (v != "waitsforthebar" && v != "startsatonce")           { bad ("a new track either: waits for the bar, or: starts at once"); continue; }
            song.quantise = v == "waitsforthebar";
        }
        else if (what.startsWith ("track"))
        {
            const int t = what.substring (5).getIntValue() - 1;
            auto parts = juce::StringArray::fromTokens (value, "/", "");
            parts.trim();

            if (t < 0 || t >= numLoopTracks || parts.isEmpty())         { bad ("a track line reads: looper track 1 = sources / length / how it comes back / how it stops / level"); continue; }

            TrackSetup tr = song.track[(size_t) t];
            bool good = sourcesFromWords (parts[0], tr.rcInputs);

            if (! good)
            {
                bad ("the sources are: vocal, acoustic, stomp, bass, electric, joined with +, or: nothing");
                continue;
            }

            for (int p = 1; p < parts.size() && good; ++p)
            {
                const auto w = squash (parts[p]);
                bool used = false;

                if (w == "free")                                        { tr.bars = 0; used = true; }
                else if (juce::CharacterFunctions::isDigit (w[0]) && (w.endsWith ("bars") || w.endsWith ("bar")))      // "4 bars", and not "from its first bar"
                                                                        { const int bars = parts[p].getIntValue(); if (bars >= 1 && bars <= 64) { tr.bars = bars; used = true; } }
                else if (w.startsWith ("level"))                        { tr.level = juce::jlimit (0, 200, parts[p].retainCharacters ("0123456789").getIntValue()); used = true; }

                for (int k = 0; k < 3 && ! used; ++k)
                {
                    if (squash (returnWords[k]) == w)   { tr.returnMode = (ReturnMode) k; used = true; }
                    else if (squash (stopWords[k]) == w) { tr.stopMode = (StopMode) k; used = true; }
                }

                good = used;
            }

            if (! good)     { bad ("part of the line was not understood"); continue; }
            song.track[(size_t) t] = tr;
        }
        else if (what.startsWith ("switch"))
        {
            const auto which = what.substring (6).trim();
            const int b = which.upToFirstOccurrenceOf (".", false, false).getIntValue() - 1;
            const int sw = which.fromFirstOccurrenceOf (".", false, false).getIntValue() - 1;
            int code = 0;

            if (! which.containsChar ('.') || b < 0 || b >= numLoopBanks || sw < 0 || sw >= 9)   { bad ("a switch is written bank.switch, such as: looper switch 1.3"); continue; }
            if (! functionFromWords (value, code))                                               { bad ("that is not something a switch can do"); continue; }
            song.bank[b][sw] = code;
        }
        else if (what == "link")
        {
            LoopAssign a;

            if (! linkFromWords (value, a))     { bad ("a link reads: when / track / what, such as: switch 1.3 / track 1 / play stop"); continue; }

            // The first link line replaces the layout's own links; the file is the whole list.
            if (! linksCleared)
            {
                song.assign = {};
                linksCleared = true;
            }

            if (nextLink >= song.assign.size())  { bad ("a layout holds at most " + juce::String (maxLoopAssigns) + " links"); continue; }
            song.assign[nextLink++] = a;
        }
        else if (what == "links")
        {
            song.assign = {};           // "looper links = none"
            linksCleared = true;
        }
        else
        {
            bad ("not a looper line this build knows");
            continue;
        }

        any = true;
    }

    return any;
}

juce::var toJson (const SongSetup& song)
{
    auto* o = new juce::DynamicObject();
    juce::var result (o);
    o->setProperty ("name", song.title());
    o->setProperty ("beats", song.beatsPerBar);
    o->setProperty ("recThenPlay", song.recThenPlay);
    o->setProperty ("quantise", song.quantise);
    o->setProperty ("tempo", song.savedBpm);

    juce::Array<juce::var> tracks, banks, links;

    for (const auto& tr : song.track)
    {
        auto* t = new juce::DynamicObject();
        t->setProperty ("inputs", tr.rcInputs);
        t->setProperty ("bars", tr.bars);
        t->setProperty ("stop", (int) tr.stopMode);
        t->setProperty ("ret", (int) tr.returnMode);
        t->setProperty ("level", tr.level);
        tracks.add (juce::var (t));
    }

    for (int b = 0; b < numLoopBanks; ++b)
    {
        juce::Array<juce::var> codes;
        for (int sw = 0; sw < 9; ++sw)
            codes.add (song.bank[b][sw]);
        banks.add (codes);
    }

    for (const auto& a : song.assign)
        if (a.source != LoopAssign::Source::off)
        {
            auto* l = new juce::DynamicObject();
            l->setProperty ("src", a.rcSource);
            l->setProperty ("tgt", a.rcTarget);
            links.add (juce::var (l));
        }

    o->setProperty ("tracks", tracks);
    o->setProperty ("banks", banks);
    o->setProperty ("links", links);
    return result;
}

juce::String fromJson (const juce::var& v, SongSetup& song)
{
    if (! v.isObject())
        return "The layout did not arrive whole.";

    const auto* tracks = v["tracks"].getArray();
    const auto* banks = v["banks"].getArray();
    const auto* links = v["links"].getArray();

    if (tracks == nullptr || tracks->size() != numLoopTracks || banks == nullptr || banks->size() != numLoopBanks)
        return "The layout did not arrive whole.";

    SongSetup s = song;
    s.beatsPerBar = juce::jlimit (1, 16, (int) v.getProperty ("beats", 4));
    s.recThenPlay = (bool) v.getProperty ("recThenPlay", true);
    s.quantise = (bool) v.getProperty ("quantise", true);

    for (int t = 0; t < numLoopTracks; ++t)
    {
        const auto& j = tracks->getReference (t);
        auto& tr = s.track[(size_t) t];
        tr.rcInputs = (int) j.getProperty ("inputs", 0) & 127;
        tr.bars = juce::jlimit (0, 64, (int) j.getProperty ("bars", 0));
        tr.stopMode = (StopMode) juce::jlimit (0, 2, (int) j.getProperty ("stop", 0));
        tr.returnMode = (ReturnMode) juce::jlimit (0, 2, (int) j.getProperty ("ret", 0));
        tr.level = juce::jlimit (0, 200, (int) j.getProperty ("level", 100));
    }

    for (int b = 0; b < numLoopBanks; ++b)
    {
        const auto* codes = banks->getReference (b).getArray();

        if (codes == nullptr || codes->size() != 9)
            return "The layout did not arrive whole.";

        for (int sw = 0; sw < 9; ++sw)
            s.bank[b][sw] = juce::jlimit (0, 999, (int) codes->getReference (sw));
    }

    s.assign = {};

    if (links != nullptr)
    {
        if (links->size() > maxLoopAssigns)
            return "A layout holds at most " + juce::String (maxLoopAssigns) + " links. Take some out and try again.";

        for (int i = 0; i < links->size(); ++i)
            s.assign[(size_t) i] = decodeRcAssign ((int) links->getReference (i).getProperty ("src", -1), (int) links->getReference (i).getProperty ("tgt", -1));
    }

    song = s;
    return {};
}

} // namespace gigrig::layout
