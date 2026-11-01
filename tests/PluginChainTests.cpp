#include "TestHelpers.h"
#include "engine/DirectOut.h"
#include "engine/Models.h"
#include "engine/PluginChain.h"
#include "engine/Show.h"
#include "sandbox/TestPlugins.h"
#include "ui/LookAndFeel.h"
#include "ui/PluginChainView.h"
#include "ui/PluginOverviewWindow.h"
#include "ui/PluginPictures.h"

#include <thread>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #undef small // (rpcndr.h)
#endif

using namespace livedaw;
using livedaw::sandbox::TestPluginFormat;
using livedaw::test::FakePlugin;
using livedaw::test::OfflineRig;
using livedaw::test::kCentre;

namespace
{
    /** y = x * mul + add (so the order of two matters), with a settable latency;
        counts its prepareToPlay calls. Its state is mul and add. */
    class AffinePlugin final : public juce::AudioPluginInstance
    {
    public:
        AffinePlugin (juce::String n, float m, float a)
            : juce::AudioPluginInstance (BusesProperties()
                                             .withInput ("In", juce::AudioChannelSet::stereo())
                                             .withOutput ("Out", juce::AudioChannelSet::stereo())),
              mul (m), add (a), name (std::move (n))
        {
        }

        std::atomic<float> mul, add;
        int prepareCalls = 0;

        void fillInPluginDescription (juce::PluginDescription& d) const override
        {
            d.name = name;
            d.pluginFormatName = "Fake";
            d.fileOrIdentifier = "affine-" + name;
            d.uniqueId = d.deprecatedUid = d.fileOrIdentifier.hashCode();
            d.numInputChannels = d.numOutputChannels = 2;
        }

        const juce::String getName() const override { return name; }
        void prepareToPlay (double, int) override { ++prepareCalls; }
        void releaseResources() override {}

        using juce::AudioPluginInstance::processBlock;

        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    buffer.setSample (ch, i, buffer.getSample (ch, i) * mul.load() + add.load());
        }

        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& data) override
        {
            const float v[] = { mul.load(), add.load() };
            data.replaceAll (v, sizeof (v));
        }

        void setStateInformation (const void* data, int size) override
        {
            if (size == 2 * (int) sizeof (float))
            {
                mul = static_cast<const float*> (data)[0];
                add = static_cast<const float*> (data)[1];
            }
        }

    private:
        juce::String name;
    };

    float processConstant (juce::AudioProcessor& p, float in)
    {
        juce::AudioBuffer<float> buffer (2, OfflineRig::kBlock);
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), in, buffer.getNumSamples());
        juce::MidiBuffer midi;
        p.processBlock (buffer, midi);
        return buffer.getSample (0, buffer.getNumSamples() - 1);
    }

    float param (juce::AudioProcessor& p, int index) { return p.getParameters()[index]->getValue(); }
} // namespace

/** FX slot plugin chains (PluginChain, FxSlots::addToChain...): processing order,
    latency and bypass, swaps that keep the running plugins, faults and shows. */
class PluginChainTests final : public juce::UnitTest
{
public:
    PluginChainTests() : juce::UnitTest ("Plugin chains", "LiveDAW") {}

