---
title: "Chapter 4.4 — The Completely Fair Scheduler (CFS)"
section: "Part III — Virtual Runtime, Nice Values, and Fair CPU Allocation"
volume: "Volume I — Enterprise Linux Systems Engineering"
difficulty: Advanced
estimated_time: "14–18 Hours"
---

# Chapter 4.4 — Part III

# Virtual Runtime (`vruntime`), Nice Values, and Fair CPU Allocation

> *"The Completely Fair Scheduler does not ask, 'How long has this task run?' It asks, 'How much CPU time should this task have received relative to every other runnable task?' The answer to that question is virtual runtime."*

---

# Introduction

In the previous sections, we learned that:

- CFS organizes runnable tasks in a Red-Black Tree.
- The scheduler selects the task with the **smallest `vruntime`**.
- Tasks are continuously reordered as they execute.

The next question is the most important one:

> **How does Linux calculate `vruntime`?**

Understanding this answer is essential for:

- Linux performance engineering
- Kubernetes CPU behavior
- Container performance tuning
- AI inference optimization
- Diagnosing unfair CPU allocation

---

# Learning Objectives

After completing this section, you should be able to:

- Explain the purpose of virtual runtime.
- Distinguish actual runtime from virtual runtime.
- Understand scheduler weights.
- Explain Linux **nice values**.
- Understand target latency and minimum granularity.
- Predict how CFS divides CPU time among competing tasks.
- Relate CPU scheduling to Kubernetes CPU requests and limits.

---

# Real Time vs Virtual Time

Suppose two tasks each execute for **10 milliseconds**.

Their actual runtime is identical:

| Task | Actual Runtime |
|------|----------------|
| A | 10 ms |
| B | 10 ms |

If Linux only tracked actual runtime, both tasks would appear equally deserving of CPU time.

However, what if Task A has a much higher scheduling priority than Task B?

Treating them identically would violate the scheduling policy.

This is why Linux measures **virtual runtime**, not just elapsed execution time.

---

# Definition

Virtual runtime (`vruntime`) is:

> **The amount of CPU time a task has effectively consumed after adjusting for its scheduling weight.**

Tasks with lower `vruntime` are considered to have received less fair service and therefore run sooner.

---

# Conceptual Formula

The actual Linux implementation is more sophisticated, but conceptually:

```text
vruntime += actual_runtime × adjustment_factor
```

The adjustment factor depends on the task's **weight**, which is derived from its **nice value**.

Higher weight:

- `vruntime` increases more slowly.

Lower weight:

- `vruntime` increases more quickly.

---

# Why This Works

Imagine two runners.

Runner A carries no backpack.

Runner B carries a heavy backpack.

Even if they run for the same amount of time, we would not judge their performance identically.

Linux applies the same idea.

Tasks with different scheduling weights accumulate virtual runtime at different rates.

---

# Nice Values

Linux allows users to influence scheduling priority through **nice values**.

The term comes from the idea that a process can be "nice" and yield more CPU time to others.

Range:

```text
-20 ............................ +19

Higher Priority          Lower Priority
```

| Nice Value | Meaning |
|------------|---------|
| -20 | Highest priority |
| 0 | Default |
| +19 | Lowest priority |

---

# Viewing Nice Values

Display running processes:

```bash
ps -eo pid,ni,comm
```

Example:

```text
PID   NI  COMMAND

  1    0  systemd
250    0  nginx
842    5  backup.sh
921  -10 realtime_app
```

---

# Changing Nice Values

Start a low-priority task:

```bash
nice -n 10 python train.py
```

Start a higher-priority task (requires appropriate privileges):

```bash
sudo nice -n -10 python inference.py
```

---

# Important Clarification

Nice values do **not** directly represent percentages of CPU time.

Instead, they map to **scheduler weights**.

The scheduler uses these weights when calculating fairness.

---

# Scheduler Weights

Internally, Linux converts each nice value into a numerical weight.

A simplified illustration:

| Nice | Approximate Weight |
|------|--------------------|
| -20 | 88761 |
| -10 | 9548 |
| 0 | 1024 |
| 10 | 110 |
| 19 | 15 |

The exact values are defined in the kernel and are chosen to provide smooth, multiplicative changes in CPU share.

---

# Why Use Weights?

Suppose:

Task A:

Weight = 1024

Task B:

Weight = 512

The scheduler attempts to provide Task A with roughly twice the CPU share of Task B over time.

Fairness is proportional, not absolute.

---

# Example

Two CPU-bound tasks:

| Task | Weight |
|------|--------|
| A | 1024 |
| B | 1024 |

Expected long-term CPU distribution:

```text
A ≈ 50%

B ≈ 50%
```

---

Now suppose:

| Task | Weight |
|------|--------|
| A | 2048 |
| B | 1024 |

Expected distribution:

```text
A ≈ 66%

B ≈ 33%
```

The higher-weight task accumulates `vruntime` more slowly, allowing it to remain closer to the left side of the Red-Black Tree.

---

# Target Latency

Another key concept in CFS is **target latency**.

Target latency is:

> **The period during which every runnable task should receive CPU time at least once, assuming the system is not overloaded.**

Example:

Suppose the target latency is:

```text
24 ms
```

and there are:

```text
4 runnable tasks
```

