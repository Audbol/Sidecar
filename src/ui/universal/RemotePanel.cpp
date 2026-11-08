#include "ui/universal/RemotePanel.h"

#include "engine/MidiRemote.h"
#include "ui/MidiRemoteUi.h"
#include "ui/WebMirror.h"

#include <algorithm>

namespace livedaw::ui::universal
{

namespace
{
    constexpr int kRow = 40, kTitle = 36;
} // namespace

//==============================================================================
TextField::TextField (const Theme& t) : theme (t) {}

void TextField::refresh()
{
    const auto now = value ? value() : juce::String();
    const bool bad = failed && failed();
    if (now != shown || bad != shownFailed)
    {
        shown = now;
        shownFailed = bad;
        repaint();
    }
}

void TextField::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (theme.raised);
    g.fillRoundedRectangle (area, sizes::kRadius);
    g.setColour (shownFailed ? theme.mute : theme.outline);
    g.drawRoundedRectangle (area, sizes::kRadius, shownFailed ? 2.0f : 1.0f);
    auto text = area.reduced (10.0f, 0.0f);
    // The value keeps its room; the caption takes what is left (narrowed, never under the value).
    const auto valueText = shown.isNotEmpty() ? shown : juce::String ("-");
    const auto valueFont = type::font (type::kValue, true);
    const float valueW = juce::jmin (text.getWidth() * 0.6f, juce::GlyphArrangement::getStringWidth (valueFont, valueText) + 4.0f);
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kCaption, true));
    g.drawFittedText (caption, text.withTrimmedRight (valueW + 6.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);
    g.setColour (shownFailed ? theme.mute : theme.text);
    g.setFont (valueFont);
    g.drawFittedText (valueText, text.withTrimmedLeft (text.getWidth() - valueW).toNearestInt(), juce::Justification::centredRight, 1, 0.7f);
}

void TextField::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown() || ! change)
        return;
    askForText (*this, caption, prompt.isNotEmpty() ? prompt : caption + ":", shown,
                [safe = juce::Component::SafePointer<TextField> (this)] (const juce::String& text)
                {
                    if (safe != nullptr && safe->change)
                    {
                        safe->change (text);
                        safe->refresh();
                    }
                });
}

