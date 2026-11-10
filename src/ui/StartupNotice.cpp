#include "ui/StartupNotice.h"
#include "engine/AppPaths.h"
#include "ui/UiContext.h"
#include "ui/WingPopups.h"

namespace livedaw::ui::startupNotice
{

namespace
{
    /** How complete each console is, as a rough percentage of its controls, screens and
        processing. Review these with every update (docs/PLAN.md, Release). */
    struct Completion
    {
        const char* name;
        int percent;
    };
    constexpr Completion kCompletion[] = {
        { "WING", 85 },
        { "ProMix 01", 70 },
        { "02R", 60 },
        { "X32", 85 },
        { "01V", 75 },
        { "SQ-5", 70 },
        { "dLive", 60 },
        { "Universal Mixer", 65 },
        { "DM2000", 70 },
        { "PRO2", 35 },
        { "Q1202USB", 40 },
    };
} // namespace

juce::String text()
{
    // Each point is checked against the code and docs/ARCHITECTURE.md ("Known
    // limitations"); keep it in step when something gets done.
    const juce::String app (kAppName);
    juce::String t;
    t << app << " is an independent hobby project, made for fun and practice. Use it at your own risk: it comes "
         "with no warranty.\n"
         "\n"
      << app << " emulates mixing consoles on an ordinary computer and audio interface: digital ones now, analog ones to follow. None of them is "
         "complete, and each can differ from the real console.\n"
         "\n"
         "## How complete each console is\n"
         "Rough estimates, reviewed with each update, of how much of a console's controls, screens and processing "
         "exists. They don't say how close it sounds.\n";
    for (const auto& c : kCompletion)
        t << "- " << c.name << ": about " << c.percent << " %\n";
    t << "\n"
         "## Before you use it live\n"
         "Don't rely on " << app << " for a critical live show without testing it thoroughly first, "
         "with your own computer, audio interface and show.\n"
         "\n"
         "## On every console\n"
         "- Sound: the processing and effects recreate each unit's controls and character from published "
         "information, not the makers' algorithms, and most haven't been compared by ear or measured against a real console.\n"
         "- Hardware: your audio interface's inputs and outputs take the place of the console's. "
         "There is no StageConnect, AES50 or Dante, and no remote preamp control: source gain is digital and +48V is only stored.\n"
         "- Recording: a multitrack recorder writes to a folder on disk (with an editor); the USB recorder uses a "
         "folder of your choice instead of a USB stick or SD card. The MP3 encoder is basic (constant bit rate, a simple masking model).\n"
         "- Plugins (VST3, AU, LV2) run inside the mixer process by default: a plugin that crashes takes " << app
      << " down (safe mode then restores the show without plugins). The experimental plugin sandbox "
         "(SETUP > SIDECAR > PLUGINS, off by default) runs each plugin in its own process, so a crash or hang only "
         "bypasses its FX slot; it adds one audio block of latency (compensated) and opens the plugin's editor in a "
         "separate window. Delay compensation (SETUP > SIDECAR > PLUGINS) lines up the paths into buses, mains and "
         "matrices up to 8192 samples; solo, monitoring, direct outs and recorder taps are not compensated.\n"
         "- MIDI and DAW remote control (MCU / HUI) have not been tested with real hardware.\n"
         "- Controls drawn but dimmed or disabled are parts of a console " << app << " has nothing behind yet.\n"
         "\n"
         "## Console by console\n"
         "Choose a console under SETUP > SIDECAR > INTERFACE.\n"
         "- WING: the WING network protocols answer every parameter of the WING 3.1 tree but one; 93.2 % drive the "
         "engine or the settings (of 79,326, counting every model's parameters and every custom control mode's "
         "function nodes; the rest is stored but does nothing). WING Edit 3.3.4 works with it; it sees FX 1-16 only, "
         "and not the direct outs, and the parameters it knows from firmware newer than 3.1 are ignored. WING Copilot "
         "has not been tried, and exported WING snapshots have not been loaded into a console. Scene and snippet files "
         "are " << app << "'s own (.lsnap / .lsnip), not the WING's.\n"
         "- X32: its main LR plays on outputs 15-16, as an X32 does.\n"
         "- ProMix 01 and 02R: fader steps, effects and tables are read from their User's Guides, not measured on a "
         "console, and their bulk dumps go back into " << app << ", not into a real one.\n"
         "- 01V: the surface and LCD follow photographs of a real console and its manual, and it has its MIDI and scene recall undo.\n"
         "- SQ-5: the add-on effects and DEEP models are the dLive's under the guide's names; several Setup and Utility pages are still dim.\n"
         "- dLive: a C3500 surface by default and a C1500 one, from photographs of real consoles. Many effect ranges and "
         "the automatic mic mixer are " << app << "'s reading of the published material; RackUltra and Dyn8 are built but run only on Universal strips for now.\n"
         "- DM2000: its processing, most of its effects, its surface and the LCD's most-used pages exist; its Automix (faders and ON only), recorder arming and locator exist; remote layers, surround and Bulk are not built.\n"
         "- PRO2: its processing, most of its effects and a first surface with its screen exist; MCA and SIS, direct outs and the safes are drawn but not built.\n"
         "- Q1202USB: the first analog mixer: its routing, blocks and surface follow the owner's photographs of the panel and the quick-start guide; its sound is " << app << "'s reading, nothing was measured. Console tape and scenes are " << app << "'s own additions.\n"
         "- Universal Mixer: " << app << "'s own interface for a console of any size.\n"
         "\n"
         "## Trademarks\n"
      << app << " is not affiliated with, endorsed by or supported by any console maker. "
         "Product names are their owners' trademarks, used here only to say which console is emulated.\n"
         "\n"
         "## Community\n"
         "Questions, setup help and feedback: the " << app << " Discord server, " << kDiscordUrl << " (the DISCORD key below "
         "opens it). Bugs: https://github.com/Audbol/Sidecar/issues\n"
         "\n"
         "## Licence\n"
      << app << " is free software under the GNU Affero General Public License, version 3 (AGPLv3), "
         "WITHOUT ANY WARRANTY. Source code, the licence and the third-party notices (JUCE, the VST3, LV2 "
         "and ASIO SDKs, the Barlow fonts, libwing's WING parameter data): https://github.com/Audbol/Sidecar";
    return t;
}

void show (juce::PropertiesFile& settings, juce::Component& host, bool atStartup)
{
    const juce::String version (JUCE_APPLICATION_VERSION_STRING);
    if (atStartup && settings.getValue (uiprefs::noticeDismissed) == version)
        return;

    popups::NoticeOptions options;
    options.checkText = "DON'T SHOW AGAIN UNTIL THE NEXT VERSION";
    options.checked = settings.getValue (uiprefs::noticeDismissed) == version;
    options.linkText = "DISCORD";
    options.linkUrl = juce::URL (kDiscordUrl);
    // Over the whole window (the window's content), so it stays readable
    // however small the main display is.
    juce::Component* cover = &host;
    while (cover->getParentComponent() != nullptr && dynamic_cast<juce::ResizableWindow*> (cover->getParentComponent()) == nullptr)
        cover = cover->getParentComponent();
    options.coverHost = true;
    options.onClose = [&settings, version] (bool dontShow)
    {
        settings.setValue (uiprefs::noticeDismissed, dontShow ? version : juce::String());
        settings.saveIfNeeded();
    };
    popups::notice (*cover, "ABOUT " + juce::String (kAppName).toUpperCase() + " " + version, text(), std::move (options));
}

} // namespace livedaw::ui::startupNotice