    void runTest() override
    {
        beginTest ("A chain runs its plugins in order");
        {
            std::vector<PluginChain::MemberPtr> members;
            members.push_back (PluginChain::makeMember (std::make_shared<AffinePlugin> ("Double", 2.0f, 0.0f)));
            members.push_back (PluginChain::makeMember (std::make_shared<AffinePlugin> ("Offset", 1.0f, 0.5f)));
            PluginChain chain (members);
            chain.prepareToPlay (48000.0, OfflineRig::kBlock);
            expectWithinAbsoluteError (processConstant (chain, 0.25f), 1.0f, 1.0e-6f); // 0.25 * 2 + 0.5
            expectEquals (chain.getName(), juce::String ("Double + Offset"));

            PluginChain reversed ({ members[1], members[0] });
            reversed.prepareToPlay (48000.0, OfflineRig::kBlock);
            expectWithinAbsoluteError (processConstant (reversed, 0.25f), 1.5f, 1.0e-6f); // (0.25 + 0.5) * 2

            chain.setBypassed (0, true);
            expectWithinAbsoluteError (processConstant (chain, 0.25f), 0.75f, 1.0e-6f);
        }

        beginTest ("An FX slot runs its chain in order; edits keep the running plugins");
        {
            MixerEngine engine (0);
            OfflineRig rig (engine, 1, 2);
            engine.outputProtection = false; // levels above 0 dBFS
            test::setChannels (engine, 1);
            auto& strip = *engine.getChannels()[0];
            strip.params.preInsert.on = true;
            strip.params.preInsert.fx = 4;
            auto& fx = engine.getFx();
            rig.setInput (0, 0.25f);

            auto* a = new AffinePlugin ("Double", 2.0f, 0.0f);
            fx.setPlugin (4, std::unique_ptr<juce::AudioPluginInstance> (a));
            expect (fx.getChain (4) == nullptr); // a plugin on its own
            expectEquals (fx.getNumChainMembers (4), 1);
            rig.run (4);
            expectWithinAbsoluteError (rig.out (0), 0.5f * kCentre, 1.0e-5f);

            auto* b = new AffinePlugin ("Offset", 1.0f, 0.5f);
            expect (fx.addToChain (4, std::unique_ptr<juce::AudioPluginInstance> (b)));
            expect (fx.getChain (4) != nullptr);
            expect (fx.getChainMember (4, 0) == a && fx.getChainMember (4, 1) == b);
            expectEquals (fx.describe (4), juce::String ("FX 5: Double + Offset"));
            rig.run (4);
            expectWithinAbsoluteError (rig.out (0), 1.0f * kCentre, 1.0e-5f);

            expect (fx.moveInChain (4, 1, 0));
            expect (fx.getChainMember (4, 0) == b && fx.getChainMember (4, 1) == a);
            rig.run (4);
            expectWithinAbsoluteError (rig.out (0), 1.5f * kCentre, 1.0e-5f);
            engine.getGarbage().collectAllNow(); // the replaced chains go; their plugins stay
            expectEquals (a->prepareCalls, 1);    // never prepared again while running
            expectEquals (b->prepareCalls, 1);

            auto* c = new AffinePlugin ("Third", 1.0f, 0.0f);
            expect (fx.addToChain (4, std::unique_ptr<juce::AudioPluginInstance> (c), 1));
            expectEquals (fx.getPlugin (4)->getName(), juce::String ("Offset +2"));
            expect (fx.removeFromChain (4, 0));
            engine.getGarbage().collectAllNow();
            expectEquals (fx.getNumChainMembers (4), 2);
            expect (fx.getChainMember (4, 0) == c && fx.getChainMember (4, 1) == a);
            rig.run (4);
            expectWithinAbsoluteError (rig.out (0), 0.5f * kCentre, 1.0e-5f);

            // Full at eight.
            for (int i = fx.getNumChainMembers (4); i < PluginChain::kMaxMembers; ++i)
                expect (fx.addToChain (4, std::make_unique<AffinePlugin> ("Unity", 1.0f, 0.0f)));
            expect (! fx.addToChain (4, std::make_unique<AffinePlugin> ("Ninth", 1.0f, 0.0f)));
            expect (! fx.addToChain (5, std::make_unique<AffinePlugin> ("Nothing to add to", 1.0f, 0.0f)));

            while (fx.getNumChainMembers (4) > 0)
                fx.removeFromChain (4, 0);
            expect (! fx.hasPlugin (4)); // the last one empties the slot
            engine.getGarbage().collectAllNow();
        }

        beginTest ("More FX slots than the WING's: a plugin in FX 40 runs, and keeps 40 slots in use");
        {
            MixerEngine engine (0);
            OfflineRig rig (engine, 1, 2);
            engine.outputProtection = false;
            test::setChannels (engine, 1);
            auto& fx = engine.getFx();
            expectEquals (fx.getNumSlots(), kWingFx);
            expectEquals (fx.setNumSlots (48), 48);
            expectEquals (fx.setNumSlots (1000), kMaxFx);

            auto& strip = *engine.getChannels()[0];
            strip.params.preInsert.on = true;
            strip.params.preInsert.fx = 39;
            fx.setPlugin (39, std::make_unique<AffinePlugin> ("Double", 2.0f, 0.0f));
            expect (fx.hasPlugin (39));
            expectEquals (fx.describe (39), juce::String ("FX 40: Double"));
            rig.setInput (0, 0.25f);
            rig.run (4);
            expectWithinAbsoluteError (rig.out (0), 0.5f * kCentre, 1.0e-5f);

            expect (fx.isInUse (39));
            expectEquals (fx.setNumSlots (kWingFx), 40);
            fx.setPlugin (39, nullptr);
            engine.getGarbage().collectAllNow();
            expectEquals (fx.setNumSlots (kWingFx), 40); // the insert still points at it
            strip.params.preInsert.fx = -1;
            expectEquals (fx.setNumSlots (kWingFx), kWingFx);
        }

        beginTest ("The latency is the sum of the members that aren't bypassed");
        {
            MixerEngine engine (0);
            OfflineRig rig (engine, 1, 2);
            auto& fx = engine.getFx();
            auto* a = new AffinePlugin ("A", 1.0f, 0.0f);
            auto* b = new AffinePlugin ("B", 1.0f, 0.0f);
            auto* c = new AffinePlugin ("C", 1.0f, 0.0f);
            a->setLatencySamples (10);
            b->setLatencySamples (20);
            c->setLatencySamples (30);
            fx.setPlugin (1, std::unique_ptr<juce::AudioPluginInstance> (a));
            expectEquals (fx.getLatency (1), 10);
            fx.addToChain (1, std::unique_ptr<juce::AudioPluginInstance> (b));
            fx.addToChain (1, std::unique_ptr<juce::AudioPluginInstance> (c));
            expectEquals (fx.getLatency (1), 60);

            fx.setChainMemberBypassed (1, 1, true);
            expect (fx.isChainMemberBypassed (1, 1));
            expectEquals (fx.getLatency (1), 40);
            a->setLatencySamples (25); // a member's change is followed at once
            expectEquals (fx.getLatency (1), 55);
            b->setLatencySamples (5); // bypassed: no effect
            expectEquals (fx.getLatency (1), 55);
            fx.setChainMemberBypassed (1, 1, false);
            expectEquals (fx.getLatency (1), 60);

            // Bypassing a plugin on its own makes it a chain of one.
            fx.setPlugin (2, std::make_unique<AffinePlugin> ("Solo", 1.0f, 0.0f));
            fx.getPlugin (2)->setLatencySamples (64);
            expectEquals (fx.getLatency (2), 64);
            fx.setChainMemberBypassed (2, 0, true);
            expect (fx.getChain (2) != nullptr);
            expectEquals (fx.getLatency (2), 0);
            fx.setPlugin (1, nullptr);
            fx.setPlugin (2, nullptr);
            engine.getGarbage().collectAllNow();
        }

        beginTest ("A plugin that fails in a chain bypasses only itself; the rest keeps running");
        {
            for (auto mode : { FakePlugin::Mode::Throw, FakePlugin::Mode::EmitNaN })
            {
                MixerEngine engine (0);
                OfflineRig rig (engine, 1, 2);
                engine.outputProtection = false;
                test::setChannels (engine, 1);
                auto& strip = *engine.getChannels()[0];
                strip.params.preInsert.on = true;
                strip.params.preInsert.fx = 0;
                auto& fx = engine.getFx();
                juce::StringArray warnings;
                engine.onPluginWarning = [&warnings] (const juce::String& text) { warnings.add (text); };

                fx.setPlugin (0, std::make_unique<FakePlugin> (0.5f));
                auto* bad = new FakePlugin (1.0f, FakePlugin::Mode::Gain, "bad");
                bad->setLatencySamples (100);
                fx.addToChain (0, std::unique_ptr<juce::AudioPluginInstance> (bad));
                fx.addToChain (0, std::make_unique<FakePlugin> (2.0f));
                rig.setInput (0, 0.25f);
                rig.run();
                expectEquals (fx.getLatency (0), 100);

                bad->mode = mode;
                rig.run (4);
                expect (! fx.isFaulted (0), "the slot faulted");
                expect (fx.isChainMemberFaulted (0, 1));
                expect (! fx.isChainMemberFaulted (0, 0) && ! fx.isChainMemberFaulted (0, 2));
                expectEquals (fx.getNumFaultedChainMembers (0), 1);
                expectWithinAbsoluteError (rig.out (0), 0.25f * 0.5f * 2.0f * kCentre, 1.0e-5f); // through the other two

                // Named once in the log; a faulted plugin counts as bypassed for the latency.
                engine.pollPlugins();
                engine.pollPlugins();
                expectEquals (warnings.size(), 1);
                const auto what = mode == FakePlugin::Mode::Throw ? juce::String ("threw an exception") : juce::String ("output NaN/Inf");
                expectEquals (warnings[0], "FX 1: Fake bad (plugin 2 of 3) " + what + "; bypassed, the rest of the chain keeps running");
                expectEquals (fx.getLatency (0), 0);
                expect (! fx.getChain (0)->createXml()->toString().containsIgnoreCase ("fault")); // not saved

                // CLEAR tries it again: still failing, it faults again (and is reported again).
                fx.clearChainMemberFault (0, 1);
                expect (! fx.isChainMemberFaulted (0, 1));
                rig.run (2);
                expect (fx.isChainMemberFaulted (0, 1));
                engine.pollPlugins();
                expectEquals (warnings.size(), 2);

                // Fixed: CLEAR brings it back.
                bad->mode = FakePlugin::Mode::Gain;
                bad->gain = 3.0f;
                fx.clearChainMemberFault (0, 1);
                rig.run (2);
                expect (! fx.isChainMemberFaulted (0, 1));
                expectWithinAbsoluteError (rig.out (0), 0.25f * 0.5f * 3.0f * 2.0f * kCentre, 1.0e-5f);
                engine.pollPlugins();
                expectEquals (fx.getLatency (0), 100);

                // BYP on or off clears a fault too, and the slot's CLEAR clears its plugins'.
                bad->mode = mode;
                rig.run (2);
                expect (fx.isChainMemberFaulted (0, 1));
                fx.setChainMemberBypassed (0, 1, true);
                expect (! fx.isChainMemberFaulted (0, 1) && fx.isChainMemberBypassed (0, 1));
                fx.setChainMemberBypassed (0, 1, false);
                rig.run (2);
                expect (fx.isChainMemberFaulted (0, 1));
                fx.clearFlags (0);
                expect (! fx.isChainMemberFaulted (0, 1));

                engine.onPluginWarning = nullptr;
                fx.setPlugin (0, nullptr);
                engine.getGarbage().collectAllNow();
            }
        }

        beginTest ("A chain of one that fails auto-bypasses the slot, as a plugin on its own");
        {
            MixerEngine engine (0);
            OfflineRig rig (engine, 1, 2);
            test::setChannels (engine, 1);
            auto& strip = *engine.getChannels()[0];
            strip.params.preInsert.on = true;
            strip.params.preInsert.fx = 0;
            auto& fx = engine.getFx();
            auto* bad = new FakePlugin (1.0f, FakePlugin::Mode::Gain, "bad");
            fx.setPlugin (0, std::unique_ptr<juce::AudioPluginInstance> (bad));
            fx.setChainMemberBypassed (0, 0, true); // a chain of one
            fx.setChainMemberBypassed (0, 0, false);
            expect (fx.getChain (0) != nullptr);
            bad->mode = FakePlugin::Mode::EmitNaN;
            rig.setInput (0, 0.5f);
            rig.run();

            expect (fx.isFaulted (0));
            expect (! fx.isChainMemberFaulted (0, 0));
            expectWithinAbsoluteError (rig.out (0), 0.5f * kCentre, 1.0e-5f); // dry signal passes
            const auto fault = fx.getChain (0)->takeFault();
            expectEquals (fault.member, 0);
            expect (fault.kind == PluginChain::FaultKind::nonFinite);
            expectEquals (fx.getChain (0)->takeFault().member, -1); // reported once
            fx.setPlugin (0, nullptr);
            engine.getGarbage().collectAllNow();
        }

        beginTest ("Editing a faulted slot's chain (zoom, collapse, add, move) keeps it bypassed until cleared");
        {
            MixerEngine engine (0);
            OfflineRig rig (engine, 1, 2);
            test::setChannels (engine, 1);
            auto& strip = *engine.getChannels()[0];
            strip.params.preInsert.on = true;
            strip.params.preInsert.fx = 0;
            auto& fx = engine.getFx();
            auto* bad = new FakePlugin (1.0f, FakePlugin::Mode::Throw, "bad");
            fx.setPlugin (0, std::unique_ptr<juce::AudioPluginInstance> (bad)); // a plugin on its own
            rig.setInput (0, 0.5f);
            rig.run();
            expect (fx.isFaulted (0));

            // Collapsing its card in the plugin window makes it a chain of one.
            fx.setChainMemberView (0, 0, { 100, true });
            expect (fx.getChain (0) != nullptr);
            expect (fx.isFaulted (0), "a view change cleared the slot's fault");
            fx.setChainMemberView (0, 0, { 67, true });
            expect (fx.isFaulted (0));

            expect (fx.addToChain (0, std::make_unique<FakePlugin> (2.0f)));
            expect (fx.isFaulted (0), "adding a plugin cleared the slot's fault");
            expect (fx.moveInChain (0, 1, 0));
            expect (fx.isFaulted (0), "moving a plugin cleared the slot's fault");
            bad->mode = FakePlugin::Mode::Gain;
            rig.run (2);
            expectWithinAbsoluteError (rig.out (0), 0.5f * kCentre, 1.0e-5f); // still bypassed: dry

            // Only clearing the fault runs the slot again.
            fx.clearFlags (0);
            rig.run (2);
            expect (! fx.isFaulted (0));
            expectWithinAbsoluteError (rig.out (0), 0.5f * 2.0f * kCentre, 1.0e-5f);

            // A new plugin (not a rearrangement) starts unfaulted, as before.
            fx.setPlugin (0, std::make_unique<FakePlugin> (1.0f, FakePlugin::Mode::Throw, "bad"));
            rig.run();
            expect (fx.isFaulted (0));
            fx.setPlugin (0, std::make_unique<FakePlugin> (1.0f));
            expect (! fx.isFaulted (0));
            fx.setPlugin (0, nullptr);
            engine.getGarbage().collectAllNow();
        }

        beginTest ("Zoom steps and FIT");
        {
            using V = ui::PluginChainView;
            expectEquals (V::nextZoom (100, -1), 85);
            expectEquals (V::nextZoom (85, -1), 75);
            expectEquals (V::nextZoom (67, -1), 50);
            expectEquals (V::nextZoom (50, -1), 50);
            expectEquals (V::nextZoom (67, 1), 75);
            expectEquals (V::nextZoom (100, 1), 100); // no growing past 100 %
            expectEquals (PluginChain::snapZoom (70), 67);
            expectEquals (PluginChain::snapZoom (130), 100);
            expectEquals (V::fitZoom (400, 500), 100);
            expectEquals (V::fitZoom (400, 340), 85);
            expectEquals (V::fitZoom (400, 300), 75);
            expectEquals (V::fitZoom (400, 299), 67);
            expectEquals (V::fitZoom (400, 100), 50); // the smallest step if none fits
        }

        beginTest ("A 3-plugin chain survives a show save and load, and scenes recall it");
        {
            PluginHost host (nullptr);
            host.getFormatManager().addFormat (std::make_unique<TestPluginFormat>());
            MixerEngine engine (0);
            OfflineRig rig (engine, 1, 2);
            Show show (engine, host);
            auto& fx = engine.getFx();

            const float gains[] = { 0.2f, 0.4f, 0.6f };
            auto withGain = [] (float g)
            {
                auto p = TestPluginFormat::create (TestPluginFormat::Kind::gain);
                p->getParameters()[0]->setValue (g);
                return p;
            };
            fx.setPlugin (6, withGain (gains[0]));
            fx.addToChain (6, withGain (gains[1]));
            fx.addToChain (6, withGain (gains[2]));
            fx.setChainMemberBypassed (6, 1, true);
            fx.setChainMemberView (6, 0, { 67, false });
            fx.setChainMemberView (6, 2, { 100, true });
            const auto xml = show.toXml();
            expect (xml->toString().contains (PluginChain::kXmlTag));

            show.addScene ("Chain");
            fx.getChainMember (6, 2)->getParameters()[0]->setValue (0.9f);
            fx.setChainMemberBypassed (6, 1, false);
            expectEquals ((int) show.recallScene (0).warnings.size(), 0);
            expectWithinAbsoluteError (param (*fx.getChainMember (6, 2), 0), gains[2], 1.0e-6f);
            expect (fx.isChainMemberBypassed (6, 1));

            MixerEngine engine2 (0);
            OfflineRig rig2 (engine2, 1, 2);
            Show loaded (engine2, host);
            const auto result = loaded.loadFromXml (*xml);
            expect (result.wasOk(), result.getErrorMessage());
            auto& fx2 = engine2.getFx();
            expect (fx2.getChain (6) != nullptr);
            expectEquals (fx2.getNumChainMembers (6), 3);
            for (int i = 0; i < fx2.getNumChainMembers (6); ++i)
            {
                expectEquals (fx2.getChainMember (6, i)->getName(), juce::String ("Sidecar Test gain"));
                expectWithinAbsoluteError (param (*fx2.getChainMember (6, i), 0), gains[i], 1.0e-6f);
                expect (fx2.isChainMemberBypassed (6, i) == (i == 1));
            }
            expectEquals (fx2.getChainMemberView (6, 0).zoom, 67);
            expect (! fx2.getChainMemberView (6, 0).collapsed);
            expect (fx2.getChainMemberView (6, 1).isDefault());
            expect (fx2.getChainMemberView (6, 2).collapsed && fx2.getChainMemberView (6, 2).zoom == 100);

            // Chains saved before zoom and collapse read with the defaults.
            auto saved = fx.getChain (6)->createXml();
            expect (saved->getChildElement (0)->getIntAttribute ("zoom") == 67 && saved->getChildElement (2)->getBoolAttribute ("collapsed"));
            for (auto* member : saved->getChildIterator())
            {
                member->removeAttribute ("zoom");
                member->removeAttribute ("collapsed");
            }
            std::vector<PluginChain::Saved> members;
            expect (PluginChain::parseXml (*saved, members));
            expectEquals ((int) members.size(), 3);
            for (auto& m : members)
                expect (m.view.isDefault());
            expectEquals (fx2.getPlugin (6)->getName(), juce::String ("Sidecar Test gain +2"));

            // A different chain isn't recalled into.
            fx2.moveInChain (6, 0, 2);
            loaded.addScene ("Moved");
            fx2.removeFromChain (6, 0);
            expectEquals ((int) loaded.recallScene (0).warnings.size(), 1);

            // Safe mode: no plugins.
            MixerEngine engine3 (0);
            Show safe (engine3, host);
            expect (safe.loadFromXml (*xml, false).wasOk());
            expect (! engine3.getFx().hasPlugin (6));

            fx.setPlugin (6, nullptr);
            fx2.setPlugin (6, nullptr);
            engine.getGarbage().collectAllNow();
            engine2.getGarbage().collectAllNow();
        }

        beginTest ("A plugin on its own (and a chain of one) is saved as before; such shows load unchanged");
        {
            PluginHost host (nullptr);
            host.getFormatManager().addFormat (std::make_unique<TestPluginFormat>());
            MixerEngine engine (0);
            OfflineRig rig (engine, 1, 2);
            Show show (engine, host);
            auto& fx = engine.getFx();

            auto plugin = TestPluginFormat::create (TestPluginFormat::Kind::gain);
            plugin->getParameters()[0]->setValue (0.3f);
            fx.setPlugin (3, std::move (plugin));
            const auto single = show.toXml();
            const auto text = single->toString();
            expect (text.contains ("<PLUGIN") && ! text.contains (PluginChain::kXmlTag));

            // Grown to two and back to one: still the single-plugin format.
            fx.addToChain (3, TestPluginFormat::create (TestPluginFormat::Kind::gain));
            fx.removeFromChain (3, 1);
            expect (fx.getChain (3) != nullptr);
            expect (! show.toXmlString().contains (PluginChain::kXmlTag));
            // ... unless its window view isn't the default.
            fx.setChainMemberView (3, 0, { 75, false });
            expect (show.toXmlString().contains (PluginChain::kXmlTag));
            fx.setChainMemberView (3, 0, {});
            expect (! show.toXmlString().contains (PluginChain::kXmlTag));

            MixerEngine engine2 (0);
            OfflineRig rig2 (engine2, 1, 2);
            Show loaded (engine2, host);
            expect (loaded.loadFromXml (*single).wasOk());
            auto& fx2 = engine2.getFx();
            expect (fx2.hasPlugin (3));
            expect (fx2.getChain (3) == nullptr); // a plugin on its own, as before
            expectWithinAbsoluteError (param (*fx2.getPlugin (3), 0), 0.3f, 1.0e-6f);

            fx.setPlugin (3, nullptr);
            fx2.setPlugin (3, nullptr);
            engine.getGarbage().collectAllNow();
            engine2.getGarbage().collectAllNow();
        }

        beginTest ("Scenes and snippets recall the plugin window's zoom and collapse; older scenes keep the current view");
        {
            PluginHost host (nullptr);
            host.getFormatManager().addFormat (std::make_unique<TestPluginFormat>());
            MixerEngine engine (0);
            OfflineRig rig (engine, 1, 2);
            Show show (engine, host);
            auto& fx = engine.getFx();

            // FX 5 a plugin on its own (the single-plugin format while its view is the default), FX 6 a chain of two.
            fx.setPlugin (4, TestPluginFormat::create (TestPluginFormat::Kind::gain));
            fx.setPlugin (5, TestPluginFormat::create (TestPluginFormat::Kind::gain));
            fx.addToChain (5, TestPluginFormat::create (TestPluginFormat::Kind::gain));
            fx.setChainMemberView (5, 0, { 67, false });
            fx.setChainMemberView (5, 1, { 100, true });
            show.addScene ("Zoomed");
            expect (show.toXmlString().contains (PluginChain::kDefaultViewAttribute));
            fx.setChainMemberView (5, 0, {});
            fx.setChainMemberView (5, 1, { 50, false });
            fx.setChainMemberView (4, 0, { 75, true });
            show.addScene ("Default and 50");

            expectEquals ((int) show.recallScene (0).warnings.size(), 0);
            expectEquals (fx.getChainMemberView (5, 0).zoom, 67);
            expect (! fx.getChainMemberView (5, 0).collapsed);
            expect (fx.getChainMemberView (5, 1).collapsed && fx.getChainMemberView (5, 1).zoom == 100);
            expect (fx.getChainMemberView (4, 0).isDefault(), "the plugin on its own kept its view");
            expectEquals ((int) show.recallScene (1).warnings.size(), 0);
            expect (fx.getChainMemberView (5, 0).isDefault(), "a default view stored in the scene isn't recalled");
            expectEquals (fx.getChainMemberView (5, 1).zoom, 50);
            expect (! fx.getChainMemberView (5, 1).collapsed);
            expect (fx.getChainMemberView (4, 0).collapsed && fx.getChainMemberView (4, 0).zoom == 75);

            // A snippet that recalls the slot's plugin state recalls its view as well.
            fx.setChainMemberView (5, 0, { 85, false });
            auto scope = state::Scope::none();
            scope.sections = state::Inserts;
            scope.fx = 1u << 5;
            const int snippet = show.addSnippet (0, "FX 6", scope);
            expect (snippet >= 0);
            fx.setChainMemberView (5, 0, { 50, true });
            expectEquals ((int) show.recallSnippet (0, snippet).warnings.size(), 0);
            expectEquals (fx.getChainMemberView (5, 0).zoom, 85);
            expect (! fx.getChainMemberView (5, 0).collapsed);

            // Scenes from before views were stored: the current view stays, except
            // where the chain stored a view other than the default (as it always did).
            auto old = show.toXml();
            std::function<void (juce::XmlElement&)> forget = [&forget] (juce::XmlElement& e)
            {
                e.removeAttribute ("views");
                e.removeAttribute (PluginChain::kDefaultViewAttribute);
                for (auto* child : e.getChildIterator())
                    forget (*child);
            };
            forget (*old);
            expect (! old->toString().contains (PluginChain::kDefaultViewAttribute) && ! old->toString().contains ("views="));

            MixerEngine engine2 (0);
            OfflineRig rig2 (engine2, 1, 2);
            Show loaded (engine2, host);
            expect (loaded.loadFromXml (*old).wasOk());
            auto& fx2 = engine2.getFx();
            expectEquals (fx2.getNumChainMembers (5), 2);
            fx2.setChainMemberView (5, 0, { 75, true });
            fx2.setChainMemberView (4, 0, { 67, false });
            expectEquals ((int) loaded.recallScene (0).warnings.size(), 0);
            expectEquals (fx2.getChainMemberView (5, 0).zoom, 67);
            expectEquals (fx2.getChainMemberView (4, 0).zoom, 67); // the plugin on its own: no view stored
            expectEquals ((int) loaded.recallScene (1).warnings.size(), 0);
            expectEquals (fx2.getChainMemberView (5, 1).zoom, 50);
            fx2.setChainMemberView (5, 0, { 75, true });
            expectEquals ((int) loaded.recallScene (1).warnings.size(), 0);
            expect (fx2.getChainMemberView (5, 0).zoom == 75 && fx2.getChainMemberView (5, 0).collapsed); // stored at the default: unknown then

            for (auto* f : { &fx, &fx2 })
                for (int slot : { 4, 5 })
                    f->setPlugin (slot, nullptr);
            engine.getGarbage().collectAllNow();
            engine2.getGarbage().collectAllNow();
        }

        beginTest ("The plugin window: chain bar, cards, zoom, collapse, faults, ROW and FOCUS");
        {
            using V = ui::PluginChainView;
            const auto snapshots = juce::SystemStats::getEnvironmentVariable ("SIDECAR_CHAIN_SNAPSHOT_DIR", {});
            std::unique_ptr<ui::ConsoleLookAndFeel> look;
            if (snapshots.isNotEmpty())
            {
                look = std::make_unique<ui::ConsoleLookAndFeel>();
                juce::LookAndFeel::setDefaultLookAndFeel (look.get());
            }

            MixerEngine engine (0);
            OfflineRig rig (engine, 1, 2);
            engine.outputProtection = false;
            test::setChannels (engine, 1);
            auto& strip = *engine.getChannels()[0];
            strip.params.preInsert.on = true;
            strip.params.preInsert.fx = 0;
            auto& fx = engine.getFx();
            fx.setPlugin (0, TestPluginFormat::create (TestPluginFormat::Kind::gain));
            int adds = 0;
            auto layoutSaved = V::Layout::row;
            {
                V view (fx, 0, { [&adds] (juce::Component&) { ++adds; }, {}, {}, [&layoutSaved] (V::Layout l) { layoutSaved = l; } });
                view.setMaxSize ({ 4000, 2000 }); // not the screen's: CI displays may be small
                expectEquals (view.getNumCards(), 1);
                expect (view.shows (fx.getPlugin (0)));
                const int single = view.getWidth();
                expect (single >= V::kMinCardWidth);
                expect (view.getZoomMethod (0) == V::ZoomMethod::transform); // generic controls

                fx.addToChain (0, TestPluginFormat::create (TestPluginFormat::Kind::crash));
                auto* nan = new FakePlugin (1.0f, FakePlugin::Mode::Gain, "nan"); // no editor: generic controls
                fx.addToChain (0, std::unique_ptr<juce::AudioPluginInstance> (nan));
                view.refresh();
                expectEquals (view.getNumCards(), 3);
                expect (view.shows (fx.getPlugin (0)) && view.shows (fx.getChainMember (0, 2)));
                expect (view.getWidth() > single); // grows to fit
                const int full0 = view.getEditorBounds (0).getHeight(), full2 = view.getEditorBounds (2).getHeight();

                // Zoom: a generic editor is scaled with a transform.
                const auto full = view.getEditorBounds (0);
                view.setZoom (0, 67);
                expectEquals (fx.getChainMemberView (0, 0).zoom, 67);
                const auto small = view.getEditorBounds (0);
                expectWithinAbsoluteError ((float) small.getWidth(), (float) full.getWidth() * 0.67f, 1.5f);
                expectWithinAbsoluteError ((float) small.getHeight(), (float) full.getHeight() * 0.67f, 1.5f);
                expectEquals (small.getY(), V::kTitleHeight);
                view.stepZoom (0, 1);
                expectEquals (fx.getChainMemberView (0, 0).zoom, 75);
                view.stepZoom (0, -1);
                expectEquals (fx.getChainMemberView (0, 0).zoom, 67);

                // Collapse: a thin column in ROW, the others move up close.
                const int before = view.getCardBounds (2).getX();
                view.setCollapsed (1, true);
                expect (fx.getChainMemberView (0, 1).collapsed);
                expectEquals (view.getCardBounds (1).getWidth(), V::kCollapsedWidth);
                expect (view.getEditorBounds (1).isEmpty());
                expect (view.getCardBounds (2).getX() < before);

                // A faulted plugin (the rest of the chain runs).
                rig.setInput (0, 0.25f);
                nan->mode = FakePlugin::Mode::EmitNaN;
                rig.run (2);
                expect (fx.isChainMemberFaulted (0, 2) && ! fx.isFaulted (0));
                view.refresh();

                auto snapshot = [&] (const juce::String& fileName)
                {
                    if (snapshots.isEmpty())
                        return;
                    juce::File file (juce::File (snapshots).getChildFile (fileName));
                    file.deleteFile();
                    juce::FileOutputStream out (file);
                    juce::PNGImageFormat().writeImageToStream (view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f), out);
                    logMessage ("Saved " + file.getFullPathName());
                };
                snapshot ("chain_row.png");

                // FOCUS: one card at a time, the chips as tabs.
                view.setLayout (V::Layout::focus);
                expect (layoutSaved == V::Layout::focus);
                view.select (2);
                expectEquals (view.getSelected(), 2);
                expect (view.isCardVisible (2) && ! view.isCardVisible (0) && ! view.isCardVisible (1));
                snapshot ("chain_focus.png");
                view.select (0);
                expect (view.isCardVisible (0) && ! view.isCardVisible (2));
                view.setLayout (V::Layout::row);
                expect (view.isCardVisible (0) && view.isCardVisible (2));

                // FIT: the largest steps that fit the window's height (collapsed ones stay).
                view.setSize (view.getWidth(), V::kBarHeight + V::kTitleHeight + 100);
                view.fitToHeight();
                expectEquals (fx.getChainMemberView (0, 0).zoom, V::fitZoom (full0, 100));
                expectEquals (fx.getChainMemberView (0, 2).zoom, V::fitZoom (full2, 100));
                expect (fx.getChainMemberView (0, 1).collapsed && fx.getChainMemberView (0, 1).zoom == 100);

                // Beyond the size limit it scrolls.
                view.setMaxSize ({ 4000, 2000 });
                const auto natural = view.getBounds();
                view.setMaxSize ({ 500, 2000 });
                expectEquals (view.getWidth(), 500);
                view.setMaxSize ({ 4000, 2000 });
                expect (view.getBounds() == natural);

                // A removed plugin's editor goes before the plugin.
                view.dropEditorFor (fx.getChainMember (0, 2));
                fx.removeFromChain (0, 2);
                expectEquals (view.getNumCards(), 2);
                view.refresh();
                expectEquals (view.getNumCards(), 2);
                fx.setPlugin (0, nullptr);
                view.refresh();
                expectEquals (view.getNumCards(), 0);
            }
            engine.getGarbage().collectAllNow();
            if (look != nullptr)
                juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        }
    }
};

