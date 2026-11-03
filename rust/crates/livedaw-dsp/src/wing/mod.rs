//! WING-style processing models: the parameter tables of every gate/dynamics,
//! EQ and filter-tool model, and their DSP.
//!
//! The tables are described in the console-neutral terms of [`crate::model`]
//! (re-exported here); [`MODELS`] is the set the WING's profile names.
//! Every model is a list of [`ParamSpec`]s. A model's state is a flat `[f32]`
//! in that order: real values for continuous parameters, 0/1 for toggles and
//! the item index for choices. The DSP reads values by parameter id through
//! [`Params`], so the tables (generated from the console's parameter tree) are
//! the single source of truth for names, ranges and defaults.
//!
//! The models reproduce each processor's controls and characteristic behaviour
//! (detector, knee, time constants, curve shapes, colouration). They are not
//! circuit-level emulations.

pub mod dynamics;
pub mod eq;
pub mod filter;
pub mod fx;
mod models_gen;

pub use crate::model::{model_index, parse_freq, ModelCategory, ModelSpec, ModelTables, ParamKind, ParamSpec, Params};
pub use models_gen::{
    AUX_DYN_MODELS, DYN_MODELS, EQ_MODELS_BUS, EQ_MODELS_CH, FILTER_TOOLS, FX_MODELS, PROCESS_ORDERS, TAP_POINTS,
};

/// The WING's models, as its profile names them (`livedaw_engine::profile`).
pub static MODELS: ModelTables = ModelTables {
    dynamics: DYN_MODELS,
    aux_dynamics: AUX_DYN_MODELS,
    eq_channel: EQ_MODELS_CH,
    eq_bus: EQ_MODELS_BUS,
    filter_tools: FILTER_TOOLS,
    fx: FX_MODELS,
    gate_default: "GATE",
    comp_default: "COMP",
};

/// Parameter slots per dynamics model (the largest, DEQ2, uses 18).
pub const MAX_DYN_PARAMS: usize = 20;
/// Parameter slots per EQ model (the bus STD EQ uses 27).
pub const MAX_EQ_PARAMS: usize = 32;
/// Parameter slots per filter tool.
pub const MAX_TOOL_PARAMS: usize = 2;
/// Parameter slots per FX device (the graphic EQ and the channel strips use 33).
pub const MAX_FX_PARAMS: usize = 36;

/// Maps a 0..10 dial to `lo..hi` logarithmically (analog-style frequency knobs).
pub fn dial_log(dial: f32, lo: f32, hi: f32) -> f32 {
    lo * (hi / lo).powf((dial / 10.0).clamp(0.0, 1.0))
}

/// Maps a 0..10 dial to `lo..hi` linearly.
pub fn dial_lin(dial: f32, lo: f32, hi: f32) -> f32 {
    lo + (hi - lo) * (dial / 10.0).clamp(0.0, 1.0)
}

