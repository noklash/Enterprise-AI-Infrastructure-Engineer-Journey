# Session 10 — Permissions, Ownership, and the Discretionary Access Control Model

## 1. Position in the Curriculum

**Phase**  
PHASE 1 — Linux Systems Engineering

**Module**  
MODULE 2 — Permissions and Security

**Session**  
SESSION 10 — Permissions, Ownership, and the Discretionary Access Control Model

**Prerequisites**  
- Files, inodes, directory entries, and path resolution (Sessions 01–04)  
- File descriptors and open-file lifetime (Session 02)  
- Filesystem capacity and troubleshooting (Session 09)  
- Processes, UIDs, and the idea that every process runs as a user

**What this session unlocks**  
The ability to reason about who can open, read, write, or execute any file or directory, how the kernel makes that decision, and how ownership and mode bits are stored and changed. This is the foundation for every later security topic (setuid, capabilities, ACLs, sudo, SSH, containers, and Kubernetes RBAC).

## 2. Why This Session Exists

You now have a solid operational model of filesystems: names, inodes, mounts, block devices, page cache, I/O, and capacity.  

The next fundamental question is: **who is allowed to do what** with those files?  

Linux answers this question first with the classic discretionary access-control model:

- Every filesystem object has an **owner** (user) and a **group**.  
- Every process runs with a real and effective user ID (UID) and group ID (GID).  
- Every object has a permission mode that grants or denies read, write, and execute access to the owner, the group, and everyone else.  

Until you understand this model precisely, every later mechanism (setuid binaries, capabilities, Access Control Lists, SELinux/AppArmor, container isolation, Kubernetes service accounts) will be built on an incomplete foundation.

## 3. Learning Objectives

By the end of this session you will be able to:

- Explain the discretionary access-control model used by traditional Unix/Linux permissions.  
- State what is stored in the inode regarding ownership and permissions.  
- Describe how the kernel decides whether a process may open a file for reading, writing, or execution.  
- Use `ls -l`, `stat`, `namei`, `id`, `whoami`, and `groups` to inspect ownership and rights.  
- Change ownership and mode bits with `chown`, `chgrp`, and `chmod` (symbolic and octal forms).  
- Predict the effect of permissions on regular files **and** on directories (the meaning of read/write/execute for directories is different).  
- Explain the difference between real UID/GID and effective UID/GID at a basic level.  
- Diagnose common “Permission denied” failures with evidence rather than guesswork.

## 4. Prerequisite Concepts

You already know:

- An inode stores metadata, including ownership and mode bits.  
- A process has an identity (at minimum a UID and GID).  
- Path resolution walks directory entries; each directory in the path must allow the necessary access.  
- Opening a file creates an open file description that continues to reference the inode even if permissions later change (subject to some nuances).

## 5. Mental Model

```
Process
  real UID / effective UID
  real GID / effective GID (+ supplementary groups)
           │
           ▼
Kernel permission check (on every open / search / …)
           │
           ▼
Inode
  owner UID
  group GID
  mode bits (owner / group / other × read/write/execute)
```

Decision order (simplified classic model):

1. If effective UID == 0 (root) → traditionally allow (modern systems add capabilities, but the classic rule still dominates intuition).  
2. Else if effective UID == inode owner UID → apply owner bits.  
3. Else if effective GID (or any supplementary group) == inode group GID → apply group bits.  
4. Else → apply other bits.

## 6. Core Concept

### Ownership

Every inode records:

- a user owner (UID)  
- a group owner (GID)  

These are simple integer identifiers. Names such as `alice` or `developers` are userspace conveniences provided by `/etc/passwd` and `/etc/group` (studied more fully in a later session).

### Mode bits

The permission mode is a bitfield traditionally written in octal or in symbolic form (`rwxr-xr-x`).

For a regular file:

| Bit | Meaning for a file |
|-----|--------------------|
| r   | May open for reading / may `read()` |
| w   | May open for writing / may `write()` / may truncate |
| x   | May execute (load as a program or script) |

For a directory the same bits have different semantics:

