# Session 12 — umask, Default Permissions, and File Creation Semantics

## 1. Position in the Curriculum

**Phase**  
PHASE 1 — Linux Systems Engineering

**Module**  
MODULE 2 — Permissions and Security

**Session**  
SESSION 12 — umask, Default Permissions, and File Creation Semantics

**Prerequisites**  
- Classic discretionary permissions and mode bits (Session 10)  
- Special bits: setuid, setgid, sticky (Session 11)  
- Process credentials and effective UID/GID  
- Inode ownership and directory inheritance rules (setgid directories)

**What this session unlocks**  
Precise control over the permissions that newly created files and directories receive. This is required for writing secure services, deployment scripts, and shared-directory workflows, and for understanding why “the file was created with the wrong mode.”

## 2. Why This Session Exists

You now know how the kernel evaluates existing permission bits and how the special bits alter execution identity and deletion rules.  

The remaining everyday question is: **when a process creates a new file or directory, where do the initial mode bits come from?**

The answer is a simple but frequently misunderstood calculation:

```
final_mode = requested_mode & ~umask
```

(with additional rules for directories and for the special bits).  

Without a clear mental model of umask and creation semantics you will repeatedly encounter:

- world-writable files created by a service  
- directories that are unexpectedly inaccessible to the group  
- scripts that work interactively but produce wrong permissions under systemd or cron  
- shared project directories whose new files do not inherit the intended group or mode  

This session closes that gap.

## 3. Learning Objectives

By the end of this session you will be able to:

- Explain what the umask is and where it is stored (per-process).  
- Compute the mode that a newly created file or directory will receive given a requested mode and a umask.  
- Inspect and change the umask of the current shell and of a running process.  
- Predict the effect of umask on ordinary files, directories, and on the setgid-directory inheritance rule.  
- Show why interactive shells, cron jobs, and systemd services often produce different default permissions.  
- Set a deliberate umask in a shell script, a service unit, or a shared-directory workflow.  
- Diagnose “wrong permissions on newly created files” incidents with evidence.

## 4. Prerequisite Concepts

You already know:

- Mode bits are stored in the inode and evaluated as owner/group/other rwx.  
- Directories have different semantics for rwx than regular files.  
- A setgid directory causes new objects to inherit its group.  
- Each process has its own credentials and, as you will see, its own umask.

## 5. Mental Model

```
Process
  umask (e.g. 0022)
           │
           ▼
open(..., O_CREAT, 0666)   or   mkdir(..., 0777)
           │
           ▼
kernel:  final_mode = requested_mode & ~umask
           │
           ▼
new inode is created with final_mode
(+ ownership, and group inheritance if setgid directory)
```

Key points:

- umask is a **mask of bits to turn off**, not the final mode.  
- The creating process supplies a requested mode; the umask clears bits from it.  
- umask is per-process and is inherited across `fork`/`exec` (unless explicitly changed).

## 6. Core Concept

### What umask is

The **umask** (user file-creation mode mask) is a per-process value that specifies which permission bits must **not** be set when a new file or directory is created.

Typical values:

| umask | Binary intuition | Common effect on files (requested 666) | Common effect on directories (requested 777) |
|-------|------------------|----------------------------------------|---------------------------------------------|
| 0022  | clear write for group & other | 644 (`rw-r--r--`) | 755 (`rwxr-xr-x`) |
| 0002  | clear write for other only | 664 (`rw-rw-r--`) | 775 (`rwxrwxr-x`) |
| 0077  | clear group & other entirely | 600 (`rw-------`) | 700 (`rwx------`) |
| 0000  | clear nothing | 666 | 777 |

### The calculation

For a regular file most creation paths request mode `0666`.  
For a directory most request `0777`.  

The kernel then does:

```
inode->i_mode = (requested_mode & ~current->fs->umask) & 0777
```

(Special bits in the requested mode are treated specially and are not freely granted to unprivileged processes.)

### Where umask comes from

