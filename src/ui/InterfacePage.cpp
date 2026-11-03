#include "ui/InterfacePage.h"

#include "engine/MixerEngine.h"
#include "ui/LookAndFeel.h"
#include "ui/RoutingGroups.h"

namespace livedaw::ui
{

namespace
{
    constexpr int kRow = 40, kHeader = 64, kCaption = 36;

    juce::String settingsKey (const MixerEngine& engine)
    {
        return "interfacePatch." + juce::File::createLegalFileName (engine.getDeviceTypeName() + "." + engine.getDeviceName());
    }

    juce::String outputLabel (int port)
    {
        return port < 0 ? juce::String ("-")
                        : juce::String (routing::info (routing::groupOfOutput (port)).prefix) + " " + juce::String (routing::outputNumber (port));
    }

    /** A menu of the console's ports, in their groups (LOCAL, AES50 B, AES50 C); NONE first. */
    juce::PopupMenu portMenu (bool inputs, int current)
    {
        using routing::Group;
        juce::PopupMenu menu;
        menu.addItem (1, "NONE", true, current < 0);
        const std::vector<Group> groups { inputs ? Group::LocalIn : Group::LocalOut, Group::Aes50B, Group::Aes50C };
        for (auto g : groups)
        {
            juce::PopupMenu sub;
            bool holdsCurrent = false;
            for (int i = 0; i < routing::kBlock; ++i)
            {
                const int port = inputs ? routing::sourceAt (g, i) : routing::outputAt (g, i);
                sub.addItem (port + 2, inputs ? routing::sourceLabel (port) : outputLabel (port), true, port == current);
                holdsCurrent = holdsCurrent || port == current;
            }
            menu.addSubMenu (routing::info (g).name, sub, true, nullptr, holdsCurrent);
        }
        return menu;
    }
} // namespace

juce::String interfaceLabel (const MixerEngine& engine)
{
    const auto type = engine.getDeviceTypeName();
    if (type.isEmpty())
        return "INTERFACE";
    if (type.startsWithIgnoreCase ("Windows Audio"))
        return "WASAPI";
    if (type.equalsIgnoreCase ("CoreAudio"))
        return "CORE AUDIO";
    return type.toUpperCase();
}

void loadInterfacePatch (UiContext& ctx)
{
    auto& patch = ctx.engine.getInterfacePatch();
    const auto text = ctx.settings.getValue (settingsKey (ctx.engine));
    if (text.isEmpty())
    {
        patch.reset();
        return;
    }
    const auto xml = juce::parseXML (text);
    patch.fromXml (xml.get());
}

void saveInterfacePatch (UiContext& ctx)
{
    auto& patch = ctx.engine.getInterfacePatch();
    if (const auto xml = patch.toXml())
        ctx.settings.setValue (settingsKey (ctx.engine), xml->toString (juce::XmlElement::TextFormat().singleLine()));
    else
        ctx.settings.removeValue (settingsKey (ctx.engine));
}

//==============================================================================
/** A row per interface channel: its number and the driver's name for it, then the
    console port it feeds (inputs) or plays (outputs); a tap on the port opens the menu. */
class InterfacePage::Column final : public juce::Component
{
public:
    Column (InterfacePage& o, bool in) : owner (o), inputs (in) {}

    int numRows() const
    {
        const auto& names = inputs ? owner.ctx.engine.getHardwareInputNames() : owner.ctx.engine.getHardwareOutputNames();
        return juce::jmin (names.size(), InterfacePatch::kMaxChannels);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& names = inputs ? owner.ctx.engine.getHardwareInputNames() : owner.ctx.engine.getHardwareOutputNames();
        for (int i = 0; i < numRows(); ++i)
        {
            auto row = juce::Rectangle<int> (0, i * kRow, getWidth(), kRow).reduced (0, 2);
            g.setColour (Colours::screenDark);
            g.fillRect (row);
            auto number = row.removeFromLeft (52);
            g.setColour (Colours::caption);
            g.setFont (wingFontPx (14.0f, WingWeight::SemiBold));
            g.drawText (juce::String (i + 1), number, juce::Justification::centred, false);
            auto port = row.removeFromRight (juce::jmin (170, row.getWidth() / 2));
            g.setColour (Colours::textDim);
            g.setFont (wingFontPx (13.0f));
            g.drawFittedText (names[i], row.reduced (6, 0), juce::Justification::centredLeft, 1, 0.8f);
            const auto text = inputs ? owner.inputText (i) : owner.outputText (i);
            g.setColour (Colours::screenTile);
            g.fillRoundedRectangle (port.reduced (4, 3).toFloat(), 4.0f);
            g.setColour (text == "-" ? Colours::textDim : Colours::gold);
            g.setFont (wingFontPx (14.0f, WingWeight::SemiBold));
            g.drawFittedText ((inputs ? juce::String::fromUTF8 ("\xe2\x86\x92 ") : juce::String::fromUTF8 ("\xe2\x86\x90 ")) + text, port.reduced (10, 0),
                              juce::Justification::centredLeft, 1, 0.7f);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const int i = e.y / kRow;
        if (! juce::isPositiveAndBelow (i, numRows()) || e.x < getWidth() - juce::jmin (170, (getWidth() - 52) / 2))
            return;
        auto& patch = owner.ctx.engine.getInterfacePatch();
        const auto fed = patch.portsFedBy (i);
        const int current = inputs ? (fed.empty() ? -1 : fed.front()) : patch.outputSource (i);
        juce::Component::SafePointer<InterfacePage> safe (&owner);
        const bool in = inputs;
        portMenu (inputs, current).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withStandardItemHeight (36),
                                                  [safe, in, i] (int result)
                                                  {
                                                      if (safe == nullptr || result == 0)
                                                          return;
                                                      const int port = result == 1 ? -1 : result - 2;
                                                      if (in)
                                                          safe->patchInput (i, port);
                                                      else
                                                          safe->patchOutput (i, port);
                                                  });
    }

