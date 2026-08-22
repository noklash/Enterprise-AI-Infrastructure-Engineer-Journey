# Session 08 — Disk I/O Path, Elevators, and Basic I/O Observability

## 1. Position in the Curriculum

**Phase**  
PHASE 1 — Linux Systems Engineering

**Module**  
MODULE 1 — Filesystems

**Session**  
SESSION 08 — Disk I/O Path, Elevators, and Basic I/O Observability

**Prerequisites**  
- Page cache, dirty pages, writeback, and `fsync` (Session 07)  
- ext4 allocation and the mapping from file offset to device blocks (Session 06)  
- Block devices and the path from filename to storage (Session 05)  
- VFS and mount points (Session 04)

**What this session unlocks**  
The ability to follow an I/O request through the block layer, understand why requests are merged and reordered, and use basic tools to measure latency, throughput, and queue behavior. This is required for performance troubleshooting, capacity planning, and later work on storage for containers, databases, and AI workloads.

## 2. Why This Session Exists

You now understand that most writes stop in the page cache and that writeback later turns dirty pages into I/O against a block device.  

The next question is: what happens inside the kernel after the filesystem has decided which device blocks must be read or written?  

That path—the **block layer**—is responsible for:

- packaging pages into I/O requests  
- merging adjacent requests  
- ordering and scheduling them (the classic “elevator” algorithms and their modern successors)  
- delivering them to the device driver  

Without this knowledge, `iostat`, `iotop`, `%util`, `await`, and queue-depth numbers remain opaque. You also cannot reason about why sequential workloads are fast, why random workloads are slow on HDDs, or why an NVMe device behaves differently from a SATA SSD.

This session completes the vertical path from a process `write()` down to the storage device and gives you the first practical observability tools for that path.

## 3. Learning Objectives

By the end of this session you will be able to:

- Trace a buffered write from the page cache through the block layer to the device driver at a conceptual level.  
- Explain request merging, plugging, and why sequential I/O achieves higher throughput than random I/O.  
- Describe the historical role of I/O elevators / schedulers and the modern multi-queue (blk-mq) design.  
- Distinguish logical block addressing, request queues, and device command queues.  
- Use `iostat`, `iotop`, `/proc/diskstats`, and selected `/sys/block` files to measure throughput, latency, utilization, and queue depth.  
- Interpret the most important columns of `iostat -x` output and relate them to application symptoms.  
- Predict the observable effect of sequential versus random workloads and of increasing queue depth on different device types.

## 4. Prerequisite Concepts

You already know:

- Dirty pages are written back by flusher threads or by explicit `fsync`.  
- The filesystem turns file offsets into device block numbers.  
- A block device is a linear array of fixed-size blocks presented by a driver.  
- The page cache and the block layer are separate kernel subsystems that hand work to each other.

## 5. Mental Model

```
Page cache / filesystem
        │  (dirty pages or read misses)
        ▼
┌─────────────────────────────────────────────┐
│              Block layer                     │
│  bio → request → request queue / blk-mq     │
│  merging, scheduling / dispatch             │
└──────────────────────┬──────────────────────┘
                       │
                       ▼
              Device driver
                       │
                       ▼
              Hardware queue(s)
                       │
                       ▼
                 Storage media
```

For a single process doing buffered I/O the full vertical path is:

```
write() → page cache → (later) writeback
       → filesystem mapping
       → bio / request
       → block layer (merge / schedule)
       → driver
       → device
```

## 6. Core Concept

### From pages to bios and requests

When the kernel decides to perform real device I/O it constructs one or more **bio** (block I/O) structures. A bio describes a contiguous range of pages and the device blocks they correspond to. Bios are then assembled into **requests**.

The block layer may:

- **merge** adjacent or overlapping requests into larger ones (reduces per-command overhead and improves sequential throughput)  
- **split** requests that exceed device limits  
- hold requests briefly (**plugging**) so that more merging opportunities appear  

### I/O schedulers / elevators (historical and modern view)

On single-queue devices the kernel used an **I/O elevator**—an algorithm that sorted and merged requests, typically by sector number, to reduce disk-head movement on rotating media. Classic examples were `cfq`, `deadline`, and `noop`.

Modern Linux uses **blk-mq** (multi-queue block layer). Each CPU (or hardware queue) has its own software queue; the elevator concept is either simplified or delegated to the device’s own command scheduler (especially on NVMe). The old single-queue elevators still exist for legacy devices but are no longer the centre of the design.

What still matters for an engineer:

- Merging remains important.  
- Queue depth (how many commands are in flight) strongly affects throughput and latency.  
- Device characteristics (HDD vs SATA SSD vs NVMe) dominate observed behaviour.

### Basic performance quantities

