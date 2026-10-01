# Experiment 07: Memory order

Notes from [`experiments/07_memory_order`](../../experiments/07_memory_order/), which has two programs:

- [`publishing_pattern.cpp`](../../experiments/07_memory_order/publishing_pattern.cpp) (`exp_07_publishing_pattern`): one thread publishes data through a flag, another receives it.
- [`store_buffering.cpp`](../../experiments/07_memory_order/store_buffering.cpp) (`exp_07_store_buffering`): two threads each write their own variable and read the other's.

## Why reordering happens

Code says "write `data`, then write `flag`". **Neither the compiler nor the CPU promises to do it in that order.**

- **The compiler** may swap independent memory operations, keep values in registers, or move a read out of a loop.
- **The CPU** executes out of order. Writes first go into a per-core **store buffer** before reaching memory other cores can see, and they can become visible to other cores in a different order.

**A single thread can never tell:** the *as-if rule* guarantees its own results look as if the code ran in order. **Other threads can tell**, because they may see the writes in a different order, or later than expected.

| CPU | Reordering allowed |
|---|---|
| x86 (Intel, AMD) | Very little: only a store followed by a load of a *different* variable can appear swapped. |
| ARM (Apple Silicon, phones, Jetson, Raspberry Pi) | Almost anything. Bugs that stay hidden on x86 actually happen. |

Code that "works" on an x86 laptop can fail on an ARM robot computer.

## Memory orders

| Order | Guarantees | Use for |
|---|---|---|
| `relaxed` | Only that this operation is indivisible. Anything around it can be reordered. | Counters and statistics nothing else depends on |
| `release` (store) | Nothing written **before** it can move **after** it. Publishes everything before it. | Writing a "ready" flag after preparing data |
| `acquire` (load) | Nothing read **after** it can move **before** it. | Reading the "ready" flag before using the data |
| `acq_rel` | Both, for read-modify-write operations (`fetch_add`, `exchange`) | |
| `seq_cst` (default) | Acquire/release **plus** one global order of all `seq_cst` operations that every thread agrees on | The default; whenever unsure |

**Release/acquire pairing:** if an acquire load reads the value written by a release store **on the same atomic**, everything written before the release is visible after the acquire. This *synchronises-with* relationship creates *happens-before* between the threads. A mutex works this way: `lock()` is an acquire, `unlock()` is a release.

The acquire only counts if **it** is the load that reads the released value. An acquire on an earlier load, followed by relaxed loads in a loop, guarantees nothing about the value the loop finally sees.

### Rules of thumb

