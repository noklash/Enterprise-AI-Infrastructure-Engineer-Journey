# Session 06 — ext4 Fundamentals: On-Disk Layout, Inodes, and Allocation

## 1. Position in the Curriculum

**Phase**  
PHASE 1 — Linux Systems Engineering

**Module**  
MODULE 1 — Filesystems

**Session**  
SESSION 06 — ext4 Fundamentals: On-Disk Layout, Inodes, and Allocation

**Prerequisites**  
- Inodes, directory entries, path resolution (Session 01)  
- File descriptors and open file descriptions (Session 02)  
- Hard links, symbolic links, deletion semantics (Session 03)  
- VFS and mount points (Session 04)  
- Block devices and the path from filename to storage (Session 05)

**What this session unlocks**  
A concrete understanding of how one real filesystem (ext4) stores inodes, maps file offsets to block-device blocks, and manages free space. This is the foundation for understanding filesystem consistency, recovery, performance characteristics, and the behavior of tools such as `fsck`, `tune2fs`, and `debugfs`.

## 2. Why This Session Exists

You now know that a filesystem sits on a block device and is mounted into the VFS directory tree. Up to this point “the filesystem” has remained an abstraction that owns inodes and data blocks.  

The next necessary step is to examine a concrete, widely deployed implementation—**ext4**—so that the abstract concepts become tangible on-disk structures.  

Without this knowledge you cannot reason about:

- why a filesystem can run out of inodes while still having free data blocks  
- how a file’s data is located from the information stored in its inode  
- what `fsck` is actually checking and repairing  
- why certain allocation patterns produce fragmentation or poor performance  
- how features such as extents change the mapping from file offset to device blocks  

ext4 is still the default or common choice on a large fraction of Linux servers and cloud images; understanding it yields transferable insight into other filesystems.

## 3. Learning Objectives

By the end of this session you will be able to:

- Describe the major on-disk regions of an ext4 filesystem (superblock, block group descriptors, inode tables, data blocks).  
- Explain how an inode number is converted into the location of the inode structure on disk.  
- Show how a regular file’s data is located via direct, indirect, or (more commonly) extent mappings.  
- Distinguish block-group level free-space tracking (block and inode bitmaps) from the global view.  
- Use `tune2fs`, `dumpe2fs`, `debugfs`, and `stat` to inspect real ext4 metadata on a live or loop-backed filesystem.  
- Predict the observable consequences of inode exhaustion versus data-block exhaustion.  
- Relate the on-disk structures back to the VFS inode and page-cache objects you already know.

## 4. Prerequisite Concepts

You already understand:

- An inode is the filesystem’s permanent identity for a file and contains metadata plus the map to data blocks.  
- A block device is addressed by block number.  
- The VFS caches inodes and directory entries and routes operations to the concrete filesystem.  
- Mounting associates a filesystem on a block device with a directory in the VFS tree.

## 5. Mental Model

```
Block device
┌──────────────────────────────────────────────────────────────────┐
│ Superblock (and backups)                                         │
│ Group Descriptor Table                                           │
│                                                                  │
│ Block Group 0                                                    │
│   ┌─────────────┬─────────────┬─────────────┬─────────────────┐  │
│   │ Block       │ Inode       │ Inode Table │ Data blocks     │  │
│   │ bitmap      │ bitmap      │             │                 │  │
│   └─────────────┴─────────────┴─────────────┴─────────────────┘  │
│ Block Group 1                                                    │
│   ...                                                            │
│ Block Group N                                                    │
└──────────────────────────────────────────────────────────────────┘
```

A file’s inode number selects a block group and an index inside that group’s inode table.  
The inode itself contains either classical block pointers or (normally) an **extent tree** that maps logical file offsets to physical block ranges on the device.

## 6. Core Concept

ext4 divides the block device into **block groups**. Each block group contains a portion of the inode table, bitmaps that track free inodes and free data blocks, and the data blocks themselves. This design improves locality and allows parallel allocation.

### Superblock

The **superblock** stores global filesystem parameters:

- total number of inodes and blocks  
- block size  
- number of blocks per group  
- feature flags (extents, 64-bit, flex_bg, etc.)  
- UUID, volume label, mount state, error behavior  

Primary superblock is at a fixed location; backup copies exist in selected block groups.

### Block groups and descriptors

Each block group is described by a **group descriptor** that records the location of:

- the block bitmap  
- the inode bitmap  
- the inode table  

Modern ext4 often uses flexible block groups (`flex_bg`) so that bitmaps and inode tables for several groups can be packed together for better performance.

### Inodes on disk

An ext4 inode is a fixed-size structure (typically 256 bytes on modern filesystems). It contains:

- mode, UID, GID, link count, size, timestamps  
- flags  
- the data-mapping information (extent tree or classical pointers)  
- extended-attribute block pointer (if needed)  

The inode number is an index, not a disk address. Conversion:

```
group = (inode_number - 1) / inodes_per_group
index = (inode_number - 1) % inodes_per_group
location = inode_table_start_of_group + index * inode_size
```

### Data mapping: extents

Classic ext2/3 used direct, single-indirect, double-indirect, and triple-indirect block pointers.  
ext4 defaults to **extents**: a compact tree that records contiguous runs of blocks as `(logical offset, physical block, length)` records. Large files therefore need far fewer metadata blocks and benefit from better contiguity.

### Allocation

When a file needs new blocks or a new inode is required:

- the allocator consults the appropriate bitmap(s)  
- preferred allocation is near the inode or near existing data of the same file (locality)  
- multiple allocation strategies and heuristics exist (reservation, delayed allocation, multi-block allocation, etc.)

**Delayed allocation** is especially important: `write()` often only updates the page cache; physical blocks are chosen later (at `fsync`, memory pressure, or write-back). This improves contiguity but means that `df` and the on-disk state can lag behind what processes have written.

## 7. Break It Into the Smallest Important Pieces

### 7.1 Superblock
- Global control structure.  
- Read at mount time; kept in memory thereafter.  
- Contains the critical parameters that define the layout.

### 7.2 Block group
- Unit of allocation and locality.  
- Owns a slice of the inode table and a region of data blocks.

### 7.3 Block bitmap / inode bitmap
- One bit per data block or per inode in the group.  
- 0 = free, 1 = in use (convention used by ext4 tools).

### 7.4 Inode table
- Array of fixed-size inode structures.  
- Pre-allocated at filesystem creation time; the total number of inodes is therefore fixed (unless online growth features are used).

### 7.5 Extent tree
- Root stored inside the inode (or in extent-tree blocks for large files).  
- Maps logical file offsets to physical block ranges.  
- Replaces the older indirect-block scheme for almost all modern files.

### 7.6 Allocation bitmaps and free-space accounting
- Local (per-group) bitmaps + global counters in the superblock and group descriptors.  
- Inode exhaustion and block exhaustion are independent conditions.

### 7.7 Journal (high-level awareness)
- ext4 is a journaling filesystem.  
- Metadata (and optionally data) changes are first written to a journal area so that recovery after a crash is fast and consistent.  
- Deep journal internals are left for a later, optional deep dive; the important engineering fact is that the journal exists and is why `fsck` is usually fast after an unclean shutdown.

## 8. What Linux Is Actually Doing

**Mount**
```
mount() system call
    → VFS
    → ext4_fill_super()
        read superblock
        read group descriptors
        initialize in-memory structures
        perform recovery from journal if needed
    → filesystem is ready for path resolution and I/O
```

**Inode lookup (simplified)**
```
VFS supplies inode number
    → ext4 calculates block group + index
    → reads the inode table block (usually already in cache)
    → fills an in-memory VFS inode
```

**Block allocation (high level)**
```
write() hits a hole or extends the file
    → page cache is updated (delayed allocation)
    → later, under memory pressure or fsync/write-back:
        ext4 allocator finds free blocks via bitmaps
        updates extent tree
        updates bitmaps and counters
        (journalled) writes metadata
```

## 9. Commands and Tools

| Command | Purpose |
|---------|---------|
| `tune2fs -l /dev/…` | Display superblock contents (safe, read-only) |
| `dumpe2fs /dev/…` | Detailed layout including block groups (can be long) |
| `debugfs -R 'stat <inode>' /dev/…` | Inspect a single inode’s on-disk structure |
| `debugfs -R 'testb <block>' …` | Test whether a block is allocated |
| `stat` / `ls -i` | Inode number and basic metadata from the VFS |
| `df -i` | Inode usage versus data-block usage |
| `findmnt -T /path -o FSTYPE,SOURCE` | Confirm a path is on ext4 |

All inspection commands above are safe when used read-only. Never run `fsck` on a mounted read-write filesystem.

## 10. Hands-On Lab

**Objective**  
Inspect the on-disk layout of a real ext4 filesystem and relate inode numbers to block-group structures.

**Setup**  
Use either the root filesystem (read-only inspection) or, safer and clearer, a small loop-backed ext4 filesystem.

```bash
mkdir -p ~/ext4-lab
cd ~/ext4-lab
dd if=/dev/zero of=ext4.img bs=1M count=512 status=progress
mkfs.ext4 -q ext4.img
mkdir mnt
sudo mount -o loop ext4.img mnt
sudo chown $USER:$USER mnt
```

**Steps**

1. Confirm filesystem type and basic parameters:
```bash
findmnt -T ~/ext4-lab/mnt
tune2fs -l ext4.img | less
# Note: Block count, Inode count, Block size, Inodes per group, Blocks per group, features
```

