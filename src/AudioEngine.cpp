#include "AudioEngine.h"
#include "DeviceReport.h"

#if JUCE_LINUX
 #include <cerrno>
 #include <cstring>
 #include <pthread.h>
 #include <sched.h>
#endif

namespace gigrig
{

namespace
{
    constexpr int pingsPerRow = 6;
    constexpr int settleTicks = 10;        // half a second for a freshly opened device to steady
    constexpr int gapTicks = 2;
    constexpr int giveUpTicks = 40;
    constexpr int scratchSize = 16384;
    constexpr int cqAnalogueInputs = 10;   // CQ12T: USB channels 1 to 10 are the input sockets
    constexpr float toneLevel = 0.125f;    // -18 dBFS
    constexpr double toneHz = 440.0;
    constexpr double loopSecondsReal = 120.0;       // the longest loop a track can hold on a real device
    constexpr double loopSecondsSimulated = 20.0;
    constexpr double cqThroughMs = 0.7;             // the CQ's own delay from an input socket to its outputs
    constexpr double cqMixMs = 0.3;                 // the part of that which is its mixing, not its converters
    constexpr double cableSlackMs = 1.5;            // a cable is at most this much later than the copy inside the mixer (measured: 0.35 ms)

    int usbPairFirstReturn (int numOutputs) noexcept    { return numOutputs >= 16 ? 4 : 0; }

    void raisePeak (std::atomic<float>& slot, float peak) noexcept
    {
        if (peak > slot.load (std::memory_order_relaxed))
            slot.store (peak, std::memory_order_relaxed);
    }

    float peakOf (const float* data, int numSamples) noexcept
    {
        const auto range = juce::FloatVectorOperations::findMinAndMax (data, numSamples);
        return juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd()));
    }

    juce::String ms (double value)      { return juce::String (value, 2) + " ms"; }
}

int returnToOutput (int returnIndex, int numOutputs) noexcept
{
    if (returnIndex < 0 || returnIndex >= numReturns)
        return -1;

    if (numOutputs >= 16)
        return 8 + returnIndex;

    return returnIndex < numOutputs ? returnIndex : -1;
}

juce::String returnName (int returnIndex)
{
    static const char* const lands[numReturns] = { "CQ channel 9", "CQ channel 10", "ST1 left", "ST1 right",
                                                   "USB left", "USB right", "Bluetooth left", "Bluetooth right" };

    if (returnIndex < 0 || returnIndex >= numReturns)
        return {};

    return "Return " + juce::String (returnIndex + 1) + " (" + lands[returnIndex] + ")";
}

juce::String inputName (int inputIndex, bool isCQ)
{
    if (! isCQ)
        return "input " + juce::String (inputIndex + 1);

    return inputIndex < cqAnalogueInputs ? "input socket " + juce::String (inputIndex + 1)
                                         : "inside the mixer (USB channel " + juce::String (inputIndex + 1) + ")";
}

LatencyRow summarisePings (const std::vector<std::vector<PingHit>>& pings, int numInputs, bool isCQ, double sampleRate)
{
    LatencyRow row;
    row.pingsSent = (int) pings.size();

    for (int input = 0; input < numInputs; ++input)
    {
        std::vector<int> delays;
        float level = -100.0f;

        for (const auto& ping : pings)
            for (const auto& hit : ping)
                if (hit.input == input)
                {
                    delays.push_back (hit.delaySamples);
                    level = juce::jmax (level, hit.levelDb);
                }

        // Heard on most of the pings, so one stray noise doesn't count.
        if (delays.empty() || (int) delays.size() * 2 < row.pingsSent)
            continue;

        std::sort (delays.begin(), delays.end());

        InputTiming timing;
        timing.input = input;
        timing.pingsHeard = (int) delays.size();
        timing.medianSamples = delays[delays.size() / 2];
        timing.minSamples = delays.front();
        timing.maxSamples = delays.back();
        timing.levelDb = level;
        row.heard.push_back (timing);
    }

    // The answer is the loudest input socket: that is the loopback cable.
    // (A microphone picking the ping up from a speaker is later and quieter.)
    // If no socket heard it, fall back to the copy that came back inside the mixer.
    const auto loudest = [&row] (bool wantAnalogue, bool cq)
    {
        int found = -1;

        for (int i = 0; i < (int) row.heard.size(); ++i)
        {
            const bool analogue = ! cq || row.heard[(size_t) i].input < cqAnalogueInputs;

            if (analogue == wantAnalogue && (found < 0 || row.heard[(size_t) i].levelDb > row.heard[(size_t) found].levelDb))
                found = i;
        }

        return found;
    };

    row.best = loudest (true, isCQ);
    row.bestIsAnalogue = row.best >= 0;

    const auto insideMixer = isCQ ? loudest (false, true) : -1;

    if (row.best >= 0 && insideMixer >= 0)
    {
        // A real cable comes back well under a millisecond after the copy inside the mixer.
        // Anything much later went through the air: a microphone or pickup hearing a speaker.
        // That is not the round trip, so it is set aside and the mixer's own copy is used.
        const auto later = row.heard[(size_t) row.best].medianSamples - row.heard[(size_t) insideMixer].medianSamples;

        if (later > juce::roundToInt (cableSlackMs * sampleRate / 1000.0))
        {
            row.acousticInput = row.heard[(size_t) row.best].input;
            row.acousticSamples = row.heard[(size_t) row.best].medianSamples;
            row.best = -1;
            row.bestIsAnalogue = false;
        }
    }

    if (row.best < 0)
        row.best = insideMixer >= 0 ? insideMixer : loudest (false, isCQ);

    return row;
}

//==============================================================================
AudioEngine::AudioEngine()
{
    tunerCore.beforeTick = [this] { tellTunerTheGuitars(); };
    juce::AudioDeviceManager manager;
    manager.createAudioDeviceTypes (types);
}

juce::StringArray AudioEngine::tunerChannels (bool silent) const
{
    juce::StringArray names;
    const auto& setup = mixCore.setup();
    bool follows = false;       // the right side of a stereo pair does what its left side does

    for (int c = 0; c < numMixChannels; ++c)
    {
        const auto& ch = setup.channel[(size_t) c];
        const bool goes = c > 0 && setup.channel[(size_t) (c - 1)].link ? follows : tunerCore.goesSilent (ch.name);
        follows = goes;

        if (ch.on && goes == silent)
            names.add (ch.name);
    }

    return names;
}

