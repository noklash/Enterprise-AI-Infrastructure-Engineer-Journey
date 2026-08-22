# Session 01 — Linux Filesystems: Files, Directories, and Inodes

## 1. Position in the Curriculum

**Phase**  
PHASE 1 — Linux Systems Engineering

**Module**  
MODULE 1 — Filesystems

**Session**  
SESSION 01 — Linux Filesystems: Files, Directories, and Inodes

**Prerequisites**  
- Process address spaces and memory isolation  
- System calls (especially those that interact with the kernel)  
- `/proc` as a virtual filesystem  
- Basic command-line usage and process inspection  

**What this session unlocks**  
Understanding of how Linux names, locates, and tracks every object that appears as a “file.” This is required before file descriptors, open files, mounts, permissions, disk I/O, page cache, and every higher-level abstraction that sits on top of the filesystem.

## 2. Why This Session Exists

You already understand processes, virtual memory, system calls, and how the kernel isolates address spaces. Until now, the kernel’s view of “stuff that lives on disk or looks like it lives on disk” has been treated as a black box.  

Every process interacts with the outside world primarily through files: configuration, logs, sockets, devices, pipes, and the data it stores. If you do not understand the data structures the kernel uses to represent those objects, you cannot reason about open files, deleted-but-still-open files, hard links, mount points, container filesystems, or why a disk fills up even when `du` looks clean.  

This session builds the foundational model: a filename is not the file; the inode is.

## 3. Learning Objectives

By the end of this session you will be able to:

- Explain the difference between a filename, a directory entry, and an inode.  
- Demonstrate, with concrete commands, how a path is resolved to an inode.  
- Show that two different names can refer to the same inode (hard link) and what that implies for deletion.  
- Inspect inode metadata and interpret every field that `stat` and `ls -i` expose.  
- Trace what happens when a process opens a file, reads it, and closes it, from the perspective of the inode.  
- Explain why deleting a file that is still open by a process does not free its space until the last file descriptor is closed.  
- Relate inodes to the virtual filesystems you already know (`/proc`, `/sys`).

## 4. Prerequisite Concepts

You already know:

- A process has a private virtual address space.  
- System calls cross the user/kernel boundary.  
- `/proc/<pid>` is a virtual filesystem that exposes process state.  
- The kernel is the only entity that can touch hardware or shared resources.

We will reuse those ideas. We will not re-teach them.

## 5. Mental Model

```
User-visible path
    /home/user/data.txt
           │
           ▼
Directory entries (names → inode numbers)
    "data.txt"  →  inode 123456
           │
           ▼
Inode (the real object)
    - type (regular file, directory, …)
    - owner, permissions, timestamps
    - size
    - pointers to data blocks (or other special data)
           │
           ▼
Data blocks on the underlying storage
    (or nothing, for virtual filesystems)
```

A filename is only a human-readable label stored inside a directory.  
The inode is the kernel’s permanent identity for the object.  
Everything else (permissions, size, data location, link count) lives in or is referenced by the inode.

## 6. Core Concept

A **file** in Linux is not the name you type. A file is an inode plus the data (if any) that the inode points to.

An **inode** (index node) is a filesystem data structure that stores metadata about a filesystem object and the location of its data. Every regular file, directory, symbolic link, device node, named pipe, and socket that lives in a traditional filesystem has exactly one inode.

A **directory** is itself a special kind of file whose data consists of a list of directory entries. Each directory entry maps a name (a string) to an inode number.

When you write:

```bash
cat /var/log/syslog
```

the kernel performs path resolution:

1. Start at the root inode (inode of `/`).  
2. Look up the name `var` inside that directory → obtain the inode of `/var`.  
3. Look up the name `log` inside the `/var` directory → obtain the inode of `/var/log`.  
4. Look up the name `syslog` → obtain the inode of the target file.  
5. Use the inode to locate the data blocks and read them.

The name is only used during path resolution. After the inode is known, the name is no longer needed for I/O.

This design is why:

- Renaming a file is cheap (only a directory entry changes).  
- Hard links are possible (multiple directory entries can point to the same inode).  
- Deleting a name does not necessarily delete the data (the inode’s link count must reach zero **and** no process may still hold the file open).

