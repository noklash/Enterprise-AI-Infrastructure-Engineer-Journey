# Session 09 — Filesystem Usage, Disk Capacity, Inode Capacity, and Space Troubleshooting

## 1. Position in the Curriculum

**Phase**  
PHASE 1 — Linux Systems Engineering

**Module**  
MODULE 1 — Filesystems

**Session**  
SESSION 09 — Filesystem Usage, Disk Capacity, Inode Capacity, and Space Troubleshooting

**Prerequisites**  
- Inodes, link counts, and deletion semantics (Sessions 01–03)  
- VFS, mounts, and the unified hierarchy (Session 04)  
- Block devices and the path to storage (Session 05)  
- ext4 layout and independent inode/data-block limits (Session 06)  
- Page cache, dirty pages, and writeback (Session 07)  
- Block-layer I/O path and basic observability (Session 08)

**What this session unlocks**  
The ability to answer, with evidence, the questions “where did the space go?”, “why is the filesystem full when `du` looks fine?”, and “why can’t I create a file when there is free space?”. These are among the most common production incidents involving filesystems.

## 2. Why This Session Exists

You now understand how data is stored, cached, and moved to devices.  

In production the most frequent filesystem emergencies are not exotic corruption cases; they are capacity problems:

- A filesystem reaches 100 % data-block usage.  
- A filesystem reaches 100 % inode usage while data blocks remain free.  
- `df` and `du` disagree dramatically.  
- Space disappears after a file is deleted (deleted-but-still-open).  
- A mount point is hidden by another mount, so data lands on the wrong filesystem.  

This session ties together every preceding filesystem concept into the practical skill of capacity diagnosis and recovery.

## 3. Learning Objectives

By the end of this session you will be able to:

- Explain the difference between filesystem data-block capacity and inode capacity.  
- Use `df`, `df -i`, `du`, `ncdu`/`dust`, and `find` to locate space and inode consumers.  
- Diagnose the four classic causes of “disk full” symptoms that `du` does not explain.  
- Recover space held by deleted-but-still-open files.  
- Detect data that has landed on the wrong filesystem because of a missing or incorrect mount.  
- Distinguish reserved blocks (root reservation) from ordinary free space.  
- Apply a systematic, evidence-based procedure for any capacity-related incident.

## 4. Prerequisite Concepts

You already know:

- Inodes are pre-allocated on ext4; data blocks and inodes are independent resources.  
- Unlinking a name decrements the link count; space is freed only when the link count is zero **and** no open file descriptions remain.  
- The page cache can hold dirty data that has not yet reached the device.  
- Mount points can hide the previous contents of a directory.  
- `df` reports filesystem-level statistics; `du` walks directory trees.

## 5. Mental Model

```
Filesystem capacity
├── Data blocks
│     ├── Used by visible files (what du sees)
│     ├── Used by deleted-but-still-open files (du misses)
│     ├── Used by hidden content under mount points
│     └── Reserved for root (ext4)
└── Inodes
      ├── Used by visible files/directories
      └── Exhausted independently of data blocks
```

`df` answers “how full is this filesystem?”  
`du` answers “how much space is reachable by walking this directory tree?”  
When the answers diverge, one of the mechanisms above is at work.

## 6. Core Concept

### Two independent resources

On ext4 (and most traditional filesystems):

- **Data blocks** store file contents.  
- **Inodes** store metadata and the map to those blocks.  

Either resource can be exhausted independently. The kernel returns the same error (`ENOSPC` – “No space left on device”) for both cases; the tools you use must distinguish them.

### Why `df` and `du` disagree

Common causes:

1. **Deleted but still open files**  
   Link count is zero, yet open file descriptions keep the inode and its blocks alive. `du` cannot see them; `df` still counts them.

2. **Data hidden under a mount point**  
   Files were written into a directory that was later used as a mount point (or the mount failed). The files exist on the parent filesystem and are invisible while the mount is active.

3. **Sparse files**  
   `du` reports actual allocated blocks; `ls -l` reports logical size. (Less often the cause of “full” incidents, but a source of confusion.)

4. **Reserved blocks**  
   ext4 reserves a percentage (default 5 %) for root. Ordinary users see a full filesystem while root can still write.

