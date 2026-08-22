# Session 17 — Linux Capabilities — Fine-Grained Privilege Beyond setuid and root

## 1. Position in the Curriculum

**Phase**  
PHASE 1 — Linux Systems Engineering

**Module**  
MODULE 2 — Permissions and Security

**Session**  
SESSION 17 — Linux Capabilities — Fine-Grained Privilege Beyond setuid and root

**Prerequisites**  
- Process credentials, real/effective UID (Sessions 10, 14)  
- setuid/setgid binaries (Session 11)  
- sudo and controlled elevation (Session 16)  
- PAM authentication path (Session 15)

**What this session unlocks**  
Understanding of the kernel’s fine-grained privilege model that decomposes the traditional power of root into distinct capabilities. This is required for modern service hardening, container security, and any design that aims to grant only the privileges a process actually needs.

## 2. Why This Session Exists

You now understand two classic ways privilege is obtained:

- A process runs as UID 0 (root) and therefore bypasses almost all discretionary checks.  
- A setuid-root binary or a sudo rule temporarily elevates a process to UID 0 for a specific command.  

Both approaches are coarse. A program that only needs to bind to port 80 still receives the ability to load kernel modules, perform raw network I/O, override permissions, etc., if it runs as root.

**Linux capabilities** split the power of root into roughly 40 distinct privileges (the exact set grows slowly over time). A process can hold a subset of those privileges without being UID 0. File capabilities allow a binary to acquire specific privileges on exec without being setuid-root.

Capabilities are the foundation of:

- systemd service hardening (`CapabilityBoundingSet`, `AmbientCapabilities`, …)  
- container runtimes (Docker, containerd, CRI-O) that drop capabilities by default  
- security profiles that implement least privilege at the kernel level  

This session gives you the mental model and the practical tools to inspect and control them.

## 3. Learning Objectives

By the end of this session you will be able to:

- Explain why the traditional “root is all-powerful” model is problematic and how capabilities improve it.  
- Describe the five capability sets a process holds (Permitted, Effective, Inheritable, Bounding, Ambient).  
- Interpret the capability-related lines in `/proc/<pid>/status`.  
- Use `capsh`, `getpcaps`, `setpriv`, and `filecap`/`getcap`/`setcap` to inspect and (carefully) modify capabilities.  
- Explain the difference between process capabilities and file capabilities.  
- Show how a binary with file capabilities can obtain privilege without being setuid-root.  
- Relate capabilities to the privilege-dropping behaviour of container runtimes and systemd services.  
- Recognise common capability names (`CAP_NET_BIND_SERVICE`, `CAP_SYS_ADMIN`, `CAP_DAC_OVERRIDE`, etc.) and the danger of overly broad sets.

## 4. Prerequisite Concepts

You already know:

- Effective UID 0 historically granted every privilege.  
- setuid-root binaries change the effective UID on exec.  
- sudo elevates according to policy and then executes a command.  
- Permission checks and many other privileged operations consult process credentials.

## 5. Mental Model

```
Traditional model
  UID == 0  →  every privilege

Capability model
  Process
    Permitted   = maximum privileges the process may ever exercise
    Effective   = privileges currently active for permission checks
    Inheritable = privileges that can be passed across exec
    Bounding    = hard ceiling that limits Permitted across exec
    Ambient     = privileges that remain effective across exec to non-capable binaries

  File
    permitted / inheritable / effective capability sets stored in an extended attribute
    on exec they are combined with the process’s sets according to fixed rules
```

A process does **not** need UID 0 to exercise a capability it holds in its Effective set. Conversely, a process with UID 0 still has its capabilities constrained by the Bounding set (modern kernels).

## 6. Core Concept

### Decomposition of root power

Each capability represents a distinct privileged operation or class of operations. Examples:

| Capability | Typical power |
|------------|---------------|
| `CAP_NET_BIND_SERVICE` | Bind to ports < 1024 |
| `CAP_NET_RAW` | Open raw sockets / write to raw devices |
| `CAP_DAC_OVERRIDE` | Bypass file read/write/execute permission checks |
| `CAP_DAC_READ_SEARCH` | Bypass file read and directory search checks |
| `CAP_SYS_ADMIN` | Broad “catch-all” administrative operations (dangerous) |
| `CAP_SYS_TIME` | Set system clock |
| `CAP_SETUID` / `CAP_SETGID` | Make arbitrary UID/GID transitions |
| `CAP_CHOWN` | Change file ownership arbitrarily |
| `CAP_FOWNER` | Bypass permission checks that require file ownership |
| `CAP_KILL` | Send signals to arbitrary processes |
| `CAP_SYS_MODULE` | Load and unload kernel modules |
| `CAP_SYS_CHROOT` | Use `chroot` |
| `CAP_SYS_PTRACE` | Trace arbitrary processes |
| `CAP_SYS_BOOT` | Reboot the system |

