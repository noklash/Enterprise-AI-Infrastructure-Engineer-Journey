# Session 15 — Pluggable Authentication Modules (PAM) and the Authentication Path

## 1. Position in the Curriculum

**Phase**  
PHASE 1 — Linux Systems Engineering

**Module**  
MODULE 2 — Permissions and Security

**Session**  
SESSION 15 — Pluggable Authentication Modules (PAM) and the Authentication Path

**Prerequisites**  
- Users, groups, UID/GID databases, and NSS (Session 14)  
- Process credentials (real / effective UID and GID)  
- Classic permissions and the fact that the kernel only understands numbers  
- Basic service management awareness (systemd units that run as a User=)

**What this session unlocks**  
Understanding of how a login program, SSH daemon, sudo, or any other privileged service decides that a claimant is entitled to a particular identity, and how that decision is modularised through PAM. This is required before studying SSH hardening, sudo policy, account lockout, password quality, and centralised authentication.

## 2. Why This Session Exists

You now know how identities are stored (`/etc/passwd`, `/etc/shadow`, `/etc/group`) and how names are resolved to numbers (NSS).  

The missing piece is **authentication**: the act of verifying that the entity requesting an identity is entitled to it.  

Linux does not hard-code one authentication method into every program. Instead, programs such as

- `/bin/login`
- `sshd`
- `sudo`
- `su`
- graphical display managers
- many custom services

call into a shared framework called **Pluggable Authentication Modules (PAM)**. PAM configuration decides, for each service, which modules are invoked, in which order, and what happens if a module succeeds or fails.

Without understanding PAM you cannot:

- explain why a password is accepted or rejected  
- add multi-factor authentication  
- implement account lockout or password-quality rules  
- diagnose “authentication failed” messages that do not come from the application itself  
- safely integrate centralised identity systems  

This session builds that understanding from first principles.

## 3. Learning Objectives

By the end of this session you will be able to:

- Explain the role of PAM as an intermediary between applications and authentication mechanisms.  
- Describe the four classic PAM management groups: account, auth, password, and session.  
- Read a PAM service configuration file and interpret control flags (`required`, `requisite`, `sufficient`, `optional`).  
- Trace the high-level path of a password login from the application through PAM to the identity databases.  
- Locate the PAM configuration for common services (`login`, `sshd`, `sudo`, `su`, `passwd`).  
- Use `pam_authenticate` concepts and diagnostic tools (`debug` arguments, journal logs, `pamtester` if available) to understand success and failure.  
- Predict the effect of adding or reordering modules for password quality, account lockout, or additional factors.  
- Relate PAM’s final result to the establishment of process credentials (UID/GID) that you already understand.

## 4. Prerequisite Concepts

You already know:

- How a username is turned into a UID via NSS.  
- That `/etc/shadow` holds password hashes.  
- That a process ultimately runs with numeric credentials.  
- That permission checks use those numeric credentials.

## 5. Mental Model

```
Application (sshd, login, sudo, …)
        │
        │  pam_start / pam_authenticate / pam_acct_mgmt / …
        ▼
┌─────────────────────────────────────────────┐
│                 PAM library                  │
│  reads /etc/pam.d/<service>                  │
│  stacks modules according to control flags   │
└──────────────────────┬──────────────────────┘
                       │
        ┌──────────────┼──────────────┐
        ▼              ▼              ▼
   pam_unix.so    pam_ldap.so    pam_faillock.so …
   (local hash)   (central)      (lockout) …
        │
        ▼
   /etc/shadow, NSS, network identity, hardware token, …
```

The application never talks directly to `/etc/shadow` or to an LDAP server; it talks only to PAM. PAM’s configuration decides which modules run and how their results are combined.

## 6. Core Concept

### Why PAM exists

Before PAM, every application that needed authentication contained its own hard-coded logic (read the shadow file, call a network service, etc.). Changing the authentication policy required modifying and recompiling each application.  

PAM turns authentication into a **configuration problem**:

- Applications are written to a standard PAM API.  
- Administrators (or packages) supply per-service policy files that list modules and control flags.  
- Modules are ordinary shared libraries that implement specific checks.

### The four management groups

Each PAM service file contains lines belonging to one of four groups:

| Group     | Purpose |
|-----------|---------|
| `auth`    | Establish identity (prompt for password, check token, etc.) |
| `account` | Check whether the account is allowed to be used right now (expired, locked, time-of-day, …) |
| `password`| Update authentication tokens (change password, etc.) |
| `session` | Perform actions before/after the session (mount home, open audit session, set limits, …) |

A complete login typically walks through `auth` → `account` → `session`. Password changes use the `password` group.

### Control flags

Each module line has a control flag that decides how its success or failure affects the overall result:

| Flag        | Meaning (simplified) |
|-------------|----------------------|
| `required`  | Must succeed; failure is recorded but later modules still run; overall failure at the end |
| `requisite` | Must succeed; failure causes immediate overall failure (no further modules in the group) |
| `sufficient`| If it succeeds and no prior `required` has failed, overall success is returned immediately |
| `optional`  | Result is ignored unless it is the only module in the stack |

There is also the more precise `[value=action …]` syntax used by modern configurations; the classic four flags remain the most important to understand first.

### Common modules (illustrative)

- `pam_unix.so` — traditional local password authentication against `/etc/shadow`  
- `pam_permit.so` / `pam_deny.so` — always succeed / always fail  
- `pam_nologin.so` — deny non-root logins when `/var/run/nologin` exists  
- `pam_faillock.so` / `pam_tally2.so` — account lockout after repeated failures  
- `pam_pwquality.so` — password complexity rules  
- `pam_ldap.so`, `pam_sss.so` — centralised identity  
- `pam_limits.so` — apply resource limits from `/etc/security/limits.conf`  
- `pam_systemd.so` — register the session with systemd logind  

### Per-service configuration

Files live in `/etc/pam.d/`. Important examples:

- `/etc/pam.d/login` — console login  
- `/etc/pam.d/sshd` — OpenSSH  
- `/etc/pam.d/sudo` — sudo  
- `/etc/pam.d/su` — su  
- `/etc/pam.d/passwd` — password changes  
- `/etc/pam.d/common-*` (Debian/Ubuntu) or `/etc/pam.d/system-*` (some other distributions) — shared sub-stacks included by the service files  

Include directives (`@include common-auth`) keep policy consistent across services.

## 7. Break It Into the Smallest Important Pieces

### 7.1 PAM-aware application
- Calls the PAM API (`pam_start`, `pam_authenticate`, `pam_acct_mgmt`, `pam_open_session`, …).  
- Supplies a service name that selects the configuration file.

### 7.2 Service configuration file
- Located in `/etc/pam.d/<service>`.  
- Lists modules, arguments, and control flags for each management group.

### 7.3 Module
- Shared library (`/lib/*/security/pam_*.so`) that implements one concrete check or action.

### 7.4 Control flag
- Determines how the module’s success/failure contributes to the final result of that management group.

### 7.5 Conversation function
- Callback supplied by the application so modules can prompt the user (for a password, a token code, etc.) without hard-coding UI logic.

### 7.6 Return to the application
- PAM returns success or failure.  
- On success the application proceeds to set credentials (or to open a session); on failure it refuses the request.

### 7.7 Relationship to NSS and shadow
- Modules such as `pam_unix` perform the actual hash comparison using NSS and `/etc/shadow`.  
- PAM itself does not replace NSS; it orchestrates modules that may use NSS.

## 8. What Linux Is Actually Doing

**Simplified password login via sshd**
```
sshd receives connection and username
    → pam_start("sshd", username, …)
    → pam_authenticate()
          PAM reads /etc/pam.d/sshd
          executes auth stack:
              pam_unix.so  → reads shadow hash via NSS, compares password
              (other modules as configured)
          returns success / failure
    → pam_acct_mgmt()
          account stack (expired? locked? …)
    → on full success:
          pam_open_session()
          establish process credentials (setuid/setgid/…)
          launch user shell or forced command
    → on failure:
          disconnect / show “Permission denied”
```

The same pattern appears, with different service names and stacks, for console login, sudo, su, and many other programs.

## 9. Commands and Tools

| Command / Path | Purpose |
|----------------|---------|
| `ls /etc/pam.d/` | List available service configurations |
| `cat /etc/pam.d/sshd` | Read the SSH PAM stack |
| `cat /etc/pam.d/common-auth` | Shared auth stack on Debian/Ubuntu |
| `man pam.d` / `man pam` | Reference for syntax and flags |
| `man pam_unix` | Documentation for a specific module |
| `journalctl -u ssh` / `auth.log` | Authentication-related log messages |
| `pamtester` (if installed) | Command-line tool to exercise a PAM service |
| `debug` argument on a module line | Increases logging for that module |

On Ubuntu/Debian the `common-*` files are the ones most often edited (or managed by `pam-auth-update`). On other distributions the corresponding shared files differ; always inspect what the service file actually includes.

## 10. Hands-On Lab

**Objective**  
Inspect the PAM configuration of common services, observe the control-flag language, and perform a controlled authentication test.

**Setup**  
Ubuntu VirtualBox VM with SSH enabled is ideal; local console login also works. No permanent policy changes are required for the basic lab.

```bash
mkdir -p ~/pam-lab
cd ~/pam-lab
```

