# Session 18 — Introduction to Mandatory Access Control and Security Modules (AppArmor / SELinux overview)

## 1. Position in the Curriculum

**Phase**  
PHASE 1 — Linux Systems Engineering

**Module**  
MODULE 2 — Permissions and Security

**Session**  
SESSION 18 — Introduction to Mandatory Access Control and Security Modules (AppArmor / SELinux overview)

**Prerequisites**  
- Discretionary access control: ownership, mode bits, ACLs (Sessions 10–13)  
- Identities, PAM, sudo, capabilities (Sessions 14–17)  
- Process credentials and the idea that UID 0 is powerful under DAC

**What this session unlocks**  
A clear mental model of Mandatory Access Control (MAC) as a policy layer that sits above (and can restrict) discretionary controls, plus a practical orientation to the two MAC frameworks you will actually encounter on Linux servers—AppArmor and SELinux—so that you can recognise them, determine their state, and know when you must engage with their policy.

## 2. Why This Session Exists

Everything you have studied so far about permissions, ACLs, capabilities, and sudo belongs to **Discretionary Access Control (DAC)**: the owner of an object (or the root user) decides who may access it.  

DAC has a fundamental limitation: once a process is compromised, it can do anything its credentials allow. If that process is running as root, or holds broad capabilities, the attacker inherits those rights. Even carefully written DAC policy cannot stop a root-owned process from reading arbitrary files, because root under DAC is allowed to override discretionary checks.

**Mandatory Access Control (MAC)** adds a second, system-enforced policy:

- Rules are defined by the administrator (or the distribution) and cannot be overridden by ordinary users—or, in the ideal case, even by root.  
- Every process runs in a **security context** (label / profile).  
- Every object (file, socket, capability use, …) also carries a label.  
- The MAC framework intercepts security-relevant operations and allows or denies them according to a policy that is independent of the UID/GID/mode bits.

Linux implements MAC primarily through the Linux Security Modules (LSM) framework. The two dominant, production-relevant LSM-based systems are:

- **AppArmor** — path-based, relatively simple profiles; default on Ubuntu and SUSE.  
- **SELinux** — type-enforcement / label-based, very expressive; default on RHEL, CentOS, Fedora, and many derivatives.

This session gives you the conceptual foundation and the day-to-day commands so that MAC stops being a mysterious source of “Permission denied” and becomes a controllable layer.

## 3. Learning Objectives

By the end of this session you will be able to:

- Explain the difference between Discretionary Access Control (DAC) and Mandatory Access Control (MAC).  
- Describe the role of the Linux Security Modules (LSM) framework.  
- State the core idea of AppArmor (path-based profiles) and of SELinux (label-based type enforcement).  
- Determine whether AppArmor or SELinux is active on a system and in which mode (enforcing / permissive / disabled).  
- List and interpret the basic status commands for each framework.  
- Recognise typical log signatures of MAC denials.  
- Explain why a process that is root under DAC can still be denied by MAC.  
- Know when you need to adjust policy versus when a DAC problem is the real cause.

## 4. Prerequisite Concepts

You already know:

- How DAC decisions are made (owner / group / other, ACLs, capabilities).  
- That UID 0 historically bypasses DAC checks.  
- That capabilities allow finer privilege without full root.  
- That “Permission denied” can originate from multiple layers.

## 5. Mental Model

```
Operation request (open, bind, exec, …)
        │
        ▼
┌───────────────────────┐
│  DAC checks           │  UID/GID, mode bits, ACLs, capabilities
│  (owner can influence)│
└───────────┬───────────┘
            │ allowed by DAC
            ▼
┌───────────────────────┐
│  MAC checks (LSM)     │  AppArmor profile  or  SELinux labels + policy
│  (system policy)      │
└───────────┬───────────┘
            │ allowed by MAC
            ▼
     Operation proceeds
```

Both layers must permit the action. A denial at either layer produces a failure (often the same `EACCES` / `EPERM` to userspace).

## 6. Core Concept

### Discretionary vs Mandatory

| Aspect | DAC | MAC |
|--------|-----|-----|
| Who defines policy | Object owner (and root) | System administrator / distribution policy |
| Can a user override? | Yes, for objects they own | No |
| Can root override? | Yes (under pure DAC) | Generally no (policy decides) |
| Typical granularity | Users, groups, mode bits | Process context ↔ object labels / paths |
| Classic examples | chmod, chown, ACLs, capabilities | SELinux, AppArmor, Smack, TOMOYO |

