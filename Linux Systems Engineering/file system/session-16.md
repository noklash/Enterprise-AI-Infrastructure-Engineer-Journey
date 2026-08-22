# Session 16 — sudo, Privilege Escalation, and Controlled Administrative Access

## 1. Position in the Curriculum

**Phase**  
PHASE 1 — Linux Systems Engineering

**Module**  
MODULE 2 — Permissions and Security

**Session**  
SESSION 16 — sudo, Privilege Escalation, and Controlled Administrative Access

**Prerequisites**  
- Users, groups, UID/GID, and NSS (Session 14)  
- PAM and the authentication path (Session 15)  
- Classic permissions, special bits, and process credentials (Sessions 10–13)  
- Real versus effective UID/GID

**What this session unlocks**  
Understanding of the primary mechanism used on modern Linux systems to grant controlled, auditable, and limited administrative privilege to ordinary users. This is required for secure operations, least-privilege design, and almost every production access model.

## 2. Why This Session Exists

You now know how a user proves their identity (PAM) and how that identity is represented as numeric credentials that the kernel uses for permission checks.  

In day-to-day system administration the question is rarely “should this person be root all the time?” The practical question is:

- Which specific commands may this person run with elevated privilege?  
- On which hosts?  
- After what authentication?  
- With what logging?

**sudo** (substitute user do / superuser do) is the standard tool that answers those questions. It allows a permitted user to execute a command as another user (commonly root) according to a policy language, while recording the attempt.  

Understanding sudo is essential because:

- almost every production Linux host uses it  
- misconfiguration is a frequent source of both lock-outs and privilege-escalation vulnerabilities  
- it is the bridge between ordinary user accounts and the privileged operations required to manage the system  

This session examines sudo from policy through execution and auditing.

## 3. Learning Objectives

By the end of this session you will be able to:

- Explain the purpose of sudo and how it differs from a persistent root login or from setuid binaries.  
- Describe the high-level execution path of a sudo command (policy check → authentication → credential change → command execution).  
- Read and interpret the main constructs of the sudoers policy language (user specifications, host, command, Runas, tags).  
- Use `visudo`, `sudo -l`, and `sudo -U` safely to inspect and modify policy.  
- Configure common safe patterns (full access for an admin group, limited command lists, NOPASSWD where justified).  
- Relate sudo’s authentication step to the PAM stack you already studied.  
- Understand the logging and auditing behaviour of sudo.  
- Recognise dangerous anti-patterns (world-writable sudoers fragments, overly broad wildcards, NOPASSWD on shells).

## 4. Prerequisite Concepts

You already know:

- How PAM authenticates a user for a service named `sudo`.  
- How process credentials (UID/GID) determine permission outcomes.  
- That setuid-root binaries elevate privilege for a single program; sudo elevates for a controlled set of programs according to policy.  
- That root is UID 0 and bypasses ordinary discretionary checks.

## 5. Mental Model

```
User (UID 1000)
    │
    │  sudo systemctl restart nginx
    ▼
┌──────────────────────────────────────────┐
│ sudo binary (setuid-root)                │
│  1. read policy (/etc/sudoers + /etc/sudoers.d) │
│  2. authenticate via PAM (service “sudo”)│
│  3. if allowed: setuid/setgid to target  │
│  4. execute the requested command        │
│  5. log the attempt                      │
└──────────────────────────────────────────┘
    │
    ▼
Command runs with target privileges (often UID 0)
```

sudo itself is a setuid-root program. Its power comes from that bit; its safety comes from the policy that decides whether the caller is allowed to use that power for a particular command.

## 6. Core Concept

### What sudo provides

- **Controlled elevation** — a user may obtain root (or another user’s) privileges only for commands listed in policy.  
- **Authentication** — usually re-proves the caller’s identity via PAM (password, or other configured factors).  
- **Accountability** — successful and failed attempts are logged.  
- **Limited lifetime** — a successful authentication is cached for a short period (timestamp), after which re-authentication is required.  
- **Fine-grained policy** — different users or groups may be granted different command sets on different hosts.

### The sudoers policy language (essential subset)

Policy is stored in `/etc/sudoers` and in files under `/etc/sudoers.d/`. The fundamental rule form is:

```
user(s)  host(s)  =  (runas-user:runas-group)  command(s)
```

Examples:

```
# Members of group sudo may run any command as root after password
%sudo   ALL=(ALL:ALL) ALL

# User alice may restart nginx without a password
alice   ALL=(root) NOPASSWD: /bin/systemctl restart nginx, /bin/systemctl status nginx

# User bob may run package-management commands as root
bob     ALL=(root) /usr/bin/apt, /usr/bin/apt-get
```

Important elements:

- `%groupname` — refers to a Unix group  
- `ALL` — wildcard for hosts, users, or commands (powerful; use carefully)  
- `NOPASSWD:` — suppress the authentication prompt for the listed commands  
- `PASSWD:` — explicitly require a password (useful to override a previous NOPASSWD)  
- Command arguments can be restricted or left open  

### Safe editing

Always edit sudoers with `visudo` (or `visudo -f /etc/sudoers.d/fragment`). `visudo` locks the file and performs a syntax check before installing the new policy; a syntax error in sudoers can lock out all administrative access.

### Relationship to PAM

sudo is a PAM-aware application. It uses the service name `sudo` (configuration in `/etc/pam.d/sudo`). That stack typically reuses the common authentication modules but can be customised independently of login or SSH.

### Logging

By default sudo logs to the system logger (journal / auth facility). Modern versions can also produce detailed JSON logs or integrate with auditd. Successful and failed attempts, the calling user, the target user, and the command are recorded.

## 7. Break It Into the Smallest Important Pieces

### 7.1 sudo binary
- Installed setuid-root.  
- The only privileged component; policy decides whether that privilege is exercised.

### 7.2 Policy sources
- `/etc/sudoers` — main file.  
- `/etc/sudoers.d/*` — drop-in fragments (preferred for configuration management).  
- Order and `#include` / `#includedir` directives matter.

### 7.3 User specification
- Who is allowed (user, `%group`, `User_Alias`).

### 7.4 Host specification
- Where the rule applies (useful in shared NFS sudoers; on a single host usually `ALL`).

### 7.5 Runas specification
- The target user/group the command may run as (default root).

### 7.6 Command specification
- Exact paths, optional arguments, wildcards, or `ALL`.  
- Tags such as `NOPASSWD`, `SETENV`, `NOEXEC`, etc.

### 7.7 Timestamp / ticket
- After successful authentication, sudo caches approval for a configurable period (default often 5–15 minutes) so subsequent commands do not re-prompt.

### 7.8 Defaults
- Global options (`Defaults`) that control environment sanitisation, timeout, lecture, logging, etc.

## 8. What Linux Is Actually Doing

**Successful `sudo cmd` (simplified)**
```
User types: sudo systemctl restart nginx
    → shell executes the setuid-root sudo binary
    → sudo reads and parses policy
    → sudo determines whether the (user, host, command, runas) tuple is allowed
    → if authentication required:
          pam_authenticate() using service “sudo”
    → if policy + authentication succeed:
          log the event
          setuid/setgid/setgroups to the target identity
          optionally adjust environment (env_reset, etc.)
          exec the requested command
    → command runs with the new credentials
    → on completion, control returns to the original user shell
```

If policy denies the command, sudo exits with an error and logs the denial; no credential change occurs.

## 9. Commands and Tools

| Command | Purpose |
|---------|---------|
| `sudo -l` | List the commands the current user is allowed to run |
| `sudo -U alice -l` | List privileges of another user (requires appropriate rights) |
| `sudo -k` | Invalidate the timestamp ticket (force re-authentication next time) |
| `sudo -v` | Extend / validate the timestamp without running a command |
| `sudo -i` / `sudo -s` | Launch a login shell or ordinary shell as target user |
| `visudo` | Safely edit `/etc/sudoers` |
| `visudo -f /etc/sudoers.d/fragment` | Safely edit a drop-in file |
| `grep -r '' /etc/sudoers /etc/sudoers.d/` | Inspect policy (read-only) |
| `man sudoers` | Authoritative policy-language reference |
| `journalctl -t sudo` / `grep sudo /var/log/auth.log` | Examine sudo log entries |

## 10. Hands-On Lab

**Objective**  
Inspect the current sudo policy, exercise allowed and (if possible) denied commands, observe logging, and create a minimal safe drop-in rule.

**Setup**  
A user that already has some sudo rights (the default `sudo` / `wheel` group membership on most Ubuntu images). Work carefully; avoid locking yourself out.

```bash
mkdir -p ~/sudo-lab
cd ~/sudo-lab
```

**Steps**

1. Discover what you are allowed to do:
```bash
sudo -l
id
```

2. Run a harmless privileged command and observe the credential change:
```bash
sudo id
sudo whoami
sudo cat /etc/shadow | head -3
```

