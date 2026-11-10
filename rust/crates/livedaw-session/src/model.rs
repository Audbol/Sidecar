//! The console model Sidecar shows: the full-size WING, the WING COMPACT or the WING RACK.
//!
//! The WING family shares one firmware and one parameter tree (3.0 and up);
//! the models differ in their surface and their local I/O (WING remote
//! protocols 3.1, "Wing, a family"):
//!
//! - WING (full size): fader sections of 12, 8 and 4 faders with seven layer
//!   keys each, 8 local MIDAS PRO preamps, 8 local outputs, 8 aux inputs and
//!   outputs, 2x2 GPIO.
//! - WING COMPACT: 3 sets of 4 faders with nine layer keys in a column at
//!   their left, and fader 13 (the main fader, `/$ctl/cfg/mfdr`); 24 local
//!   preamps, 8 local outputs, no local aux I/O, 1x2 GPIO.
//! - WING RACK: no faders. A front panel section of four channels at a time
//!   (an LCD, four encoders, MUTE and SOLO keys) on five layers (WING manual
//!   5.6 p. 56: CHANNELS, AUX, BUSES, MAIN/MTX, DCA); 24 local preamps, 8
//!   local outputs, no local aux I/O, 2x2 GPIO (WING manual 3.3 p. 11 and
//!   chapter 12; WING remote protocols, "Wing, a family").
//!
//! The model is an app setting (a preference of the user, not of the show).
//! The values here are stable: they are stored in the settings and cross the
//! C ABI.

/// A console model.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub enum ConsoleModel {
    #[default]
    Full = 0,
    Compact = 1,
    Rack = 2,
}

/// A model's surface and local I/O.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ModelIo {
    /// Motor faders on the surface (the COMPACT's fader 13 included; the RACK has none).
    pub faders: i32,
    /// Layer keys of the (left) fader section.
    pub layer_keys: i32,
    /// Local MIDAS PRO microphone preamps (LCL inputs with a preamp).
    pub local_preamps: i32,
    pub local_outputs: i32,
    pub aux_inputs: i32,
    pub aux_outputs: i32,
    /// General purpose inputs and outputs (2 per TRS socket).
    pub gpios: i32,
}

impl ConsoleModel {
    pub const ALL: [ConsoleModel; 3] = [ConsoleModel::Full, ConsoleModel::Compact, ConsoleModel::Rack];

    /// Unknown values (a later model) read as the full-size console.
    pub fn from_i32(v: i32) -> Self {
        match v {
            1 => Self::Compact,
            2 => Self::Rack,
            _ => Self::Full,
        }
    }

    pub fn as_i32(self) -> i32 {
        self as i32
    }

    /// The name SETUP shows and the settings store: "FULL", "COMPACT", "RACK".
    pub fn name(self) -> &'static str {
        match self {
            Self::Full => "FULL",
            Self::Compact => "COMPACT",
            Self::Rack => "RACK",
        }
    }

    pub fn from_name(name: &str) -> Self {
        Self::ALL.into_iter().find(|m| m.name().eq_ignore_ascii_case(name.trim())).unwrap_or_default()
    }

    pub fn is_compact(self) -> bool {
        self == Self::Compact
    }

    pub fn is_rack(self) -> bool {
        self == Self::Rack
    }

    /// The COMPACT and the RACK have a VIRTUAL fader section beside their
    /// own, on the screens only (WING manual 5.7 p. 61).
    pub fn has_virtual_section(self) -> bool {
        self != Self::Full
    }

    pub fn io(self) -> ModelIo {
        match self {
            // [WING] 12 + 8 + 4 faders, 7 layer keys per section; 8 local preamps, 2x2 GPIO.
            Self::Full => {
                ModelIo { faders: 24, layer_keys: 7, local_preamps: 8, local_outputs: 8, aux_inputs: 8, aux_outputs: 8, gpios: 4 }
            }
            // [WING COMPACT] 3 x 4 faders and fader 13, 9 layers; 24 local preamps,
            // no local aux I/O, 1x2 GPIO.
            Self::Compact => ModelIo {
                faders: 13,
                layer_keys: 9,
                local_preamps: 24,
                local_outputs: 8,
                aux_inputs: 0,
                aux_outputs: 0,
                gpios: 2,
            },
            // [WING RACK] no faders, a four-channel section on five layers; 24 local
            // preamps, 8 XLR outputs (four headphone sockets copy them), no local aux
            // I/O, 2x2 GPIO.
            Self::Rack => {
                ModelIo { faders: 0, layer_keys: 5, local_preamps: 24, local_outputs: 8, aux_inputs: 0, aux_outputs: 0, gpios: 4 }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn values_and_names_are_stable() {
        assert_eq!(ConsoleModel::default(), ConsoleModel::Full);
        for m in ConsoleModel::ALL {
            assert_eq!(ConsoleModel::from_i32(m.as_i32()), m);
            assert_eq!(ConsoleModel::from_name(m.name()), m);
        }
        assert_eq!(ConsoleModel::Compact.as_i32(), 1);
        assert_eq!(ConsoleModel::Rack.as_i32(), 2);
        assert_eq!(ConsoleModel::from_name("rack"), ConsoleModel::Rack);
        // Unknown values and names read as the full-size console.
        assert_eq!(ConsoleModel::from_i32(3), ConsoleModel::Full);
        assert_eq!(ConsoleModel::from_i32(-1), ConsoleModel::Full);
        assert_eq!(ConsoleModel::from_name("EXTENDER"), ConsoleModel::Full);
        assert!(ConsoleModel::Rack.has_virtual_section() && ConsoleModel::Compact.has_virtual_section());
        assert!(!ConsoleModel::Full.has_virtual_section());
        assert_eq!(ConsoleModel::from_name(" compact "), ConsoleModel::Compact);
    }

    #[test]
    fn io_counts() {
        let full = ConsoleModel::Full.io();
        assert_eq!((full.local_preamps, full.gpios, full.aux_inputs, full.aux_outputs), (8, 4, 8, 8));
        assert_eq!((full.faders, full.layer_keys), (24, 7));
        let compact = ConsoleModel::Compact.io();
        assert_eq!((compact.local_preamps, compact.gpios, compact.aux_inputs, compact.aux_outputs), (24, 2, 0, 0));
        assert_eq!((compact.faders, compact.layer_keys, compact.local_outputs), (13, 9, 8));
        let rack = ConsoleModel::Rack.io();
        assert_eq!((rack.local_preamps, rack.gpios, rack.aux_inputs, rack.aux_outputs), (24, 4, 0, 0));
        assert_eq!((rack.faders, rack.layer_keys, rack.local_outputs), (0, 5, 8));
    }
}
