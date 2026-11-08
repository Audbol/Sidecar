//! The Universal profile's models: every emulated console's blocks on any
//! strip (`docs/UNIVERSAL_MIXER.md`, decision 6). The WING's tables come
//! first, in the WING's order, so its models keep their indices (WING shows,
//! the WING remote); the X32's follow. A show names a block's model by its
//! token, so later consoles can be appended without changing older shows.
//!
//! The models run on the DSP that already runs them for their own consoles
//! (`dynamics`, `eq` and `fx::create` pick them by token).

use crate::model::{ModelSpec, ModelTables};
use crate::{wing, x32};

/// `a` then `b` from its `skip`th model on.
const fn join<const N: usize>(a: &[ModelSpec], b: &[ModelSpec], skip: usize) -> [ModelSpec; N] {
    assert!(a.len() + b.len() - skip == N);
    let mut out = [a[0]; N];
    let mut i = 0;
    while i < a.len() {
        out[i] = a[i];
        i += 1;
    }
    let mut j = skip;
    while j < b.len() {
        out[a.len() + j - skip] = b[j];
        j += 1;
    }
    out
}

/// The X32's EQs, offered on every strip (an input may take its 6-band, a bus
/// its 4-band).
const X32_EQS: [ModelSpec; 2] = [x32::EQ_CHANNEL[0], x32::EQ_BUS[0]];

/// The X32's FX table starts with NONE and EXT, which the WING's has already.
const X32_FX_SKIP: usize = 2;

const N_DYN: usize = wing::DYN_MODELS.len() + x32::DYN_MODELS.len();
const N_EQ_CH: usize = wing::EQ_MODELS_CH.len() + X32_EQS.len();
const N_EQ_BUS: usize = wing::EQ_MODELS_BUS.len() + X32_EQS.len();
const N_FX: usize = wing::FX_MODELS.len() + x32::FX_MODELS.len() - X32_FX_SKIP;

pub static DYN_MODELS: [ModelSpec; N_DYN] = join(wing::DYN_MODELS, x32::DYN_MODELS, 0);
pub static EQ_MODELS_CH: [ModelSpec; N_EQ_CH] = join(wing::EQ_MODELS_CH, &X32_EQS, 0);
pub static EQ_MODELS_BUS: [ModelSpec; N_EQ_BUS] = join(wing::EQ_MODELS_BUS, &X32_EQS, 0);
pub static FX_MODELS: [ModelSpec; N_FX] = join(wing::FX_MODELS, x32::FX_MODELS, X32_FX_SKIP);

/// The Universal's models. Its aux channels, FX premium slots (1-8), filter
/// tools and a new strip's gate and dynamics stay the WING's.
pub static MODELS: ModelTables = ModelTables {
    dynamics: &DYN_MODELS,
    aux_dynamics: wing::AUX_DYN_MODELS,
    eq_channel: &EQ_MODELS_CH,
    eq_bus: &EQ_MODELS_BUS,
    filter_tools: wing::FILTER_TOOLS,
    fx: &FX_MODELS,
    fx_premium_slots: 8,
    gate_default: "GATE",
    comp_default: "COMP",
};

/// The console a model comes from, by its token's prefix: "X32" for the
/// X32's, None for the WING's (the Universal's own).
pub fn console_of(token: &str) -> Option<&'static str> {
    token.starts_with("X32").then_some("X32")
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::model_index;

    #[test]
    fn the_wings_models_keep_their_indices() {
        for (ours, theirs) in [
            (&DYN_MODELS[..], wing::DYN_MODELS),
            (&EQ_MODELS_CH[..], wing::EQ_MODELS_CH),
            (&EQ_MODELS_BUS[..], wing::EQ_MODELS_BUS),
            (&FX_MODELS[..], wing::FX_MODELS),
        ] {
            assert_eq!(&ours[..theirs.len()], theirs);
        }
    }

    #[test]
    fn every_x32_model_is_offered_once() {
        for m in x32::DYN_MODELS {
            assert!(model_index(&DYN_MODELS, m.token).is_some(), "{}", m.token);
        }
        for t in [&EQ_MODELS_CH[..], &EQ_MODELS_BUS[..]] {
            assert!(model_index(t, "X32EQ4").is_some() && model_index(t, "X32EQ6").is_some());
        }
        for m in &x32::FX_MODELS[X32_FX_SKIP..] {
            assert!(model_index(&FX_MODELS, m.token).is_some(), "{}", m.token);
        }
        // Tokens stay unique, so a show's token finds one model.
        for t in [&DYN_MODELS[..], &EQ_MODELS_CH[..], &EQ_MODELS_BUS[..], &FX_MODELS[..]] {
            for (i, m) in t.iter().enumerate() {
                assert_eq!(model_index(t, m.token), Some(i), "{} twice", m.token);
            }
        }
        assert_eq!(FX_MODELS.iter().filter(|m| m.token == "NONE" || m.token == "EXT").count(), 2);
    }

    #[test]
    fn the_x32s_models_run() {
        use crate::wing::fx::{allowed_in, create, FxContext};
        let ctx = FxContext { sample_rate: 48000.0, max_block: 64 };
        for m in &FX_MODELS[wing::FX_MODELS.len()..] {
            assert!(create(m.token, ctx).is_some(), "{} has no device", m.token);
        }
        // The X32's send / return devices are premium: FX 1-8, as the WING's.
        let hall = model_index(&FX_MODELS, "X32HALL").unwrap();
        assert!(allowed_in(&MODELS, hall, 7) && !allowed_in(&MODELS, hall, 8));
        assert_eq!(console_of("X32GATE"), Some("X32"));
        assert_eq!(console_of("GATE"), None);
    }
}
