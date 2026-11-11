#include "ui/DLiveSurface.h"
#include "ui/DLiveScreens.h"

#include "engine/Layers.h"
#include "engine/MixerEngine.h"
#include "engine/Models.h"
#include "engine/Show.h"
#include "ui/DotFont.h"
#include "ui/HardwareLook.h"
#include "ui/HomeScreens.h"
#include "ui/MeterBank.h"

#include <cmath>

namespace livedaw::ui
{

using namespace universal;
using dl::look;

namespace
{
    constexpr int kTopBar = 48;
    constexpr int kStripW = 118, kStripsX = 178, kStripsY = 500, kStripH = 576, kGroupGap = 14;
    int stripX (int i) { return kStripsX + i * kStripW + (i / 4) * kGroupGap; }

    // The touchscreen: the screens' 1000 by 690 at this scale, close to the proportions of a C1500's.
    constexpr int kScreenX = 114, kScreenY = 60;
    constexpr float kScreenScale = 0.625f;

    // The keys' colours, idle and lit (photographs of a C1500).
    const juce::Colour kMuteIdle (0xffcbc9c2), kMuteLit (0xfff2526c), kSelIdle (0xff2fa897), kSelLit (0xff36e0b2), kMixIdle (0xff4497df), kMixLit (0xff0c4fe6),
        kPaflIdle (0xffdad0c7), kPaflLit (0xfff2a432), kPale (0xffc9d9d2), kPaleLit (0xff17d57a), kSoft (0xffb9c6d4), kSoftLit (0xff8a35f0), kBlueIdle (0xff3a8fe0),
        kBlueLit (0xff0c4fe6), kTeal (0xff5bb394), kPink (0xffeb8596), kRed (0xffdc5060), kAmberKey (0xffe0a040), kAmberLit (0xfff5b83a);

} // namespace

namespace dl
{
    const hw::Look& look() { return hw::Look::dlive(); }

    void switchSurface (UiContext& context, const char* model)
    {
        juce::MessageManager::callAsync ([&context, m = juce::String (model)]
        {
            context.settings.setValue (uiprefs::dliveSurface, m);
            if (context.setInterface)
            {
                context.setInterface (InterfaceKind::Universal);
                context.setInterface (InterfaceKind::DLive);
            }
        });
    }

    void dotPrint (juce::Graphics& g, float x, float y, const juce::String& s, float dot)
    {
        for (auto p = s.getCharPointer(); ! p.isEmpty();)
        {
            const auto c = p.getAndAdvance();
            const auto* glyph = dots::kFont5x7[c >= 32 && c <= 126 ? (int) c - 32 : 0];
            for (int col = 0; col < 5; ++col)
                for (int row = 0; row < 7; ++row)
                    if ((glyph[col] >> row) & 1)
                        g.fillRect (x + (float) col * dot, y + (float) row * dot, dot * 0.85f, dot * 0.85f);
            x += 6.0f * dot;
        }
    }

    bool wantsAssign (bool setupHeld)
    {
        return setupHeld || juce::ModifierKeys::getCurrentModifiers().isPopupMenu();
    }

    KnobBinding bindEq (Strip& s, const juce::String& id, const juce::String& label)
    {
        auto& eq = s.params.eq();
        const auto& m = models::model ((models::Table) s.getEqTable(), eq.model.load());
        const int i = m.find (id);
        if (i < 0)
            return {};
        auto b = bindModelParam (eq.values[i], m.params[(size_t) i]);
        b.label = label;
        return b;
    }

    KnobBinding bindDyn (ld::DynSlotParams& d, const juce::String& id, const juce::String& label)
    {
        const auto& m = models::model (models::Dynamics, d.model.load());
        const int i = m.find (id);
        if (i < 0)
            return {};
        auto b = bindModelParam (d.values[i], m.params[(size_t) i]);
        b.label = label;
        return b;
    }

    Strip* stripOf (MixerEngine& e, const Slot& s)
    {
        using S = Slot;
        const MixerEngine::StripList* list = s.kind == S::Channel ? &e.getChannels()
                                             : s.kind == S::FxReturn ? &e.getAux()
                                             : s.kind == S::Bus ? &e.getBuses()
                                             : s.kind == S::Main ? &e.getMains()
                                             : s.kind == S::Matrix ? &e.getMatrices() : nullptr;
        return list != nullptr && juce::isPositiveAndBelow (s.index, (int) list->size()) ? (*list)[(size_t) s.index].get() : nullptr;
    }

    //==========================================================================
    Key::Key (const juce::String& text, juce::Colour i, juce::Colour l, Shape s, bool withLed)
        : juce::Button (text), idle (i), lit (l), shape (s), led (withLed)
    {
    }

    void Key::refresh()
    {
        const bool on = isOn && isOn();
        if (on != shownOn)
        {
            shownOn = on;
            repaint();
        }
    }

