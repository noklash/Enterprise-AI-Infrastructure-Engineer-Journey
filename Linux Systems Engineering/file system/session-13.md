# Session 13 — Access Control Lists (ACLs) — Extending the Owner/Group/Other Model

## 1. Position in the Curriculum

**Phase**  
PHASE 1 — Linux Systems Engineering

**Module**  
MODULE 2 — Permissions and Security

**Session**  
SESSION 13 — Access Control Lists (ACLs) — Extending the Owner/Group/Other Model

**Prerequisites**  
- Classic owner/group/other permissions and mode bits (Session 10)  
- setuid, setgid, sticky bit (Session 11)  
- umask and file-creation semantics (Session 12)  
- Inodes, ownership, and path-resolution checks

**What this session unlocks**  
The ability to grant (or deny) permissions to specific users and groups beyond the single owner and single group stored in the inode. This is required for real-world shared directories, multi-team data sets, and many container/Kubernetes volume permission designs.

## 2. Why This Session Exists

The classic Unix model gives every file exactly three permission identities:

- one user owner  
- one group owner  
- everyone else  

That model is simple and efficient, but it is often insufficient:

- A directory must be writable by three specific developers who do not share a common group.  
- A data set must be readable by a service account and by a human analyst group, while remaining closed to others.  
- A sticky shared area needs finer control than “owner / group / other.”  

**Access Control Lists (ACLs)** extend the model by attaching an ordered list of additional entries to an inode. Each entry names a user or group and the permissions granted to it.  

Modern Linux filesystems (ext4, XFS, Btrfs, etc.) implement the POSIX ACL model. Once you understand ACLs you can solve permission problems that are awkward or impossible with the classic bits alone, and you will recognise ACL-related behaviour in container volume mounts and network filesystems.

## 3. Learning Objectives

By the end of this session you will be able to:

- Explain why the classic owner/group/other model is sometimes insufficient.  
- Describe the structure of a POSIX ACL (user, group, mask, other entries).  
- Use `getfacl` and `setfacl` to inspect and modify ACLs.  
- Interpret the interaction between the ACL mask and the group class permissions.  
- Create default ACLs on a directory so that newly created objects inherit useful permissions.  
- Predict the effective rights a given user will have when both classic bits and ACL entries are present.  
- Recognise the `+` indicator in `ls -l` output that signals the presence of an ACL.  
- Diagnose and remove unexpected ACLs that cause “Permission denied” or overly permissive access.

## 4. Prerequisite Concepts

You already know:

- How the kernel chooses among owner, group, and other bits.  
- That mode bits and ownership live in the inode.  
- How umask shapes the initial mode of new objects.  
- That directory permissions control traversal and creation.

## 5. Mental Model

```
Classic view (always present)
  owner  rwx
  group  rwx
  other  rwx

ACL view (optional extension)
  user::rwx          ← same as classic owner
  user:alice:r-x     ← extra named user
  group::r-x         ← same as classic group
  group:devs:rwx     ← extra named group
  mask::rwx          ← upper bound on named entries + group
  other::r--         ← same as classic other

Default ACL (on a directory)
  default:user::rwx
  default:group:devs:rwx
  …                  ← inherited by new children
```

When an ACL is present, the classic `ls -l` group permission bits become the **mask** (or are constrained by it), which is why you sometimes see group bits change after adding an ACL entry.

## 6. Core Concept

### POSIX ACL entries

A POSIX ACL is a list of entries of the following forms:

| Entry type   | Form              | Meaning |
|--------------|-------------------|---------|
| Owner        | `user::`          | Permissions for the file owner |
| Named user   | `user:name:`      | Permissions for a specific UID |
| Owning group | `group::`         | Permissions for the file’s group |
| Named group  | `group:name:`     | Permissions for a specific GID |
| Mask         | `mask::`          | Maximum rights allowed for named users, named groups, and the owning group |
| Other        | `other::`         | Permissions for everyone else |

The **mask** is the critical extra concept. It acts as a ceiling: the effective rights of any named user, named group, or the owning group are the intersection of their own entry and the mask.

### Access ACL vs Default ACL

- **Access ACL** — attached to a file or directory; used for permission checks on that object.  
- **Default ACL** — attached only to a directory; automatically copied to new children as their access ACL (and, for subdirectories, as their default ACL as well).  