`CAP_SYS_ADMIN` is notoriously broad; holding it is almost equivalent to being root for many practical purposes.

### Process capability sets

- **Permitted** — the ceiling of privileges the process is allowed to move into its Effective set.  
- **Effective** — the privileges that are actually active right now; checked by the kernel on privileged operations.  
- **Inheritable** — privileges that can be preserved across `execve` when the new binary cooperates.  
- **Bounding** — an irreversible ceiling that limits what can ever appear in Permitted after an exec.  
- **Ambient** — a relatively modern set that allows capabilities to be preserved across exec even to non-capability-aware binaries (subject to Bounding and other rules).

### File capabilities

An executable can have capability sets stored in an extended attribute (`security.capability`). On `execve` the kernel computes the new process’s capability sets from:

- the file’s capability sets  
- the calling process’s sets  
- the Bounding set  

This allows, for example, a non-setuid `ping` binary to obtain `CAP_NET_RAW` only, instead of full root.

### Interaction with UID 0

On modern kernels a process with UID 0 still has capabilities; the Bounding set can restrict even root. Clearing capabilities from a root process is a standard hardening technique (used heavily by container runtimes).

## 7. Break It Into the Smallest Important Pieces

### 7.1 Capability as a bit
- Each capability is a bit in a bit mask.  
- The kernel checks the appropriate bit in the Effective set before allowing a privileged operation.

### 7.2 Permitted set
- Privileges the process may raise into Effective (via `capset`).

### 7.3 Effective set
- Privileges that are currently “on” for security checks.

### 7.4 Bounding set
- Hard limit inherited across exec; can only be lowered, never raised.

### 7.5 Ambient set
- Privileges kept across exec to ordinary binaries; useful for service managers and containers.

### 7.6 File capability sets
- Stored on the executable; participate in the exec-time calculation.

### 7.7 Capability-aware versus legacy programs
- Legacy programs assume “UID 0 = all power.”  
- Capability-aware programs can raise/lower individual capabilities and drop those they do not need.

### 7.8 Dropping capabilities
- Best practice: start with needed privileges, then permanently drop the rest (clear from Permitted and Bounding) so they can never be regained.

## 8. What Linux Is Actually Doing

**Privileged operation check (conceptual)**
```
bind(port 80)
    → kernel examines the process’s Effective capability set
    → if CAP_NET_BIND_SERVICE is present (or UID == 0 under older rules)
         → allow
    → else
         → return EACCES / EPERM
```

**execve of a file that has capabilities (simplified)**
```
execve("./mydaemon")
    → kernel reads file capability xattr
    → computes new Permitted / Effective / Ambient according to rules
      involving old process sets, file sets, and Bounding set
    → new image starts with those capabilities
    → UID may still be non-zero
```

**systemd service with CapabilityBoundingSet**
```
systemd reads unit file
    → before exec, adjusts the service process’s Bounding / Permitted /
      Ambient sets according to the unit directives
    → then execs the service binary
```

## 9. Commands and Tools

| Command | Purpose |
|---------|---------|
| `capsh --print` | Show capability state of the current shell |
| `cat /proc/$$/status \| grep -i cap` | Raw capability masks for the current process |
| `getpcaps <pid>` | Human-readable capabilities of a process |
| `getcap /path/to/binary` | File capabilities on an executable |
| `setcap 'cap_net_bind_service=ep' /path/to/binary` | Set file capabilities (requires privilege) |
| `setpriv --dump` | Detailed view of privilege state (util-linux) |
| `capsh --caps=… -- -c '…'` | Run a command with a defined capability set |
| `grep Cap /proc/<pid>/status` | CapInh, CapPrm, CapEff, CapBnd, CapAmb |

Capability masks in `/proc` are hexadecimal bitmaps; tools such as `capsh` and `getpcaps` translate them into names.

## 10. Hands-On Lab

**Objective**  
Inspect the capability sets of ordinary and privileged processes, examine file capabilities on system binaries, and observe the difference between a setuid binary and a capability-bearing binary.

**Setup**  
Ordinary Ubuntu system; some commands require sudo.

```bash
mkdir -p ~/cap-lab
cd ~/cap-lab
```

**Steps**

1. Examine your shell’s capabilities:
```bash
capsh --print
grep -i cap /proc/$$/status
```

2. Examine a root process (e.g. systemd or another daemon):
```bash
pidof systemd
grep -i cap /proc/1/status
getpcaps 1
```

3. Look at file capabilities on common binaries:
```bash
getcap /usr/bin/ping /usr/bin/traceroute /usr/bin/newuidmap 2>/dev/null
ls -l /usr/bin/ping
# Note: modern ping often uses capabilities instead of setuid
```