    void Key::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        auto body = getLocalBounds().toFloat().reduced (1.5f);
        const float radius = shape == Shape::Oval ? body.getHeight() * 0.5f : 7.0f;
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (body.translated (0.0f, 2.0f), radius);
        if (down)
            body = body.translated (0.0f, 1.0f);
        const auto face = shownOn ? lit : idle;
        g.setGradientFill (juce::ColourGradient (face.brighter (0.2f), 0.0f, body.getY(), face.darker (0.18f), 0.0f, body.getBottom(), false));
        g.fillRoundedRectangle (body, radius);
        if (shownOn)
        {
            g.setColour (lit.withAlpha (0.3f));
            g.drawRoundedRectangle (body.expanded (1.0f), radius + 1.0f, 2.0f);
        }
        if (highlighted)
        {
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.fillRoundedRectangle (body, radius);
        }
        auto textArea = body;
        if (led && body.getHeight() >= 24.0f)
        {
            const auto window = juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ body.getCentreX(), body.getY() + 6.0f });
            g.setColour (shownOn ? juce::Colours::white : face.darker (0.6f));
            g.fillEllipse (window);
            textArea.removeFromTop (6.0f);
        }
        g.setColour (shownOn && lit.getPerceivedBrightness() < 0.45f ? juce::Colours::white : juce::Colour (0xff17191b));
        g.setFont (type::font (juce::jlimit (9.0f, 12.5f, body.getHeight() * 0.42f), true));
        g.drawFittedText (getButtonText(), textArea.reduced (2.0f, 0.0f).toNearestInt(), juce::Justification::centred, 2, 0.7f);
    }

    //==========================================================================
    Rotary::Rotary (juce::Colour l) : light (l) {}

    void Rotary::setBinding (KnobBinding b)
    {
        binding = std::move (b);
        shown = -1.0f;
        repaint();
    }

    void Rotary::setLight (juce::Colour c)
    {
        if (c != light)
        {
            light = c;
            repaint();
        }
    }

    void Rotary::refresh()
    {
        const float v = binding.isValid() ? binding.get() : -1.0f;
        if (std::abs (v - shown) > 0.001f)
        {
            shown = v;
            repaint();
        }
    }

    void Rotary::paint (juce::Graphics& g)
    {
        const auto b = getLocalBounds().toFloat().reduced (2.0f);
        const auto c = b.getCentre();
        const float r = b.getWidth() * 0.5f;
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (b.translated (1.5f, 3.0f));
        // The scalloped top: eight flutes round a silver rim.
        juce::Path top;
        for (int i = 0; i <= 64; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / 64.0f;
            const float rr = r * (0.92f + 0.08f * std::cos (a * 8.0f));
            const juce::Point<float> p (c.x + rr * std::sin (a), c.y - rr * std::cos (a));
            if (i == 0)
                top.startNewSubPath (p);
            else
                top.lineTo (p);
        }
        top.closeSubPath();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffb8bcc2), c.x, b.getY(), juce::Colour (0xff4b4e53), c.x, b.getBottom(), false));
        g.fillPath (top);
        g.setColour (juce::Colour (0xff24262a));
        g.fillEllipse (juce::Rectangle<float> (r * 1.3f, r * 1.3f).withCentre (c));
        // The light in its centre, while the rotary does something.
        const bool on = binding.isValid() || alwaysLit;
        const auto dot = juce::Rectangle<float> (r * 0.62f, r * 0.62f).withCentre (c);
        if (on)
        {
            g.setColour (light.withAlpha (0.35f));
            g.fillEllipse (dot.expanded (r * 0.14f));
        }
        g.setColour (on ? light : juce::Colour (0xff3a3d42));
        g.fillEllipse (dot);
        if (on)
        {
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.fillEllipse (dot.reduced (r * 0.18f));
        }
        if (binding.isValid())
        {
            // Where it's set.
            const float a = juce::MathConstants<float>::pi * (-0.75f + 1.5f * juce::jlimit (0.0f, 1.0f, binding.get()));
            g.setColour (juce::Colours::white);
            g.drawLine (juce::Line<float> (c + juce::Point<float> (std::sin (a), -std::cos (a)) * (r * 0.72f), c + juce::Point<float> (std::sin (a), -std::cos (a)) * (r * 0.98f)), 2.0f);
        }
    }

    void Rotary::mouseDown (const juce::MouseEvent&)
    {
        dragStart = binding.isValid() ? binding.get() : 0.0f;
    }

    void Rotary::mouseDrag (const juce::MouseEvent& e)
    {
        if (binding.isValid() && binding.set)
            binding.set (juce::jlimit (0.0f, 1.0f, dragStart - (float) e.getDistanceFromDragStartY() / 200.0f));
    }

    void Rotary::mouseDoubleClick (const juce::MouseEvent&)
    {
        if (binding.reset)
            binding.reset();
    }

    void Rotary::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        if (binding.isValid() && binding.set)
            binding.set (juce::jlimit (0.0f, 1.0f, binding.get() + (w.deltaY > 0 ? 0.01f : -0.01f)));
    }

    //==========================================================================
    bool Desk::isFrozen (const Slot& s) const
    {
        for (const auto& f : frozen)
            if (f.kind == s.kind && f.index == s.index)
                return true;
        return false;
    }

    bool Desk::applyLatch (UiContext& ctx, const Slot& slot, Strip& st, bool mixKey)
    {
        switch (latch)
        {
            case Latch::Copy:
                clipboard = slot;
                clipboardMix = mixKey;
                latch = Latch::None;
                return true;
            case Latch::Paste:
            {
                auto* from = stripOf (ctx.engine, clipboard);
                if (from != nullptr && from != &st && from->getKind() == st.getKind() && clipboardMix == mixKey)
                    copyStripSections (ctx.engine, *from, st, mixKey ? kCopySends : (juce::uint32) (kCopyPreamp | kCopyGate | kCopyDynamics | kCopyEq));
                latch = Latch::None;
                return true;
            }
            case Latch::Reset:
            {
                auto& to = st.params;
                if (mixKey)
                {
                    if (st.isInput())
                        for (int b = 0; b < (int) ctx.engine.getBuses().size(); ++b)
                            st.sends[(size_t) b].levelDb = -144.0f;
                }
                else
                {
                    st.setModel (ModelBlock::Gate, to.gate().model.load());
                    st.setModel (ModelBlock::Eq, to.eq().model.load());
                    st.setModel (ModelBlock::Dynamics, to.dynamics().model.load());
                    to.gate().on = false;
                    to.eq().on = false;
                    to.dynamics().on = false;
                    to.filter().lowCut = false;
                    to.filter().highCut = false;
                    to.input.invert = false;
                }
                latch = Latch::None;
                return true;
            }
            case Latch::Safes:
                if (! mixKey)
                    return false;
                st.params.recallSafe = ! st.params.recallSafe.load();
                return true;
            case Latch::Freeze:
            {
                if (! mixKey)
                    return false;
                for (auto it = frozen.begin(); it != frozen.end(); ++it)
                    if (it->kind == slot.kind && it->index == slot.index)
                    {
                        frozen.erase (it);
                        return true;
                    }
                frozen.push_back (slot);
                return true;
            }
            case Latch::Assign:
            case Latch::Pre:
            case Latch::None:
                break;
        }
        return false;
    }

    ld::EqBlockParams* Desk::getGeq() const
    {
        if (geqStrip != nullptr)
            for (const auto& step : geqStrip->getChain())
                if (step.isBlock() && step.getBlockKind() == BlockKind::Eq)
                    return geqStrip->getBlock (step.uid).eq();
        return nullptr;
    }

    void Desk::pressGeq (Strip* mix, int numStrips)
    {
        if (geqStrip == nullptr)
        {
            geqStrip = mix != nullptr && mix->isOutput() ? mix : nullptr;
            geqPage = 0;
            if (getGeq() == nullptr)
                geqStrip = nullptr;
            return;
        }
        const auto* geq = getGeq();
        const int bands = geq != nullptr ? (int) models::model (models::BusEq, geq->model.load()).params.size() : 0;
        ++geqPage;
        if (geqPage * (numStrips - 1) >= bands)
            geqStrip = nullptr;
    }

    int Desk::getGeqBand (int position, int numStrips) const
    {
        const auto* geq = getGeq();
        if (geq == nullptr || position >= numStrips - 1)
            return -1;
        const int bands = (int) models::model (models::BusEq, geq->model.load()).params.size();
        const int first = juce::jmin (geqPage * (numStrips - 1), juce::jmax (0, bands - (numStrips - 1)));
        return first + position < bands ? first + position : -1;
    }

    //==========================================================================
    juce::String customRotaryName (CustomRotary f)
    {
        switch (f)
        {
            case CustomRotary::Trim:      return "Trim";
            case CustomRotary::Hpf:       return "HPF Freq";
            case CustomRotary::Lpf:       return "LPF Freq";
            case CustomRotary::GateThres: return "Gate Thres";
            case CustomRotary::GateDepth: return "Gate Depth";
            case CustomRotary::CompThres: return "Comp Thres";
            case CustomRotary::CompRatio: return "Comp Ratio";
            case CustomRotary::CompGain:  return "Comp Gain";
            case CustomRotary::Pan:       return "Pan";
            case CustomRotary::Width:     return "Stereo Width";
            case CustomRotary::Delay:     return "Delay";
            case CustomRotary::NumFunctions: break;
        }
        return {};
    }

    static KnobBinding bindCustomRotary (Strip& st, CustomRotary f)
    {
        auto& p = st.params;
        const bool input = st.isInput(), hasGate = st.getKind() == StripKind::Channel, hasComp = st.getKind() != StripKind::Aux;
        switch (f)
        {
            case CustomRotary::Trim:      return bindLinear ("Trim", p.input.trimDb, -24.0f, 24.0f, " dB", 1, 0.0f);
            case CustomRotary::Hpf:       return input ? bindFreq ("HPF", p.filter().lowCutFreq, 20.0f, 2000.0f, 20.0f) : KnobBinding {};
            case CustomRotary::Lpf:       return input ? bindFreq ("LPF", p.filter().highCutFreq, 500.0f, 20000.0f, 20000.0f) : KnobBinding {};
            case CustomRotary::GateThres: return hasGate ? bindDyn (p.gate(), "thr", "Gate") : KnobBinding {};
            case CustomRotary::GateDepth: return hasGate ? bindDyn (p.gate(), "range", "Depth") : KnobBinding {};
            case CustomRotary::CompThres: return hasComp ? bindDyn (p.dynamics(), "thr", "Comp") : KnobBinding {};
            case CustomRotary::CompRatio: return hasComp ? bindDyn (p.dynamics(), "ratio", "Ratio") : KnobBinding {};
            case CustomRotary::CompGain:  return hasComp ? bindDyn (p.dynamics(), "gain", "Gain") : KnobBinding {};
            case CustomRotary::Pan:       return bindPan ("Pan", p.pan);
            case CustomRotary::Width:     return p.stereo.load() ? bindLinear ("Width", p.width, 0.0f, 100.0f, " %", 0, 100.0f) : KnobBinding {};
            case CustomRotary::Delay:
            {
                auto* in = &p.input;
                auto delay = bindLinear ("Delay", in->delay, 0.0f, st.isInput() ? 341.0f : 682.0f, " ms", 2, 0.0f);
                delay.set = [in, inner = delay.set] (float v) { in->delayMode.store (2); inner (v); }; // the engine's milliseconds
                return delay;
            }
            case CustomRotary::NumFunctions: break;
        }
        return {};
    }

    //==========================================================================
    SurfaceSetup::SurfaceSetup (juce::PropertiesFile& file, const juce::String& model, int numBanks, int numLayers, int numStrips, Factory f)
        : settings (file), key ("ui.dliveSurfaceSetup." + model), banks (numBanks), layers (numLayers), strips (numStrips), factory (std::move (f))
    {
        custom = { CustomRotary::Trim, CustomRotary::Hpf, CustomRotary::GateThres, CustomRotary::CompThres };
        slots.resize ((size_t) (banks * layers * strips));
        for (int b = 0; b < banks; ++b)
            for (int l = 0; l < layers; ++l)
                for (int s = 0; s < strips; ++s)
                    slots[(size_t) cell (b, l, s)] = factory (b, l, s);
        load();
    }

    int SurfaceSetup::cell (int bank, int layer, int strip) const noexcept
    {
        return ((juce::jlimit (0, banks - 1, bank) * layers + juce::jlimit (0, layers - 1, layer)) * strips) + juce::jlimit (0, strips - 1, strip);
    }

    Slot SurfaceSetup::getSlot (int bank, int layer, int strip) const { return slots[(size_t) cell (bank, layer, strip)]; }

    Slot SurfaceSetup::getShownSlot (int bank, int layer, int strip, int numMains) const
    {
        const auto own = getSlot (bank, layer, strip);
        const auto last = getSlot (bank, layer, strips - 1);
        const int first = strips - numMains;
        if (numMains < 2 || first < 0 || strip < first || strip >= strips || last.kind != Slot::Main || last.index != 0)
            return own;
        if (strip != strips - 1)
        {
            const auto made = factory (bank, layer, strip);
            if (own.kind != made.kind || own.index != made.index)
                return own; // the user's own strip stays
        }
        return { Slot::Main, strip - first };
    }

    void SurfaceSetup::setSlot (int bank, int layer, int strip, const Slot& s)
    {
        slots[(size_t) cell (bank, layer, strip)] = s;
        changed();
    }

    void SurfaceSetup::setSoftKey (int i, const SoftKey& k)
    {
        softKeys[(size_t) juce::jlimit (0, kSoftKeys - 1, i)] = k;
        changed();
    }

    void SurfaceSetup::setCustom (int i, CustomRotary f)
    {
        custom[(size_t) juce::jlimit (0, kCustomRotaries - 1, i)] = f;
        changed();
    }

    void SurfaceSetup::reset()
    {
        for (int b = 0; b < banks; ++b)
            for (int l = 0; l < layers; ++l)
                for (int s = 0; s < strips; ++s)
                    slots[(size_t) cell (b, l, s)] = factory (b, l, s);
        softKeys = {};
        custom = { CustomRotary::Trim, CustomRotary::Hpf, CustomRotary::GateThres, CustomRotary::CompThres };
        changed();
    }

    namespace
    {
        Slot dlSlotOf (MixerEngine& e, const state::FaderRef& f)
        {
            if (f.dca)
                return { Slot::Dca, f.dcaIndex };
            if (! f.isStrip())
                return {};
            auto* strip = state::findStrip (e, f.strip);
            if (strip == nullptr)
                return {};
            for (int k = Slot::Channel; k <= Slot::Matrix; ++k)
                for (int i = 0; i < 200; ++i)
                    if (stripOf (e, { (Slot::Kind) k, i }) == strip)
                        return { (Slot::Kind) k, i };
            return {};
        }

        state::FaderRef dlFaderOf (MixerEngine& e, const Slot& s)
        {
            if (s.kind == Slot::Dca)
                return state::FaderRef::ofDca (s.index);
            if (auto* strip = stripOf (e, s))
                return state::FaderRef::of (state::StripRef::of (*strip));
            return state::FaderRef::blank();
        }
    } // namespace

    void SurfaceSetup::linkTo (MixerEngine& e, LayerSet& set, const juce::String& console)
    {
        linkedEngine = &e;
        linkedLayers = &set;
        linkedConsole = console;
        pullShowLayers (false); // the surface is still being built
    }

    bool SurfaceSetup::pullShowLayers (bool notify)
    {
        if (linkedLayers == nullptr)
            return false;
        bool any = false;
        pulling = true;
        for (int b = 0; b < banks; ++b)
            for (int l = 0; l < layers; ++l)
                if (const auto* u = layerlink::layerFor (linkedLayers->getUserLayers(), linkedConsole, bankKey (b, l)))
                {
                    std::vector<Slot> now ((size_t) strips);
                    for (int s = 0; s < strips; ++s)
                        now[(size_t) s] = getSlot (b, l, s);
                    if (layerlink::pullInto (*u, now, [this] (const state::FaderRef& f) { return dlSlotOf (*linkedEngine, f); }))
                    {
                        for (int s = 0; s < strips; ++s)
                            slots[(size_t) cell (b, l, s)] = now[(size_t) s];
                        any = true;
                    }
                }
        pulling = false;
        if (any && notify)
            changed();
        else if (any)
            save();
        return any;
    }

    void SurfaceSetup::pushShowLayers()
    {
        if (linkedLayers == nullptr || pulling)
            return;
        for (int b = 0; b < banks; ++b)
            for (int l = 0; l < layers; ++l)
                if (const auto* u = layerlink::layerFor (linkedLayers->getUserLayers(), linkedConsole, bankKey (b, l)))
                {
                    const auto copy = *u; // the setter refreshes the snapshot
                    std::vector<Slot> now ((size_t) strips);
                    for (int s = 0; s < strips; ++s)
                        now[(size_t) s] = getSlot (b, l, s);
                    layerlink::pushFrom (*linkedLayers, copy, now, [this] (const Slot& s) { return dlFaderOf (*linkedEngine, s); },
                                         [this] (const state::FaderRef& f) { return dlSlotOf (*linkedEngine, f); });
                }
    }

    void SurfaceSetup::changed()
    {
        save();
        pushShowLayers();
        if (onChanged)
            onChanged();
    }

    void SurfaceSetup::save() const
    {
        juce::XmlElement xml ("DLiveSurfaceSetup");
        for (int b = 0; b < banks; ++b)
            for (int l = 0; l < layers; ++l)
            {
                juce::StringArray cells;
                for (int s = 0; s < strips; ++s)
                {
                    const auto slot = getSlot (b, l, s);
                    cells.add (juce::String ((int) slot.kind) + ":" + juce::String (slot.index));
                }
                auto* e = xml.createNewChildElement ("Layer");
                e->setAttribute ("bank", b);
                e->setAttribute ("layer", l);
                e->setAttribute ("strips", cells.joinIntoString (" "));
            }
        for (int i = 0; i < kSoftKeys; ++i)
        {
            const auto& k = softKeys[(size_t) i];
            if (k.kind == SoftKey::Unused)
                continue;
            auto* e = xml.createNewChildElement ("SoftKey");
            e->setAttribute ("n", i);
            e->setAttribute ("kind", (int) k.kind);
            e->setAttribute ("index", k.index);
            e->setAttribute ("slotKind", (int) k.slot.kind);
            e->setAttribute ("slotIndex", k.slot.index);
        }
        for (int i = 0; i < kCustomRotaries; ++i)
            xml.setAttribute ("custom" + juce::String (i + 1), (int) custom[(size_t) i]);
        settings.setValue (key, &xml);
    }

    void SurfaceSetup::load()
    {
        const auto xml = settings.getXmlValue (key);
        if (xml == nullptr || ! xml->hasTagName ("DLiveSurfaceSetup"))
            return;
        for (auto* e : xml->getChildWithTagNameIterator ("Layer"))
        {
            const auto cells = juce::StringArray::fromTokens (e->getStringAttribute ("strips"), false);
            for (int s = 0; s < juce::jmin (cells.size(), strips); ++s)
            {
                Slot slot;
                slot.kind = (Slot::Kind) juce::jlimit (0, (int) Slot::Dca, cells[s].upToFirstOccurrenceOf (":", false, false).getIntValue());
                slot.index = cells[s].fromFirstOccurrenceOf (":", false, false).getIntValue();
                slots[(size_t) cell (e->getIntAttribute ("bank"), e->getIntAttribute ("layer"), s)] = slot;
            }
        }
        for (auto* e : xml->getChildWithTagNameIterator ("SoftKey"))
        {
            auto& k = softKeys[(size_t) juce::jlimit (0, kSoftKeys - 1, e->getIntAttribute ("n"))];
            k.kind = (SoftKey::Kind) juce::jlimit (0, (int) SoftKey::ClearPafl, e->getIntAttribute ("kind"));
            k.index = e->getIntAttribute ("index");
            k.slot.kind = (Slot::Kind) juce::jlimit (0, (int) Slot::Dca, e->getIntAttribute ("slotKind"));
            k.slot.index = e->getIntAttribute ("slotIndex");
        }
        for (int i = 0; i < kCustomRotaries; ++i)
            custom[(size_t) i] = (CustomRotary) juce::jlimit (0, (int) CustomRotary::NumFunctions - 1, xml->getIntAttribute ("custom" + juce::String (i + 1), (int) custom[(size_t) i]));
    }

    //==========================================================================
    juce::String slotName (UiContext& ctx, const Slot& s)
    {
        const juce::String number (s.index + 1);
        switch (s.kind)
        {
            case Slot::None:     return "-";
            case Slot::Channel:  return "Ip " + number;
            case Slot::FxReturn: return "FXRet " + number;
            case Slot::Bus:
            {
                auto* st = stripOf (ctx.engine, s);
                return st != nullptr && st->getName().isNotEmpty() ? st->getName() : "Mix " + number;
            }
            case Slot::Main:
            {
                // The Main mode names them: LR, M, C, LFE, Ls/Rs.
                auto* st = stripOf (ctx.engine, s);
                return "Main " + (st != nullptr && st->getName().isNotEmpty() ? st->getName() : number);
            }
            case Slot::Matrix:   return "Mtx " + number;
            case Slot::Dca:      return "DCA " + number;
        }
        return {};
    }

    juce::String softKeyName (UiContext& ctx, const SoftKey& k)
    {
        switch (k.kind)
        {
            case SoftKey::Unused:    return "Unused";
            case SoftKey::Mute:      return "Mute " + slotName (ctx, k.slot);
            case SoftKey::MuteGroup: return "Mute Group " + juce::String (k.index + 1);
            case SoftKey::DcaMute:   return "DCA " + juce::String (k.index + 1) + " Mute";
            case SoftKey::Mix:       return "Mix " + slotName (ctx, k.slot);
            case SoftKey::SceneGo:   return "Scene Go";
            case SoftKey::SceneNext: return "Scene Next";
            case SoftKey::ScenePrev: return "Scene Prev";
            case SoftKey::Talk:      return "Talk";
            case SoftKey::ClearPafl: return "Clear PAFL";
        }
        return {};
    }

    void Desk::clearPafl (UiContext& ctx)
    {
        for (const auto* list : { &ctx.engine.getChannels(), &ctx.engine.getAux(), &ctx.engine.getBuses(), &ctx.engine.getMains(), &ctx.engine.getMatrices() })
            for (const auto& s : *list)
                s->params.solo = false;
    }

    void Desk::pressSoftKey (UiContext& ctx, int index)
    {
        const auto& k = getSetup().getSoftKey (index);
        switch (k.kind)
        {
            case SoftKey::Unused:
                break;
            case SoftKey::Mute:
                if (k.slot.kind == Slot::Dca)
                {
                    if (juce::isPositiveAndBelow (k.slot.index, ctx.engine.getNumDcas()))
                        ctx.engine.getDca (k.slot.index).mute = ! ctx.engine.getDca (k.slot.index).mute.load();
                }
                else if (auto* st = stripOf (ctx.engine, k.slot))
                    ctx.engine.pressMute (*st);
                break;
            case SoftKey::MuteGroup:
                if (juce::isPositiveAndBelow (k.index, ctx.engine.getNumMuteGroups()))
                    ctx.engine.muteGroups.toggle (k.index);
                break;
            case SoftKey::DcaMute:
                if (juce::isPositiveAndBelow (k.index, ctx.engine.getNumDcas()))
                    ctx.engine.getDca (k.index).mute = ! ctx.engine.getDca (k.index).mute.load();
                break;
            case SoftKey::Mix:
                setMixOnFaders (getMixOnFaders() == k.slot.index ? -1 : k.slot.index);
                break;
            case SoftKey::SceneGo:   sceneGo(); break;
            case SoftKey::SceneNext: sceneStep (1); break;
            case SoftKey::ScenePrev: sceneStep (-1); break;
            case SoftKey::Talk:      ctx.engine.talk.paths[0].on = ! ctx.engine.talk.paths[0].on.load(); break;
            case SoftKey::ClearPafl: clearPafl (ctx); break;
        }
    }

    bool Desk::isSoftKeyOn (UiContext& ctx, int index)
    {
        const auto& k = getSetup().getSoftKey (index);
        switch (k.kind)
        {
            case SoftKey::Mute:
                if (k.slot.kind == Slot::Dca)
                    return juce::isPositiveAndBelow (k.slot.index, ctx.engine.getNumDcas()) && ctx.engine.getDca (k.slot.index).mute.load();
                if (auto* st = stripOf (ctx.engine, k.slot))
                    return st->getMuteState() != 0;
                return false;
            case SoftKey::MuteGroup: return ctx.engine.muteGroups.contains (k.index);
            case SoftKey::DcaMute:   return juce::isPositiveAndBelow (k.index, ctx.engine.getNumDcas()) && ctx.engine.getDca (k.index).mute.load();
            case SoftKey::Mix:       return getMixOnFaders() == k.slot.index;
            case SoftKey::Talk:      return ctx.engine.isTalkOn (0);
            case SoftKey::ClearPafl: return ctx.engine.anySoloActive.load();
            case SoftKey::Unused:
            case SoftKey::SceneGo:
            case SoftKey::SceneNext:
            case SoftKey::ScenePrev: break;
        }
        return false;
    }

    /** The strips a menu can offer, in submenus by kind; an item's id: `base` + kind * 1000 + index. */
    static void addSlotMenus (juce::PopupMenu& menu, UiContext& ctx, int base, const Slot& current, bool mixesOnly)
    {
        const auto group = [&] (const juce::String& title, Slot::Kind kind, int count)
        {
            if (count <= 0)
                return;
            juce::PopupMenu sub;
            const auto addRange = [&] (juce::PopupMenu& m, int first, int last)
            {
                for (int i = first; i < last; ++i)
                    m.addItem (base + (int) kind * 1000 + i, slotName (ctx, { kind, i }), true, current.kind == kind && current.index == i);
            };
            if (count > 40)
                for (int first = 0; first < count; first += 32)
                {
                    juce::PopupMenu block;
                    addRange (block, first, juce::jmin (first + 32, count));
                    sub.addSubMenu (juce::String (first + 1) + "-" + juce::String (juce::jmin (first + 32, count)), block);
                }
            else
                addRange (sub, 0, count);
            menu.addSubMenu (title, sub, true, nullptr, current.kind == kind);
        };
        if (! mixesOnly)
        {
            group ("Inputs", Slot::Channel, (int) ctx.engine.getChannels().size());
            group ("FX Returns", Slot::FxReturn, (int) ctx.engine.getAux().size());
        }
        group ("Mixes", Slot::Bus, (int) ctx.engine.getBuses().size());
        if (! mixesOnly)
        {
            group ("Mains", Slot::Main, (int) ctx.engine.getMains().size());
            group ("Matrices", Slot::Matrix, (int) ctx.engine.getMatrices().size());
            group ("DCAs", Slot::Dca, ctx.engine.getNumDcas());
        }
    }

    static Slot decodeSlot (int id, int base)
    {
        const int value = id - base;
        return { (Slot::Kind) (value / 1000), value % 1000 };
    }

    static juce::PopupMenu::Options menuOptions (juce::Component* target)
    {
        return target != nullptr ? juce::PopupMenu::Options().withTargetComponent (target) : juce::PopupMenu::Options();
    }

    void showSoftKeyMenu (UiContext& ctx, SurfaceSetup& setup, int index, juce::Component* target, std::function<void()> then)
    {
        enum
        {
            kUnused = 1,
            kGo,
            kNext,
            kPrev,
            kTalk,
            kClear,
            kGroupBase = 1000,
            kDcaBase = 2000,
            kMuteBase = 10000,
            kMixBase = 20000
        };
        const auto& k = setup.getSoftKey (index);
        juce::PopupMenu menu;
        menu.addSectionHeader ("SoftKey " + juce::String (index + 1));
        menu.addItem (kUnused, "Unused", true, k.kind == SoftKey::Unused);
        menu.addItem (kGo, "Scene Go", true, k.kind == SoftKey::SceneGo);
        menu.addItem (kNext, "Scene Next", true, k.kind == SoftKey::SceneNext);
        menu.addItem (kPrev, "Scene Prev", true, k.kind == SoftKey::ScenePrev);
        menu.addItem (kTalk, "Talk", true, k.kind == SoftKey::Talk);
        menu.addItem (kClear, "Clear PAFL", true, k.kind == SoftKey::ClearPafl);
        {
            juce::PopupMenu mute;
            addSlotMenus (mute, ctx, kMuteBase, k.kind == SoftKey::Mute ? k.slot : Slot {}, false);
            menu.addSubMenu ("Mute", mute, true, nullptr, k.kind == SoftKey::Mute);
            juce::PopupMenu groups, dcas, mixes;
            for (int i = 0; i < ctx.engine.getNumMuteGroups(); ++i)
                groups.addItem (kGroupBase + i, juce::String (i + 1), true, k.kind == SoftKey::MuteGroup && k.index == i);
            menu.addSubMenu ("Mute Group", groups, true, nullptr, k.kind == SoftKey::MuteGroup);
            for (int i = 0; i < ctx.engine.getNumDcas(); ++i)
                dcas.addItem (kDcaBase + i, juce::String (i + 1), true, k.kind == SoftKey::DcaMute && k.index == i);
            menu.addSubMenu ("DCA Mute", dcas, true, nullptr, k.kind == SoftKey::DcaMute);
            addSlotMenus (mixes, ctx, kMixBase, k.kind == SoftKey::Mix ? k.slot : Slot {}, true);
            menu.addSubMenu ("Mix on Faders", mixes, true, nullptr, k.kind == SoftKey::Mix);
        }
        menu.showMenuAsync (menuOptions (target), [safe = juce::Component::SafePointer<juce::Component> (target), &setup, index, then = std::move (then)] (int r)
        {
            if (safe == nullptr || r <= 0)
                return;
            SoftKey picked;
            if (r == kUnused) picked.kind = SoftKey::Unused;
            else if (r == kGo) picked.kind = SoftKey::SceneGo;
            else if (r == kNext) picked.kind = SoftKey::SceneNext;
            else if (r == kPrev) picked.kind = SoftKey::ScenePrev;
            else if (r == kTalk) picked.kind = SoftKey::Talk;
            else if (r == kClear) picked.kind = SoftKey::ClearPafl;
            else if (r >= kMixBase) { picked.kind = SoftKey::Mix; picked.slot = decodeSlot (r, kMixBase); }
            else if (r >= kMuteBase) { picked.kind = SoftKey::Mute; picked.slot = decodeSlot (r, kMuteBase); }
            else if (r >= kDcaBase) { picked.kind = SoftKey::DcaMute; picked.index = r - kDcaBase; }
            else { picked.kind = SoftKey::MuteGroup; picked.index = r - kGroupBase; }
            setup.setSoftKey (index, picked);
            if (then)
                then();
        });
    }

    void showCustomRotaryMenu (SurfaceSetup& setup, int index, juce::Component* target, std::function<void()> then)
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Custom " + juce::String (index + 1));
        for (int f = 0; f < (int) CustomRotary::NumFunctions; ++f)
            menu.addItem (f + 1, customRotaryName ((CustomRotary) f), true, (int) setup.getCustom (index) == f);
        menu.showMenuAsync (menuOptions (target), [safe = juce::Component::SafePointer<juce::Component> (target), &setup, index, then = std::move (then)] (int r)
        {
            if (safe == nullptr || r <= 0)
                return;
            setup.setCustom (index, (CustomRotary) (r - 1));
            if (then)
                then();
        });
    }

    void showStripMenu (UiContext& ctx, SurfaceSetup& setup, int bank, int layer, int strip, juce::Component* target, std::function<void()> then)
    {
        constexpr int kBase = 100, kNone = 1;
        const auto current = setup.getSlot (bank, layer, strip);
        juce::PopupMenu menu;
        menu.addSectionHeader ("Layer " + juce::String::charToString ((juce::juce_wchar) ('A' + layer)) + ", strip " + juce::String (strip + 1));
        menu.addItem (kNone, "Nothing", true, current.kind == Slot::None);
        addSlotMenus (menu, ctx, kBase, current, false);
        menu.showMenuAsync (menuOptions (target), [safe = juce::Component::SafePointer<juce::Component> (target), &setup, bank, layer, strip, kBase, kNone, then = std::move (then)] (int r)
        {
            if (safe == nullptr || r <= 0)
                return;
            setup.setSlot (bank, layer, strip, r == kNone ? Slot {} : decodeSlot (r, kBase));
            if (then)
                then();
        });
    }

    //==========================================================================
    StripLayout StripLayout::c1500 (int w, int h)
    {
        StripLayout l;
        const float cx = (float) w * 0.5f;
        const int mid = w / 2;
        l.level = { cx - 22.0f, 2.0f, 15.0f, 68.0f };
        l.compMeter = { cx + 2.0f, 16.0f, 13.0f, 38.0f };
        l.gateMeter = { cx + 2.0f, 60.0f, 13.0f, 10.0f };
        l.lcd = { 1.0f, 78.0f, (float) w - 2.0f, 62.0f };
        l.rotary = { mid - 24, 145, 48, 48 };
        l.mute = { mid - 34, 197, 68, 30 };
        l.sel = { mid - 34, 243, 68, 28 };
        l.mix = { mid - 30, 274, 60, 24 };
        l.pafl = { mid - 30, 301, 60, 26 };
        l.fader = { 0, 332, w, h - 332 };
        l.mutedY = 229.0f;
        l.kindDot = 1.0f;
        l.nameDot = 2.0f;
        l.valueDot = 1.0f;
        l.kindY = 3.0f;
        l.nameY = 12.0f;
        l.barY = 31.0f;
        l.valueY = 37.0f;
        l.assignY = 48.0f;
        return l;
    }

    //==========================================================================
    StripUnit::StripUnit (Desk& s, UiContext& c, const Theme& t, const StripLayout& l, int pos, int count)
        : surface (s), ctx (c), layout (l), position (pos), numStrips (count), fader (t), rotary (juce::Colour (0xffff3838)),
          mute ("Mute", kMuteIdle, kMuteLit, Key::Shape::Block), sel ("Sel", kSelIdle, kSelLit, Key::Shape::Oval), mix ("Mix", kMixIdle, kMixLit, Key::Shape::Block),
          pafl ("PAFL", kPaflIdle, kPaflLit, Key::Shape::Oval)
    {
        fader.look = &look();
        fader.grooveFromRight = l.faderInset;
        addAndMakeVisible (fader);
        addAndMakeVisible (rotary);
        for (auto* k : { &mute, &sel, &mix, &pafl })
            addAndMakeVisible (k);
        mute.isOn = [this]
        {
            if (auto* d = dca())
                return d->mute.load();
            auto* st = strip();
            return st != nullptr && st->getMuteState() != 0;
        };
        mute.onClick = [this]
        {
            if (auto* d = dca())
                d->mute = ! d->mute.load();
            else if (auto* st = strip())
                ctx.engine.pressMute (*st);
        };
        sel.isOn = [this]
        {
            auto* st = strip();
            return st != nullptr && ctx.getSelectedStrip && ctx.getSelectedStrip() == st;
        };
        sel.onClick = [this] { pressSel(); };
        mix.isOn = [this] { return slot.kind == Slot::Bus && surface.getMixOnFaders() == slot.index; };
        mix.onClick = [this] { pressMix(); };
        pafl.isOn = [this]
        {
            if (auto* d = dca())
                return d->solo.load();
            auto* st = strip();
            return st != nullptr && st->params.solo.load();
        };
        pafl.onClick = [this]
        {
            if (auto* d = dca())
                d->solo = ! d->solo.load();
            else if (auto* st = strip())
                st->params.solo = ! st->params.solo.load();
        };
    }

    Strip* StripUnit::strip() const { return stripOf (ctx.engine, slot); }

    ld::DcaParams* StripUnit::dca() const
    {
        return slot.kind == Slot::Dca && juce::isPositiveAndBelow (slot.index, ctx.engine.getNumDcas()) ? &ctx.engine.getDca (slot.index) : nullptr;
    }

    void StripUnit::pressSel()
    {
        auto* st = strip();
        if (st == nullptr)
            return;
        if (surface.applyLatch (ctx, slot, *st, false))
            return;
        const int bus = surface.getMixOnFaders();
        if (surface.isPreMode() && bus >= 0)
        {
            const auto flip = [bus] (Strip& s)
            {
                auto& send = s.sends[(size_t) bus];
                send.mode = send.mode.load() == (int) SendMode::Pre ? (int) SendMode::Post : (int) SendMode::Pre;
            };
            if (st->isInput())
                flip (*st);
            else if (slot.kind == Slot::Bus && slot.index == bus)
                for (const auto* list : { &ctx.engine.getChannels(), &ctx.engine.getAux() }) // the mix's own Sel: every input
                    for (const auto& s : *list)
                        flip (*s);
            return;
        }
        if (ctx.selectStrip)
            ctx.selectStrip (st);
    }

    void StripUnit::pressMix()
    {
        auto* st = strip();
        if (st == nullptr)
            return;
        if (surface.applyLatch (ctx, slot, *st, true))
            return;
        const int bus = surface.getMixOnFaders();
        if (surface.isAssignMode() && bus >= 0)
        {
            if (st->isInput())
                st->sends[(size_t) bus].enabled = ! st->sends[(size_t) bus].enabled.load();
            else if (slot.kind == Slot::Bus && slot.index == bus)
            {
                // The mix's own Mix key: every input on or off.
                bool any = false;
                for (const auto* list : { &ctx.engine.getChannels(), &ctx.engine.getAux() })
                    for (const auto& s : *list)
                        any = any || s->sends[(size_t) bus].enabled.load();
                for (const auto* list : { &ctx.engine.getChannels(), &ctx.engine.getAux() })
                    for (const auto& s : *list)
                        s->sends[(size_t) bus].enabled = ! any;
            }
            return;
        }
        if (slot.kind == Slot::Bus)
            surface.setMixOnFaders (bus == slot.index ? -1 : slot.index);
    }

    void StripUnit::setSlot (const Slot& s)
    {
        slot = s;
        rebind();
    }

    void StripUnit::rebind()
    {
        shownSignature.clear();
        repaint();
        // The GEQ on the faders: a band each, the mix's master on the last strip.
        if (auto* geq = surface.getGeq())
        {
            rotary.setBinding ({});
            const int band = surface.getGeqBand (position, numStrips);
            if (position == numStrips - 1)
                fader.bind (&surface.getGeqStrip()->params.faderDb);
            else if (band >= 0)
                fader.bindLinear (&geq->values[band], -12.0f, 12.0f);
            else
                fader.bind (nullptr);
            return;
        }
        if (auto* d = dca())
        {
            fader.bind (&d->faderDb);
            rotary.setBinding ({});
            return;
        }
        auto* st = strip();
        if (st == nullptr)
        {
            fader.bind (nullptr);
            rotary.setBinding ({});
            shownSignature.clear();
            repaint();
            return;
        }
        const int bus = surface.getMixOnFaders();
        // A mix on the faders: an input's fader is its send to it (the rotaries in Sends
        // mode take the sends instead, and the faders stay the channel's).
        using F = RotaryFunction;
        const bool sendOnFader = bus >= 0 && st->isInput() && surface.getRotaryFunction() != F::Sends;
        fader.bind (sendOnFader ? &st->sends[(size_t) bus].levelDb : &st->params.faderDb);

        KnobBinding b;
        juce::Colour light (0xffff3838);
        switch (surface.getRotaryFunction())
        {
            case F::Gain:
                if (st->isInput())
                {
                    const int in = st->params.inputLeft.load();
                    if (auto* src = in >= 0 && in < 128 ? ctx.engine.getSource (in) : nullptr)
                    {
                        b = bindLinear ("Gain", src->gainDb, 0.0f, 60.0f, " dB", 0, 0.0f);
                        b.steps = 61.0f;
                    }
                    else
                        b = bindLinear ("Trim", st->params.input.trimDb, -24.0f, 24.0f, " dB", 1, 0.0f);
                }
                break;
            case F::Pan:
                b = bindPan ("Pan", st->params.pan);
                light = juce::Colour (0xffffb020);
                break;
            case F::Sends:
                if (st->isInput() && bus >= 0)
                {
                    b = bindFader ("Send", st->sends[(size_t) bus].levelDb);
                    if (auto* mixStrip = stripOf (ctx.engine, { Slot::Bus, bus }))
                        light = mixStrip->getColour(); // the active Mix's
                }
                break;
            // The four are assignable (the Surface page, or Setup held and the key).
            case F::Custom1:
            case F::Custom2:
            case F::Custom3:
            case F::Custom4:
            {
                static const juce::Colour lights[] = { juce::Colour (0xfff2c10a), juce::Colour (0xff68db0e), juce::Colour (0xff09ece6), juce::Colour (0xffb58cff) };
                const int custom = (int) surface.getRotaryFunction() - (int) F::Custom1;
                b = bindCustomRotary (*st, surface.getSetup().getCustom (custom));
                light = lights[custom];
                break;
            }
        }
        rotary.setBinding (b);
        rotary.setLight (light);
        shownSignature.clear();
        repaint();
    }

    /** A mix's factory name, Aux1 or StGrp12: its kind and its number. */
    static bool splitMixName (const juce::String& name, juce::String& kind, juce::String& number)
    {
        const int digits = name.indexOfAnyOf ("0123456789");
        if (digits <= 0 || ! name.substring (digits).containsOnly ("0123456789"))
            return false;
        kind = name.substring (0, digits);
        number = name.substring (digits);
        return kind == "Aux" || kind == "StAux" || kind == "Grp" || kind == "StGrp" || kind == "FX" || kind == "StFX";
    }

    juce::Colour StripUnit::backlight() const
    {
        if (surface.getGeq() != nullptr)
            return juce::Colour (0xffd6dde4);
        if (auto* d = dca())
            return consoleColour (d->colour.load());
        auto* st = strip();
        return st != nullptr ? st->getColour() : juce::Colour (0xff121316);
    }

    juce::String StripUnit::getLcdKind() const
    {
        if (surface.getGeq() != nullptr)
            return position == numStrips - 1 ? juce::String ("Mix") : juce::String ("GEQ");
        if (dca() != nullptr)
            return "DCA";
        auto* st = strip();
        if (st == nullptr)
            return {};
        const auto stereo = st->params.stereo.load() ? juce::String ("St") : juce::String();
        switch (slot.kind)
        {
            case Slot::Channel:  return stereo + "Ip";
            case Slot::FxReturn: return "FXRet";
            case Slot::Bus:
            {
                juce::String kind, number;
                return splitMixName (st->getName(), kind, number) ? kind : stereo + "Mix";
            }
            case Slot::Main:     return "Main";
            case Slot::Matrix:   return "Mtx";
            case Slot::Dca:
            case Slot::None:     break;
        }
        return {};
    }

    juce::String StripUnit::getLcdName() const
    {
        using F = DisplayFunction;
        if (auto* geq = surface.getGeq())
        {
            if (position == numStrips - 1)
                return surface.getGeqStrip()->getName().substring (0, 8);
            const int band = surface.getGeqBand (position, numStrips);
            return band >= 0 ? models::model (models::BusEq, geq->model.load()).params[(size_t) band].label : juce::String();
        }
        if (auto* d = dca())
        {
            if (surface.getDisplayFunction() == F::Db)
                return d->faderDb.load() <= -89.5f ? juce::String ("-Inf") : juce::String (d->faderDb.load(), 1);
            const auto name = ctx.engine.getDcaName (slot.index);
            return (name.isNotEmpty() ? name : juce::String (slot.index + 1)).substring (0, 8);
        }
        auto* st = strip();
        if (st == nullptr)
            return {};
        switch (surface.getDisplayFunction())
        {
            case F::Name: break;
            case F::Db:
            {
                const int bus = surface.getMixOnFaders();
                const bool send = bus >= 0 && st->isInput() && surface.getRotaryFunction() != RotaryFunction::Sends;
                const float db = send ? st->sends[(size_t) bus].levelDb.load() : st->params.faderDb.load();
                return db <= -89.5f ? juce::String ("-Inf") : juce::String (db, 1);
            }
            case F::Io:
                return st->isInput() ? "S-" + juce::String (st->params.inputLeft.load() + 1) : juce::String();
            case F::Chan:
                return juce::String (slot.index + 1);
        }
        const auto name = st->getName();
        juce::String kind, number;
        if (slot.kind == Slot::Bus && splitMixName (name, kind, number))
            return number;
        return (name.isNotEmpty() ? name : st->getLabel()).substring (0, 9);
    }

    juce::String StripUnit::getLcdValue() const
    {
        if (surface.getGeq() != nullptr)
            return position == numStrips - 1 ? juce::String() : juce::String ("dB");
        const auto& b = rotary.getBinding();
        if (! b.isValid())
            return {};
        if (surface.getRotaryFunction() == RotaryFunction::Gain || surface.getRotaryFunction() == RotaryFunction::Sends
            || (surface.getRotaryFunction() >= RotaryFunction::Custom1 && surface.getRotaryFunction() <= RotaryFunction::Custom4
                && surface.getSetup().getCustom ((int) surface.getRotaryFunction() - (int) RotaryFunction::Custom1) == CustomRotary::Trim))
            return b.label + " dB";
        return b.label;
    }

    juce::String StripUnit::getLcdAssign() const
    {
        auto* st = strip();
        const int bus = surface.getMixOnFaders();
        if (st == nullptr || bus < 0 || ! st->isInput() || surface.getGeq() != nullptr || ! st->sends[(size_t) bus].enabled.load())
            return {};
        return juce::String ("Aux") + (st->sends[(size_t) bus].mode.load() == (int) SendMode::Pre ? " Pre" : "");
    }

    float StripUnit::meterDb() const
    {
        auto* st = strip();
        return st == nullptr ? -100.0f : juce::Decibels::gainToDecibels (ctx.meters.get (st, st->isInput() ? MeterPoint::Trim : MeterPoint::Output, 0), -100.0f);
    }

    void StripUnit::refresh()
    {
        fader.refresh();
        rotary.refresh();
        for (auto* k : { &mute, &sel, &mix, &pafl })
            k->refresh();
        auto* st = strip();
        const auto& b = rotary.getBinding();
        const juce::String signature = (st == nullptr && dca() == nullptr) ? juce::String()
                                                      : getLcdKind() + "|" + getLcdName() + "|" + getLcdValue() + "|" + getLcdAssign() + "|" + backlight().toString() + "|" + juce::String (juce::roundToInt (meterDb() / 3.0f))
                                                            + "|" + (b.isValid() ? juce::String (juce::roundToInt (b.get() * 40.0f)) : juce::String()) + (st != nullptr && st->params.recallSafe.load() ? "S" : "")
                                                            + (st != nullptr ? juce::String (juce::roundToInt (st->meters.dynReductionDb.load())) + juce::String (juce::roundToInt (st->meters.gateReductionDb.load())) : juce::String());
        if (signature != shownSignature)
        {
            shownSignature = signature;
            repaint (0, 0, getWidth(), juce::roundToInt (layout.lcd.getBottom()) + 2);
        }
    }

    void StripUnit::paint (juce::Graphics& g)
    {
        auto* st = strip();
        const bool live = st != nullptr || dca() != nullptr || surface.getGeq() != nullptr;

        // The meters, in silver-edged windows: the level, the compressor's reduction,
        // the gate's under it.
        const auto frame = [&g] (juce::Rectangle<float> r)
        {
            g.setColour (juce::Colour (0xffb9bdc3));
            g.fillRoundedRectangle (r.expanded (1.6f), 3.0f);
            g.setColour (juce::Colour (0xff111214));
            g.fillRoundedRectangle (r, 2.0f);
        };
        const auto level = layout.level;
        frame (level);
        static constexpr float marks[] = { 18.0f, 12.0f, 6.0f, 3.0f, 0.0f, -3.0f, -6.0f, -12.0f, -20.0f, -40.0f };
        static const char* const marksText[] = { "Pk", "12", "6", "3", "0", "3", "6", "12", "20", "Sig" };
        const float db = meterDb() + 18.0f; // 0 on the meter is -18 dBFS
        const float segment = level.getHeight() / (float) std::size (marks);
        for (int i = 0; i < (int) std::size (marks); ++i)
        {
            const auto seg = juce::Rectangle<float> (level.getX() + 2.0f, level.getY() + (float) i * segment + 0.8f, level.getWidth() - 4.0f, segment - 1.6f);
            const auto colour = i == 0 ? juce::Colour (0xffff3030) : i < 4 ? juce::Colour (0xfff0c030) : juce::Colour (0xff40d070);
            g.setColour (st != nullptr && db >= marks[i] ? colour : colour.withAlpha (0.14f));
            g.fillRect (seg);
            if (layout.scale && position % 4 == 0)
                hw::silk (g, { 0.0f, seg.getY() - 2.0f, level.getX() - 2.0f, 8.0f }, marksText[i], look(), 6.5f, juce::Justification::centredRight, true);
        }
        const auto reduction = [&] (juce::Rectangle<float> r, float gr)
        {
            frame (r);
            const float seg2 = r.getHeight() / 4.0f;
            for (int i = 0; i < 4; ++i)
            {
                g.setColour (st != nullptr && gr <= -(float) (3 * (i + 1)) + 2.0f ? juce::Colour (0xffffb020) : juce::Colour (0x24ffb020));
                g.fillRect (r.getX() + 2.0f, r.getY() + (float) i * seg2 + 0.8f, r.getWidth() - 4.0f, seg2 - 1.6f);
            }
        };
        reduction (layout.compMeter, st != nullptr ? st->meters.dynReductionDb.load() : 0.0f);
        reduction (layout.gateMeter, st != nullptr ? st->meters.gateReductionDb.load() : 0.0f);

        // The strip's part of the colour LCD.
        const auto lcd = layout.lcd;
        const auto colour = backlight();
        g.setColour (colour);
        g.fillRect (lcd);
        if (live)
        {
            const auto ink = colour.getPerceivedBrightness() < 0.2f ? juce::Colour (0xffc8ccd2) : colour.interpolatedWith (juce::Colour (0xff06080c), 0.82f);
            g.setColour (ink);
            const float left = lcd.getX() + 4.0f;
            dotPrint (g, left, lcd.getY() + layout.kindY, getLcdKind(), layout.kindDot);
            if (st != nullptr && st->params.recallSafe.load())
                dotPrint (g, lcd.getRight() - 6.0f * layout.kindDot - 3.0f, lcd.getY() + layout.kindY, "S", layout.kindDot);
            const int room = juce::jmax (1, (int) ((lcd.getWidth() - 6.0f) / (6.0f * layout.nameDot)));
            dotPrint (g, left, lcd.getY() + layout.nameY, getLcdName().substring (0, room), layout.nameDot);
            const auto& b = rotary.getBinding();
            if (b.isValid())
            {
                // Where the rotary stands: a thick bar on a thin line.
                const float x = left, y = lcd.getY() + layout.barY, w = lcd.getWidth() - 10.0f;
                g.fillRect (x, y + 1.0f, w, 1.0f);
                g.fillRect (x, y, w * juce::jlimit (0.0f, 1.0f, b.get()), 3.0f);
            }
            dotPrint (g, left, lcd.getY() + layout.valueY, getLcdValue(), layout.valueDot);
            if (layout.assignY >= 0.0f)
                dotPrint (g, left, lcd.getY() + layout.assignY, getLcdAssign(), layout.valueDot);
        }
        g.setColour (juce::Colours::black);
        g.fillRect (lcd.getRight() - 1.0f, lcd.getY(), 2.0f, lcd.getHeight());

        const float w = (float) getWidth();
        hw::silk (g, { 0.0f, layout.mutedY, w * 0.5f + 8.0f, 12.0f }, "Muted", look(), 8.5f, juce::Justification::centredRight, true);
        const bool muted = st != nullptr && st->getMuteState() != 0;
        g.setColour (muted ? juce::Colour (0xffff3030) : juce::Colour (0xff3a2020));
        g.fillEllipse (w * 0.5f + 11.0f, layout.mutedY + 2.0f, 7.0f, 7.0f);

        if (layout.screws)
        {
            const auto screw = [&g] (float x, float y)
            {
                g.setColour (juce::Colour (0xffa9adb4));
                g.fillEllipse (x - 6.0f, y - 6.0f, 12.0f, 12.0f);
                g.setColour (juce::Colour (0xff2a2c31));
                g.fillEllipse (x - 3.0f, y - 3.0f, 6.0f, 6.0f);
            };
            const float cx = w - layout.faderInset;
            screw (cx, (float) layout.fader.getY() - 5.0f);
            screw (cx, (float) layout.fader.getBottom() + 1.0f);
        }
    }

    void StripUnit::resized()
    {
        rotary.setBounds (layout.rotary);
        mute.setBounds (layout.mute);
        sel.setBounds (layout.sel);
        mix.setBounds (layout.mix);
        pafl.setBounds (layout.pafl);
        fader.setBounds (layout.fader);
    }
} // namespace dl

