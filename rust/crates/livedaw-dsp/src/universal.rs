//! The Universal profile's models: every emulated console's blocks on any
//! strip (`docs/UNIVERSAL_MIXER.md`, decision 6). The WING's tables come
//! first, in the WING's order, so its models keep their indices (WING shows,
//! the WING remote); the X32's, the ProMix 01's, the 02R's, the SQ-5's, the 01V's, the dLive's, the DM2000's and the PRO2's follow. A show
//! names a block's model by its token, so later consoles can be appended
//! without changing older shows.
//!
//! The models run on the DSP that already runs them for their own consoles
//! (`dynamics`, `eq` and `fx::create` pick them by token).

use crate::model::{ModelSpec, ModelTables};
use crate::{dlive, dlive_fx, dm2000, midas_pro, promix, sq, wing, x32, xenyx1202, y01v, y02r};

/// The tables in `parts` one after the other, each from its `skip`th model on.
const fn join<const N: usize>(parts: &[(&[ModelSpec], usize)]) -> [ModelSpec; N] {
    let mut out = [parts[0].0[0]; N];
    let mut n = 0;
    let mut p = 0;
    while p < parts.len() {
        let (table, skip) = parts[p];
        let mut i = skip;
        while i < table.len() {
            out[n] = table[i];
            n += 1;
            i += 1;
        }
        p += 1;
    }
    assert!(n == N);
    out
}

/// The other consoles' EQs, offered on every strip (an input may take a bus EQ
/// and a bus an input's).
const OTHER_EQS: [ModelSpec; 19] = [
    x32::EQ_CHANNEL[0],
    x32::EQ_BUS[0],
    promix::EQ[0],
    y02r::EQ[0],
    sq::EQ[0],
    sq::EQ_BUS[1],
    y01v::TWO_BAND_EQ,
    dlive::EQ[0],
    dlive::EQ[1],
    dlive::EQ[2],
    dlive::EQ[3],
    dlive::EQ[4],
    dlive::EQ[5],
    dm2000::EQ_MODEL,
    dm2000::GEQ_MODEL,
    midas_pro::EQ4_MODEL,
    midas_pro::EQ6_MODEL,
    midas_pro::GEQ_MODEL,
    xenyx1202::EQ_MODEL,
];

/// The other consoles' FX tables start with NONE and EXT, which the WING's has already.
const FX_SKIP: usize = 2;

const N_DYN: usize = wing::DYN_MODELS.len()
    + x32::DYN_MODELS.len()
    + promix::DYN_MODELS.len()
    + y02r::DYN_MODELS.len()
    + sq::DYN_MODELS.len()
    + dlive::DYN_MODELS.len()
    + dm2000::DYN_MODELS.len()
    + midas_pro::DYN_MODELS.len()
    + xenyx1202::DYN_MODELS.len();
const N_EQ_CH: usize = wing::EQ_MODELS_CH.len() + OTHER_EQS.len();
const N_EQ_BUS: usize = wing::EQ_MODELS_BUS.len() + OTHER_EQS.len();
const N_FX: usize = wing::FX_MODELS.len()
    + x32::FX_MODELS.len()
    + promix::FX_MODELS.len()
    + y02r::fx::FX_MODELS.len()
    + sq::fx::FX_MODELS.len()
    + y01v::fx::FX_MODELS.len()
    + dlive_fx::FX_MODELS.len()
    + dm2000::FX_MODELS.len()
    + midas_pro::fx::FX_MODELS.len()
    - 8 * FX_SKIP;

