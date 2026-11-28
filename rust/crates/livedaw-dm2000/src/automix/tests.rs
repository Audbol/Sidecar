use super::*;

/// An Automix and two Input Channels, driven as the app does: a tick after every press.
struct Rig {
    am: Automix,
    faders: Vec<f32>,
    ons: Vec<bool>,
}

impl Rig {
    fn new() -> Self {
        Self { am: Automix::default(), faders: vec![0.0; 2], ons: vec![true; 2] }
    }

    fn press(&mut self, key: Key, t: f64) {
        self.am.press(key, t);
        self.tick(t);
    }

    fn tick(&mut self, t: f64) {
        self.am.tick(t, &mut self.faders, &mut self.ons);
    }

    /// Channel 1's fader moved to `db` at `t`.
    fn fader(&mut self, db: f32, t: f64) {
        self.faders[0] = db;
        self.tick(t);
    }

    /// Channel 1's fader moves, as (seconds, dB).
    fn moves(&self) -> Vec<(f64, f32)> {
        self.am.moves().iter().filter(|m| m.strip == 0 && !m.on).map(|m| ((m.t * 100.0).round() / 100.0, m.value)).collect()
    }

    /// A first take of channel 1: `steps` as (seconds, dB), then stopped.
    fn take(&mut self, steps: &[(f64, f32)]) {
        self.press(Key::Rec, 0.0);
        for &(t, db) in steps {
            self.fader(db, t);
        }
        self.press(Key::Run, 100.0);
        assert!(!self.am.is(Key::Rec) && !self.am.is(Key::Run));
    }

    /// A pass from 1000 s on: its clock reads `t` - 1000.
    fn run(&mut self) {
        self.faders = vec![0.0; 2];
        self.press(Key::Run, 1000.0);
    }

    fn at(&mut self, t: f64) {
        self.tick(1000.0 + t);
    }
}

#[test]
fn the_keys_keep_the_hosts_numbers() {
    for (i, k) in Key::ALL.iter().enumerate() {
        assert_eq!(*k as i32, i as i32);
        assert_eq!(Key::from_i32(i as i32), Some(*k));
    }
    assert_eq!(Key::from_i32(16), None);
    assert_eq!(Key::from_i32(-1), None);
}

#[test]
fn a_take_plays_back_and_undo_swaps_it_with_the_one_before() {
    let mut r = Rig::new();
    r.take(&[(1.0, -12.0)]);
    assert_eq!(r.moves(), vec![(1.0, -12.0)]);
    r.run();
    r.at(0.5);
    assert_eq!(r.faders[0], 0.0);
    r.at(1.0);
    assert_eq!(r.faders[0], -12.0, "the take plays back");
    r.press(Key::Run, 1002.0);
    // A second take replaces it; UNDO brings the first back, and again the second.
    r.faders[0] = 0.0;
    r.press(Key::Rec, 2000.0);
    r.fader(-3.0, 2001.0);
    r.press(Key::Run, 2002.0);
    assert_eq!(r.moves(), vec![(1.0, -3.0)]);
    r.press(Key::Undo, 3000.0);
    assert_eq!(r.moves(), vec![(1.0, -12.0)]);
    r.press(Key::Undo, 3000.0);
    assert_eq!(r.moves(), vec![(1.0, -3.0)]);
    r.press(Key::Clear, 3000.0);
    assert!(r.am.moves().is_empty());
}

#[test]
fn on_keys_record_and_play_with_overwrite_on() {
    let mut r = Rig::new();
    r.press(Key::Rec, 0.0);
    r.ons[1] = false;
    r.tick(1.0);
    r.press(Key::Run, 2.0);
    r.ons[1] = true;
    r.press(Key::Run, 10.0);
    r.tick(11.0);
    assert!(!r.ons[1], "the ON take plays");
    r.press(Key::Run, 12.0);
    // OVERWRITE ON off: an ON pressed in a pass isn't recorded.
    r.press(Key::On, 20.0);
    r.press(Key::Clear, 20.0);
    r.press(Key::Rec, 20.0);
    r.ons[0] = false;
    r.tick(21.0);
    assert!(r.am.moves().is_empty());
}

