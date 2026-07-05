---
title: "Chapter 4.5 — Context Switching"
volume: "Volume I — Enterprise Linux Systems Engineering"
chapter: 4
section: 4.5
difficulty: Advanced
estimated_time: "14–18 Hours"
prerequisites:
  - CPU Architecture
  - Linux Kernel
  - Process Scheduling
  - CFS Scheduler
---

# Chapter 4.5 — Context Switching

> *"A CPU can execute only one instruction stream per logical processor at any instant. Every time Linux changes from one task to another, it performs a carefully orchestrated operation called a context switch. This operation is fundamental to multitasking—but it is not free."*

---

# Learning Objectives

After completing this chapter, you should be able to:

- Explain what a context switch is.
- Understand what constitutes a process context.
- Describe the kernel's context switching sequence.
- Distinguish voluntary and involuntary context switches.
- Understand cache and TLB effects.
- Diagnose excessive context switching.
- Optimize workloads to reduce unnecessary switches.
- Relate context switching to containers, Kubernetes, and AI infrastructure.

---

# Introduction

Imagine reading four books simultaneously.

You read one page from Book A.

Then someone asks you to read Book B.

Before switching books, you place a bookmark in Book A so you can return later.

The bookmark represents the **context**.

The act of moving between books represents the **context switch**.

Linux performs this operation millions of times every second.

---

# What Is Context?

A **context** is the complete execution state of a task at a particular instant.

Without saving this information, Linux would have no way to resume execution correctly.

A process context includes:

- CPU registers
- Program Counter (Instruction Pointer)
- Stack Pointer
- Processor flags
- Scheduling metadata
- Kernel stack
- Memory mappings
- Floating-point/SIMD state
- Security context

Think of it as a complete snapshot of everything the CPU needs to continue executing the task.

---

# The Fundamental Problem

Suppose a CPU is executing:

```text
Process A
```

Suddenly:

- its time slice expires,
- a higher-priority task becomes runnable,
- or it blocks waiting for I/O.

Linux must switch to:

```text
Process B
```

But Process A is only partially complete.

Its current state must be preserved before another task can execute.

---

# High-Level Context Switch Sequence

```text
Running Task
      │
      ▼
Save CPU Registers
      │
      ▼
Save Program Counter
      │
      ▼
Save Stack Pointer
      │
      ▼
Select Next Task
      │
      ▼
Load Next Task Context
      │
      ▼
Resume Execution
```

To the user, this happens so quickly that it appears tasks are running simultaneously.

---

# What Happens During a Context Switch?

A simplified sequence:

1. A scheduling event occurs.
2. Control enters the kernel.
3. The current task's CPU state is saved.
4. Scheduler selects another runnable task.
5. The new task's CPU state is restored.
6. CPU resumes execution of the selected task.

Although conceptually simple, each step involves significant low-level work.

---

# CPU Registers

Registers are the fastest storage available to the processor.

Examples include:

- General-purpose registers
- Stack Pointer (`RSP`)
- Base Pointer (`RBP`)
- Instruction Pointer (`RIP`)
- Flags Register (`RFLAGS`)

Before switching tasks, Linux saves these registers into the task's kernel data structures.

Later, when the task is scheduled again, the registers are restored exactly as they were.

From the process's perspective, execution simply continues.

---

# Program Counter (Instruction Pointer)

The Program Counter identifies the next instruction to execute.

Example:

```text
Instruction 1
Instruction 2
Instruction 3
Instruction 4
Instruction 5
```

If the task is interrupted after Instruction 3, Linux stores the current instruction pointer.

When the task resumes, execution continues at Instruction 4—not from the beginning.

---

# The Stack

Every process owns one or more stacks.

A stack stores:

- Function calls
- Local variables
- Return addresses
- Saved registers

Switching to another process requires changing to its stack.

Without restoring the correct stack pointer, function execution would immediately fail.

---

# Kernel Stack vs User Stack

Each process typically has:

```text
User Space Stack

↓

System Call

↓

Kernel Stack
```

The kernel stack is used while executing kernel code on behalf of the process.

During a context switch, Linux changes the active kernel stack as it changes the current task.

---

# Scheduling Metadata

The scheduler also updates bookkeeping information, including:

- Execution statistics
- `vruntime`
- CPU accounting
- Scheduling class
- Runtime quotas
- Load balancing information

These values influence future scheduling decisions.

---

# Why Context Switching Is Expensive

Although modern CPUs are extremely fast, context switching consumes valuable work.

The CPU is not executing application code while switching tasks.

Instead, it is performing operating system housekeeping.

Frequent switching reduces overall throughput.

---

# Types of Context Switches

## Voluntary Context Switch

A process willingly yields the CPU.

Common reasons:

- Waiting for disk I/O
- Waiting for network packets
- Sleeping
- Waiting on a mutex
- Blocking on a semaphore

Example:

```bash
sleep 10
```

The process voluntarily gives up the processor.

---

## Involuntary Context Switch

The scheduler forces the task to stop.

Typical causes:

- Time slice expires
- Higher-priority task becomes runnable
- Real-time process preempts execution

