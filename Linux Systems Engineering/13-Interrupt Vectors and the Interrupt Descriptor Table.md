# Interrupt Vectors and the Interrupt Descriptor Table (IDT)

Understanding how hardware reaches the processor is only the first half of interrupt architecture.

We now know that a hardware device can generate an interrupt, that the interrupt can travel through the I/O APIC, and that the interrupt can be delivered to the Local APIC of a particular processor.

But this creates a new question:

> **Once the CPU receives the interrupt, how does it know what code to execute?**

The CPU cannot simply receive an interrupt and somehow "know" whether it came from a keyboard, network adapter, storage controller, or timer.

There needs to be a mechanism that tells the processor:

> "This particular interrupt happened, and this is the code responsible for handling it."

That mechanism involves two closely related concepts:

* **Interrupt vectors**
* **Interrupt Descriptor Table (IDT)**

Together, they provide the CPU with a lookup mechanism for determining where interrupt handling should begin.

---

# 1. The Problem We Need to Solve

Let's start from the previous chapter.

Suppose a network card receives a packet.

The journey might look conceptually like this:

```text
Network Card
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
```

At this point, the CPU knows:

> "An interrupt has arrived."

But that's not enough.

Imagine your phone rings.

You know that someone is calling, but you still need to know **who is calling and what you should do about it**.

The CPU has the same problem.

It needs an identifier.

That identifier is the **interrupt vector**.

---

# 2. What Is an Interrupt Vector?

An interrupt vector is essentially a number associated with an interrupt.

Think of it as an index.

For example:

```text
Interrupt
    │
    ▼
Vector 32
```

The number `32` tells the CPU:

> "Look at entry 32 in the interrupt table."

Another interrupt might arrive with:

```text
Vector 33
```

The CPU then looks at entry 33.

Conceptually:

```text
Vector 32 → Handler A

Vector 33 → Handler B

Vector 34 → Handler C
```

So the vector does not itself contain the interrupt-handling code.

It identifies **which entry the CPU should use**.

---

# 3. Think of the Vector as a Ticket Number

Imagine walking into a government office.

You take a ticket:

```text
Ticket #32
```

The ticket doesn't contain the employee who will serve you.

It simply tells the system:

> "You belong to queue 32."

The system looks up queue 32 and directs you to the correct place.

Interrupt vectors work similarly.

```text
Interrupt
    ↓
Vector number
    ↓
Look up corresponding entry
    ↓
Find handler
    ↓
Execute handler
```

---

# 4. Where Does the CPU Look?

This is where the **Interrupt Descriptor Table** comes in.

The IDT is a data structure maintained in memory.

IDT means:

**Interrupt Descriptor Table**

It contains information about how the processor should respond to different interrupts and exceptions.

Conceptually:

```text
                 CPU
                  │
                  │ Interrupt Vector
                  ▼
        ┌──────────────────────┐
        │ Interrupt Descriptor │
        │ Table (IDT)          │
        │                      │
        │ Entry 0  ────────────┼──→ Handler
        │ Entry 1  ────────────┼──→ Handler
        │ Entry 2  ────────────┼──→ Handler
        │ Entry 3  ────────────┼──→ Handler
        │ ...                  │
        │ Entry 32 ────────────┼──→ Handler
        │ Entry 33 ────────────┼──→ Handler
        │ ...                  │
        └──────────────────────┘
```

The CPU uses the interrupt vector as an index into this table.

---

# 5. The Basic Relationship

The most important relationship to understand is:

```text
Interrupt Vector
       │
       ▼
      IDT
       │
       ▼
Handler Address
       │
       ▼
Interrupt Handler
```

The vector tells the processor **which IDT entry to use**.

The IDT entry tells the processor **where the appropriate handler is located** and provides other information required for the transition.

---

# 6. What Exactly Is an IDT Entry?

An IDT is not simply a list of addresses.

Each entry is a descriptor containing information the CPU needs when transferring control to the handler.

On x86-64 systems, an interrupt gate descriptor contains information including:

* The address of the handler
* The code segment selector
* Gate type
* Descriptor privilege level
* Present bit
* Other architectural fields

You don't need to memorize these fields yet.

The important idea is:

> **An IDT entry tells the CPU how to enter the interrupt handler.**

---

# 7. The CPU Needs to Know Where the IDT Is

There is another interesting question.

If the IDT exists in memory, how does the CPU know where it is?

The processor has a special register called:

**IDTR**

The **Interrupt Descriptor Table Register**.

It contains information that tells the CPU where the IDT is located and how large it is.

Conceptually:

```text
                 CPU
                  │
                  │
                 IDTR
                  │
                  ▼
        ┌─────────────────────┐
        │ Interrupt Descriptor│
        │ Table                │
        └─────────────────────┘
```

So when an interrupt arrives, the CPU doesn't search randomly through memory.

It already knows where the IDT is.

---

# 8. The Complete Lookup Process

Let's put everything together.

Suppose an interrupt arrives with vector:

```text
32
```

The processor effectively performs a process conceptually similar to:

```text
Interrupt arrives
       │
       ▼
CPU receives vector 32
       │
       ▼
CPU consults IDTR
       │
       ▼
Finds location of IDT
       │
       ▼
Uses vector 32 as an index
       │
       ▼
Finds IDT entry 32
       │
       ▼
Reads handler information
       │
       ▼
Transfers execution to handler
```

This is the fundamental mechanism.

---

# 9. Interrupt Vectors Are Not the Same as IRQ Numbers

This distinction is important.

You may encounter:

```text
IRQ
```

and

```text
Interrupt Vector
```

They are related, but they are not the same thing.

An **IRQ** is a hardware interrupt request.

An **interrupt vector** is the number the CPU uses to identify the interrupt entry it should execute.

Conceptually:

```text
Hardware Device
      │
      ▼
     IRQ
      │
      ▼
Interrupt Controller
      │
      ▼
Interrupt Vector
      │
      ▼
     CPU
      │
      ▼
     IDT
      │
      ▼
   Handler
```

The interrupt controller helps translate and route hardware interrupt requests into CPU-level interrupt delivery.

---

# 10. Why Does the CPU Need Vectors?

Imagine a building with 1,000 rooms.

Someone tells you:

> "Someone needs you."

That's not enough information.

You need to know:

> "Which room?"

The vector provides the room number.

```text
Interrupt Vector 32
        ↓
     IDT Entry 32
        ↓
     Handler
```

Without a standardized mechanism like this, the processor wouldn't know where to transfer execution.

---

# 11. Exceptions Also Use the IDT

This is where the concept becomes more interesting.

The IDT isn't exclusively for hardware interrupts.

The CPU itself can generate events that require special handling.

These are called **exceptions**.

For example:

```text
Division by zero
Invalid instruction
Page fault
General protection fault
Debug exception
```

These can also be associated with interrupt vectors.

So the IDT provides a common architectural mechanism for both:

**CPU-generated exceptions**

and

**external interrupts.**

---

# 12. Interrupts vs Exceptions

There are two broad categories worth keeping in mind.

### Hardware Interrupt

Something outside the CPU needs attention.

For example:

```text
Network card
     ↓
Interrupt
     ↓
CPU
```

### Exception

The processor itself encounters a condition requiring special handling.

For example:

```text
Program executes:

10 / 0

     ↓

CPU detects invalid operation

     ↓

Exception
```

Both eventually involve the processor transferring control to a particular handler.

---

# 13. Example: Page Fault

This is one of the most important examples for understanding operating systems.

Imagine a program accesses memory:

```text
address = 0x12345678
```

The CPU translates the virtual address through the memory-management system.

Suppose the page isn't currently mapped into physical memory.

The CPU detects the problem.

It generates a **page fault exception**.

Conceptually:

```text
Program
   │
   ▼
Access virtual memory
   │
   ▼
CPU checks page tables
   │
   ▼
Page unavailable
   │
   ▼
Page Fault Exception
   │
   ▼
Interrupt Vector
   │
   ▼
IDT
   │
   ▼
Linux Page Fault Handler
```

Linux can then determine what happened.

Perhaps the page simply needs to be brought into memory.

Perhaps the access is invalid.

Perhaps the process should receive a segmentation-related signal.

The important point is that the CPU needs a reliable mechanism for transferring control from the currently executing program into kernel code.

The IDT is part of that mechanism.

---

# 14. What Happens to the Current Program?

This is where interrupts become more interesting.

Remember our original example.

The CPU is executing:

```text
Instruction 1
Instruction 2
Instruction 3
Instruction 4
Instruction 5
```

An interrupt arrives while executing instruction 4.

The CPU needs to temporarily transfer control somewhere else.

But it cannot simply forget what the program was doing.

It needs to preserve enough information to return later.

Conceptually:

```text
User Program
     │
     │ executing
     ▼
Instruction 4
     │
     │
     │ INTERRUPT
     ▼
CPU saves execution state
     │
     ▼
Interrupt Handler
     │
     ▼
Handler completes
     │
     ▼
Restore execution state
     │
     ▼
Instruction 5
```

This is one of the most important ideas in interrupt handling.

