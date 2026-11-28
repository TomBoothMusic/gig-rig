#include "RecordingLibrary.h"
#include "Soundcheck.h"

namespace gigrig
{

namespace
{
    juce::String gb (juce::int64 bytes)
    {
        const double g = (double) bytes / 1.0e9;
        return g >= 0.1 ? juce::String (g, 1) + " GB" : juce::String (juce::roundToInt ((double) bytes / 1.0e6)) + " MB";
    }

    bool isSound (const juce::File& f)      { return f.hasFileExtension ("wav"); }
}

RecordingLibrary::RecordingLibrary() : juce::Thread ("Recording copy") {}

RecordingLibrary::~RecordingLibrary()
{
    stopThread (8000);
}

juce::File RecordingLibrary::rootNow() const
{
    return root != nullptr ? root() : juce::File();
}

juce::File RecordingLibrary::folderOf (const juce::String& name) const
{
    // A name is a folder straight inside the recordings folder and nothing else: never a path.
    if (name.isEmpty() || name.containsAnyOf ("/\\:") || name == "." || name == "..")
        return {};

    const auto f = rootNow().getChildFile (name);
    return f.isDirectory() ? f : juce::File();
}

std::vector<RecordingLibrary::Info> RecordingLibrary::list() const
{
    std::vector<Info> all;
    const auto base = rootNow();

    if (! base.isDirectory())
        return all;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    for (const auto& folder : base.findChildFiles (juce::File::findDirectories, false))
    {
        Info i;
        i.name = folder.getFileName();
        i.folder = folder;
        juce::File first;

        for (const auto& f : folder.findChildFiles (juce::File::findFiles, false))
        {
            i.bytes += f.getSize();

            if (isSound (f))
            {
                ++i.files;

                if (first == juce::File())
                    first = f;
            }
        }

        if (i.files == 0)
            continue;       // not a recording

        if (std::unique_ptr<juce::AudioFormatReader> r { formats.createReaderFor (first) })
            if (r->sampleRate > 0.0)
                i.seconds = (double) r->lengthInSamples / r->sampleRate;

        i.kept = folder.getChildFile (keepFile).existsAsFile();
        i.copied = folder.getChildFile (copiedFile).existsAsFile();
        i.soundcheckSockets = (int) Soundcheck::sourcesIn (folder).size();
        all.push_back (i);
    }

    std::sort (all.begin(), all.end(), [] (const Info& a, const Info& b) { return a.name > b.name; });
    return all;
}

double RecordingLibrary::freeGb() const
{
    auto folder = rootNow();

    while (! folder.isDirectory() && folder.getParentDirectory() != folder)
        folder = folder.getParentDirectory();

    return (double) folder.getBytesFreeOnVolume() / 1.0e9;
}

juce::String RecordingLibrary::setKept (const juce::String& name, bool keep)
{
    const auto folder = folderOf (name);

    if (folder == juce::File())
        return "There is no recording called \"" + name + "\".";

    const auto mark = folder.getChildFile (keepFile);

    if (keep)
        return mark.replaceWithText ("This recording is kept: clearing recordings in GigRig never removes it.\n") ? juce::String() : "The drive would not take the note.";

    return ! mark.existsAsFile() || mark.deleteFile() ? juce::String() : "The drive would not let go of the note.";
}

juce::String RecordingLibrary::remove (const juce::String& name)
{
    const auto folder = folderOf (name);

    if (folder == juce::File())
        return "There is no recording called \"" + name + "\".";

    if (folder.getChildFile (keepFile).existsAsFile())
        return "This recording is kept. Switch Keep off first if it really should go.";

    if (busy != nullptr)
        if (const auto why = busy (name); why.isNotEmpty())
            return why;

    if (running.load())
        return "Recordings are being copied. Wait for the copy to finish.";

    return folder.deleteRecursively() ? juce::String() : "Some of its files could not be deleted.";
}

juce::String RecordingLibrary::clear (bool copiedOnly)
{
    if (running.load())
        return "Recordings are being copied. Wait for the copy to finish.";

    int gone = 0, kept = 0, skipped = 0;
    juce::int64 freed = 0;

    for (const auto& i : list())
    {
        if (i.kept)                                         { ++kept; continue; }
        if (copiedOnly && ! i.copied)                       { ++skipped; continue; }
        if (busy != nullptr && busy (i.name).isNotEmpty())  { ++skipped; continue; }

        if (i.folder.deleteRecursively())
        {
            ++gone;
            freed += i.bytes;
        }
        else
        {
            ++skipped;
        }
    }

    juce::String t;
    t << "Cleared " << gone << (gone == 1 ? " recording" : " recordings") << " (" << gb (freed) << ").";

    if (kept > 0)       t << " " << kept << " kept.";
    if (skipped > 0)    t << " " << skipped << (copiedOnly ? " left because not copied yet or in use." : " left because in use.");

    return t;
}

//==============================================================================
std::vector<RecordingLibrary::Drive> RecordingLibrary::drives() const
{
    std::vector<juce::File> roots;

    if (useDrivesOverride)
    {
        roots = drivesOverride;
    }
    else
    {
       #if JUCE_LINUX
        // Where the desktop mounts a drive that is plugged in.
        const auto user = juce::SystemStats::getLogonName();

        for (const auto& base : { juce::File ("/media/" + user), juce::File ("/run/media/" + user), juce::File ("/media") })
            if (base.isDirectory())
                for (const auto& f : base.findChildFiles (juce::File::findDirectories, false))
                    if (f.getFileName() != user && f.hasWriteAccess())
                        roots.push_back (f);
       #else
        juce::Array<juce::File> all;
        juce::File::findFileSystemRoots (all);

        for (const auto& f : all)
            if (f.isOnRemovableDrive() && f.isDirectory())
                roots.push_back (f);
       #endif
    }

    std::vector<Drive> found;
    const auto home = rootNow();

    for (const auto& r : roots)
    {
        if (! r.isDirectory() || (home.isAChildOf (r) && ! useDrivesOverride))
            continue;

        Drive d;
        d.root = r;
        d.name = r.getVolumeLabel().isNotEmpty() && ! useDrivesOverride ? r.getVolumeLabel() : r.getFileName();

        if (d.name.isEmpty())
            d.name = r.getFullPathName();

        d.freeGb = (double) r.getBytesFreeOnVolume() / 1.0e9;

        if (std::none_of (found.begin(), found.end(), [&] (const Drive& o) { return o.root == d.root || o.name == d.name; }))
            found.push_back (d);
    }

    return found;
}

juce::String RecordingLibrary::copy (const juce::StringArray& names, const juce::String& driveName)
{
    if (running.load() || isThreadRunning())
        return "A copy is already running.";

    if (copyBlocked != nullptr)
        if (const auto why = copyBlocked(); why.isNotEmpty())
            return why;

    const auto all = drives();
    const auto drive = std::find_if (all.begin(), all.end(), [&] (const Drive& d) { return d.name == driveName; });

    if (drive == all.end())
        return all.empty() ? "No USB drive is plugged in." : "There is no drive called \"" + driveName + "\".";

    std::vector<Job> wanted;
    juce::int64 total = 0;

    for (const auto& i : list())
    {
        if (names.isEmpty() ? i.copied : ! names.contains (i.name))
            continue;

        if (busy != nullptr && busy (i.name).isNotEmpty())
        {
            if (! names.isEmpty())
                return "\"" + i.name + "\": " + busy (i.name);

            continue;
        }

        wanted.push_back ({ i.folder, drive->root.getChildFile (driveFolder).getChildFile (i.name), i.name, i.bytes });
        total += i.bytes;
    }

    if (wanted.empty())
        return names.isEmpty() ? "Every recording has already been copied." : "There is no recording by that name.";

    if ((double) total / 1.0e9 > drive->freeGb - 0.05)
        return "The drive \"" + drive->name + "\" has " + juce::String (drive->freeGb, 1) + " GB free and this needs " + gb (total) + ".";

    stopThread (2000);
    jobs = std::move (wanted);
    jobDrive = drive->name;
    totalBytes = juce::jmax ((juce::int64) 1, total);
    doneBytes.store (0);

    {
        const juce::ScopedLock guard (textLock);
        nowText = "Starting.";
        resultText.clear();
        resultFailed = false;
    }

    running.store (true);
    startThread();
    return {};
}

void RecordingLibrary::cancelCopy()
{
    if (isThreadRunning())
        signalThreadShouldExit();
}

RecordingLibrary::Progress RecordingLibrary::progress() const
{
    Progress p;
    p.running = running.load();
    p.done = juce::jlimit (0.0, 1.0, (double) doneBytes.load() / (double) juce::jmax ((juce::int64) 1, totalBytes));
    const juce::ScopedLock guard (textLock);
    p.now = nowText;
    p.result = resultText;
    p.failed = resultFailed;
    return p;
}

bool RecordingLibrary::waitForCopy (int milliseconds)
{
    for (int waited = 0; waited < milliseconds && running.load(); waited += 5)
        juce::Thread::sleep (5);

    return ! running.load();
}

bool RecordingLibrary::copyOne (const juce::File& from, const juce::File& to, juce::String& error)
{
    constexpr int chunk = 1 << 20;
    juce::HeapBlock<char> a (chunk), b (chunk);
    const auto before = doneBytes.load();

    {
        juce::FileInputStream in (from);
        to.deleteFile();
        juce::FileOutputStream out (to);

        if (! in.openedOk() || ! out.openedOk())
        {
            error = "\"" + from.getFileName() + "\" could not be " + (in.openedOk() ? "written to the drive" : "read") + ".";
            return false;
        }

        while (! in.isExhausted())
        {
            if (threadShouldExit())
            {
                error = "The copy was stopped.";
                return false;
            }

            const auto n = in.read (a.getData(), chunk);

            if (n <= 0)
                break;

            if (! out.write (a.getData(), (size_t) n))
            {
                error = "The drive stopped taking \"" + from.getFileName() + "\" (is it full, or was it pulled out?).";
                return false;
            }

            doneBytes.fetch_add (n / 2);        // half for the copy, half for the check
        }

        out.flush();

        if (out.getStatus().failed())
        {
            error = "The drive stopped taking \"" + from.getFileName() + "\".";
            return false;
        }
    }

    // ---- the check: read both back and compare every byte ----
    juce::FileInputStream one (from), two (to);

    if (! one.openedOk() || ! two.openedOk() || one.getTotalLength() != two.getTotalLength())
    {
        error = "The copy of \"" + from.getFileName() + "\" is not the same size as the original.";
        return false;
    }

    while (! one.isExhausted())
    {
        if (threadShouldExit())
        {
            error = "The copy was stopped.";
            return false;
        }

        const auto n = one.read (a.getData(), chunk);
        const auto m = two.read (b.getData(), chunk);

        if (n != m || (n > 0 && std::memcmp (a.getData(), b.getData(), (size_t) n) != 0))
        {
            error = "The copy of \"" + from.getFileName() + "\" does not match the original.";
            return false;
        }

        if (n <= 0)
            break;

        doneBytes.fetch_add (n - n / 2);
    }

    doneBytes.store (before + from.getSize());
    return true;
}

void RecordingLibrary::run()
{
    int copied = 0;
    juce::String error;

    for (size_t j = 0; j < jobs.size() && error.isEmpty(); ++j)
    {
        const auto& job = jobs[j];

        {
            const juce::ScopedLock guard (textLock);
            nowText = "Copying " + juce::String ((int) j + 1) + " of " + juce::String ((int) jobs.size()) + ": " + job.name;
        }

        if (! job.to.createDirectory())
        {
            error = "The drive would not make a folder for \"" + job.name + "\".";
            break;
        }

        for (const auto& f : job.from.findChildFiles (juce::File::findFiles, false))
        {
            if (f.getFileName() == copiedFile)
                continue;       // the copy does not need the note that says it was copied

            if (! copyOne (f, job.to.getChildFile (f.getFileName()), error))
                break;
        }

        if (error.isEmpty())
        {
            job.from.getChildFile (copiedFile).replaceWithText ("Copied to the drive \"" + jobDrive + "\" on "
                + juce::Time::getCurrentTime().formatted ("%d %b %Y at %H:%M") + ". Every file was read back and matched.\n");
            ++copied;
        }
    }

    {
        const juce::ScopedLock guard (textLock);
        resultFailed = error.isNotEmpty();
        resultText = error.isEmpty()
            ? "Copied " + juce::String (copied) + (copied == 1 ? " recording" : " recordings") + " to \"" + jobDrive + "\", into the folder \"" + driveFolder + "\". Every file was read back and matched."
            : "The copy stopped: " + error + " " + juce::String (copied) + (copied == 1 ? " recording was" : " recordings were") + " copied and checked before that.";
        nowText.clear();
    }

    running.store (false);
}

juce::String RecordingLibrary::reportText() const
{
    juce::String t;
    t << "RECORDINGS ON THE DRIVE\n";
    const auto all = list();
    juce::int64 bytes = 0;
    int kept = 0, copied = 0, playable = 0;

    for (const auto& i : all)
    {
        bytes += i.bytes;
        kept += i.kept ? 1 : 0;
        copied += i.copied ? 1 : 0;
        playable += i.soundcheckSockets > 0 ? 1 : 0;
    }

    t << all.size() << (all.size() == 1 ? " recording, " : " recordings, ") << gb (bytes) << ". " << juce::String (freeGb(), 1) << " GB free. "
      << kept << " kept, " << copied << " copied to USB, " << playable << " can be played as a virtual soundcheck.\n";

    for (size_t i = 0; i < juce::jmin ((size_t) 5, all.size()); ++i)
        t << "  " << all[i].name << ": " << all[i].files << " files, " << juce::String (all[i].seconds / 60.0, 1) << " minutes, " << gb (all[i].bytes)
          << (all[i].kept ? ", kept" : "") << (all[i].copied ? ", copied" : "") << (all[i].soundcheckSockets > 0 ? ", soundcheck on " + juce::String (all[i].soundcheckSockets) + " sockets" : ", no raw files") << "\n";

    juce::StringArray names;
    for (const auto& d : drives())
        names.add (d.name + " (" + juce::String (d.freeGb, 1) + " GB free)");

    t << "USB drives: " << (names.isEmpty() ? juce::String ("none plugged in") : names.joinIntoString ("; ")) << ".\n";

    if (const auto p = progress(); p.running)
        t << "Copying now: " << p.now << " (" << juce::roundToInt (p.done * 100.0) << "%).\n";
    else if (p.result.isNotEmpty())
        t << "Last copy: " << p.result << "\n";

    return t;
}

} // namespace gigrig