    void updateSize() { setSize (getWidth(), numRows() * kRow); }

private:
    InterfacePage& owner;
    const bool inputs;
};

//==============================================================================
InterfacePage::InterfacePage (UiContext& c) : ctx (c)
{
    inputs = std::make_unique<Column> (*this, true);
    outputs = std::make_unique<Column> (*this, false);
    for (auto [view, column] : { std::pair<juce::Viewport*, Column*> { &inputView, inputs.get() }, { &outputView, outputs.get() } })
    {
        view->setViewedComponent (column, false);
        view->setScrollBarsShown (true, false);
        view->setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::nonHover);
        addAndMakeVisible (*view);
    }
    setOpaque (true);
    startTimerHz (4);
}

InterfacePage::~InterfacePage() = default;

juce::String InterfacePage::inputText (int input) const
{
    juce::StringArray ports;
    for (int port : ctx.engine.getInterfacePatch().portsFedBy (input))
        ports.add (routing::sourceLabel (port));
    return ports.isEmpty() ? juce::String ("-") : ports.joinIntoString (", ");
}

juce::String InterfacePage::outputText (int output) const { return outputLabel (ctx.engine.getInterfacePatch().outputSource (output)); }

void InterfacePage::patchInput (int input, int port)
{
    auto& patch = ctx.engine.getInterfacePatch();
    for (int p : patch.portsFedBy (input))
        patch.setInputFor (p, -1);
    if (port >= 0)
        patch.setInputFor (port, input);
    changed();
}

void InterfacePage::patchOutput (int output, int port)
{
    ctx.engine.getInterfacePatch().setOutputSource (output, port);
    changed();
}

void InterfacePage::changed()
{
    saveInterfacePatch (ctx);
    ctx.log.info ("Interface patch changed (" + interfaceLabel (ctx.engine) + ")");
    repaint();
    inputs->repaint();
    outputs->repaint();
}

void InterfacePage::timerCallback()
{
    // Another device, or other channels: new rows.
    const auto now = interfaceLabel (ctx.engine) + ctx.engine.getDeviceName() + juce::String (ctx.engine.getHardwareInputNames().size()) + "/"
                     + juce::String (ctx.engine.getHardwareOutputNames().size()) + (ctx.engine.getInterfacePatch().isStraight() ? "S" : "P");
    if (now != shown)
    {
        shown = now;
        resized();
        repaint();
    }
}

void InterfacePage::resized()
{
    auto b = getLocalBounds().reduced (16, 8);
    header = b.removeFromTop (kHeader);
    straightKey = header.removeFromRight (180).reduced (0, 10);
    b.removeFromTop (8);
    auto left = b.removeFromLeft (b.getWidth() / 2 - 8);
    b.removeFromLeft (16);
    inputCaption = left.removeFromTop (kCaption);
    outputCaption = b.removeFromTop (kCaption);
    inputView.setBounds (left);
    outputView.setBounds (b);
    for (auto [view, column] : { std::pair<juce::Viewport*, Column*> { &inputView, inputs.get() }, { &outputView, outputs.get() } })
    {
        column->setSize (juce::jmax (0, view->getWidth() - view->getScrollBarThickness()), 0);
        column->updateSize();
    }
}

void InterfacePage::paint (juce::Graphics& g)
{
    g.fillAll (Colours::screen);
    auto h = header;
    g.setColour (Colours::gold);
    g.setFont (wingFontPx (24.0f, WingWeight::SemiBold));
    const auto label = interfaceLabel (ctx.engine);
    g.drawText (label, h.removeFromLeft ((int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label) + 20), juce::Justification::centredLeft, false);
    g.setColour (Colours::caption);
    g.setFont (wingFontPx (16.0f));
    const auto& patch = ctx.engine.getInterfacePatch();
    g.drawFittedText (ctx.engine.getDeviceName() + (patch.isStraight() ? "   straight through: input n is LCL n" : "   patched"), h,
                      juce::Justification::centredLeft, 1, 0.8f);

    g.setColour (patch.isStraight() ? Colours::screenTile : Colours::gold);
    g.fillRoundedRectangle (straightKey.toFloat(), 4.0f);
    g.setColour (patch.isStraight() ? Colours::textDim : juce::Colours::black);
    g.setFont (wingFontPx (15.0f, WingWeight::SemiBold));
    g.drawText ("STRAIGHT", straightKey, juce::Justification::centred, false);

    g.setColour (Colours::caption);
    g.setFont (wingFontPx (15.0f, WingWeight::SemiBold));
    g.drawText ("INPUTS  >  CONSOLE INPUTS", inputCaption, juce::Justification::centredLeft, false);
    g.drawText ("CONSOLE OUTPUTS  >  OUTPUTS", outputCaption, juce::Justification::centredLeft, false);
    if (ctx.engine.getHardwareInputNames().isEmpty() && ctx.engine.getHardwareOutputNames().isEmpty())
    {
        g.setColour (Colours::textDim);
        g.drawText ("No audio device: choose one on SETUP > SIDECAR.", inputView.getBounds(), juce::Justification::centred, false);
    }
}

void InterfacePage::mouseUp (const juce::MouseEvent& e)
{
    if (straightKey.contains (e.getPosition()) && ! ctx.engine.getInterfacePatch().isStraight())
    {
        ctx.engine.getInterfacePatch().reset();
        changed();
    }
}

} // namespace livedaw::ui
