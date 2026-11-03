//! The X32's parameter types and how their values travel: floats as 0..1 on
//! the console's own steps (levels on the fader law, `linf` and `logf`
//! scales), enums and ints as ints (enums also by name), bitmaps, strings;
//! and the text forms `/node` answers and `/` takes, in real units.

use livedaw_dsp::fader::{FaderLaw, FADER_MIN};
use livedaw_wing::osc::Arg;

/// Where a float parameter's 0..1 sits in real units.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Law {
    /// The level law (`FaderLaw::X32`'s four segments) on this many steps:
    /// 1024 for faders, 161 for send levels and `mlevel`. 0 is -oo.
    Level(u32),
    /// `linf`: `min + f * (max - min)` on `steps` values.
    Lin(f32, f32, u32),
    /// `logf`: `min * (max / min) ^ f` on `steps` values (`max` may be the lower).
    Log(f32, f32, u32),
}

impl Law {
    fn steps(&self) -> u32 {
        match *self {
            Law::Level(n) | Law::Lin(_, _, n) | Law::Log(_, _, n) => n.max(2),
        }
    }

    /// The nearest 0..1 the console knows.
    pub fn snap(&self, f: f32) -> f32 {
        let n = (self.steps() - 1) as f32;
        let f = if f.is_finite() { f.clamp(0.0, 1.0) } else { 0.0 };
        (f * n).round() / n
    }

    fn fader(&self, steps: u32) -> FaderLaw {
        FaderLaw { points: FaderLaw::X32.points, steps }
    }

    /// The real value at `f` (snapped first). Levels: -oo is `FADER_MIN`;
    /// their floats are rounded as the protocol document gives it for the
    /// faders (`int(f * 1023.5) / 1023`: 0.5 is step 511, not 512).
    pub fn to_real(&self, f: f32) -> f32 {
        let f = match *self {
            Law::Level(_) => {
                let n = (self.steps() - 1) as f32;
                let f = if f.is_finite() { f.clamp(0.0, 1.0) } else { 0.0 };
                (f * (n + 0.5)).floor().min(n) / n
            }
            _ => self.snap(f),
        };
        match *self {
            Law::Level(n) => self.fader(n).to_db(f),
            Law::Lin(min, max, _) => min + f * (max - min),
            Law::Log(min, max, _) => min * (max / min).powf(f),
        }
    }

    /// The 0..1 of a real value, on a step.
    pub fn to_wire(&self, v: f32) -> f32 {
        match *self {
            Law::Level(n) => self.fader(n).to_pos(v),
            Law::Lin(min, max, _) => self.snap(if max != min { (v - min) / (max - min) } else { 0.0 }),
            Law::Log(min, max, _) => {
                let v = if v.is_finite() && v > 0.0 { v } else { min };
                self.snap((v / min).ln() / (max / min).ln())
            }
        }
    }

    /// A real value moved onto the nearest step.
    pub fn quantise(&self, v: f32) -> f32 {
        self.to_real(self.to_wire(v))
    }
}

/// How a float is written in the text forms.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Fmt {
    /// dB with one decimal, -oo at the bottom, right-aligned in 5.
    Level,
    /// Fixed decimals, a sign on 0 and up if `sign`, right-aligned in `width`.
    Num { dec: u8, sign: bool, width: u8 },
    /// Hertz: "124.7" below 1 kHz, "1k97" and "10k02" from there.
    Freq,
    /// Milliseconds on a log scale: "0.02", "50.2", "258", right-aligned in `width`.
    Time { width: u8 },
}

/// A parameter's type, as the protocol document lists it.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Ty {
    Float(Law, Fmt),
    /// An int that also goes by name (`GATE` or 3).
    Enum(&'static [&'static str]),
    Int(i32, i32),
    /// A bitmap of this many bits (`%00000101` in text).
    Bits(u32),
    /// A string of at most this many characters.
    Str(usize),
}

/// A parameter's value: real units for floats, the int for enums, ints and
/// bitmaps.
#[derive(Clone, Debug, PartialEq)]
pub enum Val {
    Real(f32),
    Int(i32),
    Text(String),
}

impl Val {
    pub fn real(&self) -> f32 {
        match self {
            Val::Real(v) => *v,
            Val::Int(i) => *i as f32,
            Val::Text(_) => 0.0,
        }
    }