pub static DYN_MODELS: [ModelSpec; N_DYN] = join(&[
    (wing::DYN_MODELS, 0),
    (x32::DYN_MODELS, 0),
    (promix::DYN_MODELS, 0),
    (y02r::DYN_MODELS, 0),
    (sq::DYN_MODELS, 0),
    (dlive::DYN_MODELS, 0),
    (dm2000::DYN_MODELS, 0),
    (midas_pro::DYN_MODELS, 0),
    (xenyx1202::DYN_MODELS, 0),
]);
pub static EQ_MODELS_CH: [ModelSpec; N_EQ_CH] = join(&[(wing::EQ_MODELS_CH, 0), (&OTHER_EQS, 0)]);
pub static EQ_MODELS_BUS: [ModelSpec; N_EQ_BUS] = join(&[(wing::EQ_MODELS_BUS, 0), (&OTHER_EQS, 0)]);
pub static FX_MODELS: [ModelSpec; N_FX] = join(&[
    (wing::FX_MODELS, 0),
    (x32::FX_MODELS, FX_SKIP),
    (&promix::FX_MODELS, FX_SKIP),
    (&y02r::fx::FX_MODELS, FX_SKIP),
    (&sq::fx::FX_MODELS, FX_SKIP),
    (&y01v::fx::FX_MODELS, FX_SKIP),
    (&dlive_fx::FX_MODELS, FX_SKIP),
    (&dm2000::FX_MODELS, FX_SKIP),
    (&midas_pro::fx::FX_MODELS, FX_SKIP),
]);

/// The Universal's models. Its aux channels, FX premium slots (1-8), filter
/// tools and a new strip's gate and dynamics stay the WING's.
pub static MODELS: ModelTables = ModelTables {
    dynamics: &DYN_MODELS,
    aux_dynamics: wing::AUX_DYN_MODELS,
    eq_channel: &EQ_MODELS_CH,
    eq_bus: &EQ_MODELS_BUS,
    filter_tools: wing::FILTER_TOOLS,
    fx: &FX_MODELS,
    fx_premium_first: 0,
    fx_premium_slots: 8,
    gate_default: "GATE",
    comp_default: "COMP",
};

/// A console's own tables followed by every other console's models of each kind
/// (the Universal's, minus what the console has): what the blocks of that console's
/// strips and FX slots hold, so any block may take another console's model
/// (`docs/UNIVERSAL_MIXER.md`, "Foreign models"). The own models keep their indices,
/// so a console's surface, shows and remote protocols number them as before; a model
/// at or past an own table's length is foreign (the token names it across tables).
/// A dLive's RackUltra and Dyn8 engines belong to its channel strips, so they stay
/// off the other consoles' racks. Built once per table set (message thread: allocates,
/// never on the audio thread); the Universal's own tables are returned as they are.
pub fn with_foreign(own: &'static ModelTables) -> &'static ModelTables {
    use std::sync::Mutex;
    static CACHE: Mutex<Vec<(usize, &'static ModelTables)>> = Mutex::new(Vec::new());
    if std::ptr::eq(own, &MODELS) {
        return &MODELS;
    }
    let key = own as *const ModelTables as usize;
    let mut cache = CACHE.lock().unwrap_or_else(|e| e.into_inner());
    if let Some((_, t)) = cache.iter().find(|(k, _)| *k == key) {
        return t;
    }
    let dlive_own = own.fx.iter().any(|m| m.token == "DLVDYN8");
    let extend = |mine: &'static [ModelSpec], all: &'static [ModelSpec], keep: &dyn Fn(&str) -> bool| -> &'static [ModelSpec] {
        let mut v: Vec<ModelSpec> = mine.to_vec();
        v.extend(all.iter().filter(|m| !mine.iter().any(|o| o.token == m.token) && keep(m.token)).copied());
        Box::leak(v.into_boxed_slice())
    };
    let foreign_fx = |t: &str| dlive_own || !(dlive_fx::is_ultra(t) || t == "DLVDYN8");
    let tables: &'static ModelTables = Box::leak(Box::new(ModelTables {
        dynamics: extend(own.dynamics, &DYN_MODELS, &|_| true),
        eq_channel: extend(own.eq_channel, &EQ_MODELS_CH, &|_| true),
        eq_bus: extend(own.eq_bus, &EQ_MODELS_BUS, &|_| true),
        filter_tools: extend(own.filter_tools, wing::FILTER_TOOLS, &|_| true),
        fx: extend(own.fx, &FX_MODELS, &foreign_fx),
        ..*own
    }));
    cache.push((key, tables));
    tables
}

