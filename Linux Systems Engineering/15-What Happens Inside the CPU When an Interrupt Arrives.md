# What Happens Inside the CPU When an Interrupt Arrives?

We now know how an interrupt gets from a hardware device to a CPU and how the CPU uses an **interrupt vector** and the **Interrupt Descriptor Table (IDT)** to determine where interrupt handling should begin.

The next step is to slow the entire process down.

Imagine the CPU is executing a normal program:

```text
Instruction 1
Instruction 2
Instruction 3
Instruction 4
Instruction 5
```

Then, suddenly, an interrupt arrives while the CPU is executing Instruction 4.

What exactly happens?

Does the CPU simply jump to the interrupt handler?

Not quite.

Before the processor can safely transfer control to the handler, it has to preserve enough information about what it was doing so that it can eventually return to the interrupted execution.

This chapter follows that transition.

---

# 1. The CPU Is Always Executing Something

At any given moment, a CPU core has an execution context.

For a normal program, that context includes things such as:

```text
Instruction Pointer
CPU Flags
Registers
Stack
Privilege Level
```

On x86-64, the instruction pointer is called:

```text
RIP
```

The flags register is:

```text
RFLAGS
```

And the stack is accessed through:

```text
RSP
```

You don't need to memorize these yet.

The important thing is that the CPU needs to know:

> **Where am I?**

> **What state am I currently in?**

> **Where should I return when this event is finished?**

---

# 2. The Instruction Pointer

Let's start with `RIP`.

Suppose the CPU is executing:

```text
0x400500
```

The instruction pointer tells the CPU where the current execution stream is located.

Conceptually:

```text
RIP
 │
 ▼
0x400500

Instruction
```

Then:

```text
RIP
 │
 ▼
0x400503

Next instruction
```

The CPU continuously updates its execution position as instructions execute.

---

# 3. An Interrupt Arrives

Now imagine this:

```text
Application
    │
    ▼
Instruction A
    │
    ▼
Instruction B
    │
    ▼
Instruction C
```

While the CPU is executing Instruction C:

```text
             NETWORK CARD
                  │
                  │ Interrupt
                  ▼
                 CPU
```

The CPU cannot simply forget about Instruction C.

It needs a way to eventually return to the interrupted execution.

So interrupt entry involves preserving processor state.

---

# 4. The CPU Saves Return Information

For an interrupt or exception, x86-64 hardware saves important state on the current stack or an appropriate interrupt stack.

Among the architectural state involved are:

```text
RIP
CS
RFLAGS
```

And if the interrupt causes a privilege-level change, additional stack information is saved.

Conceptually:

```text
┌──────────────────┐
│ Previous SS      │
│ Previous RSP     │
│ RFLAGS           │
│ CS               │
│ RIP              │
└──────────────────┘
```

The exact frame depends on the type of event.

This saved information forms part of what allows the CPU to return later.

---

# 5. Why Save RIP?

Imagine you're reading a book.

You're on:

```text
Page 147
```

Someone interrupts you.

If you don't record the page number, you have no idea where to continue.

`RIP` is roughly equivalent to remembering where you were in the instruction stream.

Conceptually:

```text
Before interrupt:

RIP → Instruction 500
```

Then:

```text
Interrupt
   ↓
Save RIP
   ↓
Run handler
   ↓
Restore execution state
   ↓
Continue
```

---

# 6. What Is RFLAGS?

`RFLAGS` contains various status and control bits describing the processor's current state.

For example, it contains flags associated with things such as:

* arithmetic results
* interrupt enable state
* processor control state

One particularly important bit for interrupts is:

```text
IF
```

The **Interrupt Flag**.

It controls whether maskable external interrupts are enabled for the current execution context.

This becomes important because the CPU has to carefully control interrupt behavior while entering and executing handlers.

---

# 7. What Is CS?

`CS` stands for:

**Code Segment**

On modern x86-64 systems, segmentation is much less prominent than it was in older x86 systems, but the code-segment information remains part of the processor's architectural state.

