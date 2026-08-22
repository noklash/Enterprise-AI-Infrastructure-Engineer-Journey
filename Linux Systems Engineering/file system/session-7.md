# Session 07 — Page Cache, Writeback, and the Relationship Between Memory and Disk

## 1. Position in the Curriculum

**Phase**  
PHASE 1 — Linux Systems Engineering

**Module**  
MODULE 1 — Filesystems

**Session**  
SESSION 07 — Page Cache, Writeback, and the Relationship Between Memory and Disk

**Prerequisites**  
- Inodes, path resolution, hard/symbolic links (Sessions 01–03)  
- VFS and mount points (Session 04)  
- Block devices and the path from filename to storage (Session 05)  
- ext4 on-disk layout, inodes, and allocation (Session 06)  
- Virtual memory and process address spaces (earlier Linux internals work)

**What this session unlocks**  
Understanding of why most reads and writes do not touch the storage device immediately, how the kernel decides when to move data between memory and disk, and what `fsync`, `O_SYNC`, and `O_DIRECT` actually guarantee. This is required for correct durability reasoning, performance analysis, and later topics such as database tuning, container storage, and AI checkpointing.

## 2. Why This Session Exists

You now know how a filesystem such as ext4 maps a file’s logical offsets to block-device blocks.  

If every `read()` and `write()` went synchronously to the device, systems would be unusably slow. The kernel therefore interposes a large, unified cache—the **page cache**—between processes and the block layer.  

Almost every ordinary file I/O operation interacts with the page cache first. Until you understand:

- what lives in the page cache,  
- when dirty pages are written back,  
- what `fsync()` forces, and  
- how memory pressure affects I/O,

you cannot correctly reason about durability, latency, throughput, or the meaning of tools such as `iostat`, `vmtouch`, or `/proc/meminfo`.

This session connects the virtual-memory knowledge you already possess to the filesystem and block-device knowledge you have just built.

## 3. Learning Objectives

By the end of this session you will be able to:

- Explain the role of the page cache as the kernel’s unified cache for file data and (most) metadata.  
- Describe the difference between clean and dirty pages and the conditions that cause writeback.  
- Show how a `write()` normally returns after updating memory, not after data reaches stable storage.  
- Demonstrate the effect of `fsync()`, `fdatasync()`, and `sync` on durability.  
- Contrast buffered I/O, `O_SYNC`/`O_DSYNC`, and `O_DIRECT`.  
- Use `/proc/meminfo`, `vmtouch`, `pcstat` (or equivalent), `iostat`, and `/proc/<pid>/smaps` to observe cache behavior.  
- Predict what happens to dirty data under memory pressure and after a crash.  
- Relate page-cache pages back to the inode and address-space objects used by the VFS.

## 4. Prerequisite Concepts

You already understand:

- Processes have virtual address spaces; the kernel manages physical pages.  
- An inode identifies a file; the filesystem maps file offsets to device blocks.  
- The VFS routes I/O to the correct filesystem implementation.  
- Block devices are the final target of storage I/O.  
- Delayed allocation in ext4 means physical blocks may be chosen after `write()` returns.

## 5. Mental Model

```
Process address space
        │
        │  read() / write()
        ▼
┌───────────────────────────────────────────┐
│              Page Cache                    │
│  (physical pages indexed by inode+offset) │
│                                           │
│   clean pages ◄──── read from disk        │
│   dirty pages ────► writeback to disk     │
└───────────────────────┬───────────────────┘
                        │
                        ▼
                 Filesystem + Block layer
                        │
                        ▼
                   Storage device
```

Key facts:

- The page cache is addressed by `(inode, offset)`, not by process.  
- Multiple processes reading the same file share the same cached pages.  
- A successful `write()` usually only marks pages dirty; the device is updated later.  
- Durability requires an explicit transfer of dirty data (and often metadata) to stable storage.

## 6. Core Concept

### The page cache