The CPU temporarily changes its execution path.

---

# 15. What State Does the CPU Save?

On x86-64, interrupt entry involves saving architectural state needed to resume execution.

Among the important pieces are:

* Instruction pointer
* Code segment
* CPU flags
* Stack information when applicable
* Stack segment when applicable

The exact details depend on the type of interrupt and privilege transition.

The important idea is:

> **The CPU needs enough information to know where execution should continue afterward.**

---

# 16. Why Is the Instruction Pointer Important?

The CPU has a register called the:

**Instruction Pointer**

On x86-64, this is commonly referred to as:

**RIP**

It tells the processor where the next instruction comes from.

Imagine:

```text
RIP → Instruction 500
```

An interrupt occurs.

The CPU needs to preserve the relevant execution location.

Otherwise, after the interrupt finishes, it wouldn't know where to return.

Conceptually:

```text
Before interrupt:

RIP
 ↓
Instruction 500


Interrupt

 ↓

Save state

 ↓

Run handler

 ↓

Restore state

 ↓

Continue program
```

---

# 17. The Stack Becomes Important

Interrupt handling relies heavily on the CPU stack and kernel stack.

Think of a stack as a temporary storage area.

```text
Top
 │
 ▼
Return information
CPU state
Other temporary data
```

When the processor enters an interrupt handler, state can be saved so that execution can later return to the interrupted context.

This is one reason understanding the stack is essential for understanding operating systems.

---

# 18. What Happens If the Interrupt Comes From User Space?

This introduces another critical concept:

**Privilege levels.**

Modern operating systems separate normal applications from kernel code.

Conceptually:

```text
User Space

Applications
Chrome
Python
Node.js
etc.

       ↓

Kernel Space

Linux Kernel
Drivers
Memory Management
Networking
Scheduler
```

User programs should not be able to execute arbitrary privileged CPU operations.

So when an interrupt requires kernel handling, the processor may need to transition between privilege levels.

---

# 19. User Mode vs Kernel Mode

Think of a building with two areas.

```text
PUBLIC AREA
───────────
Applications

RESTRICTED AREA
───────────────
Kernel
Hardware control
```

An ordinary application operates in the less privileged environment.

The kernel operates with much greater privileges.

An interrupt can cause the CPU to transition into the privileged environment so the operating system can handle the event.

Conceptually:

```text
User Mode
    │
    │ Interrupt
    ▼
Kernel Mode
    │
    ▼
Interrupt Handler
    │
    ▼
Return
    │
    ▼
User Mode
```

The exact mechanism is handled by CPU architecture and kernel entry code.

---

# 20. Why Is This Important?

Because the CPU cannot simply say:

> "I'm running Chrome, so I'll execute whatever code Chrome tells me."

Imagine allowing a normal application to directly control:

* page tables
* hardware registers
* interrupt controllers
* other processes' memory
* CPU control registers

The entire security model would collapse.

The processor therefore provides hardware-enforced privilege boundaries.

Interrupt handling is deeply connected to these boundaries.

---

# 21. What Is an Interrupt Handler?

The interrupt handler is code that runs when a particular interrupt occurs.

Conceptually:

```text
Interrupt
    ↓
Vector
    ↓
IDT
    ↓
Handler
```

The handler determines what needs to happen.

For example:

```text
Network interrupt
       ↓
Network driver handling
```

or

```text
Timer interrupt
       ↓
Timer handling
       ↓
Scheduler activity
```

or

```text
Keyboard interrupt
       ↓
Keyboard driver
```

---

# 22. ISR

You will often see the term:

**ISR**

which means:

**Interrupt Service Routine**

An ISR is the code executed to service an interrupt.

Conceptually:

```text
Hardware
   ↓
Interrupt
   ↓
CPU
   ↓
IDT
   ↓
ISR
```

The ISR performs the immediate work required to acknowledge and process the interrupt.

---

# 23. Why Can't the ISR Do Everything?

Because interrupts are time-sensitive.

Imagine a network card generating thousands of interrupts every second.

If the CPU spends too long inside each interrupt handler:

```text
Interrupt
   ↓
Handler
   ↓
Handler
   ↓
Handler
   ↓
Handler
```

normal programs don't get enough CPU time.

Linux therefore separates immediate interrupt work from work that can safely happen later.

This eventually leads us to concepts such as:

* hard IRQ
* soft IRQ
* tasklets
* workqueues
* threaded IRQs

We'll cover those later.

For now, understand the principle:

> **The immediate interrupt path should be kept as short as practical.**

---

# 24. The Entire Architecture So Far

We can now expand our previous model.

