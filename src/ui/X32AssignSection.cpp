#include "ui/X32AssignSection.h"
#include "ui/HardwareLook.h"

#include "engine/Models.h"
#include "engine/Show.h"
#include "engine/WingRemote.h"
#include "engine/X32Midi.h"

namespace livedaw::ui::x32
{

using namespace universal;

namespace
{
    constexpr int kKey = 34; // a key and its name under it
    const juce::Colour kLit (0xffffc83a);
    const char* const kSets[] = { "A", "B", "C" };

    /** What strip number `xx` (0-79) names, as a short label. */
    juce::String targetName (int xx)
    {
        if (xx < 32)
            return "CH " + juce::String (xx + 1);
        if (xx < 40)
            return "AUX " + juce::String (xx - 31);
        if (xx < 48)
            return "FX " + juce::String ((xx - 40) / 2 + 1) + ((xx - 40) % 2 == 0 ? "L" : "R");
        if (xx < 64)
            return "BUS " + juce::String (xx - 47);
        if (xx < 70)
            return "MTX " + juce::String (xx - 63);
        if (xx == 70)
            return "LR";
        if (xx == 71)
            return "M/C";
        if (xx < 80)
            return "DCA " + juce::String (xx - 71);
        return "GRP " + juce::String (xx - 79);
    }