3. Examine the policy (read-only):
```bash
sudo grep -vE '^\s*#|^\s*$' /etc/sudoers
sudo ls -l /etc/sudoers.d/
sudo cat /etc/sudoers.d/* 2>/dev/null
```

4. Inspect the PAM stack used by sudo:
```bash
cat /etc/pam.d/sudo
```

5. Observe logging:
```bash
# Terminal 1
sudo journalctl -f -t sudo
# Terminal 2
sudo true
sudo -k          # invalidate ticket
sudo true        # will prompt again
```

6. Create a minimal drop-in that allows a specific harmless command without a password (example):
```bash
# Use visudo so syntax is checked
sudo visudo -f /etc/sudoers.d/lab-nopasswd
# Add a single line (adjust username):
# yourusername ALL=(root) NOPASSWD: /usr/bin/true
# Save and exit; visudo will validate
sudo -l
sudo /usr/bin/true          # should not prompt
```

7. Remove the lab drop-in:
```bash
sudo rm /etc/sudoers.d/lab-nopasswd
sudo -k
```

**Verification**  
You must be able to:

- Show the output of `sudo -l` and interpret at least one rule.  
- Demonstrate a successful privileged command and locate the corresponding log entry.  
- Create and later remove a drop-in rule with `visudo` without breaking sudo.

**Cleanup**  
Ensure any lab drop-in has been removed. Invalidate the timestamp if desired:
```bash
sudo -k
rm -rf ~/sudo-lab
```

## 11. Investigation Lab

**Scenario**  
A new administrator is added to the `sudo` group but still receives “user is not in the sudoers file” when trying to run sudo. `id` on a fresh login shows membership in the group.

**Objective**  
Determine why group membership is not yet effective for sudo and how to make it effective.

**Available tools**  
`id`, `groups`, `getent group sudo`, `sudo -l`, login session inspection, `/etc/nsswitch.conf`

**Initial clues**  
- The user was added with `usermod -aG sudo …`.  
- A brand-new SSH login still fails.  
- Or an existing long-lived session fails while a new login works.

**Investigation questions**  
1. When are supplementary group memberships fixed for a process?  
2. Why might a newly added group not appear in an already-running session?  
3. How do you confirm that the sudoers policy actually references the group?  
4. What is the cleanest way for the user to obtain a session that carries the new group?

Work the questions before reading the solution.

**Solution**  
Supplementary groups are set at login (or when a new session is explicitly created). An existing shell retains the group list it was started with.

```bash
id
getent group sudo
# If the group is missing from the current session:
# log out and log back in, or start a fresh login shell
su - $USER
# or simply reconnect via SSH
id                  # should now list sudo
sudo -l
```
Also verify that the policy contains a rule for `%sudo` (or the equivalent group). On some systems the group is named `wheel` instead of `sudo`.

## 12. Production Failure Scenario

**Incident**  
An automated change writes a sudoers drop-in with a syntax error. Shortly afterwards no ordinary administrator can obtain root via sudo. Console root login is still possible (or break-glass credentials exist).

**Systematic troubleshooting**

1. **Observation**  
   All `sudo` attempts fail with a parse / policy error message (or a generic denial).  

2. **Hypothesis**  
   The newest file under `/etc/sudoers.d/` contains invalid syntax; sudo refuses to use a broken policy.

3. **Evidence**  
   ```bash
   # From a still-privileged session or via console root
   ls -l /etc/sudoers.d/
   # visudo -c checks the entire policy
   visudo -c
   # Identify the offending fragment
   visudo -cf /etc/sudoers.d/<suspect>
   ```

4. **Resolution**  
   - Remove or fix the broken fragment (as root).  
   - Re-run `visudo -c` until it reports a clean policy.  
   - Test with an ordinary administrative account.  

5. **Prevention**  
   - Always deploy sudoers changes with `visudo` or with a configuration-management tool that validates syntax before activation.  
   - Keep a break-glass root path (console, cloud serial console, out-of-band) so a sudoers mistake is recoverable.  
   - Prefer small, reviewed drop-ins over large monolithic edits.

## 13. Connection to Previous Linux Knowledge

- sudo is a setuid-root binary; the setuid mechanism (Session 11) is what initially elevates it.  
- After policy approval it changes credentials using the same UID/GID facilities you studied in Sessions 10 and 14.  
- Authentication is delegated to PAM with service name `sudo` (Session 15).  
- Logging uses the same system logger you will later examine more deeply in the logging and observability sessions.  
- The policy ultimately decides whether a process running as an ordinary user may obtain the credentials that bypass ordinary discretionary permission checks.

