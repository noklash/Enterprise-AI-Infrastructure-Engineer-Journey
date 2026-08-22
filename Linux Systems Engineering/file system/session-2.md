# Session 02 — File Descriptors, Open File Descriptions, and the Relationship Between Processes and Inodes

## 1. Position in the Curriculum

**Phase**  
PHASE 1 — Linux Systems Engineering

**Module**  
MODULE 1 — Filesystems

**Session**  
SESSION 02 — File Descriptors, Open File Descriptions, and the Relationship Between Processes and Inodes

**Prerequisites**  
- Inodes, directory entries, and path resolution (Session 01)  
- Process address spaces and process identifiers  
- System calls and the user-space / kernel-space boundary  
- Basic inspection of `/proc/<pid>`

**What this session unlocks**  
The ability to reason about every open file, socket, pipe, or device that a process holds. This is required for understanding file deletion behavior, resource leaks, `lsof` output, container file isolation, log rotation failures, and later concepts such as network sockets and file descriptor passing.

## 2. Why This Session Exists

In Session 01 you learned that a filename is only a directory entry pointing to an inode, and that the inode is the real object.  

A process never works directly with an inode number in normal operation. When a process wants to read or write a file it receives a small integer called a file descriptor. That integer is an index into a per-process table. The kernel uses that table entry to find a shared kernel object (the open file description) that in turn points to the inode.

Without this three-layer model:

```
Process file-descriptor table
        ↓
Kernel open-file description
        ↓
Inode
```

you cannot correctly interpret why a deleted file still consumes space, why two processes can share a file offset, why `close()` does not always free resources immediately, or what `/proc/<pid>/fd` is actually showing you.

This session connects the process model you already know to the inode model you just learned.

## 3. Learning Objectives

By the end of this session you will be able to:

- Explain the three distinct objects involved when a process has a file open: file descriptor, open file description, and inode.  
- Demonstrate that file descriptors are process-private while open file descriptions can be shared.  
- Show how the link count of an inode and the reference count of an open file description together control when data blocks are freed.  
- Use `/proc/<pid>/fd`, `lsof`, and `ls -l /proc/<pid>/fd` to map file descriptors back to inodes and paths (including deleted paths).  
- Predict the effect of `dup()`, `fork()`, and `close()` on file descriptors and open file descriptions.  
- Diagnose a process that is holding a deleted file open and quantify the space it is consuming.  
- Trace the path from an `open()` system call to the allocation of a file descriptor.

## 4. Prerequisite Concepts

You already understand:

- An inode is the filesystem’s permanent identity for a file.  
- Directory entries map names to inode numbers.  
- A process has a unique PID (Process Identifier) and a private virtual address space.  
- System calls transition from user space into the kernel.  
- `/proc/<pid>` exposes kernel data structures for a process in a filesystem-like form.

We will not re-teach these.

## 5. Mental Model

```
User-space process
┌──────────────────────────────────────────────┐
│  File-descriptor table (per process)         │
│  0 → stdin                                   │
│  1 → stdout                                  │
│  2 → stderr                                  │
│  3 → (pointer)                               │
│  4 → (pointer)                               │
└───────────────┬──────────────────────────────┘
                │
                ▼
Kernel space
┌──────────────────────────────────────────────┐
│  Open file description (can be shared)       │
│  - current file offset                       │
│  - access mode (O_RDONLY, O_RDWR, …)         │
│  - status flags                              │
│  - pointer to inode                          │
│  - reference count                           │
└───────────────┬──────────────────────────────┘
                │
                ▼
┌──────────────────────────────────────────────┐
│  Inode                                       │
│  - metadata, link count, data-block map      │
└──────────────────────────────────────────────┘
```

Key properties:

- The file-descriptor table is private to each process (after `fork()` the child receives a copy of the table).  
- The open file description is a kernel object that can be referenced by multiple file descriptors (in the same process or different processes).  
- The inode is the filesystem object; it outlives any particular open file description.

## 6. Core Concept

A **file descriptor** (often abbreviated FD after the first expansion) is a non-negative integer that a process uses as a handle for an open file, socket, pipe, or other file-like object. It is an index into the process’s file-descriptor table.

An **open file description** (also called a `struct file` in the kernel) is a kernel data structure that represents one “opening” of a file. It stores:

- the current file offset  
- the access mode with which the file was opened  
- status flags (for example `O_APPEND`)  
- a pointer to the inode (or to the equivalent VFS object)  
- a reference count

When a process calls `open()`, the kernel:

1. Resolves the path to an inode (Session 01).  
2. Allocates a new open file description and initializes it.  
3. Allocates the lowest available file descriptor number in the calling process and makes that table entry point to the new open file description.  
4. Returns the file descriptor number to user space.

Subsequent calls such as `read()`, `write()`, and `lseek()` take the file descriptor, look up the open file description, and operate on the inode through it.

