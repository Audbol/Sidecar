//! A dLive's NEQ-12 in a show: the block, its model and every one of its 51
//! values through a save and a load, and shows of the older, smaller EQs
//! coming back just as they were.

use livedaw_dsp::model::{model_index, ModelSpec, ParamKind};
use livedaw_engine::blocks::{Block, BlockKind, BlockParams};
use livedaw_engine::config::{ConsoleConfig, Counts};
use livedaw_engine::params::{EqBlockParams, StripKind};
use livedaw_engine::profile::{dlive, DLiveLayout, Profile};
use livedaw_engine::Engine;
use livedaw_session::console::{load_console, save_console, NoPlugins};
use livedaw_session::refs::StripRef;
use livedaw_session::xml::{Element, Node};
use std::sync::atomic::Ordering::Relaxed;
use std::sync::Arc;

fn dlive_engine() -> Engine {
    let engine = Engine::new(0);
    engine.configure(&ConsoleConfig { profile: Profile::AhDLive(DLiveLayout::FOH), counts: Counts::DLIVE });
    engine
}

/// The factory NEQ block of Aux 1.
fn neq_block(engine: &Engine) -> Arc<Block> {
    let strip = engine.strip_at(StripKind::Bus, dlive::AUX).unwrap();
    strip.blocks().into_iter().find(|b| b.kind() == BlockKind::Eq).expect("the mix's NEQ block")
}

fn eq_of(block: &Block) -> &EqBlockParams {
    let BlockParams::Eq(eq) = &block.params else { panic!("an EQ block") };
    eq
}

/// A value for parameter `i` away from its default, within its range.
fn distinct_value(spec: &ModelSpec, i: usize) -> f32 {
    let p = &spec.params[i];
    let x = match p.kind {
        ParamKind::Toggle => 1.0 - p.default,
        _ => p.from_normalised(0.13 + 0.31 * (i % 3) as f32),
    };
    assert_ne!(x, p.default, "{}: a change that a default-looking load would miss", p.id);
    x
}

/// The saved element of Aux 1's NEQ block.
fn saved_block<'a>(console: &'a mut Element, r: &str) -> &'a mut Element {
    let strip = console.child_mut("Buses").unwrap().children_named_mut("Strip").find(|s| s.str_or("ref", "") == r).unwrap();
    strip.child_mut("Chain").unwrap().children_named_mut("B").find(|b| b.attr("kind") == Some("eq")).unwrap()
}

#[test]
fn an_neq12_block_keeps_its_model_and_every_value_through_a_show() {
    let engine = dlive_engine();
    let block = neq_block(&engine);
    let eq = eq_of(&block);
    let model = model_index(eq.table(), "DLVNEQ12").expect("the NEQ-12 is one of the block's models");
    eq.set_model(model);
    eq.on.store(true, Relaxed);
    eq.mix.store(87.5);
    let spec = eq.model_spec().unwrap();
    assert_eq!(spec.params.len(), 51);
    for i in 0..spec.params.len() {
        eq.values[i].store(distinct_value(spec, i));
    }

    let saved = save_console(&engine, &mut NoPlugins);
    let other = dlive_engine();
    let warnings = load_console(&other, &saved, &mut NoPlugins, true);
    assert!(warnings.is_empty(), "{warnings:?}");

    let block = neq_block(&other);
    let back = eq_of(&block);
    assert_eq!(back.model_spec().unwrap().token, "DLVNEQ12");
    assert!(back.on.load(Relaxed));
    assert_eq!(back.mix.load(), 87.5);
    for (i, p) in spec.params.iter().enumerate() {
        let want = distinct_value(spec, i);
        assert!(
            (back.values[i].load() - want).abs() <= want.abs().max(1.0) * 1e-5,
            "{}: {} vs {want}",
            p.id,
            back.values[i].load()
        );
    }
}

#[test]
fn a_mix_geq_saved_before_the_neq12_loads_as_it_was() {
    let engine = dlive_engine();
    let block = neq_block(&engine);
    let eq = eq_of(&block);
    eq.on.store(true, Relaxed);
    eq.set_model(model_index(eq.table(), "DLVGEQH").unwrap());
    let spec = eq.model_spec().unwrap();
    assert_eq!(spec.params.len(), 28);
    for i in 0..spec.params.len() {
        eq.values[i].store(i as f32 / 2.0 - 7.0);
    }
    let strip_ref = StripRef::of(&engine.strip_at(StripKind::Bus, dlive::AUX).unwrap()).to_string();

    let mut saved = save_console(&engine, &mut NoPlugins);
    // What an older show holds: the model by name and exactly its parameters, by name.
    let written = saved_block(&mut saved, &strip_ref);
    assert_eq!(written.attr("mdl"), Some("DLVGEQH"));
    assert_eq!(written.children_named("P").count(), 28, "the model's own parameters, none for spare slots");
    assert!(written.children_named("P").all(|p| spec.param_index(p.str_or("n", "")).is_some()));

    // An older show that lists only some parameters: the others keep their defaults.
    let listed = 7;
    written.children.retain(|n| !matches!(n, Node::Element(p) if p.name == "P" && p.attr("n") != Some(spec.params[listed].id)));
    assert_eq!(written.children_named("P").count(), 1);

    let other = dlive_engine();
    let warnings = load_console(&other, &saved, &mut NoPlugins, true);
    assert!(warnings.is_empty(), "{warnings:?}");
    let back_block = neq_block(&other);
    let back = eq_of(&back_block);
    assert_eq!(back.model_spec().unwrap().token, "DLVGEQH");
    assert_eq!(back.values[listed].load(), listed as f32 / 2.0 - 7.0, "the one the show lists");
    for i in (0..spec.params.len()).filter(|&i| i != listed) {
        assert_eq!(back.values[i].load(), spec.params[i].default, "{}: not listed, so its default", spec.params[i].id);
    }
}

#[test]
fn a_wing_shows_eq_comes_back_value_for_value_with_nothing_spare_written() {
    let engine = Engine::new(0);
    let strip = engine.strip_at(StripKind::Bus, 0).unwrap();
    let eq = strip.params.eq();
    let spec = eq.model_spec().unwrap();
    assert!(spec.params.len() <= 32, "a model every older show held: {}", spec.params.len());
    for i in 0..spec.params.len() {
        eq.values[i].store(distinct_value(spec, i));
    }
    let saved = save_console(&engine, &mut NoPlugins);
    let other = Engine::new(0);
    assert!(load_console(&other, &saved, &mut NoPlugins, true).is_empty());
    let back = other.strip_at(StripKind::Bus, 0).unwrap();
    let back_eq = back.params.eq();
    assert_eq!(back_eq.model_spec().unwrap().token, spec.token);
    for (i, p) in spec.params.iter().enumerate() {
        assert_eq!(back_eq.values[i].load(), eq.values[i].load(), "{}", p.id);
    }
    // The slots past the model's are never written, and hold nothing.
    for v in &back_eq.values[spec.params.len()..] {
        assert_eq!(v.load(), 0.0);
    }
}