//==============================================================================
std::vector<dl::Slot> DLiveSurface::defaultBank (int b)
{
    using S = dl::Slot;
    std::vector<S> slots ((size_t) kStrips);
    for (int i = 0; i < kStrips; ++i)
    {
        switch (b)
        {
            case 0:
            case 1:
            case 2:
            case 3:  slots[(size_t) i] = { S::Channel, b * kStrips + i }; break;
            case 4:  slots[(size_t) i] = { S::FxReturn, i }; break;
            default: slots[(size_t) i] = i == kStrips - 1 ? S { S::Main, 0 } : S { S::Bus, i }; break;
        }
    }
    return slots;
}

dl::Key& DLiveSurface::key (const juce::String& name, juce::Colour idle, juce::Colour lit, juce::Rectangle<int> bounds, dl::Key::Shape shape, bool led)
{
    auto k = std::make_unique<dl::Key> (name, idle, lit, shape, led);
    k->setBounds (bounds);
    addAndMakeVisible (*k);
    keys.emplace_back (name, std::move (k));
    return *keys.back().second;
}

dl::Key* DLiveSurface::findKey (const juce::String& name) const
{
    for (auto& [n, k] : keys)
        if (n == name)
            return k.get();
    return nullptr;
}

DLiveSurface::DLiveSurface (UiContext& c)
    : ctx (c), theme (Theme::dark()), universalKey ("UNIVERSAL MIXER", theme.accent, theme), c3500Key ("C3500 SURFACE", theme.accent, theme),
      assignments (c.settings, "c1500", 1, kBanks, kStrips, [] (int, int layer, int strip) { return defaultBank (layer)[(size_t) strip]; }),
      routingView (c, theme), fxRack (c, theme), monitoring (c, theme), library (c, theme), setupView (c, theme)
{
    using Shape = dl::Key::Shape;
    assignments.onChanged = [this] { bindStrips(); };
    assignments.linkTo (ctx.engine, ctx.show.layers, "c1500");
    assignSoftKey = [this] (int i, juce::Component* t) { dl::showSoftKeyMenu (ctx, assignments, i, t); };
    assignRotary = [this] (int i, juce::Component* t) { dl::showCustomRotaryMenu (assignments, i, t); };

    universalKey.onClick = [&context = ctx]
    {
        juce::MessageManager::callAsync ([&context]
        {
            if (context.setInterface)
                context.setInterface (InterfaceKind::Universal);
        });
    };
    addAndMakeVisible (universalKey);
    c3500Key.onClick = [&context = ctx] { dl::switchSurface (context, "c3500"); };
    addAndMakeVisible (c3500Key);

    // The touchscreen: the processing page hosts the other pages in its main area.
    processing = std::make_unique<dl::ProcessingScreen> (c, *this);
    scenes = std::make_unique<dl::SceneScreen> (c, *this);
    mixRack = std::make_unique<dl::MixRackScreen> (c, *this);
    metersScreen = std::make_unique<dl::MetersScreen> (c, *this);
    ioScreen = std::make_unique<dl::IoScreen> (c, *this);
    surfaceScreen = std::make_unique<dl::SurfaceScreen> (c, *this);
    status = std::make_unique<dl::StatusScreen> (c);
    help = std::make_unique<dl::HelpPanel>();
    addAndMakeVisible (screenHost);
    screenHost.addAndMakeVisible (*processing);
    processing->setBounds (0, 0, dl::kTouchW, dl::kTouchH);
    processing->setOneScreen (true);
    processing->setView (1); // the four bands, as the C1500 shots have them
    routingView.setDragPans (true); // drag the page to reach the whole matrix
    for (auto* view : std::initializer_list<juce::Component*> { &routingView, &fxRack, &monitoring, &library, &setupView })
    {
        screenHost.addChildComponent (view);
        view->setBounds (0, 0, dl::kTouchW, dl::kTouchH);
    }
    screenHost.addChildComponent (*help);
    help->setBounds (140, 70, 720, 400);
    const auto touchTurn = [this] (const KnobBinding& b) { dataRotary->setBinding (b); };
    processing->onTouchTurn = touchTurn;
    scenes->onTouchTurn = touchTurn;
    mixRack->onTouchTurn = touchTurn;
    metersScreen->onTouchTurn = touchTurn;
    ioScreen->onTouchTurn = touchTurn;
    surfaceScreen->onTouchTurn = touchTurn;
    scenes->onSubChanged = [this]
    {
        if (page == Page::Scenes)
            processing->hostPage (scenes.get(), scenes->hostSource());
    };
    processing->onLibrary = [this] { selectPage ("Lib", Page::Library); };
    processing->onFxUnit = [this] { selectPage ("Page FX", Page::Fx); };
    processing->onWantsProcessing = [this] { selectPage ("Page Processing", Page::Processing); };
    mixRack->onOpenSetup = [this] { selectPage ("Setup", Page::Setup); };

    // Left of the screen.
    const auto pageKey = [this] (const juce::String& name, Page p, juce::Rectangle<int> r, juce::Colour idle, juce::Colour lit, Shape shape = Shape::Block) -> dl::Key&
    {
        auto& k = key (name, idle, lit, r, shape);
        k.isOn = [this, name] { return litPageKey == name; };
        k.onClick = [this, p, name]
        {
            if (litPageKey == name && p != Page::Status)
                selectPage ({}, Page::Status); // a lit key off: the status page
            else
                selectPage (name, p);
        };
        return k;
    };
    key ("Home", kPaflIdle, kPaleLit, { 28, 74, 66, 32 }, Shape::Oval, false).onClick = [this] { pressHome(); };
    silks.push_back ({ { 18.0f, 124.0f, 86.0f, 16.0f }, "Insert", 13.0f, juce::Justification::centred, false });
    silks.push_back ({ { 18.0f, 154.0f, 86.0f, 11.0f }, "Assigned", 9.0f, juce::Justification::centred, true });
    {
        auto& in = key ("In", kBlueIdle, kBlueLit, { 32, 168, 58, 28 }, Shape::Oval);
        in.isOn = [this]
        {
            auto* s = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
            return s != nullptr && s->params.preInsert().on.load();
        };
        in.onClick = [this]
        {
            if (auto* s = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr)
                s->params.preInsert().on = ! s->params.preInsert().on.load();
        };
    }
    pageKey ("Lib", Page::Library, { 28, 214, 66, 32 }, kPale, kPaleLit);
    pageKey ("Setup", Page::Setup, { 28, 260, 66, 32 }, kPale, kPaleLit);
    pageKey ("Listen", Page::Listen, { 28, 306, 66, 32 }, kPale, kPaleLit);

    // Right of the screen: what it shows.
    const std::pair<const char*, Page> pages[] = { { "Meters", Page::Meters },   { "FX", Page::Fx },        { "I/O", Page::Io },           { "Scenes", Page::Scenes },   { "Ganging", Page::Ganging },
                                                   { "MixRack", Page::MixRack }, { "Surface", Page::Surface }, { "Util/Shows", Page::Util }, { "Routing", Page::Routing }, { "Processing", Page::Processing } };
    int row = 0;
    for (auto [name, p] : pages)
    {
        const int y = 56 + row * 38;
        silks.push_back ({ { 750.0f, (float) y - 2.0f, 80.0f, 13.0f }, name, 10.0f, juce::Justification::centred, false });
        auto& k = pageKey (juce::String ("Page ") + name, p, { 758, y + 11, 64, 25 }, row == 8 ? juce::Colour (0xff1ea898) : row == 9 ? juce::Colour (0xff17b06a) : kPale, kPaleLit);
        k.setButtonText ({});
        ++row;
    }

    // PAFL's meters, Clear; Assign and Talk.
    silks.push_back ({ { 840.0f, 60.0f, 76.0f, 12.0f }, "L    R    M", 10.5f, juce::Justification::centred, false });
    silks.push_back ({ { 840.0f, 300.0f, 76.0f, 12.0f }, "PAFL", 10.5f, juce::Justification::centred, false });
    {
        auto& clear = key ("Clear", kPaflIdle, kPaflLit, { 850, 314, 56, 28 }, Shape::Oval);
        clear.onClick = [this] { clearPafl (ctx); };
        clear.isOn = [this]
        {
            for (const auto* list : { &ctx.engine.getChannels(), &ctx.engine.getAux(), &ctx.engine.getBuses(), &ctx.engine.getMains(), &ctx.engine.getMatrices() })
                for (const auto& s : *list)
                    if (s->params.solo.load())
                        return true;
            return false;
        };
    }
    silks.push_back ({ { 928.0f, 150.0f, 76.0f, 12.0f }, "+Mix", 10.0f, juce::Justification::centred, true });
    silks.push_back ({ { 922.0f, 188.0f, 88.0f, 12.0f }, "Talk Active", 10.0f, juce::Justification::centred, false });
    {
        auto& assign = key ("Assign Talk", kBlueIdle, kBlueLit, { 938, 116, 56, 30 }, Shape::Oval);
        assign.setButtonText ("Assign");
        auto& talk = key ("Talk", kPale, kPaleLit, { 934, 208, 64, 36 });
        talk.isOn = [this] { return ctx.engine.isTalkOn (0); };
        talk.onClick = [this] { ctx.engine.talk.paths[0].on = ! ctx.engine.talk.paths[0].on.load(); };
    }

    // Under the screen on the real thing, beside it here: the PEQ.
    silks.push_back ({ { 1062.0f, 60.0f, 60.0f, 18.0f }, "PEQ", 16.0f, juce::Justification::centred, false });
    silks.push_back ({ { 1062.0f, 80.0f, 60.0f, 12.0f }, "Pk!", 9.0f, juce::Justification::centred, true });
    {
        auto& in = key ("PEQ In", kSelIdle, kPaleLit, { 1068, 118, 48, 30 }, Shape::Oval);
        in.setButtonText ("In");
        in.isOn = [this]
        {
            auto* s = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
            return s != nullptr && s->params.eq().on.load();
        };
        in.onClick = [this]
        {
            if (auto* s = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr)
                s->params.eq().on = ! s->params.eq().on.load();
        };
        auto& width = key ("Width", kAmberKey, kAmberLit, { 1122, 112, 44, 44 }, Shape::Oval, false);
        width.isOn = [this] { return widthMode; };
        width.onClick = [this]
        {
            widthMode = ! widthMode;
            bindPeq();
        };
    }
    for (int band = 0; band < 4; ++band)
    {
        const int x = 1184 + band * 100;
        silks.push_back ({ { (float) x + 4.0f, 62.0f, 34.0f, 12.0f }, "Gain", 9.5f, juce::Justification::centred, false });
        silks.push_back ({ { (float) x + 46.0f, 62.0f, 36.0f, 12.0f }, "Freq", 9.5f, juce::Justification::centred, false });
        auto gain = std::make_unique<dl::Rotary> (juce::Colour (0xffff3030));
        auto freq = std::make_unique<dl::Rotary> (juce::Colour (0xff30e060));
        gain->setBounds (x + 2, 130, 44, 44);
        freq->setBounds (x + 44, 78, 44, 44);
        addAndMakeVisible (*gain);
        addAndMakeVisible (*freq);
        peq[(size_t) band * 2] = std::move (gain);
        peq[(size_t) band * 2 + 1] = std::move (freq);
    }

    // View, Help and the data rotary; Phones.
    {
        auto& view = key ("View", kPale, kPaleLit, { 1250, 212, 66, 32 });
        view.isOn = [this] { return processing->getView() != 1; };
        view.onClick = [this] { processing->toggleView(); };
        auto& h = key ("Help", kPale, kPaleLit, { 1350, 212, 66, 32 });
        h.isOn = [this] { return help->isVisible(); };
        h.onClick = [this]
        {
            if (help->isVisible())
                help->setVisible (false);
            else
                help->show (helpKey());
        };
    }
    dataRotary = std::make_unique<dl::Rotary> (juce::Colour (0xfff5b83a));
    dataRotary->alwaysLit = true;
    dataRotary->setBounds (1292, 260, 84, 84);
    addAndMakeVisible (*dataRotary);
    phones = std::make_unique<dl::Rotary> (juce::Colour (0xffe0e0e0));
    phones->setBinding (bindFader ("Phones", ctx.engine.phones.levelDb));
    phones->setBounds (1666, 262, 56, 56);
    addAndMakeVisible (*phones);
    silks.push_back ({ { 1650.0f, 326.0f, 88.0f, 14.0f }, "Phones", 11.0f, juce::Justification::centred, false });

    // SoftKeys 1-10, 11-16 and 17-19 (Setup held and a key assigns it; or the Surface page).
    const auto softKey = [this] (int n, juce::Rectangle<int> r)
    {
        auto& k = key ("Soft " + juce::String (n), kSoft, kSoftLit, r);
        k.setButtonText (juce::String (n));
        k.isOn = [this, n] { return isSoftKeyOn (ctx, n - 1); };
        k.onClick = [this, n] { softKeyPressed (n - 1); };
    };
    for (int i = 0; i < 10; ++i)
        softKey (i + 1, { 1812, 56 + i * 38, 76, 32 });
    for (int i = 0; i < 6; ++i)
        softKey (11 + i, { 1812, 516 + i * 44, 76, 36 }); // below the seam of the upper panel, as the photographs have 11
    for (int i = 0; i < 3; ++i)
        softKey (17 + i, { 1812, 900 + i * 50, 76, 40 });

    // The strips.
    for (int i = 0; i < kStrips; ++i)
    {
        strips[(size_t) i] = std::make_unique<dl::StripUnit> (*this, ctx, theme, dl::StripLayout::c1500 (kStripW, kStripH), i, kStrips);
        strips[(size_t) i]->setBounds (stripX (i), kStripsY, kStripW, kStripH);
        addAndMakeVisible (*strips[(size_t) i]);
    }

    // Left of the strips: the LCDs' and the rotaries' functions; Copy to GEQ.
    silks.push_back ({ { 10.0f, 666.0f, 84.0f, 26.0f }, "Display\nFunction", 10.0f, juce::Justification::centred, false });
    {
        const std::pair<const char*, DisplayFunction> displayKeys[] = { { "Name", DisplayFunction::Name }, { "dB", DisplayFunction::Db }, { "I/O", DisplayFunction::Io }, { "Chan", DisplayFunction::Chan } };
        int r = 0;
        for (auto [name, f] : displayKeys)
        {
            auto& k = key (name, kPale, kPaleLit, { 20, 506 + r * 39, 66, 34 });
            k.isOn = [this, target = f] { return displayFunction == target; };
            k.onClick = [this, target = f] { setDisplayFunction (target); };
            ++r;
        }
        const std::pair<const char*, RotaryFunction> rotaryKeys[] = { { "Gain", RotaryFunction::Gain },       { "Pan", RotaryFunction::Pan },         { "1", RotaryFunction::Custom1 },
                                                                      { "2", RotaryFunction::Custom2 }, { "3", RotaryFunction::Custom3 }, { "4", RotaryFunction::Custom4 },
                                                                      { "Sends", RotaryFunction::Sends } };
        r = 0;
        for (auto [name, f] : rotaryKeys)
        {
            const juce::Colour lit = f == RotaryFunction::Sends ? kBlueLit : kMuteLit;
            auto& k = key (juce::String ("Rotary ") + name, f == RotaryFunction::Sends ? kSoft : kPale, lit, { 20, 700 + r * 42, 66, 36 });
            k.setButtonText (name);
            k.isOn = [this, target = f] { return rotaryFunction == target; };
            k.onClick = [this, target = f, custom = (r >= 2 && r <= 5) ? r - 2 : -1, keyPtr = &k]
            {
                if (custom >= 0 && dl::wantsAssign (litPageKey == "Setup"))
                {
                    if (assignRotary)
                        assignRotary (custom, keyPtr);
                }
                else
                    setRotaryFunction (target);
            };
            ++r;
        }
    }
    const auto latchKey = [this] (const juce::String& name, Latch l, juce::Colour idle, juce::Colour lit, juce::Rectangle<int> r)
    {
        auto& k = key (name, idle, lit, r, Shape::Oval);
        k.isOn = [this, l] { return getLatch() == l; };
        k.onClick = [this, l] { toggleLatch (l); };
    };
    latchKey ("Copy", Latch::Copy, kTeal, kPaleLit, { 100, 862, 62, 30 });
    latchKey ("Paste", Latch::Paste, kPink, kMuteLit, { 100, 898, 62, 30 });
    latchKey ("Reset", Latch::Reset, kRed, kMuteLit, { 100, 934, 62, 30 });
    latchKey ("Safes", Latch::Safes, kBlueIdle, kBlueLit, { 100, 970, 62, 30 });
    latchKey ("Freeze", Latch::Freeze, kBlueIdle, kBlueLit, { 100, 1006, 62, 30 });
    {
        auto& geq = key ("GEQ", kTeal, kPaleLit, { 100, 1042, 62, 30 }, Shape::Oval);
        geq.isOn = [this] { return getGeq() != nullptr; };
        geq.onClick = [this]
        {
            pressGeq (ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr, kStrips);
            bindStrips();
        };
    }

    // Right of the strips: Pre and Assign, the banks.
    {
        auto& pre = key ("Pre", kSelIdle, kSelLit, { 1648, 510, 60, 32 }, Shape::Oval);
        pre.isOn = [this] { return isPreMode(); };
        pre.onClick = [this] { toggleLatch (Latch::Pre); };
        silks.push_back ({ { 1642.0f, 543.0f, 72.0f, 11.0f }, "+Sel", 9.5f, juce::Justification::centred, true });
        auto& assign = key ("Assign", kBlueIdle, kBlueLit, { 1648, 560, 60, 32 }, Shape::Oval);
        assign.isOn = [this] { return isAssignMode(); };
        assign.onClick = [this] { toggleLatch (Latch::Assign); };
        silks.push_back ({ { 1642.0f, 593.0f, 72.0f, 11.0f }, "+Mix", 9.5f, juce::Justification::centred, true });
    }
    silks.push_back ({ { 1648.0f, 622.0f, 110.0f, 16.0f }, "Bank 1", 12.0f, juce::Justification::centredLeft, false });
    for (int b = 0; b < kBanks; ++b)
    {
        auto& k = key (juce::String::charToString ((juce::juce_wchar) ('A' + b)), kPale, kPaleLit, { 1648, 642 + b * 52, 66, 40 });
        k.isOn = [this, b] { return bank == b; };
        k.onClick = [this, b] { setBank (b); };
        silks.push_back ({ { 1718.0f, 656.0f + (float) b * 52.0f, 32.0f, 12.0f }, "Pk!", 9.0f, juce::Justification::centredLeft, true });
    }

    bindStrips();
    bindPeq();
    startTimerHz (30);
}