Important consequences:

- Two file descriptors can point to the **same** open file description (created by `dup()` or by inheritance across `fork()`). In that case they share the file offset.  
- Two open file descriptions can point to the **same** inode (two independent `open()` calls). In that case each has its own offset.  
- The inode’s data blocks are released only when **both** the link count drops to zero **and** the last open file description that references the inode is released.

## 7. Break It Into the Smallest Important Pieces

### 7.1 File-descriptor table
- What it is: a per-process array (or equivalent structure) of pointers to open file descriptions.  
- Why it exists: user space needs a small, process-local integer handle; the kernel needs a place to store the mapping.  
- Observation: `/proc/<pid>/fd` is a directory whose entries are the currently open file descriptors.

### 7.2 File descriptor number
- What it is: the integer index into the file-descriptor table (0, 1, 2, …).  
- Conventional assignments: 0 = standard input, 1 = standard output, 2 = standard error.  
- Limits: controlled by `RLIMIT_NOFILE` (soft and hard). Visible in `/proc/<pid>/limits`.

### 7.3 Open file description
- What it is: the kernel object that represents one opening of a file.  
- Why it exists: the file offset, access mode, and flags must be stored somewhere that can be shared or private independently of the inode.  
- Reference count: incremented when a new file descriptor points to it; decremented on `close()` or process exit. When the count reaches zero the open file description is freed and the inode’s reference count is dropped.

### 7.4 Relationship created by `dup()` / `dup2()`
- Creates a new file descriptor that points to the **same** open file description.  
- Therefore the two descriptors share the current offset and status flags.

### 7.5 Relationship created by `fork()`
- The child receives a copy of the parent’s file-descriptor table.  
- Corresponding file descriptors in parent and child point to the **same** open file descriptions.  
- Consequently parent and child share file offsets for those descriptors until one of them closes or redirects them.

### 7.6 Relationship created by independent `open()` calls
- Each `open()` allocates a fresh open file description.  
- Even if both point to the same inode, they have independent offsets and flags.

### 7.7 Reference counting and deletion
- Inode link count = number of directory entries.  
- Open-file-description reference count (plus other kernel references) keeps the inode alive even after the link count reaches zero.  
- Space is reclaimed only when both reach the appropriate zero state.

## 8. What Linux Is Actually Doing

Trace of a typical `open()` + `read()` + `close()`:

```
User space
    fd = open("/var/log/app.log", O_RDONLY);
        ↓ (system call)
Kernel VFS
    path walk → inode
    allocate struct file (open file description)
    install pointer in current process’s fd table at the lowest free slot
    return the slot number
        ↓
    read(fd, buf, count)
        look up fd → open file description → inode
        perform I/O, update offset inside the open file description
        ↓
    close(fd)
        clear the fd table entry
        decrement reference count on the open file description
        if count reaches zero:
            release the open file description
            drop the reference on the inode
            if inode link count is also zero and no other references remain:
                free data blocks
```

The same path is used for sockets, pipes, and device files; only the inode (or VFS equivalent) and the operations it supports change.

## 9. Commands and Tools

| Command / Path | Purpose | What it shows |
|----------------|---------|---------------|
| `ls -l /proc/<pid>/fd` | List open file descriptors for a process | Symbolic links from fd number to path or “socket:[inode]”, “pipe:[inode]”, or “deleted” |
| `readlink /proc/<pid>/fd/N` | Resolve one descriptor | Exact target |
| `lsof -p <pid>` | Human-readable view of open files | Combines fd, type, inode, path |
| `lsof +L1` | Files with link count < 1 (i.e., deleted but still open) | Classic disk-space leak detector |
| `fuser -v <path>` | Processes using a given path | Quick check for a specific file |
| `stat -c '%i' /proc/<pid>/fd/N` | Inode number behind an fd | Confirms identity with a named file |
| `cat /proc/<pid>/limits` | Resource limits | Soft/hard open-file limits |

Always prefer `/proc/<pid>/fd` and `lsof` for investigation; they expose the real kernel state.

## 10. Hands-On Lab

**Objective**  
Observe the three-layer model (file descriptor → open file description → inode) and the effect of sharing versus independent opens.

**Prerequisites**  
Ubuntu in VirtualBox, ability to run processes in the background, `lsof` installed (`sudo apt install lsof` if needed).

**Setup**
```bash
mkdir -p ~/fd-lab
cd ~/fd-lab
```

**Steps**

1. Create a test file and open it with a long-running process that keeps it open:
```bash
echo "initial content" > testfile.txt
# Start a process that opens the file and sleeps
exec 3<> testfile.txt          # open on fd 3 in the current shell
sleep 300 &
SLEEPPID=$!
echo "Sleep PID is $SLEEPPID"
```