void AudioEngine::tellTunerTheGuitars()
{
    const auto& setup = mixCore.setup();
    juce::uint32 channels = 0, sockets = 0;
    bool follows = false;

    for (int c = 0; c < numMixChannels; ++c)
    {
        const auto& ch = setup.channel[(size_t) c];
        const bool goes = c > 0 && setup.channel[(size_t) (c - 1)].link ? follows : tunerCore.goesSilent (ch.name);
        follows = goes;

        if (! ch.on || ! goes)
            continue;

        channels |= 1u << c;

        // The tuner listens to a socket as it arrives; the channel listens to it after its effects
        // chain, which may have come from another socket.
        bool fromChain = false;

        for (int k = 0; k < EffectsRack::numChains; ++k)
        {
            const auto r = fxRack.routing (k);

            if (r.on && (r.looperHears == -2 ? r.source : r.looperHears) == ch.socket && r.source >= 0 && r.source < 32)
            {
                sockets |= 1u << r.source;
                fromChain = true;
            }
        }

        if (! fromChain && ch.socket >= 0 && ch.socket < 32)
            sockets |= 1u << ch.socket;
    }

    mixCore.setTunerChannels (channels);

    // With the guitars silent and the microphone live, talking must not take the tuner off the guitar.
    tunerCore.setCandidates (mixCore.isOn() && tunerCore.silence() == Tuner::Silence::guitars ? sockets : 0u);
}

AudioEngine::~AudioEngine()
{
    close();
}

juce::Array<DeviceChoice> AudioEngine::listDevices()
{
    juce::Array<DeviceChoice> choices;

    for (auto* type : types)
    {
        type->scanForDevices();

        const auto inputs  = type->getDeviceNames (true);
        const auto outputs = type->getDeviceNames (false);

        for (const auto& name : outputs)
            if (! type->hasSeparateInputsAndOutputs() || inputs.contains (name))
                choices.add ({ type->getTypeName(), name });
    }

    return choices;
}

juce::String AudioEngine::open (const DeviceChoice& newChoice, int bufferSize, double sampleRate)
{
    startNudge = {};
    auto error = openOnce (newChoice, bufferSize, sampleRate);

    // The CQ's ASIO driver can come up with its output side sized for a much bigger buffer
    // (19.33 ms instead of 5.33 ms at 96 kHz / 64) when it is opened at the size it already
    // had. Opening at another size and coming back clears it (found with Tom, 22 Nov 2026).
    if (error.isEmpty() && device != nullptr && deviceIsCQ && newChoice.typeName == "ASIO")
    {
        const auto wanted = currentBufferSize;
        const auto before = device->getOutputLatencyInSamples();
        const auto other  = nudgeBufferSize (wanted, device->getAvailableBufferSizes());

        if (other > 0)
        {
            openOnce (newChoice, other, sampleRate);
            error = openOnce (newChoice, wanted, sampleRate);

            if (error.isEmpty() && device != nullptr)
            {
                const auto ms = [this] (int samples) { return juce::String (samples * 1000.0 / currentSampleRate, 2) + " ms"; };
                const auto after = device->getOutputLatencyInSamples();

                startNudge = "Start-up nudge: opened at " + juce::String (other) + " and back to " + juce::String (currentBufferSize)
                           + "; driver's output delay " + (after < before ? "came down from " + ms (before) + " to " + ms (after)
                                                                           : "was " + ms (before) + " before and " + ms (after) + " after");
            }
        }
    }

    return error;
}

int AudioEngine::nudgeBufferSize (int wanted, const juce::Array<int>& available)
{
    if (wanted <= 0)
        return 0;

    for (auto candidate : { wanted * 2, wanted / 2 })
        if (candidate > 0 && candidate != wanted && available.contains (candidate))
            return candidate;

    for (auto candidate : available)
        if (candidate != wanted)
            return candidate;

    return 0;
}

juce::String AudioEngine::openOnce (const DeviceChoice& newChoice, int bufferSize, double sampleRate)
{
    if (sampleRate > 0.0)
        wantedRate = sampleRate;

    if (device != nullptr)
    {
        device->stop();
        device->close();
        device.reset();
    }

    simulated = false;
    choice = newChoice;
    errorText = {};
    currentSampleRate = 0.0;
    currentBufferSize = currentInputs = currentOutputs = 0;

    juce::AudioIODeviceType* type = nullptr;

    for (auto* t : types)
        if (t->getTypeName() == choice.typeName)
            type = t;

    if (type == nullptr)
        return errorText = "This computer has no \"" + choice.typeName + "\" audio driver type.";

    std::unique_ptr<juce::AudioIODevice> opened (type->createDevice (choice.deviceName, choice.deviceName));

    if (opened == nullptr)
        return errorText = "\"" + choice.deviceName + "\" would not open. It may be unplugged, switched off "
                           "or in use by another program (close Reaper or any other audio program first).";

    const auto inputNames  = opened->getInputChannelNames();
    const auto outputNames = opened->getOutputChannelNames();

    if (inputNames.isEmpty() || outputNames.isEmpty())
        return errorText = "\"" + choice.deviceName + "\" reported no channels. It may be unplugged or switched off.";

    juce::BigInteger inputs, outputs;
    inputs.setRange (0, juce::jmin (32, inputNames.size()), true);
    outputs.setRange (0, juce::jmin (32, outputNames.size()), true);

    const auto rates = opened->getAvailableSampleRates();
    const auto rate = wantedRate > 0.0 && rates.contains (wantedRate) ? wantedRate
                    : rates.contains (48000.0) || rates.isEmpty() ? 48000.0 : rates[0];
    const auto size = bufferSize > 0 ? bufferSize : opened->getDefaultBufferSize();

    const auto error = opened->open (inputs, outputs, rate, size);

    if (error.isNotEmpty())
        return errorText = "\"" + choice.deviceName + "\" would not start: " + error;

    device = std::move (opened);
    rtState.store (0);
    deviceIsCQ = looksLikeCQ (choice.deviceName) && inputNames.size() >= 16 && outputNames.size() >= 16;
    device->start (this);   // calls audioDeviceAboutToStart, then audio begins
    return {};
}