Default ACLs are the mechanism that makes shared project directories usable without constantly running `setfacl` after every creation.

### Relationship to the classic mode bits

When an ACL is set:

- The classic owner bits stay in sync with `user::`.  
- The classic other bits stay in sync with `other::`.  
- The classic group bits are set to the value of `mask::` (or are limited by it).  

That is why `ls -l` may show different group permissions after you add a named-user entry—the group field is reflecting the mask.

### When the kernel uses ACLs

On every permission check the kernel:

1. Looks for an ACL on the inode.  
2. If none exists, falls back to the classic owner/group/other algorithm.  
3. If an ACL exists, evaluates the matching entry (owner, named user, owning group / named group, other) and applies the mask where required.

## 7. Break It Into the Smallest Important Pieces

### 7.1 Access ACL
- The list consulted for permission checks on the object itself.

### 7.2 Default ACL
- Stored on a directory; supplies the initial access ACL (and default ACL) of newly created children.

### 7.3 Mask entry
- Upper bound on the effective rights of named users, named groups, and the owning group.  
- Visible in the classic group permission bits when an ACL is present.

### 7.4 Effective rights
- For any entry subject to the mask:  
  `effective = entry_permissions & mask_permissions`

### 7.5 Inheritance rules
- New file → receives the parent’s default ACL as its access ACL (minus directory-only bits).  
- New subdirectory → receives the parent’s default ACL as both its access ACL and its default ACL.

### 7.6 The `+` indicator
- `ls -l` appends a `+` to the mode string when an ACL (other than the minimal three-entry ACL) is present.

### 7.7 Extended attributes
- On most filesystems ACLs are stored as extended attributes (xattrs).  
- Tools such as `getfattr` can show the raw attribute; `getfacl`/`setfacl` are the friendly interface.

## 8. What Linux Is Actually Doing

**Permission check with ACL (simplified)**
```
access(path, mode)
    → load inode
    → if inode has ACL:
          select the most specific matching entry
            (owner → named user → group/named group → other)
          if the entry is subject to the mask:
              rights &= mask
          grant or deny according to the resulting rights
    → else:
          classic owner / group / other algorithm
```

**Creation of a new object under a directory with a default ACL**
```
open/mkdir inside directory
    → allocate new inode
    → if directory has a default ACL:
          copy default ACL → new object’s access ACL
          (and → new object’s default ACL if it is a directory)
    → apply umask to the mode bits that correspond to user:: / mask / other
    → continue with ownership and directory-entry creation
```

## 9. Commands and Tools

| Command | Purpose |
|---------|---------|
| `getfacl path` | Display the ACL of a file or directory |
| `setfacl -m u:alice:rwx path` | Add or modify a named-user entry |
| `setfacl -m g:devs:rx path` | Add or modify a named-group entry |
| `setfacl -x u:alice path` | Remove a named-user entry |
| `setfacl -b path` | Remove all extended ACL entries (return to classic only) |
| `setfacl -d -m g:devs:rwx dir` | Set a default ACL entry on a directory |
| `setfacl -R …` | Recursive application (use with care) |
| `ls -l` | `+` at the end of the mode indicates an ACL |
| `getfattr -d path` | Show raw extended attributes (including ACL storage) |

Always prefer `getfacl`/`setfacl` over manually editing extended attributes.

## 10. Hands-On Lab

**Objective**  
Create a shared directory that grants specific rights to a named user (or group) via ACLs, set a default ACL so new files inherit those rights, and observe the mask and the `+` indicator.

**Setup**  
You need a second identity to test against. On a personal VM the simplest approach is to use `sudo` to create a temporary user, or to test with a group you belong to. The commands below assume you will substitute a real secondary user or group name.

```bash
mkdir -p ~/acl-lab
cd ~/acl-lab
```

**Steps**

1. Create a directory and examine its initial ACL (minimal):
```bash
mkdir share
getfacl share
ls -ld share
```

2. Add a named-user (or named-group) entry:
```bash
# Replace alice with an actual username on your system, or use a group:
setfacl -m u:alice:rwx share
# or
setfacl -m g:$(id -gn):rwx share
getfacl share
ls -ld share          # note the ‘+’
```

3. Observe the mask:
```bash
getfacl share
# Notice the mask:: entry and how it relates to the group bits shown by ls
```

