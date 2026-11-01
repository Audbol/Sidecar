//! Sets of console-wide groups (DCAs, mute groups) by index: a strip's
//! memberships, the engaged mute groups, a scene's scope.
//!
//! The Universal core has no fixed group counts: a console configures how
//! many DCAs and mute groups it uses (`Engine::set_num_dcas`, ...), up to
//! [`MAX_GROUPS`] each, so a membership is a fixed-size bitset the audio
//! thread reads without allocating. `AtomicGroupSet` is `#[repr(C)]` and
//! mirrored by `livedaw_engine.h` (`ld::GroupMask`).

use std::fmt;
use std::ops::{BitAnd, BitAndAssign, BitOr, BitOrAssign, Not};
use std::sync::atomic::{AtomicU64, Ordering::Relaxed};

/// Words of a group set.
pub const GROUP_WORDS: usize = 4;
/// The most groups of one kind (DCAs, or mute groups) a console can have.
pub const MAX_GROUPS: usize = GROUP_WORDS * 64;
/// Every group.
pub const ALL_GROUPS: GroupSet = GroupSet([u64::MAX; GROUP_WORDS]);

/// A set of group indices (0-based, below [`MAX_GROUPS`]). Passed through
/// the C ABI as four `u64` words (`ld::GroupBits`).
#[repr(transparent)]
#[derive(Clone, Copy, Default, PartialEq, Eq, Hash)]
pub struct GroupSet(pub [u64; GROUP_WORDS]);

impl GroupSet {
    pub const EMPTY: Self = Self([0; GROUP_WORDS]);

    /// Groups `0..n`.
    pub fn first(n: usize) -> Self {
        let mut s = Self::EMPTY;
        for (w, word) in s.0.iter_mut().enumerate() {
            let bits = n.saturating_sub(w * 64).min(64);
            *word = if bits == 64 { u64::MAX } else { (1u64 << bits) - 1 };
        }
        s
    }

    /// The set of the low 64 groups given as a mask (bit n = group n).
    pub const fn from_mask(mask: u64) -> Self {
        Self([mask, 0, 0, 0])
    }

    /// Groups 0..63 as a mask (the WING's 16 DCAs and 8 mute groups fit).
    pub const fn low(self) -> u64 {
        self.0[0]
    }

    pub fn single(i: usize) -> Self {
        let mut s = Self::EMPTY;
        s.insert(i);
        s
    }

    pub fn contains(&self, i: usize) -> bool {
        i < MAX_GROUPS && self.0[i / 64] & (1 << (i % 64)) != 0
    }

    pub fn insert(&mut self, i: usize) {
        if i < MAX_GROUPS {
            self.0[i / 64] |= 1 << (i % 64);
        }
    }

    pub fn remove(&mut self, i: usize) {
        if i < MAX_GROUPS {
            self.0[i / 64] &= !(1 << (i % 64));
        }
    }

    pub fn set(&mut self, i: usize, on: bool) {
        if on {
            self.insert(i)
        } else {
            self.remove(i)
        }
    }

    pub fn is_empty(&self) -> bool {
        self.0.iter().all(|w| *w == 0)
    }

    pub fn intersects(&self, other: &Self) -> bool {
        self.0.iter().zip(&other.0).any(|(a, b)| a & b != 0)
    }

    pub fn count(&self) -> usize {
        self.0.iter().map(|w| w.count_ones() as usize).sum()
    }

    /// The indices in the set, in order.
    pub fn iter(self) -> impl Iterator<Item = usize> {
        self.0.into_iter().enumerate().flat_map(|(w, word)| BitIter(word).map(move |b| w * 64 + b))
    }

    /// The show-file form: the mask in decimal while only groups 0..63 are
    /// set (what shows have always stored), else the four words in hex
    /// separated by ':'.
    pub fn to_attr(&self) -> String {
        if self.0[1..].iter().all(|w| *w == 0) {
            self.0[0].to_string()
        } else {
            self.0.iter().map(|w| format!("{w:x}")).collect::<Vec<_>>().join(":")
        }
    }

    /// Inverse of [`to_attr`](Self::to_attr).
    pub fn parse_attr(s: &str) -> Option<Self> {
        let s = s.trim();
        if !s.contains(':') {
            return s.parse::<u64>().ok().map(Self::from_mask);
        }
        let mut out = Self::EMPTY;
        let mut words = s.split(':');
        for w in out.0.iter_mut() {
            *w = u64::from_str_radix(words.next()?.trim(), 16).ok()?;
        }
        words.next().is_none().then_some(out)
    }
}

