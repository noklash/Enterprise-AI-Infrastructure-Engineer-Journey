# Session 23 — Practical System Administration: Package Management, Updates, and System State

## 1. Position in the Curriculum

**Phase**  
PHASE 1 — Linux Systems Engineering

**Module**  
MODULE 3 — System Management (systemd)

**Session**  
SESSION 23 — Practical System Administration: Package Management, Updates, and System State

**Prerequisites**  
- Filesystem hierarchy and the locations of binaries, libraries, and configuration (Sessions 04–06)  
- Systemd units and how services are installed and enabled (Sessions 19–20)  
- Boot process and the artefacts under `/boot` (Session 22)  
- Basic privilege elevation with sudo (Session 16)

**What this session unlocks**  
The ability to inspect, install, upgrade, and remove software in a controlled way on a Debian/Ubuntu system, understand what a package actually changes on disk, and keep a system’s software state known and recoverable. This is required for patching, incident response, and any environment that is not fully immutable.

## 2. Why This Session Exists

You can boot a system, supervise services, and read logs. A running system, however, is not static: software must be installed, updated, and occasionally removed.  

On Ubuntu (and Debian) that work is done primarily through:

- **dpkg** — the low-level package manager that installs and removes `.deb` packages and records state in its database  
- **APT** (Advanced Package Tool) — the higher-level tool that resolves dependencies, fetches packages from repositories, and invokes dpkg  

Without a clear model of package state you cannot answer:

- What version of OpenSSL is actually installed?  
- Which package owns `/usr/sbin/sshd`?  
- What will happen if I run `apt upgrade`?  
- Why did a service unit appear under `/lib/systemd/system` after an install?  
- How do I back out a bad upgrade?

This session builds that model and the corresponding operational habits.

## 3. Learning Objectives

By the end of this session you will be able to:

- Explain the roles of dpkg and APT and how they relate.  
- Query installed packages, package contents, and reverse ownership of files.  
- Install, upgrade, and remove packages in a controlled manner.  
- Read and interpret APT’s proposed changes before accepting them.  
- Locate the package sources configuration and the local package cache.  
- Describe what a package may place on the system (binaries, libraries, unit files, configuration, maintainer scripts).  
- Perform safe update practices on a lab system and know the main risks on production systems.  
- Map a running service back to the package that provides it.

## 4. Prerequisite Concepts

You already know:

- Where binaries, libraries, configuration, and systemd units typically live.  
- That services are started from unit files that often ship with packages.  
- How to elevate privileges with sudo.  
- That the bootloader and kernel images under `/boot` are also package-managed on most distributions.

## 5. Mental Model

```
Repositories (archive.ubuntu.com, …)
        │
        │  metadata + .deb files
        ▼
┌───────────────────┐
│  APT              │  dependency resolution, download, orchestration
│  apt / apt-get    │
└─────────┬─────────┘
          │  invokes
          ▼
┌───────────────────┐
│  dpkg             │  unpack, configure, run maintainer scripts
│  /var/lib/dpkg    │  database of installed state
└─────────┬─────────┘
          │  places files, runs scripts
          ▼
Filesystem + systemd units + shared libraries + …
```

APT decides *what* should be installed and in which version; dpkg performs the actual installation and records the result.

## 6. Core Concept

### Packages

A **package** is a versioned bundle that typically contains:

- files to be installed (binaries, libraries, man pages, configuration templates, systemd unit files, …)  
- metadata (name, version, dependencies, conflicts, description)  
- maintainer scripts (`preinst`, `postinst`, `prerm`, `postrm`) that run at install/upgrade/remove time  

On Debian/Ubuntu the on-disk format is `.deb`.

### dpkg

`dpkg` is the low-level tool that:

- installs and removes `.deb` files  
- runs maintainer scripts  
- maintains the database under `/var/lib/dpkg`  

It does **not** download packages or resolve dependencies by itself. If a dependency is missing, dpkg will fail and leave the package in a broken state until the dependency is satisfied.

### APT

APT adds:

- repository configuration (`/etc/apt/sources.list` and `/etc/apt/sources.list.d/`)  
- downloaded package indices  
- dependency resolution  
- a cache of downloaded `.deb` files (`/var/cache/apt/archives/`)  
- higher-level commands: `update`, `upgrade`, `full-upgrade`, `install`, `remove`, `purge`, `autoremove`, `search`, `show`, …