2. Inspect the file descriptors of the sleep process (it inherits the open fd only if we arrange it; better approach below).  
Use a clearer demonstration with a small script:

```bash
cat > holdopen.sh << 'EOF'
#!/bin/bash
exec 3<> testfile.txt
echo "Holding fd 3 open, PID $$"
sleep 300
EOF
chmod +x holdopen.sh
./holdopen.sh &
HOLDPID=$!
echo "Hold PID = $HOLDPID"
```

3. Examine the open file:
```bash
ls -l /proc/$HOLDPID/fd
readlink /proc/$HOLDPID/fd/3
ls -li testfile.txt
stat -c '%i' /proc/$HOLDPID/fd/3
```
Confirm that the inode numbers match.

4. Create a second independent open in another process and compare offsets later if desired. For now observe two separate opens:
```bash
# In another terminal or with another script
exec 4<> testfile.txt
ls -l /proc/$$/fd
```

5. Delete the directory entry while the file is still open:
```bash
rm testfile.txt
ls -l /proc/$HOLDPID/fd
# Notice the “deleted” marker
lsof -p $HOLDPID
lsof +L1
```

6. Observe that the process can still read/write through the file descriptor (the inode is still alive).  
Then terminate the holder:
```bash
kill $HOLDPID
# After the process exits, the space is released (check with df if the file was large)
```

7. Demonstrate `dup` sharing (in the current shell):
```bash
exec 5<> testfile2.txt
exec 6>&5          # dup
ls -l /proc/$$/fd
# Both 5 and 6 point to the same open file description
```

**Verification**  
You must be able to show:

- A file descriptor whose target is marked “(deleted)”.  
- Matching inode numbers between a live path and `/proc/<pid>/fd/N`.  
- That `lsof +L1` surfaces the deleted-but-open file.

**Cleanup**
```bash
kill $HOLDPID 2>/dev/null
exec 3>&- 4>&- 5>&- 6>&- 2>/dev/null
cd ~
rm -rf ~/fd-lab
```

## 11. Investigation Lab

**Scenario**  
A monitoring alert shows the root filesystem at 98 % capacity. `du -sh /var/*` does not account for the used space. No large files are visible with normal `find` or `ls`.

**Objective**  
Identify any processes holding deleted files open and determine how much space each is retaining.

**Available tools**  
`lsof`, `lsof +L1`, `/proc/*/fd`, `df`, `du`, `stat`, `find`, `ps`

**Initial clues**  
- `df -h /` reports high usage.  
- `du -x -sh /` reports significantly less.  
- Recent log rotation or application restart activity occurred.

**Investigation questions**  
1. What kernel reference keeps an inode alive after its link count reaches zero?  
2. Which command lists open files that have no remaining directory entries?  
3. How can you map a deleted file shown by `lsof` back to the process and the approximate size still held?  
4. What is the safe remediation once the responsible process is identified?

Work the questions with the tools before reading the solution.

**Solution**  
```bash
sudo lsof +L1
# or
sudo lsof | grep deleted
```
The output shows the process, file descriptor, and often the original path marked “deleted”. The SIZE/OFF column approximates the space still held.  

Remediation is normally to restart or signal the process so that it closes the old file descriptor (and ideally reopens a new log file). Killing the process also works but may be more disruptive. After the last open file description is released, the kernel frees the data blocks and `df` immediately reflects the change.

## 12. Production Failure Scenario

**Incident**  
An application writes large log files. A daily log-rotate job renames the current log and creates a new empty file. The application never reopens its log file descriptor. After several days the filesystem fills, the application continues to write to the deleted file (invisible to `du` and to the new log file), and eventually other services fail because the disk is full.

**Systematic troubleshooting**

1. **Observation**  
   `df -h` shows 100 %; `du -sh /var/log` shows only a few hundred megabytes.

2. **Hypothesis**  
   Deleted-but-still-open log files held by the long-running application.

3. **Evidence gathering**  
   ```bash
   sudo lsof +L1
   sudo lsof -p $(pgrep -f myapp)
   ls -l /proc/$(pgrep -f myapp)/fd
   ```

4. **Confirmation**  
   `lsof` shows the application holding `/var/log/myapp.log (deleted)` with a large SIZE.

5. **Remediation**  
   - Short-term: restart the application (or send the signal the application uses to reopen logs).  
   - Medium-term: configure logrotate to send the correct signal (`postrotate` script).  
   - Long-term: change the application to use a logging library that supports external rotation or to reopen on `SIGHUP`.

6. **Verification**  
   After the process closes the old descriptor, `df` drops and the new log file begins receiving data.

This failure mode is extremely common in production and is a direct consequence of the reference-counting rules you studied.

## 13. Connection to Previous Linux Knowledge

