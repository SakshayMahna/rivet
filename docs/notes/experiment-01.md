# Experiment 01: Threads

Notes from [`experiments/01_threads`](../../experiments/01_threads/main.cpp): starting threads with `std::thread`, the CMake setup around it, and the C++ concepts that came up along the way.

## Threads

### What does `join()` do?

`t.join()` **blocks the calling thread until thread `t` has finished.** Two more effects:

- After `join()` returns, the `std::thread` object no longer represents a running thread. `t.joinable()` becomes `false`, and joining twice is an error.
- **It is a synchronisation point.** Everything the joined thread wrote to memory is guaranteed to be visible to the caller after `join()` returns. This is a *happens-before* relationship, the same idea that mutexes and atomics are built on.

`join()` only decides when the caller *waits*. The threads run concurrently from the moment they are constructed. In the experiment, A and B run side by side; `thread_a.join()` waits about 2.5 s, then `thread_b.join()` waits the remaining 2.5 s.

### What happens if I don't join?

**The program aborts.** If a `std::thread` is destroyed while it is still *joinable* (neither joined nor detached), its destructor calls `std::terminate()`.

This is deliberate: the library refuses to guess whether you wanted to wait or to abandon the thread. You must choose:

| Choice | Meaning |
|---|---|
| `join()` | Wait for the thread to finish. Almost always the right choice. |
| `detach()` | Let the thread run on its own. You can never wait for it or check whether it finished. Rarely a good idea. |
| `std::jthread` (C++20) | Joins automatically in its destructor. |

### What happens when `main` exits?

Returning from `main` calls `exit()`: global objects are destroyed and output buffers are flushed. Then **the whole process ends, including every thread still running.** Those threads are stopped immediately: no destructors run and their work is abandoned.

This is why `detach()` is dangerous. A detached thread may still be using a global object (a logger, say) while `exit()` is destroying it.

### What does `std::this_thread::sleep_for()` do?

It **pauses the current thread for at least the given duration.** The OS scheduler takes the thread off the CPU, so a sleeping thread uses no CPU time. The experiment ran for about 5.5 s with `0.00s user` CPU time.

The key phrase is **"at least"**. The thread wakes when the timer expires *and* the scheduler gets round to running it, which can be late on a busy system. For periodic robotics loops:

```cpp
while (true) {
    do_work();          // variable duration
    sleep_for(100ms);   // period = 100ms + work time + wake-up delay → drifts
}
```

This loop runs slower than 10 Hz, and the error builds up over time. Use `sleep_until(next_wakeup)` and add the period to `next_wakeup` on each loop instead.

## CMake

### Project layout

Each folder has its own `CMakeLists.txt`, and each parent pulls its children in with `add_subdirectory()`:

```
CMakeLists.txt                  → add_subdirectory(experiments)   (behind RIVET_BUILD_EXPERIMENTS)
experiments/CMakeLists.txt      → find_package(Threads), add_subdirectory(01_threads)
experiments/01_threads/CMakeLists.txt → add_executable + target_link_libraries
```

- **Targets** are the central idea in modern CMake: a named thing to build (an executable or library) with its settings attached to it. Target names must be unique across the whole project, hence `exp_01_threads` rather than `main`.
- **`find_package(Threads REQUIRED)`** finds the platform's threading library and creates the imported target `Threads::Threads`. On Linux, `std::thread` needs pthreads linked; on macOS it happens to work without it.
- **`target_link_libraries(exp_01_threads PRIVATE Threads::Threads)`** links the library and brings in any flags it needs. `PRIVATE` means "I use this, but whoever links to me doesn't inherit it". Executables are never linked to, so `PRIVATE` is always right for them. `PUBLIC` and `INTERFACE` matter for libraries.
- Variables set in a parent folder (such as `CMAKE_CXX_STANDARD`) apply to every folder added below it.
- **Prefix option names** (`RIVET_BUILD_EXPERIMENTS`, not `BUILD_EXAMPLES`). Options are global, so unprefixed names clash when the project is included in someone else's build.

Build from the repo root:

```bash
cmake -S . -B build          # configure (re-run only after changing CMake files)
cmake --build build          # compile
```

`build/compile_commands.json` shows the exact compiler commands CMake created. Editors use it for accurate autocomplete.

### Compiler warnings

Compilers warn about very little by default. These flags turn on more checks:

| Flag | Adds |
|---|---|
| `-Wall` | Common mistakes: unused variables, missing `return`, misleading indentation |
| `-Wextra` | More: unused parameters, signed/unsigned comparisons |
| `-Wpedantic` | Compiler-specific, non-standard extensions |

Warnings are the cheapest bug-finder available. `-Werror` turns them into errors so they can't pile up.

- `add_compile_options(...)` applies to every target defined after it, in that folder and below. Simple, but global.
- The per-target alternative is an `INTERFACE` library (e.g. `rivet_warnings`) that each target links. That lets each target choose, which matters once third-party code with unfixable warnings is involved.

### Build type

