# Hard IRQs, SoftIRQs, Deferred Work, and Threaded Interrupts

We now understand how an interrupt travels from hardware to the CPU:

```text
Hardware Device
      ↓
      IRQ
      ↓
   I/O APIC
      ↓
 Interrupt Routing
      ↓
 Local APIC
      ↓
     CPU
      ↓
Interrupt Vector
      ↓
     IDT
      ↓
Linux Interrupt Entry
```

But we still have an important question:

> **What does Linux actually do after the CPU enters the kernel because of an interrupt?**

This is where Linux's interrupt-handling architecture becomes important.

The key idea is simple:

> **Linux does not want to spend too much time doing work directly inside the immediate interrupt context.**

Why?

Because interrupts can arrive extremely frequently.

A busy network interface, NVMe device, or timer can generate a huge amount of work.

If the CPU stopped normal execution and performed all of that work immediately every time an interrupt arrived, the system could spend most of its time handling interrupts.

Linux therefore separates interrupt processing into different levels.

The most important ones to understand are:

```text
Hard IRQ
   ↓
SoftIRQ / deferred processing
   ↓
Normal kernel work
```

And Linux can also use:

```text
Threaded IRQ
```

when interrupt handling needs to behave more like a normal schedulable task.

---

# 1. The Basic Problem

Imagine a network card receives a packet.

```text
Network Card
      ↓
    IRQ
      ↓
     CPU
```

The CPU enters the kernel.

Now imagine Linux tried to do everything immediately:

```text
Receive interrupt
      ↓
Read device
      ↓
Copy packet
      ↓
Process packet
      ↓
Run networking stack
      ↓
Update routing
      ↓
Run firewall rules
      ↓
Deliver packet
```

That could take significant CPU time.

Meanwhile, more interrupts may be waiting.

So Linux asks:

> **What absolutely has to happen right now?**

and:

> **What can safely happen slightly later?**

That distinction is fundamental.

---

# 2. Hard IRQ

A **hard IRQ** is the immediate interrupt context.

This is the part closest to the actual hardware interrupt.

Conceptually:

```text
Hardware
   ↓
Interrupt
   ↓
CPU
   ↓
Hard IRQ handling
```

The goal is to respond to the hardware quickly.

The kernel may need to:

* acknowledge the interrupt
* determine which device caused it
* read important device state
* stop or acknowledge the device's interrupt condition
* schedule additional processing

The hard IRQ portion should generally be kept short.

---

# 3. Why Does It Need to Be Fast?

Imagine you're driving and someone presses the emergency brake.

You need to respond immediately.

But you don't want to stop the car and spend 30 minutes inspecting the entire vehicle.

The immediate response should be:

```text
Stop.
```

Then the detailed investigation can happen afterward.

Interrupt handling works similarly.

The immediate interrupt context should deal with urgent work.

More expensive work can be deferred.

---

# 4. Hard IRQ Context Is Special

An important thing to understand is that a hard IRQ handler is **not equivalent to a normal process**.

It is executing because hardware demanded attention.

The kernel is operating in a special interrupt context.

That means the usual scheduling behavior does not apply in the same way.

For example, you generally cannot simply say:

```text
"I'm going to sleep for 5 seconds."
```

inside a hard IRQ handler.

That would make little sense.

The CPU needs to finish handling the interrupt and return to normal execution.

---

# 5. What Does "Cannot Sleep" Mean?

This is an important Linux concept.

Suppose normal kernel code needs to wait for something.

It can potentially sleep:

```text
Kernel code
    ↓
Wait
    ↓
Scheduler
    ↓
Another task runs
```

But an interrupt handler isn't a normal schedulable task.

It arrived asynchronously because hardware generated an event.

So the kernel cannot simply treat the interrupt handler like an ordinary process and put it to sleep.

This is one of the reasons interrupt handlers must be carefully designed.

---

# 6. The Need for Deferred Work

Suppose the network card says:

> "I received 10,000 packets."

The immediate interrupt handler might do something like:

```text
Interrupt
   ↓
Acknowledge device
   ↓
Record that packets arrived
   ↓
Schedule networking work
   ↓
Return
```

