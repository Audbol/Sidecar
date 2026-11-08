//! X32 channel presets (`.chn`): one strip written with paths relative to it,
//! and read back onto another strip or another console.
use livedaw_engine::{ConsoleConfig, Counts, Engine, Profile, StripKind};
use livedaw_x32::map::State;
use livedaw_x32::{chn, scn, Console};
use std::sync::atomic::Ordering::Relaxed;

fn x32() -> Engine {
    let engine = Engine::new(0);
    engine.configure(&ConsoleConfig { profile: Profile::X32, counts: Counts::X32 });
    engine
}

#[test]
fn a_channel_preset_holds_one_strip_with_relative_paths() {
    let engine = x32();
    let state = State::default();
    let c = Console::new(&engine, &state);
    let text = chn::write(&c, "/ch/03", "Snare \"top\"").unwrap();
    let mut lines = text.lines();
    assert_eq!(lines.next(), Some("#2.1# \"Snare top\" 1 %111111111 1"));
    let rest: Vec<&str> = lines.collect();
    assert!(rest.iter().any(|l| l.starts_with("/config ")));
    assert!(rest.iter().any(|l| l.starts_with("/eq/1 ")));
    assert!(rest.iter().any(|l| l.starts_with("/mix/01 ")));
    assert!(rest.iter().all(|l| l.starts_with('/') && !l.starts_with("/ch/") && !l.starts_with("/-")), "{rest:?}");
    // The same lines as the scene file's for that strip, without the prefix.
    let scene = scn::write(&c, "", "");
    let strip: Vec<String> = scene.lines().filter_map(|l| l.strip_prefix("/ch/03/").map(|r| format!("/{r}"))).collect();
    assert_eq!(rest, strip);
    // Mains, buses and the others too; not what isn't a strip.
    for strip in ["/main/st", "/main/m", "/bus/16", "/mtx/06", "/auxin/08", "/fxrtn/01"] {
        assert!(chn::write(&c, strip, "x").is_some_and(|t| t.lines().count() > 3), "{strip}");
    }
    for strip in ["/ch/33", "/ch/1", "/config", "/ch/01/eq", "", "/fx/1"] {
        assert!(chn::write(&c, strip, "x").is_none(), "{strip}");
        assert!(chn::read(&c, strip, "/mix ON 0 ON +0 OFF -oo").is_none(), "{strip}");
    }
}

#[test]
fn a_channel_preset_loads_onto_another_strip_and_keeps_its_source() {
    let a = x32();
    let sa = State::default();
    let ca = Console::new(&a, &sa);
    let changes = "#2.1# \"Vox\" 1 %111111111 1\n\
                   /config \"Vox\" 1 RD 1\n\
                   /mix OFF -10.0 ON -25 OFF -oo\n";
    let read = chn::read(&ca, "/ch/01", changes).unwrap();
    assert_eq!((read.name.as_str(), read.applied, read.skipped), ("Vox", 2, 0));
    let text = chn::write(&ca, "/ch/01", "Vox").unwrap();

    // Onto channel 7 of another console: its name, mix and the rest, its own source.
    let b = x32();
    let sb = State::default();
    let cb = Console::new(&b, &sb);
    let source_before =
        scn::write(&cb, "", "").lines().find(|l| l.starts_with("/ch/07/config ")).unwrap().rsplit(' ').next().unwrap().to_owned();
    let read = chn::read(&cb, "/ch/07", &text).unwrap();
    assert_eq!(read.skipped, 0);
    assert_eq!(read.applied, text.lines().count() - 1);
    let ch7 = &b.strips(StripKind::Channel)[6];
    assert_eq!(ch7.name(), "Vox");
    assert!(ch7.params.mute.load(Relaxed), "mix OFF mutes");
    assert!((ch7.params.fader_db.load() + 10.0).abs() < 0.05);
    let config = scn::write(&cb, "", "").lines().find(|l| l.starts_with("/ch/07/config ")).unwrap().to_owned();
    assert!(config.starts_with("/ch/07/config \"Vox\" 1 RD "), "{config}");
    assert!(config.ends_with(&format!(" {source_before}")), "the source stays: {config}");
    // Written again from channel 7, the same preset but for that source.
    let again = chn::write(&cb, "/ch/07", "Vox").unwrap();
    let differ: Vec<_> = text.lines().zip(again.lines()).filter(|(x, y)| x != y).collect();
    assert!(differ.iter().all(|(x, _)| x.starts_with("/config ")), "{differ:?}");
}

#[test]
fn head_amps_and_unknown_lines_are_skipped() {
    let engine = x32();
    let state = State::default();
    let c = Console::new(&engine, &state);
    let text = "#2.1# \"Kick\" 1 %000000111 1\n\
                # a comment\n\
                /headamp/000 +20.0 ON\n\
                /nothing 1 2\n\
                junk\n\
                /mix ON -oo OFF +0 OFF -oo\n";
    let read = chn::read(&c, "/bus/02", text).unwrap();
    assert_eq!((read.name.as_str(), read.applied, read.skipped), ("Kick", 1, 3));
    assert!(chn::read(&c, "/bus/02", "").is_some_and(|r| r.applied == 0 && r.skipped == 0));
}
