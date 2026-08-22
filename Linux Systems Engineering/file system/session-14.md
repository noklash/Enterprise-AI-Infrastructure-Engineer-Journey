# Session 14 — Users, Groups, UID/GID Databases, and the Identity Lookup Path

## 1. Position in the Curriculum

**Phase**  
PHASE 1 — Linux Systems Engineering

**Module**  
MODULE 2 — Permissions and Security

**Session**  
SESSION 14 — Users, Groups, UID/GID Databases, and the Identity Lookup Path

**Prerequisites**  
- Classic permissions, ownership, and mode bits (Session 10)  
- Special bits and umask (Sessions 11–12)  
- Access Control Lists (Session 13)  
- Process credentials (real / effective UID and GID)

**What this session unlocks**  
Understanding of how numeric UIDs and GIDs are assigned, stored, and resolved to human-readable names, and how the system locates the authentication and group-membership information for a given identity. This is required before studying authentication (PAM), SSH, sudo, and any centralised identity system.

## 2. Why This Session Exists

Every permission check you have studied ultimately compares **numeric** UIDs and GIDs:

- the credentials of the calling process  
- the owner UID / group GID stored in the inode  
- any named-user or named-group entries in an ACL  

Humans, however, work with names (`alice`, `developers`, `root`). The kernel itself does not store names; it only stores integers. The translation between names and numbers, and the storage of the additional account attributes (home directory, shell, password hash, group membership), is performed by userspace databases and a flexible lookup mechanism.

Until you understand:

- what `/etc/passwd`, `/etc/group`, and `/etc/shadow` contain,  
- how the Name Service Switch (NSS) decides where to look,  
- and how a process obtains the UID that will later be used in permission checks,

the identity side of the security model remains incomplete.

## 3. Learning Objectives

By the end of this session you will be able to:

- Describe the purpose and format of `/etc/passwd`, `/etc/group`, and `/etc/shadow`.  
- Explain why password hashes were moved from `/etc/passwd` to `/etc/shadow`.  
- Use `getent`, `id`, `getpwuid`, `getgrnam` (and the corresponding commands) to resolve names and numbers.  
- Explain the role of the Name Service Switch (NSS) and the meaning of the entries in `/etc/nsswitch.conf`.  
- Trace the lookup path that turns a username into a UID (and vice versa).  
- Create and examine a local user and group, and show the resulting database entries.  
- Predict the effect of a missing or incorrect NSS configuration on login and on permission tools.  
- Distinguish the information the kernel needs (UID/GID) from the information that only userspace needs (home, shell, password hash).

## 4. Prerequisite Concepts

You already know:

- Processes carry numeric real and effective UIDs/GIDs.  
- Inodes store numeric owner UID and group GID.  
- Permission checks and ACL evaluation are performed on those numbers.  
- Root is UID 0.

## 5. Mental Model

```
Human name (“alice”)
        │
        ▼
┌───────────────────────┐
│  Name Service Switch  │  ← /etc/nsswitch.conf
│  (passwd: files …)    │
└───────────┬───────────┘
            │
            ▼
┌───────────────────────┐
│  Data sources         │
│  /etc/passwd          │
│  /etc/group           │
│  /etc/shadow          │
│  (optional: LDAP,     │
│   SSSD, Winbind …)    │
└───────────┬───────────┘
            │
            ▼
     Numeric UID / GID
            │
            ▼
   Kernel credential checks
   (permissions, ACLs, …)
```

The kernel never reads `/etc/passwd`. It only ever sees integers. All name resolution is performed by libraries in userspace (or by privileged helpers).

## 6. Core Concept

### `/etc/passwd`

Text file, one line per account, colon-separated fields:

```
username : password-placeholder : UID : GID : GECOS : home : shell
```

- The password field is almost always `x`, meaning “the real hash is in `/etc/shadow`”.  
- UID 0 is root.  
- System accounts traditionally occupy low UIDs; ordinary users start at a distribution-defined value (often 1000).

### `/etc/shadow`

Restricted-access file that stores the password hash and account-aging fields:

```
username : hash : lastchange : min : max : warn : inactive : expire : …
```

Only root can read it. This separation prevents ordinary users from obtaining password hashes for offline attack while still allowing them to read `/etc/passwd` for name-to-UID mapping.

### `/etc/group`

```
groupname : password-placeholder : GID : comma-separated-member-list
```

Supplementary group membership is recorded here. A user’s primary group is the GID stored in `/etc/passwd`; additional groups appear in the member lists of `/etc/group`.

### Name Service Switch (NSS)