4. Set a default ACL so children inherit rights:
```bash
setfacl -d -m u:alice:rwx share
# or the group equivalent
getfacl share
```

5. Create a new file and a new subdirectory and inspect their ACLs:
```bash
touch share/newfile
mkdir share/newdir
getfacl share/newfile
getfacl share/newdir
ls -l share
```

6. Demonstrate effective rights and the mask (optional but illuminating):
```bash
setfacl -m m::r share/newfile     # tighten the mask
getfacl share/newfile             # named entries now show #effective:
```

7. Remove the ACL and confirm return to classic behaviour:
```bash
setfacl -b share/newfile
getfacl share/newfile
ls -l share/newfile               # ‘+’ should disappear
```

**Verification**  
You must be able to:

- Show an ACL containing a named user or group entry.  
- Show that a newly created file inherited a default ACL.  
- Recognise the `+` in `ls -l` and the `mask::` / `#effective:` lines in `getfacl` output.

**Cleanup**
```bash
rm -rf ~/acl-lab
```

## 11. Investigation Lab

**Scenario**  
A user cannot write to a directory even though `ls -l` shows `drwxrwxr-x` and the user is a member of the owning group. `getfacl` reveals several named entries and a restrictive mask.

**Objective**  
Determine the effective rights of the user and fix the ACL so that group members obtain the intended write access.

**Available tools**  
`getfacl`, `setfacl`, `id`, `groups`, `ls -ld`, `namei`

**Initial clues**  
- Classic group bits look permissive.  
- The user is in the owning group.  
- An ACL is present (the `+` indicator).  
- `getfacl` shows a mask that does not include write.

**Investigation questions**  
1. What is the relationship between the classic group bits and the ACL mask when an ACL is present?  
2. How do you calculate the effective rights of the owning group?  
3. Which `setfacl` command widens the mask without destroying named entries?  
4. How do you verify the fix from the affected user’s identity?

Work the questions before reading the solution.

**Solution**  
When an ACL is present the classic group permission bits reflect the mask. If the mask lacks write, the owning group (and all named entries) lose write even if their own entries grant it.

```bash
getfacl directory
# Look for mask:: and the #effective: comments
setfacl -m m::rwx directory      # or the precise rights you intend
getfacl directory
# Re-test as the affected user
```
Effective rights are always `entry & mask`. Raising the mask restores the intended access.

## 12. Production Failure Scenario

**Incident**  
After a migration to a new NFS-backed volume, users report that newly created files in a shared project directory are no longer group-writable by the project team. On the old server a default ACL had been set; the new volume was mounted without ACL support (or the default ACL was never re-applied).

**Systematic troubleshooting**

1. **Observation**  
   New files have mode 644 (or 664 without the expected group write) and `getfacl` shows no default ACL.

2. **Hypothesis**  
   Either the filesystem is mounted without ACL support, or the default ACL was lost in the migration.

3. **Evidence**  
   ```bash
   mount | grep <volume>
   # look for noacl or absence of acl
   getfacl /shared/project
   touch /shared/project/testfile
   getfacl /shared/project/testfile
   ls -l /shared/project/testfile
   ```

4. **Resolution**  
   - Remount with ACL support if required (`acl` mount option; most modern filesystems enable it by default).  
   - Re-apply the default ACL:  
     ```bash
     setfacl -d -m g:projectgroup:rwx /shared/project
     setfacl -m g:projectgroup:rwx /shared/project
     ```  
   - Recursively fix existing objects if necessary (`setfacl -R …`).  
   - Confirm inheritance with a new test file.

5. **Prevention**  
   Capture ACL settings in configuration management; include ACL checks in migration runbooks; verify `getfacl` output after any storage migration.

## 13. Connection to Previous Linux Knowledge

- ACLs are an extension of the same inode metadata that holds the classic mode bits and ownership (Sessions 10–12).  
- The permission-check algorithm you learned earlier is replaced (when an ACL is present) by a richer evaluation that still ends in a grant or deny for the requesting process credentials.  
- umask still applies at creation time; it interacts with the mode bits that correspond to `user::`, `mask::`, and `other::`.  
- Default ACLs are the ACL analogue of the setgid-directory group-inheritance rule: both exist to make shared directories usable without constant manual intervention.

## 14. Connection to Future Infrastructure

