# Session 03 — Hard Links, Symbolic Links, and File Deletion Semantics

## 1. Position in the Curriculum

**Phase**  
PHASE 1 — Linux Systems Engineering

**Module**  
MODULE 1 — Filesystems

**Session**  
SESSION 03 — Hard Links, Symbolic Links, and File Deletion Semantics

**Prerequisites**  
- Inodes, directory entries, and path resolution (Session 01)  
- File descriptors, open file descriptions, and reference counting (Session 02)  
- Process file-descriptor tables and `/proc/<pid>/fd`

**What this session unlocks**  
Precise understanding of the two different mechanisms Linux provides for giving multiple names to filesystem objects, the exact rules that govern when data blocks are freed, and the behavioral differences that affect tools, scripts, containers, backups, and security policies.

## 2. Why This Session Exists

You now know that a filename is only a directory entry pointing to an inode, and that an open file description can keep an inode alive after every directory entry has been removed.  

The next practical questions are:

- How can multiple names refer to the same inode?  
- How can a name refer to another name instead of directly to an inode?  
- What exactly does `rm` (or the `unlink()` system call) do?  
- Why do some “deletes” free space immediately while others do not?  
- Why do certain programs and container layers behave differently with hard links versus symbolic links?

This session answers those questions by examining hard links, symbolic links, and the full deletion path.

## 3. Learning Objectives

By the end of this session you will be able to:

- Explain the difference between a hard link and a symbolic link at the level of directory entries and inodes.  
- Create, inspect, and remove both kinds of links and predict the effect on link count and data lifetime.  
- Demonstrate that a hard link shares the same inode while a symbolic link has its own inode whose data is a path string.  
- Show why hard links cannot cross filesystem boundaries and why symbolic links can.  
- Trace the exact sequence of reference-count changes that occur when a file is unlinked while still open.  
- Use `ls -l`, `stat`, `readlink`, `find -type l`, and `find -samefile` to distinguish and locate both kinds of links.  
- Predict the observable results of common operations (`cp`, `mv`, `rm`, `tar`, container layer commits) on each type of link.  
- Explain the security and operational implications of each link type.

## 4. Prerequisite Concepts

You already understand:

- A directory entry maps a name to an inode number.  
- The inode stores the link count, metadata, and data-block map.  
- An open file description holds a reference that can keep the inode alive after the link count reaches zero.  
- Path resolution walks directory entries until it reaches a terminal inode.

We will build directly on those facts.

## 5. Mental Model

```
Hard link
─────────
Directory entry A  ──┐
                     ├──► same inode ──► data blocks
Directory entry B  ──┘

Symbolic link
─────────────
Directory entry S  ──► its own inode ──► data = path string
                                              │
                                              ▼
                                         (path resolution
                                          starts over)
```

- Hard link: multiple names, one inode, one set of data blocks, shared metadata.  
- Symbolic link: a special file whose content is a pathname; the kernel performs an additional path lookup when the symbolic link is traversed.

## 6. Core Concept

### Hard links

A **hard link** is simply an additional directory entry that points to an existing inode. Creating a hard link increments the inode’s link count. There is no “original” versus “copy”; every hard link is equal. The data blocks are freed only when the link count drops to zero **and** no open file descriptions still reference the inode.

Hard links:

- Must reside on the same filesystem (inode numbers are filesystem-local).  
- Cannot normally be created for directories (to avoid cycles and complications in link-count accounting).  
- Share every piece of inode metadata (permissions, owner, timestamps, size).

### Symbolic links (also called soft links)

A **symbolic link** is a special file type (`S_IFLNK`) that has its own inode. The data stored in that inode is a pathname string (the target). When a process opens or otherwise traverses the symbolic link, the kernel replaces the symbolic-link pathname component with the target string and continues path resolution.

Symbolic links:

- May point across filesystem boundaries.  
- May point to directories or to non-existent targets (dangling links).  
- Have their own metadata (owner, permissions—though permissions on the symbolic link itself are largely ignored on modern Linux).  
- Do not increment the link count of the target.

### File deletion semantics

The `unlink()` system call (used by `rm` on non-directories) does only one thing: it removes a directory entry and decrements the inode’s link count. It does **not** directly free data blocks. Data blocks are released later, when both of the following become true:

1. Link count == 0  
2. No remaining kernel references (primarily open file descriptions)

This is why a file can be “deleted” yet continue to consume space and remain readable through any still-open file descriptors.

## 7. Break It Into the Smallest Important Pieces