//==============================================================================
RemotePanel::RemotePanel (UiContext& c, const Theme& t)
    : wingOn ("ENABLE WING REMOTE", t.accent, t), discoveryOn ("DISCOVERY", t.accent, t), oscOn ("OSC", t.accent, t), nativeOn ("NATIVE", t.accent, t),
      consoleName (t), discoveryPort (t), oscPort (t), nativePort (t), webOn ("ENABLE WEB REMOTE", t.accent, t), viewOnly ("VIEW ONLY", t.accent, t),
      webPort (t), webPin (t), dawOn ("DAW CONTROL", t.accent, t), dawEmulation (t), ctx (c), theme (t)
{
    // ---- WING REMOTE (WingRemote::Settings in the app settings) ----
    using WS = WingRemote::Settings;
    auto wing = [this] { return WS::load (ctx.settings); };
    wingOn.isOn = [wing] { return wing().enabled; };
    wingOn.onClick = [this] { changeWing ([] (WS& s) { s.enabled = ! s.enabled; }); };
    const auto server = [this, wing] (ToggleKey& key, TextField& port, const char* name, bool WS::*on, int WS::*number, int WingRemote::Status::*bound)
    {
        key.isOn = [wing, on] { return wing().*on; };
        key.onClick = [this, on] { changeWing ([on] (WS& s) { s.*on = ! (s.*on); }); };
        port.caption = juce::String (name) + " PORT";
        port.value = [wing, number] { return juce::String (wing().*number); };
        port.change = [this, number] (const juce::String& text)
        {
            const int p = text.getIntValue();
            if (p < 1 || p > 65535)
                return false;
            changeWing ([number, p] (WS& s) { s.*number = p; });
            return true;
        };
        // On, but not listening (the port in use): red.
        port.failed = [this, wing, on, bound]
        {
            const auto st = ctx.engine.getWing().getStatus();
            return st.running && wing().*on && st.*bound < 0;
        };
    };
    server (discoveryOn, discoveryPort, "DISCOVERY (UDP)", &WS::discovery, &WS::discoveryPort, &WingRemote::Status::discoveryPort);
    server (oscOn, oscPort, "OSC (UDP)", &WS::osc, &WS::oscPort, &WingRemote::Status::oscPort);
    server (nativeOn, nativePort, "NATIVE (TCP)", &WS::native, &WS::nativePort, &WingRemote::Status::nativePort);
    consoleName.caption = "CONSOLE NAME";
    consoleName.prompt = "The console's name, as WING apps list it:";
    consoleName.value = [wing] { return wing().name; };
    consoleName.change = [this] (const juce::String& text)
    {
        changeWing ([text] (WS& s) { s.name = text.isNotEmpty() ? text.toUpperCase() : juce::String ("SIDECAR"); });
        return true;
    };

    // ---- WEB REMOTE (WebRemote::Settings) ----
    using BS = WebRemote::Settings;
    auto web = [this] { return BS::load (ctx.settings); };
    webOn.isOn = [web] { return web().enabled; };
    webOn.onClick = [this] { changeWeb ([] (BS& s) { s.enabled = ! s.enabled; }); };
    viewOnly.isOn = [web] { return web().viewOnly; };
    viewOnly.onClick = [this] { changeWeb ([] (BS& s) { s.viewOnly = ! s.viewOnly; }); };
    webPort.caption = "PORT (TCP)";
    webPort.value = [web] { return juce::String (web().port); };
    webPort.change = [this] (const juce::String& text)
    {
        const int p = text.getIntValue();
        if (p < 1 || p > 65535)
            return false;
        changeWeb ([p] (BS& s) { s.port = p; });
        return true;
    };
    webPort.failed = [this, web]
    {
        if (ctx.web == nullptr)
            return false;
        const auto st = ctx.web->getStatus();
        return web().enabled && st.port < 0;
    };
    webPin.caption = "PIN";
    webPin.prompt = "4-8 digits; empty: no PIN:";
    webPin.value = [web] { return web().pin.isNotEmpty() ? juce::String::repeatedString ("*", web().pin.length()) : juce::String ("NONE"); };
    webPin.change = [this] (const juce::String& text)
    {
        if (text.isNotEmpty() && ! BS::isValidPin (text))
            return false;
        changeWeb ([text] (BS& s) { s.pin = text; });
        return true;
    };

    // ---- MIDI (MidiRemote::Settings) ----
    for (int f = 0; f < MidiRemote::kNumFunctions && f < (int) routes.size(); ++f)
    {
        auto& r = routes[(size_t) f];
        r = std::make_unique<ChoiceBar> (t);
        r->caption = MidiRemote::functionName (f);
        r->device = devices::wing; // the WING's MIDI REMOTE CONTROL rows
        r->items = [] { return juce::StringArray { "OFF", "DIN", "USB" }; };
        r->current = [this, f] { return midiSettings().routes[(size_t) f]; };
        r->choose = [this, f] (int i) { changeMidi ([f, i] (MidiRemote::Settings& s) { s.routes[(size_t) f] = juce::jlimit (0, 2, i); }); };
        addAndMakeVisible (*r);
    }
    for (int i = 0; i < 4; ++i)
    {
        devices[(size_t) i] = std::make_unique<ChoiceBar> (t);
        const int port = i < 2 ? MidiRemote::Din : MidiRemote::Usb;
        bindDevice (*devices[(size_t) i], port, i % 2 == 0, juce::String (i < 2 ? "DIN" : "USB") + (i % 2 == 0 ? " IN" : " OUT"));
        addAndMakeVisible (*devices[(size_t) i]);
    }
    dawOn.isOn = [this] { return midiSettings().daw.enabled; };
    dawOn.onClick = [this] { changeMidi ([] (MidiRemote::Settings& s) { s.daw.enabled = ! s.daw.enabled; }); };
    dawEmulation.caption = "EMULATION";
    dawEmulation.items = [] { return juce::StringArray { "MCU", "HUI" }; };
    dawEmulation.current = [this] { return midiSettings().daw.emulation == DawRemote::Emulation::Hui ? 1 : 0; };
    dawEmulation.choose = [this] (int i)
    {
        changeMidi ([i] (MidiRemote::Settings& s) { s.daw.emulation = i == 1 ? DawRemote::Emulation::Hui : DawRemote::Emulation::Mcu; });
    };
    for (int i = 0; i < 2; ++i)
    {
        dawDevices[(size_t) i] = std::make_unique<ChoiceBar> (t);
        bindDevice (*dawDevices[(size_t) i], MidiRemote::Daw1, i == 0, i == 0 ? "MIDI DAW 1 IN" : "MIDI DAW 1 OUT");
        addAndMakeVisible (*dawDevices[(size_t) i]);
    }

    for (auto* child : std::initializer_list<juce::Component*> { &wingOn, &discoveryOn, &oscOn, &nativeOn, &consoleName, &discoveryPort, &oscPort,
                                                                 &nativePort, &webOn, &viewOnly, &webPort, &webPin, &dawOn, &dawEmulation })
        addAndMakeVisible (child);
    refresh();
    startTimerHz (2);
}

