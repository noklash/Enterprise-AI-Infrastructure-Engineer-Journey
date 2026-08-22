# Session 20 — Service Units in Depth: Types, ExecStart, Restart Policy, and Lifecycle

## 1. Position in the Curriculum

**Phase**  
PHASE 1 — Linux Systems Engineering

**Module**  
MODULE 3 — System Management (systemd)

**Session**  
SESSION 20 — Service Units in Depth: Types, ExecStart, Restart Policy, and Lifecycle

**Prerequisites**  
- Systemd units, targets, and the dependency model (Session 19)  
- Process lifecycle, signals, and exit status  
- Basic identity and privilege concepts (User=, capabilities)

**What this session unlocks**  
The ability to write, read, and troubleshoot real service units: how systemd starts a process, how it decides that the service is “up,” how it reacts to crashes, and how it stops the service cleanly. This is the core operational skill for running any long-lived process under systemd.

## 2. Why This Session Exists

Session 19 gave you the graph: units, targets, dependencies, and ordering.  

Most of the units you will create or debug in practice are **service units**. A service unit answers concrete questions:

- What command do I run?  
- How do I know the service has finished starting?  
- What happens if the process exits?  
- How do I stop it cleanly?  
- Under which user, with which environment, and with which resource limits?

Incorrect answers produce the classic failures:

- systemd thinks the service is up when it is still initialising  
- systemd thinks the service has failed when it deliberately daemonised  
- a crashing service is not restarted (or is restarted too aggressively)  
- stop jobs hang because the process ignores SIGTERM  

This session builds a precise model of the service lifecycle and the directives that control it.

## 3. Learning Objectives

By the end of this session you will be able to:

- Explain the main service `Type=` values (`simple`, `exec`, `forking`, `oneshot`, `notify`, `idle`) and when each is appropriate.  
- Write a correct `ExecStart=` (and related `ExecStartPre=`, `ExecStop=`, `ExecReload=`) stanza.  
- Configure restart behaviour with `Restart=` and `RestartSec=`.  
- Distinguish between the main process and auxiliary processes, and interpret `systemctl status` output accordingly.  
- Use `User=`, `Group=`, `Environment=`, `EnvironmentFile=`, and basic resource directives safely.  
- Read a service unit and predict how systemd will start, supervise, and stop it.  
- Diagnose the common “service starts then immediately fails” and “service never finishes starting” symptoms.

## 4. Prerequisite Concepts

You already know:

- How units are loaded, enabled, and activated.  
- The difference between requirement and ordering dependencies.  
- That a process has a PID, an exit status, and can receive signals.  
- That services should usually not be run as root without a reason.

## 5. Mental Model

```
systemctl start foo.service
        │
        ▼
systemd loads unit, computes transaction
        │
        ▼
┌───────────────────────────────────────────┐
│  Service lifecycle                        │
│                                           │
│  activating                               │
│     → run ExecStartPre= (if any)          │
│     → run ExecStart=                      │
│     → wait according to Type=             │
│  active                                   │
│     → supervise main process              │
│     → on exit: apply Restart= policy      │
│  deactivating                             │
│     → run ExecStop= (if any)              │
│     → send signals as configured          │
│  inactive / failed                        │
└───────────────────────────────────────────┘
```

The `Type=` directive tells systemd **how to interpret the behaviour of ExecStart=** so that the transition from “activating” to “active” (or “failed”) is correct.

## 6. Core Concept

### Service types (`Type=`)

| Type | Meaning | Typical use |
|------|---------|-------------|
| `simple` | Main process is the one started by ExecStart=; systemd considers the service up immediately after `fork`/`exec` | Most modern foreground daemons (recommended default when the process does not daemonise) |
| `exec` | Like `simple`, but systemd waits until the execve itself succeeds | Slightly stricter variant of simple |
| `forking` | Process is expected to fork and exit the parent; systemd waits for the parent to exit and tracks the child (requires `PIDFile=` in many cases) | Traditional SysV-style daemons that daemonise themselves |
| `oneshot` | Process runs to completion; service is considered active after exit (often with `RemainAfterExit=yes`) | Initialization scripts, one-time configuration |
| `notify` | Service sends a readiness notification (sd_notify) when it is ready; systemd waits for that notification | Modern daemons written to integrate with systemd |
| `idle` | Delays execution until all other jobs are dispatched | Rare; mostly cosmetic for boot messages |

Choosing the wrong type is one of the most common unit-file mistakes.

### Exec* directives

- `ExecStart=` — the main command (required for most services).  
- `ExecStartPre=` / `ExecStartPost=` — commands run before/after the main start.  
- `ExecStop=` — command run when the service is asked to stop.  
- `ExecStopPost=` — runs after the service has stopped (even on failure).  
- `ExecReload=` — command used by `systemctl reload`.  

