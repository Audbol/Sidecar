#include "ui/universal/StripPanel.h"

#include "engine/AppPaths.h"
#include "engine/Show.h"
#include "ui/HomeScreens.h"
#include "ui/RoutingGroups.h"
#include "ui/ShowPanel.h"
#include "ui/StripCustomize.h"

namespace livedaw::ui::universal
{

namespace
{
    constexpr int kCaption = 24, kSwatch = 40, kNameRow = 56;
    /** The EXT IN and MIX SOURCE rows' height together. */
    constexpr int kMixRows = kCaption + 3 * sizes::kTouch + 2 * sizes::kGapSmall + sizes::kGap;
    /** A dLive's MixRack, DX and Surface sockets: what Ext In offers. */
    constexpr int kExtInSockets = 140, kExtTrimRange = 24;
    /** The AMM rows' height together. */
    constexpr int kAmmRows = kCaption + 3 * sizes::kTouch + 2 * sizes::kGapSmall + sizes::kGap;
    /** A channel preset holds every section but routing (as the WING's LIBRARY saves one). */
    constexpr juce::uint32 kPresetScope = state::AllSections & ~(juce::uint32) state::Routing;
} // namespace

StripPanel::StripPanel (UiContext& c, const Theme& t)
    : extIn (t), extTrim (t), mixSource (t), extPolarity ("POLARITY", t.accent, t), amm (t), ammOn ("ON", t.accent, t), ammChair ("CHAIR", t.accent, t), ammSolo ("SOLO", t.accent, t), ammPriority (t), renameKey ("RENAME", t.accent, t), savePresetKey ("SAVE PRESET", t.accent, t), loadPresetKey ("LOAD PRESET", t.accent, t), copyKey ("COPY", t.accent, t), pasteKey ("PASTE", t.accent, t), dcas (t, t.accent), muteGroups (t, t.mute), ctx (c), theme (t)
{
    renameKey.onClick = [this]
    {
        if (strip == nullptr)
            return;
        const auto own = strip->getName();
        askForText (*this, "RENAME", (linked ? "The source of " : juce::String()) + strip->getLabel() + ":", own,
                    [safe = juce::Component::SafePointer<StripPanel> (this)] (const juce::String& text)
                    {
                        if (safe != nullptr)
                            safe->rename (text);
                    });
    };
    // Channel presets, in the Library folder; no file dialogs in a test run.
    savePresetKey.onClick = [this]
    {
        if (strip == nullptr || paths::externalActionsBlocked().load())
            return;
        const auto folder = ShowPanel::libraryFolder();
        folder.createDirectory();
        const auto name = juce::File::createLegalFileName (strip->getName().isNotEmpty() ? strip->getName() : strip->getLabel());
        chooser = std::make_unique<juce::FileChooser> ("Save " + strip->getLabel() + " as a channel preset",
                                                       folder.getChildFile (name + Show::channelPresetExtension), juce::String ("*") + Show::channelPresetExtension);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::Component::SafePointer<StripPanel> (this)] (const juce::FileChooser& fc)
                              {
                                  if (safe != nullptr && fc.getResult() != juce::File())
                                      safe->savePreset (fc.getResult().withFileExtension (Show::channelPresetExtension));
                              });
    };
    loadPresetKey.onClick = [this]
    {
        if (strip == nullptr || paths::externalActionsBlocked().load())
            return;
        chooser = std::make_unique<juce::FileChooser> ("Load a channel preset onto " + strip->getLabel(), ShowPanel::libraryFolder(),
                                                       juce::String ("*") + Show::channelPresetExtension);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<StripPanel> (this)] (const juce::FileChooser& fc)
                              {
                                  if (safe != nullptr && fc.getResult() != juce::File())
                                      safe->loadPreset (fc.getResult());
                              });
    };
    dcas.count = [this] { return strip != nullptr ? ctx.engine.getNumDcas() : 0; };
    dcas.isOn = [this] (int i) { return strip != nullptr && strip->params.dcaMask.contains (i); };
    dcas.toggle = [this] (int i)
    {
        if (strip != nullptr)
            strip->params.dcaMask.toggle (i);
    };
    dcas.name = [this] (int i) { return customGroupName (ctx.engine.getDcaName (i), i); };
    muteGroups.count = [this] { return strip != nullptr ? ctx.engine.getNumMuteGroups() : 0; };
    muteGroups.isOn = [this] (int i) { return strip != nullptr && strip->params.muteGroupMask.contains (i); };
    muteGroups.toggle = [this] (int i)
    {
        if (strip != nullptr)
            strip->params.muteGroupMask.toggle (i);
    };
    muteGroups.name = [this] (int i) { return customGroupName (ctx.engine.getMuteGroupName (i), i); };
    extIn.caption = "EXT IN";
    extIn.items = []
    {
        juce::StringArray names { "OFF" };
        for (int i = 0; i < kExtInSockets; ++i)
            names.add (routing::sourceLabel (i));
        return names;
    };
    extIn.current = [this] { return strip != nullptr ? strip->mixInput().extSource.load() + 1 : 0; };
    extIn.choose = [this] (int i)
    {
        if (strip != nullptr)
            strip->mixInput().extSource = i - 1;
    };
    extTrim.caption = "EXT TRIM";
    extTrim.items = []
    {
        juce::StringArray names;
        for (int db = -kExtTrimRange; db <= kExtTrimRange; ++db)
            names.add ((db > 0 ? "+" : "") + juce::String (db) + " dB");
        return names;
    };
    extTrim.current = [this] { return strip != nullptr ? juce::roundToInt (strip->mixInput().extTrimDb.load()) + kExtTrimRange : kExtTrimRange; };
    extTrim.choose = [this] (int i)
    {
        if (strip != nullptr)
            strip->mixInput().extTrimDb = (float) (i - kExtTrimRange);
    };
    extPolarity.isOn = [this] { return strip != nullptr && strip->mixInput().extInvert.load(); };
    extPolarity.onClick = [this]
    {
        if (strip != nullptr)
            strip->mixInput().extInvert = ! strip->mixInput().extInvert.load();
    };
    mixSource.caption = "MIX SOURCE";
    mixSource.items = [] { return juce::StringArray { "POST PREAMP", "POST INSERT A", "POST PEQ", "POST DELAY" }; };
    mixSource.current = [this] { return strip != nullptr ? strip->mixInput().mixSource.load() : 3; };
    mixSource.choose = [this] (int i)
    {
        if (strip != nullptr)
            strip->mixInput().mixSource = i;
    };
    amm.caption = "AMM";
    amm.items = [this]
    {
        juce::StringArray names { "NONE" };
        for (int i = 0; i < ld::ld_engine_amm (ctx.engine.getHandle())->count.load(); ++i)
            names.add ("AMM " + juce::String (i + 1));
        return names;
    };
    amm.current = [this]
    {
        if (strip == nullptr)
            return 0;
        const int n = ld::ld_strip_amm (strip->getHandle())->amm.load();
        return n < ld::ld_engine_amm (ctx.engine.getHandle())->count.load() ? n + 1 : 0;
    };
    amm.choose = [this] (int i)
    {
        if (strip != nullptr)
            ld::ld_strip_amm (strip->getHandle())->amm = i - 1;
    };
    const auto ammFlag = [this] (ToggleKey& key, std::atomic<bool> ld::AmmStripParams::*flag)
    {
        key.isOn = [this, flag] { return strip != nullptr && (ld::ld_strip_amm (strip->getHandle())->*flag).load(); };
        key.onClick = [this, flag]
        {
            if (strip != nullptr)
            {
                auto& f = ld::ld_strip_amm (strip->getHandle())->*flag;
                f = ! f.load();
            }
        };
    };
    ammFlag (ammOn, &ld::AmmStripParams::on);
    ammFlag (ammChair, &ld::AmmStripParams::chair);
    ammFlag (ammSolo, &ld::AmmStripParams::solo);
    for (auto* child : std::initializer_list<juce::Component*> { &extIn, &extTrim, &extPolarity, &mixSource, &amm, &ammOn, &ammChair, &ammSolo, &ammPriority })
        addChildComponent (child);
    copyKey.onClick = [this] { copyStrip(); };
    copyKey.setTooltip ("Remember this strip's settings");
    pasteKey.setTooltip ("Paste the settings copied onto this strip");
    pasteKey.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.setLookAndFeel (&lookAndFeelFor (theme));
        const std::pair<juce::uint32, const char*> items[] = { { kCopyAll, "Everything" },         { kCopyPreamp, "Input and filter" }, { kCopyGate, "Gate" },
                                                               { kCopyEq, "EQ" },                  { kCopyDynamics, "Dynamics" },      { kCopySends, "Sends" },
                                                               { kCopyMix, "Fader, mute and pan" } };
        for (auto [bits, name] : items)
            menu.addItem ((int) bits, name);
        juce::Component::SafePointer<StripPanel> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pasteKey).withStandardItemHeight (sizes::kTouch - 8), [safe] (int bits)
        {
            if (safe != nullptr && bits != 0)
                safe->paste ((juce::uint32) bits);
        });
    };
    for (auto* child : std::initializer_list<juce::Component*> { &renameKey, &savePresetKey, &loadPresetKey, &copyKey, &pasteKey, &dcas, &muteGroups })
        addAndMakeVisible (child);
    startTimerHz (6);
}