| Quantity | Meaning |
|----------|---------|
| Throughput | Bytes transferred per unit time (MB/s or GB/s) |
| IOPS | I/O operations per second |
| Latency / await | Average time from request submission to completion |
| Service time | Time the device actually spent on the request |
| Queue depth | Number of outstanding requests |
| %util | Approximate fraction of time the device had work |

These are the numbers `iostat -x` and similar tools expose.

## 7. Break It Into the Smallest Important Pieces

### 7.1 bio
- Basic unit of block I/O in the kernel.  
- Describes a memory range and the corresponding device sectors.

### 7.2 request
- Higher-level object that may contain multiple bios.  
- What the scheduler / dispatcher works with.

### 7.3 Request queue / blk-mq hardware and software queues
- Software queues collect work from CPUs.  
- Hardware queues map to device submission queues.  
- Multi-queue design removes a single global lock bottleneck.

### 7.4 Merging
- Adjacent requests are combined to form larger sequential transfers.  
- Dramatically improves efficiency on both HDDs and SSDs.

### 7.5 Plugging
- Temporary deferral of dispatch so that more requests can arrive and be merged.

### 7.6 Queue depth
- How many commands the device (or the kernel queue) has outstanding.  
- Higher depth usually increases throughput up to the device’s saturation point and increases latency.

### 7.7 I/O scheduler policy (where still visible)
- For devices that still expose a scheduler: `none`, `mq-deadline`, `bfq`, etc.  
- Visible and selectable under `/sys/block/<dev>/queue/scheduler`.

## 8. What Linux Is Actually Doing

**Writeback of a dirty page (simplified)**
```
Flusher or fsync
    → collect dirty pages for an inode
    → filesystem creates bios (page → device blocks)
    → bios submitted to the block layer
    → possible merge into existing requests
    → request placed on a blk-mq software queue
    → dispatcher moves request to a hardware queue
    → driver programs the device (doorbell, command)
    → device completes → interrupt / polling
    → completion callbacks mark pages clean, wake waiters
```

**Read miss**
```
read() → page cache miss
    → filesystem allocates pages and issues read bios
    → same block-layer path
    → on completion data is in the page cache and copied to user space
```

## 9. Commands and Tools

| Command / Path | Purpose |
|----------------|---------|
| `iostat -xz 1` | Extended device statistics once per second (most important daily tool) |
| `iotop` / `iotop -o` | Per-process I/O view |
| `cat /proc/diskstats` | Raw kernel counters (what `iostat` parses) |
| `ls /sys/block/<dev>/queue/` | Queue parameters, scheduler, maximum sizes |
| `cat /sys/block/<dev>/queue/scheduler` | Current and available I/O schedulers |
| `cat /sys/block/<dev>/queue/nr_requests` | Software queue depth limit |
| `cat /sys/block/<dev>/queue/max_hw_sectors_kb` | Device-imposed size limit |
| `blktrace` / `btt` (advanced) | Detailed block-layer tracing (awareness only at this stage) |

Essential `iostat -x` columns to understand first:

- `r/s`, `w/s` — reads/writes per second  
- `rkB/s`, `wkB/s` — throughput  
- `await` — average overall latency  
- `raread` / `wawait` (newer versions) — read/write latency  
- `%util` — device utilisation approximation  

## 10. Hands-On Lab

**Objective**  
Generate controlled sequential and random I/O, observe the difference in throughput and latency, and relate the numbers to block-layer behaviour.

**Setup**  
Ubuntu VirtualBox VM. Prefer a disk that is not the root filesystem if you have a second virtual disk; otherwise work carefully on a large test file on the root filesystem.

```bash
mkdir -p ~/iolab
cd ~/iolab
# Create a test file large enough to avoid pure cache hits (adjust size to your VM RAM)
dd if=/dev/zero of=testfile bs=1M count=2048 status=progress
sync
sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'
```

**Steps**

1. Observe idle baseline:
```bash
iostat -xz 1
# Let a few intervals pass; note near-zero activity
```

2. Sequential write (buffered, then force durability):
```bash
# Terminal 1
iostat -xz 1

# Terminal 2
dd if=/dev/zero of=seq.dat bs=1M count=1024 conv=fsync
```
Note throughput (`wkB/s`), `await`, and `%util`.

3. Sequential read (after dropping caches):
```bash
sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'
dd if=seq.dat of=/dev/null bs=1M
```
Again watch `iostat`.

4. Random I/O (simple approximation with small block size and `random` if available, or use `fio` if installed):
```bash
# If fio is available (sudo apt install fio)
fio --name=randread --filename=testfile --rw=randread --bs=4k --size=512M --iodepth=16 --runtime=30 --time_based --group_reporting
# Watch iostat in another terminal
```
Compare IOPS, throughput, and latency with the sequential runs.