#[test]
fn auto_rec_is_rec_that_stays_on() {
    let mut r = Rig::new();
    r.press(Key::AutoRec, 0.0);
    assert!(r.am.is(Key::AutoRec) && r.am.is(Key::Rec) && r.am.is(Key::Run));
    r.press(Key::Run, 1.0);
    assert!(r.am.is(Key::AutoRec) && !r.am.is(Key::Rec));
    r.press(Key::Run, 2.0);
    assert!(r.am.is(Key::Rec), "the next run records");
    r.press(Key::Run, 3.0);
    r.press(Key::AutoRec, 4.0);
    assert!(!r.am.is(Key::AutoRec) && !r.am.is(Key::Rec));
}

#[test]
fn touch_sense_records_only_the_held_fader() {
    let mut r = Rig::new();
    r.press(Key::Rec, 0.0);
    r.faders = vec![-12.0, -12.0];
    r.tick(0.5);
    r.press(Key::Run, 1.0);
    r.press(Key::TouchSense, 1.0);
    assert!(r.am.is(Key::Touch) && r.am.is(Key::TouchSense));
    r.faders = vec![0.0, 0.0];
    r.press(Key::Rec, 10.0);
    r.am.hold(1, true);
    r.faders[1] = -20.0;
    r.tick(10.6);
    assert_eq!(r.faders, vec![-12.0, -20.0], "not held: its take plays; held: what it does now");
    r.am.hold(1, false);
    r.press(Key::Run, 11.0);
    r.faders = vec![0.0, 0.0];
    r.press(Key::Run, 20.0);
    r.tick(21.0);
    assert_eq!(r.faders, vec![-12.0, -20.0], "the new take of the held fader, the old one of the other");
}

#[test]
fn latch_keeps_a_touched_fader_punched_in_to_the_end_of_the_pass() {
    let mut r = Rig::new();
    r.take(&[(1.0, -10.0), (2.0, -20.0), (3.0, -30.0)]);
    r.press(Key::Latch, 0.0);
    assert!(r.am.is(Key::Latch) && !r.am.is(Key::Touch));
    // [TOUCH SENSE] remembers LATCH.
    r.press(Key::TouchSense, 0.0);
    assert!(!r.am.is(Key::TouchSense));
    r.press(Key::TouchSense, 0.0);
    assert!(r.am.is(Key::Latch));
    r.run();
    r.press(Key::Rec, 1000.0);
    r.am.hold(0, true);
    r.at(0.5);
    r.fader(-5.0, 1000.6);
    r.am.hold(0, false);
    r.at(0.8);
    for t in [1.0, 2.0, 3.0] {
        r.at(t);
        assert_eq!(r.faders[0], -5.0, "let go, still punched in");
    }
    r.press(Key::Run, 1004.0);
    assert_eq!(r.moves(), vec![(0.6, -5.0)]);
}

#[test]
fn absolute_replaces_the_existing_data_and_edit_out_off_waits_for_its_next_move() {
    let mut r = Rig::new();
    r.take(&[(1.0, -10.0), (2.0, -20.0), (3.0, -30.0)]);
    r.press(Key::Touch, 0.0);
    r.run();
    r.press(Key::Rec, 1000.0);
    r.am.hold(0, true);
    r.at(0.5);
    r.fader(-5.0, 1000.6);
    r.at(1.0);
    assert_eq!(r.faders[0], -5.0);
    r.am.hold(0, false);
    r.at(1.5);
    r.at(1.8);
    assert_eq!(r.faders[0], -5.0, "OFF: it stays until the existing data's next move");
    r.at(2.0);
    assert_eq!(r.faders[0], -20.0);
    r.press(Key::Run, 1004.0);
    assert_eq!(r.moves(), vec![(0.6, -5.0), (2.0, -20.0), (3.0, -30.0)]);
}

#[test]
fn relative_shifts_the_existing_data_by_the_faders_move() {
    let mut r = Rig::new();
    r.take(&[(1.0, -10.0), (2.0, -20.0), (3.0, -30.0), (4.0, -144.0)]);
    r.press(Key::Relative, 0.0);
    assert!(r.am.is(Key::Relative) && !r.am.is(Key::Absolute));
    r.press(Key::Touch, 0.0);
    r.run();
    r.at(1.0);
    assert_eq!(r.faders[0], -10.0);
    r.press(Key::Rec, 1001.5);
    r.am.hold(0, true);
    r.at(1.6);
    // Up 6 dB from the existing data: what follows comes 6 dB up too.
    r.fader(-4.0, 1001.7);
    r.at(2.0);
    assert_eq!(r.faders[0], -14.0);
    r.at(3.0);
    assert_eq!(r.faders[0], -24.0);
    r.at(4.0);
    assert_eq!(r.faders[0], -144.0, "-oo stays");
    r.am.hold(0, false);
    r.press(Key::Run, 1005.0);
    assert_eq!(r.moves(), vec![(1.0, -10.0), (1.7, -4.0), (2.0, -14.0), (3.0, -24.0), (4.0, -144.0)]);
    // ABSOLUTE again.
    r.press(Key::Absolute, 2000.0);
    assert!(!r.am.is(Key::Relative));
}