Then Linux processes the heavier work afterward.

Conceptually:

```text
             INTERRUPT
                 │
                 ▼
              Hard IRQ
                 │
                 │
       "Something happened."
                 │
                 ▼
         Schedule more work
                 │
                 ▼
              Return
                 │
                 ▼
       Deferred processing
```

This keeps the immediate interrupt path short.

---

# 7. SoftIRQ

One mechanism Linux uses for deferred processing is the **SoftIRQ**.

SoftIRQ means:

**Software Interrupt Request**

It is not a physical hardware interrupt.

It is a kernel mechanism for scheduling certain types of work that should happen as part of interrupt processing but don't need to happen during the immediate hardware interrupt.

Think of it as:

```text
Hard IRQ
   ↓
"Do this immediately."
```

versus:

```text
SoftIRQ
   ↓
"Do this as soon as practical."
```

---

# 8. Why Call It an Interrupt?

The name can be confusing.

A SoftIRQ is not a hardware device interrupt.

It is a Linux kernel mechanism.

You can think of the distinction like this:

```text
HARDWARE INTERRUPT

Hardware
   ↓
CPU
   ↓
Kernel
```

versus:

```text
SOFTIRQ

Kernel
   ↓
Schedules deferred kernel work
```

The second one is entirely software-driven.

---

# 9. Networking Uses SoftIRQs

Networking is one of the most important examples.

A network packet arrives:

```text
NIC
 ↓
Hardware interrupt
 ↓
Hard IRQ
```

The immediate handling can schedule network processing.

Then Linux can perform additional networking work in deferred context.

Conceptually:

```text
NIC
 ↓
IRQ
 ↓
Hard IRQ
 ↓
Networking SoftIRQ
 ↓
Network stack
 ↓
Application
```

This separation allows the kernel to respond quickly to the device while moving more expensive packet processing out of the immediate hardware interrupt path.

---

# 10. Why Not Just Do Everything in the SoftIRQ?

SoftIRQs are still special kernel execution contexts.

They aren't normal user processes.

They can execute a significant amount of work, but if the system is overloaded with deferred work, that can still become a problem.

Imagine:

```text
10,000 packets
     ↓
10,000 pieces of deferred work
     ↓
CPU
```

If the CPU spends all its time processing SoftIRQs, normal processes can suffer.

Linux therefore has additional mechanisms for managing work that can safely be handled in ordinary schedulable contexts.

---

# 11. ksoftirqd

This is where you may encounter:

```text
ksoftirqd
```

Linux has kernel threads associated with processing SoftIRQ work when the kernel needs to move that work out of the immediate interrupt context.

You may see processes such as:

```text
ksoftirqd/0
ksoftirqd/1
ksoftirqd/2
```

depending on how many CPUs the system has.

The number corresponds to the CPU.

For example:

```text
ksoftirqd/0
```

is associated with CPU 0.

---

# 12. Why Is ksoftirqd Useful?

Imagine the CPU is constantly receiving network interrupts.

Without a mechanism to control deferred work, the CPU could spend too much time processing SoftIRQs.

Linux can push some of that processing into:

```text
ksoftirqd
```

which is a schedulable kernel thread.

Now the scheduler can make decisions about how much CPU time that work receives.

Conceptually:

```text
Hardware IRQ
      ↓
Hard IRQ
      ↓
SoftIRQ work
      ↓
Too much work
      ↓
ksoftirqd
      ↓
Scheduler
```

This gives Linux more control over CPU consumption.

---

# 13. Workqueues

Another mechanism Linux uses is the **workqueue**.

A workqueue allows kernel work to be deferred into a context that behaves more like normal kernel-thread execution.

Think:

```text
Hard IRQ
    ↓
"Do this later."
    ↓
Workqueue
    ↓
Kernel worker
```

The advantage is that workqueue processing occurs in a schedulable context.

That means the kernel can perform operations that are not appropriate in hard IRQ context, including operations that may sleep when the specific workqueue context permits it.

---

# 14. Hard IRQ vs SoftIRQ vs Workqueue

Here's a useful mental model.

