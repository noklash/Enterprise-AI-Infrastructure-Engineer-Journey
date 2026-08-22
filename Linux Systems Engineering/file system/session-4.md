# Session 04 — Filesystem Hierarchy, Mount Points, and the VFS (Virtual File System)

## 1. Position in the Curriculum

**Phase**  
PHASE 1 — Linux Systems Engineering

**Module**  
MODULE 1 — Filesystems

**Session**  
SESSION 04 — Filesystem Hierarchy, Mount Points, and the VFS (Virtual File System)

**Prerequisites**  
- Inodes, directory entries, path resolution (Session 01)  
- File descriptors and open file descriptions (Session 02)  
- Hard links, symbolic links, and deletion semantics (Session 03)

**What this session unlocks**  
Understanding of how Linux presents a single unified directory tree even though data comes from many independent filesystems (disk, memory, kernel interfaces, network). This is required before working with block devices, real filesystems such as ext4, mount namespaces, containers, and any form of storage attachment.

## 2. Why This Session Exists

You now understand what a file is (an inode + data), how names point to inodes, and how processes hold references to those inodes through file descriptors.  

The next necessary question is: how does the kernel combine many separate filesystems into the single tree that begins at `/`?  

Without this knowledge you cannot correctly interpret:

- why `/proc`, `/sys`, `/dev`, `/tmp`, and `/home` behave differently  
- what `mount` and `umount` actually change  
- why a process can see a different view of the filesystem than another process (later: mount namespaces)  
- how container root filesystems are constructed  
- where data really lives when you write to a path

This session introduces the VFS (Virtual File System) as the abstraction layer and mount points as the mechanism that grafts one filesystem onto another.

## 3. Learning Objectives

By the end of this session you will be able to:

- Explain the role of the VFS (Virtual File System) as the common interface between processes and concrete filesystem implementations.  
- Describe the Linux Filesystem Hierarchy Standard at the level needed for system engineering (what belongs where and why).  
- Show how a mount point replaces the view of a directory with the root of another filesystem.  
- Use `findmnt`, `mount`, `/proc/mounts`, `/proc/self/mountinfo`, and `lsblk` to discover what is mounted where.  
- Distinguish bind mounts, tmpfs mounts, and ordinary device-backed mounts.  
- Predict what a process sees when it walks through a mount point.  
- Explain why `/proc`, `/sys`, and `/dev` can appear in the same tree as disk filesystems even though they have no ordinary data blocks.  
- Relate mount operations to the path-resolution machinery you already know.

## 4. Prerequisite Concepts

You already know:

- Path resolution walks directory entries to locate inodes.  
- Inodes may represent regular files, directories, symbolic links, etc.  
- The kernel can synthesize file and directory objects (as seen with `/proc`).  
- Each process has its own file-descriptor table, but until now we assumed a shared view of the directory tree.

## 5. Mental Model

```
Process
   │
   │  open("/var/log/app.log")
   ▼
VFS (Virtual File System)
   │  path resolution
   │  mount-point crossing
   ▼
┌─────────────┬─────────────┬─────────────┬─────────────┐
│  ext4       │  tmpfs      │  procfs     │  sysfs      │
│  (disk)     │  (memory)   │  (kernel)   │  (kernel)   │
└─────────────┴─────────────┴─────────────┴─────────────┘
```

The VFS provides a uniform set of operations (lookup, create, read, write, …).  
Each concrete filesystem registers its own implementation of those operations.  
A **mount point** is a directory inode on one filesystem that has been overlaid with the root of another filesystem. When path resolution reaches that directory, the VFS switches to the mounted filesystem.

## 6. Core Concept

### The VFS (Virtual File System)

The VFS is the kernel layer that lets processes use the same system calls (`open`, `read`, `stat`, `mkdir`, …) regardless of whether the target lives on an ext4 disk partition, in memory (tmpfs), or is generated on the fly by the kernel (procfs, sysfs).  

It maintains:

- a tree of **mount structures** that describe which filesystem is attached where  
- caches for inodes (inode cache) and directory entries (dentry cache)  
- a common representation of open files (the open file descriptions you studied in Session 02)

### Mount points

A **mount** attaches the root of one filesystem to a directory (the mount point) belonging to another filesystem. After the mount:

- the original contents of the mount-point directory become hidden for the duration of the mount  
- path resolution that reaches the mount point continues into the newly attached filesystem

Mounts form a tree (the mount tree). The ultimate root of that tree is the root filesystem that was mounted when the system booted.

### Special filesystems you already meet daily

