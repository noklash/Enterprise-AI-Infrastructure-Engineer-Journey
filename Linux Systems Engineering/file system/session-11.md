# Session 11 — Special Permission Bits: setuid, setgid, and the Sticky Bit

## 1. Position in the Curriculum

**Phase**  
PHASE 1 — Linux Systems Engineering

**Module**  
MODULE 2 — Permissions and Security

**Session**  
SESSION 11 — Special Permission Bits: setuid, setgid, and the Sticky Bit

**Prerequisites**  
- Classic discretionary permissions, ownership, UID/GID, mode bits (Session 10)  
- Process credentials (real vs effective UID/GID)  
- Inodes and directory semantics  
- Path resolution and “Permission denied” diagnosis

**What this session unlocks**  
Understanding of the three special mode bits that alter execution identity or directory deletion rules. This is required before studying Linux capabilities, sudo, privileged binaries, shared directories such as `/tmp`, and the security model of containers.

## 2. Why This Session Exists

You now understand ordinary owner/group/other rwx permissions and how the kernel evaluates them.  

Three additional bits in the mode field change behaviour in important ways:

- **setuid** — on an executable, causes the process to run with the file owner’s UID as its effective UID.  
- **setgid** — on an executable, causes the process to run with the file group’s GID as its effective GID; on a directory, causes new objects to inherit the directory’s group.  
- **sticky bit** — on a directory, restricts deletion and renaming so that only the file owner (or the directory owner, or root) may remove an entry.

These bits are the classic Unix mechanism for controlled privilege elevation and for safe shared directories. They are also a frequent source of security vulnerabilities when misapplied. Mastering them is essential before moving on to capabilities and modern privilege management.

## 3. Learning Objectives

By the end of this session you will be able to:

- Explain the effect of the setuid bit on an executable file and the resulting process credentials.  
- Explain the effect of the setgid bit on both executables and directories.  
- Explain the effect of the sticky bit on a directory and why `/tmp` uses it.  
- Identify the special bits in `ls -l` output and in octal mode.  
- Create controlled examples of each bit and demonstrate their observable behaviour.  
- Predict the security consequences of a setuid binary owned by root.  
- Diagnose common problems (and risks) involving these bits.  
- Relate setuid/setgid to the real versus effective UID/GID model introduced in Session 10.

## 4. Prerequisite Concepts

You already know:

- Every process has real and effective UID/GID.  
- Effective IDs are used for permission checks.  
- Mode bits are stored in the inode and interpreted according to file type.  
- Directory write permission controls creation/deletion of names, subject to further rules.

## 5. Mental Model

```
Ordinary executable
  process effective UID = user’s UID

setuid executable owned by root
  process effective UID = 0 (root)   ← privilege elevation

setgid executable
  process effective GID = file’s GID

Directory with setgid
  new files/dirs inherit the directory’s group

Directory with sticky bit (e.g. /tmp)
  only the owner of a file (or root) may delete it
  even if the directory is world-writable
```

## 6. Core Concept

### setuid (set user ID)

When the setuid bit is set on an **executable regular file**, a process that execs that file has its **effective UID** set to the file’s owner UID.

Classic example: `/usr/bin/passwd` is owned by root and is setuid. An ordinary user who runs `passwd` temporarily obtains root privileges for the carefully limited purpose of changing their password.

Octal representation: the setuid bit is the value 4 in the high digit (e.g. `4755`).

In `ls -l` the owner execute bit is shown as `s` (or `S` if execute is not also set):

```
-rwsr-xr-x  root root  …  /usr/bin/passwd
```

### setgid (set group ID)

On an **executable**: the process’s effective GID becomes the file’s group.  

On a **directory**: new files and subdirectories created inside it inherit the directory’s group rather than the creating process’s primary group. This is widely used for shared project directories.

Octal: value 2 in the high digit (e.g. `2755`).  
In `ls -l` the group execute bit appears as `s`.

### Sticky bit

On a **directory**: a file inside the directory may be deleted or renamed only by:

- the owner of the file, or  
- the owner of the directory, or  
- root  

