//! OSC over MIDI SysEx: the text commands an X32 takes in a SysEx message.
use livedaw_engine::{ConsoleConfig, Counts, Engine, Profile, StripKind};
use livedaw_x32::map::State;
use livedaw_x32::sysex::{command, Done};
use livedaw_x32::Console;

fn x32() -> Engine {
    let engine = Engine::new(0);
    engine.configure(&ConsoleConfig { profile: Profile::X32, counts: Counts::X32 });
    engine
}

#[test]
fn a_text_command_sets_values_and_an_address_alone_answers() {
    let engine = x32();
    let state = State::default();
    let c = Console::new(&engine, &state);
    // The document's examples: a leaf by name, a leaf in real units.
    assert_eq!(command(&c, "/ch/01/mix/on OFF"), Done::Set(1));
    assert_eq!(command(&c, "/ch/01/mix/fader -10.0"), Done::Set(1));
    let ch1 = &engine.strips(StripKind::Channel)[0];
    assert!(ch1.params.mute.load(std::sync::atomic::Ordering::Relaxed));
    assert!((ch1.params.fader_db.load() + 10.0).abs() < 0.05, "{}", ch1.params.fader_db.load());
    assert_eq!(command(&c, "/ch/01/mix/on"), Done::Answer("/ch/01/mix/on OFF".into()));
    // A node: its values in order, fewer than all set the first ones.
    assert_eq!(command(&c, "/ch/02/config \"Snare\" 1 GN"), Done::Set(3));
    assert_eq!(command(&c, " /ch/02/config "), Done::Answer("/ch/02/config \"Snare\" 1 GN 2".into()));
    assert_eq!(engine.strips(StripKind::Channel)[1].name(), "Snare");
    // What the console doesn't have, or no address.
    assert_eq!(command(&c, "/ch/99/mix/on ON"), Done::Unknown);
    assert_eq!(command(&c, ""), Done::Unknown);
}

#[test]
fn the_oscillator_plays_on_its_destination() {
    use livedaw_engine::StripKind::{Bus, Main, Matrix};
    use std::sync::atomic::Ordering::Relaxed;
    let engine = x32();
    engine.set_console_hook(Some(livedaw_x32::oscillator::apply));
    let state = State::default();
    let c = Console::new(&engine, &state);
    // As a real X32 Compact answers, off.
    assert_eq!(command(&c, "/config/osc"), Done::Answer("/config/osc -40.0 100.2 1k00 F1 PINK 0".into()));
    assert_eq!(command(&c, "/-stat/osc/on"), Done::Answer("/-stat/osc/on OFF".into()));
    let osc = &engine.oscillator;
    assert!(!osc.on.load(Relaxed));
    assert_eq!(osc.assigned(Bus, 0), [1.0, 1.0], "MX1");
    assert_eq!(command(&c, "/config/osc -20 440 2000 F2 SINE 21"), Done::Set(6));
    assert_eq!(command(&c, "/-stat/osc/on ON"), Done::Set(1));
    assert!(osc.on.load(Relaxed));
    assert_eq!(osc.mode.load(Relaxed), 0);
    assert!((osc.level_db.load() + 20.0).abs() < 0.3, "{}", osc.level_db.load());
    assert!((osc.freq_hz.load() - 2000.0).abs() < 60.0, "F2: {}", osc.freq_hz.load());
    assert_eq!((osc.assigned(Bus, 0), osc.assigned(Matrix, 1)), ([0.0; 2], [1.0; 2]));
    assert_eq!(command(&c, "/config/osc/dest 16"), Done::Set(1));
    assert_eq!(osc.assigned(Main, 0), [1.0, 0.0]);
    assert_eq!(command(&c, "/config/osc/dest 19"), Done::Set(1));
    assert_eq!(osc.assigned(Main, 1), [1.0, 1.0]);
    // Kept in the console text (shows and scenes keep it).
    assert!(engine.console_text().0.starts_with("osc=1 "), "{}", engine.console_text().0);
    engine.reset();
    assert!(!osc.on.load(Relaxed));
    assert_eq!(command(&c, "/-stat/osc/on"), Done::Answer("/-stat/osc/on OFF".into()));
}