| Bit | Meaning for a directory |
|-----|-------------------------|
| r   | May list the directory (read directory entries) |
| w   | May create, delete, or rename entries **inside** the directory (requires x as well in practice) |
| x   | May traverse / search the directory (pass through it in a path) |

The distinction between file and directory semantics is one of the most common sources of confusion and of “Permission denied” surprises.

### Process credentials

A process carries (among other things):

- real UID / real GID — normally the identity of the user who started it  
- effective UID / effective GID — the identity used for most permission checks  
- supplementary groups — additional GIDs that also grant group rights  

For ordinary programs the real and effective IDs are the same. They diverge when setuid/setgid binaries or certain privilege-management calls are used (next sessions).

### The check occurs at access time

Permission checks are performed when an operation is attempted (open, unlink, rename, etc.). Changing permissions or ownership after a process already holds a file descriptor does not revoke the existing open; the open file description retains its access rights.

## 7. Break It Into the Smallest Important Pieces

### 7.1 UID and GID
- Integer identifiers stored in the inode and in the process credential structure.  
- 0 is root (superuser).

### 7.2 Owner / group / other
- Three categories of principal against which the mode bits are evaluated.

### 7.3 Mode bits (file)
- Classic `rwx` for each of owner, group, other.  
- Stored in the inode; visible with `ls -l` and `stat`.

### 7.4 Mode bits (directory)
- `r` = list, `w` = modify entries, `x` = traverse.  
- Traversing a path requires `x` on every directory component.

### 7.5 Effective versus real IDs (basic)
- Effective IDs are used for permission checks.  
- Real IDs record the original identity (important for later setuid discussion).

### 7.6 Path-resolution permission requirements
- To reach a file you must have search (`x`) permission on every directory in its path.  
- The final file’s own permissions then control the specific open mode.

### 7.7 “Permission denied” (EACCES)
- Returned when any required check fails.  
- The kernel does not tell you which component failed; you must investigate.

## 8. What Linux Is Actually Doing

**open() permission path (simplified)**
```
open("/var/log/app.log", O_RDONLY)
    → VFS path resolution
        for each directory component:
            load inode
            check that the process has x (search) permission
        load final inode
        check that the process has r permission (for O_RDONLY)
    → if all checks pass: allocate open file description and file descriptor
    → else: return -EACCES
```

The same style of check is performed for `unlink`, `rename`, `mkdir`, `execve`, etc., each with its own required bits.

## 9. Commands and Tools

| Command | Purpose |
|---------|---------|
| `ls -l` | Mode bits, owner, group, link count, size |
| `ls -ld dir` | Same information for a directory itself |
| `stat path` | Full metadata including numeric UID/GID and mode |
| `id` | Current real/effective UID/GID and supplementary groups |
| `whoami` / `groups` | Quick identity checks |
| `namei -l path` | Show permissions of every component in a path (extremely useful) |
| `chmod` | Change mode bits (symbolic or octal) |
| `chown` | Change owner and/or group |
| `chgrp` | Change group only |
| `umask` | Default bits that are cleared when new files/directories are created |

Octal examples:

- `644` = `rw-r--r--`  
- `755` = `rwxr-xr-x`  
- `700` = `rwx------`  
- `000` = no permissions for anyone (except root)

## 10. Hands-On Lab

**Objective**  
Observe ownership and mode bits, change them, and prove the effect on access for different users and on directories versus files.

**Setup**  
Ordinary Ubuntu user account; ability to create files in your home directory. For cross-user tests you may use `sudo -u` or a second lightweight user.

```bash
mkdir -p ~/perm-lab
cd ~/perm-lab
```

**Steps**

1. Inspect identity and create a test file:
```bash
id
echo "secret data" > data.txt
ls -l data.txt
stat data.txt
```

2. Remove all permissions and observe failure:
```bash
chmod 000 data.txt
ls -l data.txt
cat data.txt          # expect Permission denied
```

3. Restore owner read/write and verify:
```bash
chmod 600 data.txt
cat data.txt
echo "more" >> data.txt
```