static PluginChainTests pluginChainTests;

//==============================================================================
namespace
{
    /** A plugin whose editor behaves like a real plugin's native one: it starts
        tiny and takes its real size later (a VST3 view on Linux, once attached),
        follows the host scale factor, and (`snapsBack`) goes back to its size
        after the host resizes it smaller (an LV2 UI with a minimum size). */
    class NativeEditorPlugin final : public juce::AudioPluginInstance
    {
    public:
        static constexpr int kWidth = 400, kHeight = 300;

        class Editor final : public juce::AudioProcessorEditor
        {
        public:
            Editor (NativeEditorPlugin& p, bool resizable, bool snaps) : juce::AudioProcessorEditor (p), snapsBack (snaps)
            {
                setResizable (resizable, false);
                setSize (32, 32);
            }

            void attach() { setSize (kWidth, kHeight); }

            void setScaleFactor (float s) override
            {
                scale = s;
                setSize (juce::roundToInt (kWidth * s), juce::roundToInt (kHeight * s));
            }

            void resized() override
            {
                if (snapsBack && scale == 1.0f && getWidth() > 32 && getWidth() < kWidth)
                    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Editor> (this)]
                    {
                        if (safe != nullptr)
                            safe->setSize (kWidth, kHeight);
                    });
            }