### 7.1 Hard-link creation (`link()` system call / `ln` without `-s`)
- Adds a new directory entry.  
- Increments `i_nlink` in the inode.  
- No new data blocks are allocated.

### 7.2 Symbolic-link creation (`symlink()` system call / `ln -s`)
- Allocates a new inode of type symbolic link.  
- Stores the target path string as the inode’s data.  
- Creates a directory entry pointing to the new inode.  
- The target’s link count is unchanged.

### 7.3 Path resolution involving a symbolic link
- When the kernel encounters a symbolic-link inode during lookup, it reads the target string and recursively continues resolution (with a recursion limit to prevent loops).  
- The final inode that is opened is the target’s inode (or an error if the target does not exist).

### 7.4 `unlink()` / `rm`
- Removes one directory entry.  
- Decrements the link count of the referenced inode.  
- If the link count reaches zero and the inode is not otherwise busy, the filesystem may free the inode and its data blocks immediately; otherwise they remain until the last reference disappears.

### 7.5 `rmdir()` and directories
- Directories have special link-count rules (`.` and `..`).  
- `rmdir()` requires the directory to be empty (only `.` and `..` remain).

### 7.6 Dangling symbolic links
- A symbolic link whose target does not exist is legal.  
- Opening it yields `ENOENT`; reading the link itself with `readlink()` still succeeds.

### 7.7 Cross-filesystem behavior
- Hard link: fails with `EXDEV`.  
- Symbolic link: permitted; the target path is stored as a string and resolved later.

## 8. What Linux Is Actually Doing

**Creating a hard link**
```
User space: ln existing newname
    ↓
linkat() system call
    ↓
VFS: resolve existing → inode
     resolve directory of newname
     check same filesystem
     add directory entry
     inode->i_nlink++
```

**Creating a symbolic link**
```
User space: ln -s target linkname
    ↓
symlinkat() system call
    ↓
VFS: allocate new inode (S_IFLNK)
     store target string in inode data
     add directory entry pointing to new inode
```

**Unlinking**
```
User space: rm name
    ↓
unlinkat() system call
    ↓
VFS: resolve name → directory entry + inode
     remove directory entry
     inode->i_nlink--
     if (i_nlink == 0 && no open references)
         schedule inode and data blocks for freeing
```

The open-file-description reference count (Session 02) is examined when the last process closes its file descriptor or exits.

## 9. Commands and Tools

| Command | Purpose | Notes |
|---------|---------|-------|
| `ln target linkname` | Create hard link | Fails across filesystems |
| `ln -s target linkname` | Create symbolic link | Target may be relative or absolute |
| `ls -l` | Show link count and symbolic-link targets | Link count appears before owner |
| `ls -i` | Show inode numbers | Hard links share the number |
| `stat` | Full metadata including link count and inode | Works on both types |
| `readlink linkname` | Print symbolic-link target | Does not traverse further |
| `readlink -f` | Canonicalize by following all links | Useful for resolution checks |
| `find . -type l` | Locate symbolic links | |
| `find . -samefile name` | Locate all hard links to a file | Walks the tree looking for matching inode |
| `find . -inum N` | Locate all names for inode N | |
| `namei -l path` | Show each component of path resolution | Excellent for understanding symbolic-link traversal |

## 10. Hands-On Lab

**Objective**  
Observe the structural difference between hard links and symbolic links and the precise deletion semantics when files are still open.

**Setup**
```bash
mkdir -p ~/link-lab
cd ~/link-lab
```

**Steps**

1. Create a regular file and two hard links:
```bash
echo "shared content" > original.txt
ln original.txt hard1.txt
ln original.txt hard2.txt
ls -li
stat original.txt hard1.txt hard2.txt
```
Confirm identical inode numbers and link count 3.

2. Create a symbolic link:
```bash
ln -s original.txt sym.txt
ls -li
stat sym.txt
readlink sym.txt
```
Confirm that `sym.txt` has a different inode and that its size equals the length of the string “original.txt”.

3. Modify content through one hard link and read through the others:
```bash
echo "changed" >> hard1.txt
cat original.txt hard2.txt
```

4. Observe that modifying the symbolic link’s target name is different from modifying the target’s content:
```bash
# This changes the content of the target
echo "via symlink" >> sym.txt
cat original.txt
```

5. Delete names one by one while watching the link count:
```bash
rm original.txt
ls -li
stat hard1.txt
rm hard1.txt
ls -li
stat hard2.txt
```
When the last hard link is removed the file is gone (assuming no open descriptors).