4. Directory permissions:
```bash
mkdir dir
echo "inside" > dir/file.txt
chmod 000 dir
ls dir                 # fail
cat dir/file.txt       # fail (cannot traverse)
chmod 111 dir          # execute/search only
ls dir                 # still fail (no read)
cat dir/file.txt       # succeed if you know the name
chmod 755 dir
ls -l dir
```

5. Path-resolution diagnosis:
```bash
namei -l $HOME/perm-lab/dir/file.txt
```

6. Ownership (careful with sudo):
```bash
sudo chown root:root data.txt
ls -l data.txt
cat data.txt           # may still succeed or fail depending on mode and your identity
sudo chown $USER:$USER data.txt
```

7. umask effect:
```bash
umask
umask 027
touch newfile
ls -l newfile          # observe default mode after umask
```

**Verification**  
You must be able to:

- Make a file unreadable by yourself and then restore access.  
- Show that directory execute permission alone is sufficient to access a known filename, while read is required to list names.  
- Use `namei -l` to display the permission of every path component.

**Cleanup**
```bash
chmod -R u+rwx ~/perm-lab
rm -rf ~/perm-lab
```

## 11. Investigation Lab

**Scenario**  
A colleague reports: “I cannot read `/opt/app/config.yml` even though `ls -l` shows `-rw-r--r--` and the file is owned by a group I belong to.”

**Objective**  
Determine why access is denied and prove the root cause.

**Available tools**  
`ls -ld`, `namei -l`, `id`, `groups`, `stat`, `cat`, `strace`

**Initial clues**  
- The file’s own mode looks permissive.  
- The user is a member of the owning group.  
- The path is `/opt/app/config.yml`.

**Investigation questions**  
1. What additional permissions are required beyond those on the final file?  
2. Which command shows the permissions of every directory in the path?  
3. How do you confirm the effective identity and group membership of the process?  
4. What is the most likely class of failure given the symptoms?

Work the questions before reading the solution.

**Solution**  
Path traversal requires execute permission on **every** directory component. A common pattern is that `/opt/app` (or an intermediate directory) lacks group/other execute permission.

```bash
namei -l /opt/app/config.yml
id
groups
```
The output of `namei` immediately highlights the component that rejects the search. Fixing that directory’s mode (or ownership) resolves the issue. The file’s own mode was never the problem.

## 12. Production Failure Scenario

**Incident**  
After a deployment script runs, an application fails to start with “Permission denied” opening its configuration file and log directory. The deployment was performed by a CI user; the application runs as a dedicated service user.

**Systematic troubleshooting**

1. **Observation**  
   Application journal / stdout shows `EACCES` on specific paths.

2. **Hypothesis set**  
   - Final file/directory mode is too restrictive for the service user.  
   - Ownership was left as the CI user.  
   - An intermediate directory lost execute permission.  
   - The process is not running with the expected UID/GID or supplementary groups.

3. **Evidence**  
   ```bash
   namei -l /path/to/config
   ls -ld /path/to/logdir
   stat /path/to/config
   id <service-user>               # or look at the running process
   ps -o user,group,supgid -p <pid>
   ```

4. **Resolution**  
   - Correct ownership (`chown`) and mode (`chmod`) to values appropriate for the service user.  
   - Prefer a deployment pattern that sets ownership explicitly (or uses a shared group and carefully chosen modes).  
   - Add a post-deployment check that verifies readability/writability under the service account.

5. **Prevention**  
   - Immutable infrastructure or configuration-management rules that enforce ownership and mode.  
   - Application startup checks that produce clear diagnostics when required paths are inaccessible.

## 13. Connection to Previous Linux Knowledge

- Ownership and mode bits live in the inode (Session 01 / 06).  
- Path resolution (Session 01 / 04) is exactly where the directory execute checks occur.  
- The process that performs the check is the same process model you studied earlier; its credentials are part of its kernel state.  
- An already-open file descriptor continues to work after a permission change because the open file description was created when access was still permitted (Session 02).  
- Capacity tools (`df`, `du`) are unaffected by permissions, but permission problems often surface at the same time as capacity problems during deployments.

## 14. Connection to Future Infrastructure

