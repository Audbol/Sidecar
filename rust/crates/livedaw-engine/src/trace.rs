//! Diagnostics: where a slow block's time went. With
//! LIVEDAW_TRACE_SLOW=<file>, every block that takes longer than it lasts
//! appends a line: each stage's wall time and the audio thread's CPU cycles,
//! and for each parallel stage the longest task (wall and cycles, who ran it)
//! and how long the audio thread waited for the workers after its own tasks.
//! Off, each call is a check of a flag read once (by `Engine::new`, off the
//! audio thread). On, a slow block's line is
//! written from the audio thread (a diagnostic run, not a show).

use std::cell::UnsafeCell;
use std::io::Write;
use std::sync::atomic::{AtomicU64, AtomicUsize, Ordering::Relaxed};
use std::sync::OnceLock;
use std::time::Instant;

#[cfg(windows)]
extern "system" {
    fn GetCurrentThread() -> isize;
    fn QueryThreadCycleTime(thread: isize, cycles: *mut u64) -> i32;
    fn GetCurrentProcess() -> isize;
    fn K32GetProcessMemoryInfo(process: isize, counters: *mut [usize; 10], cb: u32) -> i32;
}

/// The process's page faults so far (0 where unknown).
pub fn page_faults() -> u64 {
    #[cfg(windows)]
    unsafe {
        // PROCESS_MEMORY_COUNTERS: cb and PageFaultCount (two u32s), then sizes.
        let mut c = [0usize; 10];
        c[0] = std::mem::size_of::<[usize; 10]>() as u32 as usize;
        if K32GetProcessMemoryInfo(GetCurrentProcess(), &mut c, std::mem::size_of::<[usize; 10]>() as u32) != 0 {
            return (c[0] >> 32) as u64;
        }
        0
    }
    #[cfg(not(windows))]
    0
}

/// The calling thread's CPU cycles so far (0 where unknown).
pub fn thread_cycles() -> u64 {
    #[cfg(windows)]
    unsafe {
        let mut c = 0u64;
        QueryThreadCycleTime(GetCurrentThread(), &mut c);
        c
    }
    #[cfg(not(windows))]
    0
}

fn path() -> Option<&'static str> {
    static P: OnceLock<Option<String>> = OnceLock::new();
    P.get_or_init(|| std::env::var("LIVEDAW_TRACE_SLOW").ok().filter(|p| !p.is_empty())).as_deref()
}

pub fn enabled() -> bool {
    path().is_some()
}

const MARKS: usize = 64;

struct Mark {
    label: &'static str,
    ns: u64,
    cycles: u64,
    // A parallel stage's longest task, who ran it (usize::MAX the audio
    // thread), and the audio thread's wait for the workers.
    task_ns: u64,
    task_cycles: u64,
    task_who: usize,
    task_faults: u64,
    wait_ns: u64,
    tasks: usize,
    faults: u64,
}

struct Inner {
    start: Option<Instant>,
    last: Option<Instant>,
    last_cycles: u64,
    last_faults: u64,
    marks: Vec<Mark>,
}

pub struct Trace(UnsafeCell<Inner>);

// SAFETY: the audio thread only.
unsafe impl Sync for Trace {}

static TRACE: OnceLock<Trace> = OnceLock::new();

fn get() -> &'static Trace {
    TRACE.get_or_init(|| {
        Trace(UnsafeCell::new(Inner {
            start: None,
            last: None,
            last_cycles: 0,
            last_faults: 0,
            marks: Vec::with_capacity(MARKS),
        }))
    })
}

/// The pool's stats of the current parallel stage (any thread writes them).
pub struct PoolStats {
    pub task_ns: AtomicU64,
    pub task_cycles: AtomicU64,
    pub task_who: AtomicUsize,
    pub task_faults: AtomicU64,
    pub wait_ns: AtomicU64,
    pub tasks: AtomicUsize,
}

pub static POOL: PoolStats = PoolStats {
    task_ns: AtomicU64::new(0),
    task_cycles: AtomicU64::new(0),
    task_who: AtomicUsize::new(0),
    task_faults: AtomicU64::new(0),
    wait_ns: AtomicU64::new(0),
    tasks: AtomicUsize::new(0),
};

impl PoolStats {
    /// A task that took `ns` (and `cycles`) on `who`.
    pub fn task(&self, ns: u64, cycles: u64, faults: u64, who: usize) {
        self.tasks.fetch_add(1, Relaxed);
        if self.task_ns.fetch_max(ns, Relaxed) < ns {
            self.task_cycles.store(cycles, Relaxed);
            self.task_who.store(who, Relaxed);
            self.task_faults.store(faults, Relaxed);
        }
    }

    fn take(&self) -> (u64, u64, usize, u64, u64, usize) {
        (
            self.task_ns.swap(0, Relaxed),
            self.task_cycles.swap(0, Relaxed),
            self.task_who.swap(0, Relaxed),
            self.task_faults.swap(0, Relaxed),
            self.wait_ns.swap(0, Relaxed),
            self.tasks.swap(0, Relaxed),
        )
    }
}

/// The block starts (audio thread).
pub fn begin() {
    if !enabled() {
        return;
    }
    let t = unsafe { &mut *get().0.get() };
    let now = Instant::now();
    t.start = Some(now);
    t.last = Some(now);
    t.last_cycles = thread_cycles();
    t.last_faults = page_faults();
    t.marks.clear();
    POOL.take();
}

/// The stage `label` ended (audio thread).
pub fn mark(label: &'static str) {
    if !enabled() {
        return;
    }
    let t = unsafe { &mut *get().0.get() };
    let (Some(last), true) = (t.last, t.marks.len() < MARKS) else { return };
    let now = Instant::now();
    let cycles = thread_cycles();
    let faults = page_faults();
    let (task_ns, task_cycles, task_who, task_faults, wait_ns, tasks) = POOL.take();
    t.marks.push(Mark {
        label,
        ns: (now - last).as_nanos() as u64,
        cycles: cycles.wrapping_sub(t.last_cycles),
        task_ns,
        task_cycles,
        task_who,
        task_faults,
        wait_ns,
        tasks,
        faults: faults.wrapping_sub(t.last_faults),
    });
    t.last = Some(now);
    t.last_cycles = cycles;
    t.last_faults = faults;
}

/// The block ended; `budget_ns` its duration. Writes a slow block's line.
pub fn end(budget_ns: u64) {
    let Some(p) = path() else { return };
    let t = unsafe { &mut *get().0.get() };
    let Some(start) = t.start else { return };
    let total = start.elapsed().as_nanos() as u64;
    if total <= budget_ns {
        return;
    }
    let mut line = format!("SLOW {:.3} ms:", total as f64 / 1e6);
    for m in &t.marks {
        if m.ns < 100_000 && m.wait_ns < 100_000 {
            continue; // under 0.1 ms
        }
        line += &format!(" | {} {:.3}ms cyc {:.2}M pf {}", m.label, m.ns as f64 / 1e6, m.cycles as f64 / 1e6, m.faults);
        if m.tasks > 0 {
            let who = if m.task_who == usize::MAX { "audio".to_string() } else { format!("w{}", m.task_who) };
            line += &format!(
                " [{} tasks, longest {:.3}ms cyc {:.2}M pf {} on {}, wait {:.3}ms]",
                m.tasks,
                m.task_ns as f64 / 1e6,
                m.task_cycles as f64 / 1e6,
                m.task_faults,
                who,
                m.wait_ns as f64 / 1e6
            );
        }
    }
    if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(p) {
        let _ = writeln!(f, "{line}");
    }
}