6. Demonstrate deletion while open:
```bash
echo "large enough to notice" > opentest.txt
ln opentest.txt openhard.txt
exec 3<> opentest.txt          # keep open in this shell
rm opentest.txt openhard.txt
ls -l /proc/$$/fd/3
cat /proc/$$/fd/3              # still readable
lsof +L1 | grep opentest || true
exec 3>&-                      # close → space released
```

7. Cross-filesystem test (if you have another filesystem mounted, e.g. a USB or a second VirtualBox disk; otherwise note the expected `EXDEV` error):
```bash
# ln /path/on/other/fs/file .   # should fail for hard link
ln -s /path/on/other/fs/file .  # should succeed
```

8. Dangling symbolic link:
```bash
ln -s /nonexistent/path dangling
ls -l dangling
readlink dangling
cat dangling                    # expect “No such file or directory”
```

**Verification**  
You must be able to show:

- Shared inode + rising/falling link count for hard links.  
- Separate inode whose data is a path string for symbolic links.  
- A file that remains readable through an open file descriptor after every name has been unlinked.

**Cleanup**
```bash
exec 3>&- 2>/dev/null
cd ~
rm -rf ~/link-lab
```

## 11. Investigation Lab

**Scenario**  
A backup script uses `find /data -type f -links +1` to detect hard-linked files and treats them specially. After a migration to a new storage system the script suddenly reports zero hard links, yet many files that used to be hard-linked still appear to share content (identical `md5sum`).

**Objective**  
Determine whether the files are now separate copies or whether another linking mechanism is in use, and explain the observable differences.

**Available tools**  
`ls -li`, `stat`, `find -samefile`, `find -type l`, `readlink`, `md5sum`, `df`, `mount`

**Initial clues**  
- `find -links +1` returns nothing.  
- Content hashes still match for several pairs.  
- The new storage is a different filesystem type (or is network-mounted).

**Investigation questions**  
1. What does a link count greater than 1 prove? What does a link count of 1 **not** prove?  
2. How can two files have identical content without sharing an inode?  
3. What tools distinguish hard links from copies and from symbolic links?  
4. Why might a migration break hard links even if the administrator “copied everything”?

Work the questions before reading the solution.

**Solution**  
A link count of 1 only means there is a single directory entry for that inode on that filesystem. Identical content can exist because:

- The files are independent copies (most common after a naïve `cp -a` or `rsync` without hard-link preservation).  
- Symbolic links are being used (check with `find -type l` and `readlink`).  
- A higher-level deduplication mechanism (filesystem-level or userspace) is present.

`find -samefile` and comparing inode numbers with `ls -i` are the definitive checks for hard links. Most copy tools break hard links unless explicitly told to preserve them (`cp -a`, `rsync -H`, etc.). Crossing filesystem boundaries always breaks hard links.

## 12. Production Failure Scenario

**Incident**  
A log-rotation system removes the old log file and creates a new empty file with the same name. The application continues writing to the old inode (still open). Space is not reclaimed. In addition, a monitoring script that follows symbolic links begins reporting errors because a symbolic link that previously pointed at the active log now points at a deleted name.

**Systematic approach**

1. **Observation**  
   Disk usage high; new log file size stays zero; application logs continue to grow somewhere.

2. **Hypothesis A**  
   Classic deleted-but-still-open file (Session 02).

3. **Evidence**  
   `lsof +L1` shows the application holding the old log.  
   `ls -l /proc/<pid>/fd` confirms “(deleted)”.

4. **Hypothesis B**  
   Symbolic links used by other tools now dangle or point at the wrong object.

5. **Evidence**  
   `find /var/log -type l -ls` and `namei -l` on the paths the monitoring script uses.

6. **Resolution**  
   - Restart or signal the application so it closes the old descriptor and opens the new file.  
   - Fix logrotate `postrotate` script.  
   - Replace fragile symbolic-link schemes with clearer naming or with the application’s native reopen mechanism.  
   - Prefer hard links only when same-filesystem multiple names are truly required; otherwise prefer explicit renaming or application-level cooperation.

## 13. Connection to Previous Linux Knowledge

- Directory entries and inodes (Session 01) are the structures that hard links manipulate directly.  
- Open file descriptions and reference counting (Session 02) explain why unlinking is not the same as freeing space.  
- Path resolution is the common mechanism that both hard links (direct inode) and symbolic links (extra lookup) use.  
- Process inheritance of file descriptors interacts with deletion: a child can keep an inode alive after the parent has unlinked every name.

