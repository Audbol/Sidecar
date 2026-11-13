#include "ui/PluginHostRack.h"

#include "engine/Models.h"
#include "engine/SandboxedPlugin.h"
#include "ui/ChainSummary.h"

#include <algorithm>

namespace livedaw::ui::ph
{
using namespace universal;

namespace
{
    constexpr int kLongPressMs = 600;

    /** The block kinds the "+" tile offers besides a plug-in. */
    constexpr BlockKind kBuiltIn[] = { BlockKind::Filter, BlockKind::Eq, BlockKind::Dynamics, BlockKind::Insert, BlockKind::Delay };

    std::atomic<bool>* switchOfBlock (const ChainBlock& block)
    {
        switch (block.getKind())
        {
            case BlockKind::Eq:       return &block.eq()->on;
            case BlockKind::Dynamics: return &block.dynamics()->on;
            case BlockKind::Insert:   return &block.insert()->on;
            case BlockKind::Delay:    return &block.delay()->on;
            case BlockKind::Plugin:   return block.plugin();
            case BlockKind::Filter:   break;
        }
        return nullptr;
    }

    /** How much a component is scaled on the screen (the surface scales its design space to fit the window). */
    float screenScale (const juce::Component& c)
    {
        const auto a = c.localPointToGlobal (juce::Point<float> (0.0f, 0.0f)), b = c.localPointToGlobal (juce::Point<float> (1000.0f, 0.0f));
        const float s = (b.x - a.x) / 1000.0f;
        return s > 0.01f ? s : 1.0f;
    }

    juce::String tileSignature (int channel, int dockChannel, int dockUid, int selectedUid, const std::vector<RackPage::TileInfo>& infos)
    {
        auto sig = juce::String (channel) + "|" + juce::String (dockChannel) + ":" + juce::String (dockUid) + "|" + juce::String (selectedUid);
        for (const auto& i : infos)
            sig << "|" << i.uid << "," << i.name << "," << i.maker << "," << i.latency << "," << (int) i.bypassed << (int) i.faulted << (int) i.missing << (int) i.loaded;
        return sig;
    }

    juce::String latencyText (int samples, double sampleRate)
    {
        auto text = juce::String (samples) + " smp";
        if (sampleRate > 0.0)
            text << "  (" << juce::String (1000.0 * samples / sampleRate, 1) << " ms)";
        return text;
    }
} // namespace

//==============================================================================
// RackDock

/** Listens to the shown plug-in: a parameter gesture (what its editor, or a generic slider, starts when it is touched). */
class RackDock::Watcher final : public juce::AudioProcessorListener
{
public:
    explicit Watcher (RackDock& d) : dock (d) {}
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index) override
    {
        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
            dock.parameterTouched (index);
    }

private:
    RackDock& dock;
};

RackDock::RackDock (UiContext& c, const Theme& t)
    : ctx (c), theme (t), pinKey ("PIN", t.accent, t), popKey ("POP OUT", t.accent, t), openKey ("OPEN EDITOR WINDOW", t.accent, t),
      chooseKey ("CHOOSE PLUG-IN", t.accent, t), hot1Key ("ASSIGN HOT 1", t.accent, t), hot2Key ("ASSIGN HOT 2", t.accent, t), watcher (std::make_unique<Watcher> (*this)),
      blockEditor (c, t)
{
    for (int k = 0; k < 2; ++k)
    {
        auto& key = getHotKey (k);
        key.isOn = [this, k] { return assigning == k; };
        key.onClick = [this, k]
        {
            assigning = assigning == k ? -1 : k;
            refreshHotKeys();
        };
        key.setTooltip ("Tap, then touch or move a control of the plug-in: that parameter becomes this hot knob's");
        addChildComponent (key);
    }
    pinKey.isOn = [this] { return pinned; };
    pinKey.onClick = [this] { setPinned (! pinned); };
    pinKey.setTooltip ("Keep the dock on this plug-in while the channel changes");
    popKey.isOn = [] { return false; };
    popKey.onClick = [this]
    {
        if (shownSlot >= 0 && ctx.openPluginEditor)
            ctx.openPluginEditor (shownSlot);
    };
    popKey.setTooltip ("Open the plug-in in a window of its own");
    openKey.isOn = [] { return false; };
    openKey.onClick = [this] { openSandboxWindow(); };
    chooseKey.isOn = [] { return false; };
    chooseKey.onClick = [this]
    {
        const int s = slot();
        if (s < 0)
            return;
        juce::Component::SafePointer<RackDock> safe (this);
        auto menu = pluginTypesMenu (ctx, [this, s, safe] (const juce::PluginDescription& desc)
        {
            if (loadPluginIntoFx (ctx, s, desc) && safe != nullptr)
                refresh();
        });
        if (menu.getNumItems() == 0)
            menu.addItem ("Scan for plug-ins...", [this] { if (ctx.showPluginScanner) ctx.showPluginScanner(); });
        menu.setLookAndFeel (&lookAndFeelFor (theme));
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&chooseKey).withStandardItemHeight (sizes::kTouch));
    };
    for (auto* k : { &pinKey, &popKey, &openKey, &chooseKey })
        addChildComponent (*k);
    addChildComponent (holder);
    viewport.setScrollBarsShown (true, false);
    addChildComponent (viewport);
    addChildComponent (blockEditor);
    holder.setInterceptsMouseClicks (false, true);
    startTimerHz (4);
}

RackDock::~RackDock()
{
    stopTimer();
    dropEditor();
    unwatch();
}

