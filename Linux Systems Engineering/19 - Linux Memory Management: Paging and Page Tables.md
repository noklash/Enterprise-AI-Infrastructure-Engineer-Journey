# 19 - Linux Memory Management: Paging and Page Tables

Virtual memory gives each process the illusion of having its own large, continuous memory space.

But we left an important question unanswered:

> **How does the CPU actually translate a virtual address into a physical address?**

The answer begins with **paging** and **page tables**.

This is where virtual memory stops being an abstract idea and we start looking at the actual machinery underneath it.

---

## 1. The Problem Paging Solves

Imagine a process has this virtual address space:

```text
+-------------------------+
|                         |
|       Process           |
|                         |
|                         |
+-------------------------+
```

The process might think it has a continuous region of memory.

But physical RAM may look completely different:

```text
Physical RAM

+-------------------------+
| Page belonging to A     |
+-------------------------+
| Page belonging to C     |
+-------------------------+
| Free                    |
+-------------------------+
| Page belonging to A     |
+-------------------------+
| Page belonging to B     |
+-------------------------+
| Page belonging to A     |
+-------------------------+
```

The physical pages belonging to one process don't have to be physically adjacent.

Paging allows Linux to create the illusion of continuous virtual memory while physical memory can be fragmented.

---

# 2. What Is Paging?

**Paging** divides virtual memory into fixed-size blocks called **pages**.

Physical memory is divided into equally sized blocks called **page frames**.

For example, with 4 KiB pages:

```text
Virtual Memory

+--------+
| Page 0 |
+--------+
| Page 1 |
+--------+
| Page 2 |
+--------+
| Page 3 |
+--------+
```

Physical memory:

```text
RAM

+--------------+
| Frame 0      |
+--------------+
| Frame 1      |
+--------------+
| Frame 2      |
+--------------+
| Frame 3      |
+--------------+
```

A virtual page can be mapped to any suitable physical frame.

For example:

```text
Virtual Page 0  ──────> Physical Frame 8
Virtual Page 1  ──────> Physical Frame 2
Virtual Page 2  ──────> Physical Frame 15
Virtual Page 3  ──────> Physical Frame 4
```

Notice something important:

**The virtual order does not have to match the physical order.**

---

# 3. Why Fixed-Size Pages?

Why not simply divide memory into arbitrary-sized pieces?

Because fixed-size blocks make memory management much easier.

If every page is:

```text
4096 bytes
```

then Linux always knows exactly how large a page is.

It can therefore manage memory using predictable units:

```text
Page
Page
Page
Page
Page
...
```

This makes allocation, mapping, protection, and reclamation much more manageable.

---

# 4. The 4 KiB Page

A common page size on x86-64 Linux is:

```text
4 KiB
```

That's:

```text
4096 bytes
```

or:

```text
0x1000 bytes
```

because:

```text
4096 = 2¹²
```

That power-of-two relationship becomes extremely useful when translating addresses.

---

# 5. Virtual Address Structure

A virtual address can conceptually be divided into two major parts:

```text
+----------------------+------------+
| Virtual Page Number  | Page Offset|
+----------------------+------------+
```

The:

**Virtual Page Number**

tells us which virtual page we're accessing.

The:

**Page Offset**

tells us where inside that page the requested byte resides.

With 4 KiB pages:

```text
Page size = 2¹²
```

Therefore the bottom:

```text
12 bits
```

of the virtual address represent the offset inside the page.

The remaining bits identify the virtual page.

---

# 6. Example

Suppose we have:

```text
Virtual Address:

0x12345678
```

With 4 KiB pages:

```text
Page offset = lower 12 bits
```

So the address can conceptually be split into:

```text
Virtual Page Number | Offset
```

The CPU uses the virtual page number to find the corresponding physical frame.

Then it keeps the offset unchanged.

The fundamental operation is therefore:

```text
Virtual Page Number
        ↓
    Page Table
        ↓
Physical Frame Number

Physical Frame Number + Offset
        ↓
Physical Address
```

---

