# Rivet

> A C++20 concurrency and real-time pipeline framework built from scratch to explore multithreading, low-latency robotics, and ROS 2.

## Project Purpose

Rivet is a learning-first robotics systems project.

The goal is not simply to build a useful library. The goal is to develop a deep understanding of:

- Modern C++
- Multithreading and concurrency
- Memory ownership and synchronization
- Lock-free data structures
- Performance and latency
- Real-time-ish robotics pipelines
- ROS 2 execution and communication

The project will eventually become a reusable C++ pipeline framework, but the primary objective is **learning by building**.

---

# Core Question

> How should a robotics system move sensor data through multiple concurrent processing stages while keeping latency bounded and information fresh?

The first application will be a camera pipeline.

The eventual system should be generic enough to handle:

- Cameras
- LiDAR
- IMU
- Robot state
- Detections
- Commands
- Telemetry
- ROS 2 messages

---

# Learning Rules

## 1. No AI-generated implementation code

The implementation will be written manually.

AI may be used for:

- Conceptual explanations
- Documentation
- Debugging hints
- Code review
- Asking questions
- Understanding compiler errors
- Finding concepts to investigate

AI should not write the project's implementation.

## 2. Understand before implementing

Every major component follows:

```text
Learn
  ↓
Experiment
  ↓
Design
  ↓
Implement
  ↓
Break
  ↓
Debug
  ↓
Benchmark
  ↓
Document
```

## 3. Build the simple version first

Do not start with lock-free structures or a sophisticated framework.

Progress from simple synchronization toward increasingly sophisticated concurrency.

## 4. Measure instead of assuming

Every performance claim should be supported by measurements.

Important metrics include:

- Throughput
- Latency
- Frame age
- Queue depth
- Dropped messages
- CPU usage
- Contention
- P50/P95/P99 latency

---

# Roadmap

## Phase 0 — Project Foundation

Set up:

- C++20
- CMake
- Testing
- Formatting
- Static analysis
- Sanitizers
- Benchmarks
- GitHub repository
- Basic CI

Initial repository:

```text
rivet/
├── include/
├── src/
├── tests/
├── experiments/
├── benchmarks/
├── examples/
├── ros2/
├── docs/
├── CMakeLists.txt
└── README.md
```

---

# Phase 1 — C++ Concurrency Laboratory

Before building Rivet, create small standalone experiments.

```text
experiments/
├── threads/
├── jthread/
├── mutex/
├── condition_variable/
├── atomic/
├── memory_order/
└── race_conditions/
```

Learn:

- `std::thread`
- `std::jthread`
- `std::stop_token`
- `std::mutex`
- `std::lock_guard`
- `std::unique_lock`
- `std::condition_variable`
- `std::atomic`
- Basic memory ordering
- Thread lifetime
- Cooperative cancellation

The purpose is not to create reusable code.

The purpose is to understand the primitives.

---

# Phase 2 — Producer / Consumer

Build the first reusable primitive:

```text
Producer
    ↓
  Queue
    ↓
Consumer
```

Implement a basic:

```text
BlockingQueue<T>
```

Learn:

- Producer/consumer architecture
- Blocking
- Condition variables
- Predicates
- Spurious wakeups
- Shutdown
- Data ownership

Stress test:

- 1 producer / 1 consumer
- 1 producer / multiple consumers
- Multiple producers / 1 consumer
- Multiple producers / multiple consumers

---

# Phase 3 — Bounded Queues

Make the queue capacity finite.

```text
Producer
    ↓
┌───────────────┐
│ Bounded Queue │
└───────────────┘
    ↓
Consumer
```

Explore overflow policies:

```text
Block
DropNewest
DropOldest
Reject
```

Answer:

> What should a robotics system do when the consumer cannot keep up?

This introduces the concepts of:

- Backpressure
- Queue saturation
- Dropping
- Latency growth

---

# Phase 4 — Latest-Value Channel

Build a communication primitive where freshness matters more than processing every item.

Example:

```text
Camera:

F1 → F2 → F3 → F4 → F5 → F6

Processor:

                    ↓
                    F6
```