### Linux Security Modules (LSM)

LSM is a kernel framework that inserts security decision points (hooks) throughout the kernel. Multiple modules can be composed (subject to stacking rules). AppArmor and SELinux are implementations that register with LSM.

### AppArmor (overview)

- **Profile-based** and primarily **path-based**.  
- Each confined program has a profile that lists allowed path operations (read, write, execute, link, …) and other rights (network, capabilities, mount, …).  
- Profiles can be in **enforce** mode (violations denied and logged) or **complain** mode (violations only logged).  
- Simpler mental model; policy is relatively readable.  
- Default on Ubuntu and openSUSE.

### SELinux (overview)

- **Label-based** type enforcement (plus optional roles, users, multi-level security).  
- Every process runs with a domain (type); every file has a type.  
- Policy rules state which domains may perform which operations on which types.  
- Extremely fine-grained and powerful; policy is larger and harder to author by hand.  
- Modes: **enforcing**, **permissive** (log only), **disabled**.  
- Default on RHEL-family distributions.

### What MAC actually confines

Beyond ordinary file open/read/write, modern MAC systems can control:

- capability use  
- network bind / connect  
- mount operations  
- ptrace / signal targets  
- execution of other binaries  
- access to `/proc` and `/sys` objects  

This is why a root process can still be denied: the MAC policy simply does not grant that domain/profile the requested permission.

## 7. Break It Into the Smallest Important Pieces

### 7.1 Security context / profile
- AppArmor: a named profile attached to a program (usually by path).  
- SELinux: a label of the form `user:role:type:level` (type is the most commonly reasoned-about field).

### 7.2 Object label / path rule
- AppArmor: rules that name paths and permitted operations.  
- SELinux: file types (and other object classes) stored in extended attributes or computed by policy.

### 7.3 Mode of operation
- Enforcing / enforce — denials are real.  
- Permissive / complain — denials are logged but not applied.  
- Disabled — MAC framework is off.

### 7.4 Policy store
- AppArmor: text profiles under `/etc/apparmor.d/`, loaded into the kernel.  
- SELinux: compiled policy modules; file contexts under `/etc/selinux/…`; runtime state in the kernel.

### 7.5 Audit / denial logs
- Both systems emit records (often via the audit subsystem or journal) that name the process, the object, and the requested operation.  
- These logs are the primary diagnostic tool.

### 7.6 Interaction with DAC
- MAC is checked in addition to DAC.  
- Fixing a MAC denial does not remove the need for correct DAC permissions, and vice versa.

## 8. What Linux Is Actually Doing

**Simplified decision path**
```
Process calls open("/etc/shadow", O_RDONLY)
    → DAC: does the process’s UID/GID/capabilities allow it?
    → if no: return EACCES
    → if yes: LSM hook invoked
         AppArmor: look up profile of the process → check path rule
         SELinux:  look up domain of process + type of file → check allow rule
    → if MAC denies: return EACCES (and log)
    → if MAC allows: proceed with the open
```

The same pattern exists for many other operations (bind, exec, capability use, …).

## 9. Commands and Tools

### Common / discovery

| Command | Purpose |
|---------|---------|
| `cat /sys/kernel/security/lsm` | List active LSMs (order matters) |
| `dmesg \| grep -i mac` / journal | Boot-time MAC status messages |

### AppArmor

| Command | Purpose |
|---------|---------|
| `aa-status` | Profiles and their modes |
| `aa-enforce / aa-complain / aa-disable` | Change profile mode |
| `journalctl -t apparmor` / `grep DENIED` | Denial logs |
| `aa-logprof` / `aa-genprof` | Assisted profile development (awareness) |

### SELinux

| Command | Purpose |
|---------|---------|
| `getenforce` | Current mode (Enforcing / Permissive / Disabled) |
| `sestatus` | Broader status summary |
| `setenforce 0\|1` | Temporarily switch permissive ↔ enforcing |
| `ps -eZ` / `ls -Z` | Show process / file labels |
| `ausearch -m avc` / `journalctl` | Denial (AVC) messages |
| `restorecon` | Restore default file labels |

You only need the tools that match the system you are on; knowing both sets is part of being portable across distributions.

## 10. Hands-On Lab

**Objective**  
Determine which MAC framework (if any) is active on your system, inspect its global state, and locate any denial messages.

**Setup**  
Ubuntu (AppArmor) or a RHEL-family system (SELinux). The steps below cover both; run the ones that apply.

