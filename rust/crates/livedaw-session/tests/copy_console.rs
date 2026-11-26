//! COPY FROM: another console's settings copied into the running one.

use livedaw_engine::{ConsoleConfig, Engine, Profile, StripKind};
use livedaw_session::copy::{copy_console, copy_from_file, CopyPlan};
use livedaw_session::*;
use std::sync::atomic::Ordering::Relaxed;

fn console(profile: Profile) -> Engine {
    let e = Engine::new(0);
    e.configure(&ConsoleConfig { profile, counts: profile.spec().counts });
    e
}

fn channel(e: &Engine, number: usize) -> std::sync::Arc<livedaw_engine::Strip> {
    e.strip_at(StripKind::Channel, number - 1).unwrap()
}

/// A name of the strip's own, shown (unlinked from its source), as when renamed on the console.
fn own_name(s: &livedaw_engine::Strip, name: &str) {
    s.params.cust_link.store(false, Relaxed);
    s.set_name(name);
}

#[test]
fn a_wings_channels_come_across_to_a_dm2000() {
    let (wing, dm) = (console(Profile::Wing), console(Profile::Dm2000));
    let kick = channel(&wing, 1);
    own_name(&kick, "Kick");
    kick.params.fader_db.store(-5.0);
    kick.params.input.trim_db.store(6.0);
    kick.params.mute.store(true, Relaxed);
    let send = kick.sends.get_or_create(2).unwrap();
    send.enabled.store(true, Relaxed);
    send.level_db.store(-3.0);
    own_name(&channel(&wing, 40), "Spare");

    let report = copy_console(&dm, &wing, &CopyPlan::default(), &mut NoPlugins);

    let to = channel(&dm, 1);
    assert_eq!(to.name(), "Kick");
    assert_eq!(to.params.fader_db.load(), -5.0);
    assert_eq!(to.params.input.trim_db.load(), 6.0);
    assert!(to.params.mute.load(Relaxed));
    let s = to.sends.find(2).expect("the send to bus 3");
    assert!(s.enabled.load(Relaxed));
    assert_eq!(s.level_db.load(), -3.0);
    assert_eq!(channel(&dm, 40).name(), "Spare");
    assert_eq!(report.source, Profile::Wing.spec().name);
    assert!(report.strips >= 40, "{report:?}");
    assert!(!report.left_out.iter().any(|l| l.starts_with("Channel")), "the DM2000 has more channels: {:?}", report.left_out);
}

#[test]
fn channels_the_console_lacks_are_reported_not_copied() {
    let (dm, wing) = (console(Profile::Dm2000), console(Profile::Wing));
    own_name(&channel(&dm, 1), "Vocal");
    own_name(&channel(&dm, 96), "Last");

    let report = copy_console(&wing, &dm, &CopyPlan::default(), &mut NoPlugins);

    assert_eq!(channel(&wing, 1).name(), "Vocal");
    assert_eq!(wing.num_strips(StripKind::Channel), 40, "no channels added");
    assert!(report.left_out.contains(&"Channels 41-96: this console has 40".to_owned()), "{:?}", report.left_out);
}

#[test]
fn sends_to_a_bus_the_console_lacks_are_left_out() {
    let (wing, small) = (console(Profile::Wing), console(Profile::ProMix01));
    let have = small.num_strips(StripKind::Bus);
    let slot = wing.strips(StripKind::Bus).iter().map(|b| b.id() as usize).max().unwrap();
    assert!(slot >= have, "the ProMix 01 has fewer buses than the WING");
    let first = channel(&wing, 1);
    let send = first.sends.get_or_create(slot).unwrap();
    send.enabled.store(true, Relaxed);
    send.level_db.store(-7.0);

    let level = |e: &Engine| channel(e, 1).sends.find(slot).map(|s| (s.enabled.load(Relaxed), s.level_db.load()));
    let report = copy_console(&small, &wing, &CopyPlan::default(), &mut NoPlugins);

    // (A send slot may exist on its defaults: what matters is that the WING's level isn't in it.)
    assert!(level(&small).is_none_or(|(_, db)| db != -7.0), "no send to a bus it hasn't got: {:?}", level(&small));
    // Only the send that was on is named (the WING's others are off).
    let line = format!("Sends to bus {}: this console doesn't have it", slot + 1);
    assert!(report.left_out.contains(&line), "{:?}", report.left_out);
}