The **page cache** is the kernel’s unified cache of file contents (and some metadata) held in physical memory (RAM). Each cached unit is a page (normally 4 KiB on x86_64) that belongs to the address space of an inode.

When a process reads a file:

1. The kernel looks for the required pages in the page cache.  
2. If present (cache hit), data is copied to the process’s buffer.  
3. If absent (cache miss), the kernel issues I/O to the filesystem/block layer, installs the pages in the cache, then copies the data.

When a process writes a file:

1. The kernel locates or allocates pages in the page cache that cover the written range.  
2. Data is copied into those pages and the pages are marked **dirty**.  
3. The `write()` system call usually returns at this point.  
4. At a later time the kernel writes the dirty pages back to the storage device (**writeback**).

### Dirty pages and writeback

A page is **dirty** when its in-memory contents are newer than what is on disk. Writeback occurs under several conditions:

- Periodic background writeback (controlled by dirty-ratio and expire-time settings)  
- Memory pressure (the kernel needs free pages)  
- Explicit requests: `fsync()`, `fdatasync()`, `sync()`, `syncfs()`, `msync()`  
- Certain filesystem operations that require metadata consistency  

After writeback completes successfully the page becomes clean again (unless it is re-dirtied).

### Durability primitives

| Interface | What it guarantees (simplified) |
|-----------|---------------------------------|
| `write()` only | Data is in the page cache (and possibly the filesystem journal’s in-memory structures). Not durable. |
| `fsync(fd)` | Data + metadata needed to retrieve that data for the file are on stable storage. |
| `fdatasync(fd)` | Data is on stable storage; some metadata (e.g. atime/mtime) may lag. |
| `sync()` / `syncfs()` | System-wide or filesystem-wide writeback of dirty data and metadata. |
| `O_SYNC` / `O_DSYNC` open flags | Each write is synchronous (stronger, higher latency). |
| `O_DIRECT` | I/O bypasses the page cache (with alignment requirements). |

After a crash or power loss, only data that reached stable storage is guaranteed to be present.

### Relationship to virtual memory

The same physical pages used by the page cache can be mapped into process address spaces via `mmap()`. In that case the page cache and the process’s page tables refer to the same pages; modifications through the mapping also dirty the cache. This is why `mmap` + `msync` is another path to durability.

## 7. Break It Into the Smallest Important Pieces

### 7.1 Address space (struct address_space)
- Kernel object attached to an inode.  
- Owns the set of cached pages for that inode and the tree/radix structure used to look them up by offset.

### 7.2 Page cache page
- A physical page frame whose contents are a slice of a file.  
- Tracked by flags: dirty, writeback, up-to-date, etc.

### 7.3 Clean versus dirty
- Clean: memory matches disk (or is a pure cache of disk).  
- Dirty: memory is newer; must be written back before the page can be discarded.

### 7.4 Writeback
- The process of transferring dirty pages to the block layer and eventually to the device.  
- Performed by background threads (e.g. flusher threads) and by explicit sync calls.

### 7.5 Dirty accounting and thresholds
- The kernel tracks dirty memory globally and per-process.  
- When dirty ratios are exceeded, writers may be throttled or forced to perform writeback themselves.

### 7.6 `fsync` path (high level)
```
fsync(fd)
    → look up open file description → inode
    → writeback all dirty pages for that inode
    → ensure required metadata is also on disk
    → issue barrier / flush to the device as needed
    → return only after the device acknowledges durability
```

### 7.7 `O_DIRECT`
- Bypasses the page cache for the I/O itself.  
- Requires the user buffer, offset, and size to satisfy alignment constraints.  
- Does not eliminate the need for careful ordering if durability is required; it only removes the cache as an intermediary.

## 8. What Linux Is Actually Doing

**Buffered write (common case)**
```
write(fd, buf, count)
    → VFS
    → filesystem → address_space of the inode
    → locate/allocate pages in page cache
    → copy_from_user into those pages
    → mark pages dirty
    → (possibly) update inode size / timestamps in memory
    → return to user space
```