juce::String AudioEngine::slowBufferWarning() const
{
    if (! isOpen() || currentBufferSize <= 256 || currentSampleRate <= 0.0 || job.active)
        return {};

    return "The buffer is " + juce::String (currentBufferSize) + " samples, so everything you play comes out about "
         + juce::String (juce::roundToInt (2.0 * currentBufferSize * 1000.0 / currentSampleRate)) + " ms late (it sounds like a room echo). "
           "Choose 64 in the Buffer box on the first tab.";
}

void AudioEngine::close()
{
    if (job.active)
    {
        probe.cancel();
        job.active = false;
        measuring.store (false);
    }

    if (device != nullptr)
    {
        device->stop();
        device->close();
        device.reset();
    }

    simulated = false;
    currentSampleRate = 0.0;
    currentBufferSize = currentInputs = currentOutputs = 0;
}

void AudioEngine::openSimulated (double rate, int block, int ins, int outs)
{
    close();
    simulated = true;
    deviceIsCQ = ins >= 16 && outs >= 16;
    choice = { "Simulated", "Simulated device" };
    prepare (rate, block, ins, outs);
}

void AudioEngine::prepare (double rate, int block, int ins, int outs)
{
    currentSampleRate = rate;
    currentBufferSize = block;
    currentInputs = ins;
    currentOutputs = outs;

    scratch.assign ((size_t) scratchSize, 0.0f);
    mix.assign ((size_t) scratchSize, 0.0f);
    kickBuf.assign ((size_t) scratchSize, 0.0f);
    kickSock.assign ((size_t) scratchSize, 0.0f);
    probe.prepare (rate, ins);
    glitch.prepare (rate, ins, deviceIsCQ ? cqAnalogueInputs : ins);
    loopCore.prepare (rate, simulated ? loopSecondsSimulated : loopSecondsReal);
    fxRack.prepare (rate, scratchSize, ins);
    mixCore.prepare (rate, scratchSize);
    updateLoopTiming();
    tonePhase = 0.0;
    hotSamples = 0;
    kickCore.prepare (rate);
    breakCore.prepare (rate, scratchSize);
    tunerCore.prepare (rate);
    resetDropoutStats();
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    prepare (d->getCurrentSampleRate(),
             d->getCurrentBufferSizeSamples(),
             d->getActiveInputChannels().countNumberOfSetBits(),
             d->getActiveOutputChannels().countNumberOfSetBits());
}

void AudioEngine::makeAudioThreadRealtime() noexcept
{
   #if JUCE_LINUX
    // Priority 70 sits under the sound and USB interrupt threads (rtirq puts those
    // at 80 to 90), the usual place for an audio thread on a tuned Linux machine.
    // Allowed only once linux/install.sh has given the audio group realtime rights.
    int policy = 0;
    sched_param param {};

    if (pthread_getschedparam (pthread_self(), &policy, &param) == 0 && (policy == SCHED_FIFO || policy == SCHED_RR))
    {
        rtPriority.store (param.sched_priority);
        rtState.store (1);
        return;
    }

    param.sched_priority = 70;
    const auto result = pthread_setschedparam (pthread_self(), SCHED_FIFO, &param);
    rtError.store (result);
    rtPriority.store (result == 0 ? 70 : 0);
    rtState.store (result == 0 ? 1 : -1);
   #else
    rtState.store (1);   // Windows: the ASIO driver runs its own high-priority thread
   #endif
}

void AudioEngine::audioDeviceError (const juce::String& message)
{
    errorText = message;
    deviceError.store (true);
}

int AudioEngine::driverLatencySamples() const
{
    return device != nullptr ? device->getInputLatencyInSamples() + device->getOutputLatencyInSamples() : 0;
}

juce::Array<int> AudioEngine::availableBufferSizes() const
{
    if (device != nullptr)
        return device->getAvailableBufferSizes();

    return simulated ? juce::Array<int> { 8, 16, 32, 64, 128, 256 } : juce::Array<int>();
}

juce::Array<double> AudioEngine::availableSampleRates() const
{
    if (device != nullptr)
        return device->getAvailableSampleRates();

    return simulated ? juce::Array<double> { currentSampleRate } : juce::Array<double>();
}

bool AudioEngine::hasControlPanel() const
{
    return device != nullptr && device->hasControlPanel();
}

void AudioEngine::showControlPanel()
{
    if (hasControlPanel())
        device->showControlPanel();
}

juce::String AudioEngine::reopen (int newBufferSize)
{
    if (simulated)
    {
        prepare (currentSampleRate, newBufferSize, currentInputs, currentOutputs);
        return {};
    }

    const auto sameDevice = choice;
    return open (sameDevice, newBufferSize);
}

//==============================================================================
void AudioEngine::setPlayThrough (PlayThrough mode) noexcept
{
    guardTripped.store (false);
    playMode.store ((int) mode);
}

void AudioEngine::setGlitchCheck (bool shouldBeOn) noexcept
{
    const bool starting = shouldBeOn && ! glitch.isOn();

    if (shouldBeOn)
        toneReturn.store (-1);

    glitch.setOn (shouldBeOn);

    if (starting && onGlitchCheckStarted != nullptr)
        onGlitchCheckStarted();
}

void AudioEngine::resetDropoutStats() noexcept
{
    glitch.resetCount();
    statsSince = juce::Time::getCurrentTime();
    driverDropoutsAtReset = device != nullptr ? juce::jmax (0, device->getXRunCount()) : 0;
    statsResetRequested.store (true);
}

DropoutStats AudioEngine::dropoutStats() const
{
    DropoutStats stats;
    stats.seconds = (juce::Time::getCurrentTime() - statsSince).inSeconds();
    stats.driverDropouts = device == nullptr || device->getXRunCount() < 0
                             ? -1
                             : juce::jmax (0, device->getXRunCount() - driverDropoutsAtReset);

    if (! statsResetRequested.load())
    {
        stats.lateCallbacks = lateCallbacks.load();
        stats.worstGapMs = worstGapSeconds.load() * 1000.0;
        stats.peakLoad = peakLoad.load();
    }

    stats.expectedGapMs = currentSampleRate > 0.0 ? currentBufferSize * 1000.0 / currentSampleRate : 0.0;
    return stats;
}

int AudioEngine::dropoutTotal() const
{
    const auto stats = dropoutStats();
    return juce::jmax (0, stats.driverDropouts) + stats.lateCallbacks;
}