DLiveSurface::~DLiveSurface() = default;

void DLiveSurface::selectPage (const juce::String& keyName, Page p)
{
    litPageKey = keyName;
    showPage (p);
}

void DLiveSurface::softKeyPressed (int index)
{
    if (dl::wantsAssign (litPageKey == "Setup"))
    {
        if (assignSoftKey)
            assignSoftKey (index, findKey ("Soft " + juce::String (index + 1)));
    }
    else
        pressSoftKey (ctx, index);
}

void DLiveSurface::sceneGo() { scenes->goNext(); }
void DLiveSurface::sceneStep (int delta) { scenes->stepNext (delta); }

void DLiveSurface::pressHome()
{
    bank = 0;
    mixBus = -1;
    rotaryFunction = RotaryFunction::Gain;
    displayFunction = DisplayFunction::Name;
    setLatch (Latch::None);
    endGeq();
    bindStrips();
    processing->setView (1);
    selectPage ("Page Processing", Page::Processing);
}

void DLiveSurface::bindStrips()
{
    for (int i = 0; i < kStrips; ++i)
    {
        // A strip frozen in layers stays where it is.
        auto& unit = *strips[(size_t) i];
        if (unit.getSlot().kind == dl::Slot::None || ! isFrozen (unit.getSlot()))
            unit.setSlot (assignments.getShownSlot (0, bank, i, (int) ctx.engine.getMains().size()));
        else
            unit.rebind();
    }
    processing->mixChanged();
}