            void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::darkgrey); }

            float scale = 1.0f;
            bool snapsBack;
        };

        NativeEditorPlugin (juce::String f, bool r, bool s)
            : juce::AudioPluginInstance (BusesProperties()
                                             .withInput ("In", juce::AudioChannelSet::stereo())
                                             .withOutput ("Out", juce::AudioChannelSet::stereo())),
              format (std::move (f)), resizable (r), snaps (s)
        {
        }

        Editor* editor = nullptr;

        void fillInPluginDescription (juce::PluginDescription& d) const override
        {
            d.name = getName();
            d.pluginFormatName = format;
            d.fileOrIdentifier = "native-editor-" + format;
            d.numInputChannels = d.numOutputChannels = 2;
        }

        const juce::String getName() const override { return "Native editor " + format; }
        void prepareToPlay (double, int) override {}
        void releaseResources() override {}
        using juce::AudioPluginInstance::processBlock;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return editor = new Editor (*this, resizable, snaps); }
        bool hasEditor() const override { return true; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock&) override {}
        void setStateInformation (const void*, int) override {}

    private:
        juce::String format;
        bool resizable, snaps;
    };
} // namespace

/** The plugin window's zoom with editors that act as real plugins' native ones. */
class PluginChainZoomTests final : public juce::UnitTest
{
public:
    PluginChainZoomTests() : juce::UnitTest ("Plugin window zoom with native editors", "LiveDAW") {}

