//! X32 effect presets (`.efx`) and routing presets (`.rou`): written from one
//! console and read back onto another slot or console.
use livedaw_engine::{ConsoleConfig, Counts, Engine, Profile};
use livedaw_x32::map::State;
use livedaw_x32::{library, scn, Console};

fn x32() -> Engine {
    let engine = Engine::new(0);
    engine.configure(&ConsoleConfig { profile: Profile::X32, counts: Counts::X32 });
    engine
}

/// The scene file's lines that start with `prefix`.
fn lines(c: &Console, prefix: &str) -> Vec<String> {
    scn::write(c, "", "").lines().filter(|l| l.starts_with(prefix)).map(str::to_owned).collect()
}

#[test]
fn an_effect_preset_holds_one_slot_and_loads_into_another() {
    let a = x32();
    let sa = State::default();
    let ca = Console::new(&a, &sa);
    // Slot 2 as a plate with its own sources and a changed parameter.
    let read = scn::read(&ca, "/fx/2 PLAT\n/fx/2/source MIX3 MIX4\n/fx/2/par 0.25 0.50 0.75\n");
    assert_eq!((read.applied, read.skipped), (3, 0));
    let text = library::efx_write(&ca, "/fx/2", "Plate \"big\"").unwrap();
    let mut it = text.lines();
    assert_eq!(it.next(), Some("#2.1# \"Plate big\" 2 %1 1"));
    let rest: Vec<&str> = it.collect();
    assert_eq!(rest[0], "/fx PLAT", "the model first: {rest:?}");
    assert!(rest.iter().any(|l| l.starts_with("/fx/source MIX3 MIX4")));
    assert!(rest.iter().any(|l| l.starts_with("/fx/par ")));
    assert!(rest.iter().all(|l| l.starts_with("/fx") && !l.starts_with("/fx/2")), "{rest:?}");

    // Into slot 3 of another console.
    let b = x32();
    let sb = State::default();
    let cb = Console::new(&b, &sb);
    let read = library::efx_read(&cb, "/fx/3", &text).unwrap();
    assert_eq!((read.name.as_str(), read.skipped), ("Plate big", 0));
    assert_eq!(read.applied, rest.len());
    let from: Vec<String> = lines(&ca, "/fx/2").iter().map(|l| l.replacen("/fx/2", "/fx/3", 1)).collect();
    assert_eq!(lines(&cb, "/fx/3"), from);
    // Written again, the same file.
    assert_eq!(library::efx_write(&cb, "/fx/3", "Plate \"big\"").unwrap(), text);
}

#[test]
fn an_effect_preset_skips_what_the_slot_lacks_and_refuses_other_addresses() {
    let engine = x32();
    let state = State::default();
    let c = Console::new(&engine, &state);
    // Slot 6 is an insert: it has no source lines.
    let text = "#2.1# \"x\" 2 %1 1\n/fx GEQ\n/fx/source MIX1 MIX1\n/ch/01/mix ON\n";
    let read = library::efx_read(&c, "/fx/6", text).unwrap();
    assert_eq!((read.applied, read.skipped), (1, 2));
    for address in ["/fx/0", "/fx/9", "/fx/10", "/fx", "/ch/01", "", "/fx/1/par"] {
        assert!(library::efx_write(&c, address, "x").is_none(), "{address}");
        assert!(library::efx_read(&c, address, text).is_none(), "{address}");
    }
}

#[test]
fn a_routing_preset_round_trips_and_leaves_everything_else() {
    let a = x32();
    let sa = State::default();
    let ca = Console::new(&a, &sa);
    scn::read(&ca, "/config/routing/OUT/1-4 CARD1-4\n/outputs/main/03 3 8\n/ch/01/mix/fader 0.25\n");
    let text = library::rou_write(&ca, "Stage");
    assert!(text.starts_with("#2.1# \"Stage\" 3 %11 1\n"));
    assert!(text.lines().skip(1).all(|l| l.starts_with("/config/routing") || l.starts_with("/outputs/")), "{text}");
    assert!(text.lines().any(|l| l.starts_with("/outputs/main/03 ")));

    let b = x32();
    let sb = State::default();
    let cb = Console::new(&b, &sb);
    let before_mix = lines(&cb, "/ch/01/mix ");
    // A scene's lines come along but only the routing is read.
    let mixed = format!("{text}/ch/01/mix OFF 0.0 ON +0 OFF -oo\n");
    let read = library::rou_read(&cb, &mixed);
    assert_eq!((read.name.as_str(), read.skipped), ("Stage", 1));
    assert_eq!(read.applied, text.lines().count() - 1);
    for prefix in ["/config/routing", "/outputs/"] {
        assert_eq!(lines(&cb, prefix), lines(&ca, prefix), "{prefix}");
    }
    assert_eq!(lines(&cb, "/ch/01/mix "), before_mix, "the channel stays");
}