| Context   | Purpose                               | Can sleep?            |
| --------- | ------------------------------------- | --------------------- |
| Hard IRQ  | Immediate hardware response           | No                    |
| SoftIRQ   | Deferred interrupt-related processing | No                    |
| Workqueue | Deferred kernel work                  | Yes, when appropriate |

The exact kernel implementation has additional details, but this table gives you the architectural distinction you need at this stage.

---

# 15. Threaded Interrupts

Linux also supports **threaded interrupts**.

This is another interesting idea.

Instead of doing all interrupt work directly in hard IRQ context, the kernel can arrange for the main interrupt handling work to execute in a kernel thread.

Conceptually:

```text
Hardware
   ↓
IRQ
   ↓
Minimal hard IRQ handling
   ↓
IRQ Thread
   ↓
Detailed processing
```

Now the scheduler can manage the interrupt work more like other kernel tasks.

---

# 16. Why Would You Want That?

Suppose a device needs relatively complicated processing.

Doing everything inside hard IRQ context would be undesirable.

Threaded interrupts allow more of the work to happen in a schedulable context.

This is particularly useful when:

* latency matters
* the handler needs to perform operations that may sleep
* the driver needs more complicated processing
* the system uses real-time scheduling techniques

---

# 17. PREEMPT_RT

This becomes especially important in Linux real-time systems.

The **PREEMPT_RT** project modifies Linux to reduce worst-case scheduling latency.

One of the important ideas in real-time Linux is moving more interrupt handling into threads so that interrupt processing can participate in scheduling and priority management.

Conceptually:

```text
Traditional

Hardware
   ↓
Hard IRQ
   ↓
Kernel processing
```

versus a more threaded approach:

```text
Hardware
   ↓
Minimal interrupt entry
   ↓
IRQ Thread
   ↓
Scheduler
   ↓
Handler
```

This allows the system to reason more explicitly about priorities and latency.

---

# 18. The Entire Interrupt Pipeline

We can now expand our architecture.

```text
                    HARDWARE
                       │
                       ▼
                  Device Event
                       │
                       ▼
                      IRQ
                       │
                       ▼
                    I/O APIC
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
               Kernel Entry Code
                       │
                       ▼
                    Hard IRQ
                       │
             ┌─────────┴─────────┐
             │                   │
             ▼                   ▼
          Return              Deferred
                                Work
                                 │
                    ┌────────────┼────────────┐
                    │            │            │
                    ▼            ▼            ▼
                 SoftIRQ     Workqueue    IRQ Thread
                    │            │            │
                    └────────────┴────────────┘
                                 │
                                 ▼
                       Kernel Subsystem
                                 │
                                 ▼
                              Driver
```

This is the architecture you should have in your head.

---

# 19. A Network Packet Example

Let's walk through one packet.

Your server receives a packet.

### Step 1

The network card receives the packet.

```text
NIC
 ↓
Packet received
```

### Step 2

The NIC generates an interrupt.

```text
NIC
 ↓
IRQ
```

### Step 3

The interrupt is routed.

```text
IRQ
 ↓
I/O APIC
 ↓
Local APIC
 ↓
CPU
```

### Step 4

The CPU enters Linux's interrupt path.

```text
CPU
 ↓
Vector
 ↓
IDT
 ↓
Kernel entry
```

### Step 5

The hard IRQ handler performs the immediate device-specific work.

```text
Hard IRQ
 ↓
Acknowledge / service device
 ↓
Schedule network processing
```

### Step 6

The interrupt handler finishes quickly.

```text
Hard IRQ
 ↓
Return
```

### Step 7

Linux performs deferred networking work.

```text
SoftIRQ
 ↓
Network processing
```

### Step 8

The packet eventually reaches the appropriate socket.

```text
Network Stack
 ↓
Socket
 ↓
Application
```

The application may finally receive the data.

---

# 20. Why This Architecture Is Important

Imagine a 100 GbE network adapter.

It can receive enormous amounts of traffic.

If every packet required the CPU to perform every part of networking processing inside the hard IRQ handler, the system would spend huge amounts of time in interrupt context.

Instead:

```text
Immediate work
     ↓
Hard IRQ
```

and:

```text
Additional work
     ↓
Deferred processing
```