Multiple `ExecStartPre=` lines are allowed; they run in order. Failure behaviour is controlled by `set -e` style rules and by prefixes such as `-` (ignore failure).

### Restart policy

`Restart=` controls what happens when the main process exits:

| Value | Behaviour |
|-------|-----------|
| `no` | Never restart (default) |
| `on-success` | Restart only on clean exit |
| `on-failure` | Restart on unclean exit, signal, timeout, watchdog |
| `on-abnormal` | Restart on signal, timeout, watchdog (not ordinary exit codes) |
| `on-abort` | Restart only on uncaught signal / abort |
| `on-watchdog` | Restart only on watchdog timeout |
| `always` | Always restart |

`RestartSec=` sets the delay before a restart attempt.  
`StartLimitBurst=` / `StartLimitIntervalSec=` protect against tight crash loops.

### Stopping a service

Default behaviour:

1. Run `ExecStop=` if defined.  
2. Send `SIGTERM` to the main process (and possibly the process group).  
3. Wait `TimeoutStopSec=`.  
4. Send `SIGKILL` if still alive.

`KillMode=` (`control-group`, `process`, `mixed`, `none`) controls exactly which processes receive the signals.

### Identity and environment

- `User=` / `Group=` — drop privileges before exec.  
- `Environment=` / `EnvironmentFile=` — pass environment variables.  
- `WorkingDirectory=` — set cwd.  
- `Nice=`, `LimitNOFILE=`, `MemoryMax=`, … — resource control (cgroup integration).

## 7. Break It Into the Smallest Important Pieces

### 7.1 Main PID
- The process systemd considers the primary process of the service.  
- Determined differently according to `Type=`.

### 7.2 Type= semantics
- The contract between the unit author and systemd about readiness and supervision.

### 7.3 ExecStart= command line
- Must be an absolute path (or a path resolvable in a defined way).  
- Subject to systemd’s limited specifier expansion (`%i`, `%n`, `%H`, …).

### 7.4 Restart= and rate limiting
- Decides whether a new main process is spawned after exit, and how fast.

### 7.5 Stop sequence
- Ordered shutdown: ExecStop → signal → timeout → SIGKILL.

### 7.6 Readiness notification
- For `Type=notify`, the service must call `sd_notify(0, "READY=1")` (or equivalent) when it is actually ready to serve traffic.

### 7.7 RemainAfterExit=
- Relevant for `oneshot`: keeps the unit “active” after the process has exited so that dependencies can rely on it.

## 8. What Linux Is Actually Doing

**Starting a Type=simple service**
```
systemctl start myapp
    → systemd forks a child
    → applies User=, environment, cgroup, namespaces, …
    → execve(ExecStart)
    → immediately transitions unit to active (running)
    → watches the main PID
```

**Starting a Type=notify service**
```
    → same fork/exec path
    → unit stays in activating
    → service code eventually calls sd_notify("READY=1")
    → systemd receives the notification on the notify socket
    → unit becomes active
```

**Crash and restart**
```
main process exits with status ≠ 0
    → systemd consults Restart=
    → if restart is warranted and rate limit not exceeded:
          wait RestartSec
          start again (new activation)
    → else:
          unit enters failed state
```

## 9. Commands and Tools

| Command | Purpose |
|---------|---------|
| `systemctl cat unit` | Show effective unit file + drop-ins |
| `systemctl edit unit` | Create a drop-in override safely |
| `systemctl status unit` | State, main PID, recent log lines |
| `systemctl show unit -p Type -p Restart -p ExecStart -p User` | Specific properties |
| `systemctl daemon-reload` | Required after any unit-file change |
| `journalctl -u unit -f` | Follow logs for the service |
| `systemd-analyze verify unit` | Static check of unit file syntax and basic semantics |

## 10. Hands-On Lab

**Objective**  
Inspect real service units, observe different `Type=` behaviours, and create a small custom service that demonstrates restart policy.

**Setup**
```bash
mkdir -p ~/service-lab
cd ~/service-lab
```

**Steps**

1. Inspect a common service:
```bash
systemctl cat ssh
# or sshd
systemctl show ssh -p Type -p Restart -p ExecStart -p User -p KillMode
systemctl status ssh
```

2. Compare with a oneshot-style unit (examples vary by distribution):
```bash
systemctl cat systemd-tmpfiles-setup.service 2>/dev/null | head -40
systemctl show systemd-tmpfiles-setup.service -p Type -p RemainAfterExit 2>/dev/null
```