    void runTest() override
    {
        using V = ui::PluginChainView;
        MixerEngine engine (0);
        OfflineRig rig (engine, 1, 2);
        auto& fx = engine.getFx();

        beginTest ("A native editor that takes its real size once shown zooms by the host scale");
        {
            auto* plugin = new NativeEditorPlugin ("VST3", false, false);
            fx.setPlugin (0, std::unique_ptr<juce::AudioPluginInstance> (plugin));
            V view (fx, 0, {});
            view.setMaxSize ({ 4000, 2000 });
            expect (plugin->editor != nullptr);
            if (plugin->editor != nullptr)
            {
                plugin->editor->attach(); // its real size, as a VST3 view gets once attached
                test::pumpMessages (50);
                expect (view.getZoomMethod (0) == V::ZoomMethod::hostScale);
                view.setZoom (0, 67);
                test::pumpMessages (1000); // the view's timer drops a host scale the editor doesn't follow
                expect (view.getZoomMethod (0) == V::ZoomMethod::hostScale, "the host scale was dropped");
                expectEquals (view.getEditorBounds (0).getWidth(), juce::roundToInt (NativeEditorPlugin::kWidth * 0.67f));
                view.setZoom (0, 100);
                expectEquals (view.getEditorBounds (0).getWidth(), NativeEditorPlugin::kWidth);
            }
        }

        beginTest ("An editor that goes back to its size after a resize falls back to the host scale");
        {
            auto* plugin = new NativeEditorPlugin ("LV2", true, true);
            fx.setPlugin (1, std::unique_ptr<juce::AudioPluginInstance> (plugin));
            V view (fx, 1, {});
            view.setMaxSize ({ 4000, 2000 });
            if (plugin->editor != nullptr)
            {
                plugin->editor->attach();
                test::pumpMessages (50);
                expect (view.getZoomMethod (0) == V::ZoomMethod::resize);
                view.setZoom (0, 67);
                test::pumpMessages (1000);
                expect (view.getZoomMethod (0) == V::ZoomMethod::hostScale, "still shown as resized");
                expectEquals (view.getEditorBounds (0).getWidth(), juce::roundToInt (NativeEditorPlugin::kWidth * 0.67f));
            }
        }

        fx.setPlugin (0, nullptr);
        fx.setPlugin (1, nullptr);
        engine.getGarbage().collectAllNow();
    }
};