| Path   | Filesystem type | Backing          | Purpose |
|--------|-----------------|------------------|---------|
| `/`    | usually ext4/xfs| block device     | Root filesystem |
| `/proc`| procfs          | kernel data      | Process and system information |
| `/sys` | sysfs           | kernel data      | Device, driver, and kernel object hierarchy |
| `/dev` | devtmpfs / udev | kernel + userspace | Device nodes |
| `/run` | tmpfs           | memory           | Runtime data |
| `/tmp` | often tmpfs     | memory           | Temporary files |

These appear in the same hierarchy because the VFS routes operations to different implementations according to the mount tree.

### Filesystem Hierarchy Standard (high level)

Linux distributions largely follow a common layout:

- `/bin`, `/sbin`, `/usr` — essential and non-essential programs  
- `/etc` — configuration  
- `/home` — user home directories  
- `/var` — variable data (logs, caches, spools)  
- `/tmp` — temporary files  
- `/proc`, `/sys`, `/dev` — kernel and device interfaces  
- `/opt` — optional application packages  
- `/boot` — kernel and boot-loader files  

Exact contents vary; the important engineering point is that the hierarchy is a **namespace** built by mounts, not a single filesystem.

## 7. Break It Into the Smallest Important Pieces

### 7.1 Superblock
- Per-filesystem metadata object (type, block size, root inode, mount flags, …).  
- Created when a filesystem is mounted.

### 7.2 Mount structure
- Kernel object that records: the filesystem being mounted, the mount point, mount flags (read-only, noexec, …), parent mount, etc.

### 7.3 Dentry (directory entry cache object)
- VFS object that represents a directory entry in memory.  
- Links names to inodes and participates in path resolution.

### 7.4 Mount point crossing
- During path lookup, if the current dentry is a mount point, the VFS follows the mount structure to the root of the mounted filesystem and continues.

### 7.5 Bind mount
- A mount that attaches an existing directory tree to another location instead of attaching a new filesystem from a device.  
- Both locations now view the same underlying tree.

### 7.6 Shared subtrees and mount propagation (preview)
- Mechanisms that control whether a mount performed in one mount namespace is visible in another.  
- Critical later for containers; only conceptual awareness is required now.

### 7.7 `/proc/self/mountinfo`
- The authoritative per-process view of the mount tree, including mount IDs, parent IDs, mount points, and optional fields.

## 8. What Linux Is Actually Doing

When a process calls `open("/var/log/app.log")`:

```
User space
    open("/var/log/app.log", …)
        ↓
VFS path-resolution engine
    start at root dentry of the process’s root mount
    for each component (“var”, “log”, “app.log”):
        look up name in current directory’s dentries
        if current dentry is a mount point:
            switch to the root of the mounted filesystem
        obtain the inode for the component
    final inode is used to allocate an open file description
        ↓
Concrete filesystem (e.g. ext4)
    supplies the inode content and data-block mapping
```

The same path-resolution code handles ordinary files, symbolic links, and mount-point crossings.

Mounting is performed by the `mount()` system call (normally via the `mount` utility). It:

1. Locates the device or source  
2. Calls the filesystem-specific mount method to read the superblock  
3. Creates a new mount structure  
4. Attaches that structure to the mount-point dentry  

## 9. Commands and Tools

| Command / Path | Purpose |
|----------------|---------|
| `findmnt` | Tree or list view of all mounts; most readable modern tool |
| `findmnt -T /path` | Which filesystem covers a given path |
| `mount` | Older list of mounts; also the command that performs mounts |
| `cat /proc/mounts` | Kernel’s current mount table (global view) |
| `cat /proc/self/mountinfo` | Detailed per-process mount information |
| `lsblk` | Block devices and their mount points |
| `df -hT` | Filesystem usage and type |
| `stat -f /path` | Filesystem information for the filesystem that owns a path |
| `namei -l /path` | Step-by-step path resolution (shows mount crossings indirectly) |

Prefer `findmnt` for daily work; use `/proc/self/mountinfo` when you need the full kernel truth.

## 10. Hands-On Lab

**Objective**  
Discover the actual mount tree on a running Ubuntu system and observe how mount points change the view of the directory hierarchy.

**Setup**  
Ordinary Ubuntu VirtualBox installation; no special preparation required.

**Steps**

1. Obtain a clear view of the mount tree:
```bash
findmnt
findmnt --real          # only “real” filesystems, omit some virtual ones
```

2. Inspect the detailed kernel view:
```bash
findmnt -o TARGET,SOURCE,FSTYPE,OPTIONS
column -t /proc/self/mountinfo | head -20
```

3. Locate where common paths live:
```bash
findmnt -T /
findmnt -T /proc
findmnt -T /sys
findmnt -T /dev
findmnt -T /tmp
findmnt -T /home
```

4. Examine a few special filesystems:
```bash
stat -f /proc
stat -f /sys
stat -f /
ls -ld /proc /sys /dev /run /tmp
```