`/etc/nsswitch.conf` tells the C library (and therefore almost every program) **where** to look for each type of information:

```
passwd:     files systemd
group:      files systemd
shadow:     files
hosts:      files dns
…
```

`files` means the classic `/etc` databases. Other modules (ldap, sss, winbind, etc.) can be added so that the same `getent passwd alice` command works whether alice is a local account or a centrally managed one.

### Lookup path for a username

1. Application calls a libc function (`getpwnam`, `getpwuid`, etc.).  
2. libc consults `/etc/nsswitch.conf`.  
3. For each listed source, the corresponding NSS module is asked.  
4. The first successful answer is returned.  
5. The application receives a numeric UID (and other fields) and can then set credentials or perform permission-related work.

The kernel is not involved in the name lookup; it only receives the resulting integers when a process is created or when credentials are changed.

## 7. Break It Into the Smallest Important Pieces

### 7.1 UID
- Integer user identifier.  
- Stored in process credentials and in inode ownership.  
- 0 = root.

### 7.2 GID
- Integer group identifier.  
- Primary GID in the user database; supplementary GIDs in the group database.

### 7.3 `/etc/passwd`
- Public mapping of name → UID, primary GID, home, shell, etc.

### 7.4 `/etc/shadow`
- Private storage of password hashes and aging data.

### 7.5 `/etc/group`
- Mapping of group name → GID and list of members.

### 7.6 NSS (`/etc/nsswitch.conf`)
- Policy that selects which data sources are queried and in which order.

### 7.7 `getent`
- Command that exercises the same NSS lookup path used by applications; preferred diagnostic tool.

### 7.8 System accounts versus user accounts
- System accounts (daemons, services) usually have UIDs below a cut-off and often have `/usr/sbin/nologin` or `/bin/false` as the shell.

## 8. What Linux Is Actually Doing

**Name → UID resolution (userspace)**
```
getpwnam("alice")
    → libc reads /etc/nsswitch.conf
    → for each module in the passwd line:
          call into the module (e.g. files → parse /etc/passwd)
          if found: return the passwd entry (including UID)
    → caller receives struct passwd containing pw_uid = 1001
```

**Process creation with that identity**
```
login / sshd / sudo / su / systemd user unit
    → after authentication, calls setuid() / setgid() / setgroups()
      (or the equivalent keyring / credential-setting path)
    → kernel now holds only the numeric credentials
    → all subsequent permission checks use those numbers
```

The kernel never opens `/etc/passwd` or `/etc/shadow`.

## 9. Commands and Tools

| Command | Purpose |
|---------|---------|
| `getent passwd alice` | Resolve user via the full NSS path |
| `getent passwd 1001` | Reverse lookup by UID |
| `getent group developers` | Resolve group |
| `getent group 1001` | Reverse lookup by GID |
| `id alice` | UID, GID, and supplementary groups for a user |
| `id` | Same for the current process |
| `whoami` / `groups` | Quick identity checks |
| `cat /etc/passwd` | View the local user database (safe) |
| `sudo cat /etc/shadow` | View password hashes (root only) |
| `cat /etc/group` | View local groups |
| `cat /etc/nsswitch.conf` | View lookup policy |
| `useradd` / `adduser` | Create a local user (distribution wrappers differ) |
| `groupadd` | Create a local group |
| `usermod` / `groupmod` | Modify existing entries |
| `passwd` | Set or change a password (updates shadow) |

Prefer `getent` over directly grepping `/etc/passwd` when diagnosing lookup problems; `getent` respects NSS and will show centrally managed accounts as well.

## 10. Hands-On Lab

**Objective**  
Inspect the identity databases, resolve names and numbers through NSS, create a local user and group, and observe the resulting entries.

**Setup**  
Ordinary Ubuntu system; sudo access required for account creation and shadow inspection.

```bash
mkdir -p ~/identity-lab
cd ~/identity-lab
```

**Steps**

1. Examine the local databases (read-only):
```bash
head -5 /etc/passwd
sudo head -3 /etc/shadow
head -5 /etc/group
cat /etc/nsswitch.conf | grep -E 'passwd|group|shadow'
```

2. Resolve identities with the same path applications use:
```bash
getent passwd $USER
getent passwd $(id -u)
getent group $(id -gn)
id
```

3. Create a local group and user (names chosen to avoid collisions):
```bash
sudo groupadd labgroup$$
sudo useradd -m -g labgroup$$ -s /bin/bash labuser$$
getent passwd labuser$$
getent group labgroup$$
id labuser$$
```

