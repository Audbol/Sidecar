//! X32 scene files: written as a real X32's `/node` answers, read back, and
//! (with `LIVEDAW_X32_SNAPSHOT` naming a read-only dump of a freshly
//! initialised X32, kept outside the repository) compared line by line.
use livedaw_engine::{ConsoleConfig, Counts, Engine, Profile};
use livedaw_x32::map::State;
use livedaw_x32::{scn, Console};
use std::collections::HashMap;

fn x32() -> Engine {
    let engine = Engine::new(0);
    engine.configure(&ConsoleConfig { profile: Profile::X32, counts: Counts::X32 });
    engine
}

#[test]
fn a_scene_file_has_its_header_and_the_config_first() {
    let engine = x32();
    let state = State::default();
    let text = scn::write(&Console::new(&engine, &state), "Band \"A\"", "Soundcheck");
    let mut lines = text.lines();
    assert_eq!(lines.next(), Some("#2.7# \"Band A\" \"Soundcheck\" %000000000 1"));
    assert!(lines.next().unwrap().starts_with("/config/"));
    assert!(text.lines().any(|l| l.starts_with("/ch/32/mix ")));
    assert!(text.lines().any(|l| l.starts_with("/main/m/mix ")));
    assert!(!text.lines().any(|l| l.starts_with("/-")), "no preferences or status");
    let first_ch = text.lines().position(|l| l.starts_with("/ch/")).unwrap();
    assert!(text.lines().skip(first_ch).all(|l| !l.starts_with("/config/")));
}

#[test]
fn a_scene_file_reads_back_into_another_console() {
    let a = x32();
    let sa = State::default();
    let ca = Console::new(&a, &sa);
    // Change a few things through the file's own lines.
    let changes = "#2.7# \"Gig\" \"\" %000000000 1\n\
                   # a comment\n\
                   /ch/01/config \"Kick\" 1 RD 1\n\
                   /ch/01/mix OFF -10.0 ON -25 OFF -oo\n\
                   /bus/03/config \"Wedges\" 0 GN\n\
                   /fx/1 HALL\n\
                   /ch/05/automix 1 3.0\n";
    let read = scn::read(&ca, changes);
    assert_eq!((read.name.as_str(), read.note.as_str(), read.applied, read.skipped), ("Gig", "", 5, 0));
    let text = scn::write(&ca, "Gig", "");
    assert!(
        text.contains("/ch/01/config \"Kick\" 1 RD 1\n"),
        "{}",
        text.lines().find(|l| l.starts_with("/ch/01/config")).unwrap()
    );

    let b = x32();
    let sb = State::default();
    let cb = Console::new(&b, &sb);
    let read = scn::read(&cb, &text);
    assert_eq!(read.skipped, 0);
    assert_eq!(read.applied, text.lines().count() - 1);
    assert_eq!(scn::write(&cb, "Gig", ""), text, "the same scene file again");
    let ch1 = &b.strips(livedaw_engine::StripKind::Channel)[0];
    assert_eq!(ch1.name(), "Kick");
    assert!(ch1.params.mute.load(std::sync::atomic::Ordering::Relaxed), "mix ON is the X32's unmute: OFF mutes");
    assert!((ch1.params.fader_db.load() + 10.0).abs() < 0.05, "-10 in the X32's fader steps");
}

#[test]
fn a_factory_x32_writes_what_a_real_one_answers() {
    let Ok(path) = std::env::var("LIVEDAW_X32_SNAPSHOT") else {
        eprintln!("LIVEDAW_X32_SNAPSHOT isn't set: skipped");
        return;
    };
    let snapshot = std::fs::read_to_string(path).unwrap();
    let real: HashMap<&str, &str> =
        snapshot.lines().filter(|l| l.starts_with('/')).map(|l| (l.split(' ').next().unwrap(), l)).collect();
    let engine = x32();
    let state = State::default();
    let text = scn::write(&Console::new(&engine, &state), "", "");
    let (mut same, mut differ) = (0, Vec::new());
    // Where a full-size X32 (Sidecar's factory) differs from the Compact the
    // dump was read from: its 32 local inputs (the Compact takes 17-32 from
    // AES50 A) and Main LR on outputs 15-16 (the Compact's on 7-8).
    const FULL_SIZE: &[&str] = &[
        "/config/routing/IN",
        "/config/routing/CARD",
        "/outputs/main/07",
        "/outputs/main/08",
        "/outputs/main/15",
        "/outputs/main/16",
    ];
    for line in text.lines().skip(1) {
        let path = line.split(' ').next().unwrap();
        if FULL_SIZE.contains(&path) {
            continue;
        }
        match real.get(path) {
            Some(r) if r.trim_end() == line.trim_end() => same += 1,
            Some(r) => differ.push(format!("ours {line}\nreal {r}")),
            None => {}
        }
    }
    assert!(differ.is_empty(), "{} same, differ:\n{}", same, differ.join("\n"));
    assert!(same > 900, "{same}");
}