    /** The selected strip's controls of a "D" code: '@' the fader, A-E the gate's, F-L the compressor's. */
    struct SelectedParam
    {
        char code;
        bool gate;
        const char* id;
        const char* label;
    };
    const SelectedParam kSelected[] = { { 'A', true, "thr", "GATE THR" },   { 'B', true, "range", "GATE RNG" }, { 'C', true, "att", "GATE ATT" },
                                        { 'D', true, "hld", "GATE HLD" },   { 'E', true, "rel", "GATE REL" },   { 'F', false, "thr", "COMP THR" },
                                        { 'G', false, "ratio", "RATIO" },   { 'H', false, "knee", "KNEE" },     { 'I', false, "mgain", "COMP GAIN" },
                                        { 'J', false, "att", "COMP ATT" },  { 'K', false, "hld", "COMP HLD" },  { 'L', false, "rel", "COMP REL" } };
} // namespace

AssignSection::AssignSection (UiContext& c, const Theme& t) : ctx (c), theme (t)
{
    // Encoders in their LED rings; what they and the keys do is on the LCDs.
    for (auto& k : encoders)
    {
        k.setHardware (true);
        addAndMakeVisible (k);
    }
    for (int i = 0; i < 8; ++i)
    {
        auto key = std::make_unique<ToggleKey> (juce::String (i + 5), kLit, t);
        key->belowText = juce::String (i + 5);
        key->onClick = [this, i] { pressButton (shownCodes[(size_t) (4 + i)], i); };
        key->isOn = [this, i] { return buttonLit (shownCodes[(size_t) (4 + i)], i); };
        addAndMakeVisible (*key);
        buttons.push_back (std::move (key));
    }
    for (int s = 0; s < 3; ++s)
    {
        auto key = std::make_unique<ToggleKey> (juce::String ("SET ") + kSets[s], juce::Colour (0xffff7470), t);
        key->belowText = kSets[s];
        key->onClick = [this, s] { setSet (s); };
        key->isOn = [this, s] { return set == s; };
        addAndMakeVisible (*key);
        setKeys.push_back (std::move (key));
    }
    viewKey = std::make_unique<ToggleKey> ("VIEW", kLit, t);
    viewKey->setEnabled (false);
    viewKey->setTooltip ("Not on Sidecar's X32 screen yet");
    addAndMakeVisible (*viewKey);
    refreshCodes();
    startTimerHz (5);
}

AssignSection::~AssignSection()
{
    stopTimer();
}

void AssignSection::setSet (int s)
{
    set = juce::jlimit (0, 2, s);
    for (auto& k : setKeys)
        k->refresh();
    refreshCodes();
}

juce::String AssignSection::code (bool button, int n) const
{
    int applied = 0;
    const auto address = juce::String ("/config/userctrl/") + kSets[set] + (button ? "/btn/" : "/enc/") + juce::String (n);
    const auto answer = ctx.engine.getWing().runX32Text (address, applied);
    const auto text = answer.fromFirstOccurrenceOf (" ", false, false).trim().unquoted();
    return text.isEmpty() ? juce::String ("-") : text;
}

void AssignSection::refreshCodes()
{
    for (int i = 0; i < 4; ++i)
        shownCodes[(size_t) i] = code (false, i + 1);
    for (int i = 0; i < 8; ++i)
        shownCodes[(size_t) (4 + i)] = code (true, i + 5);
    shownSelected = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
    for (int i = 0; i < 4; ++i)
        encoders[(size_t) i].setBinding (bindEncoder (shownCodes[(size_t) i], i));
    for (int i = 0; i < 8; ++i)
    {
        const auto& c = shownCodes[(size_t) (4 + i)];
        buttons[(size_t) i]->setButtonText (c == "-" ? juce::String (i + 5) : describe (c, true));
        buttons[(size_t) i]->refresh();
    }
    repaint();
}

void AssignSection::timerCallback()
{
    // Codes set elsewhere (a remote app, a scene) and, for D codes, the selection.
    bool changed = shownSelected != (ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr);
    for (int i = 0; i < 12 && ! changed; ++i)
        changed = shownCodes[(size_t) i] != code (i >= 4, i + 1); // encoders 1-4, buttons 5-12
    if (changed)
        refreshCodes();
    for (auto* keys : { &buttons, &setKeys })
        for (auto& k : *keys)
            k->refresh();
}

Strip* AssignSection::stripAt (int xx) const
{
    auto& e = ctx.engine;
    auto pick = [] (const MixerEngine::StripList& list, int i) -> Strip*
    { return juce::isPositiveAndBelow (i, (int) list.size()) ? list[(size_t) i].get() : nullptr; };
    if (xx < 32)
        return pick (e.getChannels(), xx);
    if (xx < 48)
        return pick (e.getAux(), xx - 32); // aux in 1-8, then the FX returns
    if (xx < 64)
        return pick (e.getBuses(), xx - 48);
    if (xx < 70)
        return pick (e.getMatrices(), xx - 64);
    if (xx < 72)
        return pick (e.getMains(), xx - 70);
    return nullptr;
}

juce::String AssignSection::describe (const juce::String& c, bool button)
{
    if (c.isEmpty() || c == "-")
        return {};
    const auto kind = c[0];
    const int xx = c.substring (1, 3).getIntValue();
    switch (kind)
    {
        case 'F': return "FDR " + targetName (xx);
        case 'P': return button ? "PAGE" : "PAN " + targetName (xx);
        case 'S':
            if (button)
                return c.startsWith ("S0") ? "SCENE " + juce::String (c.substring (2).getIntValue()) : c.startsWith ("S9") ? juce::StringArray { "PREV", "NEXT", "UNDO", "GO" }[c.substring (2).getIntValue() % 4] : juce::String ("CUE");
            return targetName (xx) + ">B" + juce::String (c.substring (3, 5).getIntValue() + 1);
        case 'X': return "FX" + juce::String (c.substring (1, 2).getIntValue() + 1) + " P" + juce::String (c.substring (2, 4).getIntValue() + 1);
        case 'O': return xx >= 80 ? "MUTE G" + juce::String (xx - 79) : "MUTE " + targetName (xx);
        case 'I': return "INS " + targetName (xx);
        case 'M':
        case 'm': return "MIDI " + juce::String (c.substring (1, 2)).toUpperCase() + juce::String (c.substring (2, 4).getIntValue());
        case 'D':
            if (c[1] == '@')
                return "SEL FDR";
            for (const auto& p : kSelected)
                if ((juce::juce_wchar) p.code == c[1])
                    return juce::String ("SEL ") + p.label;
            return {};
        default: return c; // remote, X-Live!, USB recorder: not Sidecar's
    }
}

KnobBinding AssignSection::bindEncoder (const juce::String& c, int index)
{
    KnobBinding none;
    none.label = juce::String (index + 1);
    if (c == "-" || c.isEmpty())
        return none;
    const int xx = c.substring (1, 3).getIntValue();
    KnobBinding b;
    switch (c[0])
    {
        case 'F':
            if (auto* s = stripAt (xx))
                b = bindFader ("", s->params.faderDb);
            else if (xx >= 72 && xx < 80 && xx - 72 < ctx.engine.getNumDcas())
                b = bindFader ("", ctx.engine.getDca (xx - 72).faderDb);
            break;
        case 'P':
            if (auto* s = stripAt (xx))
                b = bindPan ("", s->params.pan);
            break;
        case 'S':
            if (auto* s = stripAt (xx); s != nullptr && s->isInput())
                b = bindFader ("", s->sends[(size_t) juce::jlimit (0, 15, c.substring (3, 5).getIntValue())].levelDb);
            break;
        case 'X':
        {
            auto& fx = ctx.engine.getFx();
            const int slot = c.substring (1, 2).getIntValue(), k = c.substring (2, 4).getIntValue();
            const int m = fx.getModel (slot);
            if (m > ld::kFxExt && k < (int) models::model (models::Fx, m).params.size())
                b = bindModelParam (fx.params (slot).values[k], models::model (models::Fx, m).params[(size_t) k]);
            break;
        }
        case 'D':
        {
            auto* s = shownSelected;
            if (s == nullptr)
                break;
            if (c[1] == '@')
            {
                b = bindFader ("", s->params.faderDb);
                break;
            }
            for (const auto& p : kSelected)
                if ((juce::juce_wchar) p.code == c[1])
                {
                    auto& block = p.gate ? s->params.gate() : s->params.dynamics();
                    const auto& m = models::model (models::Dynamics, block.model.load());
                    if (const int i = m.find (p.id); i >= 0)
                        b = bindModelParam (block.values[i], m.params[(size_t) i]);
                }
            break;
        }
        case 'M':
        {
            // "MCyyzzz": CC zzz on channel yy with the encoder's value; "MN": note zzz,
            // velocity the value; "MP": program change to the value.
            const auto type = c[1];
            const int channel = juce::jlimit (1, 16, c.substring (2, 4).getIntValue());
            const int number = juce::jlimit (0, 127, c.substring (4, 7).getIntValue());
            b.get = [this, index] { return midiEncoder[(size_t) index]; };
            b.set = [this, index, type, channel, number] (float v)
            {
                midiEncoder[(size_t) index] = juce::jlimit (0.0f, 1.0f, v);
                const int value = juce::roundToInt (midiEncoder[(size_t) index] * 127.0f);
                auto* midi = dynamic_cast<X32Midi*> (ctx.consoleMidi);
                if (midi == nullptr)
                    return;
                if (type == 'C')
                    midi->sendUser (juce::MidiMessage::controllerEvent (channel, number, value));
                else if (type == 'N')
                    midi->sendUser (juce::MidiMessage::noteOn (channel, number, (juce::uint8) value));
                else if (type == 'P')
                    midi->sendUser (juce::MidiMessage::programChange (channel, value));
            };
            b.text = [this, index] { return juce::String (juce::roundToInt (midiEncoder[(size_t) index] * 127.0f)); };
            b.steps = 128.0f;
            break;
        }
        default:
            break;
    }
    b.label = describe (c, false);
    return b.get ? b : none;
}

void AssignSection::pressButton (const juce::String& c, int index)
{
    if (c == "-" || c.isEmpty())
        return;
    auto& e = ctx.engine;
    const int xx = c.substring (1, 3).getIntValue();
    switch (c[0])
    {
        case 'O':
            if (auto* s = stripAt (xx))
                s->params.mute = ! s->params.mute.load();
            else if (xx >= 72 && xx < 80 && xx - 72 < e.getNumDcas())
                e.getDca (xx - 72).mute = ! e.getDca (xx - 72).mute.load();
            else if (xx >= 80 && xx < 86)
                e.muteGroups.set (xx - 80, ! e.muteGroups.contains (xx - 80));
            break;
        case 'I':
            if (auto* s = stripAt (xx))
                s->params.preInsert().on = ! s->params.preInsert().on.load();
            break;
        case 'X':
        {
            // An FX parameter between its lowest and highest.
            auto& fx = e.getFx();
            const int slot = c.substring (1, 2).getIntValue(), k = c.substring (2, 4).getIntValue();
            const int m = fx.getModel (slot);
            if (m > ld::kFxExt && k < (int) models::model (models::Fx, m).params.size())
            {
                const auto& p = models::model (models::Fx, m).params[(size_t) k];
                auto& v = fx.params (slot).values[k];
                v = v.load() >= (p.min + p.max) * 0.5f ? p.min : p.max;
            }
            break;
        }
        case 'M':
        case 'm':
        {
            // Upper case: push (on, then off); lower case: toggle.
            auto* midi = dynamic_cast<X32Midi*> (ctx.consoleMidi);
            const bool toggle = c[0] == 'm';
            const auto type = juce::CharacterFunctions::toUpperCase (c[1]);
            const int channel = juce::jlimit (1, 16, c.substring (2, 4).getIntValue());
            const int number = juce::jlimit (0, 127, c.substring (4, 7).getIntValue());
            const bool on = toggle ? (midiToggle[(size_t) index] = ! midiToggle[(size_t) index]) : true;
            if (midi == nullptr)
                break;
            auto send = [midi, type, channel, number] (bool down)
            {
                if (type == 'C')
                    midi->sendUser (juce::MidiMessage::controllerEvent (channel, number, down ? 127 : 0));
                else if (type == 'N')
                    midi->sendUser (down ? juce::MidiMessage::noteOn (channel, number, (juce::uint8) 127) : juce::MidiMessage::noteOff (channel, number));
                else if (type == 'P' && down)
                    midi->sendUser (juce::MidiMessage::programChange (channel, number));
            };
            send (on);
            if (! toggle)
                send (false);
            break;
        }
        case 'S':
        {
            auto& show = ctx.show;
            const int n = c.substring (2).getIntValue();
            if (c.startsWith ("S0") && juce::isPositiveAndBelow (n, show.getNumScenes()))
                show.recallScene (n);
            else if (c.startsWith ("S9"))
            {
                // Show control: PREV, NEXT, (UNDO), GO.
                const int current = show.getCurrentSceneIndex();
                const int to = n == 0 ? current - 1 : n == 1 ? current + 1 : n == 3 ? juce::jmax (0, current) : -1;
                if (juce::isPositiveAndBelow (to, show.getNumScenes()))
                    show.recallScene (to);
            }
            break;
        }
        case 'P':
        {
            // "Pxxyz": a page (y the screen: 0 channel, 1 meters, 2 routing, 3 setup,
            // 4 library, 5 effects, 8 scenes; z its tab), selecting strip xx for 0.
            const int target = c.substring (3, 4).getIntValue();
            const auto z = c.substring (4, 5);
            const int tab = z == "S" ? 0 : z.getIntValue();
            static const int screens[] = { 0, 1, 2, 3, 4, 5, -1, -1, 4, -1 }; // Display::Screen
            if (onJump && screens[target] >= 0)
                onJump (screens[target], tab, target == 0 ? stripAt (xx) : nullptr);
            break;
        }
        default:
            break;
    }
    buttons[(size_t) index]->refresh();
}

bool AssignSection::buttonLit (const juce::String& c, int index) const
{
    if (c == "-" || c.isEmpty())
        return false;
    auto& e = ctx.engine;
    const int xx = c.substring (1, 3).getIntValue();
    switch (c[0])
    {
        case 'O':
            if (auto* s = stripAt (xx))
                return s->params.mute.load();
            if (xx >= 72 && xx < 80 && xx - 72 < e.getNumDcas())
                return e.getDca (xx - 72).mute.load();
            return xx >= 80 && xx < 86 && e.muteGroups.contains (xx - 80);
        case 'I':
            if (auto* s = stripAt (xx))
                return s->params.preInsert().on.load();
            return false;
        case 'm': return midiToggle[(size_t) index];
        default: return false;
    }
}

void AssignSection::paint (juce::Graphics& g)
{
    const auto& look = hw::Look::x32();
    hw::section (g, getLocalBounds().toFloat().reduced (1.0f).withTrimmedTop (6.0f), "ASSIGN", look);
    for (int i = 0; i < 4; ++i)
        hw::silk (g, encoders[(size_t) i].getBounds().toFloat().withY ((float) encoders[(size_t) i].getBottom()).withHeight (11.0f), juce::String (i + 1),
                  look, 9.0f);
    // Each encoder's LCD: what it turns and what the two keys under it do (n, n + 4, n + 8).
    for (int i = 0; i < 4; ++i)
    {
        auto lcd = lcds[(size_t) i].toFloat();
        if (lcd.isEmpty())
            continue;
        g.setColour (juce::Colour (0xff0b0c0c));
        g.fillRoundedRectangle (lcd.expanded (2.0f), 3.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4fe06a), lcd.getX(), lcd.getY(), juce::Colour (0xff2fae48), lcd.getX(), lcd.getBottom(), false));
        g.fillRect (lcd);
        const auto& enc = encoders[(size_t) i].getBinding();
        // An unassigned key shows its number as its text: "-" on the LCD.
        const auto what = [this] (int b)
        {
            const auto text = buttons[(size_t) b]->getButtonText();
            return text == juce::String (b + 5) ? juce::String ("-") : text;
        };
        const juce::String lines[] = { juce::String (i + 1) + " " + (enc.isValid() ? enc.label : juce::String ("-")), juce::String (i + 5) + " " + what (i),
                                       juce::String (i + 9) + " " + what (4 + i) };
        const float h = lcd.getHeight() / 3.0f;
        g.setColour (juce::Colour (0xff0c2a12));
        g.setFont (universal::type::font (juce::jmin (9.5f, h * 0.9f), true));
        for (int k = 0; k < 3; ++k)
            g.drawFittedText (lines[k], lcd.withY (lcd.getY() + h * (float) k).withHeight (h).reduced (2.0f, 0.0f).toNearestInt(),
                              juce::Justification::centredLeft, 1, 0.6f);
    }
    if (! setKeys.empty())
    {
        hw::silk (g, setKeys.front()->getBounds().toFloat().withX ((float) setKeys.front()->getX() - 30.0f).withWidth (28.0f).withHeight (24.0f), "SET",
                  look, 9.5f, juce::Justification::centredRight);
        // The printed line joining A, B and C.
        const float y = (float) setKeys.front()->getY() + 12.0f;
        g.setColour (look.silkDim);
        g.drawLine ((float) setKeys.front()->getX() - 2.0f, y, (float) setKeys.back()->getRight() + 2.0f, y, 1.0f);
    }
}

