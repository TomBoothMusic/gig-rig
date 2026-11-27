#include "RemoteServer.h"
#include "BinaryData.h"

namespace gigrig
{

namespace
{
    constexpr int maxConnections = 24;
    constexpr int maxBodyBytes = 512 * 1024;
    constexpr int activeWindowMs = 4000;        // the JSON is only kept fresh while a page asked for something this recently

    const char* typeFor (const juce::String& name)
    {
        if (name.endsWith (".html"))    return "text/html; charset=utf-8";
        if (name.endsWith (".js"))      return "application/javascript; charset=utf-8";
        if (name.endsWith (".css"))     return "text/css; charset=utf-8";
        if (name.endsWith (".json"))    return "application/json; charset=utf-8";
        if (name.endsWith (".svg"))     return "image/svg+xml";
        return "text/plain; charset=utf-8";
    }

    RemoteServer::Response text (int status, const juce::String& type, const juce::String& body)
    {
        RemoteServer::Response r;
        r.status = status;
        r.type = type;
        r.body.append (body.toRawUTF8(), body.getNumBytesAsUTF8());
        return r;
    }

    RemoteServer::Response json (const juce::String& body)     { return text (200, "application/json; charset=utf-8", body); }

    const char* reason (int status)
    {
        switch (status)
        {
            case 200: return "OK";
            case 400: return "Bad Request";
            case 404: return "Not Found";
            case 413: return "Payload Too Large";
            default:  return "Error";
        }
    }

    bool writeAll (juce::StreamingSocket& socket, const void* data, int size)
    {
        const auto* bytes = static_cast<const char*> (data);

        while (size > 0)
        {
            if (socket.waitUntilReady (false, 5000) != 1)
                return false;

            const int sent = socket.write (bytes, size);

            if (sent <= 0)
                return false;

            bytes += sent;
            size -= sent;
        }

        return true;
    }
}

RemoteServer::RemoteServer (RemoteApi& a) : juce::Thread ("Remote screen"), api (a)
{
    api.onRigChanged = [this] { rigChanged = true; };
}

RemoteServer::~RemoteServer()
{
    alive->store (false);
    api.onRigChanged = nullptr;
    stop();
}

juce::String RemoteServer::start (int wantedPort)
{
    stop();

    if (! listener.createListener (wantedPort))
        return "Could not open port " + juce::String (wantedPort) + ". Another program may be using it: try another number.";

    boundPort.store (listener.getBoundPort());
    listening.store (true);
    refreshNow();
    startThread();
    startTimer (100);
    return {};
}

void RemoteServer::stop()
{
    stopTimer();
    listening.store (false);
    signalThreadShouldExit();
    listener.close();
    stopThread (3000);

    std::vector<Connection> waiting;
    {
        const std::lock_guard<std::mutex> lock (threadLock);
        waiting.swap (threads);
    }

    // A connection thread may be waiting for the message thread (this one) to carry out a command;
    // it gives up after a moment once 'listening' is false, so these joins are short.
    for (auto& c : waiting)
        if (c.thread.joinable())
            c.thread.join();

    boundPort.store (0);
}

juce::StringArray RemoteServer::addresses() const
{
    juce::StringArray list;

    for (const auto& a : juce::IPAddress::getAllAddresses (false))
        if (a != juce::IPAddress::local (false) && ! a.isNull())
            list.add ("http://" + a.toString() + ":" + juce::String (port()));

    return list;
}

double RemoteServer::secondsSinceRequest() const
{
    const auto last = lastRequestMs.load();
    return last == 0 ? -1.0 : (double) (juce::Time::currentTimeMillis() - last) / 1000.0;
}

juce::String RemoteServer::reportText() const
{
    juce::String t = "--- Remote screen (phone and tablet) ---\n";

    if (! isRunning())
        return t + "Off. No port is open. Switch it on from the Remote screen tab.\n";

    t << "On, port " << port() << ". Addresses to type into a phone or tablet on the same network: "
      << (addresses().isEmpty() ? juce::String ("none (this computer is not on a network)") : addresses().joinIntoString (", ")) << "\n";

    const auto since = secondsSinceRequest();
    t << "Requests answered: " << requestsServed() << ", commands carried out: " << api.commandsDone()
      << (since < 0.0 ? juce::String (". No page has connected yet.") : ". A page last asked " + juce::String (since, 1) + " s ago.") << "\n";
    return t;
}

//==============================================================================
juce::File RemoteServer::settingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("GigRig").getChildFile ("remote.txt");
}

RemoteServer::Settings RemoteServer::loadSettings()
{
    Settings s;

    for (const auto& line : juce::StringArray::fromLines (settingsFile().loadFileAsString()))
    {
        const auto key = line.upToFirstOccurrenceOf ("=", false, false).trim();
        const auto value = line.fromFirstOccurrenceOf ("=", false, false).trim();

        if (key == "on")        s.on = value.getIntValue() != 0;
        if (key == "port")      s.port = juce::jlimit (1024, 65535, value.getIntValue());
    }

    return s;
}

