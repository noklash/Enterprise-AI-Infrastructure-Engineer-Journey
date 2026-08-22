# Session 19 — Systemd Fundamentals: Units, Targets, and the Dependency Model

## 1. Position in the Curriculum

**Phase**  
PHASE 1 — Linux Systems Engineering

**Module**  
MODULE 3 — System Management (systemd)

**Session**  
SESSION 19 — Systemd Fundamentals: Units, Targets, and the Dependency Model

**Prerequisites**  
- Process model, process lifecycle, and signals  
- File system hierarchy and mounts (Sessions 04–05)  
- Basic identity and privilege concepts (UID 0, sudo)  
- Ability to read logs with `journalctl` at a superficial level

**What this session unlocks**  
The ability to understand how a modern Linux system starts, how services are declared and ordered, and how systemd represents the desired state of the machine. This is the foundation for every subsequent service-management, boot-troubleshooting, and orchestration-related skill.

## 2. Why This Session Exists

You have completed the major operational filesystem and security foundations. The next essential pillar of Linux systems engineering is **how the system itself is managed**: how it boots, how services are started and stopped, how dependencies are expressed, and how failures are handled.

On essentially every current server distribution that role is filled by **systemd**.  

systemd replaces the older SysV init and many ad-hoc scripts with:

- a uniform unit model  
- explicit dependency and ordering declarations  
- parallel startup  
- integrated logging  
- on-demand activation  
- a consistent control surface (`systemctl`)

Without a solid mental model of units, targets, and the dependency graph you cannot reliably:

- start or stop services  
- understand why a service did not start  
- modify boot behaviour  
- debug a machine that fails to reach a usable state  
- reason about containers and orchestration systems that reuse the same ideas  

This session builds that core model.

## 3. Learning Objectives

By the end of this session you will be able to:

- Explain what a systemd unit is and name the principal unit types.  
- Describe the role of targets as synchronisation points / milestones.  
- Interpret the basic dependency and ordering directives (`Wants=`, `Requires=`, `After=`, `Before=`).  
- Use `systemctl` to inspect unit state, list units, and view dependency relationships.  
- Trace the high-level boot path from the kernel to the default target.  
- Distinguish between a unit being *active*, *enabled*, *failed*, and *masked*.  
- Read a simple service unit file and identify its main sections and key directives.  
- Explain why parallel startup is possible and what role ordering plays.

## 4. Prerequisite Concepts

You already know:

- Processes are created with `fork`/`exec` and reaped by their parent.  
- PID 1 has a special role as the ancestor of all user-space processes.  
- The kernel mounts an initial root filesystem and then executes an init program.  
- Services are long-running processes that should be supervised.

## 5. Mental Model

```
Kernel
  │
  │  executes /sbin/init  (symlink to systemd)
  ▼
systemd (PID 1)
  │
  │  reads default target (e.g. graphical.target or multi-user.target)
  │  activates units required (directly or indirectly) by that target
  ▼
┌─────────────────────────────────────────────────────────────┐
│  Unit dependency graph                                       │
│                                                              │
│  sysinit.target → basic.target → multi-user.target → …      │
│         │              │                 │                   │
│         ▼              ▼                 ▼                   │
│      local-fs     sockets.target    sshd.service  cron.service │
│      …            …                 …             …          │
└─────────────────────────────────────────────────────────────┘
```

- **Units** are the nodes.  
- **Dependencies** (`Wants=`, `Requires=`, …) express what must be present.  
- **Ordering** (`After=`, `Before=`) expresses sequencing constraints.  
- **Targets** are units whose main purpose is to group other units and serve as milestones.

## 6. Core Concept

### Units

A **unit** is a resource that systemd knows how to manage. Each unit is described by a unit file (or created dynamically). Common types:

| Type | Extension | Purpose |
|------|-----------|---------|
| Service | `.service` | A process or daemon to be started/stopped |
| Target | `.target` | A synchronisation point / grouping of units |
| Mount | `.mount` | A filesystem mount point |
| Socket | `.socket` | A socket for socket-activated services |
| Timer | `.timer` | Time-based activation (cron replacement) |
| Path | `.path` | Path-based activation |
| Device | `.device` | A device exposed by udev |
| Slice | `.slice` | A cgroup hierarchy node for resource control |

