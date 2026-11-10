//! A dLive's Mixer Config: its mixes laid out anew, the rest of the console kept.

use livedaw_engine::blocks::{Block, BlockKind, BlockParams, Step};
use livedaw_engine::chain::Node;
use livedaw_engine::config::{ConsoleConfig, Counts};
use livedaw_engine::params::{DynSlotParams, StripKind};
use livedaw_engine::profile::{DLiveLayout, Profile};
use livedaw_engine::routing::strip_source_input;
use livedaw_engine::{Engine, Strip};
use livedaw_session::console::{apply_dlive_layout, load_console, save_console, NoPlugins};
use std::sync::atomic::Ordering::Relaxed;

fn dlive() -> Engine {
    let engine = Engine::new(0);
    engine.configure(&ConsoleConfig { profile: Profile::AhDLive(DLiveLayout::FOH), counts: Counts::DLIVE });
    engine
}

const LAYOUT: DLiveLayout =
    DLiveLayout { grp: 2, st_grp: 2, aux: 16, st_aux: 4, fx: 4, st_fx: 2, mtx: 2, st_mtx: 2, ..DLiveLayout::FOH };

fn eq_blocks(engine: &Engine, kind: StripKind, index: usize) -> usize {
    let strip = engine.strip_at(kind, index).unwrap();
    strip.steps().iter().filter(|s| matches!(s, Step::Block(b) if b.kind() == BlockKind::Eq)).count()
}

#[test]
fn it_keeps_the_channels_and_resets_the_mixes() {
    let engine = dlive();
    let ch = engine.strip_at(StripKind::Channel, 3).unwrap();
    ch.params.fader_db.store(-7.0);
    ch.params.mute.store(false, Relaxed);
    let send = ch.sends.get_or_create(2).unwrap();
    send.enabled.store(true, Relaxed);
    send.level_db.store(-3.0);
    let group = engine.strip_at(StripKind::Bus, 2).unwrap();
    group.params.fader_db.store(-20.0);

    let warnings = apply_dlive_layout(&engine, LAYOUT, &mut NoPlugins).unwrap();
    assert!(warnings.is_empty(), "{warnings:?}");

    assert_eq!(engine.profile(), Profile::AhDLive(LAYOUT));
    assert_eq!(engine.counts(), LAYOUT.counts());
    let ch = engine.strip_at(StripKind::Channel, 3).unwrap();
    assert_eq!(ch.params.fader_db.load(), -7.0, "the channel stays");
    assert!(!ch.params.mute.load(Relaxed));
    assert!(ch.sends.find(2).is_none_or(|s| !s.enabled.load(Relaxed)), "its send to the old mix is gone");
    let bus = engine.strip_at(StripKind::Bus, 2).unwrap();
    assert_eq!(bus.params.fader_db.load(), 0.0, "a mix starts at its default");
    assert_eq!(bus.name(), "StGrp1");

    // The mixes have their GEQ, once; the LR kept its one.
    assert_eq!(eq_blocks(&engine, StripKind::Bus, 2), 1);
    assert_eq!(eq_blocks(&engine, StripKind::Matrix, 0), 1);
    assert_eq!(eq_blocks(&engine, StripKind::Main, 0), 1);
    // The FX sends feed their FX: FX 1 is the first FX send.
    let fx = &engine.controls.fx[0];
    assert_eq!(fx.source_left.load(Relaxed), strip_source_input(StripKind::Bus, LAYOUT.fx_at(), 0));
    assert_eq!(engine.controls.fx[5].source_left.load(Relaxed), strip_source_input(StripKind::Bus, LAYOUT.fx_at() + 5, 0));
    assert!(engine.controls.fx[6].source_left.load(Relaxed) < 0, "FX 7 has no send in this layout");
}

#[test]
fn a_show_remembers_it() {
    let engine = dlive();
    apply_dlive_layout(&engine, LAYOUT, &mut NoPlugins).unwrap();
    let saved = save_console(&engine, &mut NoPlugins);

    let other = dlive();
    load_console(&other, &saved, &mut NoPlugins, true);
    assert_eq!(other.profile(), Profile::AhDLive(LAYOUT));
    assert_eq!(other.counts(), LAYOUT.counts());
    assert_eq!(other.strip_at(StripKind::Bus, LAYOUT.fx_at()).unwrap().name(), "FX1");
    assert_eq!(eq_blocks(&other, StripKind::Bus, 2), 1, "the GEQ comes back once");
}