void RemoteServer::saveSettings (const Settings& s)
{
    settingsFile().getParentDirectory().createDirectory();
    settingsFile().replaceWithText ("# GigRig: the remote screen (docs/remote.md)\non = " + juce::String (s.on ? 1 : 0) + "\nport = " + juce::String (s.port) + "\n");
}

//==============================================================================
void RemoteServer::refreshNow()
{
    auto body = juce::JSON::toString (api.state(), true);
    auto live = juce::JSON::toString (api.live(), true);

    const std::lock_guard<std::mutex> lock (textLock);

    if (body != stateBody)
    {
        stateBody = std::move (body);
        ++stateVersion;
        stateText = "{\"v\":" + juce::String (stateVersion) + ",\"s\":" + stateBody + "}";
    }

    liveText = "{\"v\":" + juce::String (stateVersion) + ",\"l\":" + live + "}";
}

void RemoteServer::timerCallback()
{
    ++ticks;

    if (rigChanged && ticks % 3 == 0)
    {
        rigChanged = false;

        if (onRigChanged != nullptr)
            onRigChanged();
    }

    // Nobody looking: do nothing at all.
    if (lastRequestMs.load() == 0 || juce::Time::currentTimeMillis() - lastRequestMs.load() > activeWindowMs)
        return;

    if (ticks % 5 == 0)
    {
        refreshNow();       // the whole state twice a second: this is how a change made on the rig's own window reaches the page
        return;
    }

    auto live = juce::JSON::toString (api.live(), true);
    const std::lock_guard<std::mutex> lock (textLock);
    liveText = "{\"v\":" + juce::String (stateVersion) + ",\"l\":" + live + "}";
}

juce::var RemoteServer::onMessageThread (const juce::var& command)
{
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        auto result = api.command (command);
        refreshNow();
        return result;
    }

    struct Job
    {
        juce::WaitableEvent finished;
        juce::var result;
    };

    auto job = std::make_shared<Job>();

    juce::MessageManager::callAsync ([this, job, command, stillThere = alive]
    {
        if (stillThere->load())
        {
            job->result = api.command (command);
            refreshNow();
        }

        job->finished.signal();
    });

    for (int waited = 0; waited < 5000; waited += 100)
    {
        if (job->finished.wait (100))
            return job->result;

        if (! listening.load())
            break;
    }

    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", false);
    o->setProperty ("error", "The rig did not answer in time. It may be busy; try again.");
    return juce::var (o);
}

RemoteServer::Response RemoteServer::handle (const juce::String& method, const juce::String& fullPath, const juce::String& body)
{
    ++requests;
    lastRequestMs.store (juce::Time::currentTimeMillis());
    const auto path = fullPath.upToFirstOccurrenceOf ("?", false, false);

    if (method != "GET" && method != "POST")
        return text (405, "text/plain; charset=utf-8", "This page only reads (GET) and sends commands (POST).");

    if (path == "/api/live" || path == "/api/state")
    {
        const std::lock_guard<std::mutex> lock (textLock);
        return json (path == "/api/live" ? liveText : stateText);
    }

    if (path == "/api/cmd")
    {
        if (method != "POST")
            return text (400, "text/plain; charset=utf-8", "Send commands with POST.");

        const auto command = juce::JSON::parse (body);

        if (! command.isObject())
            return json ("{\"ok\":false,\"error\":\"The command did not arrive whole.\"}");

        // A switch press goes straight in, like a click on the window's own switches.
        if (command.getProperty ("op", {}).toString() == "pedal")
        {
            api.pedal ((int) command.getProperty ("sw", -1), (bool) command.getProperty ("down", false));
            return json ("{\"ok\":true,\"note\":\"\"}");
        }

        auto result = onMessageThread (command);
        int v = 0;
        {
            const std::lock_guard<std::mutex> lock (textLock);
            v = stateVersion;
        }

        if (auto* o = result.getDynamicObject())
            o->setProperty ("v", v);

        return json (juce::JSON::toString (result, true));
    }

    if (path == "/api/report")
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("op", "report");
        const auto result = onMessageThread (juce::var (o));
        return text (200, "text/plain; charset=utf-8", result.getProperty ("note", {}).toString());
    }

    // The page itself: the files in remote/, built into the app.
    const auto wanted = path == "/" ? juce::String ("index.html") : path.fromLastOccurrenceOf ("/", false, false);

    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        if (wanted == BinaryData::originalFilenames[i])
        {
            // Only the page's own files are handed out, never the amp models or presets built in beside them.
            if (! (wanted.endsWith (".html") || wanted.endsWith (".js") || wanted.endsWith (".css") || wanted.endsWith (".svg")))
                break;

            int size = 0;
            const char* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
            Response r;
            r.type = typeFor (wanted);
            r.body.append (data, (size_t) size);
            return r;
        }

    return text (404, "text/plain; charset=utf-8", "Nothing here. The control page is at /");
}