impl fmt::Debug for GroupSet {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_set().entries(self.iter()).finish()
    }
}

impl BitAnd for GroupSet {
    type Output = Self;
    fn bitand(mut self, rhs: Self) -> Self {
        self &= rhs;
        self
    }
}

impl BitAndAssign for GroupSet {
    fn bitand_assign(&mut self, rhs: Self) {
        self.0.iter_mut().zip(rhs.0).for_each(|(a, b)| *a &= b);
    }
}

impl BitOr for GroupSet {
    type Output = Self;
    fn bitor(mut self, rhs: Self) -> Self {
        self |= rhs;
        self
    }
}

impl BitOrAssign for GroupSet {
    fn bitor_assign(&mut self, rhs: Self) {
        self.0.iter_mut().zip(rhs.0).for_each(|(a, b)| *a |= b);
    }
}

impl Not for GroupSet {
    type Output = Self;
    fn not(self) -> Self {
        Self(self.0.map(|w| !w))
    }
}

struct BitIter(u64);

impl Iterator for BitIter {
    type Item = usize;
    fn next(&mut self) -> Option<usize> {
        if self.0 == 0 {
            return None;
        }
        let b = self.0.trailing_zeros() as usize;
        self.0 &= self.0 - 1;
        Some(b)
    }
}

/// A [`GroupSet`] shared lock-free between the UI and the audio thread. A
/// reader may see a store half done across words; memberships change one
/// group at a time, so that is never a set that wasn't meant.
#[repr(C)]
#[derive(Debug, Default)]
pub struct AtomicGroupSet([AtomicU64; GROUP_WORDS]);

impl AtomicGroupSet {
    pub fn load(&self) -> GroupSet {
        GroupSet(std::array::from_fn(|w| self.0[w].load(Relaxed)))
    }

    pub fn store(&self, s: GroupSet) {
        for (a, w) in self.0.iter().zip(s.0) {
            a.store(w, Relaxed);
        }
    }

    pub fn contains(&self, i: usize) -> bool {
        i < MAX_GROUPS && self.0[i / 64].load(Relaxed) & (1 << (i % 64)) != 0
    }

    /// Adds or removes one group (atomically for that group).
    pub fn set(&self, i: usize, on: bool) {
        if i >= MAX_GROUPS {
            return;
        }
        let bit = 1u64 << (i % 64);
        if on {
            self.0[i / 64].fetch_or(bit, Relaxed);
        } else {
            self.0[i / 64].fetch_and(!bit, Relaxed);
        }
    }

    pub fn is_empty(&self) -> bool {
        self.0.iter().all(|w| w.load(Relaxed) == 0)
    }

    pub fn clear(&self) {
        self.store(GroupSet::EMPTY);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sets_hold_every_group_and_round_trip_through_shows() {
        let mut s = GroupSet::EMPTY;
        for i in [0, 7, 63, 64, 200, 255] {
            s.insert(i);
        }
        s.insert(MAX_GROUPS); // out of range: ignored
        assert_eq!(s.iter().collect::<Vec<_>>(), [0, 7, 63, 64, 200, 255]);
        assert_eq!(s.count(), 6);
        assert_eq!(GroupSet::parse_attr(&s.to_attr()), Some(s));
        // Shows store the WING's groups as a decimal mask, as they always did.
        let wing = GroupSet::from_mask(0b1010);
        assert_eq!(wing.to_attr(), "10");
        assert_eq!(GroupSet::parse_attr("10"), Some(wing));
        assert_eq!(GroupSet::parse_attr("1:2"), None);
        assert_eq!(GroupSet::first(16).low(), 0xffff);
        assert_eq!(GroupSet::first(MAX_GROUPS), !GroupSet::EMPTY);
        assert!(GroupSet::first(70).contains(69) && !GroupSet::first(70).contains(70));

        let a = AtomicGroupSet::default();
        a.set(130, true);
        a.set(3, true);
        a.set(3, false);
        assert_eq!(a.load(), GroupSet::single(130));
        assert!(a.contains(130) && !a.is_empty());
    }
}
