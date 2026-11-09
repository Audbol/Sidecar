//! The Yamaha 01V (the original, Owner's Manual): its EQ and dynamics are the
//! 02R's (`crate::y02r`: the same bands, 120 frequencies, ranges and six
//! dynamics types, pp.66, 167-172); its effects are its own, and channels 17-24
//! have only the EQ's LOW and HIGH bands (p.264).

use crate::model::{ModelCategory, ModelSpec, ModelTables, ParamSpec};
use crate::y02r;

pub mod fx;
pub use fx::FX_MODELS;

/// The 2-band EQ's parameters: the 02R EQ's LOW, HIGH, HPF and LPF.
const TWO_BAND_PARAMS: [ParamSpec; 8] = {
    let e = y02r::EQ_PARAMS;
    [e[0], e[1], e[2], e[9], e[10], e[11], e[12], e[13]]
};

/// The EQ of channels 17-24 (an Option I/O card's): LOW and HIGH (p.264).
pub const TWO_BAND_EQ: ModelSpec =
    ModelSpec { token: "O1EQ2", name: "2-Band PEQ", category: ModelCategory::Eq, params: &TWO_BAND_PARAMS };

/// The channels' and returns' EQs: the 4-band of channels 1-16 and the returns,
/// then channels 17-24's. Each of those strips keeps its own.
pub static EQ: [ModelSpec; 2] = [y02r::EQ_MODEL, TWO_BAND_EQ];

/// The model of the 2-band EQ in `EQ`.
pub const TWO_BAND_EQ_INDEX: usize = 1;

/// The 01V's models: PITCH and FREEZE run on Effect 2 only (p.119).
pub static MODELS: ModelTables = ModelTables {
    dynamics: y02r::DYN_MODELS,
    aux_dynamics: &[],
    eq_channel: &EQ,
    eq_bus: y02r::EQ,
    filter_tools: crate::wing::FILTER_TOOLS,
    fx: &fx::FX_MODELS,
    fx_premium_first: 1,
    fx_premium_slots: 2,
    gate_default: "Y2GAT",
    comp_default: "Y2CMP",
};

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::model_index;
    use crate::wing::eq::design;
    use crate::wing::fx::allowed_in;

    /// The default of each parameter of `spec`, as its table holds them.
    fn defaults(spec: &ModelSpec) -> Vec<f32> {
        spec.params.iter().map(|p| p.default).collect()
    }

    #[test]
    fn the_two_band_eq_cuts_like_the_four_bands_outer_ones() {
        let (four, two) = (&EQ[0], &EQ[TWO_BAND_EQ_INDEX]);
        let mut a = defaults(four);
        let mut b = defaults(two);
        let set = |spec: &ModelSpec, v: &mut Vec<f32>, id: &str, x: f32| {
            let i = spec.params.iter().position(|p| p.id == id).unwrap();
            v[i] = x;
        };
        for (spec, v) in [(four, &mut a), (two, &mut b)] {
            set(spec, v, "lg", 9.0);
            set(spec, v, "hg", -6.0);
        }
        for f in [40.0, 125.0, 1000.0, 10000.0, 16000.0] {
            let (x, y) = (design(four, &a, 48000.0, 1.0), design(two, &b, 48000.0, 1.0));
            assert!((x.response_db(f, 48000.0) - y.response_db(f, 48000.0)).abs() < 0.01, "{f}");
        }
        assert!(design(two, &b, 48000.0, 1.0).response_db(40.0, 48000.0) > 6.0);
    }

    #[test]
    fn pitch_and_freeze_run_on_effect_2_only() {
        let at = |token: &str| model_index(&FX_MODELS, token).unwrap();
        for token in ["O1HQP", "O1DUALP", "O1FRZ"] {
            assert!(!allowed_in(&MODELS, at(token), 0), "{token} on Effect 1");
            assert!(allowed_in(&MODELS, at(token), 1), "{token} on Effect 2");
        }
        assert!(allowed_in(&MODELS, at("O1HALL"), 0));
        assert!(allowed_in(&MODELS, at("O1HALL"), 1));
    }
}
