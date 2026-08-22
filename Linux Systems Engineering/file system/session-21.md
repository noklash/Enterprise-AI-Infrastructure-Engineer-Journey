# Session 21 — Journald and the Systemd Journal: Querying, Persistence, and Practical Logging

## 1. Position in the Curriculum

**Phase**  
PHASE 1 — Linux Systems Engineering

**Module**  
MODULE 3 — System Management (systemd)

**Session**  
SESSION 21 — Journald and the Systemd Journal: Querying, Persistence, and Practical Logging

**Prerequisites**  
- Systemd units, targets, and service lifecycle (Sessions 19–20)  
- Basic familiarity with text logs and the idea of stdout/stderr  
- Process model and service main PIDs

**What this session unlocks**  
The ability to retrieve, filter, and interpret the authoritative logs produced by systemd and by services under its control. This is the primary evidence source for boot problems, service failures, and day-to-day operational troubleshooting on modern Linux systems.

## 2. Why This Session Exists

You can now declare, start, stop, and supervise services. When something goes wrong the next question is always: **what did the system or the service actually say?**

On a systemd host the answer is almost always found in the **journal** — the structured, indexed log managed by `systemd-journald`.

The journal replaces (or coexists with) a collection of ad-hoc text files under `/var/log`. It provides:

- structured fields (unit, PID, priority, etc.) rather than only free text  
- tight integration with units (`journalctl -u …`)  
- reliable capture of early boot messages  
- optional persistence across reboots  
- powerful filtering and cursor-based navigation  

Without fluency in the journal you are forced to guess why a service failed, why a boot hung, or what a daemon printed before it crashed. With it, troubleshooting becomes an evidence-driven process.

## 3. Learning Objectives

By the end of this session you will be able to:

- Explain the role of `systemd-journald` and how it receives log data.  
- Use `journalctl` to view the entire journal, the current boot, and the logs of a specific unit.  
- Filter by time, priority, unit, PID, and free-text patterns.  
- Distinguish between volatile and persistent journal storage and configure basic retention.  
- Follow logs in real time and extract logs from a previous boot.  
- Relate journal entries to the corresponding service unit and to classic syslog priorities.  
- Perform the standard “service failed → inspect its journal” workflow quickly and accurately.

## 4. Prerequisite Concepts

You already know:

- How services are started and supervised by systemd.  
- That a service has a main PID and may emit stdout/stderr.  
- That `systemctl status` shows a short tail of recent journal lines.  
- Classic severity ideas (debug, info, warning, error, …).

## 5. Mental Model

```
Sources of log data
  ├─ service stdout / stderr (captured by systemd)
  ├─ sd_journal API / syslog(3) calls
  ├─ kernel messages (kmsg)
  ├─ audit subsystem (when present)
  └─ other journal clients
            │
            ▼
   systemd-journald
            │
            ├─ in-memory / run-time journal  (/run/log/journal)
            └─ optional persistent journal   (/var/log/journal)
            │
            ▼
   journalctl  (query / filter / follow / export)
```

Every log line is a set of **fields** (key-value pairs). The human-readable message is only one field among many.

## 6. Core Concept

### What journald does

`systemd-journald` is a system service that:

- collects messages from multiple sources  
- stores them in a compact, indexed binary format  
- exposes them to clients via an API and via `journalctl`  
- can keep logs only for the current boot (volatile) or across reboots (persistent)

### Structured fields

Important fields you will use constantly:

| Field | Meaning |
|-------|---------|
| `_SYSTEMD_UNIT` / `UNIT` | systemd unit that owns the message |
| `_PID` | process ID that logged the message |
| `_COMM` | process name |
| `PRIORITY` | syslog-style severity (0 emerg … 7 debug) |
| `MESSAGE` | the human-readable text |
| `__REALTIME_TIMESTAMP` | wall-clock time |
| `_BOOT_ID` | identifier of the boot |

Filtering on these fields is far more reliable than grepping free text.

### Volatile vs persistent storage

- Default on many systems: journal kept under `/run/log/journal` (tmpfs) → lost on reboot.  
- Persistent: directory `/var/log/journal` exists and is used → logs survive reboots.  
- Controlled by `Storage=` in `/etc/systemd/journald.conf` (`auto`, `volatile`, `persistent`, `none`).