**Later writeback**
```
flusher thread or fsync
    → collect dirty pages for the inode / filesystem
    → filesystem converts pages to block I/O (bio)
    → block layer → device
    → on completion: pages marked clean
```

**Crash**
- Any page that was dirty and not yet written back is lost.  
- Journalled filesystems can still recover metadata consistency; user data that never left the page cache is gone.

## 9. Commands and Tools

| Command / Path | Purpose |
|----------------|---------|
| `grep -E '^(Cached\|Dirty\|Writeback\|MemAvailable)' /proc/meminfo` | System-wide page-cache and dirty statistics |
| `cat /proc/vmstat \| grep -E 'nr_dirty\|nr_writeback'` | More detailed VM counters |
| `iostat -xz 1` | Observe actual device I/O versus application writes |
| `vmtouch file` | Show how much of a file is currently resident in the page cache (install if needed) |
| `pcstat file` (or `fincore`) | Alternative residency tools |
| `dirty` ratio settings in `/proc/sys/vm/` | `dirty_ratio`, `dirty_background_ratio`, `dirty_expire_centisecs`, etc. |
| `strace -e trace=write,fsync,fdatasync …` | See when an application actually requests durability |
| `dd … of=file conv=fsync` or `of=file oflag=dsync` | Controlled experiments with durability |

## 10. Hands-On Lab

**Objective**  
Observe that ordinary writes update memory long before they update the device, and that `fsync` forces the transfer.

**Setup**
```bash
mkdir -p ~/pagecache-lab
cd ~/pagecache-lab
```

**Steps**

1. Create a test file and measure baseline device I/O (in one terminal):
```bash
# Terminal 1 – watch device traffic
iostat -xz 1
```

2. In another terminal, perform a large buffered write:
```bash
dd if=/dev/zero of=large.buf bs=1M count=512
# Observe: dd finishes quickly; iostat may show little or delayed activity
```

3. Force writeback and observe the I/O:
```bash
sync
# or
fsync large.buf          # if you have a tool that calls fsync; otherwise use
python3 -c 'import os; os.fsync(os.open("large.buf", os.O_RDONLY))'
# Watch iostat spike as dirty pages are written
```

4. Examine system-wide dirty state:
```bash
grep -E '^(Dirty|Writeback|Cached)' /proc/meminfo
```

5. Demonstrate residency (install `vmtouch` if available: `sudo apt install vmtouch`):
```bash
vmtouch large.buf
# Drop caches (requires root; affects whole system – use only on lab VM)
sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'
vmtouch large.buf          # should now show little residency
dd if=large.buf of=/dev/null bs=1M   # re-read → pages return to cache
vmtouch large.buf
```

6. Contrast with direct I/O (note alignment requirements):
```bash
dd if=/dev/zero of=large.direct bs=1M count=128 oflag=direct
# iostat should show I/O roughly in step with dd progress
```

7. Crash-safety thought experiment (do **not** pull power on a machine with wanted data):  
   After a large `dd` without `sync`/`fsync`, a crash would lose the data still held only in dirty pages. After `fsync`, the data is on stable storage (subject to device write-cache behavior).

**Verification**  
You must observe:

- A buffered write that completes while device I/O is still low or zero.  
- A subsequent `sync`/`fsync` that produces visible device traffic.  
- Page-cache residency changing after drop_caches and re-read.

**Cleanup**
```bash
rm -f ~/pagecache-lab/*
sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches' 2>/dev/null
```

## 11. Investigation Lab

**Scenario**  
A logging service calls `write()` for every message but never calls `fsync`. After a power outage the last several seconds (sometimes minutes) of log messages are missing, even though the application believed it had written them.

**Objective**  
Explain the data loss using page-cache and writeback concepts, and recommend concrete code and operational changes.