## 14. Connection to Future Infrastructure

- **Configuration management / IaC**: sudoers fragments are almost always managed as code; syntax validation and least-privilege rules are part of secure pipeline design.  
- **SSH**: administrators typically log in as an unprivileged user and then use sudo; hardening guides therefore treat SSH and sudo as a single access path.  
- **Containers**: processes inside containers are often run as non-root; when host administration is required, sudo on the node (or a separate bastion) remains the control point.  
- **Kubernetes**: analogous concepts appear as RBAC, PodSecurity, and the occasional need for privileged node access; the least-privilege discipline learned with sudo transfers directly.  
- **AI / shared platforms**: operators of GPU clusters and training platforms use sudo (or equivalent) to manage drivers, mount datasets, and control device access; overly broad sudo rights on such hosts are a high-impact risk.  
- **Auditing and compliance**: sudo logs are a primary source for “who ran what privileged command when.”

## 15. Engineering Questions

1. Why is sudo preferred over sharing the root password or over making many binaries setuid-root?  
2. What is the function of `visudo` and why should it always be used?  
3. What does the rule `%sudo ALL=(ALL:ALL) ALL` permit?  
4. How does sudo decide whether to prompt for a password?  
5. What is the sudo timestamp and how do you invalidate it?  
6. Why can a user who was just added to the sudo group still be denied in an existing session?  
7. What is the danger of a NOPASSWD rule that permits a shell or an unrestricted interpreter?  
8. How does sudo’s authentication step relate to PAM?  
9. Why should sudoers drop-ins be kept small and reviewed?

## 16. Practical Assignment

1. Document the complete effective sudo policy for your lab user (`sudo -l` plus the relevant source lines).  

2. Design (but do not necessarily install) a least-privilege rule set that allows an “operator” user to:  
   - restart and inspect a specific service  
   - view the system journal  
   - and nothing else  

3. Produce a short “break-glass” note: how you would recover administrative access on a host whose sudoers policy has been rendered invalid.  

4. Examine the PAM configuration for sudo and note any differences from the login or SSH stacks; explain why they might differ.

## 17. Session Completion Test

Answer without notes.

**Conceptual**  
1. What problem does sudo solve that a shared root password does not?  
2. Why is the sudo binary installed setuid-root?

**System behavior**  
3. A user is added to the sudo group. Why might their existing terminal still be unable to use sudo?  
4. What happens when sudoers contains a syntax error?

**Command interpretation**  
5. What does `sudo -l` tell you?  
6. What is the purpose of `visudo`?

**Troubleshooting**  
7. All administrators lose sudo access after a configuration-management run. What is the first check you perform on the policy files?

**Internal**  
8. Describe the steps sudo takes from invocation until the target command begins execution.

**Explain in your own words**  
9. Explain the principle of least privilege as it applies to writing sudoers rules.

## 18. Mastery Criteria

- **Basic understanding**: You can use `sudo` for allowed commands, list your privileges with `sudo -l`, and understand the purpose of the tool.  
- **Working understanding**: You can read common sudoers rules, add a safe drop-in with `visudo`, invalidate the timestamp, and diagnose group-membership and simple policy problems.  
- **Strong understanding**: You can design least-privilege command lists, explain the full execution and authentication path, recover from a broken sudoers policy, and recognise dangerous anti-patterns.

## 19. What I Should Now Be Able to Explain

- Purpose and security model of sudo  
- High-level execution path (policy → PAM → credential change → exec)  
- Essential sudoers syntax (users, groups, hosts, Runas, commands, NOPASSWD)  
- Safe editing with `visudo` and drop-in files  
- Timestamp / ticket behaviour  
- Relationship between sudo and PAM  
- Logging of privileged commands  
- Common failure modes (syntax errors, missing group in session, overly broad rules)  
- Least-privilege practices for administrative access

## 20. Next Session

**Next Session Number**  
SESSION 17  

**Next Session Title**  
Linux Capabilities — Fine-Grained Privilege Beyond setuid and root  

**Why it comes next**  
You now understand both the classic setuid mechanism and the policy-controlled elevation provided by sudo. The next session introduces Linux capabilities—the kernel’s modern, fine-grained alternative to the all-or-nothing power of UID 0—allowing individual privileges (bind to privileged ports, load kernel modules, etc.) to be granted without full root access.