void AssignSection::resized()
{
    // As on the panel: the four encoders in a row, their LCDs under them, the
    // keys 5-8 and 9-12, SET A, B, C along the bottom.
    auto r = getLocalBounds().reduced (8, 6);
    r.removeFromTop (18);
    const int w = r.getWidth() / 4;
    const int size = juce::jmin (w - 4, 56);
    auto encRow = r.removeFromTop (size + 12);
    for (int i = 0; i < 4; ++i)
        encoders[(size_t) i].setBounds (juce::Rectangle<int> (encRow.getX() + w * i, encRow.getY(), w, size).withSizeKeepingCentre (size, size));
    r.removeFromTop (4);
    auto lcdRow = r.removeFromTop (42);
    for (int i = 0; i < 4; ++i)
        lcds[(size_t) i] = juce::Rectangle<int> (lcdRow.getX() + w * i, lcdRow.getY(), w, lcdRow.getHeight()).reduced (4, 2);
    r.removeFromTop (8);
    for (int row = 0; row < 2; ++row)
    {
        auto keys = r.removeFromTop (kKey);
        for (int i = 0; i < 4; ++i)
            buttons[(size_t) (row * 4 + i)]->setBounds (juce::Rectangle<int> (keys.getX() + w * i, keys.getY(), w, kKey).withSizeKeepingCentre (40, kKey));
        r.removeFromTop (6);
    }
    auto sets = r.removeFromTop (kKey).withTrimmedLeft (w);
    viewKey->setBounds (sets.removeFromRight (40).withSizeKeepingCentre (34, kKey));
    const int sw = sets.getWidth() / 3;
    for (int s = 0; s < 3; ++s)
        setKeys[(size_t) s]->setBounds (juce::Rectangle<int> (sets.getX() + sw * s, sets.getY(), sw, kKey).withSizeKeepingCentre (30, kKey));
}

} // namespace livedaw::ui::x32