void RackDock::watch (juce::AudioProcessor* p)
{
    unwatch();
    if (p != nullptr)
    {
        p->addListener (watcher.get());
        watched = p;
    }
}

void RackDock::unwatch()
{
    if (watched != nullptr)
        watched->removeListener (watcher.get());
    watched = nullptr;
}

void RackDock::parameterTouched (int paramIndex)
{
    auto* plugin = shownPlugin;
    const int s = slot();
    if (control == nullptr || plugin == nullptr || s < 0)
        return;
    const PluginRef ref { channel, uid, member };
    const auto param = PluginChain::ParamRef::of (*plugin, paramIndex);
    if (! param.isSet())
        return;
    if (assigning >= 0)
    {
        const int knob = assigning;
        assigning = -1;
        control->assignHot (ref, knob, param);
        refreshHotKeys();
    }
    else if (control->isLearning())
        control->captureTarget (Target::ofParam (ref, param));
}

void RackDock::refreshHotKeys()
{
    const bool usable = control != nullptr && shownPlugin != nullptr && (content == Content::Editor || content == Content::Generic || content == Content::Sandbox || content == Content::TooLarge);
    for (int k = 0; k < 2; ++k)
    {
        auto& key = getHotKey (k);
        juce::String text = "ASSIGN HOT " + juce::String (k + 1);
        if (usable)
        {
            if (assigning == k)
                text = "TOUCH A CONTROL";
            else if (const auto c = control->controlOf ({ channel, uid, member }); c.hot[(size_t) k].isSet())
                text = "HOT " + juce::String (k + 1) + ": " + c.hot[(size_t) k].name.toUpperCase();
        }
        key.setButtonText (text);
        key.setVisible (usable);
        key.refresh();
    }
}

Strip* RackDock::strip() const
{
    const auto& list = ctx.engine.getChannels();
    return juce::isPositiveAndBelow (channel, (int) list.size()) ? list[(size_t) channel].get() : nullptr;
}

int RackDock::slot() const
{
    auto* s = strip();
    return s != nullptr && uid >= 0 ? FxSlots::blockSlotOf (s->getHandle(), uid) : -1;
}

juce::String RackDock::title() const
{
    auto* s = strip();
    if (s == nullptr || uid < 0)
        return "NO BLOCK";
    juce::String name;
    for (const auto& step : s->getChain())
        if (step.isBlock() && step.uid == uid)
            name = stepName (*s, step);
    return "CH " + juce::String (channel + 1) + "  " + name;
}

void RackDock::follow (int ch, int blockUid)
{
    if (pinned || (ch == channel && blockUid == uid))
        return;
    channel = ch;
    uid = blockUid;
    member = 0;
    rebuild();
}

void RackDock::setPinned (bool shouldPin)
{
    pinned = shouldPin && uid >= 0;
    pinKey.refresh();
    repaint();
}

void RackDock::showMember (int index)
{
    if (index == member || index < 0)
        return;
    member = index;
    rebuild();
}

void RackDock::dropEditor()
{
    unwatch();
    if (editor != nullptr)
    {
        if (shownPlugin != nullptr)
            shownPlugin->editorBeingDeleted (editor.get());
        viewport.setViewedComponent (nullptr, false);
        holder.removeChildComponent (editor.get());
        editor.reset();
    }
}

void RackDock::dropEditorFor (const juce::AudioProcessor* p)
{
    if (p == nullptr || p == shownPlugin || (shownSlot >= 0 && ctx.engine.getFx().getPlugin (shownSlot) == p))
    {
        dropEditor();
        shownPlugin = nullptr;
        shownSlot = -1;
        content = Content::Empty;
        signature = "dropped"; // the timer looks again
        place();
        repaint();
    }
}

void RackDock::openSandboxWindow()
{
    auto* sandboxed = dynamic_cast<SandboxedPlugin*> (shownPlugin);
    if (sandboxed == nullptr)
        return;
    juce::Component::SafePointer<RackDock> safe (this);
    sandboxed->openEditorWindow (sandboxed->getName() + " - Sidecar plugin sandbox", [safe] (const juce::String& error)
    {
        if (safe != nullptr)
        {
            safe->detail = error.isNotEmpty() ? "The plug-in's editor could not open: " + error : juce::String();
            safe->repaint();
        }
    });
}

void RackDock::refresh()
{
    timerCallback();
}

void RackDock::timerCallback()
{
    // What the block holds now: the block itself, and each plug-in of it (a scene, a remove or a replace changes them).
    juce::String now = "none";
    if (auto* s = strip(); s != nullptr && uid >= 0)
    {
        now = "gone";
        for (const auto& step : s->getChain())
            if (step.isBlock() && step.uid == uid)
            {
                now = juce::String (step.arg);
                if (step.getBlockKind() == BlockKind::Plugin)
                {
                    auto& fx = ctx.engine.getFx();
                    const int sl = FxSlots::blockSlotOf (s->getHandle(), uid), n = fx.getNumChainMembers (sl);
                    now << ":" << n;
                    for (int i = 0; i < n; ++i)
                        now << ":" << (juce::int64) (std::intptr_t) fx.getChainMember (sl, i);
                }
            }
    }
    if (now != signature)
    {
        signature = now;
        if (now == "gone")
        {
            pinned = false;
            uid = -1;
        }
        rebuild();
        signature = now == "gone" ? juce::String ("none") : now;
    }
    else if (content == Content::Sandbox || content == Content::NoPlugin)
        repaint (body);
    refreshHotKeys();
}