4. Inspect the new lines in the databases:
```bash
getent passwd labuser$$
sudo getent shadow labuser$$
grep labuser$$ /etc/passwd
grep labgroup$$ /etc/group
```

5. Observe supplementary group membership:
```bash
sudo usermod -aG labgroup$$ $USER
id $USER
# Note: existing login sessions do not automatically gain the new group;
# a new login (or newgrp / su) is required to pick it up.
```

6. Clean up the temporary account:
```bash
sudo userdel -r labuser$$
sudo groupdel labgroup$$
```

**Verification**  
You must be able to:

- Show the seven fields of a `/etc/passwd` entry and explain each.  
- Demonstrate that `getent passwd` returns the same information whether you query by name or by UID.  
- Create a user and group and locate their records with `getent`.  
- Explain why `/etc/shadow` is not world-readable.

**Cleanup**  
The temporary user and group should already have been removed. Remove the lab directory if desired:
```bash
rm -rf ~/identity-lab
```

## 11. Investigation Lab

**Scenario**  
A newly provisioned service account appears in `/etc/passwd` and can be resolved with `getent passwd svcapp`, yet a permission check that should succeed for that account fails. `id svcapp` shows the expected UID, but the process that is actually running the service has a different UID.

**Objective**  
Determine where the identity mismatch was introduced.

**Available tools**  
`getent`, `id`, `ps`, `/proc/<pid>/status`, service unit files, `ls -l` on the relevant files.

**Initial clues**  
- `getent passwd svcapp` returns the expected UID.  
- The service process is running with a different numeric UID.  
- The service was recently migrated to systemd.

**Investigation questions**  
1. What is the difference between “the account exists in the user database” and “the process is running with that UID”?  
2. Where does a systemd service obtain the UID it will run as?  
3. How do you prove the UID of a running process independently of the service manager’s configuration?  
4. What common configuration errors produce this symptom?

Work the questions before reading the solution.

**Solution**  
The user database only supplies a mapping. The process credentials are set at process-creation time by the program that starts the service (systemd, an init script, a container runtime, etc.).

```bash
ps -o pid,user,uid,cmd -C <service-binary>
# or
cat /proc/<pid>/status | grep -E 'Uid|Gid'
systemctl cat <service> | grep -E 'User=|Group='
```
Typical causes:

- `User=` directive missing or misspelled in the unit file → service runs as root or as the default.  
- Hard-coded UID in an old init script that no longer matches the account.  
- Container `USER` instruction or securityContext that overrides the intended identity.

Fix the starter configuration so that it sets the correct UID/GID; the presence of the account in `/etc/passwd` is necessary but not sufficient.

## 12. Production Failure Scenario

**Incident**  
After an automated configuration-management run, ordinary users can no longer log in. Root can still log in on the console. `/etc/passwd` looks intact, but `getent passwd alice` returns nothing for ordinary accounts while `getent passwd root` succeeds.

**Systematic troubleshooting**

1. **Observation**  
   Interactive logins fail for normal users; root works; `getent` fails for those users.

2. **Hypothesis**  
   NSS configuration or a required data source is broken; the `files` module is no longer being consulted, or the passwd line has been corrupted.

3. **Evidence**  
   ```bash
   cat /etc/nsswitch.conf
   getent passwd root
   getent passwd alice
   ls -l /etc/passwd /etc/shadow
   # Check for recent changes
   journalctl -u <config-management> --since "1 hour ago"
   ```

4. **Confirmation**  
   The `passwd:` line in `/etc/nsswitch.conf` was accidentally changed to a module that is not installed or is misconfigured, and `files` is missing or ordered after a failing module that returns an error.

5. **Resolution**  
   Restore a working `passwd: files` (or `files systemd`, etc.) line. Confirm with `getent passwd alice`. Restart affected name-cache services if any (nscd, sssd, …).  

6. **Prevention**  
   Configuration management must treat `/etc/nsswitch.conf` as a critical file; validate it after changes; prefer additive modules over wholesale replacement.

## 13. Connection to Previous Linux Knowledge

- Permission checks and ACL evaluation (Sessions 10–13) operate exclusively on the numeric UIDs/GIDs that this session explains how to obtain.  
- The owner and group fields shown by `ls -l` are the result of a reverse NSS lookup from the numbers stored in the inode.  
- Process credentials (real/effective UID/GID) are set from the numbers returned by these databases at login or service-start time.  
- setuid binaries change the effective UID to the number stored in the file’s inode; that number was originally assigned through the mechanisms in this session.