This allows Linux to balance responsiveness against throughput.

---

# 21. Interrupt Storms

Now we reach an important failure scenario.

Imagine a device starts generating interrupts continuously.

```text
IRQ
IRQ
IRQ
IRQ
IRQ
IRQ
IRQ
IRQ
IRQ
...
```

This can become an **interrupt storm**.

The CPU spends so much time responding to interrupts that normal work gets starved.

Conceptually:

```text
Normal application
        ↓
      10%
      CPU

Interrupt handling
        ↓
      90%
      CPU
```

Or worse:

```text
Interrupt handling
████████████████████████████

Normal work
█
```

---

# 22. Why Interrupt Storms Matter

Interrupt storms can cause:

* high CPU usage
* poor application performance
* increased latency
* network throughput problems
* storage performance problems
* system instability

This is why interrupt configuration is an important performance-engineering topic.

---

# 23. Interrupt Coalescing

Modern devices often have another trick.

Instead of generating:

```text
Packet 1 → interrupt
Packet 2 → interrupt
Packet 3 → interrupt
Packet 4 → interrupt
```

the device can wait and generate fewer interrupts:

```text
Packet 1
Packet 2
Packet 3
Packet 4
     ↓
One interrupt
```

This is called **interrupt coalescing**.

It reduces interrupt overhead.

But there's a trade-off.

Waiting for multiple packets can increase latency.

So:

```text
More interrupts
    ↓
Lower latency
    ↓
Higher CPU overhead
```

while:

```text
Fewer interrupts
    ↓
Lower CPU overhead
    ↓
Potentially higher latency
```

This is a classic systems engineering trade-off.

---

# 24. Why AI Infrastructure Cares

Imagine an AI inference server.

It has:

```text
GPU
NIC
NVMe
CPU
```

The GPU may process enormous amounts of data.

The NIC may continuously receive requests.

The NVMe system may continuously load model data.

All of these devices generate events.

The CPU has to coordinate this activity.

Poor interrupt handling can create CPU bottlenecks even when the GPU itself has plenty of compute capacity.

You might have:

```text
GPU utilization: 40%

CPU interrupt load: 95%
```

The GPU isn't necessarily the bottleneck.

The system around it might be.

That's why infrastructure engineers need to understand the hardware beneath the application.

---

# 25. Observing This in Linux

Your Ubuntu VM gives you several ways to inspect the system.

First:

```bash
cat /proc/interrupts
```

Then:

```bash
ps -eLo pid,tid,psr,comm | grep ksoftirqd
```

You may see:

```text
ksoftirqd/0
ksoftirqd/1
ksoftirqd/2
...
```

depending on the CPUs exposed to your VM.

You can also use:

```bash
top
```

and look for:

```text
si
```

The `si` CPU percentage represents time spent servicing software interrupts.

For example:

```text
%Cpu(s):  5.0 us, 2.0 sy, 0.0 ni, 70.0 id, 8.0 wa, 15.0 si
```

Here:

```text
si = 15.0%
```

would mean a significant amount of CPU time is being spent servicing software interrupts.

---

# 26. A Useful Experiment

Run:

```bash
watch -n 1 cat /proc/interrupts
```

Then in another terminal:

```bash
ping 8.8.8.8
```

Then:

```bash
top
```

Look at CPU activity.

Now generate more network traffic if your environment allows it.

The goal isn't to produce a huge load inside the VM.

The goal is to observe that:

```text
Network activity
      ↓
Hardware interrupts
      ↓
Kernel processing
      ↓
CPU work
```

---

# 27. Another Useful Experiment

Check software interrupt statistics:

```bash
cat /proc/softirqs
```

This is extremely useful.

You'll see categories such as:

```text
TIMER
NET_TX
NET_RX
BLOCK
IRQ_POLL
TASKLET
SCHED
HRTIMER
RCU
```

These counters show how much SoftIRQ activity Linux has processed on each CPU.

For example:

```text
                    CPU0       CPU1
NET_RX            123456      54321
NET_TX             23456      10234
TIMER             345678     321456
RCU               456789     345678
```

The exact output depends on your system.

---

# 28. Compare the Two Files

