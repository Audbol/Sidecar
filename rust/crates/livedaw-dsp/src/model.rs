//! Processing models in console-neutral terms: a model (a gate, a compressor,
//! an EQ, an FX device...) is a list of parameters ([`ParamSpec`]: id, label,
//! unit, range, steps, default); its state is a flat `[f32]` in that order
//! (real values for continuous parameters, 0/1 for toggles, the item index for
//! choices), read by id through [`Params`].
//!
//! A console's tables are its own (the WING's: [`crate::wing::MODELS`]); a
//! profile names the set its blocks may hold ([`ModelTables`],
//! `docs/UNIVERSAL_MIXER.md`, Phase 3 step 4). Value text follows the WING's
//! conventions for now ([`ParamSpec::console_text`]).

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ModelCategory {
    Gate,
    Compressor,
    Special,
    Eq,
    Filter,
    /// FX rack: an empty slot.
    FxNone,
    /// FX rack: a VST3/AU/LV2 plugin (the console's EXT device).
    FxExternal,
    /// FX rack: devices for slots 1-8 only.
    FxPremium,
    /// FX rack: devices for any slot.
    FxStandard,
    /// FX rack: channel strips (any slot).
    FxChannel,
}

impl ModelCategory {
    pub fn is_fx(self) -> bool {
        matches!(self, Self::FxNone | Self::FxExternal | Self::FxPremium | Self::FxStandard | Self::FxChannel)
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ParamKind {
    Linear,
    Log,
    /// Value is the index of the choice.
    Choice(&'static [&'static str]),
    /// Value is the index into a list of numbers.
    Values(&'static [f32]),
    Toggle,
    Int,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct ParamSpec {
    pub id: &'static str,
    pub label: &'static str,
    pub unit: &'static str,
    pub kind: ParamKind,
    /// May be greater than `max` (the knob runs the other way).
    pub min: f32,
    pub max: f32,
    pub steps: u32,
    pub default: f32,
}

impl ParamSpec {
    pub fn lo(&self) -> f32 {
        self.min.min(self.max)
    }

    pub fn hi(&self) -> f32 {
        self.min.max(self.max)
    }

    /// Clamps (and for discrete kinds rounds) a value into range.
    pub fn sanitise(&self, v: f32) -> f32 {
        let v = if v.is_finite() { v } else { self.default };
        let v = v.clamp(self.lo(), self.hi());
        match self.kind {
            ParamKind::Linear | ParamKind::Log => v,
            _ => v.round(),
        }
    }

    pub fn is_continuous(&self) -> bool {
        matches!(self.kind, ParamKind::Linear | ParamKind::Log)
    }

    /// Value at `pos` 0..1 along the control (log taper for log parameters).
    pub fn from_normalised(&self, pos: f32) -> f32 {
        let pos = pos.clamp(0.0, 1.0);
        let v = match self.kind {
            ParamKind::Log if self.min > 0.0 && self.max > 0.0 => self.min * (self.max / self.min).powf(pos),
            _ => self.min + (self.max - self.min) * pos,
        };
        self.sanitise(v)
    }

    pub fn to_normalised(&self, v: f32) -> f32 {
        let v = self.sanitise(v);
        let pos = match self.kind {
            ParamKind::Log if self.min > 0.0 && self.max > 0.0 => (v / self.min).ln() / (self.max / self.min).ln(),
            _ if self.max != self.min => (v - self.min) / (self.max - self.min),
            _ => 0.0,
        };
        pos.clamp(0.0, 1.0)
    }

    /// Text for a value, as the console shows it ("-20.0 dB", "1:2", "8k",
    /// "5k7 Hz", "1503 ms"; see [`ParamSpec::console_text`]).
    pub fn display(&self, v: f32) -> String {
        let v = self.sanitise(v);
        match self.kind {
            ParamKind::Choice(items) => items.get(v as usize).copied().unwrap_or("?").to_string(),
            ParamKind::Values(items) => {
                let x = items.get(v as usize).copied().unwrap_or(0.0);
                if x >= 999.0 {
                    "INF".into()
                } else {
                    format_number(x, self.unit)
                }
            }
            ParamKind::Toggle => if v >= 0.5 { "ON" } else { "OFF" }.into(),
            ParamKind::Int | ParamKind::Linear | ParamKind::Log => self.console_text(v),
        }
    }
}

impl ParamSpec {
    /// A number's text as the console's screens show it (the WING's own text
    /// for the parameter, as its remote also writes it, with the unit): fixed
    /// decimals per parameter, kHz with the k as the decimal point and times
    /// in ms however long ("5k7 Hz", "1.57 s", "0.95", "50 ms", "1503 ms";
    /// manual Fig. 124 and 132, WING Edit's DUCKER and DYNAMIC EQ).
    pub fn console_text(&self, v: f32) -> String {
        let v = self.sanitise(v);
        let number = match self.kind {
            ParamKind::Int => format!("{}", v as i64),
            ParamKind::Linear | ParamKind::Log => {
                let d = self.console_decimals().unwrap_or(if v.abs() >= 100.0 {
                    0
                } else if v.abs() >= 10.0 {
                    1
                } else {
                    2
                });
                if self.unit == "Hz" {
                    console_hz(v, d)
                } else {
                    format!("{v:.d$}")
                }
            }
            _ => return self.display(v),
        };
        if self.unit.is_empty() {
            number
        } else {
            format!("{number} {}", self.unit)
        }
    }

    /// The decimals the console gives a float parameter: as many as a linear
    /// step needs (0.1 -> 1, 0.25 -> 2, 5 -> 0); for a log one by its range
    /// (two below 1, one below 10, none from 10 up and for ms; tenths of a
    /// hertz for the fine EQ frequencies).
    fn console_decimals(&self) -> Option<usize> {
        if self.steps < 2 || self.max == self.min {
            return None;
        }
        let lo = self.lo();
        Some(match self.kind {
            ParamKind::Linear => {
                let step = (self.max - self.min).abs() as f64 / (self.steps - 1) as f64;
                (0..3)
                    .find(|&d| {
                        let x = step * 10f64.powi(d);
                        (x - x.round()).abs() < 1e-3 * x.max(1.0)
                    })
                    .unwrap_or(3) as usize
            }
            ParamKind::Log if self.unit == "Hz" && lo >= 10.0 => {
                let per_decade = (self.steps - 1) as f32 / (self.max / self.min).abs().log10();
                usize::from(self.steps % 2 == 1 && per_decade >= 319.0)
            }
            ParamKind::Log if lo < 1.0 => 2,
            ParamKind::Log if self.unit == "ms" => 0,
            ParamKind::Log if lo < 10.0 => 1,
            ParamKind::Log => 0,
            _ => return None,
        })
    }
}

/// Hertz as the console writes them: "84", "998.6", from 1 kHz up with the k
/// as the decimal point and one digit more than the parameter's decimals
/// ("5k7", "1k00", "20k00").
fn console_hz(v: f32, decimals: usize) -> String {
    let text = format!("{v:.decimals$}");
    if text.parse::<f32>().is_ok_and(|r| r.abs() < 1000.0) {
        return text;
    }
    let digits = decimals + 1;
    let scale = 10i64.pow(digits as u32);
    let n = (v as f64 / 1000.0 * scale as f64).round() as i64;
    format!("{}k{:0digits$}", n / scale, n % scale)
}

fn format_number(v: f32, unit: &str) -> String {
    match unit {
        "Hz" if v >= 1000.0 => format!("{:.2} kHz", v / 1000.0),
        // LFO rates below 10 Hz ("0.50 Hz"), filter frequencies whole.
        "Hz" if v < 10.0 => format!("{v:.2} Hz"),
        "Hz" => format!("{v:.0} Hz"),
        "ms" if v >= 1000.0 => format!("{:.2} s", v / 1000.0),
        "ms" if v < 10.0 => format!("{v:.1} ms"),
        "ms" => format!("{v:.0} ms"),
        // Reverb decays as the console writes them: "1.35 s", "1.50 s".
        "s" => format!("{v:.2} s"),
        "" => {
            if v.abs() >= 100.0 {
                format!("{v:.0}")
            } else {
                format!("{v:.2}")
            }
        }
        u => format!("{v:.1} {u}"),
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct ModelSpec {
    pub token: &'static str,
    pub name: &'static str,
    pub category: ModelCategory,
    pub params: &'static [ParamSpec],
}

impl ModelSpec {
    pub fn param_index(&self, id: &str) -> Option<usize> {
        self.params.iter().position(|p| p.id == id)
    }

    /// Writes the model's defaults into `out` (the rest is zeroed).
    pub fn defaults_into(&self, out: &mut [f32]) {
        out.fill(0.0);
        for (o, p) in out.iter_mut().zip(self.params) {
            *o = p.default;
        }
    }
}

/// Finds a model by token in a table.
pub fn model_index(table: &[ModelSpec], token: &str) -> Option<usize> {
    table.iter().position(|m| m.token == token)
}

/// Read access to a model's parameter values by id.
#[derive(Clone, Copy)]
pub struct Params<'a> {
    pub spec: &'a ModelSpec,
    pub values: &'a [f32],
}

impl<'a> Params<'a> {
    pub fn new(spec: &'a ModelSpec, values: &'a [f32]) -> Self {
        Self { spec, values }
    }

    fn raw(&self, id: &str) -> Option<(f32, &ParamSpec)> {
        let i = self.spec.param_index(id)?;
        let p = &self.spec.params[i];
        Some((p.sanitise(self.values.get(i).copied().unwrap_or(p.default)), p))
    }

    /// Continuous value (or the number a Values item stands for).
    pub fn f(&self, id: &str) -> f32 {
        match self.raw(id) {
            Some((v, p)) => match p.kind {
                ParamKind::Values(items) => items.get(v as usize).copied().unwrap_or(0.0),
                _ => v,
            },
            None => 0.0,
        }
    }

    pub fn on(&self, id: &str) -> bool {
        self.raw(id).is_some_and(|(v, _)| v >= 0.5)
    }

    /// Choice index.
    pub fn choice(&self, id: &str) -> usize {
        self.raw(id).map_or(0, |(v, _)| v.max(0.0) as usize)
    }

    /// Choice text ("" if missing).
    pub fn text(&self, id: &str) -> &'static str {
        match self.raw(id) {
            Some((v, p)) => match p.kind {
                ParamKind::Choice(items) => items.get(v as usize).copied().unwrap_or(""),
                _ => "",
            },
            None => "",
        }
    }

    /// A choice like "1k6", "35", "OFF" parsed as Hz (None for OFF/unparsable).
    pub fn freq_choice(&self, id: &str) -> Option<f32> {
        parse_freq(self.text(id))
    }
}

/// Parses console frequency labels: "35", "1k6", "3k", "10k", "1k5".
pub fn parse_freq(s: &str) -> Option<f32> {
    if s.is_empty() || s.eq_ignore_ascii_case("off") {
        return None;
    }
    if let Some((a, b)) = s.split_once('k') {
        // "1k6" = 1.6 kHz; parsed without allocating (called on the audio thread).
        let whole: f32 = if a.is_empty() { 0.0 } else { a.parse().ok()? };
        let frac: f32 = if b.is_empty() { 0.0 } else { b.parse::<u32>().ok()? as f32 / 10f32.powi(b.len() as i32) };
        return Some((whole + frac) * 1000.0);
    }
    s.parse().ok()
}

/// The models a console's blocks may hold: what each block's model menu
/// offers. Tables are numbered as the host's model calls number them
/// ([`ModelTables::table`]).
#[derive(Debug, Clone, Copy)]
pub struct ModelTables {
    /// The gate and dynamics slots of the input channels.
    pub dynamics: &'static [ModelSpec],
    /// The dynamics models (tokens of `dynamics`) the aux channels may hold.
    pub aux_dynamics: &'static [&'static str],
    /// The EQ of the input channels, and of buses, mains and matrices.
    pub eq_channel: &'static [ModelSpec],
    pub eq_bus: &'static [ModelSpec],
    /// The filter block's tools.
    pub filter_tools: &'static [ModelSpec],
    /// The FX rack's devices.
    pub fx: &'static [ModelSpec],
    /// The FX slots (from the first) that take `FxPremium` devices: the WING's
    /// 1-8, the X32's 1-4 (its send / return slots).
    pub fx_premium_slots: usize,
    /// The models a new strip's GATE and DYNAMICS slots start on (tokens of
    /// `dynamics`).
    pub gate_default: &'static str,
    pub comp_default: &'static str,
}

impl ModelTables {
    /// Table `t`: 0 dynamics, 1 channel EQ, 2 bus EQ, 3 filter tools, 4 FX
    /// (anything else: dynamics).
    pub fn table(&self, t: i32) -> &'static [ModelSpec] {
        match t {
            1 => self.eq_channel,
            2 => self.eq_bus,
            3 => self.filter_tools,
            4 => self.fx,
            _ => self.dynamics,
        }
    }

    /// Whether an aux channel may hold dynamics model `token`.
    pub fn aux_may_hold(&self, token: &str) -> bool {
        self.aux_dynamics.contains(&token)
    }
}