**Steps**

1. List and examine core service files:
```bash
ls /etc/pam.d/
head -30 /etc/pam.d/sshd
head -30 /etc/pam.d/login
head -30 /etc/pam.d/sudo
head -30 /etc/pam.d/su
head -30 /etc/pam.d/passwd
```

2. Follow the include chain (Debian/Ubuntu style):
```bash
grep -E 'auth|account|password|session|include' /etc/pam.d/sshd
cat /etc/pam.d/common-auth
cat /etc/pam.d/common-account
cat /etc/pam.d/common-session
cat /etc/pam.d/common-password
```

3. Identify the module that performs traditional password checking:
```bash
grep -r pam_unix /etc/pam.d/
```

4. Observe a successful and a failed authentication in the logs (run from another terminal or from the hypervisor console if needed):
```bash
# Terminal 1
sudo journalctl -f -u ssh
# or on some systems:
# sudo tail -f /var/log/auth.log

# Terminal 2 – attempt a login with a wrong password, then a correct one
ssh localhost
```

5. (Optional) Install and use `pamtester` for a non-SSH test:
```bash
sudo apt install pamtester
pamtester login $USER authenticate
# Enter correct and incorrect passwords and observe the result
```

6. Read the documentation for one module:
```bash
man pam_unix
man pam_limits
```

**Verification**  
You must be able to:

- Point to the file that defines the auth stack for SSH.  
- Identify at least one `required` or `requisite` module and one `sufficient` or `optional` module.  
- Show a log line generated by a failed authentication attempt.  
- Explain what `pam_unix.so` does in the stack you examined.

**Cleanup**  
No permanent changes should have been made. Remove the lab directory if desired:
```bash
rm -rf ~/pam-lab
```

## 11. Investigation Lab

**Scenario**  
Users report that SSH logins with correct passwords suddenly fail. Console login still works. The only recent change was an automated security hardening run.

**Objective**  
Locate the PAM-level cause of the SSH-only failure.

**Available tools**  
`/etc/pam.d/sshd`, `/etc/pam.d/common-*`, `journalctl`, `sshd -T`, package history, `pamtester`

**Initial clues**  
- Password is known to be correct (works on console).  
- SSH returns “Permission denied” (or similar) immediately after password entry.  
- Hardening scripts often add modules such as `pam_faillock`, `pam_access`, or stricter `pam_unix` options.

**Investigation questions**  
1. Why can console login succeed while SSH fails if both ultimately use password authentication?  
2. Which configuration file is specific to SSH?  
3. How do you see the exact PAM failure reason?  
4. What control-flag mistakes can turn a non-fatal module failure into a total authentication failure?

Work the questions before reading the solution.

**Solution**  
SSH uses `/etc/pam.d/sshd` (which usually includes the common stacks but may also contain extra modules). Console login uses `/etc/pam.d/login`. A hardening change that added a failing `required` or `requisite` module only to the SSH stack (or that altered an include used only by SSH) produces exactly this symptom.

```bash
# Compare stacks
diff -u /etc/pam.d/login /etc/pam.d/sshd
# Examine recent changes and logs
journalctl -u ssh --since "1 hour ago"
grep -r faillock /etc/pam.d/
```
Typical culprits: a misconfigured `pam_access.so` deny rule, a `pam_faillock` state that has locked the account for SSH only, or a module listed as `requisite` that cannot open a required file. Revert or correct the offending line, then verify with a successful SSH login.

## 12. Production Failure Scenario

**Incident**  
After enabling a password-quality module (`pam_pwquality`), users can no longer change their passwords with `passwd`. Existing logins continue to work. The error message is generic (“Authentication token manipulation error”).

**Systematic troubleshooting**

1. **Observation**  
   `passwd` fails; login still succeeds.

2. **Hypothesis**  
   The `password` stack is rejecting the new password, or the module is misconfigured and returns failure even for valid passwords.

3. **Evidence**  
   ```bash
   cat /etc/pam.d/passwd
   cat /etc/pam.d/common-password
   # Attempt a password change while watching logs
   sudo journalctl -f
   passwd
   # Check module options (minlen, credit values, dictionary path, etc.)
   man pam_pwquality
   ```

4. **Confirmation**  
   The module is `required` or `requisite` and is rejecting passwords that do not meet newly imposed complexity rules, or it cannot read its dictionary/configuration file.

5. **Resolution**  
   - Adjust the quality options to the organisation’s intended policy, or  
   - Temporarily relax the control flag while communicating the new requirements, then re-tighten.  
   - Ensure any required dictionary files exist and are readable.  

6. **Prevention**  
   Test password-policy changes on a non-production account first; document the exact rules users must satisfy; monitor authentication error rates after the change.

