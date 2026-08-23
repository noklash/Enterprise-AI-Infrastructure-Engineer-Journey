# Session 24 — Performance Fundamentals: CPU, Memory, Disk, and the Observation Loop

## 1. Position in the Curriculum

**Phase**  
PHASE 1 — Linux Systems Engineering

**Module**  
MODULE 4 — Performance and Troubleshooting

**Session**  
SESSION 24 — Performance Fundamentals: CPU, Memory, Disk, and the Observation Loop

**Prerequisites**  
- Processes, scheduling, and CPU time (earlier Linux internals)  
- Virtual memory, page cache, and writeback (Session 07)  
- Block I/O path and basic `iostat` (Session 08)  
- Filesystem capacity and journal-based service inspection (Sessions 09, 21)  
- Service lifecycle under systemd (Sessions 19–20)

**What this session unlocks**  
A systematic way to observe a Linux system’s primary resources—CPU, memory, and disk—and to turn raw numbers into hypotheses about bottlenecks. This is the foundation of all later performance work and of evidence-based incident response.

## 2. Why This Session Exists

You can install software, boot the system, supervise services, and read logs. The next operational question is: **is the system healthy, and if it is slow or overloaded, which resource is the constraint?**

Performance problems almost always present as one of:

- high latency  
- low throughput  
- timeouts / errors under load  
- “the box feels slow”  

Guessing the cause wastes time. The disciplined approach is:

1. **Observe** the four classic resources (CPU, memory, disk, network).  
2. **Identify** which resource is saturated or erroneous.  
3. **Hypothesise** a cause consistent with the evidence.  
4. **Test** the hypothesis with finer tools.  
5. **Fix** or mitigate, then **verify**.

This session establishes that observation loop and the core tools for CPU, memory, and disk. Network performance is introduced later with the networking module; here it is only acknowledged as the fourth resource.

## 3. Learning Objectives

By the end of this session you will be able to:

- Apply a consistent observation loop: resource → saturation / error / latency → hypothesis → deeper measurement.  
- Use `uptime`, `vmstat`, `mpstat`, `top`/`htop`, and `/proc/stat` to characterise CPU usage and pressure.  
- Use `free`, `vmstat`, `/proc/meminfo`, and pressure-stall information to characterise memory usage and reclaim pressure.  
- Use `iostat`, `df`, and basic disk latency metrics to characterise storage behaviour.  
- Distinguish utilisation from saturation and from errors.  
- Interpret load average in the context of runnable and uninterruptible tasks.  
- Form a first hypothesis about whether a slow system is primarily CPU-bound, memory-bound, or disk-bound.

## 4. Prerequisite Concepts

You already know:

- Processes consume CPU time under the scheduler; runnable processes wait for CPU.  
- Memory pressure leads to reclaim, swapping, and in extreme cases the OOM killer.  
- Dirty page writeback and the block layer determine when disk I/O occurs.  
- `iostat` exposes throughput, utilisation, and latency at the block layer.  
- Services and the journal give application-level symptoms that must be correlated with resource metrics.

## 5. Mental Model

```
Symptom (latency, timeouts, “slow”)
        │
        ▼
┌─────────────────────────────────────────────┐
│  Observation loop                            │
│                                              │
│  1. Check the four resources at a glance     │
│     CPU · Memory · Disk · Network            │
│  2. For the stressed resource:               │
│        utilisation · saturation · errors     │
│  3. Form a hypothesis                        │
│  4. Measure deeper (per-process, per-device) │
│  5. Act and verify                           │
└─────────────────────────────────────────────┘
```

**Utilisation** — how busy the resource is (e.g. %CPU, disk %util).  
**Saturation** — how much work is waiting (run queue, paging, disk queue).  
**Errors** — failed operations (I/O errors, OOM events, allocation failures).

A resource can be highly utilised without saturation (healthy high load) or moderately utilised with heavy saturation (pathological queueing). Both numbers matter.

## 6. Core Concept

### The four resources

| Resource | Primary questions |
|----------|-------------------|
| CPU | Is there enough time? Are tasks waiting for CPU? Is the time user, system, iowait, or steal? |
| Memory | Is free memory adequate? Is the system reclaiming heavily? Is it swapping? Are there OOM events? |
| Disk | Is the device saturated? Are latencies high? Is the problem throughput or IOPS? Capacity full? |
| Network | (Later module) Bandwidth, packet rate, errors, retransmits, connection limits |

### Load average

The three numbers from `uptime` / `w` / `top` are exponential moving averages (1, 5, 15 minutes) of the number of tasks in:

- runnable state (waiting for CPU), plus  
- uninterruptible sleep (typically waiting for disk)

Load average is **not** “CPU percentage.” A load average of 4 on a 4-CPU host can be healthy; the same load on a 1-CPU host means sustained queueing. Always interpret load relative to the number of CPUs and together with other metrics.

### CPU view

Key breakdowns:

- **user** — time in user-space code  
- **system** — time in the kernel  
- **iowait** — idle while I/O is outstanding (hint of storage or NFS pressure)  
- **steal** — time a virtual CPU waited because the hypervisor scheduled someone else  
- **idle** — truly idle  

Per-CPU views matter on multi-core systems: one hot CPU and three idle CPUs can still produce high latency for a single-threaded workload.

### Memory view

- **Total / used / free / available** — `available` (from `/proc/meminfo`) is the better “how much can new workloads use” estimate.  
- **Buffers / cache** — page cache and related; reclaimable under pressure.  
- **Swap used / swap activity** — ongoing swap-in/out is a strong signal of memory pressure.  
- **PSI (Pressure Stall Information)** — modern kernels expose the fraction of time tasks were stalled on memory (and CPU/IO); very useful when available.

### Disk view

From Session 08 you already know:

- throughput (kB/s)  
- IOPS  
- average wait / await (latency)  
- %util  

Capacity (`df`) is a separate dimension: a full filesystem causes application errors even when the device is not saturated.

## 7. Break It Into the Smallest Important Pieces

### 7.1 Utilisation
- Fraction of capacity currently in use.

### 7.2 Saturation
- Queueing or waiting for the resource.

### 7.3 Errors
- Hard failures associated with the resource.

### 7.4 Load average
- Average count of runnable + uninterruptible tasks.

### 7.5 CPU states
- user, system, iowait, steal, idle, irq/softirq.

### 7.6 Memory pressure signals
- Low `MemAvailable`, swap activity, reclaim, PSI memory, OOM killer logs.

### 7.7 Disk pressure signals
- High await, high %util, growing queue length, I/O errors in the journal.

### 7.8 Observation order
- Glance all four → focus on the outlier → deeper per-process or per-device tools → hypothesis → test.

## 8. What Linux Is Actually Doing

**Sampling CPU**
```
/proc/stat  →  cumulative jiffies per CPU state
mpstat / vmstat / top  →  differences over an interval → percentages
```

**Sampling memory**
```
/proc/meminfo  →  kernel counters for pages, reclaim, swap, …
free / vmstat  →  human-readable presentation of those counters
```

**Sampling disk**
```
/proc/diskstats  →  cumulative I/O counts and times
iostat  →  interval deltas → tps, kB/s, await, %util
```

Tools do not create new data; they present kernel counters. Understanding the counters prevents misreading the tools.

## 9. Commands and Tools

| Command | Primary use |
|---------|-------------|
| `uptime` / `w` | Load average + who is logged in |
| `vmstat 1` | Compact CPU, memory, swap, IO overview once per second |
| `mpstat -P ALL 1` | Per-CPU breakdown |
| `top` / `htop` | Interactive process-level CPU and memory |
| `free -h` | Memory and swap summary |
| `cat /proc/meminfo` | Detailed memory counters |
| `iostat -xz 1` | Extended disk stats once per second |
| `df -h` | Filesystem capacity |
| `ps` / `pidstat` | Process-level CPU and I/O (where available) |
| `dmesg` / `journalctl -k` | OOM killer and hardware/driver errors |
| `pressure` stall files under `/proc/pressure/` (if present) | PSI for CPU, memory, IO |

A practical first pass on an unfamiliar sick host:

```bash
uptime
vmstat 1 5
free -h
iostat -xz 1 5
df -h
```

## 10. Hands-On Lab

**Objective**  
Generate controlled CPU, memory, and disk load and observe the corresponding signals with the standard tools.

**Setup**  
Lab VM with enough CPU and disk to see clear signals. Install `sysbench` or use simple built-in stress methods if packages are limited.

```bash
mkdir -p ~/perf-lab
cd ~/perf-lab
# Optional tools
sudo apt update
sudo apt install -y sysbench htop iotop 2>/dev/null || true
```

**Steps**

1. Baseline (idle system):
```bash
uptime
vmstat 1 5
free -h
iostat -xz 1 3
```

2. CPU load:
```bash
# Terminal 1 – watch
mpstat -P ALL 1
# Terminal 2 – burn one core
dd if=/dev/zero of=/dev/null &
# or: sysbench cpu --threads=1 --time=30 run
# Observe user% rise on one CPU; load average trend upward
kill %1 2>/dev/null
```