The process has no choice.

Linux interrupts it.

---

# Observing Context Switches

Linux tracks both types.

View statistics:

```bash
pidstat -w
```

Install if needed:

```bash
sudo apt install sysstat
```

Example output:

```text
cswch/s   nvcswch/s

120       35
```

Where:

- `cswch/s` = voluntary switches
- `nvcswch/s` = involuntary switches

---

# Cache Effects

Modern CPUs contain multiple cache levels:

```text
CPU

↓

L1 Cache

↓

L2 Cache

↓

L3 Cache

↓

Main Memory
```

Caches dramatically improve performance.

However, switching to another task often means:

- different instructions,
- different memory,
- different data.

The CPU cache becomes less useful.

This phenomenon is called **cache pollution**.

---

# Translation Lookaside Buffer (TLB)

Virtual memory requires address translation.

To accelerate this process, CPUs maintain a cache called the **Translation Lookaside Buffer (TLB).**

Each process has different virtual memory mappings.

Switching processes may invalidate part of the TLB, forcing expensive memory translations.

Excessive context switching therefore increases memory overhead in addition to CPU overhead.

---

# Floating-Point and SIMD State

Many workloads use:

- AVX
- AVX2
- AVX-512
- SSE
- NEON (ARM)

Their registers are much larger than general-purpose registers.

Saving and restoring these states further increases context switch cost.

This is particularly relevant for:

- scientific computing,
- machine learning,
- multimedia,
- cryptography.

---

# Enterprise Perspective

Consider a Kubernetes worker node hosting:

- PostgreSQL
- Prometheus
- Fluent Bit
- NGINX
- Redis
- AI inference service
- Service mesh sidecars

If thousands of runnable threads compete for CPU time:

- context switches increase,
- cache efficiency decreases,
- latency rises,
- throughput drops.

Performance engineers monitor context switching as closely as CPU utilization.

---

# AI Infrastructure Connection

Large Language Model serving platforms typically involve:

- request handlers,
- batching logic,
- tokenizer threads,
- GPU dispatch,
- metrics,
- logging.

If these components constantly preempt one another, GPU utilization may fall because CPUs cannot feed work quickly enough.

In many AI systems, reducing unnecessary CPU context switches improves end-to-end inference latency more effectively than adding GPU capacity.

---

# Hands-On Lab 4.5

## Objective

Measure context switching under different workloads.

### Step 1 — Observe System Statistics

```bash
vmstat 1
```

Pay attention to:

```text
cs
```

This column reports context switches per second.

---

### Step 2 — Generate CPU Load

Open four terminals:

```bash
yes > /dev/null
```

Observe:

```bash
vmstat 1
```

How does `cs` change?

---

### Step 3 — Monitor Per-Process Switching

```bash
pidstat -w 1
```

Identify which processes generate the most voluntary and involuntary switches.

---

### Step 4 — Stop the Workload

```bash
pkill yes
```

Observe the reduction in context switching activity.

---

# Production Troubleshooting

Scenario:

A Kubernetes node shows:

- CPU utilization: 55%
- High latency
- Low throughput

Initial investigation:

```bash
vmstat 1
pidstat -w
top
perf stat
```

If context switches are unusually high, the problem may be:

- excessive thread creation,
- lock contention,
- poor CPU affinity,
- oversubscription,
- noisy neighbors.

Adding more CPUs may not solve the problem.

Understanding scheduler behavior often reveals the real bottleneck.

---

# Common Misconceptions

| Misconception | Reality |
|---------------|---------|
| Context switches are free. | They consume CPU time and disrupt caches. |
| High CPU utilization always means high switching. | Utilization and switching are related but independent metrics. |
| More threads always improve performance. | Too many runnable threads can increase switching overhead. |
| GPUs eliminate CPU scheduling concerns. | GPU workloads still rely heavily on CPU orchestration. |

---

# Recommended Reading

## Official Documentation

- Linux Scheduler Documentation  
  https://docs.kernel.org/scheduler/

- Performance Analysis with `perf`  
  https://perf.wiki.kernel.org/

## Books

- *The Linux Programming Interface* — Michael Kerrisk
- *Linux Kernel Development* — Robert Love
- *Systems Performance* — Brendan Gregg

## Kernel Source

Key files:

```text
kernel/sched/core.c
kernel/sched/fair.c
kernel/sched/sched.h
```

---

# Self-Assessment

1. What information constitutes a process context?
2. Why must Linux save CPU registers during a context switch?
3. Distinguish voluntary and involuntary context switches.
4. How do context switches affect CPU caches?
5. What is the role of the TLB?
6. Why can excessive context switching reduce throughput?
7. How would you measure context switching on a production Linux server?
8. Why is minimizing unnecessary context switches important for AI inference platforms?

---

# Chapter Progress

You now understand the mechanics and cost of context switching. In the next chapter, we will explore **CPU Affinity and Processor Topology**, including NUMA awareness, CPU pinning, interrupt affinity, and workload placement—critical techniques for optimizing databases, Kubernetes nodes, and enterprise AI infrastructure.