4. Compare with a classic setuid binary:
```bash
ls -l /usr/bin/passwd
getcap /usr/bin/passwd 2>/dev/null
# passwd is typically still setuid-root rather than capability-based
```

5. Observe a process that has dropped privileges (example — after any sudo command the child may still show residual state; better example is a service):
```bash
# Inspect a network-facing service if present
pidof sshd || pidof nginx || true
# For a chosen PID:
# getpcaps <pid>
# grep Cap /proc/<pid>/status
```

6. (Read-only) Inspect systemd unit capability directives for a service:
```bash
systemctl cat ssh 2>/dev/null | grep -i capability || true
systemctl show ssh -p CapabilityBoundingSet -p AmbientCapabilities 2>/dev/null || true
```

**Verification**  
You must be able to:

- Show the capability sets of your own shell and of PID 1.  
- Identify at least one binary that has file capabilities.  
- Distinguish a setuid-root binary from a capability-enabled binary.  
- Explain what “CapEff” versus “CapBnd” means in `/proc/<pid>/status`.

**Cleanup**
```bash
rm -rf ~/cap-lab
```

## 11. Investigation Lab

**Scenario**  
A service that must bind to port 443 is failing with “Permission denied”. The service is configured to run as a non-root user. The unit file does not mention capabilities. On an older host the same binary worked because it was started as root.

**Objective**  
Determine the missing privilege and the safest way to grant it.

**Available tools**  
`getpcaps`, `/proc/<pid>/status`, `systemctl cat`, `getcap`, `ss -tlnp`, service logs

**Initial clues**  
- Process is not UID 0.  
- Error occurs on the `bind` call to a privileged port.  
- No file capabilities are present on the binary.  
- No `CapabilityBoundingSet` / `AmbientCapabilities` in the unit.

**Investigation questions**  
1. Which capability is required to bind to ports below 1024?  
2. Why did the service work when it ran as root?  
3. What are the two common modern ways to grant only that capability?  
4. Why is granting `CAP_SYS_ADMIN` the wrong answer?

Work the questions before reading the solution.

**Solution**  
Binding to ports < 1024 requires `CAP_NET_BIND_SERVICE`. Root historically possessed every capability, so the bind succeeded.  

Safe fixes (in preferred order):

1. Add to the systemd unit:  
   ```
   AmbientCapabilities=CAP_NET_BIND_SERVICE
   CapabilityBoundingSet=CAP_NET_BIND_SERVICE
   ```  
   (or a carefully reviewed larger set if other privileges are truly required).  

2. Or grant the file capability:  
   ```
   setcap cap_net_bind_service=ep /path/to/binary
   ```  

3. Or redesign to listen on an unprivileged port and use a reverse proxy / redirector that holds the privileged bind.

Verify with `getpcaps` on the running service process and a successful bind.

## 12. Production Failure Scenario

**Incident**  
After a container runtime update, a previously working container that performs a privileged network operation fails. The application log shows `EPERM`. The same image worked on the previous runtime version.

**Systematic troubleshooting**

1. **Observation**  
   Operation that requires a capability now fails; image and application code are unchanged.

2. **Hypothesis**  
   The new runtime drops more capabilities by default, or the security profile (seccomp/AppArmor/SELinux) has tightened.

3. **Evidence**  
   ```bash
   # On the host, inspect the container’s process
   getpcaps <container-pid>
   grep Cap /proc/<container-pid>/status
   # Compare with runtime documentation / previous version defaults
   # Inspect any explicit --cap-add / --cap-drop or securityContext
   ```

4. **Confirmation**  
   The required capability is absent from the Effective (and Permitted) set inside the container.

5. **Resolution**  
   - Add only the needed capability via the runtime’s `--cap-add` (Docker) or the equivalent Kubernetes `securityContext.capabilities.add`.  
   - Prefer dropping everything and adding back the minimum set.  
   - Avoid `--privileged` (which grants almost all capabilities plus other powers).  

6. **Prevention**  
   Treat capability requirements as part of the application’s contract; document them; test under the same runtime defaults used in production.

## 13. Connection to Previous Linux Knowledge

- Capabilities replace the blunt “UID 0 = all power” model that setuid-root and unrestricted sudo rely on.  
- A setuid-root binary still works, but modern practice prefers file capabilities or ambient capabilities so that the process never obtains full root.  
- sudo can be configured to set capabilities rather than full UID 0 in some advanced setups, but the common case is still UID elevation.  
- The credentials examined by the permission-checking paths you studied earlier now include the Effective capability set in addition to UID/GID.  
- PAM and login path establish the initial credentials; capability adjustment usually happens later (systemd, container runtime, or the application itself).

## 14. Connection to Future Infrastructure