This allows a directory to be world-writable (so anyone may create files) while preventing users from deleting one another’s files. `/tmp` and `/var/tmp` are the standard examples.

Octal: value 1 in the high digit (e.g. `1777`).  
In `ls -l` the other execute bit appears as `t` (or `T`).

### Security implications

A setuid-root binary is a direct path to privilege elevation. Any vulnerability in such a binary is usually a full system compromise. Modern practice therefore minimises setuid binaries and prefers Linux capabilities or other constrained mechanisms (studied later).

## 7. Break It Into the Smallest Important Pieces

### 7.1 setuid bit on an executable
- Stored in the inode mode.  
- Interpreted by the kernel at `execve` time.  
- Changes only the effective UID (real UID stays the same unless the program also changes it).

### 7.2 setgid bit on an executable
- Analogous change to effective GID at `execve`.

### 7.3 setgid bit on a directory
- Affects the group ownership of newly created inodes inside that directory.  
- Does not by itself grant write access; ordinary directory permissions still apply.

### 7.4 Sticky bit on a directory
- Additional check performed on `unlink` / `rename` inside that directory.  
- Independent of the ordinary write permission on the directory.

### 7.5 Octal and symbolic notation
- Special bits occupy the highest digit of a four-digit octal mode.  
- Symbolic forms: `u+s`, `g+s`, `+t`, etc.

### 7.6 Finding special-bit files
- `find / -perm -4000` (setuid)  
- `find / -perm -2000` (setgid)  
- `find / -perm -1000` (sticky)  
(Run with care; may require root and can be slow.)

## 8. What Linux Is Actually Doing

**execve of a setuid binary (simplified)**
```
execve("/usr/bin/passwd", …)
    → kernel loads the inode
    → observes setuid bit and owner UID == 0
    → sets the new process’s effective UID to 0
    → real UID remains the calling user’s UID
    → continues loading the binary
```

**unlink inside a sticky directory**
```
unlink("/tmp/someuserfile")
    → kernel checks ordinary write permission on /tmp
    → additionally checks sticky-bit rule:
         if sticky set and caller is not file owner
            and caller is not directory owner
            and caller is not root
         → deny with EACCES / EPERM
```

**Creation inside a setgid directory**
```
open("/shared/project/newfile", O_CREAT, …)
    → ordinary permission checks
    → new inode’s GID is taken from the directory’s GID
      (instead of the process’s effective GID)
```

## 9. Commands and Tools

| Command | Purpose |
|---------|---------|
| `ls -l` | Show special bits as `s`/`S`/`t`/`T` |
| `stat -c '%a %A %n'` | Octal and symbolic mode |
| `chmod u+s file` / `chmod 4755 file` | Add setuid |
| `chmod g+s file-or-dir` / `chmod 2755 …` | Add setgid |
| `chmod +t dir` / `chmod 1777 dir` | Add sticky bit |
| `chmod u-s / g-s / -t` | Remove the bits |
| `find /usr -perm -4000 -ls` | Locate setuid binaries (example) |
| `id` / `ps -o pid,euid,ruid,egid,rgid` | Observe credentials after execution |
| `namei -l` | Still useful for path diagnosis |

## 10. Hands-On Lab

**Objective**  
Create controlled demonstrations of setuid, setgid-on-directory, and the sticky bit, and observe the resulting behaviour.

**Setup**  
Work in a directory you own. Some experiments require a second user or careful use of `sudo`. On a personal VirtualBox VM you may create a temporary user.

```bash
mkdir -p ~/special-lab
cd ~/special-lab
```

**Steps**

1. Sticky-bit demonstration (safe, single-user approximation):
```bash
mkdir shared
chmod 1777 shared
ls -ld shared          # should show drwxrwxrwt
# Create a file
echo "mine" > shared/myfile
# In normal multi-user conditions another user could create files
# but could not delete myfile. On a single-user lab you can still
# observe the mode and the fact that the owner can delete:
rm shared/myfile
```

2. setgid directory:
```bash
mkdir project
chmod 2775 project
ls -ld project         # drwxrwsr-x or similar
# New files inherit the group of “project”
touch project/file1
ls -l project/file1    # group should match the directory’s group
```