static PluginChainZoomTests pluginChainZoomTests;


namespace
{
    /** A window with four coloured quadrants (red, green, blue, white) to picture. */
    struct Quadrants final : public juce::Component
    {
        void paint (juce::Graphics& g) override
        {
            const auto b = getLocalBounds();
            const int w = b.getWidth() / 2, h = b.getHeight() / 2;
            g.setColour (juce::Colours::red);
            g.fillRect (0, 0, w, h);
            g.setColour (juce::Colours::lime);
            g.fillRect (w, 0, b.getWidth() - w, h);
            g.setColour (juce::Colours::blue);
            g.fillRect (0, h, w, b.getHeight() - h);
            g.setColour (juce::Colours::white);
            g.fillRect (w, h, b.getWidth() - w, b.getHeight() - h);
        }
    };

    /** The plugin overview's dependencies, with a settings file of its own. */
    struct OverviewRig
    {
        explicit OverviewRig (MixerEngine& e) : engine (e), show (e, host), meters (e), ctx (e, host, show, log, devices, settings, meters)
        {
            ctx.pluginPictures = &pictures;
            ctx.openPluginEditor = [this] (int slot) { opened = slot; };
        }
        ~OverviewRig() { settingsFile.deleteFile(); }

        MixerEngine& engine;
        PluginHost host { nullptr };
        Show show;
        EventLog log;
        juce::AudioDeviceManager devices;
        juce::File settingsFile = juce::File::createTempFile (".settings");
        juce::PropertiesFile settings { settingsFile, {} };
        ui::MeterBank meters;
        ui::PluginPictures pictures;
        ui::UiContext ctx;
        int opened = -1;
    };

    /** True if `image` has a pixel of `c` (to within `tolerance` per channel) in `area`. */
    bool hasColour (const juce::Image& image, juce::Colour c, juce::Rectangle<int> area = {}, int tolerance = 6)
    {
        if (area.isEmpty())
            area = image.getBounds();
        area = area.getIntersection (image.getBounds());
        for (int y = area.getY(); y < area.getBottom(); y += 2)
            for (int x = area.getX(); x < area.getRight(); x += 2)
            {
                const auto p = image.getPixelAt (x, y);
                if (std::abs (p.getRed() - c.getRed()) <= tolerance && std::abs (p.getGreen() - c.getGreen()) <= tolerance
                    && std::abs (p.getBlue() - c.getBlue()) <= tolerance)
                    return true;
            }
        return false;
    }

    void saveSnapshot (const juce::Image& image, const juce::String& name)
    {
        // LIVEDAW_SNAPSHOT_DIR: where to put the overview's pictures to look at them.
        const auto dir = juce::SystemStats::getEnvironmentVariable ("LIVEDAW_SNAPSHOT_DIR", {});
        if (dir.isEmpty())
            return;
        juce::FileOutputStream out (juce::File (dir).getChildFile (name));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (image, out);
    }
} // namespace

/** The plugin overview window and the plugins' pictures. */
class PluginOverviewTests final : public juce::UnitTest
{
public:
    PluginOverviewTests() : juce::UnitTest ("Plugin overview", "LiveDAW") {}