2. Create some files and record their inode numbers:
```bash
echo "hello ext4" > mnt/file1.txt
echo "another file" > mnt/file2.txt
mkdir mnt/subdir
ls -li mnt
stat mnt/file1.txt
```

3. Examine free inode and block counts:
```bash
df -h mnt
df -i mnt
```

4. Use `dumpe2fs` (on the unmounted image for safety after unmount, or on the live device if you prefer):
```bash
sudo umount mnt
dumpe2fs ext4.img | head -80
# Look for Group 0 descriptors, free blocks/inodes, bitmap locations
```

5. Re-mount and use `debugfs` to inspect an inode:
```bash
sudo mount -o loop ext4.img mnt
INODE=$(stat -c %i mnt/file1.txt)
sudo debugfs -R "stat <$INODE>" ext4.img
# Observe extent information, size, link count, timestamps
```

6. Force inode exhaustion (illustrative, on the small image):
```bash
# Create many small files until df -i shows 100 % inodes
for i in $(seq 1 20000); do touch mnt/f$i 2>/dev/null || break; done
df -i mnt
df -h mnt          # still free data blocks
sudo umount mnt
```

**Verification**  
You must be able to:

- State the block size, inode size, and inodes-per-group of your test filesystem.  
- Map at least one file’s inode number to the information shown by `debugfs`.  
- Demonstrate a situation in which data blocks remain free while inodes are exhausted.

**Cleanup**
```bash
sudo umount ~/ext4-lab/mnt 2>/dev/null
rm -rf ~/ext4-lab
```

## 11. Investigation Lab

**Scenario**  
A build server reports “No space left on device” while creating many small object files. `df -h` shows 40 % free space. `df -i` shows 100 % inode usage.

**Objective**  
Confirm the diagnosis, identify the filesystem, and propose both immediate and longer-term remedies.

**Available tools**  
`df -h`, `df -i`, `findmnt`, `tune2fs -l`, `ls`, `du`, `find`

**Initial clues**  
- Error message is `ENOSPC`.  
- Data-block usage is moderate.  
- The workload creates huge numbers of small files.

**Investigation questions**  
1. Which resource is exhausted when `df -i` reports 100 %?  
2. Why does the kernel return the same error code (`ENOSPC`) for both inode and block exhaustion?  
3. How do you find which filesystem is full of inodes?  
4. What are practical recovery options (delete files, move data, recreate with more inodes, use a different filesystem design)?

Work the questions before reading the solution.

**Solution**  
```bash
df -i
findmnt -T /path/that/failed
```
The filesystem has used every pre-allocated inode. Immediate recovery is to delete unneeded files (especially small ones) or move them elsewhere. Longer-term options include:

- recreating the filesystem with a higher inode ratio (`mkfs.ext4 -i …` or `-N`)  
- using a different directory layout / packing strategy  
- switching to a filesystem that allocates inodes dynamically (e.g. XFS) if the workload is permanently inode-heavy  

Once inodes are free again, applications can create files. Data blocks were never the constraint.

## 12. Production Failure Scenario

**Incident**  
After an unclean shutdown (power loss), a database volume fails to mount. The console shows an ext4 error and a recommendation to run `fsck`. The volume is a critical 2 TB data disk.

**Systematic approach**

1. **Observation**  
   Mount fails; kernel log or `dmesg` mentions ext4 and the need for filesystem check.

2. **Hypothesis**  
   Journal recovery could not complete automatically, or corruption was detected beyond what the journal can fix.

3. **Evidence**  
   ```bash
   dmesg | tail -50
   journalctl -b -u <mount-unit>
   tune2fs -l /dev/… | grep -E 'state|error|Last checked'
   ```

4. **Action (order and safety)**  
   - Take a snapshot / backup if the volume is cloud- or LVM-backed.  
   - Run `fsck -f` (or the appropriate `e2fsck`) on the **unmounted** device.  
   - Review the questions `fsck` asks; answer conservatively if unsure.  
   - Re-attempt the mount.  
   - Verify application-level consistency (database recovery tools, checksums, etc.).

5. **Prevention**  
   - Ensure the filesystem is mounted with appropriate barrier/journal options.  
   - Prefer clean shutdowns and UPS protection for physical hosts.  
   - Monitor for earlier warning signs (I/O errors, increasing `fsck` time).

Journaling makes the common case of unclean shutdown recoverable in seconds; the rare case that still requires a full `fsck` is why you must understand the on-disk structures and the tools that manipulate them.

## 13. Connection to Previous Linux Knowledge

