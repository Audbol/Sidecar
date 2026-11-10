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

#[test]
fn preferences_and_status_are_kept_and_the_solo_switches_are_the_engines() {
    use std::sync::atomic::Ordering::Relaxed;
    let engine = x32();
    let state = State::default();
    let c = Console::new(&engine, &state);
    // Kept by the server, by name or number, in the document's ranges.
    assert_eq!(command(&c, "/-prefs/clockmode"), Done::Answer("/-prefs/clockmode 24h".into()));
    assert_eq!(command(&c, "/-prefs/clockmode 12h"), Done::Set(1));
    assert_eq!(command(&c, "/-prefs/clockmode"), Done::Answer("/-prefs/clockmode 12h".into()));
    assert_eq!(command(&c, "/-prefs/card/USBmode 5"), Done::Set(1));
    assert_eq!(command(&c, "/-prefs/card/USBmode"), Done::Answer("/-prefs/card/USBmode 2in/2out".into()));
    assert_eq!(command(&c, "/-prefs/rta/visibility"), Done::Answer("/-prefs/rta/visibility 70%".into()));
    assert_eq!(command(&c, "/-prefs/haflags 2"), Done::Set(1));
    assert_eq!(command(&c, "/-prefs/haflags"), Done::Answer("/-prefs/haflags %0010".into()));
    assert_eq!(command(&c, "/-prefs/ip/addr/3 77"), Done::Set(1));
    assert_eq!(command(&c, "/-prefs/ip/addr"), Done::Answer("/-prefs/ip/addr 192 168 1 77".into()));
    assert_eq!(command(&c, "/-prefs/lamp 44"), Done::Set(1));
    assert_eq!(command(&c, "/-prefs/lamp"), Done::Answer("/-prefs/lamp 40".into()), "steps of 10");
    assert_eq!(command(&c, "/-stat/screen/SETUP/page 5"), Done::Set(1));
    assert_eq!(command(&c, "/-stat/screen/SETUP/page"), Done::Answer("/-stat/screen/SETUP/page 5".into()));
    assert_eq!(command(&c, "/-stat/selidx 70"), Done::Set(1));
    assert_eq!(command(&c, "/-stat/selidx 99"), Done::Set(0), "no such strip");
    assert_eq!(command(&c, "/-stat/selidx"), Done::Answer("/-stat/selidx 70".into()));
    // The solo switches: channel 3, bus 2 (50), the M/C (72), DCA 8 (80).
    assert_eq!(command(&c, "/-stat/solosw/03 ON"), Done::Set(1));
    assert!(engine.strips(StripKind::Channel)[2].params.solo.load(Relaxed));
    assert_eq!(command(&c, "/-stat/solosw/03"), Done::Answer("/-stat/solosw/03 ON".into()));
    assert_eq!(command(&c, "/-stat/solosw/50 ON"), Done::Set(1));
    let bus2 = engine.strips(StripKind::Bus).into_iter().find(|s| s.id() == 1).unwrap();
    assert!(bus2.params.solo.load(Relaxed));
    assert_eq!(command(&c, "/-stat/solosw/72 ON"), Done::Set(1));
    assert!(engine.strips(StripKind::Main)[1].params.solo.load(Relaxed));
    assert_eq!(command(&c, "/-stat/solosw/80 ON"), Done::Set(1));
    assert!(engine.controls.dcas[7].solo.load(Relaxed));
    engine.controls.dcas[7].solo.store(false, Relaxed);
    assert_eq!(command(&c, "/-stat/solosw/80"), Done::Answer("/-stat/solosw/80 OFF".into()));
    assert_eq!(command(&c, "/-stat/solosw/81"), Done::Unknown);
    // A scene file leaves preferences and status out, and doesn't read them.
    let scene = livedaw_x32::scn::write(&c, "s", "");
    assert!(!scene.contains("/-prefs") && !scene.contains("/-stat"), "not in a scene file");
    let read = livedaw_x32::scn::read(&c, "#2.7# \"s\" \"\" %000000000 1\n/-prefs/clockmode 24h\n");
    assert_eq!((read.applied, read.skipped), (0, 1));
    assert_eq!(command(&c, "/-prefs/clockmode"), Done::Answer("/-prefs/clockmode 12h".into()));
}