- Login shells obtain an initial umask from system-wide and user shell configuration (`/etc/profile`, `~/.profile`, `~/.bashrc`, PAM modules, etc.).  
- A process inherits the umask of its parent across `fork` and `exec`.  
- Any process may change its own umask with the `umask()` system call (or the shell built-in of the same name).  
- systemd service units can set `UMask=` explicitly.  
- cron, at, and other task runners often start with a more restrictive umask than an interactive login.

### Interaction with setgid directories

When a file is created inside a setgid directory:

- the group owner is taken from the directory (Session 11)  
- the mode is still subject to the creating process’s umask  

Thus both mechanisms must be correct for a shared project directory to work as intended.

## 7. Break It Into the Smallest Important Pieces

### 7.1 Per-process umask value
- Stored in the process’s filesystem-related state.  
- Visible via the `umask` shell built-in or by reading `/proc/<pid>/status` (in some forms) / inspecting with appropriate tools.

### 7.2 Requested mode
- Supplied by the creating system call (`open` with `O_CREAT`, `mkdir`, `creat`, etc.).  
- Library functions such as `fopen` also imply a requested mode.

### 7.3 Masking operation
- Bits set in the umask are cleared from the requested mode.  
- umask never adds permissions; it only removes them.

### 7.4 Inheritance
- Child processes inherit the parent’s umask.  
- Changing the umask in a shell affects subsequent commands in that shell and its children, not already-running processes.

### 7.5 System defaults and policy
- Distributions choose a default (commonly 0022 or 0002).  
- Security-conscious environments often prefer 0077 for service accounts.

### 7.6 Special bits and creation
- Unprivileged processes cannot create setuid/setgid files simply by requesting those bits; the kernel clears them unless specific conditions are met.  
- The sticky bit on a newly created directory is also subject to policy.

## 8. What Linux Is Actually Doing

**File creation path (simplified)**
```
open("newfile", O_CREAT | O_WRONLY, 0666)
    → VFS / filesystem
    → final_mode = 0666 & ~current_umask
    → allocate inode
    → inode->i_mode = final_mode (plus type bits)
    → inode->i_uid  = effective UID
    → inode->i_gid  = effective GID  (or directory GID if setgid dir)
    → create directory entry
```

**Shell umask command**
```
umask 0027
    → calls umask() system call
    → updates the umask of the shell process
    → all subsequent child processes inherit the new value
```

## 9. Commands and Tools

| Command | Purpose |
|---------|---------|
| `umask` | Display current umask (shell built-in) |
| `umask 0022` | Set umask for the current shell and its children |
| `umask -S` | Display umask in symbolic form |
| `touch file; ls -l file` | Quick test of resulting mode |
| `mkdir dir; ls -ld dir` | Same for directories |
| `stat -c '%a %A' file` | Octal and symbolic mode after creation |
| `systemctl show -p UMask <service>` | UMask used by a systemd service (if set) |
| `grep -r umask /etc/profile* /etc/bash* ~/.profile ~/.bashrc 2>/dev/null` | Where interactive umask is configured |

## 10. Hands-On Lab

**Objective**  
Observe how different umask values change the mode of newly created files and directories, and confirm inheritance.

**Setup**
```bash
mkdir -p ~/umask-lab
cd ~/umask-lab
```

**Steps**

1. Record the starting umask:
```bash
umask
umask -S
```

2. Create a file and a directory with the default umask:
```bash
touch default-file
mkdir default-dir
ls -l default-file
ls -ld default-dir
stat -c '%a %A %n' default-file default-dir
```

3. Change umask and repeat:
```bash
umask 0077
touch private-file
mkdir private-dir
ls -l private-file
ls -ld private-dir
```

4. Change to a group-friendly umask:
```bash
umask 0002
touch group-file
mkdir group-dir
ls -l group-file
ls -ld group-dir
```

5. Demonstrate inheritance:
```bash
umask 0027
bash -c 'umask; touch child-file; ls -l child-file'
# The child shell inherited 0027
```