### Targets

Targets are the systemd analogue of classic runlevels, but far more flexible. Important early targets include:

- `sysinit.target` — early system initialisation  
- `basic.target` — basic system services are up  
- `multi-user.target` — normal multi-user text/console mode (typical server default)  
- `graphical.target` — multi-user + graphical stack  

The **default target** is what the system tries to reach on boot (`systemctl get-default`).

### Dependency versus ordering

Two orthogonal relationships:

- **Requirement** — “this unit should/must be active for me to be active”  
  - `Wants=` — soft; if the wanted unit fails, I still proceed  
  - `Requires=` — hard; if the required unit fails, I fail too  
  - `BindsTo=` — even stronger lifetime coupling  

- **Ordering** — “when both are being activated, who goes first”  
  - `After=` — I start after the named unit  
  - `Before=` — I start before the named unit  

A common pattern:

```
Wants=network-online.target
After=network-online.target
```

means “I would like the network to be up, and I will wait until that target is reached before I start.”

### Unit states (simplified)

- **active (running)** — the unit is up  
- **inactive (dead)** — the unit is down  
- **failed** — activation (or the main process) failed  
- **activating / deactivating** — transitional  
- **enabled** — will be considered for start at boot (symlink installed)  
- **disabled** — not started automatically  
- **masked** — completely disabled; even manual start is refused  

“Enabled” and “active” are independent: a unit can be enabled but not yet active, or active but not enabled (manually started).

## 7. Break It Into the Smallest Important Pieces

### 7.1 Unit file
- Declarative description stored under `/etc/systemd/system/`, `/run/systemd/system/`, or `/usr/lib/systemd/system/`.  
- Parsed by systemd; the on-disk files are not executed as scripts.

### 7.2 Unit type
- Determines which directives are valid and how activation is performed.

### 7.3 [Unit] section
- Generic metadata and dependency/ordering directives.

### 7.4 Type-specific section
- `[Service]`, `[Socket]`, `[Timer]`, etc.  
- Contains the concrete activation instructions (`ExecStart=`, …).

### 7.5 [Install] section
- Used by `systemctl enable/disable` to decide which symlinks to create.

### 7.6 Default target
- The root of the boot-time activation graph for a normal startup.

### 7.7 Transaction / job engine
- When you ask systemd to activate a unit, it computes a set of jobs (start/stop/…) that satisfy dependencies and ordering, then executes them.

## 8. What Linux Is Actually Doing

**Boot (high level)**
```
Kernel finishes initialisation
    → executes PID 1 = systemd
    → systemd mounts essential filesystems, starts basic helpers
    → determines default target
    → pulls in all units required (Wants/Requires) by that target
    → respects After=/Before= ordering
    → activates units in parallel where no ordering constraint exists
    → system reaches the default target (or fails trying)
```

**Manual start**
```
systemctl start sshd.service
    → systemd loads the unit file if not already loaded
    → computes the transaction (dependencies + ordering)
    → executes the jobs
    → updates unit state
```

## 9. Commands and Tools

| Command | Purpose |
|---------|---------|
| `systemctl` | Primary control and inspection tool |
| `systemctl status [unit]` | Current state, recent logs, process info |
| `systemctl list-units` | Units that are currently loaded/active |
| `systemctl list-unit-files` | All installed unit files and their enablement state |
| `systemctl list-dependencies [unit]` | Dependency tree |
| `systemctl get-default` / `set-default` | Default target |
| `systemctl is-active / is-enabled / is-failed` | Script-friendly state queries |
| `systemctl daemon-reload` | Reload unit files after editing |
| `systemctl cat unit` | Show the effective unit file (with drop-ins) |
| `systemctl show unit` | Low-level properties |
| `ls /etc/systemd/system` / `/usr/lib/systemd/system` | On-disk unit locations |

## 10. Hands-On Lab

**Objective**  
Explore the unit model on a live system, inspect the default target and its dependencies, and examine a real service unit.

**Setup**  
Any modern systemd-based Ubuntu (or other) system.

```bash
mkdir -p ~/systemd-lab
cd ~/systemd-lab
```

**Steps**