#[test]
fn ip_stereo_config_moves_the_channels_and_keeps_their_settings() {
    let engine = dlive();
    // Ip5's settings: they follow it to its new place.
    let ip5 = engine.strip_at(StripKind::Channel, 4).unwrap();
    ip5.params.fader_db.store(-9.0);
    let id = ip5.id();
    // Ip1 and Ip2 become one stereo channel.
    let layout = DLiveLayout { stereo_inputs: DLiveLayout::FOH.stereo_inputs | 1, ..DLiveLayout::FOH };
    apply_dlive_layout(&engine, layout, &mut NoPlugins).unwrap();
    assert_eq!(engine.counts().channels, 123);
    assert_eq!(engine.num_strips(StripKind::Channel), 123);
    let first = engine.strip_at(StripKind::Channel, 0).unwrap();
    assert_eq!(first.name(), "1/2");
    assert!(first.params.stereo.load(Relaxed));
    assert_eq!(first.params.input_right.load(Relaxed), 1);
    let moved = engine.strip_at(StripKind::Channel, 3).unwrap();
    assert_eq!(moved.params.fader_db.load(), -9.0);
    assert_eq!(moved.id(), id, "scenes find it by its id");
    assert_eq!(moved.name(), "Ip5");

    // And it comes back from a show.
    let saved = save_console(&engine, &mut NoPlugins);
    let other = dlive();
    load_console(&other, &saved, &mut NoPlugins, true);
    assert_eq!(other.profile(), Profile::AhDLive(layout));
    assert_eq!(other.num_strips(StripKind::Channel), 123);
}

#[test]
fn a_show_keeps_a_mixs_ext_in_and_mix_source() {
    let engine = dlive();
    let aux = engine.strip_at(StripKind::Bus, 8).unwrap();
    aux.mix_input.ext_source.store(5, Relaxed);
    aux.mix_input.ext_trim_db.store(-6.0);
    aux.mix_input.ext_invert.store(true, Relaxed);
    aux.mix_input.mix_source.store(2, Relaxed);
    let saved = save_console(&engine, &mut NoPlugins);

    let other = dlive();
    load_console(&other, &saved, &mut NoPlugins, true);
    let m = &other.strip_at(StripKind::Bus, 8).unwrap().mix_input;
    assert_eq!(m.ext_source.load(Relaxed), 5);
    assert_eq!(m.ext_trim_db.load(), -6.0);
    assert!(m.ext_invert.load(Relaxed));
    assert_eq!(m.mix_source.load(Relaxed), 2);
    let untouched = &other.strip_at(StripKind::Bus, 9).unwrap().mix_input;
    assert_eq!(untouched.ext_source.load(Relaxed), -1);
    assert_eq!(untouched.mix_source.load(Relaxed), 3);
}

#[test]
fn a_show_of_the_foh_template_is_as_before() {
    let engine = dlive();
    let saved = save_console(&engine, &mut NoPlugins);
    assert!(saved.child("Config").unwrap().attr("layout").is_none());
    let other = dlive();
    load_console(&other, &saved, &mut NoPlugins, true);
    assert_eq!(other.profile(), Profile::AhDLive(DLiveLayout::FOH));
}

fn preamp_params(block: &Block) -> &DynSlotParams {
    let BlockParams::Dynamics(d) = &block.params else { panic!("a dynamics block") };
    d
}

/// Sets Ip1's preamp on: Tube Stage mode 3 at Drive 77, Level -3 dB.
fn dress_preamp(engine: &Engine) {
    let ip1 = engine.strip_at(StripKind::Channel, 0).unwrap();
    let block = ip1.preamp_block().unwrap();
    let d = preamp_params(&block);
    d.on.store(true, Relaxed);
    let tube = livedaw_dsp::model::model_index(d.table(), "DLVTUBE").unwrap();
    d.set_model(tube);
    d.param("mode").unwrap().store(2.0);
    d.param("drive").unwrap().store(77.0);
    d.param("level").unwrap().store(-3.0);
}