//==============================================================================
void AudioEngine::measure (std::vector<int> bufferSizes)
{
    if (! isOpen() || job.active)
        return;

    const auto available = availableBufferSizes();
    std::vector<int> sizes;

    for (auto size : bufferSizes)
        if (available.contains (size))
            sizes.push_back (size);

    if (sizes.empty())
        sizes.push_back (currentBufferSize);

    rows.clear();
    job = {};
    job.active = true;
    job.sizes = std::move (sizes);
    job.originalSize = currentBufferSize;

    toneReturn.store (-1);
    glitch.setOn (false);
    measuring.store (true);
    startRow();
}

void AudioEngine::startRow()
{
    const auto size = job.sizes[job.index];
    job.rowProblem = {};
    job.pings.clear();
    job.phase = Job::Phase::settle;
    job.ticks = 0;

    if (size != currentBufferSize)
    {
        job.rowProblem = reopen (size);

        if (job.rowProblem.isEmpty() && currentBufferSize != size)
            job.rowProblem = "Asked the driver for " + juce::String (size) + " samples but it chose "
                               + juce::String (currentBufferSize) + ".";
    }
}

void AudioEngine::finishRow()
{
    auto row = summarisePings (job.pings, currentInputs, deviceIsCQ, currentSampleRate);
    row.bufferSize = isOpen() ? currentBufferSize : job.sizes[job.index];
    row.sampleRate = currentSampleRate;
    row.driverSamples = driverLatencySamples();
    row.dropouts = dropoutTotal();
    row.problem = job.rowProblem;
    rows.push_back (std::move (row));

    if (++job.index < job.sizes.size())
        startRow();
    else
        endJob();
}

void AudioEngine::endJob()
{
    job.active = false;

    if (job.originalSize > 0 && currentBufferSize != job.originalSize)
        reopen (job.originalSize);

    measuring.store (false);
    resetDropoutStats();
    updateLoopTiming();
}

void AudioEngine::tick()
{
    if (! job.active)
        return;

    ++job.ticks;

    if (! isOpen())
    {
        finishRow();   // the device would not reopen at this size; the row carries the reason
        return;
    }

    switch (job.phase)
    {
        case Job::Phase::settle:
            if (job.ticks >= settleTicks)
            {
                resetDropoutStats();
                probe.fire();
                job.phase = Job::Phase::listening;
                job.ticks = 0;
            }
            break;

        case Job::Phase::gap:
            if (job.ticks >= gapTicks)
            {
                probe.fire();
                job.phase = Job::Phase::listening;
                job.ticks = 0;
            }
            break;

        case Job::Phase::listening:
            if (probe.captureReady())
            {
                job.pings.push_back (probe.analyse());

                if ((int) job.pings.size() >= pingsPerRow)
                {
                    finishRow();
                }
                else
                {
                    job.phase = Job::Phase::gap;
                    job.ticks = 0;
                }
            }
            else if (job.ticks > giveUpTicks)
            {
                probe.cancel();
                job.rowProblem = "No audio arrived from the device.";
                finishRow();
            }
            break;
    }
}

juce::String AudioEngine::measuringStatus() const
{
    if (! job.active)
        return {};

    return "Measuring at " + juce::String (job.sizes[job.index]) + " samples ("
             + juce::String (job.index + 1) + " of " + juce::String (job.sizes.size()) + "): ping "
             + juce::String (juce::jmin (pingsPerRow, (int) job.pings.size() + 1)) + " of " + juce::String (pingsPerRow);
}

//==============================================================================
void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputs, int numIns,
                                                    float* const* outputs, int numOuts, int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    if (rtState.load (std::memory_order_relaxed) == 0)
        makeAudioThreadRealtime();

    blockCount.fetch_add (1, std::memory_order_relaxed);

    const auto startTicks = juce::Time::getHighResolutionTicks();
    const auto period = currentSampleRate > 0.0 ? numSamples / currentSampleRate : 0.0;

    if (statsResetRequested.exchange (false))
    {
        samplesSinceReset = 0;
        lateCallbacks.store (0);
        worstGapSeconds.store (0.0);
        peakLoad.store (0.0);
    }

    // The first half second after a start or a reset is left out: a device
    // that has only just opened takes a moment to find its rhythm.
    samplesSinceReset = juce::jmin (samplesSinceReset + numSamples, 1 << 30);
    const bool counting = samplesSinceReset > currentSampleRate * 0.5;

    if (counting && lastCallbackTicks != 0 && period > 0.0)
    {
        const auto gap = juce::Time::highResolutionTicksToSeconds (startTicks - lastCallbackTicks);

        if (gap > worstGapSeconds.load (std::memory_order_relaxed))
            worstGapSeconds.store (gap, std::memory_order_relaxed);

        // A whole extra block late (or 2 ms, for very small blocks that
        // normally arrive in bunches): the sound very likely clicked.
        if (gap > period + juce::jmax (period, 0.002))
            lateCallbacks.fetch_add (1, std::memory_order_relaxed);
    }

    lastCallbackTicks = startTicks;

    processBlock (inputs, numIns, outputs, numOuts, numSamples);

    if (counting && period > 0.0)
    {
        const auto load = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startTicks) / period;

        if (load > peakLoad.load (std::memory_order_relaxed))
            peakLoad.store (load, std::memory_order_relaxed);
    }
}