Now you have:

```bash
cat /proc/interrupts
```

and:

```bash
cat /proc/softirqs
```

These show two different layers.

### `/proc/interrupts`

Shows hardware interrupt activity.

```text
Hardware
   ↓
IRQ
```

### `/proc/softirqs`

Shows software interrupt activity.

```text
Kernel
   ↓
SoftIRQ
```

This gives you a concrete way to observe the distinction we've been discussing.

---

# 29. The Mental Model You Should Keep

If you remember nothing else from this chapter, remember this:

```text
HARDWARE
   │
   ▼
Hard IRQ
   │
   │ "Something happened!"
   ▼
Immediate response
   │
   ▼
Deferred work
   │
   ├── SoftIRQ
   ├── Workqueue
   └── IRQ Thread
   │
   ▼
Detailed processing
```

The architecture exists because **immediate hardware response and heavy processing have different requirements**.

---

# 30. One Important Correction to Your Mental Model

Don't think:

> "SoftIRQ is simply a slower interrupt."

That's not quite right.

A better way to think about it is:

> **A SoftIRQ is a kernel mechanism for deferring certain work from the immediate hardware interrupt context.**

Likewise, don't think:

> "ksoftirqd handles every SoftIRQ."

It doesn't.

SoftIRQ processing can occur directly in interrupt-related execution contexts, and `ksoftirqd` provides a kernel-thread context for processing pending SoftIRQs when appropriate.

That distinction becomes important when analyzing performance.

---

# 31. The Full Picture

We can now connect everything we've studied.

```text
                     DEVICE
                       │
                       ▼
                Hardware Event
                       │
                       ▼
                      IRQ
                       │
                       ▼
                    I/O APIC
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
              CPU Interrupt Entry
                       │
                       ▼
                    Hard IRQ
                       │
                       ▼
              Immediate device work
                       │
                       ▼
               Schedule deferred work
                       │
              ┌────────┼─────────┐
              ▼        ▼         ▼
           SoftIRQ  Workqueue  IRQ Thread
              │        │         │
              └────────┼─────────┘
                       ▼
               Kernel Subsystem
                       │
                       ▼
                    Driver
                       │
                       ▼
                 Application
```

This is the bridge between **hardware architecture** and **Linux kernel behavior**.

---

# Engineering Insight

There's a bigger systems principle hiding here.

A system becomes difficult to scale when every event requires immediate attention from the same execution path.

Linux solves this by separating **urgent work from deferrable work**.

The same idea appears everywhere in infrastructure.

A network server doesn't process every request synchronously at the hardware interrupt level.

A Kubernetes controller doesn't do every operation inside one event callback.

A distributed system separates event detection from expensive processing.

The principle is:

```text
Detect quickly.

Respond to what is urgent.

Defer what can wait.

Process it in an appropriate context.
```

Linux's interrupt architecture is one of the lowest-level examples of that principle.

---

# Lab Checklist

Before moving on, run these on your Ubuntu VM:

```bash
cat /proc/interrupts
```

```bash
cat /proc/softirqs
```

```bash
ps -eLo pid,tid,psr,comm | grep ksoftirqd
```

```bash
top
```

```bash
lscpu
```

And:

```bash
dmesg | grep -Ei "apic|x2apic|ioapic"
```

Then answer:

1. What is a hard IRQ?
2. Why should hard IRQ handlers be short?
3. Why can't a hard IRQ handler simply sleep?
4. What is a SoftIRQ?
5. How is a SoftIRQ different from a hardware IRQ?
6. What is `ksoftirqd`?
7. What is a workqueue?
8. Why would Linux use a threaded IRQ?
9. What is an interrupt storm?
10. What is interrupt coalescing?
11. What does `/proc/interrupts` show?
12. What does `/proc/softirqs` show?
13. Why might a server have high CPU usage even when its main application isn't doing much work?

If you can answer those, we've covered the interrupt architecture deeply enough for **Phase 1 Linux Systems Engineering**.

The next topic should be **Linux System Calls**, because we've now followed the hardware-to-kernel direction and can reverse the perspective: **how does a normal user-space program intentionally ask the kernel to do something?**