/// Gentle, level-preserving saturation used for analog colouration.
#[inline]
pub fn colour(x: f32, amount: f32) -> f32 {
    if amount <= 0.0 {
        x
    } else {
        (1.0 - amount) * x + amount * x.tanh()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tables_are_consistent() {
        assert_eq!(DYN_MODELS.len(), 32);
        assert_eq!(EQ_MODELS_CH.len(), 7);
        assert_eq!(EQ_MODELS_BUS.len(), 7);
        assert_eq!(PROCESS_ORDERS.len(), 24);
        assert_eq!(FX_MODELS.len(), 65);
        assert_eq!(FX_MODELS.iter().filter(|m| m.category == ModelCategory::FxStandard).count(), 32);
        assert_eq!(FX_MODELS.iter().filter(|m| m.category == ModelCategory::FxPremium).count(), 26);
        assert_eq!(FX_MODELS.iter().filter(|m| m.category == ModelCategory::FxChannel).count(), 5);
        for table in [DYN_MODELS, EQ_MODELS_CH, EQ_MODELS_BUS, FILTER_TOOLS, FX_MODELS] {
            for m in table {
                let max = match m.category {
                    ModelCategory::Eq => MAX_EQ_PARAMS,
                    ModelCategory::Filter => MAX_TOOL_PARAMS,
                    c if c.is_fx() => MAX_FX_PARAMS,
                    _ => MAX_DYN_PARAMS,
                };
                assert!(m.params.len() <= max, "{} has {} params", m.token, m.params.len());
                for p in m.params {
                    assert_eq!(p.sanitise(p.default), p.default, "{}/{} default out of range", m.token, p.id);
                }
            }
        }
        for t in AUX_DYN_MODELS {
            assert!(model_index(DYN_MODELS, t).is_some());
        }
    }

    #[test]
    fn values_and_display() {
        let comp = &DYN_MODELS[model_index(DYN_MODELS, "COMP").unwrap()];
        let mut v = [0.0; MAX_DYN_PARAMS];
        comp.defaults_into(&mut v);
        let p = Params::new(comp, &v);
        // As a factory-set console: THR -10 dB, 3:1, KNEE 3, RMS, AUTO ENV on.
        assert_eq!(p.f("thr"), -10.0);
        assert_eq!(p.f("ratio"), 3.0);
        assert_eq!(p.f("knee"), 3.0);
        assert_eq!(p.text("det"), "RMS");
        assert!(p.on("auto"));
        let rel = &comp.params[comp.param_index("rel").unwrap()];
        assert!((rel.from_normalised(rel.to_normalised(150.0)) - 150.0).abs() < 0.01);
        assert_eq!(rel.display(1500.0), "1500 ms");
        assert_eq!(rel.display(rel.default), "150 ms");
        // A reverb's DECAY keeps two decimals, as the rack shows it.
        let decay =
            ParamSpec { id: "dcy", label: "DECAY", unit: "s", kind: ParamKind::Log, min: 0.1, max: 5.0, steps: 51, default: 1.8 };
        assert_eq!(decay.display(1.5), "1.50 s");
        assert_eq!(parse_freq("1k6"), Some(1600.0));
        assert_eq!(parse_freq("35"), Some(35.0));
        assert_eq!(parse_freq("OFF"), None);
        assert!((dial_log(10.0, 30.0, 450.0) - 450.0).abs() < 0.01);
    }

    #[test]
    fn fx_values_read_as_the_console_writes_them() {
        // The HALL at its defaults, as manual Fig. 124 shows it: PRE DLY 50 ms,
        // SIZE 50, DECAY 1.57 s, BASS MULT 0.95, DAMPING 5k7 Hz, DIFFUSION 25.
        let hall = &FX_MODELS[model_index(FX_MODELS, "HALL").unwrap()];
        let text: Vec<String> = ["pdel", "size", "dcy", "mult", "damp", "diff"]
            .iter()
            .map(|id| {
                let p = &hall.params[hall.param_index(id).unwrap()];
                p.console_text(p.default)
            })
            .collect();
        assert_eq!(text, ["50 ms", "50", "1.57 s", "0.95", "5k7 Hz", "25"]);
        // DELAY/REVERB, Fig. 132: TIME 240 ms, PRE DLY 24 ms; times stay in ms.
        let dr = &FX_MODELS[model_index(FX_MODELS, "DEL/REV").unwrap()];
        let time = &dr.params[dr.param_index("time").unwrap()];
        assert_eq!(time.console_text(time.default), "240 ms");
        assert_eq!(time.console_text(1500.0), "1500 ms");
        let pdel = &dr.params[dr.param_index("pdel").unwrap()];
        assert_eq!(pdel.console_text(pdel.default), "24 ms");
        // Rates below 1 Hz with two decimals, percentages whole, fine EQ frequencies in tenths.
        let chorus = &FX_MODELS[model_index(FX_MODELS, "CHORUS").unwrap()];
        let spd = &chorus.params[chorus.param_index("spd").unwrap()];
        assert_eq!(spd.console_text(0.5), "0.50 Hz");
        let mix = &chorus.params[chorus.param_index("mix").unwrap()];
        assert_eq!(mix.console_text(50.0), "50 %");
        let deq = &FX_MODELS[model_index(FX_MODELS, "DEQ3").unwrap()];
        let f = &deq.params[deq.param_index("1-f").unwrap()];
        assert_eq!((f.console_text(149.9788), f.console_text(1002.374)), ("150.0 Hz".into(), "1k00 Hz".into()));
    }

    #[test]
    fn wing_models_start_as_a_factory_set_console() {
        let values = |table: &'static [ModelSpec], token| {
            let spec = &table[model_index(table, token).unwrap()];
            let mut v = [0.0; MAX_EQ_PARAMS];
            spec.defaults_into(&mut v);
            (spec, v)
        };
        // GATE: THR -40.0 dB, RATIO 1:3, RANGE 40.0 dB, ATTACK 10 ms, HOLD 10 ms, RELEASE 200 ms, ACCENT 0 %.
        let (gate, v) = values(DYN_MODELS, "GATE");
        let p = Params::new(gate, &v);
        assert_eq!(gate.name, "Wing Gate");
        assert_eq!((p.f("thr"), p.text("ratio"), p.f("range")), (-40.0, "1:3", 40.0));
        assert_eq!((p.f("att"), p.f("hld"), p.f("rel"), p.f("acc")), (10.0, 10.0, 200.47488, 0.0));
        // COMP: ATTACK 50 ms, HOLD 20 ms, RELEASE 150 ms.
        let (comp, v) = values(DYN_MODELS, "COMP");
        let p = Params::new(comp, &v);
        assert_eq!((p.f("att"), p.f("hld"), p.f("rel")), (50.0, 20.0, 150.33495));
        // The channel EQ's bands on the frequency steps: band 4 reads 3k99 Hz.
        let (eq, v) = values(EQ_MODELS_CH, "STD");
        let p = Params::new(eq, &v);
        let freqs = ["lf", "1f", "2f", "3f", "4f", "hf"].map(|id| p.f(id));
        for (f, want) in freqs.iter().zip([80.0, 200.0, 600.0, 1500.0, 4000.0, 12000.0]) {
            assert!((f / want - 1.0).abs() < 0.005, "{f} Hz for {want} Hz");
        }
        assert!((freqs[4] - 3990.5).abs() < 0.1);
        // Q 1.00 on its steps, the shelves too.
        assert_eq!((p.f("4q"), p.f("lq"), p.f("4g")), (0.9979702, 0.9979702, 0.0));
    }
}