void AudioEngine::processBlock (const float* const* inputs, int numIns,
                                float* const* outputs, int numOuts, int numSamples) noexcept
{
    for (int ch = 0; ch < numOuts; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    if (numSamples <= 0 || numSamples > scratchSize || (int) scratch.size() < scratchSize)
        return;

    for (int ch = 0; ch < juce::jmin (numIns, numMeteredInputs); ++ch)
        if (inputs[ch] != nullptr)
            raisePeak (inputPeaks[(size_t) ch], peakOf (inputs[ch], numSamples));

    const auto output = [&] (int returnIndex) -> float*
    {
        const auto index = returnToOutput (returnIndex, numOuts);
        return index >= 0 ? outputs[index] : nullptr;
    };

    // ---- the latency ping -------------------------------------------------
    const bool pinging = probe.isSounding();
    probe.process (inputs, numIns, scratch.data(), numSamples);

    if (pinging)
    {
        // A device smaller than the CQ has no USB stereo channel: use every output it has.
        const auto mask = numOuts >= 16 ? pingMask.load() : 0xff;

        for (int r = 0; r < numReturns; ++r)
            if ((mask & (1 << r)) != 0)
                if (auto* out = output (r))
                    juce::FloatVectorOperations::copy (out, scratch.data(), numSamples);
    }

    // ---- the glitch check: its tone goes where the ping goes ------------------
    const bool checking = glitch.isOn() && ! pinging && ! measuring.load();

    if (checking)
    {
        glitch.process (inputs, numIns, scratch.data(), numSamples);
        const auto mask = numOuts >= 16 ? pingMask.load() : 0xff;

        for (int r = 0; r < numReturns; ++r)
            if ((mask & (1 << r)) != 0)
                if (auto* out = output (r))
                    juce::FloatVectorOperations::copy (out, scratch.data(), numSamples);
    }

    // ---- play-through (silent while measuring or checking, so nothing can loop round) ----
    const auto mode = (PlayThrough) playMode.load();
    // The mixer listens to the sockets itself, so play-through stays quiet while it is on (it would double everything).
    const bool playing = mode != PlayThrough::off && ! pinging && ! checking && ! measuring.load() && ! mixCore.isOn();

    if (playing && mode == PlayThrough::straight)
    {
        for (int r = 0; r < juce::jmin (numReturns, numIns); ++r)
            if (auto* out = output (r); out != nullptr && inputs[r] != nullptr)
                juce::FloatVectorOperations::copy (out, inputs[r], numSamples);
    }
    else if (playing && mode == PlayThrough::mixToUsb)
    {
        juce::FloatVectorOperations::clear (mix.data(), numSamples);

        // Every input socket, but never the CQ's USB channels 11 to 16: those
        // carry copies of the mixer's own outputs, and would loop straight round.
        for (int ch = 0; ch < juce::jmin (cqAnalogueInputs, numIns); ++ch)
            if (inputs[ch] != nullptr)
                juce::FloatVectorOperations::add (mix.data(), inputs[ch], numSamples);

        const auto first = usbPairFirstReturn (numOuts);

        for (int r = first; r < first + 2; ++r)
            if (auto* out = output (r))
                juce::FloatVectorOperations::copy (out, mix.data(), numSamples);
    }

    // ---- the return check tone ----------------------------------------------
    if (const auto r = toneReturn.load(); r >= 0 && ! pinging && ! checking && ! measuring.load())
    {
        if (auto* out = output (r))
        {
            const auto step = juce::MathConstants<double>::twoPi * toneHz / juce::jmax (1.0, currentSampleRate);

            for (int i = 0; i < numSamples; ++i)
            {
                out[i] += toneLevel * (float) std::sin (tonePhase);
                tonePhase += step;
            }

            tonePhase = std::fmod (tonePhase, juce::MathConstants<double>::twoPi);
        }
    }

    // ---- the kick pad test: its kick goes straight onto a pair of returns ------------------
    {
        // A device with no USB stereo channel (anything smaller than the CQ) plays it on its first pair.
        // Its sound is made here and placed further down: on a socket (looper and mixer) or straight on the returns.
        juce::FloatVectorOperations::clear (kickBuf.data(), numSamples);
        const int piezo = kickCore.getPiezoSocket();
        kickCore.listen (piezo >= 0 && piezo < numIns ? inputs[piezo] : nullptr, numSamples, pinging || checking || measuring.load());
        kickCore.process (kickBuf.data(), nullptr, numSamples, pinging || checking || measuring.load());
    }

    const auto kickToReturns = [&]
    {
        const auto first = numOuts >= 16 ? kickCore.getReturnPair() * 2 : 0;

        for (int side = 0; side < 2; ++side)
            if (auto* out = output (first + side))
                juce::FloatVectorOperations::add (out, kickBuf.data(), numSamples);
    };

    // ---- the effects: before the looper, so loops record them ------------------
    const bool quiet = pinging || checking || measuring.load();

    fxRack.setTempo (loopCore.tempoNow());

    if (quiet)
        fxRack.bypassBlock (inputs, numIns, numSamples);
    else
        fxRack.process (inputs, numIns, numSamples);

    // ---- the looper: its loops come back on the returns ---------------------------
    if (! quiet)
    {
        float* loopOutputs[numLoopOutputs];

        for (int r = 0; r < numLoopOutputs; ++r)
            loopOutputs[r] = output (r);

        // A device with no USB stereo channel (anything smaller than the CQ) plays the loops on its first pair.
        if (numOuts < 16 && loopOutputs[4] == nullptr)
        {
            loopOutputs[4] = output (0);
            loopOutputs[5] = output (1);
        }

        // The mixer, when it is on, takes the chains and the loops in place of the returns. It stands
        // aside while the amp capture plays its test sound, which must reach its return untouched.
        const bool mixing = ! fxRack.isAmpCapturing() && mixCore.startBlock (numSamples);

        // The break music (between sets): with the mixer on it joins the main output, after the
        // main mix is recorded; with it off it goes straight to a pair of returns. Silent, it adds nothing.
        breakCore.hear (inputs, numIns, numSamples);
        tunerCore.hear (inputs, numIns, numSamples);       // off, this reads one flag and returns
        const bool music = ! fxRack.isAmpCapturing() && breakCore.render (numSamples);
        const auto* afterEffects = fxRack.looperInputs (inputs, numIns);

        // The kick pad's kick joins its socket here, as a stomp box plugged in there would: after
        // the effects (the stomp has no chain), before the mixer channel and the looper.
        const int kickSocket = kickCore.getSocket();
        const bool kickJoins = kickSocket >= 0 && kickSocket < numIns && numIns <= (int) withKick.size() && peakOf (kickBuf.data(), numSamples) > 0.0f;

        if (kickJoins)
        {
            for (int s = 0; s < numIns; ++s)
                withKick[(size_t) s] = afterEffects[s];

            juce::FloatVectorOperations::copy (kickSock.data(), kickBuf.data(), numSamples);

            if (afterEffects[kickSocket] != nullptr)
                juce::FloatVectorOperations::add (kickSock.data(), afterEffects[kickSocket], numSamples);

            withKick[(size_t) kickSocket] = kickSock.data();
            afterEffects = withKick.data();
        }
        // The tuner's silence has to be known before the channels are worked on.
        if (mixing)
        {
            const auto quiet = tunerCore.silenceNow();
            mixCore.setTuning (quiet == Tuner::Silence::mainMix, quiet == Tuner::Silence::guitars, tunerCore.monitorsKeepGuitars());
        }

        // With the mixer on, the looper hears each channel after that channel's own processing.
        const auto* heard = mixing ? mixCore.stripChannels (afterEffects, numIns, numSamples) : afterEffects;

        if (mixing)
            loopCore.setLaneBuses (mixCore.laneBuses(), numMixSockets, mixCore.strayBus());
        else
            loopCore.setLaneBuses (nullptr, 0, nullptr);

        loopCore.process (heard, numIns, loopOutputs, numSamples);

        if (mixing)
        {
            mixCore.setTempo (fxRack.effectsTempo());
            mixCore.setBreakFeed (music ? breakCore.left() : nullptr, music ? breakCore.right() : nullptr);
            mixCore.process (inputs, afterEffects, numIns, numSamples);
            mixCore.addOutputs (output, numSamples);
        }
        else
        {
            fxRack.addOutputs (output, numSamples);

            // Nothing plays a bare socket while the mixer is off, so the kick goes to its returns as before.
            if (kickJoins)
                kickToReturns();

            if (music)
            {
                const auto first = numOuts >= 16 ? breakCore.returnPair() * 2 : 0;

                if (auto* out = output (first))
                    juce::FloatVectorOperations::add (out, breakCore.left(), numSamples);
                if (auto* out = output (first + 1))
                    juce::FloatVectorOperations::add (out, breakCore.right(), numSamples);
            }
        }

        if (! kickJoins)
            kickToReturns();
    }
    else
        kickToReturns();

    // ---- meters, clipping and the feedback guard ----------------------------
    float loudest = 0.0f;

    for (int r = 0; r < numReturns; ++r)
    {
        if (auto* out = output (r))
        {
            const auto peak = peakOf (out, numSamples);
            juce::FloatVectorOperations::clip (out, out, -1.0f, 1.0f, numSamples);
            raisePeak (returnPeaks[(size_t) r], juce::jmin (1.0f, peak));
            loudest = juce::jmax (loudest, peak);
        }
    }

    // Play-through stuck at full level for a quarter of a second is a feedback
    // howl, not music: switch it off before it gets any louder.
    hotSamples = playing && loudest >= 0.99f ? hotSamples + numSamples : 0;

    if (hotSamples > currentSampleRate * 0.25)
    {
        playMode.store ((int) PlayThrough::off);
        guardTripped.store (true);
        hotSamples = 0;
    }
}

//==============================================================================
namespace
{
    juce::File rememberedTimingFile()
    {
        // "-2": figures saved by earlier builds could include a microphone hearing a speaker, so they are left behind.
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("GigRig").getChildFile ("loop-timing-2.txt");
    }

    juce::File driverDelayFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("GigRig").getChildFile ("driver-delay.txt");
    }

    constexpr double driverDelaySlackMs = 6.0;
}

