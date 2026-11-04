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