MidiRemote::Settings RemotePanel::midiSettings() const
{
    return ctx.midi != nullptr ? ctx.midi->getSettings() : MidiRemote::Settings::load (ctx.settings);
}

void RemotePanel::bindDevice (ChoiceBar& choice, int port, bool input, const juce::String& caption)
{
    // NONE, the app's virtual port (where the platform has them), the computer's
    // devices, and the device set if it isn't there (NOT FOUND), each with the
    // value the settings keep: a list by value, never by position.
    choice.caption = caption;
    auto entries = [this, port, input]
    {
        std::vector<std::pair<juce::String, juce::String>> list { { "NONE", {} } };
        if (MidiRemote::canCreateVirtualPorts())
            list.push_back ({ MidiRemote::virtualPortName (port), MidiRemote::virtualDevice });
        for (const auto& d : input ? MidiRemote::getInputDevices() : MidiRemote::getOutputDevices())
            list.push_back ({ d, d });
        const auto s = midiSettings();
        const auto& set = input ? s.inputs[(size_t) port] : s.outputs[(size_t) port];
        if (set.isNotEmpty() && std::none_of (list.begin(), list.end(), [&set] (const auto& e) { return e.second == set; }))
            list.push_back ({ (set == MidiRemote::virtualDevice ? juce::String ("VIRTUAL PORT") : set) + " (NOT FOUND)", set });
        return list;
    };
    choice.items = [entries]
    {
        juce::StringArray names;
        for (const auto& e : entries())
            names.add (e.first);
        return names;
    };
    choice.current = [this, port, input, entries]
    {
        const auto s = midiSettings();
        const auto& set = input ? s.inputs[(size_t) port] : s.outputs[(size_t) port];
        const auto list = entries();
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i].second == set)
                return (int) i;
        return 0;
    };
    choice.choose = [this, port, input, entries] (int i)
    {
        const auto list = entries();
        if (! juce::isPositiveAndBelow (i, (int) list.size()))
            return;
        const auto device = list[(size_t) i].second;
        changeMidi ([port, input, device] (MidiRemote::Settings& s) { (input ? s.inputs : s.outputs)[(size_t) port] = device; });
    };
}

void RemotePanel::changeWing (const std::function<void (WingRemote::Settings&)>& change)
{
    auto s = WingRemote::Settings::load (ctx.settings);
    change (s);
    s.save (ctx.settings);
    if (ctx.midi != nullptr)
        ctx.midi->consoleName = s.name;
    for (const auto& e : ctx.engine.getWing().apply (s))
        ctx.log.error ("WING remote: " + e);
    refresh();
}

void RemotePanel::changeWeb (const std::function<void (WebRemote::Settings&)>& change)
{
    auto s = WebRemote::Settings::load (ctx.settings);
    change (s);
    s.save (ctx.settings);
    if (ctx.web != nullptr)
        if (const auto error = ctx.web->apply (s); error.isNotEmpty())
            ctx.log.error ("Web remote: " + error);
    refresh();
}

void RemotePanel::changeMidi (const std::function<void (MidiRemote::Settings&)>& change)
{
    auto s = midiSettings();
    change (s);
    midiui::applySettings (ctx, s);
    refresh();
}

juce::StringArray RemotePanel::getWingStatus() const { return wingStatus; }
juce::StringArray RemotePanel::getWebStatus() const { return webStatus; }

void RemotePanel::refresh()
{
    for (auto* k : { &wingOn, &discoveryOn, &oscOn, &nativeOn, &webOn, &viewOnly, &dawOn })
        k->refresh();
    for (auto* f : { &consoleName, &discoveryPort, &oscPort, &nativePort, &webPort, &webPin })
        f->refresh();
    for (auto& r : routes)
        if (r != nullptr)
            r->refresh();
    for (auto& d : devices)
        d->refresh();
    for (auto& d : dawDevices)
        d->refresh();
    dawEmulation.refresh();

    // The status lines: the WING remote's clients, the web remote's addresses and browsers.
    juce::StringArray wingNow, webNow;
    const auto ws = ctx.engine.getWing().getStatus();
    if (! WingRemote::Settings::load (ctx.settings).enabled)
        wingNow.add ("WING REMOTE IS OFF");
    else
    {
        wingNow.addArray (ws.errors);
        wingNow.add (juce::String ((int) ws.clients.size()) + (ws.clients.size() == 1 ? " CLIENT" : " CLIENTS"));
        for (const auto& cl : ws.clients)
            wingNow.add (cl.address + "  " + (cl.native ? "NATIVE" : "OSC"));
    }
    if (ctx.web == nullptr || ! WebRemote::Settings::load (ctx.settings).enabled)
        webNow.add ("WEB REMOTE IS OFF");
    else
    {
        const auto st = ctx.web->getStatus();
        if (st.error.isNotEmpty())
            webNow.add (st.error);
        for (const auto& url : WebRemote::getUrls (st.port))
            webNow.add (url);
        webNow.add (juce::String (st.numWatching()) + " WATCHING");
    }
    if (wingNow != wingStatus || webNow != webStatus)
    {
        wingStatus = wingNow;
        webStatus = webNow;
        repaint();
    }
}