It also participates in determining the privilege level associated with the current execution.

This becomes extremely important when discussing:

```text
User Mode
```

and

```text
Kernel Mode
```

---

# 8. Privilege Levels

Modern x86 processors provide privilege mechanisms that allow operating systems to separate ordinary applications from privileged kernel code.

The most important levels for normal Linux operation are:

```text
Ring 3
```

for user-space programs, and:

```text
Ring 0
```

for the kernel.

Think of them as two security zones.

```text
┌─────────────────────────────┐
│          USER SPACE         │
│                             │
│ Chrome                      │
│ Python                      │
│ Node.js                     │
│ Your programs               │
│                             │
│         Ring 3              │
└──────────────┬──────────────┘
               │
               │ controlled transition
               ▼
┌─────────────────────────────┐
│         KERNEL SPACE        │
│                             │
│ Linux kernel                │
│ Device drivers              │
│ Scheduler                   │
│ Memory management           │
│ Networking                  │
│                             │
│         Ring 0              │
└─────────────────────────────┘
```

The distinction exists because the kernel has access to operations and resources that ordinary applications must not control directly.

---

# 9. Why Can't User Programs Just Become the Kernel?

Imagine a Python program being allowed to directly modify:

```text
CPU control registers
Page tables
Interrupt configuration
Physical memory
Other processes
```

One buggy program could destroy the entire operating system.

One malicious program could potentially take control of the machine.

The CPU therefore provides hardware mechanisms that help enforce privilege boundaries.

---

# 10. Interrupt From Kernel Mode

Suppose the CPU is already executing kernel code.

Then an interrupt arrives.

Conceptually:

```text
Kernel
  │
  │ executing
  ▼
Instruction
  │
  │ interrupt
  ▼
Interrupt Entry
  │
  ▼
Handler
```

The CPU does not need to transition from Ring 3 to Ring 0 because it is already in the privileged level.

---

# 11. Interrupt From User Mode

Now suppose a network interrupt arrives while the CPU is executing an application.

The situation is different.

The CPU is currently in:

```text
Ring 3
```

But the interrupt handler needs to execute privileged kernel code.

The processor therefore performs an architectural transition into the appropriate privileged context.

Conceptually:

```text
User Mode
   │
   │ Interrupt
   ▼
CPU interrupt entry
   │
   ▼
Kernel context
   │
   ▼
Interrupt handler
```

This is one of the fundamental boundaries in operating systems.

---

# 12. The Stack Becomes Critical

The stack is an area of memory used for temporary execution state.

You can visualize it like a pile of plates:

```text
Top
 ↓
┌───────────────┐
│ Data          │
├───────────────┤
│ Return info   │
├───────────────┤
│ Local data    │
├───────────────┤
│ Older data    │
└───────────────┘
```

The CPU uses a stack pointer:

```text
RSP
```

to identify the current stack location.

---

# 13. Why Does the Kernel Need Its Own Stack?

Imagine an application has its own stack:

```text
User Stack
```

Then an interrupt occurs.

The kernel cannot simply trust the application to provide a safe environment for kernel execution.

The operating system therefore maintains kernel stacks associated with execution contexts.

Conceptually:

```text
User Program
     │
     │
User Stack
     │
     │ interrupt
     ▼
Kernel Entry
     │
     ▼
Kernel Stack
```

This gives kernel execution a controlled environment.

---

# 14. Kernel Stack Switching

When an interrupt or exception arrives from a less privileged context, the processor and kernel entry mechanisms need to establish an appropriate kernel execution stack.

On x86-64 Linux, this can involve mechanisms such as:

* TSS
* `RSP0`
* Interrupt Stack Table (IST)

These are deeper architectural mechanisms we'll examine separately.

For now, understand the fundamental idea:

> **The kernel needs a safe stack for handling privileged events.**

---

# 15. What Is the TSS?

TSS means:

**Task State Segment**

Despite its historical name, modern x86-64 Linux does not use it primarily for hardware task switching.

