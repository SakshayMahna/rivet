# Experiment 03: Data race

Notes from [`experiments/03_data_race`](../../experiments/03_data_race/main.cpp): two threads increment the same `int` 1,000,000 times each, with no synchronisation.

## Passing a reference to a thread

`increment` takes `int& counter`. Starting it with `std::jthread t(increment, counter)` **doesn't compile**:

```
jthread.h:58: error: static assertion failed due to requirement
  'is_invocable_v<void (*)(int &), int> || is_invocable_v<void (*)(int &), std::stop_token, int>'
```

`jthread` copies `counter` and passes the copy as a temporary, which an `int&` can't point at. The check tries both ways of calling the function, with and without a `stop_token` first, and neither works.

- **Fix:** `std::ref(counter)` (from `<functional>`). You now promise `counter` outlives the thread.
- **Only read `counter` after the threads are joined.** Here the threads live in an inner `{ }` block, so both are joined before the print.

**Reading template errors:** the error is reported inside the standard library headers, not your file, so the editor may not show a squiggle. Check the **Problems** panel or the `cmake --build` output. The first `error:` line usually has the cause; look for your own types in it.

## Why `++counter` loses updates

`++counter` is three steps: **read** the value, **add 1**, **write** it back. Two threads can interleave like this:

```
Thread A: read 41
Thread B: read 41
Thread A: write 42
Thread B: write 42     ← A's increment is lost
```

This is a **data race**: two threads access the same memory at the same time, at least one writes, and nothing coordinates them. It is **undefined behaviour**. The C++ standard makes no promise about what the program does, and wrong numbers are only one possible outcome.

## Results

| Build | Configure | 5 runs (expected 2,000,000) |
|---|---|---|
| Debug | `-DCMAKE_BUILD_TYPE=Debug` | 973,056 · 1,023,933 · 1,006,978 · 1,006,546 · 1,440,979 |
| Release | `-DCMAKE_BUILD_TYPE=Release` | 2,000,000 every time |
| TSan | `-DRIVET_ENABLE_TSAN=ON` | `WARNING: ThreadSanitizer: data race` |

**Debug:** no optimisation, so every `++counter` really reads and writes memory. The threads overlap almost completely and about half the increments are lost. The result differs on every run.

**Release:** with `-O3`, the compiler sees "add 1 a million times" and turns the loop into roughly one `counter += 1000000`. Each thread finishes almost instantly with a single write, so they barely overlap. **The bug is still there**; optimisation only hid it. A passing test proves nothing about a data race.

**TSan:** reports the race directly:

```
WARNING: ThreadSanitizer: data race
  Write of size 4 at 0x00016ce4ea48 by thread T2:
    #0 (anonymous namespace)::increment(int&) main.cpp:10
  Previous write of size 4 at 0x00016ce4ea48 by thread T1:
    #0 (anonymous namespace)::increment(int&) main.cpp:10
  Location is stack of main thread.
  Thread T2 (tid=..., running) created by main thread at:
    ...
    #7 main main.cpp:18
```

How to read it:

- **Two accesses to the same address** (`0x00016ce4ea48`), each with a stack trace. Frame `#0` is your code: both are the `++counter` line.
- **"Write of size 4":** a 4-byte `int` being written.
- **"Location is stack of main thread":** the memory is `counter`, a local variable in `main`, shared through `std::ref`.
- **"created by main thread at … main.cpp:18":** where the thread was started.
- The long `std::__1::...` frames in between are the library code that starts the thread. Skip them.

## Separate build folders

Each `cmake -S . -B <folder>` is an **independent build** with its own settings and its own compiled programs. Same source files, different flags.

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release   # configure: once per folder, or after CMake changes
cmake --build build-release                              # compile
./build-release/experiments/03_data_race/exp_03_data_race
```

- `cmake -S . -B <folder>` **configures**. It creates the folder, `CMakeCache.txt` and the build files. Never create the folder with `mkdir` yourself.
- `cmake --build <folder>` **compiles**, and only works on a configured folder. On an empty folder it fails with `Error: could not load cache`.
- **Running an old program:** if compiling fails, the previous program is still in the folder. Always check the build output before trusting what you run.
- **After adding a new experiment folder, re-run the configure step for `build`.** That regenerates `build/compile_commands.json`, which VS Code reads. Without it the editor falls back to an older C++ standard and shows false errors (such as a missing `jthread`).
- The TSan documentation recommends `-O1` or `-O2`: `-DCMAKE_BUILD_TYPE=RelWithDebInfo` for the TSan folder.

Run a program several times with:

```bash
for i in {1..5}; do ./build/experiments/03_data_race/exp_03_data_race; done
```

## ThreadSanitizer setup

```cmake
option(RIVET_ENABLE_TSAN "Build with ThreadSanitizer" OFF)
if(RIVET_ENABLE_TSAN)
    add_compile_options(-fsanitize=thread -g)
    add_link_options(-fsanitize=thread)
endif()
```

- The flag is needed at compile time (to add the checks) **and** link time (to link the TSan runtime library).
- `-g` adds debug info, so the report shows file names and line numbers.
- Put this in the **root** `CMakeLists.txt`. Code built with and without TSan shouldn't be mixed in one program.

## Things that don't fix a data race

- **`volatile`:** stops some compiler optimisations but gives no thread safety.
- **A Release build that happens to give the right answer** (see above).
- **Fewer iterations:** with a small loop, the first thread often finishes before the second starts, so the result looks right. The race is still possible.

What does fix it: a **mutex** (experiment 04) or **`std::atomic`**.