3. Observe a real setuid binary (read-only inspection):
```bash
ls -l /usr/bin/passwd
stat -c '%a %A %U %G' /usr/bin/passwd
# Note the owner (root), the setuid bit, and the mode
```

4. (Optional, advanced / careful) Credential change observation  
If you have a second unprivileged user and a trivial setgid or setuid program of your own (never create a setuid-root binary for experimentation unless you fully understand the risk), you can run it under `strace` or inspect `/proc/<pid>/status` for UID lines. Most learners should limit themselves to inspecting system binaries and the directory bits.

5. Search for special bits on the system (may be slow; limit scope):
```bash
find /usr/bin /usr/sbin -perm -4000 -ls 2>/dev/null
find /usr/bin /usr/sbin -perm -2000 -ls 2>/dev/null
find /tmp /var/tmp -maxdepth 0 -perm -1000 -ls
```

**Verification**  
You must be able to:

- Recognise setuid, setgid, and sticky bits in `ls -l` and octal form.  
- Show a directory with the sticky bit and explain its deletion rule.  
- Show a setgid directory and a newly created file that inherited its group.  
- Identify at least one real setuid binary on the system and state its owner.

**Cleanup**
```bash
rm -rf ~/special-lab
```

## 11. Investigation Lab

**Scenario**  
A security scan reports a world-writable directory that does **not** have the sticky bit set. The directory is used by multiple users and automation accounts to exchange temporary files.

**Objective**  
Explain the risk, demonstrate the problem, and prescribe the correct mode.

**Available tools**  
`ls -ld`, `stat`, `chmod`, ordinary file creation/deletion as two different users (or simulated with `sudo -u`).

**Initial clues**  
- Directory mode is `777` or `rwxrwxrwx` with no `t`.  
- Multiple UIDs write into it.  
- The scan flags it as a vulnerability.

**Investigation questions**  
1. What can any user with write access to a non-sticky directory do to other users’ files?  
2. How does the sticky bit change that behaviour?  
3. What is the conventional mode for such a shared temporary directory?  
4. How would you confirm the fix?

Work the questions before reading the solution.

**Solution**  
Without the sticky bit, any user who has write permission on the directory may delete or rename any file inside it, regardless of the file’s ownership. That allows denial-of-service and, in some cases, race-condition attacks against other users’ workflows.

```bash
chmod 1777 /path/to/shared
ls -ld /path/to/shared    # must show drwxrwxrwt
```
The sticky bit restricts unlinking/renaming to the file owner, the directory owner, or root, which is the required behaviour for shared temporary areas.

## 12. Production Failure Scenario

**Incident**  
A legacy application relies on a setuid-root helper binary. After an OS package update the binary loses its setuid bit (or is replaced by a non-setuid version). The application begins failing with “Permission denied” on operations that previously succeeded.

**Systematic troubleshooting**

1. **Observation**  
   Application errors mentioning permission problems on privileged operations.  
   Recent package update on the host.

2. **Hypothesis**  
   The helper is no longer setuid-root (or its ownership changed).

3. **Evidence**  
   ```bash
   ls -l /path/to/helper
   stat -c '%a %A %U %G' /path/to/helper
   # Compare with a known-good host or package documentation
   dpkg -V <package>          # or rpm -V, etc.
   ```

4. **Confirmation**  
   The setuid bit is absent or the owner is no longer root.

5. **Resolution**  
   - Restore the correct ownership and mode **only if** the binary is still the expected, vendor-supplied program.  
   - Prefer migrating away from setuid-root helpers toward capabilities, policykit, or a properly privileged service.  
   - If the bit must be restored temporarily, document the exception and schedule removal of the setuid dependency.

6. **Prevention**  
   Configuration management that enforces (or explicitly forbids) setuid bits, plus package-update testing that includes privilege-path verification.

## 13. Connection to Previous Linux Knowledge

- The special bits are additional flags in the same inode mode field that holds the ordinary rwx bits (Session 10).  
- setuid/setgid change the effective credentials that the permission-checking logic (Session 10) will later use.  
- The sticky-bit check is an extra rule evaluated during the same `unlink`/`rename` path that already checks directory write permission.  
- Process credential structures (real/effective UID/GID) are the same objects you inspected with `id` and `/proc/<pid>/status`.

