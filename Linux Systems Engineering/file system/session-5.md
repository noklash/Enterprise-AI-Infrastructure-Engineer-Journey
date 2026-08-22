# Session 05 — Block Devices, Partitions, and the Path from Filename to Storage

## 1. Position in the Curriculum

**Phase**  
PHASE 1 — Linux Systems Engineering

**Module**  
MODULE 1 — Filesystems

**Session**  
SESSION 05 — Block Devices, Partitions, and the Path from Filename to Storage

**Prerequisites**  
- Inodes, directory entries, path resolution (Session 01)  
- File descriptors and open file descriptions (Session 02)  
- Hard links, symbolic links, deletion semantics (Session 03)  
- VFS, mount points, and the unified directory tree (Session 04)

**What this session unlocks**  
The ability to follow a write from a filename all the way to a storage device. This is required before studying concrete filesystems (ext4), disk I/O, the page cache, performance troubleshooting, logical volumes, and every form of persistent storage used by containers and cloud instances.

## 2. Why This Session Exists

You now understand how the VFS presents a single directory tree and how mount points graft filesystems into that tree.  

The next necessary question is: what sits underneath a mounted filesystem?  

A filesystem such as ext4 does not talk to “the disk” directly. It talks to a **block device**. Block devices are the kernel’s abstraction for storage that can be addressed in fixed-size blocks. Partitions carve a single physical (or virtual) disk into multiple block devices. Only after a filesystem has been created on a block device and that device has been mounted does a path such as `/var/log/app.log` become usable.

Without this layer you cannot answer:

- Where do the names `/dev/sda`, `/dev/sda1`, `/dev/nvme0n1p2` come from?  
- Why does `df` show a filesystem while `lsblk` shows devices?  
- What happens when you write to a file—does the data go straight to the device?  
- How do cloud volumes, virtual disks, and whole-disk filesystems fit into the same model?

This session connects the VFS view to the block layer.

## 3. Learning Objectives

By the end of this session you will be able to:

- Explain what a block device is and how it differs from a character device.  
- Identify the major block devices on a system and the partitions they contain.  
- Trace the relationship: disk → partition → block device node → filesystem → mount point → path.  
- Use `lsblk`, `blkid`, `fdisk -l`, `/proc/partitions`, and `/sys/block` to discover topology.  
- Show where device nodes in `/dev` come from and what major/minor numbers mean.  
- Describe the high-level path a `write()` system call follows from a file descriptor down to a block device.  
- Distinguish whole-disk filesystems from partitioned disks and explain when each is used.  
- Predict the effect of writing to a block device node versus writing to a file on a mounted filesystem.

## 4. Prerequisite Concepts

You already know:

- The VFS routes file operations to a concrete filesystem implementation.  
- A filesystem is mounted on a directory (mount point).  
- Inodes ultimately describe the location of data.  
- `/dev` is a filesystem (normally devtmpfs) that contains device nodes.

## 5. Mental Model

```
Application
    write(fd, buf, count)
            │
            ▼
      VFS / filesystem (ext4, xfs, …)
            │  translates file offset → block numbers
            ▼
      Block layer (request queues, scheduling, merging)
            │
            ▼
      Block device (/dev/sda1, /dev/nvme0n1p3, …)
            │
            ▼
      Storage driver (SCSI, NVMe, virtio-blk, …)
            │
            ▼
      Hardware (or virtual disk)
```

At the user-visible level the chain is:

```
Path
  → mount point
    → filesystem
      → block device
        → partition (optional)
          → whole disk
            → physical or virtual storage
```

## 6. Core Concept

### Block devices

A **block device** is a kernel abstraction for storage that:

- is addressed in fixed-size blocks (historically 512 bytes; modern devices often use 4 KiB internal blocks)  
- supports random access  
- is normally accessed through the buffer/page cache (except when opened with `O_DIRECT`)

Examples: hard disks, SSDs, NVMe namespaces, virtual disks presented by a hypervisor, loop devices, and most cloud block volumes.

Contrast with **character devices** (e.g. `/dev/tty`, `/dev/null`, `/dev/zero`), which are accessed as streams of bytes and are not addressed by block number.

### Device nodes and major/minor numbers

Block (and character) devices appear in `/dev` as special files. Each has:

- a **major number** — identifies the device driver  
- a **minor number** — identifies the specific instance (disk, partition, etc.)

These numbers are visible with `ls -l /dev/sd*` or `stat`. The kernel uses them to route I/O to the correct driver.

### Disks and partitions

A **disk** (or more precisely a block device that represents a whole drive) can be used in two ways:

1. **Whole-disk filesystem** — a filesystem is created directly on the disk device (common for some cloud volumes and for very simple setups).  
2. **Partitioned disk** — a partition table (GPT or legacy DOS/MBR) divides the disk into one or more regions. Each partition is exposed as its own block device (`/dev/sda1`, `/dev/nvme0n1p2`, …). A filesystem is then created on the partition device.

The partition table itself occupies a small area at the beginning (and, for GPT, the end) of the disk.

### The path from filename to storage

When a process writes to a regular file:

1. The file descriptor leads to an open file description and then to an inode (Sessions 01–02).  
2. The filesystem implementation (e.g. ext4) maps the file offset to one or more filesystem block numbers.  
3. Those filesystem blocks are mapped to device block numbers on the underlying block device.  
4. The block layer may merge, reorder, or schedule the resulting I/O requests.  
5. The storage driver issues the commands to the hardware (or to the hypervisor).

The page cache (studied in a later session) sits between steps 2 and 4 for most normal I/O, so a `write()` often returns before data has reached stable storage.

## 7. Break It Into the Smallest Important Pieces

### 7.1 Block size
- Logical block size presented by the device (usually 512 B or 4 KiB).  
- Filesystem block size (often 4 KiB) is independent but must be compatible.

### 7.2 Major and minor numbers
- Encoded in the device node’s inode.  
- Visible in `ls -l` as two numbers separated by a comma.

### 7.3 Partition table
- GPT (GUID Partition Table) — modern default.  
- DOS/MBR — legacy.  
- Read by the kernel at device discovery time; results appear as additional block devices.

### 7.4 Device discovery and udev
- The kernel detects disks and emits events.  
- `udev` (userspace) creates the corresponding `/dev` nodes and symbolic links (`/dev/disk/by-uuid/`, `/dev/disk/by-id/`, etc.).

### 7.5 `/sys/block`
- Kernel sysfs representation of block devices.  
- Contains topology, queue parameters, statistics, and relationships (e.g. `slaves`, `holders`).

### 7.6 Loop devices
- A block device whose “storage” is an ordinary file.  
- Used for mounting disk images and for some container storage drivers.

### 7.7 Raw I/O versus filesystem I/O
- Opening a block device node and writing to it bypasses any filesystem.  
- Doing so on a device that contains a mounted filesystem is a fast way to cause severe corruption.

## 8. What Linux Is Actually Doing

**Device discovery (simplified)**
```
Hardware / hypervisor announces disk
        ↓
Kernel driver creates gendisk / request_queue
        ↓
Kernel assigns major/minor, emits uevent
        ↓
udev receives event, creates /dev node and symlinks
        ↓
(optional) kernel reads partition table → creates additional block devices for partitions
```

**Write path (high level, ignoring page cache details)**
```
write(fd, …)
    → VFS
    → filesystem (maps offset → filesystem blocks)
    → block layer (bio / request)
    → driver
    → device
```

The exact structures (`bio`, `request`, multi-queue) are more detailed than required at this stage; the important point is that the filesystem never talks to the hardware directly.

## 9. Commands and Tools

| Command / Path | Purpose |
|----------------|---------|
| `lsblk` | Tree view of block devices, partitions, sizes, mount points, types |
| `lsblk -f` | Adds filesystem type, label, UUID |
| `lsblk -o +UUID,PARTUUID,MODEL,SERIAL` | Richer inventory |
| `blkid` | Filesystem and partition UUIDs |
| `fdisk -l` or `parted -l` | Low-level partition table view |
| `cat /proc/partitions` | Kernel’s list of block devices and sizes |
| `ls -l /dev/sd* /dev/nvme* /dev/vd*` | Device nodes and major/minor numbers |
| `ls -l /dev/disk/by-uuid/` | Stable symlinks by filesystem UUID |
| `cat /sys/block/<dev>/…` | Topology, queue, statistics |
| `file -s /dev/sdX` | Quick probe of what is on a device (use with care) |

Never run destructive partition or formatting commands on a disk that contains wanted data.

## 10. Hands-On Lab

**Objective**  
Map the complete chain from a mounted path back to the underlying block device and observe the device nodes and topology.

**Setup**  
Ubuntu VirtualBox VM with at least the root disk. (If you have a second virtual disk, the observations are richer, but not required.)

**Steps**

1. Inventory all block devices:
```bash
lsblk
lsblk -f
lsblk -o NAME,SIZE,TYPE,FSTYPE,MOUNTPOINT,UUID,MODEL
```

2. Examine device nodes:
```bash
ls -l /dev/sda* /dev/nvme* /dev/vda* 2>/dev/null
# Note major,minor numbers and the ‘b’ (block) type
```