Instead, it provides information the processor can use for certain privilege transitions and stack handling.

One important piece is the kernel stack pointer used when entering from a less privileged level.

Conceptually:

```text
TSS
 │
 └── Kernel Stack Information
```

The CPU can use this information when an interrupt or exception requires a transition to a more privileged level.

---

# 16. What Is the Interrupt Stack Table?

The **Interrupt Stack Table**, or IST, provides another mechanism for giving particular exceptions or interrupts dedicated stacks.

This is useful for events where the current stack may itself be compromised or unusable.

Conceptually:

```text
Normal execution
      │
      ▼
Normal stack

Special exception
      │
      ▼
IST stack
```

This provides additional robustness for critical exception handling.

---

# 17. The CPU Finds the IDT Entry

Remember our previous chapter.

The interrupt arrives with a vector.

Suppose:

```text
Vector = 32
```

The CPU uses:

```text
IDTR
```

to locate the IDT.

Then:

```text
IDT[32]
```

provides the descriptor information necessary to enter the appropriate handler.

Conceptually:

```text
Interrupt Vector 32
       │
       ▼
      IDTR
       │
       ▼
      IDT
       │
       ▼
   Entry 32
       │
       ▼
Handler Entry Point
```

---

# 18. The Interrupt Gate

An IDT entry commonly used for interrupts is an:

**Interrupt Gate**

The descriptor tells the CPU things such as:

* where the handler is
* which code segment to use
* privilege information
* whether the descriptor is present
* how the entry should be treated

The CPU uses this information to perform the transition.

---

# 19. The CPU Transfers Control

After the necessary architectural state has been saved and the appropriate entry has been located, the processor transfers execution to the interrupt entry point.

The flow becomes:

```text
Application
    │
    ▼
Normal instruction execution
    │
    │ interrupt
    ▼
Save architectural state
    │
    ▼
Locate IDT entry
    │
    ▼
Establish kernel context if required
    │
    ▼
Transfer control
    │
    ▼
Kernel interrupt entry
```

We are now inside the kernel's interrupt entry path.

---

# 20. But We're Not Finished

This is a subtle but important point.

The CPU does **not** simply jump directly into a complete Linux driver function.

There is architecture-specific entry code involved.

The processor enters code associated with the interrupt vector.

That entry code performs the low-level work necessary to establish the kernel's expected execution environment.

Only after this preparation does Linux proceed into the higher-level interrupt handling machinery.

Conceptually:

```text
Hardware
    ↓
APIC
    ↓
CPU
    ↓
IDT
    ↓
Architecture Entry Code
    ↓
Linux Interrupt Handling
    ↓
Driver / Kernel Subsystem
```

---

# 21. Why Is There So Much Machinery?

Because an interrupt can arrive at almost any moment.

The CPU could currently be:

```text
Running a user program
```

or:

```text
Running kernel code
```

or:

```text
Handling another interrupt
```

or:

```text
Executing code on a particular CPU
```

The operating system needs a reliable mechanism that works regardless of what the CPU was doing before the interrupt.

That is why interrupt entry is carefully designed.

---

# 22. Nested Interrupts

Imagine this:

```text
Interrupt A
    ↓
Handler A
    ↓
Interrupt B
```

An interrupt can potentially occur while another interrupt is being handled, depending on interrupt masking and the specific circumstances.

This is called **nested interrupt handling**.

The CPU and kernel need to manage this carefully.

Otherwise, interrupt handling could become:

```text
Interrupt
   ↓
Interrupt
   ↓
Interrupt
   ↓
Interrupt
   ↓
...
```

and the system could spend all of its time responding to interrupts.

This is another reason interrupt control mechanisms matter.

---

# 23. Interrupts Are Time-Sensitive

Imagine a network adapter receives packets extremely quickly.

Suppose it generates:

```text
100,000 interrupts/second
```

If every interrupt caused the CPU to spend a large amount of time doing work, the CPU would spend most of its time handling interrupts.