Modern practice prefers the `apt` command-line tool for interactive use; `apt-get` and `apt-cache` remain fully supported and are common in scripts.

### Desired state vs actual state

- **Installed** — present according to the dpkg database.  
- **Upgradable** — a newer version is available from the configured repositories.  
- **Broken / half-configured** — an operation failed part-way; must be repaired before further APT operations.  
- **Held** — marked so that APT will not upgrade it automatically.

### What an install actually does

When you `apt install nginx` (for example), APT will:

1. Update its view of available packages if needed.  
2. Compute the set of packages to install/upgrade (including dependencies).  
3. Download the `.deb` files.  
4. Invoke dpkg to unpack and configure them in a valid order.  
5. Maintainer scripts may enable and start systemd units, create users, seed configuration, etc.

That is why a package install can change running service state, not only disk contents.

## 7. Break It Into the Smallest Important Pieces

### 7.1 Package metadata
- Name, version, architecture, dependencies, description, owned files.

### 7.2 dpkg database
- `/var/lib/dpkg/status` and related files — the authoritative record of what is installed.

### 7.3 Repository indices
- Lists of available packages and versions, refreshed by `apt update`.

### 7.4 Dependency graph
- `Depends`, `Recommends`, `Suggests`, `Conflicts`, `Breaks`, `Provides`.  
- APT refuses to perform operations that would leave dependencies unsatisfied (unless forced).

### 7.5 Maintainer scripts
- Run at specific points in the install/upgrade/remove lifecycle.  
- Can enable systemd units, restart services, or migrate configuration.

### 7.6 Configuration files
- Marked specially so that local modifications are not silently overwritten on upgrade (conffile handling prompts or keeps local changes).

### 7.7 Cache
- Downloaded `.deb` files kept under `/var/cache/apt/archives/` until cleaned.

## 8. What Linux Is Actually Doing

**`apt install pkg` (simplified)**
```
apt update          # optional but usual — refresh indices
apt install pkg
    → resolve dependencies
    → download .deb files into the cache
    → for each package in a valid order:
          dpkg --unpack
          run preinst if present
          dpkg --configure
          run postinst if present
    → update dpkg database
    → possibly restart or reload services (via postinst / deb-systemd helpers)
```

**Query path**
```
dpkg -L pkg         # list files owned by an installed package
dpkg -S /path       # which package owns this file
apt show pkg        # metadata from APT’s view (available or installed)
```

## 9. Commands and Tools

| Command | Purpose |
|---------|---------|
| `apt update` | Refresh repository metadata |
| `apt upgrade` / `apt full-upgrade` | Apply available upgrades (full-upgrade may remove packages if needed) |
| `apt install pkg` | Install or upgrade a package |
| `apt remove pkg` | Remove package, keep configuration |
| `apt purge pkg` | Remove package and its configuration |
| `apt autoremove` | Remove packages that were installed only as dependencies and are no longer needed |
| `apt search keyword` | Search package names and descriptions |
| `apt show pkg` | Detailed metadata |
| `apt policy pkg` | Installed version vs candidate versions |
| `dpkg -l` | List installed packages (status flags) |
| `dpkg -L pkg` | List files owned by a package |
| `dpkg -S /path/to/file` | Find owner of a file |
| `dpkg -c package.deb` | List contents of a .deb without installing |
| `apt-cache depends pkg` | Show dependency tree |
| `apt clean` / `apt autoclean` | Manage the package cache |

Always prefer reading the proposed changes (the “The following NEW packages will be installed…” summary) before confirming on a production system.

## 10. Hands-On Lab

**Objective**  
Query package state, map files and services to packages, perform a controlled install/remove cycle, and observe what changes on disk and in systemd.

**Setup**  
Ubuntu lab VM with network access to the configured repositories. Use a disposable package for the install/remove experiment (e.g. `tree` or `htop`).

```bash
mkdir -p ~/pkg-lab
cd ~/pkg-lab
```

**Steps**

1. Baseline and metadata refresh:
```bash
sudo apt update
apt list --installed 2>/dev/null | wc -l
apt policy openssh-server
```

2. Ownership and contents:
```bash
which sshd
dpkg -S $(which sshd)
dpkg -L openssh-server | head -30
systemctl cat ssh | head -5
# Note the unit path and confirm it is owned by the package
dpkg -S /lib/systemd/system/ssh.service 2>/dev/null || dpkg -S /lib/systemd/system/sshd.service 2>/dev/null
```