#[test]
fn return_glides_back_to_the_existing_data_over_its_time() {
    let mut r = Rig::new();
    r.take(&[(0.5, -10.0)]);
    r.am.set_return_time(1.0);
    r.press(Key::Return, 0.0);
    r.press(Key::Touch, 0.0);
    assert!(r.am.is(Key::Return) && r.am.edit_out() == EditOut::Return);
    r.run();
    r.at(0.6);
    r.press(Key::Rec, 1000.7);
    r.am.hold(0, true);
    r.at(1.0);
    r.fader(0.0, 1001.1);
    r.am.hold(0, false);
    r.at(2.0);
    assert_eq!(r.faders[0], 0.0);
    r.at(2.5);
    assert!((r.faders[0] + 5.0).abs() < 0.01, "half way back: {}", r.faders[0]);
    r.at(3.0);
    assert_eq!(r.faders[0], -10.0);
    r.press(Key::Run, 1004.0);
    // The glide is part of the take.
    assert!(r.moves().contains(&(2.5, -5.0)) && r.moves().ends_with(&[(3.0, -10.0)]));
    r.press(Key::Return, 2000.0);
    assert_eq!(r.am.edit_out(), EditOut::Off);
}

#[test]
fn takeover_records_until_the_existing_data_meets_the_fader() {
    let mut r = Rig::new();
    r.take(&[(1.0, -30.0), (2.0, -20.0), (3.0, -10.0), (4.0, 0.0)]);
    r.press(Key::Takeover, 0.0);
    r.press(Key::Touch, 0.0);
    r.run();
    r.at(1.0);
    r.press(Key::Rec, 1001.1);
    r.am.hold(0, true);
    r.at(1.2);
    r.fader(-15.0, 1001.3);
    r.am.hold(0, false);
    r.at(1.5);
    r.at(2.0);
    assert_eq!(r.faders[0], -15.0, "the existing data is still below it");
    r.at(3.0);
    assert_eq!(r.faders[0], -10.0, "crossed: the existing data plays again");
    r.at(4.0);
    assert_eq!(r.faders[0], 0.0);
    r.press(Key::Run, 1005.0);
    assert_eq!(r.moves(), vec![(1.0, -30.0), (1.3, -15.0), (3.0, -10.0), (4.0, 0.0)]);
    r.press(Key::Return, 2000.0);
    assert!(r.am.is(Key::Return) && !r.am.is(Key::Takeover), "one Edit Out mode at a time");
}

#[test]
fn rec_turned_off_while_running_punches_out() {
    let mut r = Rig::new();
    r.take(&[(0.5, -10.0)]);
    r.am.set_return_time(0.0);
    r.press(Key::Return, 0.0);
    r.run();
    r.at(0.6);
    r.press(Key::Rec, 1000.7);
    r.fader(0.0, 1000.8);
    r.press(Key::Rec, 1001.0);
    assert_eq!(r.faders[0], -10.0, "RETURN at once");
    assert!(r.am.is(Key::Run) && !r.am.is(Key::Rec));
}

#[test]
fn scene_clock_starts_the_clock_again_at_a_recall() {
    let mut r = Rig::new();
    r.take(&[(1.0, -10.0)]);
    r.press(Key::Clock, 0.0);
    r.run();
    r.at(5.0);
    assert!((r.am.time() - 5.0).abs() < 1e-9);
    r.faders[0] = 0.0;
    r.am.scene_recalled(1005.0);
    r.at(5.5);
    assert_eq!(r.faders[0], 0.0);
    r.at(6.0);
    assert_eq!(r.faders[0], -10.0, "played again from the recall");
}

#[test]
fn return_time_keeps_to_its_range_and_steps() {
    let mut am = Automix::default();
    am.set_return_time(2.34);
    assert_eq!(am.return_time(), 2.3);
    am.set_return_time(99.0);
    assert_eq!(am.return_time(), RETURN_TIME_MAX);
    am.set_return_time(-1.0);
    assert_eq!(am.return_time(), 0.0);
    am.set_return_time(f32::NAN);
    assert_eq!(am.return_time(), 0.0);
}
