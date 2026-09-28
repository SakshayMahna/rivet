# rivet
C++ concurrency and low-latency pipeline framework for robotics, built from scratch to explore multithreading, real-time systems, and ROS 2.

## Requirements

- CMake 3.20 or newer
- A C++20 compiler (Clang or GCC)

## Build

From the repository root:

```bash
cmake -S . -B build
cmake --build build
```

Executables are placed under `build/`, mirroring the source tree. For example:

```bash
./build/experiments/01_threads/exp_01_threads
```