void RackDock::rebuild()
{
    dropEditor();
    memberKeys.clear();
    content = Content::Empty;
    message = "SELECT A BLOCK ON THE RACK";
    detail.clear();
    shownPlugin = nullptr;
    shownSlot = -1;
    assigning = -1;
    blockEditor.setVisible (false);

    auto* s = strip();
    if (s != nullptr && uid >= 0)
    {
        ChainStep step;
        bool found = false;
        for (const auto& c : s->getChain())
            if (c.isBlock() && c.uid == uid)
            {
                step = c;
                found = true;
            }
        if (! found)
        {
            pinned = false;
            uid = -1;
        }
        else if (step.getBlockKind() != BlockKind::Plugin)
        {
            blockEditor.setTarget (BlockTarget::of (ctx, *s, step));
            content = Content::BuiltIn;
        }
        else
        {
            auto& fx = ctx.engine.getFx();
            const int sl = FxSlots::blockSlotOf (s->getHandle(), uid), n = sl >= 0 ? fx.getNumChainMembers (sl) : 0;
            shownSlot = sl;
            if (n == 0)
            {
                content = Content::NoPlugin;
                const auto info = sl >= 0 ? fx.describeBlock (s->getHandle(), uid) : FxSlots::BlockInfo();
                message = info.missing ? "THE PLUG-IN \"" + info.name + "\" WAS NOT FOUND" : juce::String ("NO PLUG-IN IN THIS BLOCK");
            }
            else
            {
                member = juce::jlimit (0, n - 1, member);
                shownPlugin = fx.getChainMember (sl, member);
                if (n > 1)
                    for (int i = 0; i < n; ++i)
                    {
                        auto key = std::make_unique<ToggleKey> (juce::String (i + 1) + "  " + fx.getChainMember (sl, i)->getName().toUpperCase(), theme.accent, theme);
                        key->isOn = [this, i] { return member == i; };
                        key->onClick = [this, i] { showMember (i); };
                        addAndMakeVisible (*key);
                        memberKeys.push_back (std::move (key));
                    }
                if (shownPlugin == nullptr)
                {
                    content = Content::NoPlugin;
                    message = "NO PLUG-IN IN THIS BLOCK";
                }
                else if (dynamic_cast<SandboxedPlugin*> (shownPlugin) != nullptr)
                {
                    content = Content::Sandbox;
                    editor = std::make_unique<juce::GenericAudioProcessorEditor> (*shownPlugin);
                    viewport.setViewedComponent (editor.get(), false);
                    message = "THIS PLUG-IN RUNS IN THE PLUG-IN SANDBOX";
                }
                else if (shownPlugin->hasEditor())
                {
                    editor.reset (shownPlugin->createEditorAndMakeActive());
                    if (editor != nullptr)
                    {
                        holder.addAndMakeVisible (*editor);
                        content = Content::Editor;
                    }
                }
                if (content == Content::Empty)
                {
                    content = Content::Generic;
                    editor = std::make_unique<juce::GenericAudioProcessorEditor> (*shownPlugin);
                    viewport.setViewedComponent (editor.get(), false);
                }
                watch (shownPlugin);
            }
        }
    }
    place();
    refreshHotKeys();
    repaint();
}

void RackDock::resized()
{
    place();
}

void RackDock::place()
{
    if (content == Content::TooLarge && editor != nullptr)
        content = Content::Editor; // looked at again: the window may have grown
    auto area = getLocalBounds();
    auto head = area.removeFromTop (kHeader).reduced (sizes::kGap, 2);
    popKey.setBounds (head.removeFromRight (132).reduced (2, 0));
    pinKey.setBounds (head.removeFromRight (84).reduced (2, 0));
    hot2Key.setBounds (head.removeFromRight (kHotKeyWidth).reduced (2, 0));
    hot1Key.setBounds (head.removeFromRight (kHotKeyWidth).reduced (2, 0));
    const bool hasPlugin = shownSlot >= 0 && content != Content::NoPlugin && content != Content::Empty;
    popKey.setVisible (hasPlugin && ctx.openPluginEditor != nullptr);
    pinKey.setVisible (uid >= 0);
    pinKey.refresh();

    if (! memberKeys.empty())
    {
        auto row = area.removeFromTop (sizes::kTouch + 4).reduced (sizes::kGap, 2);
        const int w = juce::jmin (260, row.getWidth() / (int) memberKeys.size());
        for (auto& k : memberKeys)
            k->setBounds (row.removeFromLeft (w).reduced (2, 0));
    }
    body = area.reduced (sizes::kGap);

    openKey.setVisible (content == Content::Sandbox);
    chooseKey.setVisible (content == Content::NoPlugin && shownSlot >= 0);
    viewport.setVisible (content == Content::Generic || content == Content::Sandbox);
    holder.setVisible (content == Content::Editor);
    blockEditor.setVisible (content == Content::BuiltIn);

    auto work = body;
    switch (content)
    {
        case Content::Sandbox:
        {
            auto panel = work.removeFromTop (116);
            openKey.setBounds (panel.removeFromBottom (sizes::kTouch).removeFromLeft (280));
            viewport.setBounds (work);
            if (editor != nullptr)
                editor->setSize (juce::jmax (200, work.getWidth() - viewport.getScrollBarThickness()), editor->getHeight());
            break;
        }
        case Content::Generic:
            viewport.setBounds (work);
            if (editor != nullptr)
                editor->setSize (juce::jmax (200, work.getWidth() - viewport.getScrollBarThickness()), editor->getHeight());
            break;
        case Content::NoPlugin:
            chooseKey.setBounds (work.withSizeKeepingCentre (280, sizes::kTouch).translated (0, 40));
            break;
        case Content::BuiltIn:
            blockEditor.setBounds (work);
            break;
        case Content::Editor:
        {
            // A native editor is given real pixels: the surface's scale is taken back out. One that is
            // too big for the dock is made smaller if it can be, else the dock says so.
            const float s = screenScale (*this);
            const int availW = juce::roundToInt ((float) work.getWidth() * s), availH = juce::roundToInt ((float) work.getHeight() * s);
            int ew = juce::jmax (1, editor->getWidth()), eh = juce::jmax (1, editor->getHeight());
            if ((ew > availW || eh > availH) && editor->isResizable())
            {
                editor->setSize (juce::jmin (ew, availW), juce::jmin (eh, availH));
                ew = juce::jmax (1, editor->getWidth());
                eh = juce::jmax (1, editor->getHeight());
            }
            if (ew <= availW && eh <= availH)
            {
                holder.setBounds (juce::roundToInt ((float) work.getX() * s), juce::roundToInt ((float) work.getY() * s), ew, eh);
                holder.setTransform (juce::AffineTransform::scale (1.0f / s));
                holder.setVisible (true);
            }
            else
            {
                holder.setVisible (false);
                content = Content::TooLarge;
                message = "THIS PLUG-IN'S EDITOR (" + juce::String (ew) + " x " + juce::String (eh) + ") IS LARGER THAN THE DOCK";
                detail = "Use POP OUT to open it in a window of its own, or maximise the screen (F10).";
            }
            break;
        }
        case Content::TooLarge:
        case Content::Empty:
            break;
    }
}

