//! A dLive's RackUltra strips, Dyn8 engines, key sources and Global Key in a show
//! and in a scene (`livedaw_engine::ufx`).

use livedaw_dsp::dlive_fx::ultra::strip::STRIP;
use livedaw_dsp::model::model_index;
use livedaw_engine::config::{ConsoleConfig, Counts};
use livedaw_engine::keytap::KeySource;
use livedaw_engine::params::StripKind;
use livedaw_engine::profile::{dlive, DLiveLayout, Profile};
use livedaw_engine::routing::strip_source_input;
use livedaw_engine::ufx::{DYN8_FX_BASE, UFX_FIRST};
use livedaw_engine::Engine;
use livedaw_session::console::{self, load_console, save_console, section, NoPlugins};
use livedaw_session::scope::{self, Scope};
use livedaw_session::xml::Node;
use std::sync::atomic::Ordering::Relaxed;

fn dlive_engine() -> Engine {
    let engine = Engine::new(0);
    engine.configure(&ConsoleConfig { profile: Profile::AhDLive(DLiveLayout::FOH), counts: Counts::DLIVE });
    engine
}

const SLOT: usize = UFX_FIRST + 2;
const DYN8: usize = 5;

/// A console with something of everything set.
fn set_up(engine: &Engine) {
    let fx = engine.fx_models();
    assert!(engine.set_fx_model(SLOT, model_index(fx, "DLVTUNER").unwrap(), true));
    engine.controls.fx[SLOT].param_in(fx, "glob").unwrap().store(1.0);
    // Every value of its strip away from its default.
    let strip = engine.ufx.strip_of(SLOT).unwrap();
    for (i, p) in STRIP.params.iter().enumerate() {
        assert!(strip.set_value(i, p.from_normalised(0.3 + 0.1 * (i % 4) as f32)));
    }
    engine.fx_key(SLOT).unwrap().set(KeySource::Strip(StripKind::Bus, dlive::AUX + 1), 2);
    // FX 24 fed from FX send 8.
    let bus = strip_source_input(StripKind::Bus, dlive::FX + 7, 0);
    engine.controls.fx[UFX_FIRST + 7].source_left.store(bus, Relaxed);
    // Dyn8 6 on Ip 3's insert, keyed from Ip 9; Dyn8 20 set and not inserted.
    let ch = engine.channel_at(2).unwrap();
    ch.params.pre_insert().fx.store((DYN8_FX_BASE + DYN8) as i32, Relaxed);
    ch.params.pre_insert().on.store(true, Relaxed);
    let p = engine.fx_params(DYN8_FX_BASE + DYN8).unwrap();
    p.param_in(fx, "mt_2").unwrap().store(-31.0);
    p.param_in(fx, "mk_2").unwrap().store(2.0);
    p.mix.store(80.0);
    engine.fx_key(DYN8_FX_BASE + DYN8).unwrap().set(KeySource::Channel(8), 5);
    engine.fx_params(DYN8_FX_BASE + 19).unwrap().param_in(fx, "ef_3").unwrap().store(3000.0);
    engine.fx_params(DYN8_FX_BASE + 19).unwrap().bypass.store(true, Relaxed);
    engine.ufx.global_key.set(7, 1);
}