bool StripPanel::showsMixInput() const
{
    return strip != nullptr && strip->isOutput() && ctx.engine.getProfile() == ld::kProfileDLive;
}

bool StripPanel::showsAmm() const
{
    return strip != nullptr && strip->getKind() == StripKind::Channel && ctx.engine.getProfile() == ld::kProfileDLive;
}

void StripPanel::setStrip (Strip* s)
{
    strip = s;
    name = {};
    colour = 0;
    const bool mix = showsMixInput();
    for (auto* child : std::initializer_list<juce::Component*> { &extIn, &extTrim, &extPolarity })
        child->setVisible (mix);
    mixSource.setVisible (mix && strip->getKind() == StripKind::Bus);
    const bool hasAmm = showsAmm();
    amm.setVisible (hasAmm);
    ammOn.setVisible (hasAmm);
    if (hasAmm)
        ammPriority.bind (ValueBar::linear ("PRIORITY", ld::ld_strip_amm (strip->getHandle())->priorityDb, -15.0f, 15.0f, 0.0f, " dB", 0));
    else
        ammPriority.unbind();
    shownAmmNom = ! shownAmmNom; // refresh() sets CHAIR, SOLO and PRIORITY for the mode
    refresh();
    resized();
    repaint();
}

void StripPanel::rename (const juce::String& text)
{
    if (strip == nullptr)
        return;
    customize::rename (ctx.engine, *strip, text);
    refresh();
}