void DLiveSurface::bindPeq()
{
    auto* s = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
    boundStrip = s;
    const char* bands[] = { "l", "1", "2", "h" };
    for (int b = 0; b < 4; ++b)
    {
        const juce::String pre (bands[b]);
        peq[(size_t) b * 2]->setBinding (s != nullptr ? dl::bindEq (*s, pre + "g", "Gain") : KnobBinding {});
        // WIDTH lit: the Freq rotaries set the bands' widths.
        peq[(size_t) b * 2 + 1]->setBinding (s != nullptr ? dl::bindEq (*s, pre + (widthMode ? "q" : "f"), widthMode ? "Width" : "Freq") : KnobBinding {});
    }
}

void DLiveSurface::setBank (int b)
{
    bank = juce::jlimit (0, kBanks - 1, b);
    bindStrips();
}

void DLiveSurface::setRotaryFunction (RotaryFunction f)
{
    rotaryFunction = f;
    bindStrips();
}

void DLiveSurface::setMixOnFaders (int bus)
{
    mixBus = bus;
    bindStrips();
}

void DLiveSurface::showPage (Page p)
{
    page = p;
    juce::Component* hosted = nullptr;
    auto source = dl::wideSource();
    switch (p)
    {
        case Page::Status:   hosted = status.get(); break;
        case Page::Meters:   hosted = metersScreen.get(); metersScreen->refresh(); break;
        case Page::Io:       hosted = ioScreen.get(); ioScreen->refresh(); break;
        case Page::Scenes:   hosted = scenes.get(); source = scenes->hostSource(); scenes->refresh(); break;
        case Page::MixRack:  hosted = mixRack.get(); mixRack->refresh(); break;
        case Page::Surface:  hosted = surfaceScreen.get(); surfaceScreen->refresh(); break;
        case Page::Processing:
        case Page::Fx:
        case Page::Ganging:
        case Page::Util:
        case Page::Routing:
        case Page::Library:
        case Page::Setup:
        case Page::Listen:   break;
    }
    processing->hostPage (hosted, source);
    help->setVisible (false);

    // What has no screen of its own yet fills the screen.
    const bool universal = p == Page::Fx || p == Page::Routing || p == Page::Util || p == Page::Library || p == Page::Ganging || p == Page::Setup || p == Page::Listen;
    processing->setVisible (! universal);
    fxRack.setVisible (p == Page::Fx);
    routingView.setVisible (p == Page::Routing);
    library.setVisible (p == Page::Util || p == Page::Library);
    setupView.setVisible (p == Page::Ganging || p == Page::Setup);
    monitoring.setVisible (p == Page::Listen);
    switch (p)
    {
        case Page::Fx:      fxRack.refresh(); break;
        case Page::Routing: routingView.refresh(); break;
        case Page::Util:
        case Page::Library: library.refresh(); break;
        case Page::Ganging:
        case Page::Setup:   setupView.refresh(); break;
        case Page::Listen:  monitoring.refresh(); break;
        default: break;
    }
}