    pub fn int(&self) -> i32 {
        match self {
            Val::Real(v) => v.round() as i32,
            Val::Int(i) => *i,
            Val::Text(_) => 0,
        }
    }

    pub fn on(&self) -> bool {
        self.int() != 0
    }
}

impl Ty {
    /// A value made one the parameter can hold (on a step, in range).
    pub fn normalise(&self, v: Val) -> Val {
        match *self {
            Ty::Float(law, _) => Val::Real(law.quantise(v.real())),
            Ty::Enum(items) => Val::Int(v.int().clamp(0, items.len() as i32 - 1)),
            Ty::Int(min, max) => Val::Int(v.int().clamp(min, max)),
            Ty::Bits(n) => Val::Int(v.int() & mask(n)),
            Ty::Str(len) => match v {
                Val::Text(s) => Val::Text(s.chars().filter(|c| *c != '\0').take(len).collect()),
                _ => Val::Text(String::new()),
            },
        }
    }

    /// The value as a get reply carries it.
    pub fn to_arg(&self, v: &Val) -> Arg {
        match *self {
            Ty::Float(law, _) => Arg::Float(law.to_wire(v.real())),
            Ty::Str(_) => Arg::Str(match v {
                Val::Text(s) => s.clone(),
                _ => String::new(),
            }),
            _ => Arg::Int(self.normalise(v.clone()).int()),
        }
    }

    /// The value a set carries, if the parameter takes it: floats as floats,
    /// enums as ints or names, ints and bitmaps as ints, strings as strings.
    pub fn from_arg(&self, a: &Arg) -> Option<Val> {
        let v = match (*self, a) {
            (Ty::Float(law, _), Arg::Float(f)) => Val::Real(law.to_real(*f)),
            (Ty::Enum(items), Arg::Int(i)) if (0..items.len() as i32).contains(i) => Val::Int(*i),
            (Ty::Enum(items), Arg::Str(s)) => Val::Int(items.iter().position(|x| x.eq_ignore_ascii_case(s))? as i32),
            (Ty::Int(min, max), Arg::Int(i)) if (min..=max).contains(i) => Val::Int(*i),
            (Ty::Bits(n), Arg::Int(i)) => Val::Int(i & mask(n)),
            (Ty::Str(_), Arg::Str(s)) => Val::Text(s.clone()),
            _ => return None,
        };
        Some(self.normalise(v))
    }

    /// The value in the text forms (`/node`, `/`, scene files).
    pub fn text(&self, v: &Val) -> String {
        match *self {
            Ty::Float(law, fmt) => float_text(law, fmt, v.real()),
            Ty::Enum(items) => items.get(self.normalise(v.clone()).int() as usize).copied().unwrap_or("").to_owned(),
            Ty::Int(..) => self.normalise(v.clone()).int().to_string(),
            Ty::Bits(n) => format!("%{:0w$b}", self.normalise(v.clone()).int(), w = n as usize),
            Ty::Str(_) => match v {
                Val::Text(s) => format!("\"{}\"", s.replace('"', "")),
                _ => "\"\"".to_owned(),
            },
        }
    }

    /// A value from its text: real units ("-20.5", "-oo", "1k97"), enum names
    /// or indexes, `%0101` bitmaps, (unquoted) strings.
    pub fn parse(&self, t: &str) -> Option<Val> {
        let v = match *self {
            Ty::Float(_, fmt) => {
                let t = t.trim();
                Val::Real(if t.eq_ignore_ascii_case("-oo") || t.eq_ignore_ascii_case("-inf") {
                    FADER_MIN
                } else if fmt == Fmt::Freq {
                    livedaw_dsp::model::parse_freq(t)?
                } else {
                    t.parse::<f32>().ok().filter(|v| v.is_finite())?
                })
            }
            Ty::Enum(items) => match items.iter().position(|x| x.eq_ignore_ascii_case(t)) {
                Some(i) => Val::Int(i as i32),
                None => Val::Int(t.parse::<i32>().ok().filter(|i| (0..items.len() as i32).contains(i))?),
            },
            Ty::Int(min, max) => Val::Int(t.parse::<f32>().ok().map(|v| v.round() as i32).filter(|i| (min..=max).contains(i))?),
            Ty::Bits(_) => match t.strip_prefix('%') {
                Some(b) => Val::Int(i32::from_str_radix(b, 2).ok()?),
                None => Val::Int(t.parse().ok()?),
            },
            Ty::Str(_) => Val::Text(t.to_owned()),
        };
        Some(self.normalise(v))
    }
}