1. Identify PID 1 and the default target:
```bash
ps -p 1 -o pid,comm,args
systemctl get-default
systemctl status
```

2. List active units and unit files:
```bash
systemctl list-units --type=service --state=running
systemctl list-unit-files --type=service | head -30
systemctl list-units --type=target
```

3. Inspect the dependency tree of the default target:
```bash
systemctl list-dependencies multi-user.target
systemctl list-dependencies --after multi-user.target | head -40
```

4. Examine a concrete service (ssh is usually present):
```bash
systemctl status ssh
# or sshd on some distributions
systemctl cat ssh
systemctl show ssh -p Requires -p Wants -p After -p Before -p Description
systemctl list-dependencies ssh
```

5. Distinguish enabled vs active:
```bash
systemctl is-enabled ssh
systemctl is-active ssh
systemctl is-failed ssh
```

6. Look at unit file search paths:
```bash
systemctl show --property=UnitPath
ls /usr/lib/systemd/system/ssh* 2>/dev/null
ls /etc/systemd/system/ssh* 2>/dev/null
```

**Verification**  
You must be able to:

- Name the default target on your system.  
- Show the dependency tree of that target.  
- Display the full content of a service unit and identify its `[Unit]`, `[Service]`, and `[Install]` sections.  
- State whether a given service is active and whether it is enabled.

**Cleanup**
```bash
rm -rf ~/systemd-lab
```

## 11. Investigation Lab

**Scenario**  
After a reboot a custom service that should start automatically is not running. `systemctl status myapp` shows it is inactive. The unit file exists under `/etc/systemd/system/`.

**Objective**  
Determine why the service did not start and bring it into the desired state.

**Available tools**  
`systemctl status`, `systemctl is-enabled`, `systemctl list-dependencies`, `journalctl -u`, unit file inspection

**Initial clues**  
- Unit file is present.  
- Service is inactive after boot.  
- No obvious crash in a quick status glance.

**Investigation questions**  
1. What is the difference between a unit being present and a unit being enabled?  
2. How do you check whether the unit is pulled in by the default target?  
3. What command shows why a unit failed if it attempted to start?  
4. What steps are required after creating or editing a unit file before enable/start will see the changes?

Work the questions before reading the solution.

**Solution**  
```bash
systemctl is-enabled myapp
systemctl status myapp
systemctl cat myapp
journalctl -u myapp -b
systemctl list-dependencies multi-user.target | grep myapp
```
Common causes:

- Unit was never enabled (`systemctl enable myapp`).  
- Unit was enabled but failed during activation (look at `journalctl -u`).  
- Unit file was edited but `daemon-reload` was not run.  
- A required dependency failed and the unit used `Requires=` rather than `Wants=`.  

Fix the root cause, then `systemctl enable --now myapp` (or the appropriate combination of enable and start).

## 12. Production Failure Scenario

**Incident**  
A host boots but never becomes reachable over the network. Local console access works. The operator suspects a service ordering or dependency problem.

**Systematic troubleshooting**

1. **Observation**  
   System reaches multi-user state locally; network services appear not to be listening or the network stack is not ready when they start.

2. **Hypothesis set**  
   - Network-related units failed.  
   - Application services started before the network was ready.  
   - A target that should pull in networking is not part of the graph.

3. **Evidence**  
   ```bash
   systemctl status
   systemctl list-units --failed
   systemctl status NetworkManager   # or systemd-networkd, networking.service
   systemctl list-dependencies multi-user.target
   journalctl -b -u ssh -u NetworkManager -u systemd-networkd
   systemctl show ssh -p After -p Wants -p Requires
   ```

4. **Resolution examples**  
   - Fix the failed network unit.  
   - Add `Wants=network-online.target` and `After=network-online.target` to services that truly need a configured network.  
   - Avoid ordering every service after `network-online.target` (it slows boot and is often unnecessary).  

5. **Prevention**  
   - Explicit, minimal dependencies.  
   - Health checks and monitoring that detect “booted but unreachable.”  
   - Test boot after networking or service changes.

## 13. Connection to Previous Linux Knowledge