#[test]
fn a_range_copies_only_those_channels_and_routing_stays_unless_chosen() {
    let (a, b) = (console(Profile::Wing), console(Profile::Wing));
    for n in 1..=10 {
        own_name(&channel(&a, n), &format!("A{n}"));
        channel(&a, n).params.input_left.store(30 + n as i32, Relaxed);
    }
    let before = channel(&b, 3).params.input_left.load(Relaxed);

    let plan = CopyPlan { channels: Some((1, 8)), ..CopyPlan::default() };
    copy_console(&b, &a, &plan, &mut NoPlugins);
    assert_eq!(channel(&b, 8).name(), "A8");
    assert_ne!(channel(&b, 9).name(), "A9", "outside the range");
    assert_eq!(channel(&b, 3).params.input_left.load(Relaxed), before, "routing isn't copied by default");

    let plan = CopyPlan { sections: section::ROUTING, ..CopyPlan::default() };
    copy_console(&b, &a, &plan, &mut NoPlugins);
    assert_eq!(channel(&b, 3).params.input_left.load(Relaxed), 33, "copied when chosen");
}

#[test]
fn a_name_shown_from_the_source_comes_across_as_the_strips_own() {
    let (wing, dm) = (console(Profile::Wing), console(Profile::Dm2000));
    let ch = channel(&wing, 1);
    let index = livedaw_engine::sources::source_index(ch.params.input_left.load(Relaxed)).expect("channel 1 has a source");
    wing.sources().set_name(index, "Lead vox");
    assert!(ch.params.cust_link.load(Relaxed), "a WING channel shows its source's name");

    copy_console(&dm, &wing, &CopyPlan::default(), &mut NoPlugins);

    let to = channel(&dm, 1);
    assert_eq!(to.name(), "Lead vox");
    assert!(!to.params.cust_link.load(Relaxed), "shown as the strip's own, not this console's source name");
}

#[test]
fn undo_takes_a_copy_back() {
    let wing = console(Profile::Wing);
    own_name(&channel(&wing, 1), "Kick");
    channel(&wing, 1).params.fader_db.store(-12.0);
    let path = std::env::temp_dir().join(format!("sidecar-copy-undo-{}.lshow", std::process::id()));
    Show::new().save_to_file(&wing, &mut NoPlugins, &path).unwrap();

    let dm = console(Profile::Dm2000);
    own_name(&channel(&dm, 1), "Mine");
    channel(&dm, 1).params.fader_db.store(0.0);
    let mut show = Show::new();
    let missing = std::path::Path::new("no such file.lshow");
    assert!(copy::copy_into_show(&mut show, &dm, missing, &CopyPlan::default(), &mut NoPlugins).is_err());
    assert!(!show.can_undo_recall(&dm), "a file that couldn't be read sets no step back");

    copy::copy_into_show(&mut show, &dm, &path, &CopyPlan::default(), &mut NoPlugins).unwrap();
    let _ = std::fs::remove_file(&path);
    let _ = std::fs::remove_file(path.with_extension("lshow.bak"));
    assert_eq!(channel(&dm, 1).name(), "Kick");

    assert!(show.can_undo_recall(&dm));
    show.undo_recall(&dm, &mut NoPlugins);
    assert_eq!(channel(&dm, 1).name(), "Mine");
    assert_eq!(channel(&dm, 1).params.fader_db.load(), 0.0);
}

#[test]
fn a_kept_consoles_file_is_copied_from() {
    let wing = console(Profile::Wing);
    own_name(&channel(&wing, 2), "Snare");
    let path = std::env::temp_dir().join(format!("sidecar-copy-{}.lshow", std::process::id()));
    Show::new().save_to_file(&wing, &mut NoPlugins, &path).unwrap();

    let dm = console(Profile::Dm2000);
    let report = copy_from_file(&dm, &path, &CopyPlan::default(), &mut NoPlugins).unwrap();
    let _ = std::fs::remove_file(&path);
    let _ = std::fs::remove_file(path.with_extension("lshow.bak"));

    assert_eq!(channel(&dm, 2).name(), "Snare");
    assert_eq!(dm.profile(), Profile::Dm2000, "still a DM2000");
    assert_eq!(report.source, Profile::Wing.spec().name);
    assert!(copy_from_file(&dm, std::path::Path::new("no such file.lshow"), &CopyPlan::default(), &mut NoPlugins).is_err());
}