3. Relate a mounted path to its device:
```bash
findmnt -T /
findmnt -T / -o TARGET,SOURCE,FSTYPE,UUID
df -hT /
```

4. Inspect kernel and sysfs views:
```bash
cat /proc/partitions
ls /sys/block
ls /sys/block/$(lsblk -no PKNAME $(findmnt -n -o SOURCE -T /))/
```

5. Look at stable identifiers:
```bash
ls -l /dev/disk/by-uuid/
ls -l /dev/disk/by-partuuid/ 2>/dev/null
ls -l /dev/disk/by-id/ | head
```

6. (Safe) Observe a loop device:
```bash
dd if=/dev/zero of=~/loopfile.img bs=1M count=64 status=progress
sudo losetup -f --show ~/loopfile.img
lsblk
sudo losetup -d /dev/loopN          # replace N with the number shown
rm ~/loopfile.img
```

7. Trace one concrete path (example for root):
```bash
ROOT_DEV=$(findmnt -n -o SOURCE -T /)
echo "Root is on $ROOT_DEV"
lsblk -f "$ROOT_DEV"
```

**Verification**  
You must be able to state, for the root filesystem:

- the block device (and partition if any)  
- the filesystem UUID  
- the major/minor numbers of the device node  
- whether the disk is partitioned or used whole

**Cleanup**  
Any loop devices created above should already have been detached. Remove the temporary image file.

## 11. Investigation Lab

**Scenario**  
A system administrator added a new virtual disk to a VM, created a partition, made an ext4 filesystem, and added an entry to `/etc/fstab` using the device name `/dev/sdb1`. After the next reboot the mount failed and the application that depends on the data is down. `lsblk` now shows the new disk as `/dev/sdc`.

**Objective**  
Explain why the mount failed and design a more reliable `/etc/fstab` entry.

**Available tools**  
`lsblk`, `blkid`, `cat /etc/fstab`, `findmnt`, `journalctl`, `/dev/disk/by-uuid/`

**Initial clues**  
- The fstab entry used a `/dev/sd*` name.  
- Device names changed across reboot.  
- The filesystem UUID is still present.

**Investigation questions**  
1. Why are `/dev/sd*` names not stable?  
2. What identifiers remain stable across reboots and device reordering?  
3. How do you discover the correct stable identifier for a filesystem?  
4. What does a correct, robust fstab line look like?

Work the questions before reading the solution.

**Solution**  
Kernel device names (`/dev/sda`, `/dev/sdb`, …) are assigned dynamically according to discovery order. Adding or removing disks, or changes in firmware/driver timing, can alter the names. Filesystem UUIDs (and, to a lesser extent, labels) are stored inside the filesystem and do not change when the device name changes.

```bash
blkid /dev/sdc1          # or whatever the device is now called
ls -l /dev/disk/by-uuid/
```
Replace the fstab device field with `UUID=…`.  
Example:
```
UUID=xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx  /data  ext4  defaults  0  2
```
After the change, `findmnt --verify` (or a reboot) confirms the mount works regardless of the `/dev/sd*` name.

## 12. Production Failure Scenario

**Incident**  
A cloud instance was resized; the root volume grew from 20 GB to 100 GB. `lsblk` shows the larger size, but `df -h /` still reports only 20 GB free space. New application data cannot be written.

**Systematic troubleshooting**

1. **Observation**  
   `lsblk` shows the block device is 100 GB.  
   `df -h /` shows a ~20 GB filesystem.

2. **Hypothesis**  
   The partition table and/or the filesystem were not expanded after the block device grew.

3. **Evidence**  
   ```bash
   lsblk
   findmnt -T /
   # If partitioned:
   sudo fdisk -l /dev/sda          # or the appropriate disk
   # Filesystem size:
   sudo tune2fs -l /dev/sda1 | grep -i block   # for ext4
   ```

4. **Confirmation**  
   The partition still ends at the old boundary, or the partition was grown but the filesystem was not.

5. **Resolution** (order matters)  
   - Grow the partition (e.g. `growpart` or `parted`).  
   - Grow the filesystem (`resize2fs` for ext4, `xfs_growfs` for XFS).  
   - Verify with `lsblk` and `df -h`.  

Modern cloud images often perform these steps automatically on boot; when they do not, the above sequence is the standard recovery path. Writing a new partition table incorrectly can destroy data—always take a snapshot first in cloud environments.

## 13. Connection to Previous Linux Knowledge