void AudioEngine::loadRemembered()
{
    if (rememberedLoaded || simulated)
        return;

    rememberedLoaded = true;

    for (const auto& line : juce::StringArray::fromLines (rememberedTimingFile().loadFileAsString()))
    {
        const auto parts = juce::StringArray::fromTokens (line, " ", {});

        if (parts.size() == 4 && parts[0].getIntValue() > 0 && parts[1].getIntValue() > 0 && parts[3].getIntValue() > 0)
            remembered.push_back ({ parts[0].getIntValue(), parts[1].getIntValue(), parts[2].getIntValue(), parts[3].getIntValue() });
    }
}

void AudioEngine::remember (int samples)
{
    if (simulated || device == nullptr || samples <= 0)
        return;

    loadRemembered();
    const RememberedTiming entry { juce::roundToInt (currentSampleRate), currentBufferSize, driverLatencySamples(), samples };

    bool found = false;

    for (auto& r : remembered)
    {
        if (r.rate == entry.rate && r.buffer == entry.buffer && r.driver == entry.driver)
        {
            if (r.samples == entry.samples)
                return;     // nothing new: this is called twice a second

            r.samples = entry.samples;
            found = true;
        }
    }

    if (! found)
        remembered.push_back (entry);

    juce::StringArray lines;

    for (const auto& r : remembered)
        lines.add (juce::String (r.rate) + " " + juce::String (r.buffer) + " " + juce::String (r.driver) + " " + juce::String (r.samples));

    const auto file = rememberedTimingFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText (lines.joinIntoString ("\n") + "\n");
}

const DriverDelayNote* driverDelayGrown (const std::vector<DriverDelayNote>& history, int rate, int buffer, int totalNow)
{
    const DriverDelayNote* lowest = nullptr;

    for (const auto& n : history)
        if (n.rate == rate && n.buffer == buffer && n.total() > 0 && (lowest == nullptr || n.total() < lowest->total()))
            lowest = &n;

    // Safe mode adds about 3 ms on the CQ, which is expected. Twice that is not.
    if (lowest == nullptr || rate <= 0 || (totalNow - lowest->total()) * 1000.0 / rate <= driverDelaySlackMs)
        return nullptr;

    return lowest;
}

void AudioEngine::noteDriverDelay()
{
    if (simulated || device == nullptr || currentSampleRate <= 0.0)
        return;

    const auto file = driverDelayFile();

    if (! driverHistoryLoaded)
    {
        driverHistoryLoaded = true;

        for (const auto& line : juce::StringArray::fromLines (file.loadFileAsString()))
        {
            const auto parts = juce::StringArray::fromTokens (line, " ", {});

            if (parts.size() == 5 && parts[0].getIntValue() > 0 && parts[1].getIntValue() > 0)
                driverHistory.push_back ({ parts[0].getIntValue(), parts[1].getIntValue(), parts[2].getIntValue(),
                                           parts[3].getIntValue(), parts[4].getLargeIntValue() });
        }
    }

    DriverDelayNote now { juce::roundToInt (currentSampleRate), currentBufferSize, device->getInputLatencyInSamples(),
                          device->getOutputLatencyInSamples(), juce::Time::currentTimeMillis() };

    if (now.total() <= 0)
        return;

    for (const auto& n : driverHistory)
        if (n.rate == now.rate && n.buffer == now.buffer && n.inSamples == now.inSamples && n.outSamples == now.outSamples)
            return;

    driverHistory.push_back (now);

    while (driverHistory.size() > 60)
        driverHistory.erase (driverHistory.begin());

    juce::StringArray lines;

    for (const auto& n : driverHistory)
        lines.add (juce::String (n.rate) + " " + juce::String (n.buffer) + " " + juce::String (n.inSamples) + " "
                     + juce::String (n.outSamples) + " " + juce::String (n.firstSeenMs));

    file.getParentDirectory().createDirectory();
    file.replaceWithText (lines.joinIntoString ("\n") + "\n");
}