- **Containers**: bind-mounted host directories often carry ACLs; user-namespace mappings can make named-user entries refer to unexpected UIDs inside the container.  
- **Kubernetes**: some CSI drivers and NFS volumes preserve ACLs; others strip them. fsGroup and supplementalGroups interact with both classic bits and ACLs.  
- **NFS / network filesystems**: ACL support depends on protocol version and server capabilities; loss of ACLs on migration is a classic incident.  
- **Shared AI/ML data sets**: multi-team feature stores and training corpora frequently rely on default ACLs (or equivalent richer models) so that new files remain accessible to the correct set of service accounts and human groups.  
- **Security audits**: scanners often flag overly broad ACLs or unexpected named-user entries; understanding the mask is required to interpret the findings.

## 15. Engineering Questions

1. What problem do ACLs solve that the classic owner/group/other model cannot solve cleanly?  
2. What is the purpose of the ACL mask entry?  
3. How do the classic group permission bits relate to the ACL when an ACL is present?  
4. What is the difference between an access ACL and a default ACL?  
5. How does a newly created file obtain ACL entries when its parent directory has a default ACL?  
6. Why does `ls -l` sometimes show a `+` at the end of the mode string?  
7. If a named-user entry grants `rwx` but the mask is `r--`, what are the effective rights of that user?  
8. How do you remove all extended ACL entries from a file and return it to pure classic permissions?  
9. Why must default ACLs be set on a directory before children are created in order to affect those children?

## 16. Practical Assignment

1. Create a project directory that meets the following requirements using ACLs (and classic bits as needed):  
   - Owner (you) has full control.  
   - A specific secondary user or group has read/write/execute.  
   - Others have no access.  
   - Every newly created file and subdirectory automatically receives the same rights for the secondary identity.  

2. Document the exact `setfacl` commands, the resulting `getfacl` output, and a test that proves inheritance works.  

3. Intentionally set a restrictive mask, show the `#effective:` rights, then restore the intended access.  

4. Write a short migration checklist item: “How I would verify that ACLs survived a filesystem move or NFS remount.”

## 17. Session Completion Test

Answer without notes.

**Conceptual**  
1. What is a POSIX ACL and what additional identities can it grant rights to?  
2. What is the ACL mask and why does it exist?

**System behavior**  
3. A directory has a default ACL. What ACL does a newly created file inside it receive?  
4. A named-user entry grants `rwx` but the mask is `r--`. What can that user actually do?

**Command interpretation**  
5. `ls -l` shows `drwxrwxr-x+`. What does the `+` mean?  
6. What does `getfacl` display that `ls -l` does not?

**Troubleshooting**  
7. Group members cannot write to a directory even though the classic group bits appear to allow write. An ACL is present. What do you inspect next?

**Internal**  
8. Describe how the kernel decides whether to grant access when an ACL is present on the inode.

**Explain in your own words**  
9. Explain why default ACLs are necessary for practical shared directories.

## 18. Mastery Criteria

- **Basic understanding**: You can read `getfacl` output, recognise the `+` indicator, and add a named-user or named-group entry.  
- **Working understanding**: You can set default ACLs, interpret the mask and effective rights, and fix common ACL-related access failures.  
- **Strong understanding**: You can design a complete shared-directory permission scheme using ACLs, predict inheritance behaviour, and diagnose ACL loss or mask problems after migrations or mount changes.

## 19. What I Should Now Be Able to Explain

- Limitation of the classic owner/group/other model  
- Structure of a POSIX ACL (user, group, mask, other)  
- Access ACL versus default ACL  
- Role of the mask and the meaning of effective rights  
- How new objects inherit ACLs from a parent directory  
- Meaning of the `+` in `ls -l`  
- Use of `getfacl` and `setfacl` for inspection and modification  
- Interaction of ACLs with umask and classic mode bits  
- Common operational failure modes (restrictive mask, lost default ACL, missing ACL support on mount)

## 20. Next Session

**Next Session Number**  
SESSION 14  

**Next Session Title**  
Users, Groups, UID/GID Databases, and the Identity Lookup Path  

**Why it comes next**  
You now understand how permissions and ACLs attach rights to numeric UIDs and GIDs. The next session examines how those numeric identities are created, stored, and resolved to names—i.e. `/etc/passwd`, `/etc/group`, `/etc/shadow`, the Name Service Switch (NSS), and the lookup path that turns “alice” into UID 1001—completing the identity side of the access-control model.
