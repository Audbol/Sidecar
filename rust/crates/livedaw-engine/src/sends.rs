//! Per-strip send storage that grows in pages: sends to buses, mains and
//! matrices, by the destination's index.

use crate::atomic::AtomicF32;
use crate::profile::{Profile, ProfileSpec};
use crate::StripKind;
use std::cell::UnsafeCell;
use std::sync::atomic::{AtomicBool, AtomicI32, AtomicPtr, Ordering};

/// Upper bound on bus slots. Pages are allocated on demand, so the ceiling costs
/// only the page-pointer table (2 KB per strip); in practice CPU is the limit.
pub const MAX_BUSES: usize = 16384;
pub const PAGE_SIZE: usize = 64;

/// Bus send modes: TAP (the WING's name for input channels; pre-fader for
/// buses), post-fader, or GROUP (post-fader and post-pan, like routing the
/// strip to a group: no send level).
pub const SEND_MODES: [&str; 3] = ["PRE", "POST", "GRP"];

/// Send modes (indices into [`SEND_MODES`]).
pub mod send_mode {
    pub const PRE: i32 = 0;
    pub const POST: i32 = 1;
    pub const GROUP: i32 = 2;
}

#[repr(C)]
#[derive(Debug)]
pub struct SendParams {
    pub enabled: AtomicBool,
    /// Pan follows the strip's pan (else `pan` is used).
    pub pan_link: AtomicBool,
    /// IGNORE CHANNEL MUTE (input and aux channels, PRE/TAP mode only): the send
    /// stays on while the channel is muted.
    pub ignore_mute: AtomicBool,
    /// With `pan_link`: follow the strip's pan mirrored (LINK REVERSED).
    pub pan_reversed: AtomicBool,
    /// Index into [`SEND_MODES`].
    pub mode: AtomicI32,
    pub level_db: AtomicF32,
    pub pan: AtomicF32,
    /// ABI 27: a user tap of the source strip to take the signal at (`taps.rs`; -1 = none).
    pub tap: AtomicI32,
    /// Gain ramp state (left, right), owned by the audio task that sums the destination bus.
    pub(crate) last_gain: UnsafeCell<[f32; 2]>,
}

// SAFETY: `last_gain` is only touched by the single audio task that sums the bus
// this send feeds; everything else is atomic.
unsafe impl Sync for SendParams {}

/// The send level of a send never set: -oo, as on a factory-initialized WING.
pub const DEFAULT_SEND_LEVEL_DB: f32 = livedaw_dsp::fader::FADER_MIN;

/// The mode a factory-initialized WING gives the send from a strip of `kind`
/// to bus slot `slot`: input and aux channels send PRE (TAP) to buses 1-8, GRP
/// to buses 9-10 and POST to buses 11-16; buses 9-16 send POST to buses 1-8.
/// Buses past the console's 16 take PRE. PAN LINK starts on for the sends
/// that aren't PRE ([`default_pan_link`]).
pub fn default_mode(kind: StripKind, slot: usize) -> i32 {
    match (kind, slot) {
        (StripKind::Bus, _) => send_mode::POST,
        (_, 8 | 9) => send_mode::GROUP,
        (_, 10..=15) => send_mode::POST,
        _ => send_mode::PRE,
    }
}

/// PAN LINK of a factory send in `mode`: on, except in PRE (TAP) mode (a
/// factory-set WING links the pan of the sends to buses 9-16, not 1-8).
pub fn default_pan_link(mode: i32) -> bool {
    mode != send_mode::PRE
}

impl Default for SendParams {
    fn default() -> Self {
        Self::with_mode(send_mode::PRE)
    }
}

impl SendParams {
    /// The factory send in `mode`: off, at -oo, PAN LINK as [`default_pan_link`].
    pub fn with_mode(mode: i32) -> Self {
        Self {
            enabled: AtomicBool::new(false),
            pan_link: AtomicBool::new(default_pan_link(mode)),
            ignore_mute: AtomicBool::new(false),
            pan_reversed: AtomicBool::new(false),
            mode: AtomicI32::new(mode),
            level_db: AtomicF32::new(DEFAULT_SEND_LEVEL_DB),
            pan: AtomicF32::new(0.0),
            tap: AtomicI32::new(-1),
            last_gain: UnsafeCell::new([0.0; 2]),
        }
    }