5. Create a temporary mount to observe mount-point behavior (safe, memory-backed):
```bash
mkdir -p ~/mnt-lab/data ~/mnt-lab/mountpoint
echo "visible before mount" > ~/mnt-lab/mountpoint/before.txt
echo "data file" > ~/mnt-lab/data/secret.txt

sudo mount -t tmpfs -o size=16m tmpfs ~/mnt-lab/mountpoint
ls -l ~/mnt-lab/mountpoint          # before.txt is now hidden
echo "visible after mount" > ~/mnt-lab/mountpoint/after.txt
ls -l ~/mnt-lab/mountpoint
findmnt -T ~/mnt-lab/mountpoint
```

6. Unmount and confirm the original content reappears:
```bash
sudo umount ~/mnt-lab/mountpoint
ls -l ~/mnt-lab/mountpoint          # before.txt is back
```

7. (Optional) Bind mount demonstration:
```bash
mkdir -p ~/mnt-lab/bind-target
sudo mount --bind ~/mnt-lab/data ~/mnt-lab/bind-target
ls -l ~/mnt-lab/bind-target
sudo umount ~/mnt-lab/bind-target
```

**Verification**  
You must be able to:

- Identify the source, type, and options for the root filesystem and for `/proc`.  
- Show that mounting a tmpfs over a directory hides its previous contents and that unmounting restores them.  
- Explain the difference between the view given by `findmnt` and the view given by a plain `ls` of the mount point.

**Cleanup**
```bash
sudo umount ~/mnt-lab/mountpoint 2>/dev/null
sudo umount ~/mnt-lab/bind-target 2>/dev/null
rm -rf ~/mnt-lab
```

## 11. Investigation Lab

**Scenario**  
A colleague reports: “I copied a large file into `/mnt/data/incoming` but `df -h /mnt/data` shows no change in used space. The file is visible with `ls`.”

**Objective**  
Determine which filesystem actually received the file and why the expected `df` output did not change.

**Available tools**  
`findmnt`, `df`, `ls`, `stat -f`, `mount`, `/proc/self/mountinfo`, `lsblk`

**Initial clues**  
- The path `/mnt/data/incoming` exists and contains the new file.  
- `df -h /mnt/data` reports unchanged usage.  
- There have been recent mount-related changes on the system.

**Investigation questions**  
1. What does `df` measure—directories or filesystems?  
2. How can a path that appears to be under `/mnt/data` actually belong to a different filesystem?  
3. Which command tells you the filesystem that owns a given path?  
4. What mount-related mistakes commonly produce this symptom?

Work the questions with the tools before reading the solution.

**Solution**  
`df` reports usage of a **filesystem**, not of a directory tree. If `/mnt/data` is a mount point, but a previous mount failed or was never performed, then `/mnt/data` is just an ordinary directory on the parent filesystem (often the root filesystem). Files written there consume space on the parent, not on the intended volume.

```bash
findmnt -T /mnt/data/incoming
df -h /mnt/data/incoming
stat -f /mnt/data/incoming
```
These commands reveal the true filesystem. The usual operational fixes are to mount the correct device at `/mnt/data` (after moving any files that landed on the parent) or to correct an automated mount unit that failed.

## 12. Production Failure Scenario

**Incident**  
After a reboot, an application fails to start because `/var/lib/app/data` is empty. The disk that should contain the data is present (`lsblk` shows it) but is not mounted. A residual directory `/var/lib/app/data` on the root filesystem exists and is empty. The application’s unit file has no dependency on the mount.

**Systematic troubleshooting**

1. **Observation**  
   Application logs: “data directory empty”.  
   `ls /var/lib/app/data` shows nothing.  
   `lsblk` shows the expected disk and partition.

2. **Hypothesis**  
   The filesystem that should be mounted at `/var/lib/app/data` is not mounted.

3. **Evidence**  
   ```bash
   findmnt -T /var/lib/app/data
   cat /etc/fstab          # or the relevant systemd .mount unit
   systemctl status var-lib-app-data.mount   # if using systemd mounts
   journalctl -u var-lib-app-data.mount
   ```

4. **Confirmation**  
   `findmnt` shows that `/var/lib/app/data` is still on the root filesystem. The expected source device is not mounted.

5. **Resolution**  
   - Mount the filesystem manually to recover.  
   - Fix the `/etc/fstab` entry or systemd `.mount` unit (wrong UUID, missing `x-systemd.requires`, etc.).  
   - Add appropriate ordering dependencies so the application starts only after the mount is active.  
   - Verify with `findmnt` and a test write after reboot.

This class of failure is extremely common and is diagnosed by asking “which filesystem owns this path?” rather than assuming the directory tree matches the intended storage layout.

## 13. Connection to Previous Linux Knowledge