void RackDock::paint (juce::Graphics& g)
{
    g.fillAll (theme.panel);
    g.setColour (theme.outline);
    g.drawVerticalLine (0, 0.0f, (float) getHeight());
    auto head = getLocalBounds().removeFromTop (kHeader).reduced (sizes::kGapLarge, 0);
    head.removeFromRight (84 + 132 + 8 + 2 * kHotKeyWidth);
    g.setColour (theme.text);
    g.setFont (type::font (type::kHeading, true));
    g.drawFittedText ((pinned ? "PINNED   " : juce::String()) + title(), head, juce::Justification::centredLeft, 1, 0.8f);
    g.setColour (theme.outline);
    g.drawHorizontalLine (kHeader - 1, 0.0f, (float) getWidth());

    if (content == Content::Editor || content == Content::Generic || content == Content::BuiltIn)
        return;
    auto text = body.reduced (sizes::kGapLarge, 0);
    if (content == Content::Sandbox)
        text = text.removeFromTop (60);
    else
        text = text.withTrimmedTop (juce::jmax (0, body.getHeight() / 2 - 70)).removeFromTop (110);
    g.setColour (content == Content::Empty ? theme.textDim : theme.text);
    g.setFont (type::font (type::kHeading, true));
    g.drawFittedText (message, text.removeFromTop (28), content == Content::Sandbox ? juce::Justification::centredLeft : juce::Justification::centred, 1, 0.8f);
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kLabel));
    juce::String more = detail;
    if (more.isEmpty() && content == Content::Sandbox)
        more = "Its own editor opens in a window of the sandbox process and cannot be embedded here. OPEN shows it; its parameters are below.";
    if (more.isEmpty() && content == Content::NoPlugin)
        more = "Choose a plug-in from the browser.";
    g.drawFittedText (more, text, content == Content::Sandbox ? juce::Justification::topLeft : juce::Justification::centredTop, 3, 0.9f);
}

//==============================================================================
// RackPage: the tiles

class RackPage::Canvas final : public juce::Component, private juce::Timer
{
public:
    explicit Canvas (RackPage& o) : owner (o) { setViewportIgnoreDragFlag (true); }
    ~Canvas() override { stopTimer(); }

    int numItems() const { return owner.getNumTiles() + (owner.getNumTiles() < ld::kMaxExtraBlocks ? 1 : 0); }

    static juce::Rectangle<int> itemBounds (int i)
    {
        const int col = i % kColumns, row = i / kColumns;
        return { 16 + col * (kTileWidth + kTileGap), 8 + row * (kTileHeight + kTileGap), kTileWidth, kTileHeight };
    }

    int totalHeight() const { return itemBounds (juce::jmax (0, numItems() - 1)).getBottom() + 12; }

    int itemAt (juce::Point<float> p) const
    {
        for (int i = 0; i < numItems(); ++i)
            if (itemBounds (i).toFloat().contains (p))
                return i;
        return -1;
    }

    /** The tile a drop at `p` goes before (the number of tiles: the end). */
    int dropAt (juce::Point<float> p) const
    {
        int before = 0;
        for (int i = 0; i < owner.getNumTiles(); ++i)
        {
            const auto r = itemBounds (i);
            if (p.y > (float) r.getBottom() || (p.y >= (float) r.getY() && p.x > (float) r.getCentreX()))
                before = i + 1;
        }
        return before;
    }

    static juce::Rectangle<int> bypassRect (juce::Rectangle<int> tile) { return tile.removeFromBottom (52).reduced (12, 6); }