3. Search and show:
```bash
apt search '^tree$'
apt show tree
```

4. Controlled install:
```bash
sudo apt install tree
which tree
dpkg -L tree
dpkg -l tree
```

5. Remove and purge comparison (re-install first if you want to see both):
```bash
sudo apt remove tree
# configuration (if any) would remain
sudo apt install tree
sudo apt purge tree
dpkg -l tree
```

6. Upgradable packages (read-only observation):
```bash
apt list --upgradable 2>/dev/null
# Do not run a full upgrade unless you intend to change the lab system
```

7. Cache and disk use:
```bash
du -sh /var/cache/apt/archives
apt-cache stats | head -20
```

**Verification**  
You must be able to:

- Identify which package owns a given binary and a given systemd unit file.  
- Install a package, list its files, and remove it cleanly.  
- Explain the difference between `remove` and `purge`.  
- Show the installed vs candidate version of a package with `apt policy`.

**Cleanup**
```bash
sudo apt purge tree 2>/dev/null
sudo apt autoremove -y
rm -rf ~/pkg-lab
```

## 11. Investigation Lab

**Scenario**  
After an unattended upgrade window, `sshd` is no longer running and attempts to start it fail. `which sshd` still points at a binary. The operator needs to know whether the package is still installed correctly and whether files are missing or the unit is broken.

**Objective**  
Verify package integrity and map the service failure back to package state (or rule package state out).

**Available tools**  
`dpkg -l`, `dpkg -V` (verify), `apt policy`, `systemctl status`, `journalctl -u`, `dpkg -S`, `dpkg -L`

**Initial clues**  
- Service failed after an upgrade window.  
- Binary path still exists.  
- Unattended-upgrades or a manual upgrade recently ran.

**Investigation questions**  
1. How do you confirm the package is still in a fully installed (not half-configured) state?  
2. How do you detect missing or modified files that belong to the package?  
3. Where do you look for the maintainer-script or postinst activity that might have altered the unit?  
4. What is the difference between “package is installed” and “service is working”?

Work the questions before reading the solution.

**Solution**  
```bash
dpkg -l openssh-server
apt policy openssh-server
sudo dpkg -V openssh-server
systemctl status ssh
journalctl -u ssh -b
dpkg -L openssh-server | grep systemd
```
`dpkg -l` status should show `ii` (installed). Any other status flags indicate a broken or incomplete state. `dpkg -V` reports missing or modified files (configuration files are often modified intentionally). If the package is intact, the failure is likely configuration, keys, permissions, or a dependency service—not a missing binary. Repair paths include `apt install --reinstall openssh-server` (after confirming that local configuration will be handled acceptably) or fixing the specific configuration error identified in the journal.

## 12. Production Failure Scenario

**Incident**  
An automated `apt full-upgrade` on a small fleet removes a package that was pulled in only as a dependency of a now-removed component. That package happened to provide a shared library still used by a critical application that was installed outside of APT (or via a poorly declared dependency). The application fails at next restart.

**Systematic troubleshooting**

1. **Observation**  
   Application fails with “shared library not found.” Recent upgrade logs show removals.

2. **Hypothesis**  
   `apt autoremove` or `full-upgrade` removed a package that was still needed but not recorded as a dependency of any remaining APT package.

3. **Evidence**  
   ```bash
   journalctl --since "2 days ago" | grep -i apt
   # or /var/log/apt/history.log and term.log
   ls /var/log/apt/
   dpkg -l | grep <library-ish-name>
   ldd /path/to/app | grep "not found"
   ```

4. **Resolution**  
   - Reinstall the missing library package.  
   - If the application is third-party, either package it properly with declared dependencies or mark the shared library package as manually installed (`apt-mark manual pkg`) so autoremove will not take it.  

5. **Prevention**  
   - Review the proposed changes of `full-upgrade` in staging.  
   - Use `apt-mark` to protect packages that must not be autoremoved.  
   - Prefer packaging third-party software as real `.deb` packages with correct dependencies, or use containers/images that pin their own runtime.  
   - Keep APT history logs and enable persistent journal for post-incident analysis.

## 13. Connection to Previous Linux Knowledge