### Retention and size limits

Journald rotates and vacuums its own files according to:

- `SystemMaxUse=` / `RuntimeMaxUse=`  
- `SystemKeepFree=` / `RuntimeKeepFree=`  
- `MaxFileSec=` / `MaxRetentionSec=`  

These prevent the journal from filling the disk.

### Relationship to classic syslog

Many systems still run `rsyslog` or `syslog-ng`, which may forward from the journal or receive a copy of messages. For service-centric troubleshooting, `journalctl` is usually the first and most precise tool; classic text files remain useful for long-term archival or for tools that only understand plain text.

## 7. Break It Into the Smallest Important Pieces

### 7.1 journald daemon
- The collector and store.  
- Runs early and is itself a systemd service.

### 7.2 Journal files
- Binary, append-only, indexed.  
- Located under `/run/log/journal/<machine-id>/` and/or `/var/log/journal/<machine-id>/`.

### 7.3 Fields
- Structured metadata attached to every entry.  
- Enable precise queries without brittle string matching.

### 7.4 Boots
- Each boot has a unique `_BOOT_ID`.  
- `journalctl --list-boots` enumerates them; `-b` selects one.

### 7.5 Priorities
- 0–7 scale (emerg … debug), compatible with classic syslog.

### 7.6 Vacuum / rotation
- Automatic or manual (`journalctl --vacuum-size=`, `--vacuum-time=`) reclamation of space.

### 7.7 Forwarding
- Optional forwarding to console, syslog, or wall; controlled by journald.conf and unit settings (`StandardOutput=`, `StandardError=`).

## 8. What Linux Is Actually Doing

**Service logs its stdout**
```
service process writes to stdout
    → systemd has attached a pipe / socket to that file descriptor
    → data is handed to journald
    → journald adds fields (_SYSTEMD_UNIT, _PID, PRIORITY, …)
    → entry is appended to the current journal file
```

**Query**
```
journalctl -u myapp.service -p err
    → opens journal files
    → applies filters (unit, priority, time, boot, …)
    → formats matching entries for display
```

**Persistence**
```
if /var/log/journal exists (or Storage=persistent)
    → journald writes there instead of (or in addition to) /run
    → logs remain available after reboot under previous boot IDs
```

## 9. Commands and Tools

| Command | Purpose |
|---------|---------|
| `journalctl` | Default view of the journal (pager) |
| `journalctl -b` | Current boot only |
| `journalctl -b -1` | Previous boot |
| `journalctl --list-boots` | List known boots |
| `journalctl -u unit` | Messages for a specific unit |
| `journalctl -f` | Follow (like `tail -f`) |
| `journalctl -p err` / `-p warning` | Filter by priority |
| `journalctl --since "1 hour ago"` | Time filter |
| `journalctl -k` | Kernel messages only |
| `journalctl -o verbose` / `-o json` | Alternative output formats |
| `journalctl --disk-usage` | Space used by the journal |
| `journalctl --vacuum-size=200M` | Reclaim space |
| `systemctl status unit` | Includes a short, recent journal tail |

Configuration: `/etc/systemd/journald.conf` and drop-ins under `journald.conf.d/`.

## 10. Hands-On Lab

**Objective**  
Practice the queries you will use every day: by unit, by boot, by time, by priority, and live follow.

**Setup**  
Any systemd system. Persistence is nice but not required.

```bash
mkdir -p ~/journal-lab
cd ~/journal-lab
```

**Steps**

1. Basic views:
```bash
journalctl --disk-usage
journalctl --list-boots
journalctl -b | head -50
```

2. Unit-centric investigation (use a real service, e.g. ssh):
```bash
systemctl status ssh
journalctl -u ssh -b --no-pager | tail -30
journalctl -u ssh -p warning -b
```

3. Time and priority filters:
```bash
journalctl --since "10 min ago" --no-pager | tail -20
journalctl -p err -b --no-pager
journalctl -p err..warning -b --no-pager | tail -20
```

4. Follow mode (run a command that generates a log line in another terminal):
```bash
# Terminal 1
journalctl -f -u ssh

# Terminal 2 (example)
sudo systemctl reload ssh
# Watch the line appear in Terminal 1
```