- The VFS (Session 04) sits above the filesystem; the filesystem sits above the block device.  
- Inodes store (directly or indirectly) the device block numbers that hold file data.  
- Device nodes in `/dev` are themselves inodes of type “block device”; opening them gives a file descriptor that points at the block device rather than at a regular file.  
- Mount points bind a filesystem (which lives on a block device) into the directory tree.

## 14. Connection to Future Infrastructure

- **Containers**: container runtimes frequently use loop devices, device mapper, or block-backed storage for image layers and volumes.  
- **Kubernetes**: PersistentVolumes of type block or the more common filesystem volumes ultimately rest on block devices attached to the node.  
- **Cloud**: almost every persistent disk is presented to the instance as a block device. Understanding naming, UUIDs, and the grow-partition/grow-filesystem sequence is daily operational knowledge.  
- **AI infrastructure**: large model stores, dataset volumes, and checkpoint disks are block devices (often networked or cloud-attached). Incorrect alignment, outdated partition tables, or missing filesystem growth are common sources of capacity and performance incidents.  
- **Logical Volume Management (LVM)** and **RAID** (later topics) sit between the physical disks and the block devices that filesystems see.

## 15. Engineering Questions

1. What is the difference between a block device and a character device?  
2. Why do `/dev/sda` and `/dev/sda1` both exist, and what does each represent?  
3. What do the major and minor numbers of a device node mean?  
4. Why is it unsafe to write directly to `/dev/sda1` while a filesystem is mounted from it?  
5. How does a path such as `/var/log/app.log` eventually reach a storage device?  
6. Why should `/etc/fstab` prefer UUIDs over `/dev/sd*` names?  
7. What information does `lsblk -f` add that plain `lsblk` does not show?  
8. When would you intentionally create a filesystem on a whole disk rather than on a partition?  
9. How can a block device exist without a corresponding partition table entry?

## 16. Practical Assignment

1. Produce a complete map of every block device on your VM, including:  
   - kernel name  
   - size  
   - partitions (if any)  
   - filesystem type and UUID  
   - current mount point  
   - major:minor numbers  

2. For the root filesystem, write the full chain:  
   path → mount point → filesystem → block device → (partition) → disk  

3. Create a small loop-backed filesystem, mount it, write a file, unmount it, and destroy it. Document every command and what each layer (file, filesystem, loop device) looked like at each step.

4. Explain in your own words what would happen if the partition table on the root disk were destroyed while the system was running.

## 17. Session Completion Test

Answer without notes.

**Conceptual**  
1. What is a block device?  
2. What is the relationship between a disk, a partition, and a filesystem?

**System behavior**  
3. You write to a file on a mounted filesystem. Does the data reach the storage device before `write()` returns? (High-level answer is sufficient.)  
4. What happens if you open a block device node and write to it while a filesystem is mounted from that device?

**Command interpretation**  
5. `lsblk -f` shows a line with a UUID and a mount point. What does each column tell you?  
6. `ls -l /dev/sda1` shows major,minor numbers. What are they used for?

**Troubleshooting**  
7. After a disk resize, `lsblk` shows the new size but `df` does not. What two layers may still need to be grown?

**Internal**  
8. Describe the high-level steps from a `write()` system call on a regular file down to the block device.

**Explain in your own words**  
9. Explain why device names such as `/dev/sdb` are not reliable in `/etc/fstab` and what should be used instead.

## 18. Mastery Criteria

- **Basic understanding**: You can list block devices and partitions with `lsblk` and relate a mount point to its device.  
- **Working understanding**: You can follow the chain from any path to its block device, interpret major/minor numbers, and write a robust fstab entry using UUIDs.  
- **Strong understanding**: You can diagnose capacity problems caused by unexpanded partitions/filesystems, explain the write path at the block-layer boundary, and reason about the risks of raw device access.

## 19. What I Should Now Be Able to Explain

- Block device versus character device  
- Major and minor numbers  
- Disk versus partition versus filesystem  
- How device nodes in `/dev` are created and used  
- The path from a filename through the VFS and filesystem to a block device  
- Why UUIDs are preferred for persistent configuration  
- The difference between writing to a file and writing to a block device node  
- Basic topology discovery with `lsblk`, `blkid`, and `/sys/block`

## 20. Next Session

**Next Session Number**  
SESSION 06  

**Next Session Title**  
ext4 Fundamentals: On-Disk Layout, Inodes, and Allocation  

**Why it comes next**  
You now know that a filesystem lives on a block device and is attached to the directory tree by a mount. The next step is to examine one concrete, widely used filesystem—ext4—so you understand how inodes, data blocks, allocation, and the superblock are actually stored and managed. This turns the abstract “filesystem” into a tangible set of on-disk structures.