- Packages populate the filesystem hierarchy you studied (binaries under `/usr`, configuration under `/etc`, units under `/lib/systemd/system`).  
- Maintainer scripts often call `systemctl enable` / `daemon-reload` / `restart`, linking package operations to the service model (Sessions 19–20).  
- Kernel and initramfs packages update artefacts under `/boot` that the boot process (Session 22) depends on.  
- Ownership of files (`dpkg -S`) is the package-level analogue of “which component is responsible for this path.”  
- Privilege to run APT/dpkg is governed by the same sudo and capability model you already know.

## 14. Connection to Future Infrastructure

- **Immutable infrastructure**: many modern environments minimise or eliminate on-host package changes and rebuild images instead; understanding packages remains essential because images are built *with* package managers.  
- **Configuration management**: tools like Ansible still invoke APT and must reason about package state, holds, and restarts.  
- **Containers**: container build files (`Dockerfile`, etc.) are largely sequences of package installs; layer caching and image size depend on how cleanly packages are installed and cleaned.  
- **Security / CVE response**: knowing how to determine the installed version (`apt policy`, `dpkg -l`) and how to apply a targeted upgrade is the core of host-level vulnerability management.  
- **AI infrastructure**: driver packages (NVIDIA), CUDA toolkits, and monitoring agents are often distributed as packages or as `.deb`-based installers; mixed manual and APT-managed installs are a common source of broken dependencies on GPU nodes.

## 15. Engineering Questions

1. What is the division of responsibility between APT and dpkg?  
2. What kinds of objects can a single package install on a system?  
3. Why does `apt install` sometimes start or restart a service?  
4. What is the difference between `apt remove` and `apt purge`?  
5. How do you determine which package owns a given file on disk?  
6. What does `apt update` change, and what does it not change?  
7. Why can an unattended upgrade remove packages that an application still needs?  
8. What does the `ii` status in `dpkg -l` mean?  
9. Why should production upgrades be reviewed (or tested in staging) rather than applied blindly?

## 16. Practical Assignment

1. Map the SSH stack on your system: package name(s), versions, unit file path(s), and the main binary. Document the commands used.  

2. Choose a small, harmless package that is not currently installed. Install it, list its files, identify any systemd units it shipped (if any), then purge it and confirm removal.  

3. From `/var/log/apt/` (and the journal if useful), reconstruct the last APT transaction that occurred on the system and summarise what changed.  

4. Write a short personal policy: what you will always check before running `apt full-upgrade` on a production host, and when you would choose a targeted `apt install pkg=version` instead.

## 17. Session Completion Test

Answer without notes.

**Conceptual**  
1. What problem does APT solve that dpkg alone does not?  
2. What is recorded in the dpkg database?

**System behavior**  
3. You run `apt install foo`. Name three kinds of changes that may occur on the system.  
4. What is the difference between a package being removed and being purged?

**Command interpretation**  
5. What does `dpkg -S /usr/sbin/sshd` tell you?  
6. What does `apt policy openssh-server` display?

**Troubleshooting**  
7. After an upgrade a service fails. How do you check whether the package itself is in a broken state?

**Internal**  
8. Describe the high-level steps APT and dpkg take when installing a new package that has one dependency.

**Explain in your own words**  
9. Explain why package-managed systems are easier to audit for “what software is present” than systems maintained by manual compilation and copy.

## 18. Mastery Criteria

- **Basic understanding**: You can update indices, install/remove packages, and query installed versions and file ownership.  
- **Working understanding**: You can map services and binaries back to packages, interpret APT’s proposed changes, distinguish remove vs purge, and investigate post-upgrade service failures.  
- **Strong understanding**: You can reason about dependency and autoremove risks, use package state as part of incident response, and describe how packaging interacts with systemd and the filesystem hierarchy.

## 19. What I Should Now Be Able to Explain

- Roles of APT and dpkg  
- Contents of a package and the dpkg database  
- Repository indices and `apt update`  
- Install / upgrade / remove / purge / autoremove semantics  
- How to find which package owns a file or provides a service  
- How maintainer scripts connect packages to systemd  
- Basic safe-update practices and the main production risks  
- Where APT logs and cached packages live  

## 20. Next Session

**Next Session Number**  
SESSION 24  

**Next Session Title**  
Performance Fundamentals: CPU, Memory, Disk, and the Observation Loop  

**Why it comes next**  
You can now install, run, and maintain software on a host. The next major operational skill is performance: how to observe CPU, memory, and disk behaviour, form hypotheses about bottlenecks, and use the tools that expose saturation, latency, and errors. This begins the performance and troubleshooting module and reuses the process, memory, filesystem, and I/O knowledge built earlier in the curriculum.