fn mask(bits: u32) -> i32 {
    if bits >= 31 {
        i32::MAX
    } else {
        (1 << bits) - 1
    }
}

fn pad(s: String, width: u8) -> String {
    format!("{s:>w$}", w = width as usize)
}

/// A number with `dec` decimals; "-0.0" is written "0.0".
fn fixed(v: f32, dec: u8, sign: bool) -> String {
    let d = dec as usize;
    let text = format!("{v:.d$}");
    let zero = text.trim_start_matches('-').chars().all(|c| c == '0' || c == '.');
    let text = if zero { text.trim_start_matches('-').to_owned() } else { text };
    if sign && !text.starts_with('-') {
        format!("+{text}")
    } else {
        text
    }
}

fn float_text(law: Law, fmt: Fmt, v: f32) -> String {
    let wire = law.to_wire(v);
    let v = law.to_real(wire);
    match fmt {
        Fmt::Level => pad(if wire <= 0.0 { "-oo".to_owned() } else { fixed(v, 1, false) }, 5),
        Fmt::Num { dec, sign, width } => pad(fixed(v, dec, sign), width),
        Fmt::Freq => {
            if v < 999.95 {
                format!("{v:.1}")
            } else {
                // Cut, not rounded, as the console does (1977 Hz is "1k97").
                let hundredths = (v / 10.0 + 1e-3).floor() as i64;
                format!("{}k{:02}", hundredths / 100, hundredths % 100)
            }
        }
        Fmt::Time { width } => {
            let dec = if v < 1.0 {
                2
            } else if v < 100.0 {
                1
            } else {
                0
            };
            pad(fixed(v, dec, false), width)
        }
    }
}