- **setuid / setgid and capabilities** (next sessions) build directly on the effective-UID model introduced here.  
- **ACLs** extend the owner/group/other model with per-user and per-group entries.  
- **Containers**: the UID/GID namespace mapping and the permissions on mounted volumes are among the most common sources of “Permission denied” inside containers.  
- **Kubernetes**: securityContexts, fsGroup, runAsUser, and volume permission recursion all rest on this model.  
- **SSH and service accounts**: every remote login and every service ultimately obtains a UID/GID that is then subjected to these checks.  
- **AI infrastructure**: shared model and dataset volumes must be readable by training jobs that often run as non-root users; incorrect ownership or mode bits are a frequent cause of job failure at scale.

## 15. Engineering Questions

1. What is the difference between the owner bits and the group bits of a file’s mode?  
2. Why does traversing a path require execute permission on every directory component?  
3. A directory has mode `r--r--r--`. What can a process still do, and what can it not do?  
4. Why can a process sometimes still read a file after the file’s permissions have been changed to deny read access?  
5. How does the kernel decide whether to apply owner, group, or other bits?  
6. What does `namei -l` show that a single `ls -l` on the final file does not?  
7. Why is `chmod 777` almost always the wrong solution to a permission problem?  
8. What information does the inode store that `ls -l` displays as the owner and group names?  
9. How does `umask` affect the mode of newly created files and directories?

## 16. Practical Assignment

1. Create a directory hierarchy three levels deep. Intentionally remove execute permission from the middle directory and demonstrate the resulting access failure with both `ls` and `cat` of a file beneath it. Restore access and show the difference with `namei -l`.  

2. Create a file owned by your user. Change its group to a group you belong to and experiment with owner-only versus group-readable modes while testing access.  

3. Write a short checklist an on-call engineer can follow when an application reports “Permission denied” on a path.  

4. Using only the tools introduced so far, prove whether a given process can or cannot read a particular file **without** switching to that process’s user (hint: examine credentials and then simulate the checks).

## 17. Session Completion Test

Answer without notes.

**Conceptual**  
1. What three categories of principal appear in the classic permission model?  
2. What does execute permission mean on a directory?

**System behavior**  
3. A process has read permission on a file but lacks execute permission on one directory in the file’s path. What happens when it tries to open the file?  
4. After a process opens a file for reading, the owner runs `chmod 000` on the file. Can the process still read from its existing file descriptor?

**Command interpretation**  
5. `ls -l` shows `-rw-r----- 1 alice dev 1234 …`. Who may read the file?  
6. What does `namei -l /var/log/app.log` tell you that `ls -l /var/log/app.log` does not?

**Troubleshooting**  
7. A user who is a member of the file’s group still receives “Permission denied”. What is the first path-related check you perform?

**Internal**  
8. Describe the order in which the kernel evaluates owner, group, and other bits.

**Explain in your own words**  
9. Explain why directory permissions control path traversal and why that design is necessary.

## 18. Mastery Criteria

- **Basic understanding**: You can read `ls -l` output and change modes and ownership with `chmod`/`chown`.  
- **Working understanding**: You can diagnose path-traversal failures with `namei`, distinguish file versus directory permission semantics, and predict access outcomes for a given process identity.  
- **Strong understanding**: You can reason about effective credentials, explain why an open file descriptor survives later permission changes, and produce a reliable troubleshooting procedure for “Permission denied” incidents.

## 19. What I Should Now Be Able to Explain

- Discretionary access-control model (owner / group / other)  
- Meaning of r/w/x for files and for directories  
- UID/GID ownership stored in the inode  
- How the kernel selects which set of bits to apply  
- Path-resolution permission requirements  
- Effect of `chmod`, `chown`, `chgrp`, and `umask`  
- Use of `namei -l` and `id` for diagnosis  
- Why “Permission denied” often originates in a directory component rather than the final file

## 20. Next Session

**Next Session Number**  
SESSION 11  

**Next Session Title**  
Special Permission Bits: setuid, setgid, and the Sticky Bit  

**Why it comes next**  
You now understand ordinary ownership and the rwx bits. The next session examines the three special mode bits that modify execution and directory semantics—setuid, setgid, and the sticky bit—and the powerful (and dangerous) behaviours they enable.
