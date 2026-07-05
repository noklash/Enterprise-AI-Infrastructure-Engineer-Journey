---
title: "Chapter 4.6 — CPU Affinity, Processor Topology, and NUMA"
volume: "Volume I — Enterprise Linux Systems Engineering"
chapter: 4
section: 4.6
difficulty: Advanced
estimated_time: "18–24 Hours"
prerequisites:
  - Computer Architecture
  - CPU Scheduling
  - Context Switching
---

# Chapter 4.6 — CPU Affinity, Processor Topology, and NUMA

> *"Not all CPU cores are equal, and not all memory is equally close. Modern servers are no longer simple collections of processors—they are distributed systems on a motherboard. Understanding processor topology is essential for building high-performance enterprise platforms."*

---

# Learning Objectives

After completing this chapter, you should be able to:

- Explain CPU affinity and why it matters.
- Understand logical CPUs, physical cores, sockets, and NUMA nodes.
- Use Linux tools to inspect processor topology.
- Pin workloads to specific CPUs.
- Understand interrupt affinity.
- Explain NUMA architecture.
- Optimize workload placement for Kubernetes and AI infrastructure.
- Diagnose CPU migration and locality issues.

---

# Introduction

Imagine a team of engineers working in a large office.

Each engineer has:

- a desk,
- nearby documents,
- local tools,
- and colleagues.

Now imagine moving every engineer to a different desk every five minutes.

Although everyone can still work, productivity declines because each engineer must constantly reorient themselves.

Linux processes experience a similar phenomenon.

Moving a running task from one CPU to another may appear harmless, but it can invalidate caches, reduce locality, and increase latency.

This is where **CPU affinity** becomes important.

---

# What Is CPU Affinity?

CPU affinity is the relationship between a task and the CPUs on which it is allowed or encouraged to execute.

Instead of allowing a task to run on any processor, Linux can restrict or prefer specific CPUs.

Conceptually:

```text
Without Affinity

Task A

↓

CPU0

↓

CPU2

↓

CPU5

↓

CPU1

↓

CPU7
```

The task migrates frequently.

---

With affinity:

```text
Task A

↓

CPU3

↓

CPU3

↓

CPU3

↓

CPU3
```

The task remains on the same processor whenever possible.

---

# Why CPU Affinity Matters

Modern CPUs contain multiple cache levels:

```text
Registers

↓

L1 Cache

↓

L2 Cache

↓

L3 Cache

↓

Main Memory
```

When a task remains on the same CPU:

- cache contents remain useful,
- instruction locality improves,
- memory accesses become faster.

Migrating the task to another CPU often forces the processor to rebuild these caches.

---

# CPU Migration

CPU migration occurs when Linux moves a runnable task from one logical processor to another.

Migration may happen because:

- load balancing,
- CPU hot-plug events,
- affinity changes,
- scheduler optimization,
- processor idle states.

Although migration improves global CPU utilization, it may reduce local performance.

---

# Processor Topology

To understand affinity, we first need to understand processor topology.

A modern server is organized hierarchically:

```text
Server

│

├── Socket

│     ├── Core

│     │     ├── Logical CPU

│     │     └── Logical CPU (Hyper-Thread)

│     └── Core

│

└── Socket

      ├── Core

      └── Core
```

Each level has performance implications.

---

# Socket

A socket is a physical processor package installed on the motherboard.

Enterprise servers commonly have:

- 1 socket
- 2 sockets
- 4 sockets

Each socket has its own memory controllers and cache hierarchy.

---

# Core

Each processor contains multiple physical cores.

Example:

```text
Socket

↓

16 Physical Cores
```

Each core can independently execute instructions.

---

# Hyper-Threading / SMT

Many processors expose multiple logical CPUs per physical core.

Example:

```text
1 Physical Core

↓

Logical CPU0

Logical CPU1
```

Intel calls this **Hyper-Threading**.

AMD generally refers to the same concept as **Simultaneous Multithreading (SMT)**.

These logical CPUs share many hardware resources, so they do not provide the same performance as additional physical cores.

---

# Inspecting CPU Topology

Display processor information:

```bash
lscpu
```

Example output:

```text
Architecture:          x86_64
CPU(s):                32
Core(s) per socket:    8
Socket(s):             2
Thread(s) per core:    2
NUMA node(s):          2
```

This tells you:

- 2 physical CPUs,
- 8 physical cores each,
- Hyper-Threading enabled,
- 32 logical processors.

---

# Visualizing Topology

A simplified example:

```text
Socket 0

├── Core 0

│      ├── CPU0

│      └── CPU1

├── Core 1

│      ├── CPU2

│      └── CPU3

...

Socket 1

├── Core 8

│      ├── CPU16

│      └── CPU17
```

This structure influences scheduling decisions and memory access times.

---

# NUMA (Non-Uniform Memory Access)

Early computers used **Uniform Memory Access (UMA)**.

All CPUs accessed memory with roughly the same latency.

As processors gained more cores, UMA became a bottleneck.

NUMA was introduced to improve scalability.

In a NUMA system:

- each CPU socket has local memory,
- accessing local memory is faster,
- accessing another socket's memory incurs additional latency.

---

# NUMA Architecture

```text
          +-----------------------+
          |      Socket 0         |
          |  CPU Cores            |
          +-----------+-----------+
                      |
                 Local Memory
                      |
        ==============================
                      |
          Remote Memory Access
                      |
          +-----------+-----------+
          |      Socket 1         |
          |  CPU Cores            |
          +-----------------------+
```

Accessing **local memory** is significantly faster than crossing the interconnect to remote memory.

---