```bash
mkdir -p ~/mac-lab
cd ~/mac-lab
```

**Steps**

1. Discover active LSMs:
```bash
cat /sys/kernel/security/lsm
```

2. **If AppArmor is present:**
```bash
sudo aa-status
# Note how many profiles are loaded and how many are in enforce vs complain
sudo journalctl -t apparmor --since "1 day ago" | tail -20
# or
sudo grep -i denied /var/log/syslog 2>/dev/null | tail -10
```

3. **If SELinux is present:**
```bash
getenforce
sestatus
ps -eZ | head -15
ls -Z /etc/passwd /bin/ls
sudo ausearch -m avc -ts recent 2>/dev/null | tail -20
# or
sudo journalctl | grep -i avc | tail -10
```

4. Observe that root is still subject to MAC (conceptual demonstration):
```bash
# On an enforcing system you can sometimes find a known confined service
# and inspect its profile / domain. Do not disable MAC globally on a
# production host for experimentation.
```

5. Document the state of your lab VM:
```bash
echo "LSM: $(cat /sys/kernel/security/lsm)"
# plus the output of aa-status or getenforce/sestatus
```

**Verification**  
You must be able to state for your system:

- Which LSM(s) are active.  
- Whether the dominant MAC framework is in enforcing/enforce mode.  
- Where you would look for denial messages.

**Cleanup**
```bash
rm -rf ~/mac-lab
```

## 11. Investigation Lab

**Scenario**  
An application that runs as root fails to open a configuration file with “Permission denied”. Classic DAC looks correct (`ls -l` shows the file is world-readable, ownership is fine). Capabilities are not involved. The same binary works when MAC is set to permissive/complain.

**Objective**  
Confirm that MAC is the denying layer and identify the responsible profile or label.

**Available tools**  
`aa-status` / `getenforce`, denial logs, `ls -Z` / profile listing, process context tools

**Initial clues**  
- DAC permits the access.  
- Switching MAC to permissive makes the problem disappear.  
- The process is root (or holds broad capabilities).

**Investigation questions**  
1. Why can a root process still receive “Permission denied”?  
2. Which log channel records the denial for AppArmor versus SELinux?  
3. What information in the denial message identifies the subject (process) and the object?  
4. What are the two broad remediation approaches (change policy vs change object label / path)?

Work the questions before reading the solution.

**Solution**  
MAC is independent of DAC. Even UID 0 is constrained by the active profile (AppArmor) or domain (SELinux).

```bash
# AppArmor
sudo aa-status
sudo journalctl -t apparmor --since "10 min ago"
# SELinux
getenforce
sudo ausearch -m avc -ts recent
```
The denial record names the profile/domain and the path or target type. Remediation is either:

- adjust the policy (allow the operation), or  
- fix the object’s label / place the file on an allowed path, or  
- run the process under a different profile/domain that already permits the access.

Never leave a production system in permissive mode as a permanent fix.

## 12. Production Failure Scenario

**Incident**  
After a package update, a previously working service fails to start. Logs show “Permission denied” on a path under `/var/lib/myapp`. DAC permissions are correct. The host is SELinux-enforcing (or AppArmor-enforcing).

**Systematic troubleshooting**

1. **Observation**  
   Service fails; denial appears only when MAC is enforcing.

2. **Hypothesis**  
   The update introduced a new binary path, a new file location, or a label mismatch; policy no longer matches reality.

3. **Evidence**  
   ```bash
   # SELinux example
   getenforce
   sudo ausearch -m avc -ts recent
   ls -Z /var/lib/myapp
   ps -eZ | grep myapp
   # AppArmor example
   sudo aa-status
   sudo journalctl -t apparmor --since "30 min ago"
   ```

4. **Resolution examples**  
   - SELinux: restore correct labels (`restorecon -Rv /var/lib/myapp`) or install the updated policy module that ships with the package.  
   - AppArmor: update or reload the profile so the new path is allowed; or place the files under a path the existing profile already permits.  

5. **Prevention**  
   - Include MAC-aware steps in packaging and deployment (file contexts, profile updates).  
   - Test service start under enforcing mode in CI or staging.  
   - Treat persistent permissive mode as a defect, not a configuration.

## 13. Connection to Previous Linux Knowledge

- DAC (Sessions 10–13) is still evaluated; MAC is an additional gate.  
- Capabilities (Session 17) can themselves be restricted by MAC policy—holding a capability does not guarantee MAC will allow its use.  
- sudo and setuid (Sessions 11, 16) elevate DAC identity; they do not automatically grant MAC permissions outside the target process’s profile/domain.  
- “Permission denied” is now a multi-layer diagnosis: DAC, capabilities, **and** MAC must all be considered.