Each task ideally receives approximately:

```text
24 ms ÷ 4 = 6 ms
```

before the scheduling cycle repeats.

---

# Increasing Number of Tasks

Suppose there are:

```text
24 runnable tasks
```

Each would receive only about:

```text
24 ms ÷ 24 = 1 ms
```

Very small slices increase the number of context switches.

Too many context switches reduce performance.

This leads to another important parameter.

---

# Minimum Granularity

Linux defines a **minimum granularity**.

This is the smallest amount of CPU time the scheduler tries to allocate before switching tasks.

Without this limit:

- Tasks would execute for tiny fractions of a millisecond.
- Context-switch overhead would dominate useful work.

Minimum granularity prevents excessive scheduler overhead.

---

# Balancing Fairness and Efficiency

CFS continuously balances two competing goals:

- Fair distribution of CPU time.
- Minimizing unnecessary context switches.

This trade-off is central to scheduler design.

---

# Example Timeline

Three runnable tasks:

```text
A

B

C
```

Initially:

| Task | vruntime |
|------|-----------|
| A | 10 |
| B | 12 |
| C | 15 |

Scheduler selects:

```
A
```

After execution:

| Task | vruntime |
|------|-----------|
| A | 17 |
| B | 12 |
| C | 15 |

Next selection:

```
B
```

After execution:

| Task | vruntime |
|------|-----------|
| A | 17 |
| B | 18 |
| C | 15 |

Next selection:

```
C
```

Over time, `vruntime` values remain relatively close together.

---

# Production Perspective

Consider a Kubernetes worker node hosting:

- API Gateway
- PostgreSQL
- Redis
- Prometheus
- Fluent Bit
- AI inference server

All compete for CPU time.

Because CFS measures **relative fairness**, background monitoring agents continue to make progress without starving latency-sensitive request handlers.

---

# Cgroups and CPU Shares

Modern Linux rarely schedules individual processes in isolation.

Containers are commonly organized into **control groups (cgroups)**.

Cgroups allow administrators to allocate CPU resources proportionally.

Example:

```text
Container A

CPU Weight = 200

Container B

CPU Weight = 100
```

Container A receives approximately twice the CPU share under contention.

CFS enforces these weights when scheduling runnable tasks.

---

# Kubernetes Connection

Kubernetes does **not** schedule CPU instructions.

Instead:

1. Kubernetes assigns CPU requests and limits.
2. The container runtime configures cgroups.
3. Linux CFS enforces CPU allocation.

Understanding CFS explains why Pods behave differently under CPU contention.

---

# AI Infrastructure Connection

Large AI inference platforms often contain:

- request routers
- batching threads
- GPU workers
- telemetry exporters
- asynchronous logging
- metrics collection

Although GPUs perform the numerical computation, the CPU scheduler still determines how quickly requests reach the GPU and how efficiently auxiliary tasks execute.

Poor CPU scheduling can increase end-to-end inference latency even when GPUs remain underutilized.

---

# Hands-On Lab 4.4.3

## Objective

Observe the effect of nice values.

### Step 1 — Start Two CPU-Bound Tasks

Terminal 1:

```bash
yes > /dev/null
```

Terminal 2:

```bash
nice -n 10 yes > /dev/null
```

---

### Step 2 — Observe CPU Usage

Run:

```bash
top
```

Questions:

- Which task receives more CPU time?
- How does the scheduler distribute work?

---

### Step 3 — Inspect Nice Values

```bash
ps -eo pid,ni,pcpu,comm
```

Observe the relationship between:

- nice value
- CPU usage
- scheduler behavior

---

# Common Misconceptions

| Misconception | Reality |
|---------------|---------|
| Nice values directly represent CPU percentages. | They determine scheduler weights, not fixed percentages. |
| Equal runtime always means fairness. | CFS measures weighted fairness using `vruntime`. |
| High-priority tasks always run continuously. | They receive proportionally more CPU over time, not exclusive access. |
| Kubernetes controls CPU scheduling. | Kubernetes configures policies; Linux CFS performs scheduling. |

---

# Recommended Reading

## Official Documentation

- CFS Design: https://docs.kernel.org/scheduler/sched-design-CFS.html
- Scheduler Documentation: https://docs.kernel.org/scheduler/

## Kernel Source

Relevant files:

```text
kernel/sched/fair.c
kernel/sched/core.c
include/linux/sched/prio.h
```

Pay particular attention to:

- `calc_delta_fair()`
- `update_curr()`
- `enqueue_entity()`
- `pick_next_task_fair()`

These functions form the heart of CFS scheduling.

---

# Self-Assessment

1. What problem does `vruntime` solve?
2. Why can't CFS rely only on actual runtime?
3. How do nice values influence scheduling?
4. What is the purpose of scheduler weights?
5. Explain target latency.
6. Why is minimum granularity necessary?
7. How do cgroups influence CFS?
8. Why is understanding CFS important for Kubernetes and AI infrastructure?

---

# Chapter Progress

You now understand how CFS converts scheduling policy into measurable fairness using `vruntime`, scheduler weights, and target latency. In the next section, we will examine **context switching**, including CPU registers, kernel stacks, Translation Lookaside Buffers (TLBs), cache effects, and why excessive context switching can become a major performance bottleneck in production systems.