fn assert_dressed(strip: &Strip) {
    let block = strip.preamp_block().expect("the preamp block");
    assert_eq!(strip.blocks().len(), 1, "one block, once");
    let d = preamp_params(&block);
    assert!(d.on.load(Relaxed));
    assert_eq!(d.model_spec().unwrap().token, "DLVTUBE");
    assert_eq!(d.param("mode").unwrap().load(), 2.0);
    assert_eq!(d.param("drive").unwrap().load(), 77.0);
    assert_eq!(d.param("level").unwrap().load(), -3.0);
}

fn assert_preamp_off_after_input(strip: &Strip) {
    let block = strip.preamp_block().expect("the preamp block");
    assert_eq!(strip.blocks().iter().filter(|b| b.uid() == block.uid()).count(), 1);
    let d = preamp_params(&block);
    assert!(!d.on.load(Relaxed));
    assert_eq!(d.model_spec().unwrap().token, "DLVVALVE");
    let steps = strip.steps();
    let input = steps.iter().position(|s| matches!(s, Step::Node(Node::Input))).unwrap();
    assert!(matches!(&steps[input + 1], Step::Block(b) if b.uid() == block.uid()));
}

#[test]
fn a_show_keeps_the_preamps_settings() {
    let engine = dlive();
    dress_preamp(&engine);
    let saved = save_console(&engine, &mut NoPlugins);

    let other = dlive();
    let warnings = load_console(&other, &saved, &mut NoPlugins, true);
    assert!(warnings.is_empty(), "{warnings:?}");
    assert_dressed(&other.strip_at(StripKind::Channel, 0).unwrap());
    // Every other channel: one block, off, where it starts.
    for i in [1, 63, 123] {
        let ch = other.strip_at(StripKind::Channel, i).unwrap();
        assert_preamp_off_after_input(&ch);
        assert_eq!(ch.blocks().len(), 1);
    }
    // And it comes back once more, the same.
    let again = save_console(&other, &mut NoPlugins);
    let third = dlive();
    load_console(&third, &again, &mut NoPlugins, true);
    assert_dressed(&third.strip_at(StripKind::Channel, 0).unwrap());
}

#[test]
fn a_mixer_config_keeps_the_preamp() {
    let engine = dlive();
    dress_preamp(&engine);
    let ip5 = engine.strip_at(StripKind::Channel, 4).unwrap();
    preamp_params(&ip5.preamp_block().unwrap()).on.store(true, Relaxed);

    // New mixes: the channels stay as they were, the preamps with them.
    apply_dlive_layout(&engine, LAYOUT, &mut NoPlugins).unwrap();
    assert_dressed(&engine.strip_at(StripKind::Channel, 0).unwrap());
    assert!(preamp_params(&engine.strip_at(StripKind::Channel, 4).unwrap().preamp_block().unwrap()).on.load(Relaxed));
    assert_preamp_off_after_input(&engine.strip_at(StripKind::Channel, 1).unwrap());

    // Ip1 and Ip2 become one stereo channel: it starts on its defaults (its preamp off),
    // and what moved takes its preamp with it.
    let layout = DLiveLayout { stereo_inputs: DLiveLayout::FOH.stereo_inputs | 1, ..LAYOUT };
    apply_dlive_layout(&engine, layout, &mut NoPlugins).unwrap();
    let first = engine.strip_at(StripKind::Channel, 0).unwrap();
    assert!(first.params.stereo.load(Relaxed));
    assert_preamp_off_after_input(&first);
    assert_eq!(first.blocks().len(), 1, "the block is there once");
    let moved = engine.strip_at(StripKind::Channel, 3).unwrap();
    assert_eq!(moved.name(), "Ip5");
    assert!(preamp_params(&moved.preamp_block().unwrap()).on.load(Relaxed));
    assert_eq!(moved.blocks().len(), 1);
}