## 7. Break It Into the Smallest Important Pieces

### 7.1 Filename
- What it is: a string stored in a directory entry.  
- Why it exists: humans and programs need human-readable names.  
- How it relates: the filename itself carries almost no metadata; it only points to an inode number.

### 7.2 Directory entry
- What it is: a record inside a directory’s data that contains (at minimum) a name and an inode number.  
- Why it exists: directories are the namespace of the filesystem.  
- Observation: `ls -i` shows the inode number next to each name.

### 7.3 Inode number
- What it is: an integer that uniquely identifies an inode within a given filesystem.  
- Why it exists: the kernel and the filesystem need a compact, fixed-size identifier.  
- Important: inode numbers are unique only within one filesystem. The same number can appear on different filesystems.

### 7.4 Inode itself
- What it is: a data structure containing:  
  - file type  
  - ownership (UID/GID)  
  - permissions  
  - link count  
  - size  
  - timestamps (atime, mtime, ctime)  
  - pointers to data blocks (or, for large files, indirect/extent structures)  
- Why it exists: all permanent metadata must live in one place that survives renames and hard links.  
- Observation: `stat` and `ls -l` read fields from the inode.

### 7.5 Link count
- What it is: the number of directory entries that currently point to this inode.  
- Why it exists: the filesystem must know when it is safe to free the inode and its data blocks.  
- Behavior: when the link count drops to zero **and** no process still has the file open, the space can be reclaimed.

### 7.6 Data blocks / extents
- What they are: the actual content of the file, stored on the block device (or synthesized for virtual filesystems).  
- Relationship: the inode does not contain the data; it contains the map that locates the data.

### 7.7 Special files
- Directories, symbolic links, device nodes, sockets, and named pipes are also represented by inodes. Their “data” is interpreted differently (directory entries, target path, major/minor numbers, etc.).

## 8. What Linux Is Actually Doing

When a process calls `open("/path/to/file", …)`:

```
User space
    open() library wrapper
        ↓
System call (openat or open)
        ↓
Kernel VFS (Virtual File System) layer
    path resolution (walk directory entries → inodes)
        ↓
Filesystem-specific code (ext4, xfs, …)
    read inode from disk (or from inode cache)
        ↓
Allocate a file descriptor in the process’s file-descriptor table
    that points to a kernel open-file description
        that points to the inode (and current offset, flags, etc.)
```

The VFS layer is the abstraction that lets the same system calls work on ext4, XFS, NFS, `/proc`, tmpfs, etc. You already met the VFS idea when you examined `/proc`; now you see that ordinary files use the same layer.

## 9. Commands and Tools

| Command | Purpose | What it actually queries |
|---------|---------|--------------------------|
| `ls -i` | Show inode number next to each name | Directory entries |
| `ls -l` | Show type, permissions, link count, owner, size, timestamps | Inode metadata |
| `stat <path>` | Full inode metadata in human- and machine-readable form | Inode + some VFS state |
| `find /path -inum <number>` | Locate all names that point to a given inode | Walk directory entries looking for matching inode number |
| `df -i` | Show inode usage per filesystem | Superblock / filesystem statistics |
| `debugfs` (read-only) | Low-level inspection of ext4 inodes and blocks | Direct filesystem structures (use with care) |

Important flags:

- `stat -c '%i %n'` — print only inode number and name.  
- `ls -id` — show inode of a directory itself.

## 10. Hands-On Lab

**Objective**  
Prove that a filename is only a directory entry pointing to an inode, and that the inode is the real object.

**Prerequisites**  
Ubuntu in VirtualBox, ordinary user with sudo if needed for some inspection, working directory you can write to (e.g. `~/filesystem-lab`).

**Setup**
```bash
mkdir -p ~/filesystem-lab
cd ~/filesystem-lab
```

**Steps**

1. Create a regular file and record its inode:
```bash
echo "hello inode world" > data.txt
ls -i data.txt
stat data.txt
```
Note the inode number and the link count (should be 1).

2. Create a hard link:
```bash
ln data.txt data-hardlink.txt
ls -li
```
Observe that both names share the identical inode number and that the link count is now 2.