5. **Other filesystems mounted underneath**  
   `du` without `-x` crosses into other filesystems and can mislead.

### Systematic diagnosis order

When a filesystem is reported full:

1. Confirm with `df -h` **and** `df -i`.  
2. If inodes are exhausted → find and remove many small files, or re-create with a higher inode density.  
3. If data blocks are exhausted →  
   a. Compare `df` vs `du -x`.  
   b. Check for deleted-but-open files (`lsof +L1` / `lsof | grep deleted`).  
   c. Check for hidden data under mount points.  
   d. Look for large visible consumers with `du`.  
4. Act on the evidence; never guess.

## 7. Break It Into the Smallest Important Pieces

### 7.1 `df` (disk free)
- Reads filesystem superblock / statfs data.  
- Reports total, used, available blocks (and inodes with `-i`).  
- Does not walk directories; therefore sees deleted-open files and reserved blocks.

### 7.2 `du` (disk usage)
- Walks a directory tree and sums the space used by the inodes it reaches.  
- Does not see unlinked-but-open files.  
- Crosses filesystems unless `-x` is used.

### 7.3 Deleted-but-still-open
- Link count = 0, open file description reference count > 0.  
- Space is released only when the last file descriptor is closed (process exit, `close()`, etc.).

### 7.4 Root reserved blocks
- Controlled by `tune2fs -m`.  
- Visible as the difference between “Used” and what ordinary users may consume.

### 7.5 Mount-point hiding
- The directory that serves as a mount point can contain files on the parent filesystem.  
- Those files are inaccessible (and invisible to a naïve `du` of the mount) while the mount is active.

### 7.6 Sparse files
- Logical size ≫ allocated blocks.  
- `du` reports allocated; `ls -l` / `stat` report logical size.

## 8. What Linux Is Actually Doing

**statfs / df path**
```
df
  → statfs() / statvfs() on the mount
  → filesystem reads superblock counters
  → returns blocks total / free / available and inode totals / free
```

**du path**
```
du
  → recursive tree walk (or fts equivalent)
  → for each inode: query allocated blocks (or apparent size)
  → sum
```

**Release of deleted-open space**
```
last close() or process exit
  → open-file-description reference count → 0
  → inode reference count drops
  → if link count already 0: filesystem frees data blocks and inode
  → superblock free counters increase
  → df immediately shows the recovered space
```

## 9. Commands and Tools

| Command | Purpose |
|---------|---------|
| `df -hT` | Human-readable size, filesystem type, usage |
| `df -i` | Inode usage |
| `df -h <path>` | Usage of the filesystem that owns a path |
| `du -sh *` | Summarise each entry in the current directory |
| `du -x` | Do not cross filesystem boundaries |
| `du -h --max-depth=1` | Controlled depth summary |
| `ncdu` / `dust` | Interactive / fast visual consumers (install if desired) |
| `lsof +L1` | Files with link count < 1 (deleted but open) |
| `lsof \| grep deleted` | Same, broader pattern |
| `find /path -xdev -type f -size +100M` | Large files, stay on one filesystem |
| `find /path -xdev -type f \| wc -l` | Rough file count (inode pressure indicator) |
| `tune2fs -l /dev/… \| grep -i reserve` | Root reserved-block percentage |

## 10. Hands-On Lab

**Objective**  
Reproduce the classic capacity discrepancies and recover space held by a deleted-but-still-open file.

**Setup**
```bash
mkdir -p ~/spacelab
cd ~/spacelab
```

**Steps**

1. Baseline:
```bash
df -h .
df -i .
```

2. Create a visible large consumer:
```bash
dd if=/dev/zero of=bigfile bs=1M count=512 status=progress
df -h .
du -sh .
```

3. Demonstrate deleted-but-still-open:
```bash
# Open the file and keep it open in the background
exec 3<> bigfile
rm bigfile
ls -l                    # file name is gone
df -h .                  # space still consumed
du -sh .                 # du no longer sees it
lsof +L1                 # or lsof | grep deleted
# Recover the space
exec 3>&-
df -h .                  # space returns
```

