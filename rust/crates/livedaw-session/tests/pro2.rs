//! A PRO2's own settings (`livedaw_engine::pro2`) in a show and in a scene.

use livedaw_engine::config::{ConsoleConfig, Counts};
use livedaw_engine::params::StripKind;
use livedaw_engine::pro2::{direct_mode, mix_mode, safe};
use livedaw_engine::profile::Profile;
use livedaw_engine::routing::DirectSource;
use livedaw_engine::sources::source_index;
use livedaw_engine::Engine;
use livedaw_session::console::{apply, capture, load_console, save_console, section, NoPlugins};
use std::sync::atomic::Ordering::Relaxed;

fn pro2() -> Engine {
    let engine = Engine::new(0);
    engine.configure(&ConsoleConfig { profile: Profile::MidasPro2, counts: Counts::PRO2 });
    engine
}

#[test]
fn a_show_keeps_the_mix_modes_links_mcas_direct_outputs_and_sis() {
    let engine = pro2();
    let aux3 = engine.strip_at(StripKind::Bus, 2).unwrap();
    engine.pro2_set_mix_mode(&aux3, mix_mode::MIX_MINUS);
    aux3.pro2.mca_db[5].store(-7.5);
    engine.pro2_set_linked(&aux3, true); // the even one takes the odd one's mode and MCAs
    let aux9 = engine.strip_at(StripKind::Bus, 8).unwrap();
    engine.pro2_set_linked(&aux9, false);
    let ch = engine.strip_at(StripKind::Channel, 4).unwrap();
    ch.pro2.direct_mode.store(direct_mode::PRE_PROCESSING, Relaxed);
    ch.pro2.direct_db.store(4.0);
    ch.pro2.direct_mute.store(true, Relaxed);
    ch.pro2.sis.store(true, Relaxed);
    ch.pro2.sis_image.store(80.0);
    let mtx = engine.strip_at(StripKind::Matrix, 1).unwrap();
    mtx.pro2.mca_db[0].store(-3.0);

    let console = save_console(&engine, &mut NoPlugins);
    let other = pro2();
    let warnings = load_console(&other, &console, &mut NoPlugins, true);
    assert!(warnings.is_empty(), "{warnings:?}");

    let (aux3, aux4) = (other.strip_at(StripKind::Bus, 2).unwrap(), other.strip_at(StripKind::Bus, 3).unwrap());
    assert_eq!(aux3.pro2.mix_mode.load(Relaxed), mix_mode::MIX_MINUS);
    assert_eq!(aux4.pro2.mix_mode.load(Relaxed), mix_mode::MIX_MINUS, "the pair takes the odd one's mode");
    assert!(aux3.pro2.linked.load(Relaxed) && aux4.pro2.linked.load(Relaxed));
    assert_eq!(aux4.pro2.mca_db[5].load(), -7.5);
    assert!(!other.strip_at(StripKind::Bus, 9).unwrap().pro2.linked.load(Relaxed), "aux 9-10 unlinked");
    let ch = other.strip_at(StripKind::Channel, 4).unwrap();
    assert_eq!(ch.pro2.direct_mode.load(Relaxed), direct_mode::PRE_PROCESSING);
    assert_eq!(ch.pro2.direct_db.load(), 4.0);
    assert!(ch.pro2.direct_mute.load(Relaxed) && ch.pro2.sis.load(Relaxed));
    assert_eq!(ch.pro2.sis_image.load(), 80.0);
    assert_eq!(other.strip_at(StripKind::Matrix, 1).unwrap().pro2.mca_db[0].load(), -3.0);
    // Untouched strips keep a new console's settings: aux 11-16 stereo groups.
    let aux11 = other.strip_at(StripKind::Bus, 10).unwrap();
    assert!(aux11.pro2.linked.load(Relaxed) && aux11.pro2.mix_mode.load(Relaxed) == mix_mode::GROUP);
}