juce::Result StripPanel::savePreset (const juce::File& file)
{
    if (strip == nullptr)
        return juce::Result::fail ("No strip");
    const auto r = ctx.show.saveChannelPreset (state::StripRef::of (*strip), kPresetScope, file, file.getFileNameWithoutExtension());
    if (r.failed())
        ctx.log.error (r.getErrorMessage());
    else
        ctx.log.info ("Saved channel preset " + file.getFileName());
    return r;
}

juce::Result StripPanel::loadPreset (const juce::File& file)
{
    if (strip == nullptr)
        return juce::Result::fail ("No strip");
    const auto r = ctx.show.loadChannelPreset (state::StripRef::of (*strip), file);
    if (r.failed())
        ctx.log.error (r.getErrorMessage());
    else
        ctx.log.info ("Loaded channel preset " + file.getFileName() + " onto " + strip->getLabel());
    refresh();
    return r;
}

void StripPanel::chooseColour (int c)
{
    if (strip == nullptr)
        return;
    customize::setColour (*strip, c);
    refresh();
}

void StripPanel::copyStrip()
{
    if (strip != nullptr)
        copied = state::StripRef::of (*strip);
    refresh();
}

void StripPanel::paste (juce::uint32 sections)
{
    auto* from = copied.has_value() ? state::findStrip (ctx.engine, *copied) : nullptr;
    if (from == nullptr || strip == nullptr || from == strip)
        return;
    copyStripSections (ctx.engine, *from, *strip, sections);
    ctx.log.info ("Pasted " + from->getLabel() + " onto " + strip->getLabel());
}