## 13. Connection to Previous Linux Knowledge

- PAM’s successful authentication is the usual precursor to setting the numeric UID/GID credentials you studied in Session 14.  
- Modules such as `pam_unix` read the same `/etc/shadow` hashes and use the same NSS path.  
- Once the session is open, all permission checks (Sessions 10–13) use the credentials that the login program established after PAM returned success.  
- `sudo` and `su` are themselves PAM-aware applications; their policies are expressed both in their own configuration and in their PAM stacks.

## 14. Connection to Future Infrastructure

- **SSH hardening**: almost every advanced SSH authentication feature (public key + password, keyboard-interactive, forced commands, etc.) interacts with or bypasses parts of the PAM stack.  
- **sudo / polkit**: policy decisions are often made after PAM has established identity.  
- **Centralised authentication**: SSSD, FreeIPA, Active Directory integrations appear as additional PAM modules (and NSS modules).  
- **Containers and Kubernetes**: admission of users into nodes or the use of host users inside pods can involve PAM on the node; many cloud images ship with opinionated PAM configurations.  
- **Multi-factor authentication**: hardware tokens, TOTP, and push-based second factors are almost always implemented as additional PAM modules in the `auth` stack.  
- **Compliance and auditing**: password quality, lockout, and session recording requirements are enforced through PAM configuration and the modules it loads.

## 15. Engineering Questions

1. Why was PAM introduced instead of letting each application implement its own authentication?  
2. What are the four PAM management groups and what is each responsible for?  
3. What is the difference between a `required` module and a `requisite` module?  
4. How does an application select which PAM configuration file is used?  
5. Why can console login and SSH behave differently even when both ask for a password?  
6. What role does `pam_unix.so` typically play?  
7. How does PAM relate to the Name Service Switch and `/etc/shadow`?  
8. What happens after PAM returns success to a login program?  
9. Why is editing PAM configuration files considered high-risk?

## 16. Practical Assignment

1. Draw (text or diagram) the complete path of a successful SSH password login from the moment the password is typed until a shell process is running with the correct UID. Label the PAM management groups involved.  

2. For the services `sshd`, `sudo`, and `passwd` on your system, list:  
   - the primary PAM configuration file  
   - whether it includes common/system stacks  
   - the module that ultimately verifies a local password  

3. Intentionally introduce a non-destructive diagnostic change (add a `debug` argument to a module, or use `pamtester`) and capture the additional log output produced by a failed authentication. Restore the original configuration.  

4. Write a short “safe PAM change” checklist that you would follow before modifying a production authentication stack.

## 17. Session Completion Test

Answer without notes.

**Conceptual**  
1. What problem does PAM solve?  
2. Name the four PAM management groups and give one example of what each does.

**System behavior**  
3. A `requisite` module in the auth stack fails. Do later modules in the same stack still run?  
4. Why might SSH reject a password that console login accepts?

**Command interpretation**  
5. What does a line such as `auth required pam_unix.so` mean?  
6. Where do you look for the PAM policy that governs `sudo`?

**Troubleshooting**  
7. Users cannot change passwords after a policy update, but they can still log in. Which PAM management group do you examine first?

**Internal**  
8. Describe the high-level sequence of PAM calls a typical login program makes.

**Explain in your own words**  
9. Explain why authentication policy can be changed without recompiling sshd or login.

## 18. Mastery Criteria

- **Basic understanding**: You know that PAM sits between applications and authentication mechanisms and can locate the configuration files for common services.  
- **Working understanding**: You can read a PAM stack, interpret the main control flags, follow include directives, and diagnose simple authentication failures from logs and configuration.  
- **Strong understanding**: You can design a minimal safe change to a PAM stack (e.g. adding lockout or password quality), predict its effect on different services, and relate PAM success to the establishment of process credentials.

## 19. What I Should Now Be Able to Explain

- Purpose and architecture of PAM  
- Four management groups (auth, account, password, session)  
- Meaning of the principal control flags  
- Per-service configuration under `/etc/pam.d/`  
- Role of common/shared include files  
- How `pam_unix` ties PAM to the shadow/NSS identity data  
- High-level authentication path for SSH and console login  
- Why different services can enforce different authentication policies  
- Relationship between PAM success and the numeric credentials used for permission checks

## 20. Next Session

**Next Session Number**  
SESSION 16  

**Next Session Title**  
sudo, Privilege Escalation, and Controlled Administrative Access  

**Why it comes next**  
You now understand how identity is established (NSS + PAM) and how ordinary permission checks work. The next session examines the primary controlled privilege-escalation tool used on Linux systems—sudo—including its policy language, the relationship to PAM, logging, and the operational and security practices that surround administrative access.