5. Previous boot (if the journal is persistent):
```bash
journalctl --list-boots
journalctl -b -1 -u ssh --no-pager | tail -20
# If -1 is empty, the journal is probably volatile only
```

6. Kernel messages and structured output:
```bash
journalctl -k -b --no-pager | tail -20
journalctl -u ssh -o verbose -n 3
```

7. (Optional) Make the journal persistent and confirm:
```bash
sudo mkdir -p /var/log/journal
sudo systemctl restart systemd-journald
# After a reboot, previous-boot queries would then work
# For the lab it is enough to observe that the directory is used:
journalctl --disk-usage
```

**Verification**  
You must be able to:

- Show logs for the current boot only.  
- Show logs for one unit for the current boot.  
- Filter by priority and by time.  
- Follow a unit’s logs live.  
- State whether your journal is currently persistent or volatile.

**Cleanup**
```bash
rm -rf ~/journal-lab
# Leave journald configuration as you found it unless you intentionally made it persistent
```

## 11. Investigation Lab

**Scenario**  
A service is in the `failed` state after boot. `systemctl status myapp` shows only a few lines and ends with “Main process exited, code=exited, status=1/FAILURE”. You need the detailed error message the application printed.

**Objective**  
Extract the full relevant log for that service from the current boot and identify the first fatal error line.

**Available tools**  
`systemctl status`, `journalctl -u`, time filters, priority filters, `journalctl -o cat` / `-o verbose`

**Initial clues**  
- Unit is failed.  
- Status shows only a short tail.  
- Application is known to print a clear error to stderr before exiting.

**Investigation questions**  
1. Why is `systemctl status` often insufficient for root-cause analysis?  
2. Which `journalctl` invocation gives you the complete story for this boot?  
3. How do you jump to the first error if the log is long?  
4. How do you confirm that the lines you are reading belong to the failed activation and not to an older run?

Work the questions before reading the solution.

**Solution**  
```bash
systemctl status myapp
journalctl -u myapp -b --no-pager
# or, more focused:
journalctl -u myapp -b -p err --no-pager
journalctl -u myapp -b --since "today" -o cat
```
`systemctl status` deliberately shows only a short recent excerpt. The full activation attempt—including stdout/stderr captured by systemd—is in the journal for that unit and boot. The first ERROR/FATAL line (or the first non-zero exit context) is usually the smoking gun. Note the timestamps so you correlate with the failed start job.

## 12. Production Failure Scenario

**Incident**  
After a reboot a critical service fails to start. The on-call engineer has console access but no persistence was configured for the journal, and the only visible clue in the current boot is that the unit is failed. A similar incident happened last week and the logs from that boot are gone.

**Systematic troubleshooting**

1. **Observation**  
   Service failed on the current boot; previous-boot journal data is unavailable.

2. **Hypothesis**  
   Journal is volatile-only (`Storage=auto` and `/var/log/journal` missing, or explicit `volatile`).

3. **Evidence**  
   ```bash
   journalctl --list-boots
   journalctl --disk-usage
   ls /var/log/journal
   grep ^Storage /etc/systemd/journald.conf /etc/systemd/journald.conf.d/* 2>/dev/null
   ```

4. **Immediate recovery**  
   - Use whatever is still in the current-boot journal (`journalctl -u unit -b`).  
   - Inspect application-specific log files if the service writes any.  
   - Attempt a manual start with debugging if safe:  
     `systemctl start unit` while following `journalctl -u unit -f`.

5. **Permanent fix**  
   - Enable persistent journal:  
     ```bash
     sudo mkdir -p /var/log/journal
     sudo systemctl restart systemd-journald
     ```  
     or set `Storage=persistent` and restart journald.  
   - Set reasonable size limits so the journal cannot fill the disk.  
   - Confirm after the next reboot that `--list-boots` shows more than one entry.

## 13. Connection to Previous Linux Knowledge

- Service units (Session 20) have their stdout/stderr captured automatically; the journal is where that data lands.  
- `systemctl status` is a thin presentation layer on top of unit state + a short journal query.  
- Boot targets and ordering (Session 19) determine when a service starts; the journal tells you what happened at that moment.  
- Kernel messages you previously saw via `dmesg` are also ingested into the journal and visible with `journalctl -k`.  
- Privilege and identity (security module) still apply: ordinary users may be restricted to their own journal data; root (or members of the `systemd-journal` / `adm` groups) can see the system journal.