Instead of forcing the processor to process:

```text
F1 → F2 → F3 → F4 → F5 → F6
```

it receives the newest available value.

Build:

```text
LatestChannel<T>
```

Measure:

- Frame age
- Dropped frames
- Latency
- Throughput

This becomes the conceptual foundation of the camera system.

---

# Phase 5 — Camera Pipeline v1

Build the first real application.

### Single-threaded

```text
Camera
  ↓
Capture
  ↓
Process
  ↓
Display
```

Then build:

### Multi-threaded

```text
Camera
  ↓
Capture Thread
  ↓
Queue
  ↓
Processing Thread
  ↓
Viewer Thread
```

Use:

- `std::jthread`
- `std::stop_token`

Measure the difference.

Intentionally make processing slower than the camera.

Observe queue growth and frame age.

---

# Phase 6 — Buffer Ownership

Introduce explicit frame ownership.

Conceptually:

```text
Capture
   ↓
Frame Buffer
   ↓
Processing
```

Build a:

```text
BufferPool
```

Study:

- Object lifetime
- Ownership
- References
- Pointers
- `unique_ptr`
- `shared_ptr`
- Move semantics
- Copying vs moving
- Buffer reuse

The key question:

> Who owns this buffer right now?

Draw ownership transitions before implementing them.

---

# Phase 7 — Atomics and SPSC

Build a single-producer/single-consumer ring buffer.

```text
Producer
   ↓
┌───────────────────┐
│ SPSC Ring Buffer  │
└───────────────────┘
   ↓
Consumer
```

Study:

- `std::atomic`
- Acquire
- Release
- Relaxed ordering
- Happens-before
- Cache lines
- False sharing

Benchmark:

```text
Mutex Queue
     vs
SPSC Ring Buffer
```

Do not assume the lock-free implementation will be faster.

Measure it.

---

# Phase 8 — Thread Pool

Build a reusable thread pool.

```text
                 ┌── Worker
                 ├── Worker
Task Queue ──────┼── Worker
                 └── Worker
```

Learn:

- Worker lifecycle
- Task queues
- Condition variables
- Task submission
- Shutdown
- Work distribution
- Contention

Benchmark:

```text
1 worker
2 workers
4 workers
8 workers
```

with different workloads.

---

# Phase 9 — Rivet Pipeline

Now combine the primitives.

Conceptually:

```text
Source
  ↓
Channel
  ↓
Stage
  ↓
Channel
  ↓
Stage
  ↓
Sink
```

Build the initial abstractions:

```text
Pipeline
Stage
Channel
Executor
```

Keep the API deliberately small.

The first goal is:

```text
source → stage → stage → sink
```

not a giant framework.

---

# Phase 10 — Pipeline Policies

Allow different stages to use different execution strategies.

Potential policies:

```text
Serial
Parallel
Latest
Periodic
```

Example:

```text
Camera
  ↓
Latest
  ↓
Preprocess
  ↓
Parallel × 4
  ↓
Inference
  ↓
Latest
  ↓
Controller
```

Investigate how different policies affect:

- Throughput
- Latency
- Freshness
- CPU utilization

---

# Phase 11 — Telemetry

Make observability a first-class part of Rivet.

Every message should carry metadata such as:

```text
Sequence number
Timestamp
```

Measure:

```text
Capture → Queue
Queue → Processing
Processing time
Processing → Output
Total latency
Frame age
```

Report:

```text
FPS
P50 latency
P95 latency
P99 latency
Maximum latency
Dropped messages
Queue depth
```

---

# Phase 12 — Stress Testing

Try to break the system.

Test:

- Producer faster than consumer
- Consumer faster than producer
- Multiple producers
- Multiple consumers
- Queue saturation
- Random processing delays
- Rapid startup/shutdown
- Shutdown while blocked
- Repeated creation/destruction
- High thread counts

Use appropriate tools such as:

- AddressSanitizer
- ThreadSanitizer
- UndefinedBehaviorSanitizer

The objective is not simply to make the code work.

The objective is to discover how it fails.