    /// Back to the factory send in `mode` (the ramp state is left alone).
    pub fn reset(&self, mode: i32) {
        self.enabled.store(false, Ordering::Relaxed);
        self.pan_link.store(default_pan_link(mode), Ordering::Relaxed);
        self.ignore_mute.store(false, Ordering::Relaxed);
        self.pan_reversed.store(false, Ordering::Relaxed);
        self.mode.store(mode, Ordering::Relaxed);
        self.level_db.store(DEFAULT_SEND_LEVEL_DB);
        self.pan.store(0.0);
        self.tap.store(-1, Ordering::Relaxed);
    }
}

impl BankItem for SendParams {
    /// The WING's factory sends ([`default_mode`], off); an X32's inputs' are
    /// on at -oo (its `sends_on`), to buses 1-8 PRE at its `send_tap`, to buses
    /// 9-16 POST (as a freshly initialised X32 has them). (An X32's buses send
    /// to its matrices only: their bus sends stay off.)
    fn factory(spec: &ProfileSpec, kind: StripKind, slot: usize, index: usize) -> Self {
        if !spec.sends_on || !kind.is_input() {
            return Self::with_mode(default_mode(kind, index));
        }
        // The X32's FX returns (aux slots of its "FX" range) send POST.
        let fx_return = spec.slot(kind, slot).is_some_and(|r| r.label == "FX");
        // A dLive's Aux start pre-fade, its Groups and FX sends post-fade (Sidecar's reading).
        let post = fx_return
            || match spec.dlive_layout {
                Some(layout) => !(layout.aux_at()..layout.fx_at()).contains(&index),
                None => index >= 8,
            };
        let s = Self::with_mode(if post { send_mode::POST } else { send_mode::PRE });
        s.enabled.store(true, Ordering::Relaxed);
        s.pan_link.store(false, Ordering::Relaxed); // the X32's pan follow starts off
        s.pan.store(crate::profile::x32_pair_pan(kind, slot)); // a linked pair's sends too
        if !post {
            s.tap.store(spec.send_tap, Ordering::Relaxed);
        }
        s
    }
}

/// An item of a [`Bank`]: a strip's send to one destination.
pub trait BankItem: Sized {
    /// The factory send `index` of a strip of `kind` in slot `slot` on a
    /// console of `spec`.
    fn factory(spec: &ProfileSpec, kind: StripKind, slot: usize, index: usize) -> Self;
}

/// Per-strip sends to the destinations of one kind (buses, mains,
/// matrices), by the destination's index. Pages are only added (by the
/// message thread) and freed with the strip, so the audio thread reads them
/// lock-free; an index whose page doesn't exist has no send (off).
pub struct Bank<T> {
    pages: Box<[AtomicPtr<T>]>,
    page_size: usize,
    max: usize,
    /// The strip the sends are from and its console (their factory settings,
    /// [`BankItem`]).
    kind: StripKind,
    slot: usize,
    spec: &'static ProfileSpec,
}

/// A strip's sends to buses (by bus slot).
pub type SendBank = Bank<SendParams>;

// SAFETY: pages are published once (Release) and only read after; the items
// are shared like any parameter block (atomics, and ramps owned by one task).
unsafe impl<T: Sync> Sync for Bank<T> {}
unsafe impl<T: Send> Send for Bank<T> {}

impl Default for SendBank {
    fn default() -> Self {
        Self::for_kind(StripKind::Channel)
    }
}

impl SendBank {
    /// The bus sends of a WING strip of `kind`.
    pub fn for_kind(kind: StripKind) -> Self {
        Self::of(Profile::Wing.spec(), kind, 0)
    }

    /// The bus sends of the strip of `kind` in `slot` on a console of `spec`.
    pub fn of(spec: &'static ProfileSpec, kind: StripKind, slot: usize) -> Self {
        Bank::new(MAX_BUSES, PAGE_SIZE, kind, slot, spec)
    }

    /// The factory mode of the send to `slot`.
    pub fn default_mode(&self, slot: usize) -> i32 {
        default_mode(self.kind, slot)
    }
}