#[test]
fn a_show_without_the_preamp_blocks_loads_and_gains_them_off() {
    let engine = dlive();
    // A channel an old show gave another block of its own, after the filter.
    let ip3 = engine.strip_at(StripKind::Channel, 2).unwrap();
    let eq = engine.insert_block(
        &ip3,
        BlockKind::Eq,
        ip3.steps().iter().position(|s| matches!(s, Step::Node(Node::Filter))).unwrap() + 1,
    );
    let eq_uid = eq.unwrap().uid();
    let mut old = save_console(&engine, &mut NoPlugins);
    // A show from before the blocks: none in any channel.
    let list = old.child_mut("Channels").unwrap();
    for strip in list.children_named_mut("Strip") {
        strip.remove_children("Chain");
    }
    let list = old.child_mut("Channels").unwrap();
    let ip3 = list.children_named_mut("Strip").nth(2).unwrap();
    ip3.add_new("Chain").add_new("B").set("uid", eq_uid).set("kind", "eq").set("at", 6);

    let other = dlive();
    let warnings = load_console(&other, &old, &mut NoPlugins, true);
    assert!(warnings.is_empty(), "{warnings:?}");
    assert_preamp_off_after_input(&other.strip_at(StripKind::Channel, 0).unwrap());
    let ip3 = other.strip_at(StripKind::Channel, 2).unwrap();
    assert_preamp_off_after_input(&ip3);
    assert_eq!(ip3.blocks().len(), 2, "its own block stays, and it gains the preamp");
    assert!(ip3.blocks().iter().any(|b| b.kind() == BlockKind::Eq));
}

#[test]
fn a_cleared_show_has_its_preamps_again() {
    use livedaw_session::show::Show;
    let engine = dlive();
    dress_preamp(&engine);
    let mut show = Show::new();
    show.clear(&engine, Counts::DLIVE.channels);
    assert_eq!(engine.num_strips(StripKind::Channel), Counts::DLIVE.channels);
    assert_preamp_off_after_input(&engine.strip_at(StripKind::Channel, 0).unwrap());
}

#[test]
fn another_consoles_show_has_no_preamp() {
    let wing = Engine::new(0);
    wing.configure(&ConsoleConfig::wing());
    let saved = save_console(&wing, &mut NoPlugins);
    let other = Engine::new(0);
    other.configure(&ConsoleConfig::wing());
    assert!(load_console(&other, &saved, &mut NoPlugins, true).is_empty());
    for ch in other.channels() {
        assert!(ch.preamp_block().is_none());
        assert!(ch.blocks().is_empty(), "the template, as before");
    }
}

#[test]
fn it_refuses_a_layout_that_does_not_fit_and_other_consoles() {
    let engine = dlive();
    assert!(apply_dlive_layout(&engine, DLiveLayout { aux: 62, ..LAYOUT }, &mut NoPlugins).is_err());
    assert_eq!(engine.profile(), Profile::AhDLive(DLiveLayout::FOH), "nothing changed");
    let wing = Engine::new(0);
    wing.configure(&ConsoleConfig::wing());
    assert!(apply_dlive_layout(&wing, LAYOUT, &mut NoPlugins).is_err());
}

#[test]
fn a_main_mode_comes_and_goes_with_the_mixer_config() {
    use livedaw_engine::profile::MainMode;
    let engine = dlive();
    let lrm = DLiveLayout { main: MainMode::LrM, ..LAYOUT };
    apply_dlive_layout(&engine, lrm, &mut NoPlugins).unwrap();
    assert_eq!(engine.num_strips(StripKind::Main), 2);
    let m = engine.strip_at(StripKind::Main, 1).unwrap();
    assert_eq!(m.name(), "M");
    assert!(m.params.mono.load(Relaxed), "M is mono");
    assert!(!m.params.stereo.load(Relaxed));
    let lr = engine.strip_at(StripKind::Main, 0).unwrap();
    assert!(lr.params.stereo.load(Relaxed), "LR stays stereo");
    // On the output after the LR.
    assert_eq!(engine.output_patch().get(2), strip_source_input(StripKind::Main, 1, 0));

    let surround = DLiveLayout { main: MainMode::Surround, ..LAYOUT };
    apply_dlive_layout(&engine, surround, &mut NoPlugins).unwrap();
    assert_eq!(engine.num_strips(StripKind::Main), 4);
    assert_eq!(engine.strip_at(StripKind::Main, 3).unwrap().name(), "Ls/Rs");
    assert_eq!(engine.output_patch().get(5), strip_source_input(StripKind::Main, 3, 1));

    apply_dlive_layout(&engine, LAYOUT, &mut NoPlugins).unwrap();
    assert_eq!(engine.num_strips(StripKind::Main), 1);
    assert_ne!(engine.output_patch().get(2), strip_source_input(StripKind::Main, 1, 0), "M's output is free again");
}