```text
                   HARDWARE
                      │
                      ▼
              Network / SSD / GPU
                      │
                      ▼
                    IRQ
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
              Interrupt Entry
                      │
                      ▼
              Kernel Entry Code
                      │
                      ▼
                   ISR
                      │
                      ▼
             Interrupt Handling
                      │
                      ▼
                Return to Work
```

We're getting much closer to what actually happens inside a Linux system.

---

# 25. A More Accurate Mental Model

Don't think of the IDT as:

> "A table containing interrupts."

Think of it as:

> **A table that tells the CPU how to enter the appropriate handler for a particular interrupt or exception.**

And don't think of the interrupt vector as:

> "The interrupt itself."

Think:

> **An identifier used by the processor to select the appropriate IDT entry.**

Those distinctions become extremely important as we go deeper.

---

# 26. A Simple Analogy

Imagine a hospital.

A patient arrives.

```text
Patient
   ↓
Reception
   ↓
Case number
   ↓
Department
   ↓
Doctor
```

The case number identifies the correct path.

The reception system contains the information needed to route the patient.

Now translate that into computer architecture:

```text
Hardware Event
      ↓
Interrupt Vector
      ↓
IDT
      ↓
Interrupt Handler
```

The analogy isn't perfect, but it gives you the basic mental model.

---

# 27. Why This Matters for Linux

Linux is not simply receiving interrupts and randomly executing code.

There is an architectural chain connecting:

```text
Hardware
```

to

```text
CPU
```

to

```text
Kernel
```

The CPU architecture provides mechanisms such as:

* interrupt vectors
* IDT
* privilege levels
* saved execution state

Linux builds its interrupt-handling infrastructure on top of these mechanisms.

That is why understanding the hardware layer makes later Linux kernel concepts much easier.

---

# 28. Try It Yourself

You can inspect some of this from your Ubuntu VM.

Start with:

```bash
cat /proc/interrupts
```

Then:

```bash
ls /proc/irq/
```

You'll see IRQ numbers such as:

```text
0
1
8
9
...
```

You can inspect an individual IRQ:

```bash
ls /proc/irq/1/
```

You may see information such as:

```text
affinity_hint
effective_affinity
smp_affinity
smp_affinity_list
```

These expose Linux's interrupt affinity configuration.

---

# 29. Inspect APIC Information

Run:

```bash
dmesg | grep -Ei "apic|x2apic|ioapic"
```

You may see information about:

* Local APIC
* I/O APIC
* x2APIC
* interrupt routing

Also run:

```bash
lscpu
```

Look at:

```text
CPU(s)
Core(s) per socket
Thread(s) per core
Socket(s)
```

Now you can connect the hardware topology to interrupt distribution.

---

# 30. A Small Experiment

Run:

```bash
watch -n 1 cat /proc/interrupts
```

Then generate activity:

```bash
ping 8.8.8.8
```

Open another terminal and generate disk activity:

```bash
dd if=/dev/zero of=testfile bs=1M count=100
```

Then remove it:

```bash
rm testfile
```

Watch the interrupt counters.

You're observing the consequences of the architecture we have been discussing.

Hardware generates events.

Linux receives them.

The processor routes execution to appropriate handling code.

And the system eventually returns to whatever it was doing.

---

# Engineering Insight

The important idea here is that an interrupt is **not simply a signal that says "stop."**

There is an entire architecture behind that moment.

A hardware device generates a request.

The interrupt controller routes it.

The processor receives a vector.

The CPU consults the IDT.

The appropriate entry provides the information required to enter the handler.

The processor preserves the necessary execution state.

Control moves into kernel interrupt-handling code.

The interrupt is serviced.

The saved state is restored.

Execution continues.

So what looks like a tiny event:

```text
"Network packet arrived."
```

actually crosses several layers of the system:

```text
Device
   ↓
Interrupt Controller
   ↓
CPU
   ↓
Interrupt Vector
   ↓
IDT
   ↓
Kernel Entry
   ↓
ISR
   ↓
Driver / Kernel
```

That chain is one of the foundations underneath modern operating systems.

---

# Looking Ahead

We now know **how the CPU identifies where interrupt handling should begin**.

The next question gets even more interesting:

> **What exactly happens inside the CPU during interrupt entry?**

We'll need to look closely at:

* CPU state preservation
* the stack
* `RIP`
* `RFLAGS`
* privilege levels
* user mode vs kernel mode
* kernel stack switching
* interrupt gates
* the actual transition into Linux
* and eventually the Interrupt Service Routine itself

That is where we'll move from understanding **the interrupt architecture** to understanding **the actual CPU-to-kernel transition**.
