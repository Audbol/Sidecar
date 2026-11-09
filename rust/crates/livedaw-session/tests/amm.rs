//! A dLive's Automatic Mic Mixer in shows and scenes.

use livedaw_engine::config::{ConsoleConfig, Counts};
use livedaw_engine::params::StripKind;
use livedaw_engine::profile::{DLiveLayout, Profile};
use livedaw_engine::Engine;
use livedaw_session::console::{self, section};
use livedaw_session::xml;
use livedaw_session::NoPlugins;
use std::sync::atomic::Ordering::Relaxed;

fn dlive() -> Engine {
    let engine = Engine::new(0);
    engine.configure(&ConsoleConfig { profile: Profile::AhDLive(DLiveLayout::FOH), counts: Counts::DLIVE });
    engine
}

fn set_up(e: &Engine) {
    let a = &e.amm;
    a.mode.store(1, Relaxed);
    a.count.store(2, Relaxed);
    a.off_atten_db.store(-55.0);
    a.on_hold_s.store(3.5);
    a.nom_atten_db.store(4.5);
    a.chair_sensitivity.store(7.0);
    a.chair_duck_db.store(-12.0);
    a.key_hpf_hz.store(180.0);
    a.key_lpf_hz.store(8000.0);
    a.best_mic[1].store(true, Relaxed);
    a.solo[0].store(true, Relaxed);
    let ch = e.strip_at(StripKind::Channel, 2).unwrap();
    ch.amm.amm.store(1, Relaxed);
    ch.amm.on.store(false, Relaxed);
    ch.amm.chair.store(true, Relaxed);
    ch.amm.solo.store(true, Relaxed);
    ch.amm.priority_db.store(-7.5);
}

fn assert_set_up(e: &Engine) {
    let a = &e.amm;
    assert_eq!(a.mode.load(Relaxed), 1);
    assert_eq!(a.count.load(Relaxed), 2);
    assert_eq!(a.off_atten_db.load(), -55.0);
    assert_eq!(a.on_hold_s.load(), 3.5);
    assert_eq!(a.nom_atten_db.load(), 4.5);
    assert_eq!(a.chair_sensitivity.load(), 7.0);
    assert_eq!(a.chair_duck_db.load(), -12.0);
    assert_eq!(a.key_hpf_hz.load(), 180.0);
    assert_eq!(a.key_lpf_hz.load(), 8000.0);
    assert!(a.best_mic[1].load(Relaxed) && !a.best_mic[0].load(Relaxed));
    assert!(a.solo[0].load(Relaxed) && !a.solo[1].load(Relaxed));
    let ch = e.strip_at(StripKind::Channel, 2).unwrap();
    assert_eq!(ch.amm.amm.load(Relaxed), 1);
    assert!(!ch.amm.on.load(Relaxed));
    assert!(ch.amm.chair.load(Relaxed));
    assert!(ch.amm.solo.load(Relaxed));
    assert_eq!(ch.amm.priority_db.load(), -7.5);
}

#[test]
fn a_show_keeps_the_amm() {
    let engine = dlive();
    set_up(&engine);
    let text = console::save_console(&engine, &mut NoPlugins).to_document();
    let saved = xml::parse(&text).unwrap();

    let other = dlive();
    let warnings = console::load_console(&other, &saved, &mut NoPlugins, false);
    assert!(warnings.is_empty(), "{warnings:?}");
    assert_set_up(&other);
    let untouched = other.strip_at(StripKind::Channel, 3).unwrap();
    assert_eq!(untouched.amm.amm.load(Relaxed), -1);
    assert!(untouched.amm.on.load(Relaxed), "On is the default");
}

#[test]
fn a_scene_recalls_the_amm_with_inserts() {
    let engine = dlive();
    set_up(&engine);
    let scene = console::capture(&engine, section::ALL, None, &mut NoPlugins);

    let ch = engine.strip_at(StripKind::Channel, 2).unwrap();
    ch.amm.amm.store(-1, Relaxed);
    ch.amm.priority_db.store(5.0);
    engine.amm.mode.store(0, Relaxed);
    engine.amm.off_atten_db.store(-20.0);
    engine.amm.best_mic[1].store(false, Relaxed);

    console::apply(&engine, &scene, section::ALL, None, false, None, &mut NoPlugins);
    assert_set_up(&engine);

    // A scene without the INSERTS section leaves it alone.
    ch.amm.priority_db.store(5.0);
    engine.amm.off_atten_db.store(-20.0);
    console::apply(&engine, &scene, section::ALL & !section::INSERTS, None, false, None, &mut NoPlugins);
    assert_eq!(ch.amm.priority_db.load(), 5.0);
    assert_eq!(engine.amm.off_atten_db.load(), -20.0);
}

#[test]
fn a_recalled_scene_takes_a_channel_out_of_its_amm() {
    let engine = dlive();
    let scene = console::capture(&engine, section::ALL, None, &mut NoPlugins);
    let ch = engine.strip_at(StripKind::Channel, 5).unwrap();
    ch.amm.amm.store(0, Relaxed);
    console::apply(&engine, &scene, section::ALL, None, false, None, &mut NoPlugins);
    assert_eq!(ch.amm.amm.load(Relaxed), -1, "this channel wasn't in an AMM in the scene");
}

#[test]
fn an_old_dlive_show_loads_with_the_amm_as_it_was() {
    // A show saved before the AMM: without its elements.
    let engine = dlive();
    let mut saved = console::save_console(&engine, &mut NoPlugins);
    saved.remove_children("Amm");
    for tag in ["Channels", "Aux", "Buses", "Mains", "Matrices"] {
        for list in saved.children_named_mut(tag) {
            for strip in list.children_named_mut("Strip") {
                if let Some(ins) = strip.child_mut("Ins") {
                    ins.remove_children("Amm");
                }
            }
        }
    }
    let text = saved.to_document();
    assert!(!text.contains("<Amm"), "really without them");

    let other = dlive();
    let warnings = console::load_console(&other, &xml::parse(&text).unwrap(), &mut NoPlugins, false);
    assert!(warnings.is_empty(), "{warnings:?}");
    assert_eq!(other.amm.mode.load(Relaxed), 0);
    assert_eq!(other.amm.off_atten_db.load(), -40.0);
    let ch = other.strip_at(StripKind::Channel, 0).unwrap();
    assert_eq!(ch.amm.amm.load(Relaxed), -1);
    assert!(ch.amm.on.load(Relaxed));
}

#[test]
fn another_console_writes_no_amm_and_ignores_one() {
    let wing = Engine::new(0);
    wing.set_num_channels(2);
    let text = console::save_console(&wing, &mut NoPlugins).to_document();
    assert!(!text.contains("<Amm"), "a WING's show is as it was");
    let scene = console::capture(&wing, section::ALL, None, &mut NoPlugins).to_document();
    assert!(!scene.contains("<Amm"), "a WING's scene is as it was");
}

#[test]
fn a_new_console_starts_clean() {
    let engine = dlive();
    set_up(&engine);
    engine.reset();
    assert_eq!(engine.amm.mode.load(Relaxed), 0);
    assert_eq!(engine.amm.count.load(Relaxed), 1);
    assert_eq!(engine.amm.off_atten_db.load(), -40.0);
    assert!(!engine.amm.best_mic[1].load(Relaxed));
}