Normal applications would suffer.

This leads to a fundamental Linux design principle:

> **Do the minimum necessary work in the immediate interrupt context and defer work that can safely happen later.**

This is where Linux's distinction between hard IRQ handling and deferred processing becomes important.

We'll cover that in the next chapter.

---

# 24. From CPU Entry to Linux

We can now build a much more detailed picture.

```text
                 HARDWARE
                    │
                    ▼
             Device generates IRQ
                    │
                    ▼
                 I/O APIC
                    │
                    ▼
             Interrupt Routing
                    │
                    ▼
                Local APIC
                    │
                    ▼
                   CPU
                    │
                    ▼
            Interrupt Vector
                    │
                    ▼
                   IDT
                    │
                    ▼
          Save CPU architectural state
                    │
                    ▼
       Privilege transition if necessary
                    │
                    ▼
             Kernel stack/context
                    │
                    ▼
          Architecture entry code
                    │
                    ▼
          Linux interrupt machinery
                    │
                    ▼
            Interrupt handler
                    │
                    ▼
          Deferred processing
                    │
                    ▼
              Return from IRQ
                    │
                    ▼
          Resume interrupted work
```

Now we're looking at an actual operating-system execution path rather than simply discussing interrupts as an abstract concept.

---

# 25. What Happens When the Handler Finishes?

Eventually the interrupt handling path reaches the point where the CPU can return to the interrupted execution.

The processor uses the saved state to reconstruct the previous execution context.

On x86, this involves a special return-from-interrupt instruction:

```text
IRET
```

or, in 64-bit mode:

```text
IRETQ
```

Conceptually:

```text
Interrupt Handler
       │
       ▼
Restore execution context
       │
       ▼
IRETQ
       │
       ▼
Previous execution
```

The CPU restores the relevant state and resumes execution.

---

# 26. The Entire Event

Let's now follow one complete example.

A network card receives a packet.

### Stage 1: Device

```text
Network Card
     │
     ▼
"I received data."
```

### Stage 2: Interrupt Controller

```text
Network Card
     │
     ▼
I/O APIC
     │
     ▼
Route interrupt
```

### Stage 3: Local APIC

```text
I/O APIC
     │
     ▼
Local APIC
     │
     ▼
CPU
```

### Stage 4: CPU

CPU receives an interrupt vector.

```text
CPU
 │
 ▼
Vector
```

### Stage 5: IDT

```text
Vector
 │
 ▼
IDT entry
 │
 ▼
Interrupt entry point
```

### Stage 6: CPU State

The processor establishes the architectural state needed to enter the handler.

```text
Save relevant state
       │
       ▼
Kernel context
```

### Stage 7: Linux

Linux's architecture-specific entry machinery begins processing the interrupt.

```text
CPU Entry
   ↓
Linux interrupt machinery
```

### Stage 8: Driver

The appropriate device driver handles the event.

```text
Linux
   ↓
Network Driver
   ↓
Packet processing
```

### Stage 9: Return

Eventually:

```text
Handler complete
      ↓
IRETQ
      ↓
Resume interrupted execution
```

That entire sequence may happen extremely quickly.

---

# 27. The Important Mental Model

Don't think:

```text
Device
 ↓
CPU
 ↓
Driver
```

That's too simplistic.

Think:

```text
Device
 ↓
IRQ
 ↓
Interrupt Controller
 ↓
Local APIC
 ↓
CPU
 ↓
Interrupt Vector
 ↓
IDT
 ↓
CPU State Preservation
 ↓
Privilege / Stack Handling
 ↓
Architecture Entry Code
 ↓
Linux Interrupt Handling
 ↓
Driver
 ↓
Return
```

That is much closer to the real architecture.

---

# 28. Why This Matters for Infrastructure Engineering

At first glance, this may seem like low-level trivia.

It isn't.

Interrupt behavior affects real infrastructure performance.

Consider a server with:

```text
128 CPUs
100 GbE NIC
NVMe storage
GPU accelerators
NUMA memory
```