6. Restore a sensible umask and clean up the demonstration files if desired:
```bash
umask 0022
```

7. (Optional) Inspect a systemd service:
```bash
systemctl show -p UMask ssh || true
systemctl show -p UMask cron  || true
```

**Verification**  
You must be able to:

- Predict the mode of a file created with requested 0666 under umask 0022, 0002, and 0077.  
- Show that a child process inherits the parent’s umask.  
- Produce both a world-readable and a fully private file simply by changing umask before creation.

**Cleanup**
```bash
rm -rf ~/umask-lab
umask 0022          # or your preferred default
```

## 11. Investigation Lab

**Scenario**  
A service running under systemd creates log files that are mode `666` (world-writable). Security scanning flags them. The same service, when started manually from an interactive shell, creates logs with mode `644`.

**Objective**  
Explain the discrepancy and prescribe a fix that does not require changing application code.

**Available tools**  
`umask`, `systemctl cat`, `systemctl show`, `ls -l`, process inspection, service unit files.

**Initial clues**  
- Interactive shell umask is 0022.  
- Files created by the service are 666.  
- Application code requests the conventional 0666.

**Investigation questions**  
1. What umask would cause a requested mode of 0666 to become 666?  
2. Where can a systemd service’s umask be set?  
3. Why might the service’s umask differ from that of an interactive login?  
4. What is the safest permanent fix?

Work the questions before reading the solution.

**Solution**  
A umask of `0000` leaves the requested mode unchanged, producing `666`. Systemd services that do not set `UMask=` inherit a default (historically often 0022, but the effective value can differ by distribution and by how the service is launched). In this scenario the service is running with a clear-all umask.

Fix:
```ini
# in the service unit (or a drop-in)
[Service]
UMask=0022
```
Then `systemctl daemon-reload && systemctl restart <service>`.  
Newly created logs receive mode 644. Confirm with `ls -l` after the service writes a new file.

## 12. Production Failure Scenario

**Incident**  
A shared project directory is mode `2775` (setgid, group-writable). Developers complain that some newly created files are group-readable/writable while others are only owner-accessible, depending on which user or CI job created them.

**Systematic troubleshooting**

1. **Observation**  
   Mixed final modes on new files inside the same setgid directory.

2. **Hypothesis**  
   Different creators are running with different umask values.

3. **Evidence**  
   ```bash
   # As each user / in each CI job context
   umask
   touch /shared/project/test-$USER
   ls -l /shared/project/test-$USER
   ```
   Compare the umask values and the resulting modes.

4. **Confirmation**  
   Interactive users have umask 0002 (producing 664); CI jobs have umask 0022 (producing 644).

5. **Resolution**  
   - Standardise umask for all creators that write into the shared tree (shell profile, CI configuration, service `UMask=`).  
   - Or accept the more restrictive default and explicitly `chmod`/`chgrp` after creation if the application supports it.  
   - Document the required umask for the project.

6. **Prevention**  
   Configuration management or container images that set a consistent umask for all roles that write to shared areas.

## 13. Connection to Previous Linux Knowledge

- The mode bits calculated at creation time are exactly the bits later evaluated by the permission logic of Session 10.  
- setgid directory group inheritance (Session 11) occurs at the same moment the umask is applied; both affect the new inode.  
- The creating process’s effective UID/GID become the owner/group of the new inode (unless group is overridden by setgid).  
- Because umask is per-process and inherited, the process model and credential inheritance you studied earlier directly determine the permissions of everything a service or script creates.

## 14. Connection to Future Infrastructure