5. Inspect queue settings for the device that holds your test files:
```bash
DEV=$(findmnt -n -o SOURCE -T . | sed 's/[0-9]*$//')   # approximate whole-disk name
ls /sys/block/$(basename $DEV)/queue/
cat /sys/block/$(basename $DEV)/queue/scheduler
cat /sys/block/$(basename $DEV)/queue/nr_requests
```

6. Observe merging effect indirectly: sequential large-block I/O should show far higher throughput than 4 KiB random I/O on the same device.

**Verification**  
You must be able to show:

- Sequential throughput substantially higher than random 4 KiB throughput.  
- Non-zero `await` and `%util` while I/O is in progress.  
- The current I/O scheduler name for your device.

**Cleanup**
```bash
rm -f ~/iolab/*
sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'
```

## 11. Investigation Lab

**Scenario**  
An application that used to sustain 400 MB/s sequential reads now reports only ~40 MB/s. `top` shows low CPU usage. `iostat` shows the device at ~100 %util with high `await` and modest throughput. The workload is still sequential according to the application owner.

**Objective**  
Form a short list of plausible block-layer or device causes and state what evidence would confirm or eliminate each.

**Available tools**  
`iostat -xz`, `iotop`, `/proc/diskstats`, `/sys/block/.../queue`, `lsblk`, application configuration, `dmesg`

**Initial clues**  
- Throughput dropped by an order of magnitude.  
- `%util` is high, `await` is high.  
- CPU is not saturated.  
- Recent changes: new VM host, storage migration, or increased concurrency from other tenants.

**Investigation questions**  
1. What does near-100 % `%util` together with low throughput suggest about the device?  
2. How would you distinguish “the device is genuinely slow” from “the workload has become random”?  
3. What queue-related parameters could affect observed performance?  
4. What external (hypervisor / cloud volume / shared storage) factors could produce the same numbers?

Work the questions before reading the solution.

**Solution**  
High `%util` + low throughput + high latency usually means the device is saturated but each I/O is expensive (small random I/Os, or a slow underlying medium).  

Evidence steps:

- Confirm I/O pattern size and randomness (`fio` controlled test, or `blktrace` if available).  
- Compare sequential `dd`/`fio` numbers against the application’s numbers.  
- Check whether the volume was moved to a slower storage tier or is now shared with noisy neighbours.  
- Inspect queue depth and scheduler.  
- Look for kernel messages indicating medium errors or throttling.

In cloud environments the most common root cause is a change in the underlying volume performance class or contention; the block-layer tools tell you the symptom is real and device-side.

## 12. Production Failure Scenario

**Incident**  
A latency-sensitive service begins missing SLOs. Application metrics show elevated database read latency. On the database host `iostat` reports the data volume at 95–100 %util, `await` of several tens of milliseconds, and modest IOPS. The same host previously ran comfortably at 30–40 %util.

**Systematic troubleshooting**

1. **Observation**  
   Application latency ↑, device `%util` ≈ 100 %, `await` ↑.

2. **Hypothesis set**  
   - Workload increased (more queries, larger working set).  
   - I/O pattern became more random (index or cache behaviour change).  
   - Underlying storage performance degraded (cloud throttling, failing disk, shared array contention).  
   - Queue depth or scheduler misconfiguration.  
   - Page-cache hit rate collapsed (memory pressure).

3. **Evidence**  
   ```bash
   iostat -xz 1
   iotop -o
   free -h
   grep -E 'Dirty|Cached|MemAvailable' /proc/meminfo
   # Application and database metrics (queries/s, cache hit rate)
   # Cloud provider volume metrics if applicable
   ```

4. **Isolation**  
   - Controlled sequential versus random tests with `fio` to characterise the device.  
   - Compare current IOPS/latency with historical baselines.  
   - Check for memory pressure that would force reads to hit the device.

5. **Resolution examples**  
   - Add capacity or move to a higher-performance volume tier.  
   - Restore cache effectiveness (more RAM, query tuning).  
   - Reduce concurrent random I/O (batching, indexing, connection limits).  
   - Fix a runaway process that is generating unexpected I/O.

The block-layer numbers do not by themselves name the root cause, but they tell you the problem is real, device-side, and quantify how bad it is—exactly the starting point of a proper performance investigation.

## 13. Connection to Previous Linux Knowledge

- Writeback from the page cache (Session 07) is the most common producer of the bios that enter the block layer.  
- Filesystem block mapping (Session 06) determines the device sectors that appear in those bios.  
- The block device (Session 05) is the target that the driver ultimately programs.  
- The VFS and inode objects remain the higher-level identities; the block layer only sees device addresses and memory pages.