juce::String AudioEngine::driverDelayWarning()
{
    if (simulated || device == nullptr || currentSampleRate <= 0.0)
        return {};

    noteDriverDelay();
    const auto* before = driverDelayGrown (driverHistory, juce::roundToInt (currentSampleRate), currentBufferSize, driverLatencySamples());

    if (before == nullptr)
        return {};

    const auto oneDp = [] (double v) { return juce::String (v, 1); };
    return "DELAY HIGHER THAN BEFORE: the CQ's driver says " + oneDp (driverLatencySamples() * 1000.0 / currentSampleRate)
             + " ms now. At this same setting it said " + oneDp (before->ms()) + " ms on "
             + juce::Time (before->firstSeenMs).toString (true, false) + ". Loops still line up, but play-through will feel late. "
               "Restarting the computer is the first thing to try.";
}

AudioEngine::LoopTiming AudioEngine::updateLoopTiming()
{
    LoopTiming timing;

    if (currentSampleRate <= 0.0)
        return timing;

    noteDriverDelay();

    // A loop has to come back out exactly one loop after it was played. A sound takes the
    // round trip to get into the computer and back out again, so each recording is shifted
    // earlier by that much, less the short delay the live sound itself has through the mixer.
    const LatencyRow* measured = nullptr;

    for (const auto& row : rows)
        if (row.best >= 0 && row.bufferSize == currentBufferSize && juce::approximatelyEqual (row.sampleRate, currentSampleRate))
            measured = &row;

    const auto msToSamples = [this] (double ms) { return juce::roundToInt (ms * currentSampleRate / 1000.0); };

    if (measured != nullptr)
    {
        const auto roundTrip = measured->heard[(size_t) measured->best].medianSamples;
        timing.baseSamples = roundTrip - (deviceIsCQ ? msToSamples (measured->bestIsAnalogue ? cqThroughMs : cqMixMs) : 0);
        timing.source = measured->bestIsAnalogue ? "the latency test at this buffer size"
                                                 : "the latency test at this buffer size (no cable, so the copy inside the mixer)";
        timing.measured = true;
        remember (timing.baseSamples);
    }
    else if (const auto* kept = [this]() -> const RememberedTiming*
             {
                 loadRemembered();

                 for (const auto& r : remembered)
                     if (r.rate == juce::roundToInt (currentSampleRate) && r.buffer == currentBufferSize && r.driver == driverLatencySamples())
                         return &r;

                 return nullptr;
             }())
    {
        timing.baseSamples = kept->samples;
        timing.source = "the latency test at this setting, remembered from before";
        timing.measured = true;
    }
    else if (const auto driver = driverLatencySamples(); driver > 0)
    {
        timing.baseSamples = driver - (deviceIsCQ ? msToSamples (cqThroughMs) : 0);
        timing.source = "the driver's own figure (press Measure latency on the first tab for a measured one)";
    }
    else
    {
        timing.source = "nothing: no figure from the driver and no latency test yet";
    }

    timing.baseSamples = juce::jmax (0, timing.baseSamples);
    timing.samples = juce::jmax (0, timing.baseSamples + msToSamples (loopTrimMs));
    loopCore.setCompensation (timing.samples);
    return timing;
}

juce::String AudioEngine::latencyText() const
{
    juce::StringArray lines;
    const auto add = [&lines] (const juce::String& line = {}) { lines.add (line); };

    add ("STAGE 1: AUDIO AND LATENCY");
    add ("--------------------------");

    if (! isOpen())
    {
        add ("Audio device: none running."
               + (errorText.isNotEmpty() ? " Last problem: " + errorText : juce::String()));
    }
    else
    {
        add ("Audio device: " + choice.label() + ", " + juce::String (currentSampleRate, 0) + " Hz, "
               + juce::String (currentBufferSize) + "-sample buffer, "
               + juce::String (currentInputs) + " in / " + juce::String (currentOutputs) + " out");

        if (const auto slow = slowBufferWarning(); slow.isNotEmpty())
            add ("  WARNING: " + slow);

       #if JUCE_LINUX
        if (device != nullptr)
        {
            const auto state = rtState.load();

            if (state == 1)
                add ("Audio thread: realtime, priority " + juce::String (rtPriority.load()) + ".");
            else if (state == -1)
                add ("Audio thread: NOT realtime (the system said: " + juce::String (std::strerror (rtError.load()))
                       + "). Run linux/install.sh, then log out and back in. See docs/linux-setup.md.");
            else
                add ("Audio thread: not started yet.");
        }
       #endif

        if (device != nullptr)
            add ("Driver's own latency figure: in " + ms (device->getInputLatencyInSamples() * 1000.0 / currentSampleRate)
                   + " + out " + ms (device->getOutputLatencyInSamples() * 1000.0 / currentSampleRate)
                   + " = " + ms (driverLatencySamples() * 1000.0 / currentSampleRate));

        if (startNudge.isNotEmpty())
            add ("  " + startNudge);

        if (! simulated)
        {
            if (const auto* before = driverDelayGrown (driverHistory, juce::roundToInt (currentSampleRate), currentBufferSize, driverLatencySamples()))
                add ("  DELAY HIGHER THAN BEFORE: at this same sample rate and buffer the driver said "
                       + ms (before->ms()) + " on " + juce::Time (before->firstSeenMs).toString (true, true, false, true) + ".");

            if (! driverHistory.empty())
            {
                add ("Driver's delay figures seen so far (each with when it was first seen):");

                for (const auto& n : driverHistory)
                    add ("  " + juce::Time (n.firstSeenMs).toString (true, true, false, true) + ": " + juce::String (n.rate) + " Hz, buffer "
                           + juce::String (n.buffer) + ": in " + ms (n.inSamples * 1000.0 / n.rate) + " + out " + ms (n.outSamples * 1000.0 / n.rate)
                           + " = " + ms (n.ms()));
            }
        }
    }

    add();

    if (rows.empty())
    {
        add ("Latency test: not run yet.");
    }
    else
    {
        juce::StringArray pingOn;

        for (int r = 0; r < numReturns; ++r)
            if ((pingMask.load() & (1 << r)) != 0)
                pingOn.add (juce::String (r + 1));

        add ("Latency test (ping sent on return " + pingOn.joinIntoString ("+") + ", "
               + juce::String (pingsPerRow) + " pings per buffer size):");

        for (const auto& row : rows)
        {
            const auto head = "  Buffer " + juce::String (row.bufferSize).paddedRight (' ', 5);

            if (row.best < 0)
            {
                add (head + "ping not heard on any input."
                       + (row.problem.isNotEmpty() ? " " + row.problem : juce::String()));
                continue;
            }

            const auto& best = row.heard[(size_t) row.best];

            add (head + "round trip " + ms (row.ms (best.medianSamples)) + " (" + juce::String (best.medianSamples)
                   + " samples), heard on " + inputName (best.input, deviceIsCQ)
                   + " at " + juce::String (juce::roundToInt (best.levelDb)) + " dB");
            add ("               " + juce::String (best.pingsHeard) + " of " + juce::String (row.pingsSent)
                   + " pings, fastest " + juce::String (best.minSamples) + " and slowest " + juce::String (best.maxSamples)
                   + " samples; driver's own figure " + ms (row.ms (row.driverSamples))
                   + "; dropouts during the test: " + juce::String (row.dropouts));

            if (row.acousticInput >= 0)
                add ("               NOTE: " + inputName (row.acousticInput, deviceIsCQ) + " heard the ping " + ms (row.ms (row.acousticSamples))
                     + " after it was sent, too late to be a cable: a microphone or pickup hearing a speaker. "
                     "It was set aside and the copy inside the mixer used instead.");
            else if (! row.bestIsAnalogue)
                add ("               NOTE: no input socket heard the ping, so this is the copy that came back inside "
                     "the mixer. It leaves out the mixer's converters. Check the loopback cable.");

            juce::StringArray others;

            for (int i = 0; i < (int) row.heard.size(); ++i)
                if (i != row.best)
                    others.add (inputName (row.heard[(size_t) i].input, deviceIsCQ) + " "
                                  + ms (row.ms (row.heard[(size_t) i].medianSamples)) + " at "
                                  + juce::String (juce::roundToInt (row.heard[(size_t) i].levelDb)) + " dB");

            if (! others.isEmpty())
                add ("               also heard on: " + others.joinIntoString ("; "));

            if (row.problem.isNotEmpty())
                add ("               " + row.problem);
        }
    }

    return lines.joinIntoString ("\n") + "\n";
}