#[test]
fn too_many_busses_with_pafl_are_refused() {
    let full = DLiveLayout { grp: 0, st_grp: 0, aux: 62, st_aux: 0, fx: 0, st_fx: 0, mtx: 0, st_mtx: 0, ..DLiveLayout::FOH };
    assert_eq!(full.busses_used(), 66);
    assert!(apply_dlive_layout(&dlive(), full, &mut NoPlugins).is_err());
    assert!(apply_dlive_layout(&dlive(), DLiveLayout { pafl: 0, ..full }, &mut NoPlugins).is_ok());
}

#[test]
fn a_show_keeps_a_strips_dlive_settings_and_the_consoles() {
    let engine = dlive();
    let ch = engine.strip_at(StripKind::Channel, 2).unwrap();
    ch.dlive.stereo_image.store(7, Relaxed);
    ch.dlive.image_width.store(40.0);
    ch.dlive.divergence.store(25.0);
    ch.dlive.direct_db.store(-3.0);
    ch.dlive.abcd_left[2].store(33, Relaxed);
    engine.abcd_enable(&ch, true);
    let mix = engine.strip_at(StripKind::Bus, 12).unwrap();
    engine.set_follow_main_pan(&mix, true, true);
    let group = engine.strip_at(StripKind::Bus, 0).unwrap();
    group.dlive.group_source.store(0, Relaxed);
    engine.dlive.direct_source.store(2, Relaxed);
    engine.dlive.direct_follow_all.store(true, Relaxed);
    engine.set_selector_output(3, Some((StripKind::Bus, 12)));
    engine.set_selector_source(3, 4, Some((StripKind::Channel, 7)));
    engine.dlive.selectors[3].sources[4].on_db.store(-6.0);
    engine.dlive.selectors[3].active.store(4, Relaxed);
    engine.dlive.selectors[3].fade_in_s.store(7.5);

    let console = save_console(&engine, &mut NoPlugins);
    let other = dlive();
    let warnings = load_console(&other, &console, &mut NoPlugins, true);
    assert!(warnings.is_empty(), "{warnings:?}");

    let ch = other.strip_at(StripKind::Channel, 2).unwrap();
    assert_eq!(ch.dlive.stereo_image.load(Relaxed), 7);
    assert_eq!(ch.dlive.image_width.load(), 40.0);
    assert_eq!(ch.dlive.divergence.load(), 25.0);
    assert_eq!(ch.dlive.direct_db.load(), -3.0);
    assert!(ch.dlive.abcd_on.load(Relaxed));
    assert_eq!(ch.dlive.abcd_left[2].load(Relaxed), 33);
    let mix = other.strip_at(StripKind::Bus, 12).unwrap();
    assert!(mix.dlive.follow_main_pan.load(Relaxed) && mix.dlive.follow_invert.load(Relaxed));
    assert_eq!(other.strip_at(StripKind::Bus, 0).unwrap().dlive.group_source.load(Relaxed), 0);
    assert_eq!(other.dlive.direct_source.load(Relaxed), 2);
    assert!(other.dlive.direct_follow_all.load(Relaxed));
    let sel = &other.dlive.selectors[3];
    assert_eq!(sel.destination(), Some(12));
    assert_eq!(sel.active.load(Relaxed), 4);
    assert_eq!(sel.fade_in_s.load(), 7.5);
    assert_eq!(sel.sources[4].strip(), Some((StripKind::Channel, 7)));
    assert_eq!(sel.sources[4].on_db.load(), -6.0);
    assert!(other.strip_at(StripKind::Channel, 3).unwrap().dlive.is_default(), "the others stay as new");
}

#[test]
fn a_show_keeps_the_pafl_busses_and_main_mode() {
    use livedaw_engine::profile::MainMode;
    let engine = dlive();
    let layout = DLiveLayout { pafl: 2, main: MainMode::Lcr, ..LAYOUT };
    apply_dlive_layout(&engine, layout, &mut NoPlugins).unwrap();
    let console = save_console(&engine, &mut NoPlugins);
    let other = dlive();
    load_console(&other, &console, &mut NoPlugins, true);
    assert_eq!(other.profile(), Profile::AhDLive(layout));
    assert_eq!(other.num_strips(StripKind::Main), 2);
    assert_eq!(other.strip_at(StripKind::Main, 1).unwrap().name(), "C");
}