3. Memory pressure (careful — do not lock up the VM):
```bash
free -h
# Allocate a large chunk of memory for a short time (adjust size to your VM)
# Example with Python:
python3 -c "
import time
print('allocating')
x = bytearray(512*1024*1024)
time.sleep(15)
print('done')
" &
sleep 2
free -h
vmstat 1 5
# Watch cache/available change; avoid sizes near total RAM
```

4. Disk load:
```bash
# Terminal 1
iostat -xz 1
# Terminal 2 – sequential write
dd if=/dev/zero of=./bigfile bs=1M count=1024 conv=fdatasync
# Observe wkB/s, await, %util
rm -f ./bigfile
```

5. Combined glance workflow:
```bash
# Run a mixed load briefly and practice the five-command first pass
uptime; vmstat 1 3; free -h; iostat -xz 1 3; df -h
```

6. (Optional) Pressure stall information:
```bash
cat /proc/pressure/cpu 2>/dev/null
cat /proc/pressure/memory 2>/dev/null
cat /proc/pressure/io 2>/dev/null
```

**Verification**  
You must have observed:

- Clear rise in user CPU% under a CPU burn.  
- Change in `free` / `vmstat` memory fields under allocation.  
- Non-zero disk throughput and utilisation under a write.  
- Ability to run the standard first-pass command set and narrate what each line means.

**Cleanup**
```bash
rm -f ~/perf-lab/bigfile
killall dd 2>/dev/null
rm -rf ~/perf-lab
```

## 11. Investigation Lab

**Scenario**  
Users report that an application is “slow.” The application host shows a load average of 12 on a 4-vCPU machine. You must decide whether the primary constraint is CPU, memory, or disk before changing any application config.

**Objective**  
Using only observation tools, classify the bottleneck class and cite the evidence.

**Available tools**  
`uptime`, `vmstat`, `mpstat`, `free`, `iostat`, `top`, `journalctl`, `df`

**Initial clues**  
- Load average ≈ 12, nproc = 4.  
- Application latency is elevated.  
- No recent deployment (according to the report).

**Investigation questions**  
1. What does a load average three times the CPU count suggest, and what does it *not* yet prove?  
2. Which `vmstat` columns distinguish CPU queueing from memory pressure and from disk wait?  
3. How would high `iowait` plus high disk `await` change your hypothesis compared with high `us`/`sy` and low `iowait`?  
4. What single additional signal would confirm memory pressure?

Work the questions before reading the solution.

**Solution**  
Load average 12 on 4 CPUs means sustained queueing of runnable or uninterruptible tasks, but not *which* resource.

```bash
vmstat 1 10
mpstat -P ALL 1 5
free -h
iostat -xz 1 5
```
Interpretation patterns:

- High `r` (runnable), high `us`/`sy`, low `wa` → CPU-bound.  
- High `wa`, high disk `await`/%util → disk-bound (or remote FS).  
- Growing swap activity, low `MemAvailable`, PSI memory, or OOM entries → memory-bound.  

Cite the specific counters in your conclusion. Only after classifying the resource do you zoom into per-process tools (`top`, `pidstat`, `iotop`) or application logs.

## 12. Production Failure Scenario

**Incident**  
A payment API’s p99 latency jumps from 40 ms to 2 s. The app tier is auto-scaled on CPU utilisation, which remains ~55 %. On-call confirms the database host shows load average well above CPU count and elevated disk await.

**Systematic troubleshooting**

1. **Observation**  
   App latency up; app CPU not saturated; database host load and disk latency up.

2. **Hypothesis**  
   Database storage (or query-driven I/O) is the bottleneck; app tier is waiting on the DB.

3. **Evidence**  
   ```bash
   # On DB host
   uptime
   vmstat 1 5
   iostat -xz 1 5
   free -h
   journalctl -u postgresql -b   # or mysql, etc.
   ```
   Confirm high `await`, high `%util` or queue depth, and application wait events / slow-query logs.

4. **Actions** (examples)  
   - Reduce pathological queries, add missing index, or increase IOPS/throughput of the volume.  
   - Short-term: shed load, enable caching, or scale a read replica if the workload allows.  

5. **Verification**  
   Disk `await` and application p99 return to baseline.  

6. **Prevention**  
   Monitor disk latency and saturation on the DB tier, not only CPU on the app tier. Alert on resource saturation, not only on utilisation.

## 13. Connection to Previous Linux Knowledge

