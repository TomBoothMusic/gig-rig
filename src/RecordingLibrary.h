#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <functional>
#include <vector>

namespace gigrig
{

/** The recordings on the rig's drive, and what can be done with them (built 28 Nov 2026,
    docs/recording.md, "The Recordings tab"): keep one for good, copy them to a USB drive and
    check the copy, and clear old ones to make room.

    A recording is a folder in Documents/GigRig/Recordings. Two small files in a folder are
    this class's memory, so they survive a new build and a new computer. **Their names are on
    Tom's disk: never rename them.**
      - "Keep.txt": this recording is kept (Tom, 20 Nov 2026: one soundcheck recording per act
        stays on the drive for good). Clearing never removes it.
      - "Copied to USB.txt": every file was copied to a drive and read back the same.

    Copying runs on a background thread. No audio thread comes near any of this. */
class RecordingLibrary final : private juce::Thread
{
public:
    struct Info
    {
        juce::String name;              // the folder's name: the date and time it was started
        juce::File folder;
        int files = 0;                  // sound files
        juce::int64 bytes = 0;
        double seconds = 0.0;
        bool kept = false, copied = false;
        int soundcheckSockets = 0;      // raw files a virtual soundcheck can play (0: it cannot be played back)
    };

    struct Drive
    {
        juce::String name;
        juce::File root;
        double freeGb = 0.0;
    };

    struct Progress
    {
        bool running = false;
        double done = 0.0;              // 0..1
        juce::String now;               // "Copying 2 of 3: 2026-11-28 20-15-00"
        juce::String result;            // what the last copy came to (empty until one has finished)
        bool failed = false;
    };

    RecordingLibrary();
    ~RecordingLibrary() override;

    /** Where the recordings are (the self-test points it somewhere else). */
    std::function<juce::File()> root;
    /** Why a recording cannot be touched just now ("It is being recorded."), or empty. */
    std::function<juce::String (const juce::String& name)> busy;
    /** Why nothing may be copied just now ("Stop the recording first."), or empty. */
    std::function<juce::String()> copyBlocked;

    /** Every recording, newest first. Reads the drive: not for a fast timer. */
    std::vector<Info> list() const;
    double freeGb() const;

    juce::String setKept (const juce::String& name, bool keep);
    /** Deletes one recording for good. A kept one must be un-kept first. Returns an error, or empty. */
    juce::String remove (const juce::String& name);
    /** Deletes every recording that is not kept (copiedOnly: and has been copied to USB). Says what it did. */
    juce::String clear (bool copiedOnly);

    /** The USB drives plugged in now. */
    std::vector<Drive> drives() const;
    /** Set, these are the drives instead (the self-test does). */
    static inline std::vector<juce::File> drivesOverride;
    static inline bool useDrivesOverride = false;

    /** Starts copying recordings (all that are not yet copied when 'names' is empty) to a drive, into
        its folder "GigRig recordings". Each file is read back and compared. Returns an error, or empty. */
    juce::String copy (const juce::StringArray& names, const juce::String& driveName);
    void cancelCopy();
    Progress progress() const;
    /** For the self-test: waits for a copy to finish. */
    bool waitForCopy (int milliseconds);

    juce::String reportText() const;

    static constexpr const char* keepFile = "Keep.txt";
    static constexpr const char* copiedFile = "Copied to USB.txt";
    static constexpr const char* driveFolder = "GigRig recordings";

private:
    void run() override;
    bool copyOne (const juce::File& from, const juce::File& to, juce::String& error);
    juce::File rootNow() const;
    juce::File folderOf (const juce::String& name) const;

    struct Job { juce::File from, to; juce::String name; juce::int64 bytes = 0; };
    std::vector<Job> jobs;                      // set before the thread starts
    juce::String jobDrive;
    juce::int64 totalBytes = 0;
    std::atomic<juce::int64> doneBytes { 0 };
    std::atomic<bool> running { false };

    mutable juce::CriticalSection textLock;
    juce::String nowText, resultText;
    bool resultFailed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecordingLibrary)
};

} // namespace gigrig