4. Inode exhaustion illustration (small filesystem):
```bash
dd if=/dev/zero of=small.img bs=1M count=64
mkfs.ext4 -N 2000 small.img     # deliberately low inode count
mkdir mnt
sudo mount -o loop small.img mnt
sudo chown $USER:$USER mnt
df -i mnt
for i in $(seq 1 3000); do touch mnt/f$i 2>/dev/null || { echo "failed at $i"; break; }; done
df -i mnt
df -h mnt                       # data blocks still free
sudo umount mnt
rm small.img
```

5. (Optional) Mount-point hiding demonstration:
```bash
mkdir -p hide/data
echo "hidden content" > hide/data/secret
sudo mount -t tmpfs tmpfs hide/data
ls hide/data                    # secret is invisible
df -h hide/data
sudo umount hide/data
ls hide/data                    # secret reappears
```

**Verification**  
You must have observed:

- Space still held after `rm` while a file descriptor remained open.  
- Immediate recovery of that space after the final close.  
- Inode exhaustion while data blocks remained available.  
- (Optional) Content hidden by a mount and restored after unmount.

**Cleanup**
```bash
exec 3>&- 2>/dev/null
sudo umount ~/spacelab/hide/data 2>/dev/null
sudo umount ~/spacelab/mnt 2>/dev/null
rm -rf ~/spacelab
```

## 11. Investigation Lab

**Scenario**  
Alert: root filesystem 95 % full. On-call runs `du -sh /*` and the numbers fall far short of what `df` reports. No single large directory stands out.

**Objective**  
Identify the missing space and restore it to a safe level.

**Available tools**  
`df -h`, `df -i`, `du -x`, `lsof +L1`, `findmnt`, `lsof`, `systemctl`, process list

**Initial clues**  
- `df -h /` ≈ 95 %.  
- `du -x -sh /*` sums to a much smaller value.  
- Several long-running services were recently restarted for an unrelated reason; one was not.

**Investigation questions**  
1. What single command immediately checks for the most common cause of `df` ≫ `du`?  
2. How do you attribute the held space to a specific process?  
3. What is the safest way to reclaim the space once the process is known?  
4. How do you guard against the same incident after the next log rotation?

Work the questions before reading the solution.

**Solution**  
```bash
sudo lsof +L1
# or
sudo lsof | grep deleted
```
The output lists processes holding unlinked files and the approximate size still allocated.  

Reclamation: restart (or signal) the responsible process so it closes the old file descriptor. After the last close, `df` drops immediately.  

Prevention: ensure log-rotation scripts send the correct signal (or use a logging library that supports external rotation), and consider monitoring for deleted-open files.

## 12. Production Failure Scenario

**Incident**  
A build server begins failing jobs with “No space left on device”. `df -h` shows 30 % used on the data volume; `df -i` shows 100 % inodes used. The volume contains millions of small build artefacts and cache files.

**Systematic troubleshooting**

1. **Observation**  
   `ENOSPC` on create; `df -h` healthy; `df -i` 100 %.

2. **Hypothesis**  
   Inode exhaustion.

3. **Evidence**  
   ```bash
   df -i /data
   find /data -xdev -type f | wc -l
   find /data -xdev -type f | head
   du -x -sh /data/* | sort -h
   ```

4. **Immediate recovery**  
   Delete or archive unneeded small files/directories (old build caches, temporary artefacts). Confirm `df -i` drops.

5. **Longer-term actions**  
   - Adjust retention policies.  
   - Recreate the filesystem with a higher inode ratio if the workload permanently needs many small files (`mkfs.ext4 -i` or `-N`).  
   - Consider a filesystem with dynamic inode allocation (e.g. XFS) for future volumes.  
   - Add monitoring on inode usage, not only on byte usage.

This is the classic dual-resource nature of traditional filesystems appearing in production.

## 13. Connection to Previous Linux Knowledge

- Link count and open-file-description reference counts (Sessions 02–03) are exactly the mechanism behind deleted-but-still-open space.  
- Independent inode and data-block allocation (Session 06) explains why `df -i` and `df -h` can tell different stories.  
- Mount points (Session 04) explain hidden data.  
- Page cache and writeback (Session 07) remind us that dirty data also occupies space until written (and that `sync`/`fsync` affect when it becomes visible on disk).  
- Block-layer observability (Session 08) is what you use when capacity problems turn into latency problems under memory pressure or heavy writeback.

## 14. Connection to Future Infrastructure

