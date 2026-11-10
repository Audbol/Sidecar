#include "ui/RackSection.h"

namespace livedaw::ui
{

RackSection::RackSection (UiContext& c, FaderSection& ch, CustomControlSection& cs) : ctx (c), channels (ch), controls (cs)
{
    // INPUT AUX, BUSES MAIN, DCA MUTE, CUST TRANS: each toggles between its two layers.
    const char* names[kNumKeys] = { "INPUT\nAUX", "BUSES\nMAIN", "DCA\nMUTE", "CUST\nTRANS" };
    const char* tips[kNumKeys] = { "INPUT AUX: input channels 1-40; again: aux channels 1-8", "BUSES MAIN: buses 1-16; again: main 1-4 and matrix 1-8",
                                   "DCA MUTE: DCA 1-16; again: the eight mute groups on the eight keys",
                                   "CUST TRANS: the custom controls (16 layers); again: the USB player's transport, then the multitrack recorder's" };
    for (int i = 0; i < kNumKeys; ++i)
    {
        auto* k = layerKeys.add (new SurfaceKey (names[i]));
        k->ledColour = Colours::keyLayer;
        k->setTooltip (tips[i]);
        k->onPress = [this, i] { pressKey (i); };
        k->ledState = [this, i] { return isKeyLit (i) ? SurfaceKey::Lit : SurfaceKey::Off; };
        addAndMakeVisible (k);
    }

    // "<4" and "4>": lit while there is more that way; held: the first / last.
    for (auto* k : { &shiftLeft, &shiftRight })
    {
        const int direction = k == &shiftLeft ? -1 : 1;
        k->arrow = direction;
        k->ledColour = Colours::keyWhite;
        k->onPress = [this, direction] { shift (direction, false); };
        k->onHold = [this, direction] { shift (direction, true); };
        k->ledState = [this, direction]
        {
            if (mode == Mode::Custom)
                return SurfaceKey::Lit;
            return mode == Mode::Channels && channels.canShift (direction) ? SurfaceKey::Lit : SurfaceKey::Off;
        };
        addAndMakeVisible (k);
    }
    shiftLeft.setTooltip ("Four channels back (hold: the layer's first); on CUST: the previous custom control layer");
    shiftRight.setTooltip ("Four channels on (hold: the layer's last); on CUST: the next custom control layer");

    // VIEW (WING manual Table 4.6): OVERVIEW, again FADERS; held, CONFIG.
    viewKey.setTooltip ("VIEW: OVERVIEW of the layer shown, again: FADERS (hold: CONFIG). On CUST: CUSTOM CONTROLS");
    viewKey.onPress = [this] { pressView (false); };
    viewKey.onHold = [this] { pressView (true); };
    viewKey.isActive = [this]
    {
        if (! ctx.getScreen)
            return false;
        const auto screen = ctx.getScreen();
        switch (mode)
        {
            case Mode::Channels:
                return ctx.viewSection == LayerSet::Left
                       && (screen == Screen::Overview || screen == Screen::Faders || screen == Screen::Config || screen == Screen::Sends);
            case Mode::MuteGroups:     return screen == Screen::MuteGroups;
            case Mode::UsbTransport:   return screen == Screen::UsbRecorder;
            case Mode::MultiTransport: return screen == Screen::Recorder;
            case Mode::Custom:         break;
        }
        // CUSTOM CONTROLS is a page of SETUP: lit from the press until the display leaves it.
        viewOpened = viewOpened && screen == Screen::Setup;
        return viewOpened;
    };
    addAndMakeVisible (viewKey);

    // PHONES LEVEL: the PHONES path, as the full-size console's pot (manual 4.7 p. 24).
    phonesLevel.setHardware (true);
    phonesLevel.setCap (DisplayKnob::Cap::Pot);
    phonesLevel.setBinding (bindLinear ("PHONES LEVEL", ctx.engine.phones.levelDb, -60.0f, 10.0f, " dB", 1, 0.0f));
    phonesLevel.setTooltip ("LEVEL: the level of the PHONES monitor path");
    addAndMakeVisible (phonesLevel);

    startTimerHz (8);
}

void RackSection::setActive (bool shouldBeActive)
{
    active = shouldBeActive;
    channels.setRackStyle (active);
    if (active)
        applyMode();
    else
        controls.setRackMode (CustomControlSection::RackMode::Off);
}

void RackSection::refresh()
{
    // A spill (FX SPILL: the device's parameters on the four encoders, manual
    // 6.1 p. 71) brings the channels back when it starts.
    const bool spilling = channels.getLayerId() == LayerSet::kSpillId;
    if (active && spilling && ! spillShown)
        setMode (Mode::Channels);
    spillShown = spilling;
    shownDisplay = layerDisplayText();
    repaint();
}

void RackSection::setMode (Mode m)
{
    if (m == mode)
        return;
    mode = m;
    viewOpened = false;
    applyMode();
    repaint();
}

void RackSection::applyMode()
{
    if (! active)
        return;
    using RackMode = CustomControlSection::RackMode;
    // (With the channels showing, the custom controls keep their RACK layout, hidden.)
    controls.setRackMode (mode == Mode::MuteGroups       ? RackMode::MuteGroups
                          : mode == Mode::UsbTransport   ? RackMode::TwoTrack
                          : mode == Mode::MultiTransport ? RackMode::MultiTrack
                                                         : RackMode::Custom);
    if (onModeChanged)
        onModeChanged();
}

void RackSection::showChannels()
{
    setMode (Mode::Channels);
}

void RackSection::toggleLayers (const char* first, const char* second)
{
    const bool onFirst = mode == Mode::Channels && channels.getLayerId() == first;
    setMode (Mode::Channels);
    channels.selectLayer (onFirst ? second : first);
}

void RackSection::pressKey (int key)
{
    switch (key)
    {
        case InputAux:  toggleLayers (kInputs, kAux); break;
        case BusesMain: toggleLayers (kBuses, kMainMatrix); break;
        case DcaMute:
            // DCA 1-16, then the mute groups (the encoders do nothing there).
            if (mode == Mode::Channels && channels.getLayerId() == kDca)
                setMode (Mode::MuteGroups);
            else
            {
                setMode (Mode::Channels);
                channels.selectLayer (kDca);
            }
            break;
        case CustTrans:
            setMode (mode == Mode::Custom ? Mode::UsbTransport : mode == Mode::UsbTransport ? Mode::MultiTransport : Mode::Custom);
            break;
        default: break;
    }
    refresh();
}

bool RackSection::isKeyLit (int key) const
{
    const auto& layer = channels.getLayerId();
    const bool ch = mode == Mode::Channels;
    switch (key)
    {
        case InputAux:  return ch && (layer == kInputs || layer == kAux);
        case BusesMain: return ch && (layer == kBuses || layer == kMainMatrix);
        case DcaMute:   return mode == Mode::MuteGroups || (ch && layer == kDca);
        case CustTrans: return mode == Mode::Custom || mode == Mode::UsbTransport || mode == Mode::MultiTransport;
        default:        return false;
    }
}

void RackSection::shift (int direction, bool held)
{
    if (mode == Mode::Channels)
    {
        if (held)
            channels.jumpToEnd (direction);
        else
            channels.shift (direction);
    }
    else if (mode == Mode::Custom)
    {
        if (held && ctx.customControls != nullptr)
        {
            // The first or the last of the 16 layers.
            ctx.customControls->currentLayer = direction < 0 ? 0 : cc::kNumLayers - 1;
            if (ctx.customControlsChanged)
                ctx.customControlsChanged();
            else
                controls.refresh();
        }
        else if (! held)
            controls.stepLayer (direction);
    }
    refresh();
}

void RackSection::pressView (bool held)
{
    auto show = [this] (Screen s)
    {
        if (ctx.showScreen)
            ctx.showScreen (s);
    };
    switch (mode)
    {
        case Mode::Channels:
        {
            // First press: OVERVIEW of the layer shown; second: its FADERS; held: CONFIG.
            const bool onOverview = ctx.getScreen && ctx.getScreen() == Screen::Overview && ctx.viewSection == LayerSet::Left;
            ctx.viewSection = LayerSet::Left;
            show (held ? Screen::Config : onOverview ? Screen::Faders : Screen::Overview);
            break;
        }
        case Mode::Custom:
            if (ctx.showCustomControls)
                ctx.showCustomControls (controls.currentLayer());
            break;
        case Mode::MuteGroups:     show (Screen::MuteGroups); break;
        case Mode::UsbTransport:   show (Screen::UsbRecorder); break;
        case Mode::MultiTransport: show (Screen::Recorder); break;
    }
    viewOpened = true;
}

juce::String RackSection::layerDisplayText() const
{
    if (mode == Mode::Custom)
        return juce::String (controls.currentLayer() + 1);
    if (mode == Mode::Channels)
        return juce::String (channels.getOffset() / layout::rack::kChannels + 1);
    return {};
}

void RackSection::timerCallback()
{
    if (const auto text = layerDisplayText(); text != shownDisplay)
    {
        shownDisplay = text;
        repaint (plan.layerDisplay);
    }
}

void RackSection::paint (juce::Graphics& g)
{
    const auto& sc = surfaceColours();
    g.fillAll (sc.surface);

    // MUTE and SOLO, printed left of their rows.
    const float caption = juce::jlimit (7.0f, 11.0f, (float) plan.columnW * 0.11f);
    drawSilk (g, plan.muteLabel.toFloat(), "MUTE", caption, juce::Justification::centredRight, sc.surfacePrint);
    drawSilk (g, plan.soloLabel.toFloat(), "SOLO", caption, juce::Justification::centredRight, sc.surfacePrint);

    // The two-digit layer display: blue seven-segment digits over the unlit
    // "88" behind a smoked window (as the full-size console's custom controls').
    const auto display = plan.layerDisplay;
    g.setColour (juce::Colour (0xff232326));
    g.fillRoundedRectangle (display.toFloat(), 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (display.toFloat(), 2.0f, 1.0f);
    g.setFont (juce::Font (juce::FontOptions ((float) display.getHeight() * 0.9f, juce::Font::bold)).withHorizontalScale (0.8f));
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawText ("88", display.reduced (2, 0), juce::Justification::centredRight);
    g.setColour (Colours::segmentBlue);
    g.drawText (shownDisplay, display.reduced (2, 0), juce::Justification::centredRight);

    // The USB socket's plate: the socket, the ACCESS LED and the supply it gives.
    const auto plate = plan.usb.toFloat();
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (plate, 3.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (plate, 3.0f, 1.0f);
    auto inner = plate.reduced (juce::jmax (3.0f, plate.getWidth() * 0.05f), juce::jmax (2.0f, plate.getHeight() * 0.14f));
    auto socket = inner.removeFromLeft (inner.getWidth() * 0.5f);
    g.setColour (sc.silk);
    g.drawRoundedRectangle (socket, 2.0f, 1.5f);
    g.drawRect (socket.reduced (socket.getWidth() * 0.16f, socket.getHeight() * 0.3f), 1.0f);
    inner.removeFromLeft (inner.getWidth() * 0.12f);
    auto led = inner.removeFromTop (inner.getHeight() * 0.5f);
    g.setColour (sc.ledOff.brighter (0.2f));
    g.fillRect (led.withSizeKeepingCentre (juce::jmin (16.0f, led.getWidth() * 0.5f), juce::jmin (5.0f, led.getHeight() * 0.5f)));
    drawSilk (g, inner, "5V  1000mA", juce::jlimit (6.0f, 9.0f, inner.getHeight() * 0.7f));

    // LEVEL under the pot; the headphone socket and its mark.
    const float below = juce::jlimit (10.0f, 14.0f, (float) plan.columnW * 0.13f);
    drawSilk (g, plan.level.toFloat().withY ((float) plan.level.getBottom() + 2.0f).withHeight (below).expanded (20.0f, 0.0f), "LEVEL", caption, juce::Justification::centred,
              sc.surfacePrint);
    const auto jack = plan.phones.toFloat();
    g.setColour (juce::Colour (0xff2c2e32));
    g.fillEllipse (jack);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawEllipse (jack.reduced (0.5f), 1.0f);
    g.setColour (juce::Colour (0xff18191b));
    g.fillEllipse (jack.reduced (jack.getWidth() * 0.16f));
    g.setColour (juce::Colours::black);
    g.fillEllipse (jack.reduced (jack.getWidth() * 0.3f));
    drawIcon (g, Icon::Headphones, juce::Rectangle<float> (below, below).withCentre ({ jack.getCentreX(), jack.getBottom() + 2.0f + below * 0.5f }), sc.surfacePrint);
}

void RackSection::resized()
{
    plan = layout::rack::planSection (getLocalBounds());
    for (int i = 0; i < kNumKeys; ++i)
        layerKeys[i]->setBounds (plan.layerKeys[(size_t) i]);
    shiftLeft.setBounds (plan.shiftLeft);
    shiftRight.setBounds (plan.shiftRight);
    viewKey.setBounds (plan.view);
    phonesLevel.setBounds (plan.level);
}

} // namespace livedaw::ui