juce::String DLiveSurface::helpKey() const
{
    switch (page)
    {
        case Page::Status:     return "Status";
        case Page::Processing: return processing->helpKey();
        case Page::Meters:     return "Meters";
        case Page::Fx:         return "Fx";
        case Page::Io:         return "Io";
        case Page::Scenes:
        {
            static const char* const subs[] = { "Scenes", "Scenes/Update", "Scenes/Cue", "Scenes/Import", "Scenes/Safes" };
            return subs[juce::jlimit (0, 4, scenes->getSub())];
        }
        case Page::Ganging:    return "Ganging";
        case Page::MixRack:    return "MixRack";
        case Page::Surface:    return "Surface";
        case Page::Util:       return "Util";
        case Page::Routing:    return "Routing";
        case Page::Library:    return "Library";
        case Page::Setup:      return "Setup";
        case Page::Listen:     return "Listen";
    }
    return {};
}

void DLiveSurface::refreshAll()
{
    metersScreen->showTab (metersScreen->getTab());
    selectionChanged();
    processing->refreshAll();
    bindStrips();
    bindPeq();
    repaint();
}

void DLiveSurface::selectionChanged()
{
    auto* s = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
    if (s != processing->getStrip())
    {
        processing->setStrip (s);
        scenes->setStrip (s);
        mixRack->setStrip (s);
        metersScreen->setStrip (s);
        ioScreen->setStrip (s);
        surfaceScreen->setStrip (s);
    }
    bindPeq();
}

