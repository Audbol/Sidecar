//! Another console's models on a console's blocks (`ProfileSpec::run_models`,
//! `docs/UNIVERSAL_MIXER.md`, "Foreign models").

use livedaw_dsp::model::model_index;
use livedaw_engine::config::{ConsoleConfig, Counts};
use livedaw_engine::params::StripKind;
use livedaw_engine::profile::{DynSlot, Profile};
use livedaw_engine::Engine;

fn engine(profile: Profile, counts: Counts) -> Engine {
    let e = Engine::new(0);
    e.configure(&ConsoleConfig { profile, counts });
    e
}

#[test]
fn a_promix_eq_swaps_to_the_wings_and_back() {
    let e = engine(Profile::ProMix01, Counts::PROMIX01);
    let ch = e.strips(StripKind::Channel)[0].clone();
    let eq = ch.params.eq();
    let own = Profile::ProMix01.spec().models.eq_channel;
    assert_eq!(&eq.table()[..own.len()], own, "its own models keep their indices");
    assert_eq!(eq.model_spec().unwrap().token, "PMEQ");
    let wing = model_index(eq.table(), "STD").expect("the WING's EQ is offered");
    assert!(wing >= own.len(), "a foreign model sits after the own ones");
    eq.set_model(wing);
    assert_eq!(eq.model_spec().unwrap().token, "STD");
    eq.set_model(model_index(eq.table(), "PMEQ").unwrap());
    assert_eq!(eq.model_spec().unwrap().token, "PMEQ");
}

#[test]
fn dynamics_and_fx_of_other_consoles_are_accepted() {
    let e = engine(Profile::X32, Counts::X32);
    let spec = Profile::X32.spec();
    assert!(!spec.is_own_dynamics("COMP"), "the WING's is foreign on an X32");
    assert!(spec.may_hold(DynSlot::Dynamics, "COMP"));
    let ch = e.strips(StripKind::Channel)[0].clone();
    let d = ch.params.dynamics();
    d.set_model(model_index(d.table(), "COMP").unwrap());
    assert_eq!(d.model_spec().unwrap().token, "COMP");
    let fx = e.fx_models();
    let own = spec.models.fx;
    assert_eq!(&fx[..own.len()], own);
    let plate = model_index(fx, "PLATE").expect("a WING reverb");
    assert!(plate >= own.len());
    assert!(
        e.set_fx_model(0, plate, true)
            == (fx[plate].category != livedaw_dsp::model::ModelCategory::FxPremium || spec.models.fx_premium_slots > 0)
    );
}

#[test]
fn a_promix_takes_a_wing_gate_and_the_universal_keeps_its_tables() {
    let spec = Profile::ProMix01.spec();
    assert!(spec.may_hold(DynSlot::Gate, "GATE"), "a ProMix takes a WING gate");
    let u = Profile::Universal.spec();
    assert!(std::ptr::eq(u.run_models(), u.models), "the Universal has them all already");
}
