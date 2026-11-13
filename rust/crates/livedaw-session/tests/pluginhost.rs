//! A plug-in host console saved and loaded: its size is its channel count.

use livedaw_engine::profile::HostSize;
use livedaw_engine::{ConsoleConfig, Engine, Profile, StripKind};
use livedaw_session::console::{load_console, save_console, NoPlugins};
use livedaw_session::xml;
use std::sync::atomic::Ordering::Relaxed;

#[test]
fn a_plug_in_host_keeps_its_size_and_its_bypass() {
    for size in HostSize::ALL {
        let profile = Profile::PluginHost(size);
        let engine = Engine::new(0);
        engine.configure(&ConsoleConfig { profile, counts: profile.spec().counts });
        engine.strips(StripKind::Channel)[1].params.input.bypass.store(true, Relaxed);
        let saved = xml::parse(&save_console(&engine, &mut NoPlugins).to_document()).unwrap();

        let other = Engine::new(0);
        let warnings = load_console(&other, &saved, &mut NoPlugins, false);
        assert!(warnings.is_empty(), "{warnings:?}");
        assert_eq!(other.profile(), profile);
        assert_eq!(other.strips(StripKind::Channel).len(), size.channels());
        assert!(other.strips(StripKind::Channel)[1].params.input.bypass.load(Relaxed));
        assert!(!other.strips(StripKind::Channel)[0].params.input.bypass.load(Relaxed));
    }
}

#[test]
fn the_patch_comes_back_with_the_show() {
    use livedaw_engine::routing::DirectSource;
    let profile = Profile::PluginHost(HostSize::Ch32);
    let engine = Engine::new(0);
    engine.configure(&ConsoleConfig { profile, counts: profile.spec().counts });
    // Channel 1 takes interface input 9; interface output 3 plays channel 20 before its chain.
    engine.strips(StripKind::Channel)[0].params.input_left.store(8, Relaxed);
    let source = DirectSource { kind: StripKind::Channel, number: 20, side: 0, post: false }.code();
    assert!(engine.route_output_source(2, source));
    let saved = xml::parse(&save_console(&engine, &mut NoPlugins).to_document()).unwrap();

    let other = Engine::new(0);
    let warnings = load_console(&other, &saved, &mut NoPlugins, false);
    assert!(warnings.is_empty(), "{warnings:?}");
    assert_eq!(other.strips(StripKind::Channel)[0].params.input_left.load(Relaxed), 8);
    assert_eq!(other.strips(StripKind::Channel)[1].params.input_left.load(Relaxed), 1, "the others stay 1:1");
    assert_eq!(other.output_patch().get(2), source);
    assert_eq!(other.output_patch().get(3), engine.output_patch().get(3));
}

// ---- scenes: the blocks of a rack ---------------------------------------------------------

mod rack_recall {
    use super::*;
    use livedaw_engine::blocks::{BlockKind, BlockParams};
    use livedaw_session::console::{CapturedInsert, InsertMode, PluginBridge, RestoreError, PLUGIN_BLOCK_SLOT_BASE};
    use livedaw_session::{xml::Element, Show};
    use std::collections::HashMap;

    /// A host whose plug-ins are a name and a state per block slot; it records what a recall asked of it.
    #[derive(Default)]
    struct Recorder {
        plugins: HashMap<usize, (String, String)>,
        cleared: Vec<usize>,
        recalled: Vec<usize>,
        glide: Vec<f64>,
    }

    impl PluginBridge for Recorder {
        fn capture_fx(&mut self, slot: usize) -> Option<CapturedInsert> {
            let (name, state) = self.plugins.get(&slot)?;
            let mut desc = Element::new("PLUGIN");
            desc.set("name", name).set("format", "Fake");
            Some(CapturedInsert { description: desc.to_compact(), state: state.clone() })
        }
        fn restore_fx(&mut self, slot: usize, _: InsertMode, description: &str, state: &str) -> Result<(), RestoreError> {
            let name = xml::parse(description).map_err(|e| RestoreError::Failed(e.to_string()))?.str_or("name", "").to_owned();
            self.plugins.insert(slot, (name, state.into()));
            self.recalled.push(slot);
            Ok(())
        }
        fn clear_fx(&mut self, slot: usize) {
            self.plugins.remove(&slot);
            self.cleared.push(slot);
        }
        fn begin_recall(&mut self, glide_seconds: f64) {
            self.glide.push(glide_seconds);
        }
    }

    unsafe extern "C" fn pass_through(_: *mut std::ffi::c_void, _: *const *mut f32, _: i32, _: i32) -> i32 {
        0
    }

    fn slot_of(b: &livedaw_engine::blocks::Block) -> usize {
        match &b.params {
            BlockParams::Plugin(p) => PLUGIN_BLOCK_SLOT_BASE + p.pid() as usize,
            _ => unreachable!(),
        }
    }

    fn add(engine: &Engine, host: &mut Recorder, at: usize, name: &str, state: &str) -> u32 {
        let strip = engine.strips(StripKind::Channel)[0].clone();
        let b = engine.insert_block(&strip, BlockKind::Plugin, at).unwrap();
        host.plugins.insert(slot_of(&b), (name.into(), state.into()));
        let cb = livedaw_engine::InsertCallbacks { process: pass_through, release: None, ctx: std::ptr::null_mut() };
        assert!(engine.set_plugin_block(slot_of(&b) as u32 - PLUGIN_BLOCK_SLOT_BASE as u32, Some(cb), false));
        b.uid()
    }

    fn uids(engine: &Engine) -> Vec<u32> {
        engine.strips(StripKind::Channel)[0].blocks().iter().map(|b| b.uid()).collect()
    }