- **systemd**: `UMask=` is the standard way to control creation modes for services.  
- **Containers**: the umask inside a container is whatever the entrypoint process sets or inherits; volume-mounted shared data often requires explicit umask or post-creation `chmod`.  
- **Kubernetes**: application containers and init containers that write to shared volumes must agree on umask (or use an fsGroup + suitable umask strategy) or files become inaccessible across pods.  
- **CI/CD and AI pipelines**: jobs that materialise datasets, checkpoints, or artefacts into shared storage must run with a deliberate umask; otherwise downstream training jobs fail with permission errors.  
- **Security baselines**: many hardening guides require a restrictive default umask (0077 or 0027) for service accounts.

## 15. Engineering Questions

1. What does the umask value represent—bits to set or bits to clear?  
2. Given requested mode 0666 and umask 0027, what is the final mode of a new file?  
3. Why do directories commonly end up with mode 755 when the umask is 0022?  
4. Does changing the umask in a shell affect already-running processes?  
5. How does umask interact with a setgid directory?  
6. Why might a systemd service create files with different permissions than the same binary started from an interactive shell?  
7. What is a reasonable umask for a service that should never create world-readable files?  
8. Can an ordinary user create a setuid binary simply by requesting the setuid bit at creation time?  
9. Where would you set the umask for a program started by cron versus one started by systemd?

## 16. Practical Assignment

1. Construct a table for umask values 0000, 0002, 0022, 0027, and 0077 showing the resulting mode for:  
   - a file created with requested 0666  
   - a directory created with requested 0777  

2. Write a small shell script that forces a known umask, creates a file and a directory, and prints their modes. Run it both interactively and under a simulated service environment (e.g. `env -i` or a minimal systemd unit) and explain any difference.

3. Given a setgid shared directory, demonstrate the combination of group inheritance and umask that produces new files with mode 664 and the correct group. Document the exact commands.

4. Produce a short “creation-permissions” checklist for a new service you are about to deploy (what umask, what directory modes, what ownership).

## 17. Session Completion Test

Answer without notes.

**Conceptual**  
1. What is the umask and what operation does the kernel perform with it at file-creation time?  
2. Does the umask add permissions or remove them?

**System behavior**  
3. A process with umask 0077 creates a file with requested mode 0666. What mode does the file receive?  
4. A child process is started without explicitly setting umask. What umask does it have?

**Command interpretation**  
5. `umask` prints `0022`. What final mode will a typical new file receive?  
6. How do you set the umask for a systemd service without changing the application binary?

**Troubleshooting**  
7. Files created by a service are world-writable. The application requests 0666. What is the most likely umask of the service process?

**Internal**  
8. Describe the steps from an `open(…, O_CREAT, 0666)` call to the final mode bits stored in the new inode.

**Explain in your own words**  
9. Explain why two users writing into the same setgid directory can still produce files with different permission modes.

## 18. Mastery Criteria

- **Basic understanding**: You can display and set umask and predict the mode of a newly created file under common umask values.  
- **Working understanding**: You can diagnose services or scripts that create files with unexpected permissions and correct the umask at the appropriate layer (shell, unit file, etc.).  
- **Strong understanding**: You can design the combination of umask, directory mode, setgid bit, and ownership that produces the intended permissions for a multi-user or multi-service shared workspace, and you can explain the inheritance rules that make the design reliable.

## 19. What I Should Now Be Able to Explain

- Meaning and per-process nature of umask  
- Exact calculation `requested & ~umask`  
- Typical umask values and their effects on files and directories  
- Inheritance of umask across fork/exec  
- Interaction of umask with setgid directories  
- How to set umask for interactive shells, scripts, and systemd services  
- Diagnosis of “wrong default permissions” incidents  
- Why creation-time permissions are a security and operability concern

## 20. Next Session

**Next Session Number**  
SESSION 13  

**Next Session Title**  
Access Control Lists (ACLs) — Extending the Owner/Group/Other Model  

**Why it comes next**  
You now have complete control over the classic owner/group/other permission model, including creation defaults and special bits. The next session introduces Access Control Lists (ACLs), the mechanism that allows permissions to be granted to specific users and groups beyond the single owner and single group stored in the inode, which is required for many real-world shared-data and multi-team workflows.