## 14. Connection to Future Infrastructure

- **Containers**: runtimes ship with default AppArmor profiles or SELinux types; breaking out of a container often means finding a MAC (and seccomp/cgroup) gap, not just a DAC gap.  
- **Kubernetes**: PodSecurityPolicy / Pod Security Admission, SELinux options in securityContext, and AppArmor annotations are the orchestration-level controls that map onto the mechanisms in this session.  
- **Distribution defaults**: Ubuntu → AppArmor; RHEL family → SELinux. Portable automation must detect and handle both.  
- **Compliance**: many security baselines require MAC to be enabling and enforcing.  
- **AI infrastructure**: multi-tenant training hosts and shared GPU nodes increasingly rely on MAC (and namespaces/cgroups) to keep workloads from interfering with each other even when DAC is misconfigured.

## 15. Engineering Questions

1. What is the essential difference between discretionary and mandatory access control?  
2. Why can a process running as root still be denied by AppArmor or SELinux?  
3. What is the Linux Security Modules framework?  
4. How does AppArmor’s primary decision key differ from SELinux’s?  
5. What are the common operating modes of each framework and what do they mean?  
6. Where do you look for denial messages on an AppArmor system versus an SELinux system?  
7. Why is leaving a system in permissive/complain mode a poor long-term solution?  
8. How do MAC and capabilities interact?  
9. Why must a portable operations practice understand both AppArmor and SELinux?

## 16. Practical Assignment

1. On your lab system, produce a one-page status report:  
   - active LSMs  
   - dominant MAC framework and its mode  
   - number of confined profiles / sample of process labels  
   - location of denial logs  

2. Intentionally trigger a harmless denial (if a confined utility is available) or locate a recent historical denial in the logs; document the subject, object, and requested operation.  

3. Write a short decision tree: “I received Permission denied — how do I decide whether the cause is DAC, capabilities, or MAC?”  

4. Compare (from documentation or a second VM if available) one concrete difference in day-to-day workflow between an AppArmor host and an SELinux host when a service is denied access to a new path.

## 17. Session Completion Test

Answer without notes.

**Conceptual**  
1. Define Mandatory Access Control and contrast it with Discretionary Access Control.  
2. What is the role of LSM in the Linux kernel?

**System behavior**  
3. A root process is denied read access to a world-readable file. Which layer is responsible?  
4. What is the difference between enforcing/enforce mode and permissive/complain mode?

**Command interpretation**  
5. What does `getenforce` tell you? What does `aa-status` tell you?  
6. Why might `ls -Z` or a profile name appear in a denial log?

**Troubleshooting**  
7. Give the first three checks you perform when you suspect a MAC denial.

**Internal**  
8. Describe the high-level decision order between DAC and MAC on a typical Linux system.

**Explain in your own words**  
9. Explain why distributions ship with a MAC framework enabled by default even though it occasionally breaks applications after updates.

## 18. Mastery Criteria

- **Basic understanding**: You can state what MAC is, name the two main Linux implementations, and determine which is active on a host.  
- **Working understanding**: You can check mode, find denial logs, and decide whether a “Permission denied” is likely a MAC problem.  
- **Strong understanding**: You can explain the interaction of DAC, capabilities, and MAC, recognise the operational differences between AppArmor and SELinux, and apply a systematic approach that does not permanently disable MAC.

## 19. What I Should Now Be Able to Explain

- DAC versus MAC  
- Role of the Linux Security Modules framework  
- Core idea of AppArmor (profiles, path-oriented)  
- Core idea of SELinux (labels, type enforcement)  
- Operating modes (enforcing / permissive / disabled)  
- How to determine which framework is active and in which mode  
- Where denial messages appear  
- Why root can still be denied  
- Why MAC denials must be fixed by policy or labelling, not by “chmod 777” or permanent permissive mode

## 20. Next Session

**Next Session Number**  
SESSION 19  

**Next Session Title**  
Systemd Fundamentals: Units, Targets, and the Dependency Model  

**Why it comes next**  
The permissions and security module is now complete for the purposes of this curriculum phase. The next major operational pillar is system and service management. Systemd is the init system and service manager used by essentially all modern Linux distributions; understanding units, targets, dependencies, and the boot process is required before reliable service operation, troubleshooting, and later container/orchestration work.