/// Whether `token` is one of `own`'s own models of its kind (any table).
pub fn is_own(own: &ModelTables, token: &str) -> bool {
    [own.dynamics, own.eq_channel, own.eq_bus, own.filter_tools, own.fx].iter().any(|t| t.iter().any(|m| m.token == token))
}

/// The console a model comes from, by its token's prefix ("X32" for the X32's,
/// "PROMIX 01" for the ProMix 01's, "02R" for the 02R's, "SQ-5" for the SQ-5's); None for the WING's
/// (the Universal's own).
pub fn console_of(token: &str) -> Option<&'static str> {
    if token.starts_with("X32") {
        Some("X32")
    } else if token.starts_with("PM")
        && (model_in(promix::DYN_MODELS, token) || model_in(promix::EQ, token) || promix::fx::is_promix(token))
    {
        Some("PROMIX 01")
    } else if token.starts_with("Y2")
        && (model_in(y02r::DYN_MODELS, token) || model_in(y02r::EQ, token) || y02r::fx::is_02r(token))
    {
        Some("02R")
    } else if token.starts_with("DM")
        && (model_in(dm2000::DYN_MODELS, token) || model_in(&dm2000::EQ_BUS, token) || dm2000::fx::is_dm2000(token))
    {
        Some("DM2000")
    } else if token.starts_with("MP")
        && (model_in(midas_pro::DYN_MODELS, token)
            || model_in(&midas_pro::EQ_BUS, token)
            || model_in(&midas_pro::EQ_CHANNEL, token)
            || midas_pro::fx::is_pro2(token))
    {
        Some("PRO2")
    } else if token.starts_with("XY") && (model_in(xenyx1202::DYN_MODELS, token) || token == xenyx1202::EQ_MODEL.token) {
        Some("Q1202USB")
    } else if token.starts_with("DLV") {
        Some("dLive")
    } else if token.starts_with("O1") && (y01v::fx::is_01v(token) || token == y01v::TWO_BAND_EQ.token) {
        Some("01V")
    } else if token.starts_with("SQ") && (model_in(sq::DYN_MODELS, token) || model_in(&sq::EQ_BUS, token) || sq::fx::is_sq(token))
    {
        Some("SQ-5")
    } else {
        None
    }
}