## 14. Connection to Future Infrastructure

- **Containers and Kubernetes**: storage performance isolation is weak on shared devices; noisy-neighbour I/O appears exactly as elevated `await` and `%util` on the node.  
- **Databases**: I/O schedulers, queue depths, and the difference between sequential and random patterns dominate tuning.  
- **Cloud volumes**: provisioned IOPS and throughput limits are enforced below the guest block layer; `iostat` is how you see the throttling.  
- **AI infrastructure**:  
  - Dataset loading and checkpointing are usually sequential and benefit from high throughput and merging.  
  - Random small reads (feature stores, many small samples) expose IOPS and latency limits.  
  - GPU nodes often share storage; block-layer observability is the first tool when training jobs slow down because of data starvation.  
- **Observability stacks**: `iostat` metrics, node-exporter disk metrics, and eBPF-based block-layer tracing all rest on the concepts in this session.

## 15. Engineering Questions

1. Why does merging adjacent requests improve throughput?  
2. What does `%util` in `iostat` approximately represent, and what are its limitations?  
3. Why is sequential I/O usually much faster than random 4 KiB I/O on the same device?  
4. What is queue depth and how does it affect both throughput and latency?  
5. How does the multi-queue block layer (blk-mq) differ from the older single-queue design?  
6. A process is writing rapidly but `iostat` shows little device traffic. What layer is absorbing the writes?  
7. Why might increasing the number of concurrent random readers decrease overall throughput on an HDD but increase it on an NVMe SSD?  
8. What information does `/sys/block/<dev>/queue/scheduler` give you?  
9. How would you demonstrate that a performance problem is inside the block layer / device rather than in application CPU logic?

## 16. Practical Assignment

1. Using `fio` (install if necessary) or carefully constructed `dd` loops, produce three workloads on the same test file:  
   - sequential read  
   - sequential write  
   - 4 KiB random read  

2. For each workload capture `iostat -xz 1` output and summarise: throughput, IOPS, average latency, `%util`.  

3. Inspect the queue parameters of the underlying device and note the current scheduler.  

4. Write a short report that explains the observed differences using the concepts of merging, queue depth, and device characteristics.  

5. (Optional) Repeat one workload with a deliberately low `iodepth` and a high `iodepth` and comment on the change in throughput and latency.

## 17. Session Completion Test

Answer without notes.

**Conceptual**  
1. What is the role of the block layer between the page cache / filesystem and the device driver?  
2. What is request merging and why is it beneficial?

**System behavior**  
3. Why does a sequential `dd` usually achieve higher MB/s than a 4 KiB random read workload on the same disk?  
4. What does a high `%util` together with low throughput typically indicate?

**Command interpretation**  
5. In `iostat -x` output, what do `await` and `wkB/s` tell you?  
6. What does reading `/sys/block/<dev>/queue/scheduler` show?

**Troubleshooting**  
7. Application latency is high, CPU is low, and the data disk shows 100 %util. What is your first block-layer evidence-gathering step?

**Internal**  
8. Describe the high-level steps that turn a dirty page into a completed device write.

**Explain in your own words**  
9. Explain why modern Linux uses a multi-queue block layer rather than a single shared request queue.

## 18. Mastery Criteria

- **Basic understanding**: You can read basic `iostat -x` output and explain sequential versus random performance differences.  
- **Working understanding**: You can generate controlled workloads, measure throughput/latency/utilisation, inspect queue settings, and decide whether a problem is likely device-side.  
- **Strong understanding**: You can relate bio/request/merging/queue-depth concepts to observed numbers, discuss the shift from classic elevators to blk-mq, and use the block layer as the starting point of a systematic storage-performance investigation.

## 19. What I Should Now Be Able to Explain

- The path from writeback / read miss into the block layer  
- bios, requests, merging, and plugging at a conceptual level  
- Why sequential I/O outperforms random I/O  
- Queue depth and its effect on throughput and latency  
- Meaning of the principal `iostat -x` columns  
- How to inspect I/O scheduler and queue parameters under `/sys/block`  
- Difference between older single-queue elevators and modern blk-mq  
- How to recognise a saturated storage device from observability data

## 20. Next Session

**Next Session Number**  
SESSION 09  

**Next Session Title**  
Filesystem Usage, Disk Capacity, Inode Capacity, and Space Troubleshooting  

**Why it comes next**  
You have now followed data all the way from process buffers through the page cache, the filesystem, and the block layer to the device. The remaining practical filesystem skill is capacity management: understanding how space and inodes are consumed, how to locate what is using them, and how to diagnose the classic “disk full” and “but `du` doesn’t add up” incidents that appear constantly in production.