- Processes (Session material on process lifecycle) own the file-descriptor table.  
- `fork()` copies the table; the underlying open file descriptions are shared—exactly the same sharing semantics you saw with other inherited resources.  
- System calls (`open`, `read`, `write`, `close`, `dup`, `fcntl`) are the only way user space manipulates these kernel objects.  
- The inode model from Session 01 is the final target of every file descriptor that refers to a regular file.  
- `/proc/<pid>/fd` is another virtual filesystem view, consistent with the `/proc` material you already know.

## 14. Connection to Future Infrastructure

- **Containers**: each container (and each process inside it) has its own file-descriptor table and mount namespace. Understanding open file descriptions is required to diagnose “why does this container still hold space on the host after I deleted the file?”  
- **Docker / container runtimes**: log drivers and volume mounts interact with the same open-file-description lifetime rules.  
- **Kubernetes**: sidecar log collectors, emptyDir volumes, and persistent volume claims all surface the same “deleted but open” class of problems.  
- **Network sockets**: a socket also appears as a file descriptor; the same table and reference-counting machinery is used.  
- **AI / inference services**: long-running model-server processes that open large model files or log files are subject to exactly the same resource-leak patterns.

## 15. Engineering Questions

1. Why does a file descriptor number by itself carry no information about which file is open? What additional kernel object is required?  
2. After `fd2 = dup(fd1)`, what is shared and what is not shared between `fd1` and `fd2`?  
3. A process opens the same file twice with two separate `open()` calls. Do the two file descriptors share a file offset? Why or why not?  
4. Explain the exact conditions under which the kernel frees the data blocks of a regular file.  
5. How would you prove that `/proc/<pid>/fd/3` and a still-existing path refer to the same inode?  
6. What happens to open file descriptors when a process calls `execve()`? What happens on `fork()`?  
7. Why can `lsof +L1` show files that `find` and `du` cannot see?  
8. A process has reached its `RLIMIT_NOFILE` limit. What symptoms appear, and how do you confirm the cause?  
9. How does the kernel know that it is safe to free an open file description?

## 16. Practical Assignment

Write a small demonstration (shell or C) that:

1. Creates a file and records its inode number and initial size.  
2. Opens the file and keeps the file descriptor open in a child process that periodically writes a timestamp.  
3. In the parent, deletes every directory entry for that inode.  
4. Shows, using `/proc` and `lsof`, that the child still holds the file and that the space is still allocated.  
5. Allows the child to continue writing for a measurable period.  
6. Terminates the child and proves that the space is then released.  

Document the reference counts (link count vs. open-file-description references) at each stage. This assignment forces you to combine process management, inode knowledge, and file-descriptor lifetime.

## 17. Session Completion Test

Answer without notes.

**Conceptual**  
1. Define file descriptor, open file description, and inode, and state how they are related.  
2. What information is stored in an open file description that is not stored in the inode?

**System behavior**  
3. Process A opens a file and then forks. Process B (the child) writes data. Does process A’s file offset change? Why?  
4. A file’s link count drops to zero while three processes still hold it open. When are the data blocks freed?

**Command interpretation**  
5. `ls -l /proc/1234/fd/5` shows `... (deleted)`. What does this tell you?  
6. `lsof +L1` lists a file with SIZE 4 GB. What is the operational significance?

**Troubleshooting**  
7. Describe the evidence you would gather to confirm that a disk-full condition is caused by deleted-but-still-open files.

**Internal**  
8. Trace the steps from the `open()` system call until a file descriptor number is returned to user space.

**Explain in your own words**  
9. Explain why closing a file descriptor does not always free disk space, even if the file has already been unlinked.

## 18. Mastery Criteria

- **Basic understanding**: You can define the three objects and correctly interpret `/proc/<pid>/fd` and simple `lsof` output.  
- **Working understanding**: You can diagnose deleted-but-open files, predict the effect of `dup` and `fork` on offsets, and free space by terminating the responsible process.  
- **Strong understanding**: You can reason about reference counts, explain why certain log-rotation schemes fail, and map every field in `lsof` back to the corresponding kernel object.

## 19. What I Should Now Be Able to Explain

- File-descriptor table (per-process)  
- File descriptor number  
- Open file description and its contents  
- How `open()`, `dup()`, `fork()`, and `close()` manipulate these objects  
- The two independent reference counts that control inode lifetime  
- Why a deleted file can still consume space  
- How to map a file descriptor back to an inode and (when possible) a path  
- The operational use of `lsof +L1` and `/proc/<pid>/fd`

## 20. Next Session

**Next Session Number**  
SESSION 03  

**Next Session Title**  
Hard Links, Symbolic Links, and File Deletion Semantics  

**Why it comes next**  
You now understand both the inode and the file-descriptor machinery that keeps an inode alive. The next session examines the two different ways of creating additional names for a file (hard links vs. symbolic links), the precise semantics of `unlink()`, and the observable differences that matter for tools, backups, containers, and security.