Every device can generate events that require CPU attention.

If interrupts are poorly distributed:

```text
CPU 0
████████████████████████

CPU 1
██

CPU 2
█

CPU 3
██
```

You can have an overloaded CPU while others are underutilized.

That can affect:

* network throughput
* packet processing
* storage latency
* application latency
* CPU utilization
* NUMA locality
* AI inference performance

This is why interrupt architecture eventually connects directly to infrastructure performance engineering.

---

# 29. Lab Experiments

You can observe parts of this architecture from your Ubuntu VM.

Start with:

```bash
cat /proc/interrupts
```

Then:

```bash
ls /proc/irq/
```

Pick an IRQ:

```bash
ls /proc/irq/1/
```

Look for:

```text
smp_affinity
smp_affinity_list
effective_affinity
effective_affinity_list
```

These show how Linux controls where interrupts can be handled.

---

## Inspect CPU Topology

Run:

```bash
lscpu
```

Then:

```bash
lscpu -e
```

This lets you see the logical CPUs exposed by your VM.

---

## Watch Interrupts Live

Run:

```bash
watch -n 1 cat /proc/interrupts
```

Then generate network activity:

```bash
ping 8.8.8.8
```

Watch which interrupt counters increase.

---

## Find APIC Information

```bash
dmesg | grep -Ei "apic|x2apic|ioapic"
```

This gives you a glimpse of how Linux detected and configured the interrupt architecture during boot.

---

# 30. Questions to Test Yourself

Before moving on, make sure you can answer these:

1. What is `RIP`?

2. Why does the CPU need to preserve execution state when an interrupt arrives?

3. What is `RFLAGS`?

4. What is the Interrupt Flag?

5. What is the difference between Ring 0 and Ring 3?

6. Why does kernel execution require a controlled stack?

7. What is the purpose of the TSS?

8. What is the Interrupt Stack Table?

9. What does the IDTR contain?

10. What is an interrupt gate?

11. What is `IRETQ`?

12. Why can't interrupt handlers perform unlimited amounts of work?

13. What happens if an interrupt arrives while the CPU is already handling another interrupt?

14. What is the difference between the CPU's hardware interrupt entry and Linux's higher-level interrupt handling?

If you can explain those in your own words, you're ready for the next layer.

---

# Engineering Insight

The interesting thing about interrupts is how much work is hidden behind what looks like a tiny event.

From the outside, it looks like:

```text
"Network packet arrived."
```

Inside the machine, a much longer chain occurs:

```text
Device
  ↓
Interrupt Request
  ↓
Interrupt Controller
  ↓
Local APIC
  ↓
CPU
  ↓
Interrupt Vector
  ↓
IDT
  ↓
CPU State
  ↓
Privilege Transition
  ↓
Kernel Stack
  ↓
Architecture Entry
  ↓
Linux
  ↓
Driver
```

And eventually:

```text
Driver
  ↓
Interrupt completion
  ↓
IRETQ
  ↓
Previous execution
```

This is a good example of why systems engineering requires thinking across layers.

The network card doesn't know about the Linux driver.

The Linux driver doesn't decide how the CPU performs the architectural interrupt entry.

The CPU doesn't understand what a network packet means.

Each layer performs its own responsibility and passes control to the next layer.

That separation is what allows a modern operating system to coordinate extremely complicated hardware without every component needing to understand the entire system.

---

## Looking Ahead

We now understand:

**how the device generates an interrupt,**

**how the interrupt controller routes it,**

**how the CPU identifies it,**

**how the IDT provides the entry point,**

and

**how the CPU transitions into interrupt handling.**

The next question is:

> **Once Linux is inside the interrupt handler, what does the kernel actually do with the interrupt?**

That takes us into:

# **Hard IRQs, SoftIRQs, Deferred Work, and Threaded Interrupts**

This is where the hardware interrupt architecture we've been studying finally connects directly to **Linux kernel behavior and performance**.
