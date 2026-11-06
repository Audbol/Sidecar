//! The X32's oscillator (MAN p.62; `/config/osc`, OSC document pp.21-22, and
//! `/-stat/osc/on`, p.64): its level, two frequencies and the one selected,
//! its type (sine, pink or white noise) and one destination (mix bus 1-16, L,
//! R, L+R, M/C, matrix 1-6). It plays through the engine's console oscillator
//! (`livedaw_engine::oscillator`) and is kept in the engine's console text (an
//! `osc=` line), so shows and scenes keep it.

use livedaw_dsp::generator::generator_mode;
use livedaw_engine::{Engine, StripKind};

/// `dest`'s choices (OSC document p.22).
pub const DESTINATIONS: [&str; 26] = [
    "MX1", "MX2", "MX3", "MX4", "MX5", "MX6", "MX7", "MX8", "MX9", "MX10", "MX11", "MX12", "MX13", "MX14", "MX15", "MX16", "L",
    "R", "L+R", "M/C", "MTX1", "MTX2", "MTX3", "MTX4", "MTX5", "MTX6",
];

/// Its fields, in the order of the map's `Bind::Osc` (0 ON, 1 level, 2 F1,
/// 3 F2, 4 FSEL, 5 type, 6 dest).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Oscillator {
    pub on: bool,
    /// dB (-oo as `f32::NEG_INFINITY`).
    pub level_db: f32,
    pub f1: f32,
    pub f2: f32,
    /// 0 F1, 1 F2.
    pub fsel: u8,
    /// 0 SINE, 1 PINK, 2 WHITE.
    pub kind: u8,
    pub dest: u8,
}

impl Default for Oscillator {
    /// Off, then as a real X32 Compact answered (`/config/osc -40.0 100.2 1k00
    /// F1 PINK 0`, November 2026).
    fn default() -> Self {
        Oscillator { on: false, level_db: -40.0, f1: 100.237, f2: 1000.0, fsel: 0, kind: 1, dest: 0 }
    }
}

impl Oscillator {
    /// The `osc=` line of a console text (the defaults without one).
    pub fn from_text(text: &str) -> Self {
        let d = Oscillator::default();
        let Some(line) = text.lines().find_map(|l| l.strip_prefix("osc=")) else { return d };
        let v: Vec<f32> = line.split_whitespace().map(|x| x.parse().unwrap_or(f32::NAN)).collect();
        let at = |k: usize, def: f32| v.get(k).copied().filter(|x| !x.is_nan()).unwrap_or(def);
        Oscillator {
            on: at(0, 0.0) != 0.0,
            level_db: at(1, d.level_db),
            f1: at(2, d.f1).clamp(20.0, 20000.0),
            f2: at(3, d.f2).clamp(20.0, 20000.0),
            fsel: at(4, 0.0).clamp(0.0, 1.0) as u8,
            kind: at(5, d.kind as f32).clamp(0.0, 2.0) as u8,
            dest: at(6, 0.0).clamp(0.0, 25.0) as u8,
        }
    }

    /// `text` with its `osc=` line set to this.
    pub fn write_into(&self, text: &str) -> String {
        let line =
            format!("osc={} {} {} {} {} {} {}", self.on as u8, self.level_db, self.f1, self.f2, self.fsel, self.kind, self.dest);
        let mut out: Vec<String> = text.lines().filter(|l| !l.starts_with("osc=")).map(str::to_string).collect();
        out.push(line);
        out.join("\n") + "\n"
    }

    /// Field `k` (`Bind::Osc`) as a number: levels and hertz as such, the rest as ints.
    pub fn field(&self, k: usize) -> f32 {
        match k {
            0 => self.on as u8 as f32,
            1 => self.level_db,
            2 => self.f1,
            3 => self.f2,
            4 => self.fsel as f32,
            5 => self.kind as f32,
            _ => self.dest as f32,
        }
    }

    pub fn set_field(&mut self, k: usize, v: f32) {
        match k {
            0 => self.on = v != 0.0,
            1 => self.level_db = v,
            2 => self.f1 = v.clamp(20.0, 20000.0),
            3 => self.f2 = v.clamp(20.0, 20000.0),
            4 => self.fsel = v.clamp(0.0, 1.0) as u8,
            5 => self.kind = v.clamp(0.0, 2.0) as u8,
            _ => self.dest = v.clamp(0.0, 25.0) as u8,
        }
    }

    /// Sets the engine's console oscillator to it: the selected frequency, its
    /// level (at most 0 dBFS) and its one destination.
    pub fn apply(&self, engine: &Engine) {
        let osc = &engine.oscillator;
        osc.clear();
        let (kind, i, gains) = match self.dest {
            d @ 0..=15 => (StripKind::Bus, d as usize, [1.0, 1.0]),
            16 => (StripKind::Main, 0, [1.0, 0.0]),
            17 => (StripKind::Main, 0, [0.0, 1.0]),
            18 => (StripKind::Main, 0, [1.0, 1.0]),
            19 => (StripKind::Main, 1, [1.0, 1.0]),
            d => (StripKind::Matrix, d as usize - 20, [1.0, 1.0]),
        };
        osc.assign(kind, i, gains);
        let mode = [generator_mode::SINE, generator_mode::PINK, generator_mode::WHITE][self.kind.min(2) as usize];
        let freq = if self.fsel == 0 { self.f1 } else { self.f2 };
        osc.set(self.on && self.level_db > -100.0, mode, freq, self.level_db.min(0.0));
    }
}

/// The console text's oscillator onto the engine (the host's console hook).
pub fn apply(engine: &Engine, text: &str) {
    Oscillator::from_text(text).apply(engine);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn it_round_trips_through_the_console_text() {
        let mut o = Oscillator::default();
        o.set_field(0, 1.0);
        o.set_field(1, f32::NEG_INFINITY);
        o.set_field(3, 440.0);
        o.set_field(4, 1.0);
        o.set_field(6, 22.0);
        let text = o.write_into("other=1\nosc=0 0 0 0 0 0 0\n");
        assert!(text.starts_with("other=1\n"), "{text}");
        assert_eq!(text.matches("osc=").count(), 1);
        assert_eq!(Oscillator::from_text(&text), o);
        assert_eq!(Oscillator::from_text(""), Oscillator::default());
    }

    #[test]
    fn it_plays_on_its_destination() {
        let e = Engine::new(0);
        let mut o = Oscillator { on: true, level_db: -20.0, ..Oscillator::default() };
        for (dest, kind, i, gains) in [
            (3, StripKind::Bus, 3, [1.0, 1.0]),
            (16, StripKind::Main, 0, [1.0, 0.0]),
            (17, StripKind::Main, 0, [0.0, 1.0]),
            (19, StripKind::Main, 1, [1.0, 1.0]),
            (25, StripKind::Matrix, 5, [1.0, 1.0]),
        ] {
            o.dest = dest;
            o.apply(&e);
            assert_eq!(e.oscillator.assigned(kind, i), gains, "dest {dest}");
            assert_eq!(e.oscillator.assigned(StripKind::Bus, 0), [0.0, 0.0], "only one destination");
        }
        o.fsel = 1;
        o.apply(&e);
        assert_eq!(e.oscillator.freq_hz.load(), 1000.0, "F2");
        o.level_db = f32::NEG_INFINITY;
        o.apply(&e);
        assert!(!e.oscillator.on.load(std::sync::atomic::Ordering::Relaxed), "-oo is silence");
    }
}