# NUMA Locality

The scheduler attempts to keep:

- CPU execution,
- memory allocation,
- and cache usage

within the same NUMA node whenever possible.

This is called **NUMA locality**.

Good locality improves:

- latency,
- throughput,
- cache efficiency,
- memory bandwidth.

---

# CPU Affinity in Practice

Linux allows administrators to bind processes to specific CPUs.

View affinity:

```bash
taskset -pc <PID>
```

Example:

```text
pid 1234's current affinity list: 0-7
```

Restrict a process:

```bash
taskset -cp 2,3 <PID>
```

Now the process may execute only on CPUs 2 and 3.

---

# Launching a Program with Affinity

Example:

```bash
taskset -c 4 python inference.py
```

The process begins execution pinned to CPU 4.

This is commonly used for:

- databases,
- packet processing,
- AI inference,
- benchmark testing.

---

# Interrupt Affinity

Not only processes consume CPU time.

Hardware interrupts also execute on processors.

Examples:

- Network cards
- NVMe SSDs
- GPUs
- USB controllers

Linux allows interrupt handling to be assigned to specific CPUs.

This reduces contention between application threads and interrupt processing.

---

# Observing CPU Migration

Linux exposes scheduler statistics through:

```bash
pidstat -t
```

and

```bash
perf sched
```

These tools help identify excessive migration.

High migration rates often correlate with reduced cache efficiency.

---

# Enterprise Perspective

Consider a PostgreSQL database.

The database repeatedly accesses:

- buffer cache,
- indexes,
- query execution plans.

Keeping worker threads on the same CPU improves cache reuse.

Migrating them unnecessarily increases latency.

---

# Kubernetes Connection

Kubernetes exposes CPU management features such as:

- CPU Manager
- Guaranteed QoS
- Static CPU Policy

These features rely on Linux CPU affinity mechanisms.

For latency-sensitive workloads, Kubernetes can dedicate physical CPUs to specific Pods.

This is essential for:

- financial trading,
- telecom,
- AI inference,
- real-time analytics.

---

# AI Infrastructure Connection

An enterprise inference server may dedicate:

| CPU | Responsibility |
|------|----------------|
| CPU0 | HTTP server |
| CPU1 | Request queue |
| CPU2 | Tokenizer |
| CPU3 | GPU dispatch |
| CPU4 | Metrics |
| CPU5 | Logging |

Keeping these threads on stable CPUs reduces migration, improves cache locality, and lowers tail latency.

Many production AI serving platforms—including Triton Inference Server and vLLM deployments—benefit from careful CPU placement.

---

# Hands-On Lab 4.6

## Objective

Inspect CPU topology and experiment with affinity.

### Step 1 — Display CPU Layout

```bash
lscpu
```

Identify:

- sockets,
- cores,
- threads,
- NUMA nodes.

---

### Step 2 — Launch a CPU-Bound Process

```bash
yes > /dev/null &
```

Record its PID:

```bash
pgrep yes
```

---

### Step 3 — View Current Affinity

```bash
taskset -pc <PID>
```

---

### Step 4 — Restrict the Process

```bash
taskset -cp 2 <PID>
```

Verify the change:

```bash
taskset -pc <PID>
```

---

### Step 5 — Observe Scheduling

Open another terminal:

```bash
htop
```

Press:

```
1
```

Observe that the process remains on the assigned CPU.

---

# Production Troubleshooting

Scenario:

An LLM inference service exhibits inconsistent response times despite low CPU utilization.

Possible causes:

- frequent CPU migration,
- poor NUMA locality,
- interrupt contention,
- shared logical CPUs,
- incorrect Kubernetes CPU policy.

Investigation:

```bash
lscpu
numactl --hardware
taskset -pc <PID>
perf sched record
perf sched latency
```

Understanding processor topology often reveals performance issues that application logs cannot.

---

# Common Misconceptions

| Misconception | Reality |
|---------------|---------|
| More logical CPUs always mean more performance. | Logical CPUs on the same core share execution resources. |
| CPU migration is always beneficial. | It improves balance but can hurt cache locality and latency. |
| NUMA is only relevant for supercomputers. | Most enterprise dual-socket servers are NUMA systems. |
| Kubernetes manages CPU placement independently. | It relies on Linux affinity and scheduler mechanisms. |

---

# Recommended Reading

## Official Documentation

- CPU Affinity (`taskset`): https://man7.org/linux/man-pages/man1/taskset.1.html
- Kernel Scheduler Documentation: https://docs.kernel.org/scheduler/
- NUMA Documentation: https://docs.kernel.org/admin-guide/mm/numa_memory_policy.html

## Books

- *Systems Performance* — Brendan Gregg
- *Linux Kernel Development* — Robert Love
- *The Linux Programming Interface* — Michael Kerrisk

---

# Self-Assessment

1. What is CPU affinity, and why does it improve performance?
2. Distinguish sockets, cores, and logical CPUs.
3. What is NUMA, and why does it exist?
4. Why can CPU migration reduce cache efficiency?
5. How do you inspect processor topology on Linux?
6. How can `taskset` be used to control process execution?
7. Why is NUMA awareness important for enterprise databases?
8. How do Kubernetes CPU Manager and Linux affinity work together?

---

# Chapter Progress

You now understand how processor topology, CPU affinity, and NUMA influence scheduling decisions and application performance. In the next chapter, we will explore **Linux Control Groups (cgroups)**—the resource management framework that powers Docker, Kubernetes, systemd, and virtually every modern container platform. We will examine CPU, memory, I/O, and process controllers, and connect them directly to enterprise AI infrastructure and platform engineering.
