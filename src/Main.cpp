#include <juce_gui_basics/juce_gui_basics.h>

#include <iostream>

#include "AmpModels.h"
#include "DeviceReport.h"
#include "MainComponent.h"
#include "Responses.h"
#include "SelfTest.h"

namespace gigrig
{

class GigRigApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String& commandLine) override
    {
        // Two windowless modes for the automatic builds:
        //   GigRig --report <file>      write the device report and exit
        //   GigRig --self-test <file>   check the matching logic and exit (non-zero on failure)
        const auto args = juce::StringArray::fromTokens (commandLine, true);
        const auto fileAfter = [&args] (const juce::String& flag) -> juce::File
        {
            const auto index = args.indexOf (flag);
            const auto path = index >= 0 ? args[index + 1].unquoted() : juce::String();
            return path.isNotEmpty() ? juce::File::getCurrentWorkingDirectory().getChildFile (path) : juce::File();
        };

        if (args.contains ("--self-test"))
        {
            bool passed = false;
            const auto log = runSelfTest (passed);
            const auto wrote = fileAfter ("--self-test").replaceWithText (log);
            setApplicationReturnValue (passed && wrote ? 0 : 1);
            quit();
            return;
        }

        //   GigRig --make-response <capture.wav> <response.wav>   a response file from a capture of a tone-only pedal
        if (args.contains ("--make-response"))
        {
            const int at = args.indexOf ("--make-response");
            const auto made = at + 2 < args.size() ? responses::makeFromCapture (juce::File::getCurrentWorkingDirectory().getChildFile (args[at + 1].unquoted()),
                                                                                juce::File::getCurrentWorkingDirectory().getChildFile (args[at + 2].unquoted()))
                                                   : responses::Made { false, "Use: GigRig --make-response <capture.wav> <response.wav>", {} };
            std::cout << made.text << std::endl;
            setApplicationReturnValue (made.ok ? 0 : 1);
            quit();
            return;
        }

        if (args.contains ("--report"))
        {
            const auto wrote = fileAfter ("--report").replaceWithText (scanDevices().toText());
            setApplicationReturnValue (wrote ? 0 : 1);
            quit();
            return;
        }

        // The amp models built into the app go to Documents/GigRig/Amp models before anything
        // can load an amp (the self-test uses its own copy).
        ampmodels::setUp (ampmodels::folder());
        responses::setUp();
        starterpresets::setUp (EffectsPanel::presetFolder());

        // Only one window at a time. Two copies would both hear the pedal and both loop, each with
        // its own timing, which sounds like loops drifting apart (it happened on 23 Nov 2026).
        // The windowless modes above are not counted, so the checks can run beside an open app.
        if (! onlyWindow.enter (0))
        {
            juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
                                                                                  "GigRig is already open",
                                                                                  "Another copy of GigRig is already running, so this one will close. "
                                                                                  "Look for it on the taskbar. Close it first if you want to open a newer build."),
                                          [this] (int) { quit(); });
            return;
        }

        holdingWindowLock = true;
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow.reset();

        if (holdingWindowLock)
            onlyWindow.exit();
    }
    void systemRequestedQuit() override     { quit(); }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, juce::Colour (0xff15181c), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, false);
            setResizeLimits (900, 600, 4000, 3000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    std::unique_ptr<MainWindow> mainWindow;
    juce::InterProcessLock onlyWindow { "GigRig-one-window" };
    bool holdingWindowLock = false;
};

} // namespace gigrig

START_JUCE_APPLICATION (gigrig::GigRigApplication)
