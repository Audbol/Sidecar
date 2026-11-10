//! A XENYX 1204USB console saved as a show scene and recalled.

use livedaw_engine::{ConsoleConfig, Counts, Engine, Profile, StripKind};
use livedaw_session::console::NoPlugins;
use livedaw_session::Show;
use std::sync::atomic::Ordering::Relaxed;

#[test]
fn a_1204usb_console_is_captured_in_a_scene_and_recalled() {
    let engine = Engine::new(0);
    engine.configure(&ConsoleConfig { profile: Profile::Xenyx1204, counts: Counts::XENYX1204 });
    let mut show = Show::new();
    let ch = engine.strips(StripKind::Channel)[0].clone();
    ch.params.fader_db.store(-10.0);
    ch.set_name("KICK");
    assert_eq!(show.add_scene(&engine, &mut NoPlugins, "Verse", -1), 0);
    ch.params.fader_db.store(-20.0);
    ch.set_name("SNARE");
    show.recall_scene(&engine, &mut NoPlugins, 0);
    assert_eq!(ch.params.fader_db.load(), -10.0);
    assert_eq!(ch.name(), "KICK");
    let _ = Relaxed;
}