void RemotePanel::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    auto panel = [&] (juce::Rectangle<int> r, const juce::String& title, bool tagged)
    {
        g.setColour (theme.panel);
        g.fillRoundedRectangle (r.toFloat(), sizes::kRadius);
        auto t = r.withHeight (kTitle).reduced (sizes::kGap + 4, 0);
        g.setColour (theme.text);
        g.setFont (type::font (type::kLabel + 2.0f, true));
        g.drawText (title, t, juce::Justification::centredLeft);
        if (tagged)
            drawDeviceTag (g, t.toFloat(), devices::wing, theme);
    };
    panel (wingPanel, "WING REMOTE", true);
    panel (webPanel, "WEB REMOTE", false);
    panel (midiPanel, "MIDI", true);
    auto lines = [&] (juce::Rectangle<int> r, const juce::StringArray& text)
    {
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kLabel));
        for (const auto& line : text)
        {
            if (r.getHeight() < 18)
                break;
            g.drawFittedText (line, r.removeFromTop (20), juce::Justification::centredLeft, 1, 0.7f);
        }
    };
    lines (wingStatusArea, wingStatus);
    lines (webStatusArea, webStatus);
}

void RemotePanel::resized()
{
    auto area = getLocalBounds().reduced (sizes::kGap);
    const int w = (area.getWidth() - 2 * sizes::kGap) / 3;
    wingPanel = area.removeFromLeft (w);
    area.removeFromLeft (sizes::kGap);
    webPanel = area.removeFromLeft (w);
    area.removeFromLeft (sizes::kGap);
    midiPanel = area;

    auto column = [] (juce::Rectangle<int> p)
    {
        auto r = p.reduced (sizes::kGap);
        r.removeFromTop (kTitle - sizes::kGap);
        return r;
    };
    auto row = [] (juce::Rectangle<int>& r, int h)
    {
        auto out = r.removeFromTop (h);
        r.removeFromTop (sizes::kGapSmall);
        return out;
    };
    // WING REMOTE: ENABLE, then each server's key beside its port, the name, the status.
    auto c = column (wingPanel);
    wingOn.setBounds (row (c, sizes::kTouch));
    for (auto [key, port] : { std::pair { &discoveryOn, &discoveryPort }, { &oscOn, &oscPort }, { &nativeOn, &nativePort } })
    {
        auto r = row (c, sizes::kTouch);
        key->setBounds (r.removeFromLeft (120));
        r.removeFromLeft (sizes::kGapSmall);
        port->setBounds (r);
    }
    consoleName.setBounds (row (c, kRow));
    c.removeFromTop (sizes::kGap);
    wingStatusArea = c;

    c = column (webPanel);
    webOn.setBounds (row (c, sizes::kTouch));
    webPort.setBounds (row (c, kRow));
    webPin.setBounds (row (c, kRow));
    viewOnly.setBounds (row (c, sizes::kTouch));
    c.removeFromTop (sizes::kGap);
    webStatusArea = c;

    c = column (midiPanel);
    for (auto& r : routes)
        if (r != nullptr)
            r->setBounds (row (c, kRow - 6));
    c.removeFromTop (sizes::kGapSmall);
    for (auto& d : devices)
        d->setBounds (row (c, kRow - 6));
    c.removeFromTop (sizes::kGapSmall);
    auto dawRow = row (c, sizes::kTouch);
    dawOn.setBounds (dawRow.removeFromLeft (dawRow.getWidth() / 2).withTrimmedRight (sizes::kGapSmall / 2));
    dawEmulation.setBounds (dawRow.withTrimmedLeft (sizes::kGapSmall / 2));
    for (auto& d : dawDevices)
        d->setBounds (row (c, kRow - 6));
}

} // namespace livedaw::ui::universal
