#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "RemoteServer.h"
#include "Style.h"

namespace gigrig
{

/** The "Remote screen" tab: switches the door for phones and tablets on and off, and shows the
    address to type into the phone (docs/remote.md). Off until Tom ticks the box; what he chose
    is remembered in remote.txt. */
class RemotePanel final : public juce::Component,
                          private juce::Timer
{
public:
    explicit RemotePanel (RemoteServer&);
    ~RemotePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void apply (bool save);

    RemoteServer& server;
    juce::Label heading, portLabel, explain;
    juce::ToggleButton onBox { "Let a phone or tablet on the same network control the rig" };
    juce::TextEditor portBox, addressView;
    juce::TextButton openButton { "Open it on this computer" };
    StatusRow status;
    juce::String error;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RemotePanel)
};

} // namespace gigrig