    void paint (juce::Graphics& g) override
    {
        const auto& t = owner.theme;
        const int n = owner.getNumTiles();
        for (int i = 0; i < numItems(); ++i)
        {
            const auto r = itemBounds (i).toFloat();
            // The signal's way: a chevron in the gap to the next tile.
            if (i + 1 < numItems() && (i + 1) % kColumns != 0)
            {
                const auto c = juce::Point<float> (r.getRight() + (float) kTileGap * 0.5f, r.getCentreY());
                juce::Path p;
                p.startNewSubPath (c.x - 4.0f, c.y - 8.0f);
                p.lineTo (c.x + 4.0f, c.y);
                p.lineTo (c.x - 4.0f, c.y + 8.0f);
                g.setColour (t.textDim);
                g.strokePath (p, juce::PathStrokeType (2.0f));
            }
            if (i == n)
            {
                g.setColour (t.well);
                g.fillRoundedRectangle (r, 8.0f);
                g.setColour (t.outline);
                const float dashes[] = { 8.0f, 6.0f };
                juce::Path outline;
                outline.addRoundedRectangle (r.reduced (1.0f), 8.0f);
                juce::Path dashed;
                juce::PathStrokeType (1.5f).createDashedStroke (dashed, outline, dashes, 2);
                g.fillPath (dashed);
                g.setColour (t.accent);
                g.setFont (type::font (56.0f, true));
                g.drawText ("+", r.withTrimmedBottom (34.0f), juce::Justification::centred);
                g.setColour (t.textDim);
                g.setFont (type::font (type::kLabel, true));
                g.drawText ("ADD A PLUG-IN OR BLOCK", r.withTrimmedTop (r.getHeight() - 44.0f), juce::Justification::centred);
                continue;
            }
            paintTile (g, i, r);
        }
        if (dragging)
        {
            const int before = juce::jlimit (0, n, drop);
            const auto r = itemBounds (juce::jmin (before, juce::jmax (0, numItems() - 1)));
            const float x = before < n ? (float) r.getX() - (float) kTileGap * 0.5f : (float) itemBounds (juce::jmax (0, n - 1)).getRight() + (float) kTileGap * 0.5f;
            const float y = before < n ? (float) r.getY() : (float) itemBounds (juce::jmax (0, n - 1)).getY();
            g.setColour (t.accent);
            g.fillRoundedRectangle (x - 2.0f, y, 4.0f, (float) kTileHeight, 2.0f);
        }
    }