3. Create a simple demo service that crashes:
```bash
cat > crash-demo.sh << 'EOF'
#!/bin/bash
echo "crash-demo starting, pid $$"
sleep 3
echo "crash-demo exiting with failure"
exit 1
EOF
chmod +x crash-demo.sh

# Unit file (requires sudo)
sudo tee /etc/systemd/system/crash-demo.service > /dev/null << EOF
[Unit]
Description=Crash demo service

[Service]
Type=simple
ExecStart=/home/$USER/service-lab/crash-demo.sh
Restart=on-failure
RestartSec=2
StartLimitIntervalSec=30
StartLimitBurst=3

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl start crash-demo
systemctl status crash-demo
journalctl -u crash-demo -b --no-pager
```

4. Observe the restart limit:
```bash
# Wait ~15–20 seconds, then:
systemctl status crash-demo
# Should eventually show failed after the burst limit
```

5. Change the script to stay running and use a clean stop:
```bash
cat > long-demo.sh << 'EOF'
#!/bin/bash
echo "long-demo starting, pid $$"
trap 'echo "got SIGTERM, exiting cleanly"; exit 0' TERM
while true; do sleep 1; done
EOF
chmod +x long-demo.sh

sudo tee /etc/systemd/system/long-demo.service > /dev/null << EOF
[Unit]
Description=Long-running demo

[Service]
Type=simple
ExecStart=/home/$USER/service-lab/long-demo.sh
Restart=no

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl start long-demo
systemctl status long-demo
sudo systemctl stop long-demo
journalctl -u long-demo -b --no-pager | tail -20
```

**Verification**  
You must have observed:

- A service that restarts on failure and then hits a start limit.  
- A service that handles SIGTERM and exits cleanly under `systemctl stop`.  
- The difference in `systemctl status` between active (running) and failed.

**Cleanup**
```bash
sudo systemctl stop crash-demo long-demo 2>/dev/null
sudo systemctl disable crash-demo long-demo 2>/dev/null
sudo rm -f /etc/systemd/system/crash-demo.service /etc/systemd/system/long-demo.service
sudo systemctl daemon-reload
rm -rf ~/service-lab
```

## 11. Investigation Lab

**Scenario**  
A newly written service unit uses `Type=forking` because the vendor documentation says “the daemon backgrounds itself.” `systemctl start myapp` hangs for ~90 seconds and then fails with a timeout. The process is actually running.

**Objective**  
Explain the failure and recommend the correct unit configuration.

**Available tools**  
`systemctl status`, `systemctl cat`, `ps`, `journalctl -u`, inspection of whether a PID file is created

**Initial clues**  
- Unit has `Type=forking`.  
- No `PIDFile=` directive.  
- Start job times out.  
- Process is visible in `ps` after the timeout.

**Investigation questions**  
1. What does systemd wait for when `Type=forking` is set?  
2. Why does the absence of a PID file (or a double-fork that does not exit the intermediate parent cleanly) cause a timeout?  
3. What `Type=` is usually more appropriate for a program that can run in the foreground?  
4. How would you confirm that the service is ready before considering it active?

Work the questions before reading the solution.

**Solution**  
With `Type=forking`, systemd expects the process started by `ExecStart=` to fork and for the parent to exit. It then tries to determine the main PID (often via `PIDFile=`). If the program does not behave that way, or the PID file is missing/wrong, systemd never sees the expected transition and the start job hits `TimeoutStartSec`.

Preferred fixes:

- Best: run the program in the foreground and use `Type=simple` (or `Type=notify` if the program supports sd_notify).  
- If the program must fork: supply a correct `PIDFile=` and ensure the forking behaviour matches what systemd expects.  

After correcting the unit, `daemon-reload`, and restart, `systemctl status` should show active (running) quickly and with the correct main PID.

## 12. Production Failure Scenario

**Incident**  
A production service begins crash-looping after a buggy release. systemd restarts it so quickly that logs are flooded and the operator cannot examine the failure state. Eventually the start limit is hit and the service stays failed, taking the application down.

**Systematic troubleshooting**

1. **Observation**  
   `systemctl status` shows repeated restarts then `failed`. Journal is full of short-lived start attempts.

2. **Hypothesis**  
   Application bug + `Restart=always` (or `on-failure`) with a very small `RestartSec` and a high or disabled start limit.

3. **Evidence**  
   ```bash
   systemctl status myapp
   systemctl show myapp -p Restart -p RestartSec -p StartLimitIntervalSec -p StartLimitBurst
   journalctl -u myapp -b --no-pager | tail -100
   ```

4. **Immediate actions**  
   - Stop the restart storm: `systemctl stop myapp`.  
   - Capture logs and core dumps if any.  
   - Roll back to the previous known-good version or fix the configuration error.

5. **Longer-term hardening**  
   - Use `Restart=on-failure` rather than `always` unless truly required.  
   - Set a sensible `RestartSec=` (seconds, not milliseconds).  
   - Keep `StartLimitBurst` / `StartLimitIntervalSec` so that persistent failures become visible.  
   - Add monitoring on unit failed state and on restart rate.