/// Splits `/` text into fields: blanks separate them, double quotes keep a
/// string with blanks whole.
pub fn fields(text: &str) -> Vec<String> {
    let mut out = Vec::new();
    let mut cur = String::new();
    let (mut quoted, mut any) = (false, false);
    for c in text.chars() {
        match c {
            '"' => {
                quoted = !quoted;
                any = true;
            }
            c if c.is_whitespace() && !quoted => {
                if any {
                    out.push(std::mem::take(&mut cur));
                    any = false;
                }
            }
            c => {
                cur.push(c);
                any = true;
            }
        }
    }
    if any {
        out.push(cur);
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    const FADER: Ty = Ty::Float(Law::Level(1024), Fmt::Level);
    const SEND: Ty = Ty::Float(Law::Level(161), Fmt::Level);
    const FREQ: Ty = Ty::Float(Law::Log(20.0, 20000.0, 201), Fmt::Freq);
    const Q: Ty = Ty::Float(Law::Log(10.0, 0.3, 72), Fmt::Num { dec: 1, sign: false, width: 0 });

    #[test]
    fn the_level_law() {
        let l = Law::Level(1024);
        // The protocol document's examples: 0 dB, +3 dB (0.8250), -oo.
        assert!((l.to_real(0.75) - 0.0).abs() < 0.03);
        assert!((l.to_real(0.825) - 3.0).abs() < 0.03);
        assert_eq!(l.to_real(0.0), FADER_MIN);
        // 1024 steps: k / 1023 (0 dB is step 767, as the console answers it).
        assert_eq!(l.to_wire(0.0), 767.0 / 1023.0);
        assert_eq!(l.to_wire(l.to_real(0.8250)), 844.0 / 1023.0);
        assert_eq!(l.to_wire(l.to_real(0.5)), 511.0 / 1023.0, "int(f * 1023.5) / 1023");
        assert_eq!(l.to_wire(l.to_real(1.0)), 1.0);
        for k in 0..1024 {
            let f = k as f32 / 1023.0;
            assert_eq!(l.to_wire(l.to_real(f)), f, "step {k} stays");
        }
        // Sends: 161 steps land on whole dB, 3 dB apart, from -90 to -60.
        let s = Law::Level(161);
        assert!((s.to_real(1.0 / 160.0) + 87.0).abs() < 1e-3);
        assert!((s.to_real(10.0 / 160.0) + 60.0).abs() < 1e-3);
        assert_eq!(s.to_wire(-3.3), s.to_wire(s.quantise(-3.3)));
    }

    #[test]
    fn texts_as_the_console_writes_them() {
        assert_eq!(FADER.text(&Val::Real(FADER_MIN)), "  -oo");
        // 0 dB is -0.01 dB on its step: written 0.0.
        assert_eq!(FADER.text(&Val::Real(0.0)), "  0.0");
        assert_eq!(FADER.text(&Val::Real(-20.5)), "-20.5");
        assert_eq!(SEND.text(&Val::Real(-90.0)), "  -oo");
        // The EQ's factory bands as an X32 writes them (124.7, 496.6, 1k97, 10k02;
        // buses 79.6, 158.9, 5k02).
        let bands = [(53, "124.7"), (93, "496.6"), (133, "1k97"), (180, "10k02"), (40, "79.6"), (60, "158.9"), (160, "5k02")];
        for (k, t) in bands.into_iter().chain([(0, "20.0"), (200, "20k00")]) {
            assert_eq!(FREQ.text(&Val::Real(Law::Log(20.0, 20000.0, 201).to_real(k as f32 / 200.0))), t, "{k}");
        }
        assert_eq!(Q.text(&Val::Real(2.0)), "2.0");
        let pan = Ty::Float(Law::Lin(-100.0, 100.0, 101), Fmt::Num { dec: 0, sign: true, width: 0 });
        assert_eq!((pan.text(&Val::Real(0.0)), pan.text(&Val::Real(-100.0))), ("+0".into(), "-100".into()));
        let hold = Ty::Float(Law::Log(0.02, 2000.0, 101), Fmt::Time { width: 0 });
        assert_eq!(hold.text(&Val::Real(Law::Log(0.02, 2000.0, 101).to_real(0.68))), "50.2");
        let release = Ty::Float(Law::Log(5.0, 4000.0, 101), Fmt::Time { width: 4 });
        assert_eq!(release.text(&Val::Real(Law::Log(5.0, 4000.0, 101).to_real(0.59))), " 258");
        assert_eq!(Ty::Bits(8).text(&Val::Int(5)), "%00000101");
        assert_eq!(Ty::Str(12).text(&Val::Text(String::new())), "\"\"");
        assert_eq!(Ty::Enum(&["OFF", "ON"]).text(&Val::Int(1)), "ON");
    }

    #[test]
    fn values_from_the_wire_and_from_text() {
        let gate = Ty::Enum(&["EXP2", "EXP3", "EXP4", "GATE", "DUCK"]);
        assert_eq!(gate.from_arg(&Arg::Str("GATE".into())), Some(Val::Int(3)));
        assert_eq!(gate.from_arg(&Arg::Int(3)), Some(Val::Int(3)));
        assert_eq!(gate.from_arg(&Arg::Int(9)), None);
        assert_eq!(gate.from_arg(&Arg::Float(0.5)), None);
        assert_eq!(FADER.from_arg(&Arg::Int(1)), None, "a float takes a float");
        assert_eq!(FADER.parse("-oo"), Some(Val::Real(FADER_MIN)));
        assert_eq!(FADER.parse("-90"), Some(Val::Real(FADER_MIN)), "the bottom is -oo");
        assert!((FADER.parse("-20.5").unwrap().real() + 20.5).abs() < 0.05);
        assert!((FREQ.parse("1k97").unwrap().real() - 1972.0).abs() < 15.0);
        assert_eq!(Ty::Bits(6).parse("%000101"), Some(Val::Int(5)));
        assert_eq!(gate.parse("duck"), Some(Val::Int(4)));
        assert_eq!(Ty::Str(12).from_arg(&Arg::Str("A very long name".into())), Some(Val::Text("A very long ".into())));
        assert_eq!(fields(r#"/ch/01/config "Kick Drum" 3 YE 1"#), ["/ch/01/config", "Kick Drum", "3", "YE", "1"]);
        assert_eq!(fields(r#"x "" 1"#), ["x", "", "1"]);
    }
}