# 7. The Offset Does Not Change

This is a very important concept.

Suppose:

```text
Virtual page
        ↓
Physical frame
```

The CPU does **not** change the position inside the page.

For example:

```text
Virtual Page 100
Offset 0x250
```

maps to:

```text
Physical Frame 500
Offset 0x250
```

The offset remains:

```text
0x250
```

So:

```text
Virtual:

[ Page 100 ][ 0x250 ]

Physical:

[Frame 500][ 0x250 ]
```

Only the page/frame portion changes.

---

# 8. Page Tables

Now we need something that stores the mapping.

That's the job of the:

**Page Table**

Conceptually:

```text
Virtual Page
     ↓
+----------------+
| Page Table     |
+----------------+
     ↓
Physical Frame
```

For example:

```text
Virtual Page 0  → Frame 8
Virtual Page 1  → Frame 2
Virtual Page 2  → Frame 15
Virtual Page 3  → Frame 4
```

The page table is therefore a mapping between virtual pages and physical frames.

---

# 9. Page Table Entries

The mapping isn't just:

```text
Virtual Page → Physical Frame
```

Each mapping also contains important metadata.

A simplified page-table entry might contain:

```text
+--------------------------------+
| Physical Frame Number          |
+--------------------------------+
| Present                        |
| Writable                       |
| User accessible                |
| Executable                     |
| Accessed                       |
| Dirty                          |
+--------------------------------+
```

These bits allow the CPU to enforce memory permissions.

---

# 10. Present Bit

The **present** bit tells the processor whether the page currently has a valid physical-memory mapping.

Conceptually:

```text
Present = 1
```

means the page is currently mapped.

Whereas:

```text
Present = 0
```

can indicate that the page isn't currently available through a normal physical mapping.

This can trigger a page fault when accessed.

---

# 11. Read and Write Permissions

A page can have different permissions.

For example:

```text
Read-only
```

or:

```text
Read + Write
```

or:

```text
Read + Execute
```

A page containing program instructions might be:

```text
R-X
```

while writable program data might be:

```text
RW-
```

This prevents arbitrary writes to executable code.

---

# 12. User vs Kernel Access

Page mappings can also distinguish between:

```text
User
```

and:

```text
Kernel
```

This matters because a normal user-space application shouldn't be able to access arbitrary kernel memory.

Conceptually:

```text
User Process
     ↓
Page Table
     ↓
User-accessible pages
```

while:

```text
Kernel
     ↓
Page Table
     ↓
Privileged mappings
```

The CPU enforces these permissions.

---

# 13. Why One Page Table Isn't Enough

Now we encounter an interesting problem.

Modern x86-64 systems use **multi-level page tables**.

Why?

Because a process can have an enormous virtual address space.

Imagine trying to create one gigantic table containing an entry for every possible virtual page.

That would waste enormous amounts of memory.

Most processes don't use anywhere near their entire virtual address space.

So instead of creating one massive table, Linux uses a hierarchy.

---

# 14. Multi-Level Page Tables

Conceptually:

```text
Virtual Address
      ↓
+-------------+
| Level 1     |
+-------------+
      ↓
+-------------+
| Level 2     |
+-------------+
      ↓
+-------------+
| Level 3     |
+-------------+
      ↓
+-------------+
| Level 4     |
+-------------+
      ↓
+-------------+
| Page Table  |
+-------------+
      ↓
Physical Frame
```

On modern x86-64 systems, you will commonly encounter four- or five-level paging depending on the configuration and address-space capabilities.

Linux abstracts the architecture-specific details through its own page-table interfaces.

---

# 15. Why Multi-Level Paging Saves Memory

Imagine a huge virtual address space:

```text
+----------------------------------------+
|                                        |
|            Huge address space          |
|                                        |
+----------------------------------------+
```

But the process only uses:

```text
+--------+
| Code   |
+--------+

                    lots of unused space

+--------+
| Stack  |
+--------+
```

A flat page table would potentially require enormous amounts of storage.