## 13. Connection to Previous Linux Knowledge

- The main process of a service is an ordinary Linux process supervised by PID 1 (Session 19 and earlier process work).  
- `User=` / `Group=` set the credentials you studied in the security module.  
- Restart policy is a userspace implementation of “supervise this process,” historically done by daemon tools or custom scripts.  
- Stop behaviour relies on signals (`SIGTERM`, `SIGKILL`) that you already understand from process management.  
- `Type=notify` is an explicit readiness protocol that removes the guesswork inherent in “sleep and hope it is ready.”

## 14. Connection to Future Infrastructure

- **Containers**: the same lifecycle questions (when is the app ready, what happens on crash, how is it stopped) appear as Kubernetes probes, restart policies, and preStop hooks.  
- **Orchestration**: desired-state controllers assume processes can be started, health-checked, and stopped cleanly—exactly what a well-written service unit provides on the host.  
- **Configuration management**: unit files and drop-ins are first-class artefacts; restart behaviour is part of the service contract.  
- **AI infrastructure**: training job launchers, model servers, and data daemons should be packaged as notify-aware or simple foreground services with deliberate restart policies so that node reboots and process crashes produce predictable recovery.

## 15. Engineering Questions

1. What does `Type=simple` tell systemd about the process started by `ExecStart=`?  
2. When is `Type=forking` appropriate, and what extra directive is usually required?  
3. What is the purpose of `Type=notify`?  
4. How does `Restart=on-failure` differ from `Restart=always`?  
5. What prevents a crash-looping service from restarting forever?  
6. Describe the default stop sequence of a service.  
7. Why should long-running services generally be run in the foreground under systemd?  
8. What is the difference between `ExecStop=` and simply sending SIGTERM?  
9. Why is `systemctl daemon-reload` required after editing a unit file?

## 16. Practical Assignment

1. Write a service unit for a simple foreground program (your own script or a real binary) that:  
   - runs as a non-root user  
   - restarts on failure with a 5-second delay  
   - has a start-rate limit  
   - stops cleanly on `systemctl stop`  

2. Demonstrate (with journal excerpts) both a clean start/stop and a crash/restart cycle.  

3. Convert the same program to a `Type=oneshot` unit that performs a single task and leaves the unit active via `RemainAfterExit=yes`. Document when you would choose oneshot versus simple.  

4. Inspect three existing services on your system and classify each by `Type=` and `Restart=` policy; note whether the chosen type matches the program’s actual behaviour.

## 17. Session Completion Test

Answer without notes.

**Conceptual**  
1. What does the `Type=` directive control?  
2. Name three common `Type=` values and one typical use for each.

**System behavior**  
3. A `Type=simple` service’s main process exits with status 1 and `Restart=on-failure`. What does systemd do next?  
4. What happens if a `Type=notify` service never sends `READY=1`?

**Command interpretation**  
5. What does `systemctl show foo -p Type -p Restart` tell you?  
6. Why might `systemctl start foo` hang for a long time before failing?

**Troubleshooting**  
7. A forking service times out on start but the process is running. What is the most common configuration mistake?

**Internal**  
8. Describe the steps systemd takes when stopping a service that has no `ExecStop=` line.

**Explain in your own words**  
9. Explain why running daemons in the foreground under `Type=simple` (or `notify`) is preferred to traditional double-fork daemonisation.

## 18. Mastery Criteria

- **Basic understanding**: You can read a service unit’s `Type=`, `ExecStart=`, and `Restart=` lines and interpret `systemctl status`.  
- **Working understanding**: You can write a correct simple service unit, choose an appropriate restart policy, and diagnose start-timeout and crash-loop problems.  
- **Strong understanding**: You can select the right `Type=` for a given program behaviour, design stop/reload behaviour, and explain readiness notification and rate limiting.

## 19. What I Should Now Be Able to Explain

- Meaning of the principal `Type=` values  
- Role of `ExecStart=`, `ExecStop=`, `ExecReload=`, and the Pre/Post variants  
- Restart policies and start-rate limiting  
- Default stop sequence and `KillMode=`  
- How `User=`, environment, and basic resource directives are applied  
- Difference between main PID and other processes in the control group  
- Why `daemon-reload` is required after unit changes  
- Common failure modes: wrong Type, missing PID file, missing readiness notification, crash loops

## 20. Next Session

**Next Session Number**  
SESSION 21  

**Next Session Title**  
Journald and the Systemd Journal: Querying, Persistence, and Practical Logging  

**Why it comes next**  
You can now start, stop, and supervise services. The next operational necessity is to see what those services (and the system itself) are saying. Session 21 covers the systemd journal—how logs are collected, stored, queried, and related to units—so that status inspection and troubleshooting become evidence-based rather than guesswork.