juce::String AudioEngine::dropoutText() const
{
    juce::StringArray lines;
    const auto add = [&lines] (const juce::String& line = {}) { lines.add (line); };

    const auto mode = playThrough();
    const auto stats = dropoutStats();
    const auto seconds = (int) stats.seconds;

    add ("Play-through: " + juce::String (mode == PlayThrough::off ? "off"
                                           : mode == PlayThrough::straight ? "each input to its own return"
                                                                           : "inputs 1 to 10 mixed to the USB stereo channel")
           + (guardTripped.load() ? "  (SWITCHED OFF BY THE FEEDBACK GUARD)" : ""));
    add ("Dropout watch, running for " + juce::String (seconds / 60) + " min " + juce::String (seconds % 60) + " s:");
    add ("  Dropouts counted by the driver: "
           + (stats.driverDropouts < 0 ? juce::String ("not reported") : juce::String (stats.driverDropouts)));
    add ("  Late blocks of audio: " + juce::String (stats.lateCallbacks)
           + " (longest wait " + ms (stats.worstGapMs) + ", normal is " + ms (stats.expectedGapMs) + ")");
    add ("  Busiest moment: " + juce::String (stats.peakLoad * 100.0, 1) + "% of the time available");

    const auto found = glitchStats();
    const auto clock = [] (double s) { const auto whole = (int) s; return juce::String (whole / 60) + ":" + juce::String (whole % 60).paddedLeft ('0', 2); };

    const bool stopped = found.state == GlitchStats::State::off;

    if (stopped && found.seconds <= 0.0)
    {
        add ("Glitch check: off.");
    }
    else if (found.state == GlitchStats::State::notHeard)
    {
        add ("Glitch check: on, but the tone is not coming back cleanly on any input.");
    }
    else if (found.state == GlitchStats::State::listening && found.seconds <= 0.0)
    {
        add ("Glitch check: on, listening for the tone.");
    }
    else
    {
        add (juce::String (stopped ? "Glitch check (stopped now; this is the last run), tone watched for "
                                   : "Glitch check, tone watched for ") + clock (found.seconds) + ", heard on " + inputName (found.input, deviceIsCQ)
               + " at " + juce::String (juce::roundToInt (found.levelDb)) + " dB (background "
               + juce::String (found.backgroundPercent, 2) + "% of the tone):");
        add ("  Glitches heard: " + juce::String (found.glitches)
               + (found.breaks > found.glitches ? " (" + juce::String (found.breaks) + " separate breaks; breaks less than a second apart count as one glitch)" : juce::String())
               + (found.lostSeconds >= 0.05 ? "; tone missing altogether for " + juce::String (found.lostSeconds, 1) + " s" : juce::String()));

        for (size_t i = 0; i < found.times.size(); ++i)
        {
            const auto breaks = found.breaksEach[i];

            add ("    at " + clock (found.times[i]) + ": "
                   + (breaks <= 1 ? juce::String ("a single break")
                      : breaks < 5 ? juce::String (breaks) + " breaks close together"
                                   : "a burst of " + juce::String (breaks) + " breaks (a long glitch, or another sound getting into "
                                     "what the check hears, such as a lead being plugged or unplugged)"));
        }

        if (found.glitches > (int) found.times.size())
            add ("    and " + juce::String (found.glitches - (int) found.times.size()) + " more after that");

        if (! found.analogue && deviceIsCQ)
            add ("  NOTE: heard on the copy inside the mixer, not through a cable. That still covers the USB link both ways.");

        if (found.state == GlitchStats::State::listening)
            add ("  The tone has gone missing: listening for it again.");
    }

    return lines.joinIntoString ("\n") + "\n";
}

} // namespace gigrig