    void runTest() override
    {
        beginTest ("A picture of an editor shows what it draws on screen, scaled down to the width asked for");
        {
            juce::DocumentWindow window ("Picture test", juce::Colours::black, 0);
            window.setUsingNativeTitleBar (true);
            Quadrants quadrants;
            quadrants.setSize (400, 200);
            window.setContentNonOwned (&quadrants, true);
            window.setTopLeftPosition (60, 60);
            window.setVisible (true);
            quadrants.repaint();
            test::pumpMessages (300);

            const auto image = ui::PluginPictures::grab (quadrants, 200);
            expect (image.isValid());
            expectEquals (image.getWidth(), 200);
            expectEquals (image.getHeight(), 100);
            const int w = image.getWidth(), h = image.getHeight();
            expect (hasColour (image, juce::Colours::red, { 0, 0, w / 2 - 4, h / 2 - 4 }));
            expect (hasColour (image, juce::Colours::lime, { w / 2 + 4, 0, w / 2 - 4, h / 2 - 4 }));
            expect (hasColour (image, juce::Colours::blue, { 0, h / 2 + 4, w / 2 - 4, h / 2 - 4 }));
            expect (hasColour (image, juce::Colours::white, { w / 2 + 4, h / 2 + 4, w / 2 - 4, h / 2 - 4 }));

            // Only what shows: a hidden or minimised editor gives no picture.
            window.setVisible (false);
            expect (ui::PluginPictures::grab (quadrants).isNull());
        }

       #if JUCE_WINDOWS
        beginTest ("A native plugin view (a child window of its own) is in the picture");
        {
            // What a VST3 editor is on Windows: a child window JUCE doesn't draw. A
            // component snapshot shows black there; the window's own drawing shows it.
            static const auto* className = L"SidecarPictureTestView";
            WNDCLASSEXW wc {};
            wc.cbSize = sizeof (wc);
            wc.lpfnWndProc = DefWindowProcW;
            wc.hInstance = GetModuleHandleW (nullptr);
            wc.hbrBackground = CreateSolidBrush (RGB (255, 0, 255));
            wc.lpszClassName = className;
            RegisterClassExW (&wc);

            juce::DocumentWindow window ("Native picture test", juce::Colours::black, 0);
            window.setUsingNativeTitleBar (true);
            juce::HWNDComponent nativeView;
            nativeView.setSize (300, 160);
            window.setContentNonOwned (&nativeView, true);
            window.setTopLeftPosition (80, 80);
            window.setVisible (true);
            auto* parent = static_cast<HWND> (window.getPeer()->getNativeHandle());
            auto* child = CreateWindowExW (0, className, L"", WS_CHILD | WS_VISIBLE, 0, 0, 300, 160, parent, nullptr, wc.hInstance, nullptr);
            expect (child != nullptr);
            nativeView.setHWND (child);
            nativeView.updateHWNDBounds();
            test::pumpMessages (300);

            const auto image = ui::PluginPictures::grab (nativeView);
            expect (image.isValid());
            expect (hasColour (image, juce::Colours::magenta, image.getBounds().reduced (10)));
            expect (! hasColour (nativeView.createComponentSnapshot (nativeView.getLocalBounds()), juce::Colours::magenta));

            nativeView.setHWND (nullptr);
            DestroyWindow (child);
            DeleteObject (wc.hbrBackground);
            UnregisterClassW (className, wc.hInstance);
        }
       #endif

        beginTest ("Pictures are kept per plugin, never shown for another plugin at the same address, and pruned");
        {
            ui::PluginPictures pictures;
            AffinePlugin a ("Alpha", 1.0f, 0.0f), b ("Beta", 1.0f, 0.0f);
            juce::Image red (juce::Image::ARGB, 8, 8, true);
            red.clear (red.getBounds(), juce::Colours::red);
            const int before = pictures.getGeneration();
            pictures.store (a, red);
            pictures.store (b, {}); // an empty picture is ignored
            expect (pictures.getGeneration() > before);
            expectEquals (pictures.size(), 1);
            expect (pictures.find (&a) != nullptr && pictures.find (&b) == nullptr);
            expectEquals (pictures.find (&a)->name, juce::String ("Alpha"));

            // Only the plugins still in a slot keep theirs.
            AffinePlugin c ("Gamma", 1.0f, 0.0f);
            pictures.store (c, red);
            pictures.retainOnly ({ &c });
            expect (pictures.find (&a) == nullptr && pictures.find (&c) != nullptr);
            pictures.forget (&c);
            expectEquals (pictures.size(), 0);
        }

        beginTest ("The overview opens in a window of its own, shows every slot's plugins with their pictures and remembers that it was open");
        {
            MixerEngine engine (0);
            OfflineRig rig (engine, 2, 2);
            test::setChannels (engine, 2);
            OverviewRig ui (engine);

            auto& fx = engine.getFx();
            fx.setNumSlots (48);
            fx.setPlugin (2, std::make_unique<AffinePlugin> ("Double", 2.0f, 0.0f));
            expect (fx.addToChain (2, std::make_unique<AffinePlugin> ("Offset", 1.0f, 0.5f)));
            fx.setChainMemberBypassed (2, 1, true);
            fx.setPlugin (39, std::make_unique<AffinePlugin> ("Far away", 1.0f, 0.0f));
            fx.setPlugin (5, std::make_unique<AffinePlugin> ("Bypassed slot", 1.0f, 0.0f));
            fx.params (5).bypass = true;
            engine.getChannels()[0]->params.preInsert.on = true;
            engine.getChannels()[0]->params.preInsert.fx = 2;
            engine.getChannels()[1]->params.preInsert.on = true;
            engine.getChannels()[1]->params.preInsert.fx = 5;
            rig.setInput (0, 0.25f);
            rig.run (4);

            // A picture of FX 3's first plugin: magenta.
            juce::Image magenta (juce::Image::ARGB, 320, 180, true);
            magenta.clear (magenta.getBounds(), juce::Colours::magenta);
            ui.pictures.store (*fx.getChainMember (2, 0), magenta);

            {
                ui::PluginOverviewWindow window (ui.ctx, [] {});
                expect (window.isOnDesktop());
                expect (ui.settings.getBoolValue (ui::PluginOverviewWindow::openKey, false));
                expect (window.getShownSlots() == std::vector<int> ({ 2, 5, 39 }));
                auto* content = window.getContentComponent();
                expect (content != nullptr && content->getWidth() > 0);

                window.setLayout (ui::PluginOverviewWindow::Layout::cards);
                const auto cards = content->createComponentSnapshot (content->getLocalBounds());
                saveSnapshot (cards, "plugin-overview-cards.png");
                expect (hasColour (cards, ui::Colours::green));   // FX 3 runs
                expect (hasColour (cards, ui::Colours::chipOff)); // FX 6 bypassed, FX 40 not used
                expect (hasColour (cards, juce::Colours::magenta)); // FX 3's picture

                window.setLayout (ui::PluginOverviewWindow::Layout::list);
                expect (ui.settings.getValue ("pluginOverview.layout") == "list");
                const auto list = content->createComponentSnapshot (content->getLocalBounds());
                saveSnapshot (list, "plugin-overview-list.png");
                expect (hasColour (list, juce::Colours::magenta));

                // A slot emptied goes from the overview at its next look.
                fx.setPlugin (39, nullptr);
                test::pumpMessages (200);
                expect (window.getShownSlots() == std::vector<int> ({ 2, 5 }));

                window.closeButtonPressed();
                expect (! ui.settings.getBoolValue (ui::PluginOverviewWindow::openKey, true));
            }
            // A new overview opens with the layout chosen last time.
            {
                ui::PluginOverviewWindow window (ui.ctx, [] {});
                expect (window.getLayout() == ui::PluginOverviewWindow::Layout::list);
            }
            fx.setPlugin (2, nullptr);
            fx.setPlugin (5, nullptr);
            engine.getGarbage().collectAllNow();
        }

        beginTest ("The overview with nothing loaded says so");
        {
            MixerEngine engine (0);
            OverviewRig ui (engine);
            ui::PluginOverviewWindow window (ui.ctx, [] {});
            expect (window.getShownSlots().empty());
            const auto image = window.getContentComponent()->createComponentSnapshot (window.getContentComponent()->getLocalBounds());
            saveSnapshot (image, "plugin-overview-empty.png");
            expect (image.isValid());
        }
    }
};

static PluginOverviewTests pluginOverviewTests;

/** Monkey test: random operations on the FX slots (more of them, plugins, chains,
    devices, faults), the inserts, the direct outs and RECORDER taps, scenes and
    shows, with the audio running on its own thread and the plugin overview open
    and clicked, resized and drawn at random. It checks the audio stays finite and
    the rules hold: 16 to 64 slots, none in use past them, only real direct outs
    taken, the overview showing the slots that hold plugins.
    LIVEDAW_MONKEY_SEED / LIVEDAW_MONKEY_SECONDS replay a seed or run longer. */
class MonkeyTests final : public juce::UnitTest
{
public:
    MonkeyTests() : juce::UnitTest ("Monkey", "LiveDAW") {}