1. **Default to `seq_cst`** (don't pass an order).
2. **One-directional handoff** ("data prepared, flag says ready"): release on the flag store, acquire on the flag load.
3. **`relaxed`** only for values nothing else depends on.
4. **Never signal between threads with a plain variable.** That's a data race: undefined behaviour on every CPU.
5. **"I write mine, then read yours"** across two variables needs `seq_cst` or a mutex.

## Part A: Publishing pattern

```
Writer: for i = 1..50M:   data.store(i, relaxed);   flag.store(i, ORDER);
Reader: until flag == 50M: f = flag.load(ORDER);    d = data.load(relaxed);
                           if d < f → violation
```

A violation means the reader saw the flag for item `i` but data from before `i`. That's impossible if the order of each pair is kept.

- **`data` must be an atomic too** (relaxed). A plain `int` would be a data race: undefined behaviour, and the results would mean nothing. As relaxed atomics, the program is well-defined but reordering is allowed, which is what the experiment looks for.
- **Check on every pass of the loop, while the writer is running.** The first version only checked once, after the flag reached 50M. By then the writer had finished, so both versions looked identical. Reordering can only show up in the few nanoseconds around each store.
- **Count, don't print** inside the loop; printing changes the timing so much the effect disappears.

### Results

Release build, 50M items, 5 runs each:

| Flag order | Checks per run | Violations |
|---|---|---|
| release / acquire | 57.6M – 68.9M | **0** every run |
| relaxed (modified copy of the experiment) | 42.9M – 67.3M | **223,193 – 448,022** (~0.4–0.7% of checks) |

ThreadSanitizer: no reports (relaxed atomics are not data races).

- **Relaxed lets the reader see the flag before the data.** Either the writer's two stores became visible out of order, or the reader's two loads ran out of order. ARM allows both, and at `-O3` the compiler may reorder them too.
- **The rate depends on how the code was compiled.** An earlier quick test of the same pattern, built slightly differently, saw only 0–1,391 violations per run. A rare bug and a frequent bug are the same bug.
- **Release/acquire: zero in every run.** But zero in a test doesn't prove correctness. The ordering guarantee is what makes it correct.

## Part B: Store buffering

```
Thread 1:  x.store(1);  r1 = y.load();
Thread 2:  y.store(1);  r2 = x.load();
```

Each thread writes before it reads, so it seems at least one must see the other's `1`. Can both read 0?

### Setup

- **`std::barrier` (C++20):** `arrive_and_wait()` blocks until all participants arrive, then releases them together.
  - `start_barrier` lines both threads up at the start of each trial, so their store/load pairs overlap. Without it, one thread usually finishes before the other starts.
  - `end_barrier` runs a **completion function** exactly once per trial, after both arrive and before either is released. It checks `r1`/`r2` and resets `x`/`y`. It must be `noexcept`.
- **Thread bodies are lambdas:** the type of a barrier with a completion lambda has no name you can write in a function parameter. Lambdas with `[&]` use the barriers and variables from the surrounding function directly.
- **`r1`, `r2`, `both_zero` are plain ints.** Each `r` is written by one thread before arriving at `end_barrier` and only read in the completion function; the barrier makes those writes visible, like `join()`.

### Results

Release build, 200,000 trials, 3 runs:

| Orders | Trials where both read 0 |
|---|---|
| release / acquire | 197,826 · 199,465 · 199,390 (**~99%**) |
| seq_cst | 0 · 0 · 0 |

ThreadSanitizer: no reports.

### What happens: the store buffer

```
             Core 1                         Core 2
t0   x = 1 → into Core 1's buffer    y = 1 → into Core 2's buffer
     (not visible to Core 2 yet)     (not visible to Core 1 yet)
t1   r1 = y.load() → y is still 0    r2 = x.load() → x is still 0
t2   buffer drains: x = 1 visible    buffer drains: y = 1 visible
```

Each store happened first in its own core's view, but its load ran **before the store became visible to the other core**. From the outside, store and load were swapped. The barrier releases both threads at almost the same instant, so this is the *normal* outcome here, not a rare one.

### Why release/acquire doesn't prevent it

Each thread needs **its store to stay before its later load of a different variable**:

- **Release** only stops things *before* the store moving after it. The load is after the store, so it isn't covered.
- **Acquire** only stops things *after* the load moving before it. The store is before the load, so it isn't covered.

Neither rule orders **a store followed by a load of a different variable**. Release/acquire is designed for **one-directional handoff through the same variable** (Part A). Here, Thread 2's acquire load of `x` isn't guaranteed to read Thread 1's release store, so no synchronisation happens.

### Why seq_cst prevents it

All `seq_cst` operations fall into **one order every thread agrees on**, respecting each thread's program order. Whichever store comes first in that order, the other thread's load comes after it and must see `1`. Both reading 0 has no place in any single order.

In hardware: on ARM, a `seq_cst` load (`LDAR`) waits until earlier `seq_cst` stores (`STLR`) have left the store buffer. On x86, a `seq_cst` store is compiled to `XCHG`, which empties the store buffer. Either way, the store becomes visible before the load runs, and that waiting is why `seq_cst` can be slower.

### This one happens on x86 too

Store-then-load of a different variable is the one reordering x86 allows. Part A's bug can't happen on x86; Part B's happens on every common CPU.

### Why it matters

Any logic of the form **"I set my flag, then check yours"**:

- **Hand-made locks** ("I want in; you don't? I enter"): both threads can enter. Peterson's and Dekker's algorithms need `seq_cst`.
- **Sleep/wake handshakes** ("I'm going to sleep; is there work?" vs. "I added work; are you asleep?"): both miss each other, and the worker sleeps with work waiting. This is the lost wakeup from [experiment 06](experiment-06.md); "change the state under the mutex" prevents it because the mutex provides the ordering.

## TSan can't find these

Every access here is atomic, so there's no data race and ThreadSanitizer stays silent, as with the broken atomic in [experiment 05](experiment-05.md). Memory-ordering bugs are found by reasoning about the code, not by tools.

## Further study

- Jeff Preshing's blog (preshing.com): *Memory Reordering Caught in the Act* (this exact store-buffering test) and *Acquire and Release Semantics*.
- Herb Sutter, *atomic<> Weapons* (C++ and Beyond 2012): the standard long-form talk on the C++ memory model.
- Fedor Pikus, *C++ atomics, from basic to advanced* (CppCon 2017).

## Follow-ups

- Run the relaxed version of Part A from the experiment code itself, by passing the memory order as a parameter, so both Part A results come from the same program.
- Optional: time Part B's release/acquire against seq_cst to see the cost of the stronger guarantee.
