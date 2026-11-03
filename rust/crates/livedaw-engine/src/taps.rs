//! Tap markers anywhere in a strip's chain (the Universal core, Phase 1 step
//! 3c): user taps, which sends can take their signal from.
//!
//! A strip's taps have ids: 0-9 are the WING's (`strip::tap`, `keytap.rs`:
//! TAP_POINTS, EQ post, INS 2), and user taps take [`FIRST_USER_TAP`] and
//! up, placed as `Node::Tap(id)` steps in the strip's chain layout
//! (`Engine::insert_tap`). A send to a bus, main or matrix with a tap (its
//! `tap` field) takes its source's signal at that tap, like a pre-fader send
//! (cut by the source's mute unless it ignores it, panned by the send).
//!
//! The strip writes a user tap into a stereo buffer of its own when it runs
//! that node, but only while a send reads it: the message thread plans the
//! buffers (`Engine::update_taps`, from `collect`) and retires the ones no
//! longer read through the garbage list. A buffer is stamped with the block
//! it holds; a reader takes it only when it was written this block (the
//! source ran in an earlier stage), else silence. User taps are not delay
//! compensated (as solo and direct outs aren't).

use crate::garbage::Garbage;
use std::cell::UnsafeCell;
use std::sync::atomic::{AtomicPtr, AtomicU64, Ordering::*};

/// The first user tap id (the WING's taps are 0-9).
pub const FIRST_USER_TAP: u8 = 16;
/// The most user taps a strip holds.
pub const MAX_USER_TAPS: usize = 32;
/// Tap ids of a strip: the WING's, then the user taps.
pub const NUM_TAP_IDS: usize = FIRST_USER_TAP as usize + MAX_USER_TAPS;

/// A send's tap field: the tap it names, if any (-1 = none: the send's mode):
/// one of the strip's own tap points (0-9: an X32's IN/LC, <-EQ and EQ->
/// sends take them) or a user tap.
pub fn send_tap(tap: i32) -> Option<usize> {
    usize::try_from(tap).ok().filter(|&t| t < NUM_TAP_IDS)
}

struct TapBuf {
    data: UnsafeCell<[Vec<f32>; 2]>,
    /// The block the data holds, plus one (0 = none yet).
    stamp: AtomicU64,
}

/// The user tap buffers of a strip.
pub struct StripTaps {
    bufs: [AtomicPtr<TapBuf>; NUM_TAP_IDS],
}

impl Default for StripTaps {
    fn default() -> Self {
        Self { bufs: std::array::from_fn(|_| AtomicPtr::new(std::ptr::null_mut())) }
    }
}

// SAFETY: a buffer's data is written by the task running its strip and read by
// later stages after the stamp (Release / Acquire) says it holds this block.
unsafe impl Sync for StripTaps {}
unsafe impl Send for StripTaps {}

impl StripTaps {
    /// Audio thread, the task running the strip: tap `id` of block `block`
    /// from the work buffers (a mono strip fills both sides), if a send reads it.
    pub(crate) unsafe fn write(&self, id: usize, block: u64, work: [&[f32]; 2], channels: usize, n: usize) {
        let Some(buf) = self.bufs.get(id).and_then(|b| b.load(Acquire).as_ref()) else { return };
        let data = &mut *buf.data.get();
        for (ch, d) in data.iter_mut().enumerate() {
            let n = n.min(d.len());
            d[..n].copy_from_slice(&work[ch.min(channels.max(1) - 1)][..n]);
        }
        buf.stamp.store(block + 1, Release);
    }

    /// Audio thread, a later stage: side `ch` of tap `id` as written this
    /// block (None: not read, not in the chain, or not run yet).
    pub(crate) unsafe fn read(&self, id: usize, block: u64, ch: usize, n: usize) -> Option<&[f32]> {
        let buf = self.bufs.get(id)?.load(Acquire).as_ref()?;
        if buf.stamp.load(Acquire) != block + 1 {
            return None;
        }
        let d = &(*buf.data.get())[ch.min(1)];
        (n <= d.len()).then(|| &d[..n])
    }

    /// Message thread: buffers for the taps in `wanted` (by id), of
    /// `max_block` samples; the others are retired.
    pub(crate) fn plan(&self, wanted: &[bool; NUM_TAP_IDS], max_block: usize, garbage: &Garbage) {
        for (slot, &want) in self.bufs.iter().zip(wanted) {
            let current = slot.load(Acquire);
            // SAFETY: a published buffer lives until retired here.
            let fits = unsafe { current.as_ref() }.is_some_and(|b| unsafe { (*b.data.get())[0].len() } == max_block);
            if want == fits && (want || current.is_null()) {
                continue;
            }
            let next = if want {
                let buf =
                    TapBuf { data: UnsafeCell::new([vec![0.0; max_block], vec![0.0; max_block]]), stamp: AtomicU64::new(0) };
                Box::into_raw(Box::new(buf))
            } else {
                std::ptr::null_mut()
            };
            let old = slot.swap(next, AcqRel);
            if !old.is_null() {
                // SAFETY: created by Box::into_raw above; now unpublished.
                garbage.retire(unsafe { Box::from_raw(old) });
            }
        }
    }

    /// Message thread, while the audio thread can't reach the strip: frees
    /// every buffer (the block size changed; the next plan rebuilds them).
    pub(crate) fn drop_all(&self) {
        for slot in &self.bufs {
            let old = slot.swap(std::ptr::null_mut(), AcqRel);
            if !old.is_null() {
                // SAFETY: created by Box::into_raw in `plan`; unreachable now.
                drop(unsafe { Box::from_raw(old) });
            }
        }
    }
}

impl Drop for StripTaps {
    fn drop(&mut self) {
        self.drop_all();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_tap_is_read_in_the_block_it_was_written() {
        let taps = StripTaps::default();
        let garbage = Garbage::default();
        let mut wanted = [false; NUM_TAP_IDS];
        wanted[20] = true;
        taps.plan(&wanted, 8, &garbage);
        let (l, r) = ([0.5; 8], [0.25; 8]);
        unsafe {
            assert!(taps.read(20, 3, 0, 8).is_none(), "nothing written yet");
            taps.write(20, 3, [&l, &r], 2, 8);
            assert_eq!(taps.read(20, 3, 1, 8).unwrap()[0], 0.25);
            assert!(taps.read(20, 4, 0, 8).is_none(), "an older block's");
            taps.write(20, 4, [&l, &r], 1, 8); // mono: both sides the left
            assert_eq!(taps.read(20, 4, 1, 8).unwrap()[0], 0.5);
            taps.write(21, 4, [&l, &r], 2, 8); // not planned: nothing
            assert!(taps.read(21, 4, 0, 8).is_none());
        }
        assert_eq!(send_tap(-1), None);
        assert_eq!(send_tap(1), Some(1), "a tap point of the strip's own (FILT)");
        assert_eq!(send_tap(16), Some(16));
        assert_eq!(send_tap(NUM_TAP_IDS as i32), None);
    }
}