| `CMAKE_BUILD_TYPE` | Flags | Use for |
|---|---|---|
| `Debug` | `-O0 -g` | Stepping through code in a debugger |
| `Release` | `-O3 -DNDEBUG` | Shipping, benchmarking |
| `RelWithDebInfo` | `-O2 -g -DNDEBUG` | Profiling |
| *(empty)* | *nothing* | Nothing. Unoptimised *and* no debug info. |

With no build type set, timings are meaningless and debugging works poorly, so set a default:

```cmake
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
```

`CMAKE_CONFIGURATION_TYPES` is set by tools such as Xcode that manage build types themselves; the guard leaves those alone. `-DNDEBUG` disables `assert()`, so `Debug` is a good default while learning. Switch to `Release` when benchmarking.

### ThreadSanitizer (TSan)

`-fsanitize=thread` makes the compiler add a check around every memory access. A runtime library tracks which thread touched which memory and whether anything synchronised the accesses. It reports **data races** with both stack traces.

- A **data race** means two threads access the same memory at the same time, at least one writes, and nothing (mutex, atomic) coordinates them. It is *undefined behaviour*: racy code often passes every test and then fails rarely and unpredictably.
- TSan only catches races that actually happen during that run.
- The program runs about 5–15× slower, with more memory, so it's for testing only.
- It can't be combined with AddressSanitizer.
- The flag is needed at both compile *and* link time.

**Race condition vs. data race:** mixed-up `std::cout` output is a *race condition* (the result depends on timing), but not a *data race*. The standard guarantees `std::cout` handles concurrent use safely, so TSan won't flag it. Every data race is a bug; a race condition is a bug only if the ordering matters.

## C++ concepts

### `constexpr` vs. `const`

- **`const`**: can't be changed after it's set, but the value can be computed while the program runs (`const int n = read_from_file();`).
- **`constexpr`**: must be known at compile time, or compilation fails.

Compile-time constants can be used where the language needs a fixed value: array sizes, template arguments, `static_assert`. For an `int` initialised with a literal, `const` is already a compile-time constant, so the difference is small. For other types, such as `std::chrono::milliseconds`, only `constexpr` guarantees it. **Rule: use `constexpr` for every fixed value.**

### Units in types

Pass `std::chrono::milliseconds`, not a bare `int`. The unit becomes part of the type, so passing seconds where milliseconds were expected is no longer possible by accident.

### Translation units, the linker and linkage

1. **Compilation:** each `.cpp` file is compiled *on its own* into an object file. This unit is a *translation unit*; the compiler can't see other `.cpp` files.
2. **Linking:** the linker combines object files, connecting uses of a name in one file to its definition in another. These names are *symbols*.

**Linkage** decides whether a name is visible across translation units:

- **External:** visible to the linker from other files. Functions and non-const globals have this by default.
- **Internal:** private to its own file. `static` functions, anything in an anonymous namespace, and `const`/`constexpr` namespace-scope variables.

**Why it matters:** if two `.cpp` files in the same library both define `void thread_function(...)` with external linkage, the linker fails with *duplicate symbol*. With `inline` functions or templates it can be worse: the linker silently picks one version and uses it everywhere. That breaks the One Definition Rule and causes confusing bugs.

**Fix:** put file-local helpers in an anonymous namespace:

```cpp
namespace {
void thread_function(...) { ... }   // internal linkage: private to this .cpp
}  // namespace
```

This also lets the compiler optimise more (it sees every place the function is called) and warn if it's never used. **Named** namespaces (`namespace rivet { ... }`) solve a different problem: keeping public names from clashing with other libraries.

In a single-file experiment nothing can clash; the anonymous namespace is a habit that becomes important once files share a library.

### Passing arguments to `std::thread`

**`std::thread` always copies its arguments** into storage owned by the new thread, then calls the function with those copies.

| Parameter type | Result |
|---|---|
| `std::string name` | Works. Built from the stored copy. |
| `const std::string& name` | Also works. Refers to the thread's own copy, which lives as long as the thread. |
| `std::string& name` | **Doesn't compile.** The copy is passed as a temporary, and a non-const reference can't point at one. This stops you from thinking you're changing the caller's variable. |
| `const std::string name` | Works. `const` only stops the function body from changing its own copy; it's a style choice. |

**Where it goes wrong:** when the copied thing is itself a *pointer*, such as `std::string_view`, `const char*` or a raw pointer. `std::thread` copies the pointer, not the data. If that data is a local variable destroyed while the thread still runs, the thread reads freed memory.

**`std::ref(x)`** is how you deliberately pass a real reference, promising that `x` stays alive for as long as the thread uses it.

## Style

- End each file with a newline (enforced by `.editorconfig`).
- Mark where a namespace closes: `}  // namespace`.
- Use one naming style throughout (`thread_a`, not `tA`, next to `sleep_duration_a`).
- Prefer `++i` to `i++` out of habit: no difference for `int`, but `i++` can make an extra copy for iterators and other class types.
- Start each experiment with a comment saying what it demonstrates and what to watch for.