- **systemd**: `CapabilityBoundingSet`, `AmbientCapabilities`, `PrivateDevices`, `NoNewPrivileges`, etc., are the primary hardening knobs for services.  
- **Containers**: Docker, containerd, and CRI-O drop most capabilities by default; Kubernetes `securityContext.capabilities` is the pod-level control.  
- **Kubernetes**: Pod Security Standards and admission policies often forbid `privileged` and restrict added capabilities.  
- **AI infrastructure**: training and inference processes rarely need broad capabilities; GPU access is typically mediated by device files and group membership rather than capabilities, but any component that binds privileged ports or manipulates network namespaces will require careful capability selection.  
- **Least-privilege design**: capabilities are the kernel mechanism that makes “run as non-root with only the privileges you need” practical.

## 15. Engineering Questions

1. Why is running a process as UID 0 considered coarser than granting it a small set of capabilities?  
2. What is the difference between the Permitted and Effective capability sets?  
3. What does the Bounding set prevent?  
4. How can a non-root binary obtain `CAP_NET_BIND_SERVICE`?  
5. Why is `CAP_SYS_ADMIN` regarded as especially dangerous?  
6. How do file capabilities differ from the setuid bit?  
7. What does `getcap /usr/bin/ping` typically show on a modern distribution, and why?  
8. How does a container runtime use capabilities to implement least privilege?  
9. Why might a service that worked as root fail after being switched to a non-root user even though the binary and configuration are unchanged?

## 16. Practical Assignment

1. Produce a table for your lab system showing:  
   - the capability sets of your login shell  
   - the capability sets of PID 1  
   - any file capabilities you find under `/usr/bin` and `/usr/sbin`  

2. For a service of your choice, determine from its unit file and from the running process which capabilities it actually holds. Note any discrepancy between the unit’s stated bounding set and the process’s effective set.  

3. Design (on paper) the minimal capability set required for a hypothetical service that:  
   - binds to port 443  
   - reads files in `/etc/ssl`  
   - writes logs under `/var/log/myapp`  
   Justify each capability and explicitly list capabilities you would drop.  

4. Write a short warning note about the use of `CAP_SYS_ADMIN` and `--privileged` containers.

## 17. Session Completion Test

Answer without notes.

**Conceptual**  
1. What problem do Linux capabilities solve?  
2. Name the five process capability sets and give a one-sentence purpose for each.

**System behavior**  
3. A process has `CAP_NET_BIND_SERVICE` in its Effective set but is not UID 0. Can it bind to port 80?  
4. What prevents a process from regaining a capability it has removed from its Bounding set?

**Command interpretation**  
5. What does `getcap /path/to/binary` display?  
6. What information do the `CapEff` and `CapBnd` lines in `/proc/<pid>/status` convey?

**Troubleshooting**  
7. A non-root service fails to bind to a privileged port. Which capability is the first to check for, and where do you look?

**Internal**  
8. Describe how file capabilities influence the capability sets of a new process at `execve` time.

**Explain in your own words**  
9. Explain why granting a process only `CAP_NET_BIND_SERVICE` is preferable to running it as root.

## 18. Mastery Criteria

- **Basic understanding**: You know that capabilities decompose root power and can inspect the capability sets of a process and a binary.  
- **Working understanding**: You can identify which capability is missing for a common failure (privileged bind, raw sockets, etc.), read systemd and container capability settings, and distinguish file capabilities from setuid.  
- **Strong understanding**: You can design a minimal capability set for a service, explain the five process sets and the exec-time rules at a conceptual level, and evaluate the security impact of adding or dropping individual capabilities.

## 19. What I Should Now Be Able to Explain

- Motivation for capabilities versus monolithic root  
- The five process capability sets (Permitted, Effective, Inheritable, Bounding, Ambient)  
- Meaning of common capabilities (`CAP_NET_BIND_SERVICE`, `CAP_SYS_ADMIN`, `CAP_DAC_OVERRIDE`, …)  
- File capabilities and how they differ from setuid  
- How to inspect process and file capabilities with standard tools  
- Relationship between capabilities and UID 0 on modern kernels  
- How systemd and container runtimes use capabilities for hardening  
- Why broad capabilities (especially `CAP_SYS_ADMIN`) defeat least privilege

## 20. Next Session

**Next Session Number**  
SESSION 18  

**Next Session Title**  
Introduction to Mandatory Access Control and Security Modules (AppArmor / SELinux overview)  

**Why it comes next**  
You have now completed the discretionary access-control model (permissions, ACLs, identities, PAM, sudo, capabilities). The next session introduces the complementary idea of mandatory access control—policy that even root cannot override—and gives a practical overview of the two dominant Linux implementations, AppArmor and SELinux, so you understand where they fit and when you will need to work with them.