    fn host_engine() -> Engine {
        let profile = Profile::PluginHost(HostSize::Ch16);
        let engine = Engine::new(0);
        engine.configure(&ConsoleConfig { profile, counts: profile.spec().counts });
        engine
    }

    #[test]
    fn a_recall_brings_back_the_racks_blocks_in_their_order_and_leaves_safe_ones() {
        let engine = host_engine();
        let mut host = Recorder::default();
        let (a, b, c) =
            (add(&engine, &mut host, 1, "A", "a1"), add(&engine, &mut host, 2, "B", "b1"), add(&engine, &mut host, 3, "C", "c1"));
        let mut show = Show::new();
        let scene = show.add_scene(&engine, &mut host, "Verse", -1);
        assert_eq!(uids(&engine), vec![a, b, c]);

        // The rack changes: B goes, D comes (safe), C moves ahead of A, and E comes (not safe).
        let strip = engine.strips(StripKind::Channel)[0].clone();
        host.plugins.remove(&slot_of(&strip.blocks()[1]));
        assert!(engine.remove_block(&strip, b));
        let d = add(&engine, &mut host, 1, "D", "d1");
        let e = add(&engine, &mut host, 1, "E", "e1");
        strip.blocks().iter().find(|x| x.uid() == d).unwrap().set_safe(true);
        assert!(engine.move_block(&strip, c, 1));
        let e_slot = slot_of(strip.blocks().iter().find(|x| x.uid() == e).unwrap());

        let result = show.recall_scene(&engine, &mut host, scene);
        assert!(result.warnings.is_empty(), "{:?}", result.warnings);
        let now = uids(&engine);
        assert!(!now.contains(&e), "a block the scene lacks goes");
        assert!(host.cleared.contains(&e_slot), "its plug-in is closed first");
        assert!(now.contains(&d), "a safe block stays");
        let order: Vec<u32> = now.iter().copied().filter(|u| *u != d).collect();
        assert_eq!(order, vec![a, b, c], "the scene's order, B back");
        let b_slot = slot_of(strip.blocks().iter().find(|x| x.uid() == b).unwrap());
        assert_eq!(host.plugins[&b_slot], ("B".to_string(), "b1".to_string()), "B's plug-in comes back from its description");

        // The safe block's state is left alone by a recall that has other values for it.
        let d_slot = slot_of(strip.blocks().iter().find(|x| x.uid() == d).unwrap());
        host.plugins.insert(d_slot, ("D".into(), "d2".into()));
        host.recalled.clear();
        show.recall_scene(&engine, &mut host, scene);
        assert!(!host.recalled.contains(&d_slot));
        assert_eq!(host.plugins[&d_slot].1, "d2");
    }

    #[test]
    fn safes_are_saved_with_the_console_and_scenes_say_when_to_glide() {
        let engine = host_engine();
        let mut host = Recorder::default();
        let a = add(&engine, &mut host, 1, "A", "a1");
        let strip = engine.strips(StripKind::Channel)[0].clone();
        strip.blocks().iter().find(|x| x.uid() == a).unwrap().set_safe(true);
        let saved = xml::parse(&save_console(&engine, &mut host).to_document()).unwrap();

        let other = host_engine();
        let mut host2 = Recorder::default();
        load_console(&other, &saved, &mut host2, true);
        let blocks = other.strips(StripKind::Channel)[0].blocks();
        assert_eq!(blocks.len(), 1);
        assert!(blocks[0].safe());

        let mut show = Show::new();
        let scene = show.add_scene(&engine, &mut host, "Glide", -1);
        show.scene_mut(scene).unwrap().fade_seconds = 2.5;
        show.scene_mut(scene).unwrap().glide = true;
        assert!(show.to_xml(&engine, &mut host).to_document().contains("glide=\""), "the show's file holds it");
        show.recall_scene(&engine, &mut host, scene);
        assert_eq!(host.glide, vec![2.5, 0.0], "the bridge hears the glide time, then zero");
    }

    #[test]
    fn a_host_rack_mark_is_saved_with_the_console_and_comes_back_with_a_scene_recall() {
        let engine = host_engine();
        let mut host = Recorder::default();
        let a = add(&engine, &mut host, 1, "A", "a1");
        let b = add(&engine, &mut host, 2, "B", "b1");
        let strip = engine.strips(StripKind::Channel)[0].clone();
        strip.blocks().iter().find(|x| x.uid() == a).unwrap().set_host_rack(true);
        let saved = xml::parse(&save_console(&engine, &mut host).to_document()).unwrap();

        let other = host_engine();
        let mut host2 = Recorder::default();
        load_console(&other, &saved, &mut host2, true);
        let blocks = other.strips(StripKind::Channel)[0].blocks();
        assert_eq!(blocks.len(), 2);
        assert!(blocks.iter().find(|x| x.uid() == a).unwrap().host_rack(), "the rack stays a rack");
        assert!(!blocks.iter().find(|x| x.uid() == b).unwrap().host_rack(), "a plain plug-in block stays plain");

        // A scene that holds the rack puts it back marked when the block was taken out.
        let mut show = Show::new();
        let scene = show.add_scene(&engine, &mut host, "Racks", -1);
        let x = engine.strips(StripKind::Channel)[0].clone();
        engine.remove_block(&x, a);
        assert_eq!(uids(&engine), vec![b]);
        show.recall_scene(&engine, &mut host, scene);
        let blocks = engine.strips(StripKind::Channel)[0].blocks();
        assert!(blocks.iter().find(|y| y.uid() == a).unwrap().host_rack());
    }
}