A hierarchical page table only creates lower-level structures where they are actually needed.

Conceptually:

```text
Root
 |
 +---- Used region
 |       |
 |       +---- Page table
 |
 +---- Unused region
         |
         X
      Nothing needed
```

This makes page-table storage much more efficient.

---

# 16. x86-64 Page Table Hierarchy

On a typical four-level x86-64 setup, the hierarchy is commonly represented as:

```text
PGD
 ↓
P4D
 ↓
PUD
 ↓
PMD
 ↓
PTE
```

Depending on the architecture and configuration, some levels may effectively collapse into another level.

The important conceptual structure is:

```text
Virtual Address
      ↓
PGD
      ↓
PUD/P4D
      ↓
PMD
      ↓
PTE
      ↓
Physical Frame
```

Don't worry about memorizing every acronym yet.

The important thing is understanding **why the hierarchy exists**.

---

# 17. What Is a PTE?

The final mapping is commonly represented by a:

**Page Table Entry**

or:

**PTE**

Conceptually:

```text
Virtual Page
      ↓
PTE
      ↓
Physical Frame
```

The PTE contains the information needed to determine where the page is located and what permissions apply.

---

# 18. Walking the Page Tables

Suppose the CPU receives:

```text
Virtual Address
```

The processor needs to determine:

```text
Which physical frame?
```

It walks the page-table hierarchy.

Conceptually:

```text
Virtual Address
      ↓
Extract index
      ↓
PGD
      ↓
Extract next index
      ↓
PUD
      ↓
Extract next index
      ↓
PMD
      ↓
Extract next index
      ↓
PTE
      ↓
Physical Frame
```

The CPU then combines:

```text
Physical Frame
+
Page Offset
```

to produce the physical address.

---

# 19. A Simple Analogy

Imagine a huge library.

You want a particular book.

You could search every book individually.

That would be terrible.

Instead:

```text
Building
 ↓
Floor
 ↓
Section
 ↓
Shelf
 ↓
Book
```

Each level narrows down where you need to look.

Page tables work similarly:

```text
Root
 ↓
Directory
 ↓
Directory
 ↓
Table
 ↓
Page
```

Each level narrows down the location of the final physical frame.

---

# 20. The CPU Register That Points to the Page Tables

On x86-64, the processor uses:

```text
CR3
```

to identify the active page-table root.

Conceptually:

```text
CR3
 ↓
Page-table root
 ↓
Page-table hierarchy
 ↓
Physical memory
```

When the CPU executes a process, the relevant address-space translation context must be available to the processor.

This connects directly to your earlier lesson on context switching.

---

# 21. Context Switching and CR3

Imagine:

```text
Process A
```

is running.

The CPU is using Process A's address-space translation structures.

Then the scheduler switches to:

```text
Process B
```

The processor must now operate using the appropriate address-space context for Process B.

Conceptually:

```text
Process A
    ↓
Address Space A
    ↓
CR3 / translation context

          ↓ context switch

Process B
    ↓
Address Space B
    ↓
CR3 / translation context
```

This is one reason process switching and memory management are closely connected.

---

# 22. Page Tables Are Memory Too

Here's another important realization.

Page tables themselves occupy memory.

They are data structures maintained by the operating system.

So:

```text
Process memory
```

isn't the only memory being consumed.

The kernel also needs memory for:

```text
Page tables
Kernel structures
Caches
Buffers
```

For large servers with many processes and enormous address spaces, page-table memory can become significant.

---

# 23. Shared Pages

Virtual memory also makes sharing possible.

Suppose two processes use the same shared library.

They may have different virtual mappings:

```text
Process A
Virtual Page 100
      ↓
Physical Frame 500
```

and:

```text
Process B
Virtual Page 200
      ↓
Physical Frame 500
```

Both virtual pages refer to the same physical frame.

This means:

```text
Two virtual addresses
        ↓
One physical page
```

That is extremely useful.

---

# 24. Copy-on-Write Again

This also explains Copy-on-Write more precisely.

After:

```c
fork();
```