## 14. Connection to Future Infrastructure

- **Containers**: container runtimes often forward container logs to the host journal or to a log driver; node-level troubleshooting still starts with `journalctl` on the host.  
- **Kubernetes**: kubelet and system addons on nodes are frequently systemd services; node-level journal access is a standard debugging path when the control plane cannot reach the node.  
- **Centralised logging**: journald is commonly a source for fluentd, rsyslog, Vector, or promtail agents that ship logs to ELK, Loki, Cloud Logging, etc.  
- **Observability**: structured journal fields map cleanly onto label-based log systems; treating the journal as structured data rather than opaque text is the modern practice.  
- **AI infrastructure**: training launchers, GPU device plugins, and model servers run as services; their failure modes are diagnosed first through the unit journal, then through application-specific logs.

## 15. Engineering Questions

1. What sources of log data does journald collect?  
2. Why are journal entries more than just text lines?  
3. What is the difference between a volatile and a persistent journal?  
4. How do you view logs from the previous boot?  
5. Why is `journalctl -u myapp -b` usually better than `tail /var/log/myapp.log` for a systemd service?  
6. What does the priority filter `-p err` select?  
7. How does journald prevent the journal from filling the disk?  
8. What relationship exists between `systemctl status` and the journal?  
9. When would you still look at classic text logs under `/var/log` in addition to the journal?

## 16. Practical Assignment

1. For three different services on your system, extract:  
   - the last 20 log lines of the current boot  
   - any lines at priority warning or higher  
   Document the exact `journalctl` commands.  

2. Determine whether your journal is persistent. If it is not, enable persistence in a controlled way and verify that a new boot appears in `--list-boots` after a reboot (lab VM only).  

3. Simulate a service failure (use the crash-demo style unit from Session 20 or an equivalent), then perform a full journal-based investigation and write a short incident note that quotes the decisive log lines.  

4. Produce a personal “first five journalctl commands” cheat sheet for on-call use.

## 17. Session Completion Test

Answer without notes.

**Conceptual**  
1. What is the systemd journal and what problem does it solve?  
2. What is the difference between volatile and persistent storage for the journal?

**System behavior**  
3. A service writes an error to stderr and exits. Where does that message appear?  
4. Why might `journalctl -b -1` return no data on some systems?

**Command interpretation**  
5. What does `journalctl -u ssh -p err -b` show?  
6. What does `journalctl --list-boots` tell you?

**Troubleshooting**  
7. A unit is failed after boot. What is the first journalctl command you run?

**Internal**  
8. Describe how a line written to stdout by a service under systemd reaches a journalctl query.

**Explain in your own words**  
9. Explain why structured fields make journal queries more reliable than grepping plain text log files.

## 18. Mastery Criteria

- **Basic understanding**: You can view current-boot logs and unit logs with `journalctl`.  
- **Working understanding**: You can filter by time, priority, and unit; follow logs live; distinguish volatile from persistent storage; and use the journal as the primary evidence source for a failed service.  
- **Strong understanding**: You can configure basic persistence and retention, correlate journal data with unit state and boot IDs, and integrate journal queries into a systematic troubleshooting procedure.

## 19. What I Should Now Be Able to Explain

- Role of systemd-journald  
- Structured nature of journal entries and important fields  
- Volatile versus persistent journal storage  
- Core `journalctl` invocations (boot, unit, time, priority, follow)  
- Relationship between service stdout/stderr and the journal  
- How to obtain logs from previous boots  
- Basic retention / vacuum controls  
- Why the journal is the first place to look when a unit fails  

## 20. Next Session

**Next Session Number**  
SESSION 22  

**Next Session Title**  
Boot Process Deep Dive: From Firmware to Default Target  

**Why it comes next**  
You can now supervise services and read their logs. The next session connects those skills to the full boot sequence—firmware, bootloader, kernel, initramfs, and systemd’s progression through targets—so you can diagnose systems that fail before or during the transition to a usable multi-user state.