## 14. Connection to Future Infrastructure

- **PAM** (next major topic) performs authentication and then relies on the same identity databases to establish the session’s credentials.  
- **SSH**: public-key and password authentication both ultimately result in a UID that is looked up through NSS.  
- **sudo / polkit**: policy is often written in terms of names; enforcement uses the corresponding numbers.  
- **Containers**: `/etc/passwd` inside an image supplies names for tools; user namespaces map container UIDs to host UIDs; mismatches produce the classic “files owned by unknown UID” appearance.  
- **Kubernetes**: service accounts, runAsUser, fsGroup, and projected tokens all rest on numeric identities; the cluster identity system is a distributed analogue of the local databases.  
- **Centralised identity (LDAP, FreeIPA, Active Directory via SSSD)**: appears simply as additional NSS modules; the rest of the permission model stays unchanged.

## 15. Engineering Questions

1. Why does the kernel never read `/etc/passwd`?  
2. What problem does `/etc/shadow` solve that storing hashes in `/etc/passwd` would not?  
3. What is the purpose of the Name Service Switch?  
4. Why is `getent passwd alice` preferable to `grep alice /etc/passwd` when diagnosing login problems?  
5. What is the difference between a user’s primary group and their supplementary groups?  
6. How does a process that was started before a group membership change pick up the new membership?  
7. What information in `/etc/passwd` is required by the kernel versus required only by userspace programs?  
8. Why can root still log in when NSS is broken for ordinary users?  
9. How does the existence of a user account in the database relate to the UID under which a systemd service actually runs?

## 16. Practical Assignment

1. Produce a clean map (table or short text) of the fields in `/etc/passwd`, `/etc/shadow`, and `/etc/group`, marking which fields are used by the kernel and which are used only by userspace.  

2. Using only `getent` and `id`, demonstrate:  
   - name → UID resolution  
   - UID → name resolution  
   - full group membership for your account  

3. Create a temporary user and group, document every line that appears in the three databases, then delete them cleanly.  

4. Inspect `/etc/nsswitch.conf` and write a short explanation of what would happen if the `passwd: files` entry were removed on a system that has no other identity source.

## 17. Session Completion Test

Answer without notes.

**Conceptual**  
1. What is stored in `/etc/passwd` versus `/etc/shadow`?  
2. What is the Name Service Switch and why does it exist?

**System behavior**  
3. A user account exists in `/etc/passwd`. Does that guarantee that a service configured with `User=thatname` will run as the corresponding UID?  
4. Why do newly added supplementary groups not appear in an already-running shell?

**Command interpretation**  
5. What does `getent passwd 1000` return and which subsystem does it exercise?  
6. Why is `/etc/shadow` mode `640` or `000` and owned by root?

**Troubleshooting**  
7. `getent passwd alice` returns nothing, yet the line exists in `/etc/passwd`. What is the first configuration file you examine?

**Internal**  
8. Describe the steps from a program calling `getpwnam("alice")` to the program receiving the numeric UID.

**Explain in your own words**  
9. Explain why permission checks can work entirely with integers while humans still need the name databases.

## 18. Mastery Criteria

- **Basic understanding**: You can read `/etc/passwd` and `/etc/group`, resolve names with `getent` and `id`, and explain why `/etc/shadow` exists.  
- **Working understanding**: You can create and delete local users/groups, interpret `/etc/nsswitch.conf`, and diagnose mismatches between database entries and running process credentials.  
- **Strong understanding**: You can trace the full identity path from a login or service start to the numeric credentials used in permission checks, and you can predict the effect of NSS misconfiguration on both interactive and non-interactive workloads.

## 19. What I Should Now Be Able to Explain

- Contents and purpose of `/etc/passwd`, `/etc/group`, and `/etc/shadow`  
- Separation of public identity data from private password hashes  
- Numeric UID/GID versus human-readable names  
- Role of the Name Service Switch and `/etc/nsswitch.conf`  
- How `getent` and libc resolution work  
- Difference between primary and supplementary groups  
- Relationship between an account existing in the database and a process actually running with that UID  
- Why the kernel never opens these files itself

## 20. Next Session

**Next Session Number**  
SESSION 15  

**Next Session Title**  
Pluggable Authentication Modules (PAM) and the Authentication Path  

**Why it comes next**  
You now understand how identities are stored and resolved to numbers. The next session examines how a user (or service) proves that they are entitled to that identity—i.e. the authentication step—and how Linux modularises that process through PAM, which sits between login programs (login, sshd, sudo, …) and the identity databases you have just studied.