Linux can have:

```text
Process A
Virtual Page X
      ↓
Physical Frame 100
```

and:

```text
Process B
Virtual Page X
      ↓
Physical Frame 100
```

Both point to the same physical frame.

The mappings are initially protected against modification.

If one process writes:

```text
Process B
   ↓
Write
   ↓
Page fault
   ↓
Kernel
   ↓
Copy page
```

Then:

```text
Process A ─────> Frame 100

Process B ─────> Frame 200
```

This is a direct application of page-table permissions.

---

# 25. Page Faults and Page Tables

Now connect this to your previous interrupt/exception lesson.

Suppose a process accesses:

```text
Virtual Page X
```

The CPU walks the page tables.

It discovers:

```text
No valid mapping
```

or:

```text
Access violates permissions
```

The CPU generates a page fault exception.

Conceptually:

```text
Memory access
      ↓
Page-table lookup
      ↓
Problem
      ↓
Page Fault Exception
      ↓
IDT
      ↓
Linux page-fault handler
```

So the page-table mechanism directly connects to the exception architecture you already studied.

---

# 26. TLB: Why Page-Table Walking Isn't Done Every Time

There's a major performance problem.

Modern CPUs execute enormous numbers of memory accesses.

If every memory access required:

```text
PGD
 ↓
PUD
 ↓
PMD
 ↓
PTE
```

the processor would spend a huge amount of time doing address translation.

That's where the:

**TLB**

comes in.

The Translation Lookaside Buffer caches recent translations.

---

# 27. TLB Hit

Suppose the CPU needs:

```text
Virtual Page 100
```

The TLB already contains:

```text
Virtual Page 100
      ↓
Physical Frame 500
```

That's a:

**TLB hit**

The CPU can quickly obtain the physical frame.

```text
Virtual Address
      ↓
     TLB
      ↓
Physical Frame
      ↓
RAM
```

No complete page-table walk is necessary.

---

# 28. TLB Miss

Suppose the translation isn't in the TLB.

That's a:

**TLB miss**

The CPU must obtain the translation through the page-table machinery.

Conceptually:

```text
Virtual Address
      ↓
     TLB
      ↓
    MISS
      ↓
Page-table walk
      ↓
Translation found
      ↓
TLB updated
      ↓
Memory access
```

We'll study this much more deeply in the next lesson.

---

# 29. Huge Pages

The normal page size might be:

```text
4 KiB
```

But Linux also supports larger pages.

These are often called:

**Huge Pages**

Larger pages reduce the number of page-table entries required and can reduce TLB pressure.

For example, x86 systems can support sizes such as:

```text
4 KiB
2 MiB
1 GiB
```

depending on the specific hardware and configuration.

This can be extremely useful for:

* databases
* virtualization
* high-performance networking
* AI workloads
* large memory applications

---

# 30. Why Huge Pages Help

Suppose you have:

```text
1 GiB memory
```

Using 4 KiB pages requires:

```text
1 GiB / 4 KiB
```

which is:

```text
262,144 pages
```

That's a lot of pages.

With 2 MiB pages:

```text
1 GiB / 2 MiB
```

you need:

```text
512 pages
```

That's dramatically fewer mappings.

The translation system therefore has fewer pages to manage.

---

# 31. Internal Fragmentation

Huge pages aren't automatically better.

Suppose a program only needs:

```text
10 KiB
```

A 4 KiB page system needs approximately:

```text
3 pages
```

A huge 2 MiB page would be excessive.

Larger pages can therefore cause more wasted space through internal fragmentation.

Systems engineering is full of these tradeoffs.

---

# 32. Page Tables and Performance

Page tables affect performance in several ways.

Large address spaces can require:

```text
More page-table memory
```

Frequent translation misses can cause:

```text
More TLB misses
```

Poor memory locality can cause:

```text
More cache misses
```

Large numbers of processes can create:

```text
More address-space management overhead
```

And huge workloads can create:

```text
More page faults
```

So virtual memory isn't simply a convenience.