fn check(engine: &Engine, what: &str) {
    let fx = engine.fx_models();
    assert_eq!(fx[engine.controls.fx[SLOT].model_index_in(fx)].token, "DLVTUNER", "{what}");
    assert_eq!(engine.controls.fx[SLOT].param_in(fx, "glob").unwrap().load(), 1.0, "{what}");
    let strip = engine.ufx.strip_of(SLOT).unwrap();
    for (i, p) in STRIP.params.iter().enumerate() {
        let want = p.sanitise(p.from_normalised(0.3 + 0.1 * (i % 4) as f32));
        assert!((strip.values[i].load() - want).abs() <= 1e-4 * want.abs().max(1.0), "{what}: strip value {}", p.id);
    }
    assert_eq!(engine.fx_key(SLOT).unwrap().get().0, KeySource::Strip(StripKind::Bus, dlive::AUX + 1), "{what}");
    assert_eq!(engine.fx_key(SLOT).unwrap().tap.load(Relaxed), 2, "{what}");
    assert!(engine.controls.fx[UFX_FIRST + 7].is_send(), "{what}");
    let p = engine.fx_params(DYN8_FX_BASE + DYN8).unwrap();
    assert_eq!(fx[p.model_index_in(fx)].token, "DLVDYN8", "{what}");
    assert_eq!(p.param_in(fx, "mt_2").unwrap().load(), -31.0, "{what}");
    assert_eq!(p.param_in(fx, "mk_2").unwrap().load(), 2.0, "{what}");
    assert_eq!(p.mix.load(), 80.0, "{what}");
    assert_eq!(engine.fx_key(DYN8_FX_BASE + DYN8).unwrap().get().0, KeySource::Channel(8), "{what}");
    assert_eq!(engine.fx_key(DYN8_FX_BASE + DYN8).unwrap().tap.load(Relaxed), 5, "{what}");
    let other = engine.fx_params(DYN8_FX_BASE + 19).unwrap();
    assert_eq!(other.param_in(fx, "ef_3").unwrap().load(), 3000.0, "{what}");
    assert!(other.bypass.load(Relaxed), "{what}");
    assert_eq!((engine.ufx.global_key.key(), engine.ufx.global_key.scale()), (7, 1), "{what}");
}

/// Everything `set_up` set, somewhere else.
fn disturb(engine: &Engine) {
    let fx = engine.fx_models();
    engine.controls.fx[SLOT].param_in(fx, "glob").unwrap().store(0.0);
    engine.ufx.strip_of(SLOT).unwrap().reset();
    engine.fx_key(SLOT).unwrap().reset();
    let p = engine.fx_params(DYN8_FX_BASE + DYN8).unwrap();
    p.param_in(fx, "mt_2").unwrap().store(3.0);
    p.param_in(fx, "mk_2").unwrap().store(0.0);
    p.mix.store(100.0);
    engine.fx_key(DYN8_FX_BASE + DYN8).unwrap().set(KeySource::Channel(1), 0);
    engine.fx_params(DYN8_FX_BASE + 19).unwrap().param_in(fx, "ef_3").unwrap().store(500.0);
    engine.fx_params(DYN8_FX_BASE + 19).unwrap().bypass.store(false, Relaxed);
    engine.ufx.global_key.set(0, 0);
}

#[test]
fn a_show_keeps_the_ufx_strips_the_dyn8_engines_the_keys_and_the_global_key() {
    let engine = dlive_engine();
    set_up(&engine);
    check(&engine, "as set");
    let console = save_console(&engine, &mut NoPlugins);
    let fx = console.child("Fx").unwrap();
    assert_eq!(fx.children_named("Slot").count(), 24);
    assert_eq!(fx.children_named("Dyn8").count(), 2, "the one inserted and the one changed");
    assert_eq!(fx.children_named("Slot").filter(|s| s.child("Ufx").is_some()).count(), 8);

    let other = Engine::new(0);
    let warnings = load_console(&other, &console, &mut NoPlugins, true);
    assert!(warnings.is_empty(), "{warnings:?}");
    check(&other, "loaded");
    assert_eq!(other.num_fx(), 24);
    let ch = other.channel_at(2).unwrap();
    assert_eq!(ch.params.pre_insert().fx.load(Relaxed), (DYN8_FX_BASE + DYN8) as i32);
    other.collect();
    assert!(other.fx(DYN8_FX_BASE + DYN8).unwrap().has_device(), "the inserted Dyn8 runs");
    assert!(!other.fx(DYN8_FX_BASE + 19).unwrap().has_device());
    // An engine the show doesn't list is a new console's.
    let untouched = other.fx_params(DYN8_FX_BASE).unwrap();
    assert_eq!(untouched.param_in(other.fx_models(), "mr_1").unwrap().load(), 2.4);
    assert!(other.fx_key(DYN8_FX_BASE).unwrap().source.load(Relaxed) < 0);
}

