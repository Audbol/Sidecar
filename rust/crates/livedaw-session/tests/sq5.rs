//! An SQ-5's show: its Direct Out settings (the dLive's, with three more follows), its
//! Input to PAFL Source Point and its gates' and compressors' side chains.

use livedaw_engine::config::{ConsoleConfig, Counts};
use livedaw_engine::dlive::{direct_source, pafl_source};
use livedaw_engine::params::StripKind;
use livedaw_engine::profile::Profile;
use livedaw_engine::Engine;
use livedaw_session::console::{load_console, save_console, NoPlugins};
use std::sync::atomic::Ordering::Relaxed;

fn sq5() -> Engine {
    let engine = Engine::new(0);
    engine.configure(&ConsoleConfig { profile: Profile::AhSq5, counts: Counts::SQ5 });
    engine
}

#[test]
fn a_show_keeps_the_sqs_direct_out_pafl_and_side_chain_settings() {
    let engine = sq5();
    let c = &engine.dlive;
    c.direct_source.store(direct_source::POST_GATE, Relaxed);
    c.direct_follow_mute.store(false, Relaxed);
    c.direct_follow_dca_fader.store(true, Relaxed);
    c.direct_follow_mute_group.store(true, Relaxed);
    c.pafl_source.store(pafl_source::POST_PEQ, Relaxed);
    let ch = engine.strip_at(StripKind::Channel, 4).unwrap();
    ch.dlive.direct_db.store(-9.0);
    let gate = ch.params.gate();
    gate.sc_source.store(11, Relaxed);
    gate.sc_tap.store(5, Relaxed);
    gate.sc_filter.store(3, Relaxed);
    gate.sc_freq.store(2500.0);

    let console = save_console(&engine, &mut NoPlugins);
    let other = sq5();
    let d = &other.dlive;
    assert_eq!(d.direct_source.load(Relaxed), direct_source::POST_COMP, "a new SQ's");
    assert!(d.direct_follow_mute.load(Relaxed));
    let warnings = load_console(&other, &console, &mut NoPlugins, true);
    assert!(warnings.is_empty(), "{warnings:?}");

    assert_eq!(d.direct_source.load(Relaxed), direct_source::POST_GATE);
    assert!(!d.direct_follow_mute.load(Relaxed) && !d.direct_follow_fader.load(Relaxed));
    assert!(d.direct_follow_dca_fader.load(Relaxed) && d.direct_follow_mute_group.load(Relaxed));
    assert!(!d.direct_follow_dca_mute.load(Relaxed));
    assert_eq!(d.pafl_source.load(Relaxed), pafl_source::POST_PEQ);
    let ch = other.strip_at(StripKind::Channel, 4).unwrap();
    assert_eq!(ch.dlive.direct_db.load(), -9.0);
    let gate = ch.params.gate();
    assert_eq!((gate.sc_source.load(Relaxed), gate.sc_tap.load(Relaxed), gate.sc_filter.load(Relaxed)), (11, 5, 3));
    assert_eq!(gate.sc_freq.load(), 2500.0);
}