It is part of the performance architecture of the machine.

---

# 33. Lab 1: Inspect Page Size

Run:

```bash
getconf PAGE_SIZE
```

You will commonly see:

```text
4096
```

You can also run:

```bash
getconf PAGESIZE
```

These commands tell you the system's standard memory page size from the userspace perspective.

---

# 34. Lab 2: Inspect Process Mappings

Run:

```bash
sleep 300 &
```

Find the PID:

```bash
pgrep sleep
```

Then:

```bash
cat /proc/<PID>/maps
```

Now look at the mapping ranges.

For example:

```text
55ab00000000-55ab00001000 r--p
55ab00001000-55ab00002000 r-xp
```

The addresses are virtual addresses.

You are looking directly at the process's virtual address space.

---

# 35. Lab 3: Inspect Page Information

Linux provides:

```text
/proc/<pid>/pagemap
```

This interface contains information about the relationship between virtual pages and physical page information.

However, access to physical page information is restricted on many systems for security reasons.

You can still use it as an introduction to the concept.

The important relationship is:

```text
Virtual Page
      ↓
Pagemap information
      ↓
Physical page information
```

---

# 36. Lab 4: Inspect Memory Maps with `pmap`

Install the appropriate package if necessary, then run:

```bash
pmap <PID>
```

For example:

```bash
pmap $(pgrep sleep)
```

You'll get a more human-readable view of the process's mappings.

Compare:

```bash
cat /proc/<PID>/maps
```

with:

```bash
pmap <PID>
```

You're looking at the same general concept through different interfaces.

---

# 37. Lab 5: Observe Huge Pages

Check:

```bash
cat /proc/meminfo | grep -i huge
```

You may see entries such as:

```text
HugePages_Total
HugePages_Free
HugePages_Rsvd
HugePages_Surp
Hugepagesize
Hugetlb
```

This lets you inspect the system's huge-page configuration.

---

# 38. Lab 6: Watch Memory While Allocating

Use the allocation program from the previous lesson.

Run:

```bash
./memory_alloc
```

Then:

```bash
watch -n 1 "grep -E 'VmSize|VmRSS' /proc/<PID>/status"
```

Now modify the program so it touches memory one page at a time:

```c
for (size_t i = 0; i < size; i += 4096)
    buffer[i] = 1;
```

Watch the RSS change.

You are observing the consequences of page-level memory management.

---

# 39. Lab 7: Observe Page Faults

Linux provides page-fault statistics.

Run:

```bash
cat /proc/<PID>/stat
```

There are fields containing fault information.

A more convenient tool is:

```bash
/usr/bin/time -v ./memory_alloc
```

Look for:

```text
Minor (re) page faults
Major (re) page faults
```

This gives you a practical way to observe page-fault activity.

---

# 40. Minor vs Major Page Faults

A **minor page fault** generally means the kernel had to resolve a page fault without requiring a disk I/O operation.

A **major page fault** involves more expensive backing-store I/O, such as retrieving a page from storage.

Conceptually:

```text
Minor:

Page fault
   ↓
Memory already available
   ↓
Kernel establishes mapping
```

versus:

```text
Major:

Page fault
   ↓
Page needs storage I/O
   ↓
Disk / SSD
   ↓
Page loaded
   ↓
Mapping established
```

Major faults are considerably more expensive.

---

# 41. Lab 8: Observe Address Randomization

Compile your memory-layout program and run:

```bash
./memory_layout
```

Then run it again:

```bash
./memory_layout
```

Compare:

```text
Code address
Stack address
Heap address
```

You should see that addresses can change between executions.

Now check:

```bash
cat /proc/sys/kernel/randomize_va_space
```

This connects ASLR to the virtual-address architecture.

---

# 42. The Full Translation Pipeline

At this point, build this model in your head:

```text
                    CPU
                     │
                     │ Virtual Address
                     ▼
              +-------------+
              |     TLB     |
              +-------------+
                 │       │
              HIT│       │MISS
                 │       ▼
                 │   Page Tables
                 │       │
                 │       ▼
                 │   Physical Frame
                 │       │
                 └───────┤
                         ▼
                 +---------------+
                 | Physical RAM  |
                 +---------------+
```