#[test]
fn a_scene_recalls_them_with_the_sends() {
    let engine = pro2();
    let aux1 = engine.strip_at(StripKind::Bus, 0).unwrap();
    let ch = engine.strip_at(StripKind::Channel, 0).unwrap();
    let scene = capture(&engine, section::ALL, None, &mut NoPlugins);
    engine.pro2_set_mix_mode(&aux1, mix_mode::GROUP);
    aux1.pro2.mca_db[1].store(-10.0);
    ch.pro2.sis.store(true, Relaxed);
    // Not the sends: nothing of it changes.
    apply(&engine, &scene, section::ALL & !section::SENDS, None, false, None, &mut NoPlugins);
    assert_eq!(aux1.pro2.mix_mode.load(Relaxed), mix_mode::GROUP);
    apply(&engine, &scene, section::SENDS, None, false, None, &mut NoPlugins);
    assert_eq!(aux1.pro2.mix_mode.load(Relaxed), mix_mode::MIX);
    assert_eq!(aux1.pro2.mca_db[1].load(), 0.0);
    assert!(!ch.pro2.sis.load(Relaxed));
}

/// Channel 1 changed in every part a safe keeps, after `scene` was taken.
fn change_channel_1(engine: &Engine) {
    let ch = engine.strip_at(StripKind::Channel, 0).unwrap();
    let p = &ch.params;
    p.eq().on.store(!p.eq().on.load(Relaxed), Relaxed);
    p.filter().low_cut.store(!p.filter().low_cut.load(Relaxed), Relaxed);
    p.gate().on.store(!p.gate().on.load(Relaxed), Relaxed);
    p.dynamics().on.store(!p.dynamics().on.load(Relaxed), Relaxed);
    p.input.trim_db.store(6.0);
    ch.pro2.direct_db.store(-12.0);
    p.mute.store(true, Relaxed);
    ch.pro2.direct_mute.store(true, Relaxed);
    p.fader_db.store(-20.0);
    p.pan.store(0.5);
    ch.pro2.sis.store(true, Relaxed);
}

#[test]
fn the_channel_safes_keep_their_parts_through_a_recall() {
    let engine = pro2();
    let ch = engine.strip_at(StripKind::Channel, 0).unwrap();
    let p = &ch.params;
    let scene = capture(&engine, section::ALL, None, &mut NoPlugins);
    let (eq, low_cut, gate, comp) =
        (p.eq().on.load(Relaxed), p.filter().low_cut.load(Relaxed), p.gate().on.load(Relaxed), p.dynamics().on.load(Relaxed));
    let fader = p.fader_db.load();

    // EQ and MUTE: the EQ, the filters and the mutes stay; the dynamics and the fader are the scene's.
    change_channel_1(&engine);
    ch.pro2.safes.store(safe::EQ | safe::MUTE, Relaxed);
    apply(&engine, &scene, section::ALL, None, true, None, &mut NoPlugins);
    assert_eq!(p.eq().on.load(Relaxed), !eq);
    assert_eq!(p.filter().low_cut.load(Relaxed), !low_cut);
    assert!(p.mute.load(Relaxed) && ch.pro2.direct_mute.load(Relaxed));
    assert_eq!((p.gate().on.load(Relaxed), p.dynamics().on.load(Relaxed)), (gate, comp));
    assert_eq!(p.fader_db.load(), fader);
    assert_eq!(p.input.trim_db.load(), 0.0);
    assert_eq!(ch.pro2.safes.load(Relaxed), safe::EQ | safe::MUTE, "a recall leaves the safes");

    // DYN, MIC and FADER: the other way round.
    change_channel_1(&engine);
    ch.pro2.safes.store(safe::DYN | safe::MIC | safe::FADER, Relaxed);
    apply(&engine, &scene, section::ALL, None, true, None, &mut NoPlugins);
    assert_eq!((p.gate().on.load(Relaxed), p.dynamics().on.load(Relaxed)), (!gate, !comp));
    assert_eq!((p.input.trim_db.load(), ch.pro2.direct_db.load()), (6.0, -12.0));
    assert_eq!((p.fader_db.load(), p.pan.load()), (-20.0, 0.5));
    assert!(ch.pro2.sis.load(Relaxed));
    assert_eq!(p.eq().on.load(Relaxed), eq);
    assert!(!p.mute.load(Relaxed) && !ch.pro2.direct_mute.load(Relaxed));

    // Not respecting safes (a RESTORE, NO GLOB): all of it is the scene's.
    change_channel_1(&engine);
    apply(&engine, &scene, section::ALL, None, false, None, &mut NoPlugins);
    assert_eq!((p.input.trim_db.load(), p.fader_db.load()), (0.0, fader));
    assert!(!ch.pro2.sis.load(Relaxed));
}