- Path resolution (Sessions 01–03) is performed by the VFS and now includes mount-point crossing.  
- Inodes and dentries still exist; the VFS caches them and routes operations to the correct filesystem implementation.  
- Open file descriptions continue to point at inodes; the fact that those inodes may belong to different filesystems is hidden by the VFS.  
- `/proc` and `/sys`, which you already used for process and system inspection, are simply filesystems mounted into the tree by the same mechanism.

## 14. Connection to Future Infrastructure

- **Containers**: every container normally receives its own mount namespace. The root filesystem of a container is typically a mount (often OverlayFS or a bind mount of an image layer). Understanding host mounts versus container mounts is essential.  
- **Docker / Kubernetes**: volume mounts, bind mounts, emptyDir (tmpfs or disk), and projected volumes are all expressed as mounts inside the container’s mount namespace.  
- **systemd**: `.mount` and `.automount` units manage the mount tree and encode dependencies.  
- **Cloud / distributed storage**: remote filesystems (NFS, Ceph, cloud block volumes) are attached by mounting them into the hierarchy.  
- **AI infrastructure**: large model files, datasets, and checkpoint directories are almost always accessed through mounts; incorrect mount options (noatime, ro, size limits on tmpfs) directly affect training and inference performance and reliability.

## 15. Engineering Questions

1. Why can `/proc` and an ext4 filesystem appear under the same root directory?  
2. What happens to the original contents of a directory when another filesystem is mounted on top of it?  
3. How does the VFS know which filesystem implementation should handle a given path?  
4. Why does `df /some/dir` sometimes show different numbers from `du /some/dir`?  
5. What information does `/proc/self/mountinfo` contain that the older `/proc/mounts` does not?  
6. What is a bind mount, and why is it useful?  
7. How would you prove that a path is on a tmpfs rather than on disk?  
8. Why is it dangerous for an application to assume that a path such as `/var/lib/app` is always on the same filesystem as `/`?  
9. What does the kernel do when path resolution reaches a mount point?

## 16. Practical Assignment

On your Ubuntu system:

1. Produce a clean map (text or diagram) of the major mounts, noting source, target, type, and important options.  
2. Create a temporary directory hierarchy and demonstrate both an ordinary tmpfs mount and a bind mount. Record what is visible before, during, and after each mount.  
3. Intentionally write a file into a mount-point directory **before** mounting a filesystem on it; then mount the filesystem and observe that the file disappears from view. Recover the file after unmounting.  
4. Write a short explanation of how this behavior can cause data to land on the wrong filesystem in production, and how `findmnt -T` prevents that mistake.

## 17. Session Completion Test

Answer without notes.

**Conceptual**  
1. What is the VFS and why does it exist?  
2. What is a mount point?

**System behavior**  
3. You mount a tmpfs on `/mnt/test` that already contained a file. What happens to that file?  
4. How can two different paths view exactly the same directory tree?

**Command interpretation**  
5. `findmnt -T /var/log` returns a line. What questions does that line answer?  
6. Why might `df -h /mnt/data` and `du -sh /mnt/data` disagree dramatically?

**Troubleshooting**  
7. An application’s data directory is empty after reboot even though the disk is present. What is the first filesystem-related check you perform?

**Internal**  
8. Describe how path resolution interacts with a mount point.

**Explain in your own words**  
9. Explain why Linux can put `/proc`, `/sys`, and ordinary disk files under a single directory tree without the process knowing which is which.

## 18. Mastery Criteria

- **Basic understanding**: You can list the major mounts on a system and explain what a mount point does.  
- **Working understanding**: You can use `findmnt` and `/proc/self/mountinfo` to determine which filesystem owns any path, create and remove safe temporary mounts, and diagnose “file written to wrong filesystem” problems.  
- **Strong understanding**: You can reason about the VFS routing of operations, the relationship between the mount tree and path resolution, and the implications for application startup dependencies and container filesystem construction.

## 19. What I Should Now Be Able to Explain

- Role of the VFS (Virtual File System)  
- Mount point and mount tree  
- How path resolution crosses mounts  
- Difference between device-backed, memory-backed, and kernel-synthetic filesystems  
- Meaning of the core entries in the Filesystem Hierarchy  
- How to discover the filesystem that owns any given path  
- Bind mounts versus ordinary mounts  
- Why data can silently land on the wrong filesystem

## 20. Next Session

**Next Session Number**  
SESSION 05  

**Next Session Title**  
Block Devices, Partitions, and the Path from Filename to Storage  

**Why it comes next**  
You now understand how filesystems are attached to the directory tree. The next step is to go one layer lower: how block devices and partitions appear, how a filesystem is placed on a partition, and how a write issued by a process eventually reaches a storage device. This connects the VFS view you just learned to the hardware and to the disk-I/O concepts that follow.