#[test]
fn a_scene_recalls_them_with_the_fx() {
    let engine = dlive_engine();
    set_up(&engine);
    let scene = console::capture(&engine, section::ALL, None, &mut NoPlugins);
    disturb(&engine);
    console::apply(&engine, &scene, section::ALL, None, false, None, &mut NoPlugins);
    check(&engine, "recalled");

    // Without the INSERTS section nothing of the FX is recalled.
    disturb(&engine);
    console::apply(&engine, &scene, section::ALL & !section::INSERTS, None, false, None, &mut NoPlugins);
    assert_eq!(engine.ufx.global_key.key(), 0);
    assert!(engine.fx_key(SLOT).unwrap().source.load(Relaxed) < 0);

    // A scope with one FX slot recalls that slot's strip and key; the Dyn8 engines
    // and the Global Key go with any FX in the scope.
    let scoped = Scope { fx: 1 << SLOT, ..Scope::default() };
    scope::apply_scoped(&engine, &scene, &scoped, None, false, None, &mut NoPlugins);
    check(&engine, "recalled in a scope of one FX");
    disturb(&engine);
    let none = Scope { fx: 0, ..Scope::default() };
    scope::apply_scoped(&engine, &scene, &none, None, false, None, &mut NoPlugins);
    assert_eq!(engine.ufx.global_key.key(), 0, "no FX in the scope");
    assert_eq!(engine.fx_params(DYN8_FX_BASE + DYN8).unwrap().mix.load(), 100.0);
}

#[test]
fn another_consoles_show_has_none_of_it_and_takes_a_key_source() {
    let engine = Engine::new(0);
    engine.configure(&ConsoleConfig::wing());
    let console = save_console(&engine, &mut NoPlugins);
    let fx = console.child("Fx").unwrap();
    assert!(!fx.has_attr("gkey") && fx.children_named("Dyn8").count() == 0);
    assert!(fx.children_named("Slot").all(|s| s.child("Ufx").is_none() && !s.has_attr("key")));

    // A key source and the Global Key are any console's.
    engine.fx_key(3).unwrap().set(KeySource::Channel(4), 5);
    engine.ufx.global_key.set(2, 2);
    let console = save_console(&engine, &mut NoPlugins);
    let other = Engine::new(0);
    load_console(&other, &console, &mut NoPlugins, true);
    assert_eq!(other.fx_key(3).unwrap().get().0, KeySource::Channel(4));
    assert_eq!((other.ufx.global_key.key(), other.ufx.global_key.scale()), (2, 2));
    assert!(other.fx_key(2).unwrap().source.load(Relaxed) < 0);
}

#[test]
fn a_show_from_before_the_rackultra_engines_is_still_a_dlive() {
    let engine = dlive_engine();
    let mut console = save_console(&engine, &mut NoPlugins);
    // As it was saved then: 16 FX, no strips, no Dyn8 engines, no Global Key.
    console.child_mut("Config").unwrap().set("fx", 16);
    let fx = console.child_mut("Fx").unwrap();
    fx.attrs.retain(|(name, _)| name != "gkey" && name != "gscale");
    fx.children.retain(|c| match c {
        Node::Element(e) => e.name == "Slot" && e.int_or("i", 0) < 16,
        _ => true,
    });
    let other = Engine::new(0);
    let warnings = load_console(&other, &console, &mut NoPlugins, true);
    assert!(warnings.is_empty(), "{warnings:?}");
    assert_eq!(other.profile(), Profile::AhDLive(DLiveLayout::FOH));
    assert_eq!((other.num_fx(), other.config().counts.fx), (24, 24));
    assert!(other.ufx.live() && other.ufx.strip_of(SLOT).is_some());
    assert_eq!(other.ufx.strip_of(SLOT).unwrap().param("width").unwrap().load(), 100.0);
}