## 14. Connection to Future Infrastructure

- **Container images**: OverlayFS and similar union filesystems make extensive use of hard links internally for efficiency; understanding link counts helps diagnose “why is this layer larger than expected?”  
- **Docker / Podman**: `COPY --link` and image-layer deduplication rely on hard-link semantics. Symbolic links inside images are stored as symbolic links and can become dangling when volumes or bind-mounts change.  
- **Kubernetes**: ConfigMaps and Secrets projected as volumes can contain symbolic links; volume mounts can break or override them.  
- **Backup and migration tools**: Failure to preserve hard links inflates storage and changes performance characteristics.  
- **Security**: Symbolic links are a classic source of race conditions (time-of-check-to-time-of-use) in setuid programs and in container entrypoints; hard links have different attack surfaces (same-filesystem only).

## 15. Engineering Questions

1. Why can’t a hard link normally be created for a directory?  
2. Why does creating a hard link fail across filesystems while creating a symbolic link succeeds?  
3. After `ln -s target link` and then `rm target`, what does `cat link` return? What does `readlink link` return?  
4. A file has link count 3. You open it, then remove two of the names. What is the link count? When will the data blocks be freed?  
5. How would you prove that two names are hard links to each other rather than independent copies?  
6. Why do most programs that “follow links” follow symbolic links but treat hard links as ordinary files?  
7. What does the size field of a symbolic link mean?  
8. How can a symbolic link create a loop, and how does the kernel defend against it?  
9. Why might a container image that uses many hard links be smaller than the same content stored as independent files?

## 16. Practical Assignment

Construct a directory tree that simultaneously demonstrates:

1. Multiple hard links to the same regular file.  
2. A symbolic link to that file.  
3. A symbolic link to a directory.  
4. A dangling symbolic link.  
5. A file that is unlinked while held open by a background process.

For each object record:

- inode number  
- link count  
- file type  
- result of `readlink` (where applicable)  
- result of reading through an open file descriptor after unlinking

Then write a short explanation of what would happen to each object if the entire tree were copied with `cp -a` versus `rsync -aH` versus a naïve `cp -r`.

## 17. Session Completion Test

Answer without notes.

**Conceptual**  
1. What is the fundamental difference between a hard link and a symbolic link?  
2. What does `unlink()` actually do, and what does it not do?

**System behavior**  
3. A file has three hard links. You delete two of them. What is the link count? Is the data still reachable?  
4. You create a symbolic link, then delete the target. What happens when you open the symbolic link?

**Command interpretation**  
5. `ls -l` shows `lrwxrwxrwx ... sym -> original.txt`. What parts of that line describe the symbolic link’s own inode versus the target?  
6. `stat` on two names shows identical inode numbers and a link count of 2. What kind of relationship exists?

**Troubleshooting**  
7. After a file is removed, `df` still shows the space consumed. Which two reference counts must both reach zero before the space is reclaimed?

**Internal**  
8. Describe the steps the kernel takes when resolving a path that contains a symbolic link.

**Explain in your own words**  
9. Explain why hard links are restricted to a single filesystem while symbolic links are not.

## 18. Mastery Criteria

- **Basic understanding**: You can create and identify hard links and symbolic links and correctly interpret link counts.  
- **Working understanding**: You can predict the effect of `rm` on open files, locate all hard links to an inode, and diagnose dangling symbolic links.  
- **Strong understanding**: You can reason about reference-count interactions, explain the impact on backup/container tools, and choose the appropriate link type for a given operational requirement.

## 19. What I Should Now Be Able to Explain

- Hard link = additional directory entry to an existing inode  
- Symbolic link = special inode whose data is a path string  
- Link-count versus open-file-description reference count  
- Exact semantics of `unlink()`  
- Why space reclamation is deferred  
- Cross-filesystem rules  
- How to observe each type with standard tools  
- Operational consequences for deletion, backup, and containers

## 20. Next Session

**Next Session Number**  
SESSION 04  

**Next Session Title**  
Filesystem Hierarchy, Mount Points, and the VFS (Virtual File System)  

**Why it comes next**  
You now understand files, inodes, names, and the references that keep them alive. The next step is to understand how multiple filesystems are combined into a single tree, what a mount point really is, how the VFS routes operations to the correct filesystem implementation, and how `/proc`, `/sys`, `/dev`, and ordinary disk filesystems all appear under one hierarchy.