- The VFS inode you examined with `stat` is the in-memory representation of the on-disk ext4 inode.  
- The block device (Session 05) is the linear array of blocks that the superblock, bitmaps, inode tables, and data blocks occupy.  
- Path resolution (Session 01) ultimately ends at an ext4 inode number that is converted to a disk location by the formulas above.  
- Open file descriptions (Session 02) keep a reference to the in-memory inode; the on-disk structures are updated according to journaling and write-back rules.  
- Mount points (Session 04) attach this concrete layout into the global directory tree.

## 14. Connection to Future Infrastructure

- **Containers**: container image layers and writable layers frequently use ext4 (or OverlayFS on top of ext4). Inode density matters when a node runs thousands of containers.  
- **Kubernetes**: PersistentVolumes backed by cloud disks are commonly formatted with ext4 or XFS; the same exhaustion and growth issues appear.  
- **Cloud**: root and data volumes are almost always ext4 or XFS; understanding `resize2fs` and inode ratios is routine operational work.  
- **AI infrastructure**: training jobs that create millions of small sample files or checkpoint shards can hit inode limits long before space limits; choosing the correct `mkfs` parameters or filesystem type is an infrastructure decision.  
- **Performance**: extent-based layout, delayed allocation, and block-group locality directly affect sequential versus random I/O patterns seen by GPUs and data pipelines.

## 15. Engineering Questions

1. Why can a filesystem report free space with `df -h` yet still refuse to create a new file?  
2. How is an inode number turned into a physical location on the block device?  
3. What problem do extents solve compared with classical indirect block pointers?  
4. Why are inodes pre-allocated at `mkfs` time on ext4?  
5. What is delayed allocation and why does it improve performance?  
6. How does the existence of a journal change the recovery process after a crash?  
7. What does a block group give the filesystem that a single linear layout would not?  
8. Why might `debugfs` show extents while `stat` only shows size and timestamps?  
9. When would you deliberately create an ext4 filesystem with a much higher or lower inode density than the default?

## 16. Practical Assignment

1. Create a loop-backed ext4 filesystem with an unusually low inode count (use `mkfs.ext4 -N`).  
2. Fill it with tiny files until inode exhaustion occurs; record `df -h` and `df -i` at the failure point.  
3. Delete a fraction of the files, confirm that new files can again be created, and note how many inodes were returned.  
4. Use `tune2fs -l` and `dumpe2fs` to document block size, inodes per group, and the location of the inode table for group 0.  
5. Pick one file, obtain its inode number, and use `debugfs` to display its on-disk inode (including extent information if present).  
6. Write a short paragraph relating the on-disk extent map back to the logical file offsets a process uses when it calls `read()` / `write()`.

## 17. Session Completion Test

Answer without notes.

**Conceptual**  
1. What are the main regions of an ext4 block group?  
2. What is stored in the superblock that is essential for mounting?

**System behavior**  
3. A filesystem has free data blocks but zero free inodes. What happens when a process tries to create a file?  
4. Why does `write()` to a new file often not allocate device blocks immediately?

**Command interpretation**  
5. `df -i` shows IUse% = 100 while `df -h` shows 50 % used. What is the constraint?  
6. What kind of information does `tune2fs -l` provide that `stat` on a file does not?

**Troubleshooting**  
7. After a power failure a volume will not mount. What is the first safe tool to run, and in what state must the filesystem be?

**Internal**  
8. Describe how ext4 locates the on-disk inode structure given only an inode number.

**Explain in your own words**  
9. Explain why extents are more efficient than the older indirect-block scheme for large files.

## 18. Mastery Criteria

- **Basic understanding**: You can list the major on-disk components of ext4 and interpret `df -i` versus `df -h`.  
- **Working understanding**: You can inspect a live ext4 filesystem with `tune2fs`/`dumpe2fs`/`debugfs`, map an inode number to its group, and diagnose inode exhaustion.  
- **Strong understanding**: You can reason about allocation locality, delayed allocation, the role of the journal in recovery, and the operational consequences of inode density choices.

## 19. What I Should Now Be Able to Explain

- Superblock and its role at mount time  
- Block groups, bitmaps, and inode tables  
- How an inode number becomes a disk location  
- Extent-based data mapping versus classical pointers  
- Independent limits of inodes and data blocks  
- Delayed allocation at a high level  
- Why journaling makes crash recovery fast  
- How to inspect the above with standard tools

## 20. Next Session

**Next Session Number**  
SESSION 07  

**Next Session Title**  
Page Cache, Writeback, and the Relationship Between Memory and Disk  

**Why it comes next**  
You now understand how ext4 maps file offsets to device blocks on disk. The next critical piece is the layer that sits between processes and the filesystem for almost all ordinary I/O: the page cache. Understanding when data lives only in memory, when it is written back, and how `fsync` forces durability is essential for both correctness and performance.
