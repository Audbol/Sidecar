//! The SQ-5's RackFX (`crate::sq::fx`) on the WING's devices of the same kind:
//! each SQ control is mapped onto the WING device's, as the X32's are
//! (`x32_voices`). Which device runs which model, and the scalings, are
//! Sidecar's reading, to be compared with a console. A send effect's return is
//! wet only, so the modulation devices run at full MIX.

use super::x32_voices::{bridge, Map};
use super::{FxAlgo, FxContext};
use crate::model::Params;
use crate::sq::fx::ADDONS;

fn device(token: &str) -> Option<(&'static str, Map)> {
    Some(match token {
        "SQSMRC" => ("CHAMBER", smr),
        "SQSMRH" => ("HALL", smr),
        "SQSMRR" => ("ROOM", smr),
        "SQSMRE" => ("PLATE", smr),
        "SQTAP" => ("ST-DL", tap),
        "SQADT" => ("DOUBLE", adt),
        "SQCHOR" => ("CHORUS", chorus),
        "SQSYMP" => ("CHORUS", symphonic),
        "SQFLNG" => ("FLANGER", flanger),
        "SQPHAS" => ("PHASER", phaser),
        "SQGATE2" => ("GATED", gated),
        _ => return None,
    })
}

pub(super) fn create(token: &str, ctx: FxContext) -> Option<Box<dyn FxAlgo>> {
    // The add-ons are the dLive's models (`crate::sq::fx::ADDONS`).
    if let Some((_, dlive)) = ADDONS.iter().find(|(sq, _)| *sq == token) {
        return crate::dlive_fx::create(dlive, ctx);
    }
    let (wing, map) = device(token)?;
    bridge(wing, None, map, ctx)
}