impl<T: BankItem> Bank<T> {
    /// Up to `max` sends in pages of `page_size`, from a strip of `kind` in slot `slot`.
    pub fn new(max: usize, page_size: usize, kind: StripKind, slot: usize, spec: &'static ProfileSpec) -> Self {
        let pages = (0..max.div_ceil(page_size)).map(|_| AtomicPtr::new(std::ptr::null_mut())).collect();
        Self { pages, page_size, max, kind, slot, spec }
    }

    /// The most sends the bank holds.
    pub fn capacity(&self) -> usize {
        self.max
    }

    /// Any thread: the send to `index`, or None if it was never configured.
    #[inline]
    pub fn find(&self, index: usize) -> Option<&T> {
        if index >= self.max {
            return None;
        }
        let page = self.pages[index / self.page_size].load(Ordering::Acquire);
        // SAFETY: pages are never freed while the bank is alive; each holds
        // `page_size` items.
        (!page.is_null()).then(|| unsafe { &*page.add(index % self.page_size) })
    }

    /// Any thread but the audio thread (it allocates): the send to `index`,
    /// allocating its page if needed. Two threads creating the same page race
    /// through a compare-exchange; the loser frees its copy.
    pub fn get_or_create(&self, index: usize) -> Option<&T> {
        if index >= self.max {
            return None;
        }
        let ptr = &self.pages[index / self.page_size];
        if ptr.load(Ordering::Acquire).is_null() {
            let first = index / self.page_size * self.page_size;
            let items: Box<[T]> =
                (first..first + self.page_size).map(|i| T::factory(self.spec, self.kind, self.slot, i)).collect();
            let page = Box::into_raw(items) as *mut T;
            // Release publishes the initialised page to the audio thread.
            if ptr.compare_exchange(std::ptr::null_mut(), page, Ordering::AcqRel, Ordering::Acquire).is_err() {
                // SAFETY: never published; allocated just above with `page_size` items.
                drop(unsafe { Box::from_raw(std::ptr::slice_from_raw_parts_mut(page, self.page_size)) });
            }
        }
        self.find(index)
    }
}

impl<T> Drop for Bank<T> {
    fn drop(&mut self) {
        for p in self.pages.iter() {
            let page = p.swap(std::ptr::null_mut(), Ordering::AcqRel);
            if !page.is_null() {
                // SAFETY: allocated by get_or_create as a boxed slice of `page_size` items.
                drop(unsafe { Box::from_raw(std::ptr::slice_from_raw_parts_mut(page, self.page_size)) });
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::Ordering::Relaxed;

    #[test]
    fn pages_allocate_on_demand() {
        let bank = SendBank::default();
        assert!(bank.find(5000).is_none());
        bank.get_or_create(5000).unwrap().enabled.store(true, Relaxed);
        assert!(bank.find(5000).unwrap().enabled.load(Relaxed));
        assert!(bank.find(4999).is_some()); // same page
        assert!(bank.find(5100).is_none());
        assert!(bank.get_or_create(MAX_BUSES).is_none());
    }

    #[test]
    fn new_sends_start_as_the_wings_factory_init() {
        // Off at -oo; channels PRE to buses 1-8 (pan not linked), GRP to 9-10
        // and POST to 11-16 (pan linked); buses POST.
        let ch = SendBank::for_kind(StripKind::Channel);
        let s = ch.get_or_create(0).unwrap();
        assert!(!s.enabled.load(Relaxed) && !s.pan_link.load(Relaxed));
        assert_eq!(s.level_db.load(), DEFAULT_SEND_LEVEL_DB);
        let modes: Vec<i32> = (0..16).map(|i| ch.get_or_create(i).unwrap().mode.load(Relaxed)).collect();
        assert_eq!(modes, [[0; 8].as_slice(), &[2; 2], &[1; 6]].concat());
        assert!(ch.get_or_create(8).unwrap().pan_link.load(Relaxed) && ch.get_or_create(15).unwrap().pan_link.load(Relaxed));
        assert_eq!(ch.get_or_create(100).unwrap().mode.load(Relaxed), send_mode::PRE);
        let bus = SendBank::for_kind(StripKind::Bus);
        assert_eq!(bus.get_or_create(3).unwrap().mode.load(Relaxed), send_mode::POST);
    }
}