**Available tools / knowledge**  
`strace`, `/proc/meminfo`, `iostat`, application source or configuration, filesystem mount options.

**Initial clues**  
- Application logs show the messages were “written”.  
- No `fsync`/`fdatasync` appears in `strace` of the process.  
- Dirty page count was non-zero for long periods.

**Investigation questions**  
1. When does a successful `write()` guarantee that data is on stable storage?  
2. What kernel mechanisms eventually write the dirty log pages to disk in the absence of `fsync`?  
3. How would you prove that the application is not requesting durability?  
4. What are the latency versus durability trade-offs of adding `fsync` (or a periodic `fdatasync`) to the logging path?

Work the questions before reading the solution.

**Solution**  
`write()` only guarantees that the data is in the page cache (and that the kernel has accepted responsibility for it). Without an explicit `fsync`/`fdatasync` (or `O_SYNC`), the kernel writes the pages back according to its dirty-ratio and expire-time heuristics. A crash before writeback completes loses the data.

Proof:
```bash
strace -p <pid> -e trace=write,fsync,fdatasync,open,close
```
Absence of `fsync`/`fdatasync` is conclusive.

Remedies (choose according to durability requirements):

- Call `fdatasync` (or `fsync`) at defined durability points (e.g. after every N messages or every T seconds).  
- Open the log with `O_DSYNC` if every write must be durable (high cost).  
- Use a logging library that already provides configurable durability.  
- Accept some loss window and document it; rely on background writeback for performance.

## 12. Production Failure Scenario

**Incident**  
A database reports “commit succeeded” but after a host crash the most recent transactions are missing. The database is configured for asynchronous commits (or the application disabled its own `fsync` for speed). Disk monitoring shows that the device write cache was enabled and no barriers were issued.

**Systematic troubleshooting**

1. **Observation**  
   Application-level success, data missing after crash.

2. **Hypothesis hierarchy**  
   - Application or database skipped durability calls.  
   - Filesystem mounted with options that weaken barriers.  
   - Device write cache acknowledged data before it reached non-volatile media.

3. **Evidence**  
   ```bash
   # Application / DB configuration and strace
   # Mount options
   findmnt -T /var/lib/db -o OPTIONS
   # Device and write-cache state (example for a SCSI/SATA disk)
   hdparm -W /dev/sdX          # or vendor-specific tools
   # Kernel messages around the crash
   journalctl -b -1
   ```

4. **Resolution**  
   - Restore durability in the database/application (synchronous commit, correct `fsync` usage).  
   - Ensure mount options do not disable barriers (`barrier=1` / modern defaults).  
   - Disable or battery-back the device write cache, or use devices with power-loss protection.  
   - Verify with a controlled crash test on a non-production replica.

This class of failure is the direct consequence of misunderstanding the difference between “written to the kernel” and “on stable storage.”

## 13. Connection to Previous Linux Knowledge

- Virtual memory and page frames (earlier work) are the physical substrate of the page cache.  
- The inode’s address space (Session 06) is the object that owns the cached pages.  
- Delayed allocation in ext4 interacts with the page cache: pages can be dirty before physical blocks are even chosen.  
- The block layer and device (Session 05) are the ultimate destination of writeback.  
- `fsync` is a system call that crosses the same user/kernel boundary you studied earlier; it simply requests a stronger completion condition.

## 14. Connection to Future Infrastructure

- **Databases and message queues**: durability and latency are controlled by how they use the page cache and `fsync`.  
- **Containers**: the page cache is host-global (unless special drivers are used); noisy-neighbor cache thrashing is a real effect.  
- **Kubernetes**: emptyDir, hostPath, and many CSI volumes inherit the same cache and writeback behavior.  
- **AI infrastructure**:  
  - Model checkpoints written without proper `fsync` can be truncated or torn after a crash.  
  - Large sequential reads of datasets benefit enormously from the page cache; random small reads may not.  
  - GPU training nodes with heavy checkpoint traffic must size memory and storage, and tune dirty ratios, carefully.  