void StripPanel::refresh()
{
    dcas.refresh();
    muteGroups.refresh();
    // PASTE names what it holds; it can't go onto the strip it came from.
    auto* source = copied.has_value() ? state::findStrip (ctx.engine, *copied) : nullptr;
    pasteKey.setEnabled (source != nullptr && source != strip);
    pasteKey.setButtonText (source != nullptr ? "PASTE " + source->getLabel() : juce::String ("PASTE"));
    if (strip == nullptr)
        return;
    if (showsAmm())
    {
        const bool nom = ld::ld_engine_amm (ctx.engine.getHandle())->mode.load() == 0;
        if (nom != shownAmmNom)
        {
            shownAmmNom = nom;
            ammChair.setVisible (nom);
            ammSolo.setVisible (nom);
            ammPriority.setVisible (! nom);
        }
        amm.refresh();
        ammOn.refresh();
        ammChair.refresh();
        ammSolo.refresh();
        ammPriority.refresh();
        // The gain it's taking now, beside the caption.
        const float gain = ld::ld_strip_amm (strip->getHandle())->gainDb.load();
        if (std::abs (gain - shownAmmGain) > 0.05f)
        {
            shownAmmGain = gain;
            repaint (ammCaption);
        }
    }
    const auto newName = strip->getName();
    const int newColour = strip->getColourIndex();
    const bool newLinked = customize::goesToSource (*strip);
    if (newName != name || newColour != colour || newLinked != linked)
    {
        name = newName;
        colour = newColour;
        linked = newLinked;
        repaint();
    }
}

juce::Rectangle<int> StripPanel::swatchArea (int c) const
{
    if (! juce::isPositiveAndBelow (c - 1, palette::count()) || coloursArea.isEmpty())
        return {};
    const int cols = juce::jmax (1, coloursArea.getWidth() / kSwatch);
    const int w = coloursArea.getWidth() / cols;
    return { coloursArea.getX() + ((c - 1) % cols) * w, coloursArea.getY() + ((c - 1) / cols) * kSwatch, w, kSwatch };
}

void StripPanel::paint (juce::Graphics& g)
{
    if (strip == nullptr)
        return;
    auto caption = [&] (juce::Rectangle<int> r, const juce::String& text)
    {
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kCaption, true));
        g.drawText (text, r, juce::Justification::bottomLeft);
    };

    // NAME: the name shown, and whose it is while linked.
    auto n = nameArea;
    caption (n.removeFromTop (kCaption / 2 + 4), linked ? "NAME (THE SOURCE'S, LINKED)" : "NAME");
    g.setColour (theme.text);
    g.setFont (type::font (type::kHeading, true));
    g.drawFittedText (name.isNotEmpty() ? name : strip->getLabel(), n, juce::Justification::centredLeft, 1, 0.7f);

    if (! ammCaption.isEmpty())
        caption (ammCaption, "AUTOMATIC MIC MIXING   GAIN " + juce::String (shownAmmGain, 1) + " dB");
    caption (colourCaption, "COLOUR");
    for (int c = 1; c <= palette::count(); ++c)
    {
        const auto r = swatchArea (c).toFloat().reduced (3.0f);
        g.setColour (consoleColour (c));
        g.fillRoundedRectangle (r, sizes::kRadius);
        if (c == colour)
        {
            g.setColour (theme.text);
            g.drawRoundedRectangle (r.reduced (1.0f), sizes::kRadius, 3.0f);
        }
    }
    caption (dcaCaption, "DCAS (A TAP ASSIGNS OR UNASSIGNS)");
    caption (muteCaption, "MUTE GROUPS");
    caption (presetCaption, "CHANNEL PRESET (EVERY SECTION BUT ROUTING)");
}