If something goes wrong during translation:

```text
Memory Access
     ↓
Translation
     ↓
Fault
     ↓
CPU Exception
     ↓
IDT
     ↓
Linux Page Fault Handler
     ↓
Memory Management Subsystem
```

---

# 43. The Bigger Picture

Look at how much of Linux you have now connected.

You started with:

```text
Process
```

Then:

```text
Process
 ↓
Scheduler
 ↓
CPU
```

You learned:

```text
Context Switching
```

Then:

```text
Interrupts
 ↓
APIC
 ↓
CPU
```

Then:

```text
Interrupt
 ↓
IDT
 ↓
ISR
```

Then:

```text
System Call
 ↓
Kernel
```

Now:

```text
Process
 ↓
Virtual Address
 ↓
Page Tables
 ↓
MMU
 ↓
Physical Memory
```

These aren't isolated topics anymore.

They're pieces of one operating system.

---

# 44. Engineering Mental Model

The most important thing to understand from this lesson is:

> **Paging separates the virtual organization of memory from its physical organization.**

A process sees:

```text
Virtual Pages

0
1
2
3
4
5
...
```

Physical RAM might contain:

```text
Frame 91
Frame 3
Frame 700
Frame 18
Frame 42
...
```

The page tables establish the relationship:

```text
Virtual Page 0 → Frame 91
Virtual Page 1 → Frame 3
Virtual Page 2 → Frame 700
Virtual Page 3 → Frame 18
Virtual Page 4 → Frame 42
```

The process doesn't need to know where those pages physically reside.

That's the abstraction.

---

# 45. What You Should Be Able to Explain

Before moving forward, you should be able to answer:

### What is paging?

A memory-management technique that divides virtual memory into fixed-size pages and physical memory into corresponding page frames.

### Why do we use pages?

They provide a predictable unit for memory mapping, protection, allocation, and management.

### What is a page table?

A hierarchical data structure used to map virtual pages to physical frames and enforce memory permissions.

### What is a PTE?

A Page Table Entry describing the mapping and properties of a virtual page.

### Why are page tables hierarchical?

To avoid allocating enormous flat tables for largely unused virtual address spaces.

### What is the page offset?

The portion of a virtual address identifying the byte's position inside a page.

### Does the page offset change during translation?

No.

### What changes?

The virtual page number is translated into a physical frame number.

### What is CR3?

On x86-64, a processor control register used to identify the active page-table root.

### What happens when a page mapping is invalid?

The CPU can generate a page-fault exception, which transfers control to the kernel.

### Why does the TLB exist?

To cache recent address translations and avoid repeatedly walking page tables.

### Why are huge pages useful?

They allow larger memory regions to be represented with fewer pages and can reduce translation overhead and TLB pressure.

---

# 46. Final Mental Picture

Keep this diagram:

```text
                 PROCESS
                    │
                    ▼
           VIRTUAL ADDRESS
                    │
             ┌──────┴──────┐
             │             │
       Page Number      Offset
             │             │
             ▼             │
       PAGE TABLES          │
             │             │
       Virtual Page         │
             ↓              │
       Physical Frame       │
             │              │
             └──────┬───────┘
                    │
                    ▼
            PHYSICAL ADDRESS
                    │
                    ▼
                   RAM
```

And remember the performance shortcut:

```text
Virtual Address
      ↓
     TLB
      │
   ┌──┴──┐
   │     │
  HIT   MISS
   │     │
   │     ▼
   │  Page Tables
   │     │
   └──┬──┘
      ▼
Physical Address
      ↓
     RAM
```

And the failure path:

```text
Virtual Address
      ↓
Translation
      ↓
Page Fault
      ↓
CPU Exception
      ↓
Linux Kernel
      ↓
Memory Management
```

**Next: 20 - Linux Memory Management: Address Translation and the TLB.**