fn model_in(table: &[ModelSpec], token: &str) -> bool {
    table.iter().any(|m| m.token == token)
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
    fn every_consoles_models_are_offered_once() {
        for table in
            [x32::DYN_MODELS, promix::DYN_MODELS, y02r::DYN_MODELS, sq::DYN_MODELS, dlive::DYN_MODELS, dm2000::DYN_MODELS]
        {
            for m in table {
                assert!(model_index(&DYN_MODELS, m.token).is_some(), "{}", m.token);
            }
        }
        for t in [&EQ_MODELS_CH[..], &EQ_MODELS_BUS[..]] {
            for token in [
                "X32EQ4", "X32EQ6", "PMEQ", "Y2EQ", "SQPEQ", "SQGEQ", "O1EQ2", "DLVPEQ", "DLVGEQC", "DLVGEQP", "DLVGEQD",
                "DLVGEQH", "DLVNEQ12", "DMEQ", "DMGEQ",
            ] {
                assert!(model_index(t, token).is_some(), "{token}");
            }
        }
        for table in [
            x32::FX_MODELS,
            &promix::FX_MODELS[..],
            &y02r::fx::FX_MODELS[..],
            &sq::fx::FX_MODELS[..],
            &dlive_fx::FX_MODELS[..],
            &dm2000::FX_MODELS[..],
        ] {
            for m in &table[FX_SKIP..] {
                assert!(model_index(&FX_MODELS, m.token).is_some(), "{}", m.token);
            }
        }
        // Tokens stay unique, so a show's token finds one model.
        for t in [&DYN_MODELS[..], &EQ_MODELS_CH[..], &EQ_MODELS_BUS[..], &FX_MODELS[..]] {
            for (i, m) in t.iter().enumerate() {
                assert_eq!(model_index(t, m.token), Some(i), "{} twice", m.token);
            }
        }
        assert_eq!(FX_MODELS.iter().filter(|m| m.token == "NONE" || m.token == "EXT").count(), 2);
        assert!(DYN_MODELS.iter().all(|m| m.params.len() <= wing::MAX_DYN_PARAMS));
        assert!(EQ_MODELS_CH.iter().chain(EQ_MODELS_BUS.iter()).all(|m| m.params.len() <= wing::MAX_EQ_PARAMS));
        assert!(FX_MODELS.iter().all(|m| m.params.len() <= wing::MAX_FX_PARAMS));
    }

    #[test]
    fn every_consoles_models_run() {
        use crate::wing::fx::{allowed_in, create, FxContext};
        let ctx = FxContext { sample_rate: 48000.0, max_block: 64 };
        for m in &FX_MODELS[wing::FX_MODELS.len()..] {
            assert!(create(m.token, ctx).is_some(), "{} has no device", m.token);
        }
        // The X32's send / return devices are premium: FX 1-8, as the WING's.
        let hall = model_index(&FX_MODELS, "X32HALL").unwrap();
        assert!(allowed_in(&MODELS, hall, 7) && !allowed_in(&MODELS, hall, 8));
        // Each console's dynamics and EQ make a configuration on the shared DSP.
        let mut values = [0.0f32; 32];
        for m in DYN_MODELS.iter().skip(wing::DYN_MODELS.len()) {
            m.defaults_into(&mut values);
            let mut out = [0.0f32; 2];
            // A model the DSP doesn't know has no static curve and no configuration: every one here
            // has one, the gates aside and the dLive's preamp models (saturation has no level curve).
            let known = crate::wing::dynamics::static_curve_in(
                &DYN_MODELS,
                model_index(&DYN_MODELS, m.token).unwrap(),
                &values,
                &[-40.0, 0.0],
                &mut out,
            );
            let exempt =
                m.category == crate::model::ModelCategory::Gate || dlive::is_preamp(m.token) || xenyx1202::is_clip(m.token);
            assert!(known || exempt, "{} has no level curve", m.token);
        }
    }

    #[test]
    fn a_model_names_its_console() {
        assert_eq!(console_of("X32GATE"), Some("X32"));
        assert_eq!(console_of("PMCOMP"), Some("PROMIX 01"));
        assert_eq!(console_of("PMREV"), Some("PROMIX 01"));
        assert_eq!(console_of("Y2EQ"), Some("02R"));
        assert_eq!(console_of("DLV16T"), Some("dLive"));
        assert_eq!(console_of("DLVSMRH"), Some("dLive"));
        assert_eq!(console_of("DLVGEQH"), Some("dLive"));
        assert_eq!(console_of("DLVVALVE"), Some("dLive"));
        assert_eq!(console_of("DLVTUBE"), Some("dLive"));
        assert_eq!(console_of("XYEQ"), Some("Q1202USB"));
        assert_eq!(console_of("XYCMP"), Some("Q1202USB"));
        assert_eq!(console_of("DLY"), None, "the X32's delay isn't a dLive's");
        assert_eq!(console_of("Y2CHREV"), Some("02R"));
        assert_eq!(console_of("SQCOMP"), Some("SQ-5"));
        assert_eq!(console_of("SQPEQ"), Some("SQ-5"));
        assert_eq!(console_of("SQGEQ"), Some("SQ-5"));
        assert_eq!(console_of("SQSMRH"), Some("SQ-5"));
        assert_eq!(console_of("DMEQ"), Some("DM2000"));
        assert_eq!(console_of("DMCMP"), Some("DM2000"));
        assert_eq!(console_of("DMHALL"), Some("DM2000"));
        assert_eq!(console_of("DMGEQ"), Some("DM2000"));
        assert_eq!(console_of("MPEQ4"), Some("PRO2"));
        assert_eq!(console_of("MPCMPO"), Some("PRO2"));
        assert_eq!(console_of("MPDN780"), Some("PRO2"));
        assert_eq!(console_of("MPGEQ"), Some("PRO2"));
        for wing in ["GATE", "COMP", "HALL", "PIA", "PULSAR", "PHASER", "PCORR"] {
            assert_eq!(console_of(wing), None, "{wing}");
        }
    }
}