    void runTest() override
    {
        const auto seedText = juce::SystemStats::getEnvironmentVariable ("LIVEDAW_MONKEY_SEED", {});
        const juce::int64 seed = seedText.isNotEmpty() ? seedText.getLargeIntValue() : 20261031;
        const int seconds = juce::jlimit (1, 3600, juce::SystemStats::getEnvironmentVariable ("LIVEDAW_MONKEY_SECONDS", "4").getIntValue());
        beginTest ("Monkey: FX slots, plugins, direct outs, scenes and the plugin overview at random while audio runs (seed "
                   + juce::String (seed) + ", " + juce::String (seconds) + " s)");

        MixerEngine engine (2);
        OfflineRig rig (engine, 8, 8);
        for (int i = 0; i < 8; ++i)
            rig.setInput (i, 0.05f * static_cast<float> (i + 1));
        test::setChannels (engine, 8);
        OverviewRig ui (engine);
        int opened = -1;
        ui.ctx.openPluginEditor = [&opened] (int slot) { opened = slot; };
        ui.show.addScene ("Start");
        engine.getGarbage().setDeviceRunning (true);

        std::atomic<bool> running { true }, notFinite { false };
        std::atomic<int> blocks { 0 };
        std::thread audio ([&]
        {
            std::vector<const float*> in;
            std::vector<float*> out;
            for (auto& v : rig.inputs)
                in.push_back (v.data());
            for (auto& v : rig.outputs)
                out.push_back (v.data());
            while (running.load())
            {
                engine.processBlock (in.data(), (int) in.size(), out.data(), (int) out.size(), OfflineRig::kBlock);
                for (auto& o : rig.outputs)
                    for (float x : o)
                        if (! std::isfinite (x))
                            notFinite = true;
                ++blocks;
            }
        });

        auto& fx = engine.getFx();
        juce::Random rng (seed);
        auto window = std::make_unique<ui::PluginOverviewWindow> (ui.ctx, [] {});
        auto* content = window->getContentComponent();
        juce::Component* body = nullptr;
        std::vector<juce::Button*> keys;
        for (auto* c : content->getChildren())
        {
            if (auto* b = dynamic_cast<juce::Button*> (c))
                keys.push_back (b);
            if (auto* v = dynamic_cast<juce::Viewport*> (c))
                body = v->getViewedComponent();
        }
        expect (body != nullptr && keys.size() >= 6);

        auto randomPlugin = [&rng]
        {
            const int r = rng.nextInt (20);
            const auto mode = r == 0 ? FakePlugin::Mode::EmitNaN : r == 1 ? FakePlugin::Mode::Throw : FakePlugin::Mode::Gain;
            return std::make_unique<FakePlugin> (rng.nextFloat() * 1.5f, mode, "monkey-" + juce::String (rng.nextInt (6)));
        };
        auto randomStrip = [&]() -> Strip*
        {
            const auto kind = static_cast<StripKind> (rng.nextInt (kNumStripKinds));
            auto& strips = engine.getStrips (kind);
            return strips.empty() ? nullptr : strips[(size_t) rng.nextInt ((int) strips.size())].get();
        };
        auto randomDirect = [&rng]
        {
            // Mostly direct outs (mains and matrices have none: refused), some junk.
            if (rng.nextInt (8) == 0)
                return rng.nextInt();
            return DirectOut { static_cast<StripKind> (rng.nextInt (kNumStripKinds)), 1 + rng.nextInt (70), rng.nextInt (2), rng.nextBool() }.code();
        };
        auto click = [] (juce::Component& c, juce::Point<int> p)
        {
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent e (source, p.toFloat(), {}, juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, p.toFloat(), now,
                                      1, false);
            c.mouseMove (e);
            c.mouseUp (e);
        };
        const int numModels = (int) models::table (models::Fx).size();

        int operations = 0, ruleBreaks = 0, overviewMismatches = 0, refusedJunk = 0, lastOp = -1;
        auto breakRule = [&] (const juce::String& what)
        {
            if (ruleBreaks++ < 5)
                logMessage ("RULE BROKEN after operation " + juce::String (operations) + " (kind " + juce::String (lastOp) + "): " + what);
        };
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) seconds * 1000u;
        while (juce::Time::getMillisecondCounter() < end)
        {
            const int slot = rng.nextInt (kMaxFx);
            lastOp = rng.nextInt (14);
            switch (lastOp)
            {
                case 0: fx.setNumSlots (rng.nextInt (90)); break;
                case 1: fx.setPlugin (slot, rng.nextInt (4) == 0 ? nullptr : randomPlugin()); break;
                case 2:
                    if (fx.hasPlugin (slot))
                    {
                        const int n = fx.getNumChainMembers (slot);
                        switch (rng.nextInt (4))
                        {
                            case 0: fx.addToChain (slot, randomPlugin(), rng.nextInt (n + 2) - 1); break;
                            case 1: fx.removeFromChain (slot, rng.nextInt (n)); break;
                            case 2: fx.moveInChain (slot, rng.nextInt (n), rng.nextInt (n)); break;
                            default: fx.setChainMemberBypassed (slot, rng.nextInt (n), rng.nextBool()); break;
                        }
                    }
                    break;
                case 3:
                {
                    const int model = rng.nextInt (numModels);
                    if (model != ld::kFxExt && FxSlots::isAllowed (model, slot))
                        fx.setModel (slot, model);
                    break;
                }
                case 4:
                    if (auto* s = randomStrip())
                    {
                        auto& ins = rng.nextBool() ? s->params.preInsert : s->params.postInsert;
                        ins.on = rng.nextBool();
                        ins.fx = rng.nextInt (kMaxFx + 1) - 1;
                    }
                    break;
                case 5:
                {
                    const int port = rng.nextInt (8), code = randomDirect();
                    const bool ok = engine.routeOutputSource (port, code);
                    if (! ok && code >= ld::kDirectSourceBase)
                        ++refusedJunk;
                    if (code >= ld::kDirectSourceBase && ok != DirectOut::isDirectOut (code))
                        breakRule ("code " + juce::String::toHexString (code) + (ok ? " taken" : " refused")); // only real direct outs are taken
                    break;
                }
                case 6:
                    engine.getUsbRecorder().setTap (rng.nextInt (UsbRecorder::kNumChannels), UsbRecorder::Tap::fromCode (randomDirect()));
                    break;
                case 7:
                    if (rng.nextBool())
                        test::setChannels (engine, rng.nextInt (24));
                    else if (rng.nextBool())
                        engine.addBus();
                    else if (engine.getBuses().size() > (size_t) kWingBuses)
                        engine.removeStrip (engine.getBuses().back().get());
                    break;
                case 8:
                    switch (rng.nextInt (3))
                    {
                        case 0: ui.show.addScene ("M" + juce::String (operations)); break;
                        case 1: ui.show.recallScene (rng.nextInt (juce::jmax (1, ui.show.getNumScenes()))); break;
                        default:
                        {
                            auto xml = ui.show.toXml();
                            ui.show.loadFromXml (*xml, false);
                            break;
                        }
                    }
                    break;
                case 9: fx.clearFlags (slot); break;
                case 10:
                    // Pictures come and go.
                    if (fx.hasPlugin (slot) && rng.nextBool())
                    {
                        juce::Image picture (juce::Image::ARGB, 40 + rng.nextInt (600), 20 + rng.nextInt (400), true);
                        picture.clear (picture.getBounds(), juce::Colour ((juce::uint32) rng.nextInt() | 0xff000000u));
                        ui.pictures.store (*fx.getChainMember (slot, rng.nextInt (fx.getNumChainMembers (slot))), picture);
                    }
                    else
                    {
                        std::vector<const juce::AudioProcessor*> live;
                        for (int s = 0; s < kMaxFx; ++s)
                            for (int i = 0; i < fx.getNumChainMembers (s); ++i)
                                live.push_back (fx.getChainMember (s, i));
                        ui.pictures.retainOnly (live);
                    }
                    break;
                case 11:
                    // The overview: its keys, its slots, its size, its layout.
                    switch (rng.nextInt (4))
                    {
                        case 0: keys[(size_t) rng.nextInt ((int) keys.size())]->triggerClick(); break;
                        case 1: click (*body, { rng.nextInt (juce::jmax (1, body->getWidth())), rng.nextInt (juce::jmax (1, body->getHeight())) }); break;
                        case 2: window->setSize (300 + rng.nextInt (1800), 150 + rng.nextInt (1100)); break;
                        default: window->setLayout (rng.nextBool() ? ui::PluginOverviewWindow::Layout::cards : ui::PluginOverviewWindow::Layout::list); break;
                    }
                    break;
                case 12:
                    // Draw it as it is now (every paint path, any state).
                    if (const auto image = content->createComponentSnapshot (content->getLocalBounds()); ! image.isValid())
                        breakRule ("the overview didn't draw");
                    break;
                default: test::pumpMessages (rng.nextInt (30)); break; // timers: the overview polls, keys click
            }
            engine.getGarbage().collect();
            ++operations;

            // The rules.
            const int n = fx.getNumSlots();
            if (n < kWingFx || n > kMaxFx)
                breakRule (juce::String (n) + " slots");
            for (int s = n; s < kMaxFx; ++s)
                if (fx.isInUse (s))
                    breakRule ("FX " + juce::String (s + 1) + " in use past " + juce::String (n) + " slots");
            if (operations % 250 == 0)
            {
                test::pumpMessages (120); // a poll or two
                std::vector<int> withPlugins;
                for (int s = 0; s < kMaxFx; ++s)
                    if (fx.hasPlugin (s))
                        withPlugins.push_back (s);
                if (window->getShownSlots() != withPlugins)
                    ++overviewMismatches;
            }
        }

        running = false;
        audio.join();
        window.reset();
        test::pumpMessages (50);
        for (int s = 0; s < kMaxFx; ++s)
            fx.setPlugin (s, nullptr);
        engine.getGarbage().setDeviceRunning (false);
        engine.getGarbage().collectAllNow();

        logMessage (juce::String (operations) + " random operations during " + juce::String (blocks.load()) + " audio blocks; "
                    + juce::String (refusedJunk) + " codes that are no direct out refused; the overview opened " + (opened >= 0 ? "FX " + juce::String (opened + 1) : juce::String ("no slot")) + " last");
        expect (operations > 200, "too few operations to mean much");
        expect (blocks.load() > 100);
        expect (! notFinite.load(), "the outputs stay finite");
        expectEquals (ruleBreaks, 0);
        expectEquals (overviewMismatches, 0);
        expectEquals ((int) engine.getGarbage().numPending(), 0);
    }
};

static MonkeyTests monkeyTests;
