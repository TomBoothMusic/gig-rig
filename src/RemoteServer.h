#pragma once

#include <juce_events/juce_events.h>

#include "RemoteApi.h"

#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace gigrig
{

/** The remote screen's door (built 27 Nov 2026; the guide is docs/remote.md).

    A very small web server inside the app. A phone, tablet or laptop on the same network opens
    http://<the rig's address>:<port>/ in its browser and gets the control page (the files in
    remote/, built into the app), which then asks for:

        GET  /api/state    everything that changes only when somebody changes it, with a version: {"v":7,"s":{...}}
        GET  /api/live     what moves by itself (tracks, beat, meters) and the same version:      {"v":7,"l":{...}}
        POST /api/cmd      one thing to do, as JSON (RemoteApi::command)
        GET  /api/report   the Copy report text

    OFF BY DEFAULT, and switched on from the Remote screen tab. Switched off, no port is open and
    nothing here runs, so the app is exactly what it was before this existed.

    Threads: one thread waits for connections and each connection gets its own. They never touch
    the rig: the message thread's timer writes the two JSON texts into strings the connection
    threads hand out, and a command is passed to the message thread and waited for. The one
    exception is a switch press ("pedal"), which goes straight to PedalInput as a click on the
    window's own switches does, so it is not held up behind the screen's drawing.

    There is no password: anyone on the same network can open the page. The rig's own Wi-Fi
    should have one. */
class RemoteServer final : private juce::Thread,
                           private juce::Timer
{
public:
    explicit RemoteServer (RemoteApi&);
    ~RemoteServer() override;

    static constexpr int defaultPort = 8080;

    /** Opens the door. Returns an error, or an empty string. Port 0 lets the system choose (the self-test does). */
    juce::String start (int port);
    void stop();
    bool isRunning() const noexcept             { return listening.load(); }
    int port() const noexcept                   { return boundPort.load(); }

    /** "http://192.168.1.20:8080" for every network this computer is on. */
    juce::StringArray addresses() const;

    /** Message thread: writes the two JSON texts afresh. The timer does this by itself; the self-test calls it. */
    void refreshNow();

    /** Seconds since a page last asked for anything, or -1 if none has. */
    double secondsSinceRequest() const;
    int requestsServed() const noexcept         { return requests.load(); }

    /** The remote screen's part of the report. */
    juce::String reportText() const;

    /** The on/off and port the app remembers (remote.txt beside the other settings). */
    struct Settings { bool on = false; int port = defaultPort; };
    static juce::File settingsFile();
    static Settings loadSettings();
    static void saveSettings (const Settings&);

    /** Called on the message thread, a few times a second at most, after the page changed the rig. */
    std::function<void()> onRigChanged;

    /** One request worked out into an answer: used by the connection threads, and by the self-test without a network. */
    struct Response { int status = 200; juce::String type; juce::MemoryBlock body; };
    Response handle (const juce::String& method, const juce::String& path, const juce::String& body);

private:
    void run() override;
    void timerCallback() override;
    void serve (std::unique_ptr<juce::StreamingSocket>);
    juce::var onMessageThread (const juce::var& command);

    RemoteApi& api;
    juce::StreamingSocket listener;
    std::atomic<bool> listening { false };
    std::atomic<int> boundPort { 0 }, requests { 0 }, openConnections { 0 };
    std::atomic<juce::int64> lastRequestMs { 0 };

    mutable std::mutex textLock;
    juce::String stateBody, stateText, liveText;
    int stateVersion = 0, ticks = 0;
    bool rigChanged = false;

    std::mutex threadLock;
    struct Connection { std::thread thread; std::shared_ptr<std::atomic<bool>> finished; };
    std::vector<Connection> threads;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RemoteServer)
};

} // namespace gigrig