## 14. Connection to Future Infrastructure

- **Linux capabilities** (next major topic) were introduced largely to replace the coarse “all or nothing” power of setuid-root binaries.  
- **sudo** and policykit are userspace mechanisms that also achieve controlled privilege elevation; they interact with the same credential model.  
- **Containers**: setuid binaries inside images can be security risks; many runtimes and security policies restrict or strip setuid bits. User namespaces further change the meaning of UID 0.  
- **Kubernetes**: securityContext settings (allowPrivilegeEscalation, privileged, capabilities) are the orchestration-level controls that govern whether setuid-style elevation is possible inside pods.  
- **Shared volumes and AI workloads**: multi-user training or data-prep directories often need the setgid-directory pattern or sticky-bit directories so that collaborative write access does not permit mutual deletion.

## 15. Engineering Questions

1. What does the setuid bit do when set on an executable?  
2. Why is a setuid-root binary a high-value target for attackers?  
3. What is the difference between setgid on a file and setgid on a directory?  
4. Why does `/tmp` conventionally have mode `1777`?  
5. How do the special bits appear in `ls -l` output?  
6. A directory is mode `777` with no sticky bit. What attack or accident is possible?  
7. Does the setuid bit affect the real UID or the effective UID?  
8. How would you safely locate all setuid binaries under `/usr`?  
9. Why have many modern systems reduced the number of setuid binaries compared with older Unix systems?

## 16. Practical Assignment

1. Document every setuid and setgid binary you find under `/usr/bin` and `/usr/sbin` on your lab system (command + owner + mode). Note which ones are owned by root.  

2. Create a setgid shared directory, demonstrate that new files inherit its group, and show the exact `chmod`/`chown` sequence you used.  

3. Create a sticky directory, explain (and if possible demonstrate with two users) the deletion rule.  

4. Write a short security note: under what circumstances, if any, you would create a new setuid-root binary, and what alternatives you would prefer.

## 17. Session Completion Test

Answer without notes.

**Conceptual**  
1. What is the effect of the setuid bit on an executable?  
2. What is the effect of the sticky bit on a directory?

**System behavior**  
3. A non-root user runs a setuid-root binary. What is the effective UID of the new process?  
4. A directory has mode `1777`. User A creates a file; may user B delete it?

**Command interpretation**  
5. `ls -l` shows `-rwsr-xr-x 1 root root …`. What special bit is set and who owns the file?  
6. What octal mode corresponds to a sticky, world-writable directory?

**Troubleshooting**  
7. After a package update a previously working privileged helper fails with “Permission denied”. What two properties of the binary do you check first?

**Internal**  
8. At what point in process creation does the kernel apply the setuid bit?

**Explain in your own words**  
9. Explain why the sticky bit is necessary for a world-writable temporary directory.

## 18. Mastery Criteria

- **Basic understanding**: You can recognise and name the three special bits and state their primary effects.  
- **Working understanding**: You can create sticky and setgid directories, identify setuid binaries, and explain the deletion and credential-change rules.  
- **Strong understanding**: You can assess the security risk of a setuid binary, diagnose loss of special bits after updates, and choose the appropriate special bit (or decide that none is appropriate) for a shared-directory design.

## 19. What I Should Now Be Able to Explain

- setuid bit and its effect on effective UID  
- setgid bit on executables and on directories  
- Sticky bit and the deletion rule it enforces  
- Octal and `ls -l` representations of the special bits  
- Why `/tmp` is mode `1777`  
- Security implications of setuid-root binaries  
- How these bits interact with the real/effective credential model  
- How to locate and inspect special-bit files

## 20. Next Session

**Next Session Number**  
SESSION 12  

**Next Session Title**  
umask, Default Permissions, and File Creation Semantics  

**Why it comes next**  
You now understand both ordinary and special permission bits and how they are evaluated. The next practical topic is how the mode of a newly created file or directory is determined—i.e. the interaction between the mode requested by `open`/`mkdir` and the process’s umask—and the operational consequences for services, scripts, and multi-user systems.