- CPU counters reflect the scheduler and process states you studied in the process module.  
- Memory metrics are the observable face of the virtual memory system and page cache (Session 07).  
- Disk metrics are produced by the block layer path (Session 08).  
- Filesystem full conditions (`df`) are the capacity failures from Session 09.  
- Service-level symptoms appear in the journal (Session 21) and must be correlated with resource metrics rather than treated in isolation.

## 14. Connection to Future Infrastructure

- **Containers and Kubernetes**: the same four resources appear as node conditions, cgroup limits, and pod metrics; the observation loop is identical, only the tools change (`kubectl top`, metrics-server, node-exporter).  
- **Cloud**: “CPU credit exhaustion,” network burst limits, and provisioned IOPS are the same utilisation/saturation/error ideas under different names.  
- **Observability stacks**: Prometheus node exporters, Grafana dashboards, and alerting rules are largely continuous versions of the glance you perform with `vmstat`/`iostat`.  
- **AI infrastructure**: GPU nodes add a fifth resource (accelerator utilisation, memory, and interconnect), but training stalls are still often caused by classic CPU, host memory, or disk/network bottlenecks on the data path. The observation loop remains the starting point.

## 15. Engineering Questions

1. What is the difference between utilisation and saturation?  
2. Why is load average alone insufficient to declare a CPU bottleneck?  
3. What does a high `iowait` percentage suggest?  
4. Why is `MemAvailable` often more useful than `MemFree`?  
5. What disk metrics indicate saturation rather than mere activity?  
6. In what order should you glance at resources on an unfamiliar slow host?  
7. How can a system show modest CPU utilisation yet still deliver poor application latency?  
8. What evidence would make you suspect memory pressure over CPU pressure?  
9. Why should application-level latency and host-level resource metrics be examined together?

## 16. Practical Assignment

1. On your lab system, create a one-page “first five minutes” runbook: exact commands, what each output line means, and what pattern pushes you toward CPU vs memory vs disk.  

2. Generate three separate load scenarios (CPU, memory, disk). For each, capture the critical tool output and write a two-sentence diagnosis as if you were filing an incident note.  

3. Correlate a service-level symptom (e.g. slow responses from a local nginx or a synthetic app) with resource metrics and journal entries. Document the chain: user symptom → resource evidence → process evidence.  

4. If `/proc/pressure` exists on your kernel, compare PSI readings under idle and under each load scenario; note whether they make the bottleneck clearer.

## 17. Session Completion Test

Answer without notes.

**Conceptual**  
1. Define utilisation, saturation, and errors as they apply to system resources.  
2. What does load average measure?

**System behavior**  
3. Load average is 8 on a 2-CPU host while `vmstat` shows high `wa` and `iostat` shows high `await`. What is the most likely resource class?  
4. Why might `top` show low idle time on one CPU while overall utilisation looks moderate?

**Command interpretation**  
5. What does `vmstat 1` help you see that a single snapshot of `top` does not?  
6. Which fields in `free -h` and `/proc/meminfo` best indicate whether new workloads can allocate memory?

**Troubleshooting**  
7. List the first commands you run when told “the server is slow,” and what you look for in each.

**Internal**  
8. Where do `iostat` and `mpstat` obtain the counters they display?

**Explain in your own words**  
9. Explain why high utilisation is not always a problem and why low utilisation does not always mean the system is healthy.

## 18. Mastery Criteria

- **Basic understanding**: You can run the standard glance tools and identify obviously high CPU, memory, or disk activity.  
- **Working understanding**: You can separate utilisation from saturation, interpret load average in context, and form a defensible first hypothesis about the constrained resource.  
- **Strong understanding**: You can run a full observation loop under time pressure, correlate application symptoms with resource evidence, and choose the next deeper tool based on the bottleneck class.

## 19. What I Should Now Be Able to Explain

- The observation loop (resource → utilisation/saturation/errors → hypothesis → deeper measure)  
- Meaning of load average relative to CPU count  
- CPU time breakdown (user, system, iowait, steal, idle)  
- Key memory health signals (`MemAvailable`, swap activity, pressure, OOM)  
- Key disk health signals (await, %util, throughput vs IOPS, capacity)  
- Standard first-pass command set and how to read it  
- Why application latency and host metrics must be correlated  

## 20. Next Session

**Next Session Number**  
SESSION 25  

**Next Session Title**  
CPU Performance in Depth: Scheduling, Saturation, and Per-Process Analysis  

**Why it comes next**  
You now have a system-wide observation loop. The next session focuses on the CPU resource in depth—scheduler run queues, per-process and per-thread analysis, softirq load, steal time in virtualised environments, and how to confirm that an application is actually CPU-bound rather than waiting on something else.