3. Modify the file through one name and read it through the other:
```bash
echo "modified through hard link" >> data-hardlink.txt
cat data.txt
```
Both names see the change because they refer to the same inode and therefore the same data blocks.

4. Delete one name:
```bash
rm data.txt
ls -li
stat data-hardlink.txt
```
The remaining name still works; the link count drops to 1. The data is still there.

5. Find all names for an inode (if you still have the number):
```bash
find . -inum <the-inode-number>
```

6. Create a directory and inspect its inode:
```bash
mkdir testdir
ls -id testdir
stat testdir
```
Note that a directory has a link count of at least 2 (`.` and the entry in its parent). Adding a subdirectory increases the link count further.

7. Observe a virtual file:
```bash
ls -i /proc/self/status
stat /proc/self/status
```
Notice that `/proc` files have inodes, but they are synthesized by the kernel; there are no data blocks on a disk.

**Verification**  
You should be able to show two names sharing one inode, a link count that changes with hard-link creation and removal, and that deleting one name does not destroy the data while the other name (or an open file descriptor) still exists.

**Cleanup**
```bash
cd ~
rm -rf ~/filesystem-lab
```

## 11. Investigation Lab

**Scenario**  
A colleague says: “I deleted `/var/log/bigapp.log` but `df -h` still shows the filesystem 100 % full.”

**Objective**  
Determine whether the space is still held by an open file descriptor and identify the responsible process.

**Available tools**  
`lsof`, `fuser`, `ls -l /proc/*/fd`, `df`, `du`, `stat`, `find`

**Initial clues**  
- `df -h` shows the filesystem full.  
- `du -sh /var/log` shows far less usage.  
- The file name `/var/log/bigapp.log` no longer exists.

**Investigation questions**  
1. How can a deleted file still consume space?  
2. Which kernel data structure keeps the inode alive after the last directory entry is gone?  
3. How do you find processes that hold a file open by inode or by path (even if the path is marked deleted)?  
4. What happens to the space when the last process closes the file or exits?

(Work through the questions with the tools before reading the solution.)

**Solution**  
A deleted file whose link count has reached zero is still present in the filesystem until every process that holds it open closes its file descriptor. The kernel open-file description keeps a reference to the inode.  

Commands that reveal this:
```bash
lsof | grep deleted
# or
lsof +L1
ls -l /proc/<pid>/fd
```
Once the responsible process closes the descriptor (or is restarted), the space is reclaimed. This is a classic production issue with long-running services that keep log files open.

## 12. Production Failure Scenario

**Incident**  
After a log-rotation script runs, the application continues writing to the old (now deleted) log file. The filesystem fills up. New log files appear empty. Monitoring alerts on disk usage, but `du` on the log directory looks normal.

**Systematic approach**  
1. Confirm the symptom: `df -h` vs `du -sh`.  
2. Hypothesis: deleted-but-still-open files.  
3. Evidence: `lsof +L1` or `lsof | grep deleted` shows the application process holding the old inode.  
4. Test: restart the application (or send it a signal that forces it to reopen its log).  
5. Result: space is freed, new logs start receiving data.  
6. Longer-term fix: configure the application to reopen logs on signal (or use a logging library that handles rotation correctly), and ensure log-rotation scripts signal the process.

This is the same mechanism you just investigated, now seen in a realistic operational context.

## 13. Connection to Previous Linux Knowledge

- Processes hold file descriptors; those descriptors ultimately reference inodes.  
- System calls such as `open`, `read`, `write`, `unlink`, `rename` all operate on the VFS and therefore on inodes.  
- Virtual memory and the page cache will later sit between the inode’s data blocks and the process’s address space.  
- `/proc` and `/sys` are filesystems whose “inodes” are manufactured by the kernel on the fly—the same VFS interface you are now studying for real files.  
- Context switches and scheduling are unaffected by filesystems directly, but a process blocked in disk I/O is waiting for work that began with an inode.

## 14. Connection to Future Infrastructure