- **Observability**: `iostat`, cache-hit ratios, and dirty-page metrics are core signals for storage and application performance.

## 15. Engineering Questions

1. Why does a `write()` usually return before data reaches the storage device?  
2. What is the difference between a clean page and a dirty page?  
3. Under what conditions does the kernel perform writeback without any process calling `fsync`?  
4. What additional guarantee does `fsync` provide over a plain `write`?  
5. How can two processes reading the same file share the same physical pages?  
6. What does `O_DIRECT` change, and what does it not change, about durability?  
7. Why can dropping the page cache (`drop_caches`) make subsequent reads slower?  
8. How does memory pressure affect dirty-page writeback?  
9. Why might an application that calls `fsync` after every small write exhibit poor throughput?

## 16. Practical Assignment

1. Write a small program (C or Python) that:  
   - opens a file  
   - writes a known pattern  
   - optionally calls `fsync`  
   - prints a timestamp after the write and after the `fsync`  

2. Run it under `strace` and under concurrent `iostat` observation, both with and without the `fsync` call.  

3. Measure the latency difference and the timing of device I/O.  

4. With the page cache populated, use `vmtouch` (or equivalent) before and after `echo 3 > /proc/sys/vm/drop_caches` and explain the residency change.  

5. Write a short analysis of the durability versus latency trade-off you observed and when you would choose each approach in a production service.

## 17. Session Completion Test

Answer without notes.

**Conceptual**  
1. What is the page cache and why does it exist?  
2. When is data considered dirty?

**System behavior**  
3. A process calls `write()` and the call returns. Is the data guaranteed to survive a power failure?  
4. What forces dirty pages to the device even if no process calls `fsync`?

**Command interpretation**  
5. `/proc/meminfo` shows a large “Dirty” value. What does that mean operationally?  
6. `iostat` shows no device traffic while an application is writing heavily. What is the most likely explanation?

**Troubleshooting**  
7. After a crash, recent log messages are missing. The application never called `fsync`. Explain the loss using page-cache concepts.

**Internal**  
8. Describe the steps performed by a buffered `write()` up to the point the system call returns.

**Explain in your own words**  
9. Explain the difference between “the kernel has accepted the data” and “the data is on stable storage.”

## 18. Mastery Criteria

- **Basic understanding**: You know that the page cache sits between processes and disk and that `write()` is normally asynchronous with respect to the device.  
- **Working understanding**: You can observe dirty pages, force writeback with `fsync`/`sync`, contrast buffered and direct I/O, and explain post-crash data loss caused by missing durability calls.  
- **Strong understanding**: You can reason about dirty ratios, the interaction of delayed allocation with the page cache, the exact guarantees of `fsync` versus `fdatasync`, and the performance implications of different durability strategies.

## 19. What I Should Now Be Able to Explain

- Purpose and structure of the page cache  
- Clean versus dirty pages  
- Writeback triggers (background, memory pressure, explicit sync)  
- Guarantees (and non-guarantees) of `write`, `fsync`, `fdatasync`, `O_SYNC`, `O_DIRECT`  
- How the same cached pages are shared across processes  
- Relationship between the page cache, the inode’s address space, and virtual memory  
- How to observe cache residency and dirty state with standard tools  
- Why durability requires explicit action beyond a successful `write()`

## 20. Next Session

**Next Session Number**  
SESSION 08  

**Next Session Title**  
Disk I/O Path, Elevators, and Basic I/O Observability  

**Why it comes next**  
You now understand how data moves between process buffers, the page cache, and the filesystem’s block mapping. The next step is to follow the I/O into the block layer itself—request queues, merging, scheduling (elevators / I/O schedulers), and the tools used to observe latency and throughput at the device level. This completes the path from `write()` to the storage hardware and prepares you for systematic performance troubleshooting.