//==============================================================================
void RemoteServer::run()
{
    while (! threadShouldExit())
    {
        std::unique_ptr<juce::StreamingSocket> connection (listener.waitForNextConnection());

        if (connection == nullptr)
        {
            if (! threadShouldExit())
                wait (50);

            continue;
        }

        if (openConnections.load() >= maxConnections)
            continue;       // too many at once: this one is closed again

        ++openConnections;
        const std::lock_guard<std::mutex> lock (threadLock);

        // Finished threads are tidied away as new ones arrive.
        for (auto it = threads.begin(); it != threads.end();)
        {
            if (it->finished->load())
            {
                it->thread.join();
                it = threads.erase (it);
            }
            else
            {
                ++it;
            }
        }

        auto finished = std::make_shared<std::atomic<bool>> (false);
        threads.push_back ({ std::thread ([this, finished, c = std::move (connection)]() mutable
        {
            serve (std::move (c));
            --openConnections;
            finished->store (true);
        }), finished });
    }
}

void RemoteServer::serve (std::unique_ptr<juce::StreamingSocket> socket)
{
    juce::MemoryBlock pending;
    char chunk[4096];
    int idleMs = 0;

    while (listening.load() && socket->isConnected())
    {
        // ---- read one request: headers, then as many body bytes as Content-Length says ----
        int headerEnd = -1;

        for (;;)
        {
            const auto view = juce::String::fromUTF8 (static_cast<const char*> (pending.getData()), (int) pending.getSize());
            headerEnd = view.indexOf ("\r\n\r\n");

            if (headerEnd >= 0 || (int) pending.getSize() > 16384)
                break;

            const int ready = socket->waitUntilReady (true, 250);

            if (ready < 0 || ! listening.load())
                return;

            if (ready == 0)
            {
                idleMs += 250;

                if (idleMs > 30000)
                    return;         // a page that has gone quiet: let the connection go

                continue;
            }

            const int got = socket->read (chunk, sizeof (chunk), false);

            if (got <= 0)
                return;

            idleMs = 0;
            pending.append (chunk, (size_t) got);
        }

        if (headerEnd < 0)
            return;

        // Headers are plain ASCII, so characters and bytes are the same up to here.
        const auto head = juce::String::fromUTF8 (static_cast<const char*> (pending.getData()), headerEnd);
        const auto lines = juce::StringArray::fromLines (head);
        const auto first = juce::StringArray::fromTokens (lines[0], " ", "");
        int length = 0;
        bool close = false;

        for (int i = 1; i < lines.size(); ++i)
        {
            const auto name = lines[i].upToFirstOccurrenceOf (":", false, false).trim().toLowerCase();
            const auto value = lines[i].fromFirstOccurrenceOf (":", false, false).trim();

            if (name == "content-length")   length = value.getIntValue();
            if (name == "connection")       close = value.equalsIgnoreCase ("close");
        }

        Response response;

        if (first.size() < 2)
        {
            response = text (400, "text/plain; charset=utf-8", "Not understood.");
            close = true;
        }
        else if (length < 0 || length > maxBodyBytes)
        {
            response = text (413, "text/plain; charset=utf-8", "Too much.");
            close = true;
        }
        else
        {
            const size_t bodyStart = (size_t) headerEnd + 4;

            while (pending.getSize() < bodyStart + (size_t) length)
            {
                if (socket->waitUntilReady (true, 3000) != 1)
                    return;

                const int got = socket->read (chunk, sizeof (chunk), false);

                if (got <= 0)
                    return;

                pending.append (chunk, (size_t) got);
            }

            const auto body = juce::String::fromUTF8 (static_cast<const char*> (pending.getData()) + bodyStart, length);
            pending.removeSection (0, bodyStart + (size_t) length);
            response = handle (first[0], first[1], body);
        }

        juce::String header;
        header << "HTTP/1.1 " << response.status << " " << reason (response.status) << "\r\n"
               << "Content-Type: " << response.type << "\r\n"
               << "Content-Length: " << (int) response.body.getSize() << "\r\n"
               << "Cache-Control: no-store\r\n"
               << "Connection: " << (close ? "close" : "keep-alive") << "\r\n\r\n";

        if (! writeAll (*socket, header.toRawUTF8(), (int) header.getNumBytesAsUTF8())
            || ! writeAll (*socket, response.body.getData(), (int) response.body.getSize())
            || close)
            return;
    }
}

} // namespace gigrig