void DLiveSurface::showChanged()
{
    scenes->refresh();
}

void DLiveSurface::timerCallback()
{
    if (++linkTick % 5 == 0)
        assignments.pullShowLayers(); // a layer made or edited in the Universal Mixer or on another surface
    for (auto& s : strips)
        s->refresh();
    for (auto& [name, k] : keys)
        k->refresh();
    for (auto& r : peq)
        r->refresh();
    dataRotary->refresh();
    phones->refresh();
    universalKey.refresh();
    c3500Key.refresh();
    if ((ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr) != boundStrip)
        selectionChanged();
    // PAFL's meters, the Assigned light, the banks' peak lights.
    repaint (juce::Rectangle<float> (834.0f, 72.0f, 90.0f, 230.0f).transformedBy (design).getSmallestIntegerContainer());
    repaint (juce::Rectangle<float> (50.0f, 140.0f, 24.0f, 14.0f).transformedBy (design).getSmallestIntegerContainer());
    repaint (juce::Rectangle<float> (960.0f, 168.0f, 16.0f, 16.0f).transformedBy (design).getSmallestIntegerContainer());
    repaint (juce::Rectangle<float> (1744.0f, 650.0f, 16.0f, 320.0f).transformedBy (design).getSmallestIntegerContainer());
}

void DLiveSurface::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0b0b0c));
    g.addTransform (design);
    const auto& l = look();

    auto top = juce::Rectangle<int> (kDesignWidth, kTopBar);
    g.setColour (theme.panel);
    g.fillRect (top);
    g.setColour (theme.text);
    g.setFont (type::font (type::kTitle, true));
    g.drawText ("dLive C1500", top.reduced (sizes::kGapLarge, 0), juce::Justification::centredLeft);
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kCaption));
    g.drawText ("Not affiliated with or endorsed by any console maker; product names are their owners' trademarks.",
                top.reduced (sizes::kGapLarge, 0).withTrimmedLeft (170).withTrimmedRight (390), juce::Justification::centredLeft);

    // The upper panel, nearly black; the surface under it.
    const juce::Rectangle<float> upper (0.0f, (float) kTopBar, (float) kDesignWidth, (float) (kStripsY - kTopBar - 2));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff191a1e), 0.0f, upper.getY(), juce::Colour (0xff24262b), 0.0f, upper.getBottom(), false));
    g.fillRect (upper);
    hw::panel (g, juce::Rectangle<float> (0.0f, upper.getBottom(), (float) kDesignWidth, (float) kDesignHeight - upper.getBottom()), l);
    g.setColour (juce::Colours::white.withAlpha (0.14f));
    g.fillRect (0.0f, upper.getBottom(), (float) kDesignWidth, 1.5f);

    // The screen's silver bezel.
    {
        const juce::Rectangle<float> bezel ((float) kScreenX - 5.0f, (float) kScreenY - 5.0f, (float) dl::kTouchW * kScreenScale + 10.0f, (float) dl::kTouchH * kScreenScale + 10.0f);
        g.setColour (juce::Colour (0xffb9bdc3));
        g.fillRoundedRectangle (bezel, 4.0f);
        g.setColour (juce::Colours::black);
        g.fillRect (bezel.reduced (3.0f));
    }

    // The boxes the upper panel's keys sit in.
    const auto outline = [&g] (juce::Rectangle<float> r)
    {
        g.setColour (juce::Colour (0xff9fa5ad).withAlpha (0.55f));
        g.drawRoundedRectangle (r, 8.0f, 1.2f);
    };
    outline ({ 18.0f, 122.0f, 86.0f, 78.0f });
    outline ({ 18.0f, 204.0f, 86.0f, 144.0f });
    outline ({ 750.0f, 52.0f, 80.0f, 386.0f });
    outline ({ 836.0f, 54.0f, 84.0f, 296.0f });
    outline ({ 926.0f, 104.0f, 84.0f, 152.0f });
    outline ({ 1060.0f, 54.0f, 64.0f, 110.0f });
    for (int band = 0; band < 4; ++band)
        outline ({ 1180.0f + (float) band * 100.0f, 58.0f, 96.0f, 122.0f });

    // PAFL's three meters.
    {
        const float x[] = { 845.0f, 870.0f, 895.0f };
        const float marks[] = { 12.0f, 6.0f, 3.0f, 0.0f, -3.0f, -6.0f, -9.0f, -12.0f, -16.0f, -20.0f, -40.0f };
        for (int m = 0; m < 3; ++m)
        {
            const juce::Rectangle<float> r (x[m], 76.0f, 15.0f, 220.0f);
            g.setColour (juce::Colour (0xffb9bdc3));
            g.fillRoundedRectangle (r.expanded (2.0f), 3.0f);
            g.setColour (juce::Colour (0xff111214));
            g.fillRoundedRectangle (r, 2.0f);
            const auto levelAt = [this] (int side) { return juce::Decibels::gainToDecibels (ctx.engine.phones.peak[side].load(), -100.0f); };
            const float db = (m == 2 ? juce::jmax (levelAt (0), levelAt (1)) : levelAt (m)) + 18.0f;
            const float seg = r.getHeight() / (float) std::size (marks);
            for (int i = 0; i < (int) std::size (marks); ++i)
            {
                const auto colour = i == 0 ? juce::Colour (0xffff3030) : i < 3 ? juce::Colour (0xfff0c030) : juce::Colour (0xff40d070);
                g.setColour (db >= marks[i] ? colour : colour.withAlpha (0.14f));
                g.fillRect (r.getX() + 2.5f, r.getY() + (float) i * seg + 2.0f, r.getWidth() - 5.0f, seg - 4.0f);
            }
        }
    }

    // The Assigned light.
    {
        auto* s = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
        const bool assigned = s != nullptr && (s->params.preInsert().fx.load() >= 0 || (s->getKind() != StripKind::Aux && s->params.postInsert().fx.load() >= 0));
        g.setColour (assigned ? juce::Colour (0xffff4040) : juce::Colour (0xff3a2020));
        g.fillEllipse (57.0f, 143.0f, 8.0f, 8.0f);
    }
    g.setColour (ctx.engine.isTalkOn (0) ? juce::Colour (0xffff4040) : juce::Colour (0xff3a2020));
    g.fillEllipse (964.0f, 172.0f, 8.0f, 8.0f); // Talk Active

    // The two USB sockets, drawn.
    {
        const char* names[] = { "Record/Playback", "Data" };
        for (int i = 0; i < 2; ++i)
        {
            const juce::Rectangle<float> r (1640.0f, 60.0f + (float) i * 88.0f, 108.0f, 82.0f);
            g.setColour (juce::Colour (0xff3c3f46));
            g.fillRoundedRectangle (r, 6.0f);
            hw::silk (g, r.withHeight (14.0f).translated (0.0f, 3.0f), names[i], l, 8.5f, juce::Justification::centred, false);
            g.setColour (juce::Colour (0xff0d0e10));
            g.fillRoundedRectangle (juce::Rectangle<float> (46.0f, 24.0f).withCentre (r.getCentre().translated (0.0f, 8.0f)), 2.0f);
            g.setColour (juce::Colour (0xff4c74c0));
            g.fillRect (juce::Rectangle<float> (40.0f, 8.0f).withCentre (r.getCentre().translated (0.0f, 8.0f)));
        }
    }

    // The banks' peak lights.
    for (int b = 0; b < kBanks; ++b)
    {
        bool peak = false;
        for (int s = 0; s < kStrips && ! peak; ++s)
        {
            auto* st = dl::stripOf (ctx.engine, assignments.getShownSlot (0, b, s, (int) ctx.engine.getMains().size()));
            peak = st != nullptr && juce::Decibels::gainToDecibels (ctx.meters.get (st, st->isInput() ? MeterPoint::Trim : MeterPoint::Output, 0), -100.0f) > -5.0f;
        }
        g.setColour (peak ? juce::Colour (0xffff3030) : juce::Colour (0xff3a2020));
        g.fillEllipse (1749.0f, 658.0f + (float) b * 52.0f, 7.0f, 7.0f);
    }

    // The strips' groups: the outlines above the faders, the LCD windows.
    for (int group = 0; group < 3; ++group)
    {
        const float x0 = (float) stripX (group * 4), x1 = (float) stripX (group * 4 + 3) + (float) kStripW;
        g.setColour (juce::Colour (0xffb9bdc3));
        g.drawRect (juce::Rectangle<float> (x0 - 4.0f, (float) kStripsY + 74.0f, x1 - x0 + 8.0f, 70.0f), 2.5f);
        g.setColour (juce::Colour (0xff9fa5ad).withAlpha (0.5f));
        g.drawVerticalLine ((int) x1 + 7, (float) kStripsY, (float) kStripsY + (float) kStripH - 12.0f);
    }

    for (const auto& s : silks)
    {
        if (s.text.containsChar ('\n'))
        {
            g.setColour (l.silk);
            g.setFont (type::font (s.size, true));
            g.drawFittedText (s.text, s.area.toNearestInt(), s.justification, 2);
        }
        else
            hw::silk (g, s.area, s.text, l, s.size, s.justification, s.dim);
    }
}

void DLiveSurface::resized()
{
    const float s = juce::jmin ((float) getWidth() / (float) kDesignWidth, (float) getHeight() / (float) kDesignHeight);
    design = s > 0.0f ? juce::AffineTransform::scale (s).translated (((float) getWidth() - (float) kDesignWidth * s) * 0.5f, ((float) getHeight() - (float) kDesignHeight * s) * 0.5f)
                      : juce::AffineTransform();
    for (auto* c : getChildren())
        c->setTransform (design);
    auto bar = juce::Rectangle<int> (kDesignWidth, kTopBar);
    universalKey.setBounds (bar.removeFromRight (190).reduced (sizes::kGap, 7));
    c3500Key.setBounds (bar.removeFromRight (190).reduced (sizes::kGap, 7));
    screenHost.setBounds (0, 0, dl::kTouchW, dl::kTouchH);
    screenHost.setTransform (juce::AffineTransform::scale (kScreenScale).translated ((float) kScreenX, (float) kScreenY).followedBy (design));
    maximiser.layout();
}

} // namespace livedaw::ui