---

# Phase 13 — ROS 2 Integration

Only after the core C++ system is understood.

Create:

```text
ros2/
```

Explore:

- `rclcpp`
- Executors
- Callback groups
- QoS
- Publishers
- Subscriptions
- Composable nodes
- Intra-process communication
- Message ownership
- ROS 2 tracing

Architecture:

```text
             ROS 2
               │
               ▼
        ┌─────────────┐
        │    Rivet    │
        │    Core     │
        └─────────────┘
               │
               ▼
          Processing
```

Keep the core independent of ROS 2.

ROS 2 should be an integration layer.

---

# Phase 14 — Benchmarking

Compare multiple architectures using the same workload.

```text
A. Single-threaded
B. Multi-threaded + mutex
C. Bounded queue
D. Latest-value
E. SPSC
F. Thread pool
G. ROS 2
H. ROS 2 + intra-process
I. Rivet
```

Measure:

- Throughput
- P50 latency
- P95 latency
- P99 latency
- Frame age
- Dropped frames
- CPU usage

Do not decide the result beforehand.

The measurements determine the conclusion.

---

# Phase 15 — Final Camera Experiment

Return to the original problem.

### Naive pipeline

```text
Camera
  ↓
Process
  ↓
Display
```

### Rivet pipeline

```text
Camera
  ↓
Capture
  ↓
Latest Channel
  ↓
Processing
  ↓
Display
```

Point the camera at a stopwatch.

Measure glass-to-glass latency.

The final experiment should demonstrate:

> A system can have good FPS while still operating on stale information.

The objective is to show why **freshness and latency matter in robotics**, not merely that multiple threads can increase FPS.

---

# Final Repository Structure

```text
rivet/
│
├── include/
│   └── rivet/
│
├── src/
│
├── tests/
│
├── benchmarks/
│
├── experiments/
│   ├── threads/
│   ├── mutex/
│   ├── condition_variable/
│   ├── atomics/
│   └── memory_order/
│
├── examples/
│   ├── producer_consumer/
│   ├── camera/
│   └── pipeline/
│
├── ros2/
│
├── docs/
│
├── CMakeLists.txt
├── README.md
└── LICENSE
```

---

# Single Development Log

The project should have **one major development log**, rather than a series of separate technical tutorials.

## Working title

**I Learned C++ Multithreading by Building a Robotics Pipeline**

The story:

```text
I know C++.
       ↓
I realize I don't deeply understand concurrency.
       ↓
I decide to learn it by building something real.
       ↓
Threads
       ↓
Queues
       ↓
Condition variables
       ↓
Buffer ownership
       ↓
Atomics
       ↓
Lock-free structures
       ↓
Thread pools
       ↓
Pipeline architecture
       ↓
ROS 2
       ↓
Latency experiment
       ↓
I finally understand concurrency.
```

The development log should focus on:

- What I thought would happen
- What actually happened
- What broke
- What I misunderstood
- What experiment I ran
- What I learned
- What I changed

The final video is **the story of learning**, not a documentation walkthrough of the repository.

---

# Definition of Done

The project is complete when I can independently explain and demonstrate:

- Why and when a mutex is required
- How condition variables work
- Producer/consumer synchronization
- Bounded queues and backpressure
- Latest-value semantics
- Data ownership between threads
- Atomics
- Acquire/release memory ordering
- SPSC ring buffers
- Why lock-free does not automatically mean faster
- Thread pools
- Thread lifecycle and cancellation
- Pipeline scheduling
- Latency accumulation
- Frame freshness
- Data races and how to detect them
- ROS 2 executors and callback groups
- ROS 2 QoS
- Intra-process communication
- How to benchmark a concurrent robotics system

The real finish line is:

> **I can look at a multi-threaded robotics system and reason about its threads, ownership, synchronization, failure modes, and latency without relying on someone else's implementation.**

---

# Project Philosophy

Rivet is not being built to prove that I can write a sophisticated library.

It is being built to answer one question:

> **Can I become genuinely comfortable with C++ concurrency by building a real robotics system from first principles?**