int StripPanel::heightFor (int width) const
{
    const int w = width - 2 * sizes::kGap;
    const int cols = juce::jmax (1, w / kSwatch);
    const int rows = (palette::count() + cols - 1) / cols;
    return (showsMixInput() ? kMixRows : 0) + (showsAmm() ? kAmmRows : 0) + sizes::kGap + kNameRow + sizes::kGap + kCaption + rows * kSwatch + sizes::kGap + kCaption + dcas.heightFor (w) + sizes::kGap + kCaption
           + muteGroups.heightFor (w) + sizes::kGap + kCaption + 2 * sizes::kTouch + sizes::kGapSmall + sizes::kGap;
}

void StripPanel::resized()
{
    auto area = getLocalBounds().reduced (sizes::kGap, 0);
    area.removeFromTop (sizes::kGap);
    nameArea = area.removeFromTop (kNameRow);
    renameKey.setBounds (nameArea.removeFromRight (112).withSizeKeepingCentre (112, sizes::kTouch));
    area.removeFromTop (sizes::kGap);
    mixCaption = {};
    if (showsMixInput())
    {
        mixCaption = area.removeFromTop (kCaption);
        extIn.setBounds (area.removeFromTop (sizes::kTouch));
        area.removeFromTop (sizes::kGapSmall);
        auto trim = area.removeFromTop (sizes::kTouch);
        extPolarity.setBounds (trim.removeFromRight (96));
        trim.removeFromRight (sizes::kGapSmall);
        extTrim.setBounds (trim);
        area.removeFromTop (sizes::kGapSmall);
        mixSource.setBounds (area.removeFromTop (sizes::kTouch));
        area.removeFromTop (sizes::kGap);
    }
    ammCaption = {};
    if (showsAmm())
    {
        ammCaption = area.removeFromTop (kCaption);
        amm.setBounds (area.removeFromTop (sizes::kTouch));
        area.removeFromTop (sizes::kGapSmall);
        auto keys = area.removeFromTop (sizes::kTouch);
        const int keyW = (keys.getWidth() - 2 * sizes::kGapSmall) / 3;
        ammOn.setBounds (keys.removeFromLeft (keyW));
        keys.removeFromLeft (sizes::kGapSmall);
        ammChair.setBounds (keys.removeFromLeft (keyW));
        keys.removeFromLeft (sizes::kGapSmall);
        ammSolo.setBounds (keys);
        area.removeFromTop (sizes::kGapSmall);
        ammPriority.setBounds (area.removeFromTop (sizes::kTouch));
        area.removeFromTop (sizes::kGap);
    }
    colourCaption = area.removeFromTop (kCaption);
    const int cols = juce::jmax (1, area.getWidth() / kSwatch);
    const int rows = (palette::count() + cols - 1) / cols;
    coloursArea = area.removeFromTop (rows * kSwatch);
    area.removeFromTop (sizes::kGap);
    dcaCaption = area.removeFromTop (kCaption);
    dcas.setBounds (area.removeFromTop (dcas.heightFor (area.getWidth())));
    area.removeFromTop (sizes::kGap);
    muteCaption = area.removeFromTop (kCaption);
    muteGroups.setBounds (area.removeFromTop (muteGroups.heightFor (area.getWidth())));
    area.removeFromTop (sizes::kGap);
    presetCaption = area.removeFromTop (kCaption);
    auto keys = area.removeFromTop (sizes::kTouch);
    savePresetKey.setBounds (keys.removeFromLeft (keys.getWidth() / 2).withTrimmedRight (sizes::kGapSmall / 2));
    loadPresetKey.setBounds (keys.withTrimmedLeft (sizes::kGapSmall / 2));
    area.removeFromTop (sizes::kGapSmall);
    keys = area.removeFromTop (sizes::kTouch);
    copyKey.setBounds (keys.removeFromLeft (keys.getWidth() / 2).withTrimmedRight (sizes::kGapSmall / 2));
    pasteKey.setBounds (keys.withTrimmedLeft (sizes::kGapSmall / 2));
}

void StripPanel::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown())
        return;
    for (int c = 1; c <= palette::count(); ++c)
        if (swatchArea (c).contains (e.getPosition()))
        {
            chooseColour (c);
            return;
        }
}

} // namespace livedaw::ui::universal