- **Containers**: dense container hosts frequently hit inode limits on the container filesystem or on overlay layers long before byte limits.  
- **Kubernetes**: emptyDir volumes, container image layers, and node-local caches produce the same `df`/`du`/`df -i` failure modes; node-problem-detector and monitoring must watch both dimensions.  
- **Cloud**: volume size and inode density are chosen at format time; growing a volume does not automatically grow inode capacity on ext4 in a useful way for already-created inodes.  
- **AI infrastructure**: training jobs that materialise millions of small sample files, tokenised shards, or intermediate outputs are classic inode-exhaustion workloads. Checkpoint directories that are deleted while still open by a training process produce the deleted-open pattern at multi-gigabyte scale.  
- **Observability**: production alerts must include both byte and inode usage; relying on byte usage alone is a common blind spot.

## 15. Engineering Questions

1. Why can `df` and `du` report very different used-space numbers for the same path?  
2. What two independent resources does a traditional filesystem exhaust?  
3. How do you reclaim space held by a deleted file that is still open?  
4. Why does ext4 reserve a percentage of blocks for the root user?  
5. How can files exist on a filesystem yet be invisible to `ls` of a directory that is a mount point?  
6. What does `df -i` tell you that `df -h` does not?  
7. Why is the error message the same (`ENOSPC`) for both inode and data-block exhaustion?  
8. How would you prove that a large space discrepancy is caused by deleted-open files rather than by a mount-point problem?  
9. When would you deliberately format a filesystem with a much higher inode density than the default?

## 16. Practical Assignment

On your lab system:

1. Produce a one-page “capacity runbook” that lists, in order, the exact commands you will run when a filesystem-full alert fires.  
2. Intentionally create each of the following situations (on a disposable loop filesystem or directory) and record the diagnostic commands that identify it:  
   - ordinary large-file consumption  
   - deleted-but-still-open file  
   - inode exhaustion with free data blocks  
   - data hidden under a mount point  
3. For each situation write the safest recovery step.  
4. Add a short section on what you would monitor in production so that you receive warning before either resource reaches 100 %.

## 17. Session Completion Test

Answer without notes.

**Conceptual**  
1. Name the two independent capacity resources of a traditional filesystem.  
2. List three reasons `df` and `du` can disagree.

**System behavior**  
3. A file is deleted while a process still has it open. When does the space become free?  
4. A filesystem reports 100 % inodes used and 40 % data blocks used. Can a process create a new empty file?

**Command interpretation**  
5. `df -i` shows 100 % IUse. What is the constraint?  
6. `lsof +L1` shows a 20 GB file marked deleted. What does this mean for `df`?

**Troubleshooting**  
7. Give the first three commands you run when told “the disk is full”.

**Internal**  
8. Explain why space held by a deleted-open file is still counted by `df` but not by `du`.

**Explain in your own words**  
9. Describe a complete, ordered procedure for diagnosing a filesystem-capacity incident.

## 18. Mastery Criteria

- **Basic understanding**: You can read `df` / `df -i` / `du` and know that deleted-open files exist.  
- **Working understanding**: You can diagnose all four classic discrepancy causes, reclaim space from deleted-open files, and distinguish inode exhaustion from data-block exhaustion.  
- **Strong understanding**: You can write a reliable runbook, choose appropriate monitoring thresholds for both resources, and explain every discrepancy in terms of the kernel mechanisms studied in earlier sessions.

## 19. What I Should Now Be Able to Explain

- Difference between data-block capacity and inode capacity  
- Why `df` and `du` diverge  
- Deleted-but-still-open files and how to reclaim their space  
- Root reserved blocks  
- Data hidden under mount points  
- Systematic order of investigation for capacity incidents  
- How to locate large consumers and high inode consumers  
- Operational consequences for containers, build systems, and AI workloads

## 20. Next Session

**Next Session Number**  
SESSION 10  

**Next Session Title**  
Permissions, Ownership, and the Discretionary Access Control Model  

**Why it comes next**  
The filesystem module is now complete for operational purposes: you can find files, understand their storage, follow I/O, and manage capacity. The next major module is security and access control—starting with the classic Unix permissions model (users, groups, UIDs, GIDs, mode bits, ownership) that determines who may read, write, or execute the files and directories you have been studying.