type Set<'a> = &'a mut dyn FnMut(&'static str, f32);

fn smr(p: &Params, set: Set) -> f32 {
    set("pdel", p.f("pre"));
    set("dcy", p.f("decay"));
    set("damp", p.f("hfd"));
    set("lc", p.f("lc"));
    set("hc", p.f("hc"));
    0.0
}

fn tap(p: &Params, set: Set) -> f32 {
    set("time", p.f("time"));
    set("feed", p.f("feed") * 10.0);
    set("lc", p.f("lc"));
    set("hc", p.f("hc"));
    set("fhc", p.f("fb"));
    0.0
}

fn adt(p: &Params, set: Set) -> f32 {
    // DOUBLE the device's TIGHT, QUAD its GROUP.
    set("mode", if p.text("mode") == "QUAD" { 2.0 } else { 0.0 });
    set("sprd", p.f("sep"));
    set("mix", p.f("thick"));
    0.0
}

fn chorus(p: &Params, set: Set) -> f32 {
    let (phase, spread) = match p.text("field") {
        "MONO" => (0.0, 0.0),
        "MULTI VOICE" => (180.0, 100.0),
        _ => (120.0, 100.0),
    };
    set("spd", p.f("rate"));
    set("depl", p.f("depth"));
    set("depr", p.f("depth"));
    set("phase", phase);
    set("sprd", spread);
    set("mix", 100.0);
    0.0
}

fn symphonic(p: &Params, set: Set) -> f32 {
    set("spd", p.f("freq"));
    set("depl", p.f("depth"));
    set("depr", p.f("depth"));
    set("phase", 180.0);
    set("sprd", 100.0);
    set("mix", 100.0);
    0.0
}

fn flanger(p: &Params, set: Set) -> f32 {
    // AMBIENT long and gentle, WILD short and fed back harder.
    let (delay, feed) = match p.text("type") {
        "AMBIENT" => (8.0, 0.7),
        "WILD" => (2.0, 1.0),
        _ => (4.2, 0.85),
    };
    set("spd", p.f("speed"));
    set("depl", p.f("depth"));
    set("depr", p.f("depth"));
    set("phase", p.f("split"));
    set("feed", p.f("reso") * feed);
    set("dlyl", delay);
    set("dlyr", delay);
    set("mix", 100.0);
    0.0
}

fn phaser(p: &Params, set: Set) -> f32 {
    set("spd", p.f("rate"));
    set("depth", p.f("depth"));
    set("range", p.f("offset"));
    set("reso", p.f("reso"));
    set("stg", 12.0);
    set("mix", 100.0);
    0.0
}

fn gated(p: &Params, set: Set) -> f32 {
    set("pdel", p.f("pre"));
    set("att", p.f("att"));
    set("dcy", ((p.f("hold") + p.f("rel")) * 0.001).clamp(0.14, 1.0));
    set("diff", p.f("diff"));
    set("lc", p.f("lc"));
    set("hsf", p.f("hc"));
    set(
        "sprd",
        match p.text("type") {
            "PANNED" => 100.0,
            "POWERBOX" => 20.0,
            _ => 60.0,
        },
    );
    0.0
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::sq::fx::{is_sq, FX_MODELS};
    use crate::wing::MAX_FX_PARAMS;

    #[test]
    fn every_model_runs_and_names_only_its_wing_devices_controls() {
        let ctx = FxContext { sample_rate: 48_000.0, max_block: 64 };
        for m in &FX_MODELS[2..crate::sq::fx::FIRST_ADDON] {
            assert!(is_sq(m.token), "{}", m.token);
            assert!(m.params.len() <= MAX_FX_PARAMS);
            assert!(create(m.token, ctx).is_some(), "{}", m.token);
            let (wing, map) = device(m.token).unwrap();
            let spec = &crate::wing::FX_MODELS[crate::model::model_index(crate::wing::FX_MODELS, wing).unwrap()];
            let mut values = [0.0; MAX_FX_PARAMS];
            m.defaults_into(&mut values);
            let mut seen = 0;
            map(&Params::new(m, &values[..m.params.len()]), &mut |id, v| {
                seen += 1;
                let s = spec.params.iter().find(|s| s.id == id).unwrap_or_else(|| panic!("{}: {wing} has no {id}", m.token));
                let (lo, hi) = (s.min.min(s.max), s.min.max(s.max));
                assert!((lo..=hi).contains(&v), "{}: {id} = {v} outside {lo}..{hi}", m.token);
            });
            assert!(seen >= 2, "{}", m.token);
        }
    }

    #[test]
    fn the_addons_are_the_dlives_models_with_its_controls() {
        let ctx = FxContext { sample_rate: 48_000.0, max_block: 64 };
        assert_eq!(FX_MODELS.len() - crate::sq::fx::FIRST_ADDON, ADDONS.len());
        for (m, (sq, dlive)) in FX_MODELS[crate::sq::fx::FIRST_ADDON..].iter().zip(ADDONS) {
            assert_eq!(m.token, sq);
            assert!(is_sq(m.token), "{}", m.token);
            let d = crate::dlive_fx::FX_MODELS.iter().find(|d| d.token == dlive).unwrap();
            assert_eq!(m.params.len(), d.params.len(), "{}", m.token);
            assert!(m.params.iter().zip(d.params).all(|(a, b)| a.id == b.id), "{}", m.token);
            assert!(create(m.token, ctx).is_some(), "{}", m.token);
        }
    }

    #[test]
    fn every_model_passes_the_device_checks() {
        for m in &FX_MODELS[2..] {
            crate::wing::fx::tests::check_device(m.token);
        }
    }

    #[test]
    fn the_reverbs_tail_rings_after_an_impulse() {
        let ctx = FxContext { sample_rate: 48_000.0, max_block: 1024 };
        let m = FX_MODELS.iter().find(|m| m.token == "SQSMRH").unwrap();
        let mut dev = create(m.token, ctx).unwrap();
        let mut values = [0.0; MAX_FX_PARAMS];
        m.defaults_into(&mut values);
        dev.set_params(&Params::new(m, &values[..m.params.len()]));
        let (mut l, mut r) = (vec![0.0f32; 1024], vec![0.0f32; 1024]);
        l[0] = 1.0;
        r[0] = 1.0;
        dev.process(&mut l, &mut r);
        let mut energy = 0.0;
        for _ in 0..20 {
            let (mut l, mut r) = (vec![0.0f32; 1024], vec![0.0f32; 1024]);
            dev.process(&mut l, &mut r);
            energy += l.iter().chain(r.iter()).map(|s| s * s).sum::<f32>();
        }
        assert!(energy > 1e-5, "{energy}");
    }
}