- Containers: each container has its own mount namespace and often its own filesystem layers (OverlayFS, etc.). Understanding inodes is required to understand why a file deleted inside a container may still exist in a lower layer, and why inode exhaustion can affect dense container hosts.  
- Kubernetes: persistent volumes, ConfigMaps, and Secrets are ultimately filesystems or filesystem-like objects mounted into pods.  
- Image layers and container storage drivers manipulate directory entries and inodes extensively.  
- Observability agents and log collectors frequently encounter the “deleted but still open” problem you investigated.  
- Later, when we reach distributed filesystems or object storage, the local inode model remains the foundation on which remote abstractions are built.

## 15. Engineering Questions

1. Why can two different filenames share the same inode, but two different inodes cannot share the same data blocks under normal hard-link rules?  
2. What exactly happens to the link count and the data blocks when the last name for an inode is removed while a process still has the file open?  
3. How would you prove that `/proc/self/fd/3` and a path you opened earlier refer to the same underlying inode?  
4. Why does `df -i` matter on a filesystem that still has free data blocks?  
5. What is the difference between the information returned by `ls -l` and the information returned by `stat`? Which fields come purely from the inode?  
6. If a process calls `unlink()` on a file it has open, does the next `read()` from its file descriptor still succeed? Why?  
7. How does the VFS allow the same `open()` system call to work for both an ext4 file and a file under `/proc`?  
8. Why is renaming a large file essentially instantaneous while copying it is not?

## 16. Practical Assignment

Create a small investigation script or sequence of commands that:

1. Creates a file and records its inode.  
2. Creates three hard links to it.  
3. Opens the file in a background process that keeps it open and writes a timestamp every few seconds.  
4. Deletes all directory entries for that inode.  
5. Shows that the background process continues to write and that `df` still accounts for the space.  
6. Terminates the background process and confirms the space is released.  

Document every observation and the kernel mechanism that explains it. This assignment deliberately combines process management (which you already know) with the new inode model.

## 17. Session Completion Test

Answer without referring to notes.

**Conceptual**  
1. What is the difference between a filename and an inode?  
2. What does the link count of an inode represent?  
3. Why does deleting a file that is still open not free its space immediately?

**System behavior**  
4. After `ln a.txt b.txt` and then `rm a.txt`, what happens when you `cat b.txt`?  
5. A process has a file open. You delete every name that pointed to its inode. What can you still do with the open file descriptor?

**Command interpretation**  
6. You run `ls -li` and see two names with the same inode number and a link count of 2. What does that tell you?  
7. `stat` shows “Links: 0”. How is that possible?

**Troubleshooting**  
8. `df -h` shows 100 % full, `du -sh /*` shows much less. What is the most likely cause related to this session’s material, and how do you confirm it?

**Internal**  
9. Describe the steps the kernel takes, starting from the `open()` system call, until a file descriptor is returned to user space.

**Explain in your own words**  
10. Explain, as if to a junior engineer, why a filename is not the file.

## 18. Mastery Criteria

- **Basic understanding**: You can define inode, directory entry, and link count, and you can use `ls -i` and `stat` correctly.  
- **Working understanding**: You can create hard links, predict the effect of `rm` on an open file, and diagnose a “deleted but still open” disk-space problem.  
- **Strong understanding**: You can walk through path resolution, explain the relationship between the process file-descriptor table, the kernel open-file description, and the inode, and you can reason about why certain container and log-rotation behaviors occur.

## 19. What I Should Now Be Able to Explain

- Filename versus inode  
- Directory entry  
- Inode number and its scope  
- Link count and its role in deletion  
- Path resolution at a high level  
- Why hard links share data and metadata  
- Why a deleted open file continues to consume space  
- The role of the VFS in making different filesystems look the same to processes  
- How to observe all of the above with standard tools

## 20. Next Session

**Next Session Number**  
SESSION 02  

**Next Session Title**  
File Descriptors, Open File Descriptions, and the Relationship Between Processes and Inodes  

**Why it comes next**  
You now know that the inode is the real object. The next step is to understand how a process actually holds a reference to that object while it is working with it—the file descriptor table, the kernel’s open-file description, reference counting, and what `lsof`, `/proc/<pid>/fd`, and `fuser` are really showing you.