    void paintTile (juce::Graphics& g, int i, juce::Rectangle<float> r)
    {
        const auto& t = owner.theme;
        const auto& info = owner.infos[(size_t) i];
        const bool sel = info.uid == owner.selectedUid;
        const bool docked = owner.dock.getChannel() == owner.channel && owner.dock.getUid() == info.uid;
        const bool dim = info.bypassed || info.missing || (info.kind == BlockKind::Plugin && ! info.loaded);
        g.setColour (sel ? t.lifted (t.raised, 0.08f) : t.raised);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (sel ? t.accent : docked ? t.accent.withAlpha (0.55f) : t.outline);
        g.drawRoundedRectangle (r.reduced (1.0f), 8.0f, sel ? 2.5f : 1.0f);
        if (dragging && pressed == i)
        {
            g.setColour (t.background.withAlpha (0.55f));
            g.fillRoundedRectangle (r, 8.0f);
        }

        auto area = r.reduced (14.0f, 12.0f);
        auto top = area.removeFromTop (28.0f);
        auto badge = top.removeFromLeft (28.0f);
        g.setColour (t.well);
        g.fillEllipse (badge);
        g.setColour (t.accent);
        g.setFont (type::font (type::kLabel, true));
        g.drawText (juce::String (i + 1), badge, juce::Justification::centred);
        top.removeFromLeft (10.0f);
        if (docked)
        {
            g.setColour (t.accent);
            g.setFont (type::font (type::kMicro, true));
            g.drawText ("DOCKED", top.removeFromRight (60.0f), juce::Justification::centredRight);
        }
        g.setColour (dim ? t.textDim : t.text);
        g.setFont (type::font (18.0f, true));
        g.drawFittedText (info.name, top.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        area.removeFromTop (4.0f);
        g.setColour (t.textDim);
        g.setFont (type::font (type::kLabel));
        g.drawFittedText (info.maker, area.removeFromTop (20.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        const auto latency = "LATENCY " + latencyText (info.latency, owner.ctx.engine.getSampleRate());
        g.setFont (type::font (type::kCaption));
        g.drawFittedText (latency, area.removeFromTop (20.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        juce::String status;
        if (info.missing)
            status = "NOT FOUND - BYPASSED";
        else if (info.kind == BlockKind::Plugin && ! info.loaded)
            status = "EMPTY - SELECT IT TO CHOOSE A PLUG-IN";
        else if (info.faulted)
            status = "FAULT - BYPASSED";
        else if (info.bypassed)
            status = "BYPASSED";
        g.setColour (info.faulted || info.missing ? t.mute : t.textDim);
        g.drawFittedText (status, area.removeFromTop (20.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

        // BYPASS: a key across the bottom of the tile.
        const auto key = bypassRect (itemBounds (i)).toFloat();
        g.setColour (info.bypassed ? t.mute : t.well);
        g.fillRoundedRectangle (key, 5.0f);
        g.setColour (info.canBypass ? t.outline : t.outline.withAlpha (0.4f));
        g.drawRoundedRectangle (key.reduced (0.5f), 5.0f, 1.0f);
        g.setColour (info.bypassed ? t.textOnAccent : info.canBypass ? t.text : t.textDim);
        g.setFont (type::font (type::kLabel, true));
        g.drawText (info.canBypass ? "BYPASS" : "-", key, juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        pressed = itemAt (e.position);
        dragging = false;
        longPressed = false;
        if (pressed >= 0 && pressed < owner.getNumTiles())
            startTimer (kLongPressMs);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (pressed < 0 || pressed >= owner.getNumTiles() || longPressed)
            return;
        if (! dragging && e.getDistanceFromDragStart() < 10)
            return;
        stopTimer();
        dragging = true;
        drop = dropAt (e.position);
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        stopTimer();
        const int n = owner.getNumTiles();
        const int was = pressed;
        pressed = -1;
        if (longPressed)
        {
            longPressed = false;
            return;
        }
        if (dragging)
        {
            dragging = false;
            repaint();
            owner.moveTile (was, drop);
            return;
        }
        if (was < 0 || itemAt (e.position) != was)
            return;
        if (was == n)
            owner.showAddMenu();
        else if (bypassRect (itemBounds (was)).toFloat().contains (e.position) && owner.infos[(size_t) was].canBypass)
            owner.setTileBypassed (was, ! owner.infos[(size_t) was].bypassed);
        else
            owner.selectTile (was);
    }

    void timerCallback() override
    {
        stopTimer();
        if (pressed >= 0 && pressed < owner.getNumTiles() && ! dragging)
        {
            longPressed = true;
            owner.showTileMenu (pressed);
        }
    }

private:
    RackPage& owner;
    int pressed = -1, drop = 0;
    bool dragging = false, longPressed = false;
};

//==============================================================================

RackPage::RackPage (UiContext& c, const Theme& t) : ctx (c), theme (t), canvas (std::make_unique<Canvas> (*this)), dock (c, t)
{
    viewport.setViewedComponent (canvas.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, theme.outline);
    addAndMakeVisible (viewport);
    addAndMakeVisible (dock);
    rebuildTiles();
    startTimerHz (4);
}

RackPage::~RackPage()
{
    stopTimer();
}

juce::Component& RackPage::getCanvas() noexcept
{
    return *canvas;
}

Strip* RackPage::strip() const
{
    const auto& list = ctx.engine.getChannels();
    return juce::isPositiveAndBelow (channel, (int) list.size()) ? list[(size_t) channel].get() : nullptr;
}

std::vector<ChainStep> RackPage::blocks() const
{
    std::vector<ChainStep> out;
    if (auto* s = strip())
        for (const auto& step : s->getChain())
            if (step.isBlock())
                out.push_back (step);
    return out;
}

std::atomic<bool>* RackPage::switchOf (int uid) const
{
    auto* s = strip();
    if (s == nullptr)
        return nullptr;
    const auto block = s->getBlock (uid);
    return block.isValid() ? switchOfBlock (block) : nullptr;
}

PluginRef RackPage::getFocus() const
{
    if (selectedUid < 0)
        return {};
    return { channel, selectedUid, dock.getChannel() == channel && dock.getUid() == selectedUid ? dock.getMember() : 0 };
}

RackPage::TileInfo RackPage::describe (const ChainStep& step) const
{
    TileInfo t;
    t.uid = step.uid;
    t.kind = step.getBlockKind();
    auto* s = strip();
    if (s == nullptr)
        return t;
    if (t.kind == BlockKind::Plugin)
    {
        const auto info = ctx.engine.getFx().describeBlock (s->getHandle(), step.uid);
        t.name = info.name.isNotEmpty() ? info.name : juce::String (info.missing ? "plug-in" : "EMPTY PLUG-IN BLOCK");
        t.maker = info.vendor;
        t.loaded = info.loaded;
        t.missing = info.missing;
        t.faulted = info.faulted;
        t.bypassed = info.bypassed;
        t.latency = info.latency;
        t.canBypass = info.loaded;
    }
    else
    {
        t.name = stepName (ChainStep { ld::kStepBlock, (int) t.kind });
        t.maker = "Built-in";
        t.loaded = true;
        auto* on = switchOf (step.uid);
        t.canBypass = on != nullptr;
        t.bypassed = on != nullptr && ! on->load();
    }
    return t;
}

void RackPage::rebuildTiles()
{
    tiles = blocks();
    infos.clear();
    for (const auto& step : tiles)
        infos.push_back (describe (step));
    selected = -1;
    for (int i = 0; i < (int) tiles.size(); ++i)
        if (tiles[(size_t) i].uid == selectedUid)
            selected = i;
    if (selected < 0)
        selectedUid = -1;
    signature = tileSignature (channel, dock.getChannel(), dock.getUid(), selectedUid, infos);
    canvas->setSize (juce::jmax (1, viewport.getWidth() - viewport.getScrollBarThickness()), juce::jmax (viewport.getHeight(), canvas->totalHeight()));
    canvas->repaint();
    repaint (0, 0, kLeftWidth, kHead);
}

void RackPage::refresh()
{
    const auto chain = blocks();
    std::vector<TileInfo> now;
    for (const auto& step : chain)
        now.push_back (describe (step));
    bool same = chain.size() == tiles.size();
    for (size_t i = 0; same && i < chain.size(); ++i)
        same = chain[i].uid == tiles[i].uid;
    if (same && tileSignature (channel, dock.getChannel(), dock.getUid(), selectedUid, now) == signature)
        return;
    rebuildTiles();
    // The selected block went (removed elsewhere): follow the first one.
    if (selectedUid < 0 && ! tiles.empty())
        selectUid (tiles.front().uid);
    else
        followSelection();
}

void RackPage::timerCallback()
{
    refresh();
}

void RackPage::setChannel (int index)
{
    index = juce::jlimit (0, juce::jmax (0, (int) ctx.engine.getChannels().size() - 1), index);
    if (selectedUid >= 0)
        lastUid[channel] = selectedUid;
    channel = index;
    selectedUid = -1;
    const auto remembered = lastUid.find (channel);
    const auto chain = blocks();
    int pick = -1;
    if (remembered != lastUid.end())
        for (const auto& step : chain)
            if (step.uid == remembered->second)
                pick = step.uid;
    // Else the first plug-in, else the first block.
    for (const auto& step : chain)
        if (pick < 0 && step.getBlockKind() == BlockKind::Plugin)
            pick = step.uid;
    if (pick < 0 && ! chain.empty())
        pick = chain.front().uid;
    selectedUid = pick;
    rebuildTiles();
    followSelection();
    repaint();
}

void RackPage::followSelection()
{
    dock.follow (channel, selectedUid);
}

void RackPage::selectUid (int uid)
{
    selectedUid = uid;
    if (uid >= 0)
        lastUid[channel] = uid;
    rebuildTiles();
    followSelection();
    rebuildTiles(); // the DOCKED mark
}

void RackPage::reselect (int uid)
{
    selectUid (uid);
}

RackPage::TileInfo RackPage::getTile (int index) const
{
    return juce::isPositiveAndBelow (index, (int) infos.size()) ? infos[(size_t) index] : TileInfo();
}

juce::Rectangle<int> RackPage::getTileBounds (int index) const
{
    return Canvas::itemBounds (index);
}

void RackPage::selectTile (int index)
{
    if (juce::isPositiveAndBelow (index, (int) tiles.size()))
        selectUid (tiles[(size_t) index].uid);
}

int RackPage::insertionPoint() const
{
    auto* s = strip();
    if (s == nullptr)
        return -1;
    const auto chain = s->getChain();
    if (selectedUid >= 0)
        for (int i = 0; i < (int) chain.size(); ++i)
            if (chain[(size_t) i].isBlock() && chain[(size_t) i].uid == selectedUid)
                return i + 1;
    for (int i = 0; i < (int) chain.size(); ++i)
        if (chain[(size_t) i].kind == ld::kStepPfl)
            return i;
    return -1;
}

bool RackPage::moveTile (int from, int before)
{
    auto* s = strip();
    const int n = (int) tiles.size();
    if (s == nullptr || ! juce::isPositiveAndBelow (from, n) || before < 0 || before > n || before == from || before == from + 1)
        return false;
    const auto chain = s->getChain();
    std::vector<int> at;
    for (int i = 0; i < (int) chain.size(); ++i)
        if (chain[(size_t) i].isBlock())
            at.push_back (i);
    if ((int) at.size() != n)
        return false;
    const int fromStep = at[(size_t) from];
    const int toStep = before < n ? at[(size_t) before] : at.back() + 1;
    const bool moved = s->moveBlock (tiles[(size_t) from].uid, toStep > fromStep ? toStep - 1 : toStep);
    rebuildTiles();
    return moved;
}

bool RackPage::removeTile (int index)
{
    auto* s = strip();
    if (s == nullptr || ! juce::isPositiveAndBelow (index, (int) tiles.size()))
        return false;
    const int uid = tiles[(size_t) index].uid;
    // A plug-in block: its editor closes before the engine lets the plug-in go.
    if (tiles[(size_t) index].getBlockKind() == BlockKind::Plugin)
        if (auto* plugin = ctx.engine.getFx().getPlugin (FxSlots::blockSlotOf (s->getHandle(), uid)))
        {
            dock.dropEditorFor (plugin);
            if (ctx.pluginRemoved)
                ctx.pluginRemoved (plugin);
        }
    const bool wasSelected = uid == selectedUid;
    const bool removed = s->removeBlock (uid);
    rebuildTiles();
    if (wasSelected)
    {
        const int next = tiles.empty() ? -1 : tiles[(size_t) juce::jmin (index, (int) tiles.size() - 1)].uid;
        selectUid (next);
    }
    dock.refresh();
    return removed;
}

bool RackPage::setTileBypassed (int index, bool bypassed)
{
    auto* s = strip();
    if (s == nullptr || ! juce::isPositiveAndBelow (index, (int) tiles.size()))
        return false;
    const auto& step = tiles[(size_t) index];
    if (step.getBlockKind() == BlockKind::Plugin)
    {
        const int slot = FxSlots::blockSlotOf (s->getHandle(), step.uid);
        if (slot < 0 || ! infos[(size_t) index].loaded)
            return false;
        ctx.engine.getFx().setBlockBypassed (slot, bypassed); // also clears a fault
    }
    else if (auto* on = switchOf (step.uid))
        on->store (! bypassed);
    else
        return false;
    rebuildTiles();
    return true;
}

bool RackPage::addBlock (BlockKind kind)
{
    auto* s = strip();
    if (s == nullptr)
        return false;
    const int uid = s->insertBlock (kind, insertionPoint());
    if (uid < 0)
        return false;
    reselect (uid);
    return true;
}

bool RackPage::addPlugin (const juce::PluginDescription& desc)
{
    auto* s = strip();
    if (s == nullptr)
        return false;
    const int uid = s->insertBlock (BlockKind::Plugin, insertionPoint());
    if (uid < 0)
        return false;
    const int slot = FxSlots::blockSlotOf (s->getHandle(), uid);
    if (slot < 0 || ! loadPluginIntoFx (ctx, slot, desc))
    {
        s->removeBlock (uid); // nothing was loaded into it: no editor to close
        rebuildTiles();
        return false;
    }
    reselect (uid);
    return true;
}

void RackPage::copyTile (int index)
{
    auto* s = strip();
    if (s == nullptr || ! juce::isPositiveAndBelow (index, (int) tiles.size()))
        return;
    clip = {};
    clip.valid = true;
    clip.kind = tiles[(size_t) index].getBlockKind();
    if (clip.kind == BlockKind::Plugin)
    {
        auto& fx = ctx.engine.getFx();
        const int slot = FxSlots::blockSlotOf (s->getHandle(), tiles[(size_t) index].uid);
        for (int i = 0; i < fx.getNumChainMembers (slot); ++i)
            if (auto* p = fx.getChainMember (slot, i))
            {
                Clip::Member m;
                m.description = PluginHost::describe (*p);
                p->getStateInformation (m.state);
                clip.members.push_back (std::move (m));
            }
    }
}

bool RackPage::pasteTile()
{
    auto* s = strip();
    if (s == nullptr || ! clip.valid)
        return false;
    const int uid = s->insertBlock (clip.kind, insertionPoint());
    if (uid < 0)
        return false;
    if (clip.kind == BlockKind::Plugin)
    {
        auto& fx = ctx.engine.getFx();
        const int slot = FxSlots::blockSlotOf (s->getHandle(), uid);
        for (size_t i = 0; i < clip.members.size(); ++i)
        {
            const auto& m = clip.members[i];
            const bool ok = slot >= 0 && (i == 0 ? loadPluginIntoFx (ctx, slot, m.description) : addPluginToChain (ctx, slot, m.description));
            if (! ok)
            {
                if (i == 0)
                {
                    s->removeBlock (uid);
                    rebuildTiles();
                    return false;
                }
                break;
            }
            if (auto* p = fx.getChainMember (slot, (int) i))
                p->setStateInformation (m.state.getData(), (int) m.state.getSize());
        }
    }
    reselect (uid);
    return true;
}

void RackPage::showAddMenu()
{
    juce::Component::SafePointer<RackPage> safe (this);
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeelFor (theme));
    auto browser = pluginTypesMenu (ctx, [safe] (const juce::PluginDescription& desc)
    {
        if (safe != nullptr)
            safe->addPlugin (desc);
    });
    menu.addSubMenu ("PLUG-IN (BROWSER)", browser, browser.getNumItems() > 0 && ! models::remoteHeld());
    menu.addItem ("Scan for plug-ins...", [this] { if (ctx.showPluginScanner) ctx.showPluginScanner(); });
    menu.addSeparator();
    for (auto kind : kBuiltIn)
        menu.addItem (stepName (ChainStep { ld::kStepBlock, (int) kind }), [safe, kind] { if (safe != nullptr) safe->addBlock (kind); });
    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetScreenArea (canvas->localAreaToGlobal (Canvas::itemBounds ((int) tiles.size())))
                            .withStandardItemHeight (sizes::kTouch)
                            .withMinimumWidth (260));
}

void RackPage::showTileMenu (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) tiles.size()))
        return;
    juce::Component::SafePointer<RackPage> safe (this);
    const auto info = infos[(size_t) index];
    const int uid = info.uid;
    // The tile is found again by its block when the menu answers: the chain may have changed meanwhile.
    const auto indexOf = [safe, uid]
    {
        if (safe != nullptr)
            for (int i = 0; i < (int) safe->tiles.size(); ++i)
                if (safe->tiles[(size_t) i].uid == uid)
                    return i;
        return -1;
    };
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeelFor (theme));
    menu.addItem (info.bypassed ? "Enable" : "Bypass", info.canBypass, false, [safe, indexOf, info]
    {
        if (const int i = indexOf(); i >= 0)
            safe->setTileBypassed (i, ! info.bypassed);
    });
    menu.addItem ("Remove", [safe, indexOf] { if (const int i = indexOf(); i >= 0) safe->removeTile (i); });
    menu.addSeparator();
    menu.addItem ("Copy", [safe, indexOf] { if (const int i = indexOf(); i >= 0) safe->copyTile (i); });
    menu.addItem ("Paste", clip.valid, false, [safe] { if (safe != nullptr) safe->pasteTile(); });
    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetScreenArea (canvas->localAreaToGlobal (Canvas::itemBounds (index)))
                            .withStandardItemHeight (sizes::kTouch)
                            .withMinimumWidth (220));
}

void RackPage::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    auto head = juce::Rectangle<int> (0, 0, kLeftWidth, kHead).reduced (sizes::kGapLarge, 0);
    g.setColour (theme.text);
    g.setFont (type::font (type::kHeading, true));
    juce::String name = "CH " + juce::String (channel + 1);
    if (auto* s = strip())
    {
        const auto own = s->getOwnName();
        if (own.isNotEmpty() && own != juce::String (channel + 1))
            name << "  " << own;
    }
    g.drawText (name, head.removeFromLeft (head.getWidth() / 2), juce::Justification::centredLeft);
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kCaption, true));
    g.drawText ("INPUT  >  CHAIN  >  OUTPUT      DRAG TO REORDER   -   HOLD FOR MENU", head, juce::Justification::centredRight);
    g.setColour (theme.outline);
    g.drawHorizontalLine (kHead - 1, 0.0f, (float) kLeftWidth);
}

void RackPage::resized()
{
    auto area = getLocalBounds();
    dock.setBounds (area.removeFromRight (area.getWidth() - kLeftWidth));
    viewport.setBounds (area.withTrimmedTop (kHead));
    canvas->setSize (juce::jmax (1, viewport.getWidth() - viewport.getScrollBarThickness()), juce::jmax (viewport.getHeight(), canvas->totalHeight()));
}

} // namespace livedaw::ui::ph