#[test]
fn a_mic_safe_input_keeps_its_source_and_its_direct_outputs_patch() {
    let engine = pro2();
    let ch = engine.strip_at(StripKind::Channel, 0).unwrap();
    let source = source_index(ch.params.input_left.load(Relaxed)).expect("channel 1 hears a source");
    let gain = &engine.sources().get(source).unwrap().gain_db;
    let direct = DirectSource { kind: StripKind::Channel, number: 1, side: 0, post: false }.code();
    let other = DirectSource { kind: StripKind::Channel, number: 2, side: 0, post: false }.code();
    let port = |i: usize| engine.output_patch().ports[i].load(Relaxed);
    engine.output_patch().set(3, direct);
    engine.output_patch().set(5, -1);
    let scene = capture(&engine, section::ALL, None, &mut NoPlugins);
    let scene_4 = port(4);
    gain.store(gain.load() + 10.0);
    engine.output_patch().set(3, -1);
    engine.output_patch().set(4, direct);
    engine.output_patch().set(5, other);
    let changed = gain.load();

    ch.pro2.safes.store(safe::MIC, Relaxed);
    apply(&engine, &scene, section::ALL, None, true, None, &mut NoPlugins);
    assert_eq!(gain.load(), changed, "the gain is the input's own");
    assert_eq!((port(3), port(4)), (-1, direct), "the direct output stays where it was patched");
    assert_eq!(port(5), -1, "another channel's direct output is the scene's");

    ch.pro2.safes.store(0, Relaxed);
    apply(&engine, &scene, section::ALL, None, true, None, &mut NoPlugins);
    assert_ne!(gain.load(), changed);
    assert_eq!((port(3), port(4)), (direct, scene_4));
}

#[test]
fn an_aux_sends_fader_safe_keeps_its_mode_and_trim() {
    let engine = pro2();
    let aux1 = engine.strip_at(StripKind::Bus, 0).unwrap();
    let scene = capture(&engine, section::ALL, None, &mut NoPlugins);
    engine.pro2_set_mix_mode(&aux1, mix_mode::GROUP);
    aux1.params.input.trim_db.store(-4.0);
    aux1.params.fader_db.store(-9.0);
    aux1.params.mute.store(true, Relaxed);
    aux1.pro2.safes.store(safe::FADER, Relaxed);
    apply(&engine, &scene, section::ALL, None, true, None, &mut NoPlugins);
    assert_eq!(aux1.pro2.mix_mode.load(Relaxed), mix_mode::GROUP);
    assert_eq!((aux1.params.input.trim_db.load(), aux1.params.fader_db.load()), (-4.0, -9.0));
    assert!(!aux1.params.mute.load(Relaxed), "the mute is MUTE safe's");
}

#[test]
fn a_show_keeps_the_safes() {
    let engine = pro2();
    let ch = engine.strip_at(StripKind::Channel, 6).unwrap();
    ch.pro2.safes.store(safe::MIC | safe::FADER, Relaxed);
    ch.params.recall_safe.store(true, Relaxed);
    let console = save_console(&engine, &mut NoPlugins);
    let other = pro2();
    assert!(load_console(&other, &console, &mut NoPlugins, true).is_empty());
    let ch = other.strip_at(StripKind::Channel, 6).unwrap();
    assert_eq!(ch.pro2.safes.load(Relaxed), safe::MIC | safe::FADER);
    assert!(ch.params.recall_safe.load(Relaxed));
    assert_eq!(other.strip_at(StripKind::Channel, 5).unwrap().pro2.safes.load(Relaxed), 0);
}