- systemd is PID 1; it reaps orphaned processes and is the ultimate parent of user-space services (process model).  
- Mount units replace or complement classic `/etc/fstab` handling (filesystem sessions).  
- Service units ultimately execute processes under chosen UIDs/GIDs and capabilities (security sessions).  
- The journal that `systemctl status` shows is the same journal you will use for deeper logging work.  
- Targets and dependency graphs are the same ideas later reused by higher-level orchestration systems.

## 14. Connection to Future Infrastructure

- **Containers**: many container entrypoints and sidecar patterns are supervised by systemd on the host, or by a systemd-inspired model inside the container.  
- **Kubernetes**: the notions of desired state, dependency, and restart policy are conceptual relatives of systemd’s unit model.  
- **Configuration management / IaC**: unit files and drop-ins are managed as code; enablement state is part of the desired system configuration.  
- **Observability**: unit state and journal logs are primary signals for host-level health.  
- **AI infrastructure**: GPU nodes, storage daemons, and training launchers are commonly packaged as systemd services; correct dependencies (devices, mounts, network) determine whether jobs start reliably after reboot.

## 15. Engineering Questions

1. What is a systemd unit?  
2. What is the purpose of a target?  
3. What is the difference between `Wants=` and `Requires=`?  
4. What is the difference between `After=` and `Wants=`?  
5. How does a unit become started automatically at boot?  
6. What does “masked” mean for a unit?  
7. Why can systemd start many services in parallel?  
8. Where does systemd look for unit files, and in what order of precedence?  
9. Why must you run `systemctl daemon-reload` after editing a unit file?

## 16. Practical Assignment

1. Draw (text is fine) the path from kernel start to `multi-user.target`, naming the major early targets and two or three example services that are typically pulled in.  

2. For the SSH service on your system, document:  
   - full unit name  
   - whether it is enabled and active  
   - its main dependency and ordering relationships  
   - the contents of its `[Service]` section (ExecStart, User, Restart, etc.)  

3. Create a trivial custom service unit (for example one that runs `/bin/true` or a short script), place it under `/etc/systemd/system/`, load it, start it, enable it, then disable and remove it. Record every command.  

4. Write a short explanation of why a service that does not need the network should not list `After=network-online.target`.

## 17. Session Completion Test

Answer without notes.

**Conceptual**  
1. What is the difference between a service unit and a target unit?  
2. What two kinds of relationship do `Wants=`/`Requires=` and `After=`/`Before=` express?

**System behavior**  
3. A unit is enabled but inactive after boot. What are two possible explanations?  
4. What happens if a unit listed in `Requires=` fails to start?

**Command interpretation**  
5. What does `systemctl list-dependencies multi-user.target` show?  
6. What is the difference between `systemctl status foo` and `systemctl is-enabled foo`?

**Troubleshooting**  
7. A newly installed service does not start at boot. What three checks do you perform first?

**Internal**  
8. Describe the high-level steps systemd takes when asked to start a unit that has dependencies.

**Explain in your own words**  
9. Explain why modern Linux systems can boot faster than classic sequential init scripts.

## 18. Mastery Criteria

- **Basic understanding**: You can list units, check status, and identify the default target.  
- **Working understanding**: You can read a service unit, interpret common dependency and ordering directives, enable/start/stop services, and diagnose why a unit did not start at boot.  
- **Strong understanding**: You can design a minimal correct dependency set for a new service, explain the boot graph, and distinguish requirement failures from ordering problems.

## 19. What I Should Now Be Able to Explain

- What units and unit types are  
- Role of targets as milestones  
- Difference between requirement and ordering dependencies  
- Meaning of active / enabled / failed / masked  
- High-level boot path to the default target  
- Basic `systemctl` inspection and control commands  
- Structure of a simple service unit file  
- Why `daemon-reload` is required after unit-file changes  
- Parallel activation and the role of ordering constraints

## 20. Next Session

**Next Session Number**  
SESSION 20  

**Next Session Title**  
Service Units in Depth: Types, ExecStart, Restart Policy, and Lifecycle  

**Why it comes next**  
You now understand the unit model and the dependency graph. The next session focuses on the most important unit type for day-to-day operations—the service unit—covering process lifecycle, `Type=` semantics, restart behaviour, environment, and the practical patterns used to run reliable long-lived processes under systemd.
