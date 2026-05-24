# Voxia2 Virtual File System (VFS) Specification

## 1. Introduction
The Voxia2 Virtual File System (VFS) is a kernel-level abstraction layer that provides a uniform interface for accessing various filesystem types. It decouples the system calls from the underlying filesystem implementation, allowing the kernel to support multiple filesystems (e.g., Ext2, FAT32, Procfs) and devices transparently.

## 2. Core Architecture
The VFS architecture is built upon four primary objects:
- **VNode (Virtual Node)**: Represents a unique file or directory in the system.
- **Dentry (Directory Entry)**: Represents a component in a path and maintains the filesystem hierarchy.
- **Filesystem**: Defines the behavior and operations of a specific filesystem type.
- **Mount Point**: Links a physical device and a filesystem instance to the global VFS tree.

---

## 3. Data Structures

### 3.1 VNode (`struct vnode`)
The VNode is the core representation of a file-like object. Unlike physical inodes, a VNode exists only in memory while the file is being accessed.

| Field | Type | Description |
| :--- | :--- | :--- |
| `id` | `vnode_id_t` | Unique identifier for the node. |
| `type` | `uint8_t` | Type of node (FILE, DIR, DEV, BLK, etc.). |
| `size` | `size_t` | Size of the file in bytes. |
| `ops` | `void*` | Pointer to operation tables (`vops_file_t` or `vops_blk_t`). |
| `fs_instance` | `struct fs_instance*` | The filesystem instance this VNode belongs to. |
| `vnode_private`| `void*` | Filesystem-specific private data. |

### 3.2 Dentry (`struct dentry`)
Dentries (Directory Entries) are used to resolve paths and manage the hierarchy. They connect names to VNodes and are cached to speed up path resolution.

- **Hierarchy**: Each dentry has a `parent` and a `child_list`.
- **Dcache**: Dentries are stored in a hash-based cache (`vfs_cache`) for fast lookup.
- **States**:
    - `DENTRY_PINNED`: Prevent dentry from being reclaimed.
    - `DENTRY_MOUNTPOINT`: Indicates the dentry is a target for a mount operation.

### 3.3 Filesystem Type (`struct filesystem`)
Defines a filesystem driver.
- `name`: String identifier (e.g., "ext2").
- `ops`: Operations such as `lookup`.

### 3.4 Mount (`struct mount`)
Represents an active filesystem attachment.
- `dev`: The underlying device (`cdev_t`).
- `mount_point`: The dentry where the filesystem is attached.
- `root`: The root VNode of the mounted filesystem.

---

## 4. Operational Interfaces

### 4.1 Path Resolution (`vxnamei` / `resolve_dentry`)
The VFS resolves paths by traversing the dentry tree.
1. Start from the root dentry (`/`).
2. Tokenize the path (e.g., `/usr/bin/ls` -> `usr`, `bin`, `ls`).
3. For each component, check the Dcache.
4. If not in cache, invoke the filesystem's `lookup` operation to find/create the VNode.
5. Create a new dentry and insert it into the cache.

### 4.2 File Operations (`vops_file_t`)
Basic interface for regular files:
- `read(vnode, buf, len, offset)`: Read data from the file.

### 4.3 Block Operations (`vops_blk_t`)
Interface for block devices:
- `open(vdata, mode, thread)`
- `read(vdata, addr, buf, count)`
- `write(vdata, addr, buf, count)`
- `close(vdata)`

---

## 5. Device Management
Voxia2 uses a Major/Minor number system for device identification.
- **Major Number**: Identifies the device driver (e.g., `DEV_MAJOR_HD` for IDE disks).
- **Minor Number**: Identifies the specific instance or partition.

Devices are registered as `cdev_t` structures and can be retrieved using `retrieve_dev(major, minor)`.

---

## 6. Dentry Cache (Dcache)
To minimize expensive filesystem lookups, the VFS maintains a global dentry cache.
- **Storage**: A hash table with `VFS_CACHE_SIZE` (default 32) buckets.
- **Collision Handling**: Linked lists (hlist) within each bucket.
- **Lookup**: Uses a hash of the component name and the parent dentry pointer.

---

## 7. VNode Types
The system supports several types of virtual nodes:

- `VNODE_TYPE_FILE`: Regular data file.
- `VNODE_TYPE_DIR`: Directory containing other entries.
- `VNODE_TYPE_DEV`: Generic device node.
- `VNODE_TYPE_CHR`: Character device.
- `VNODE_TYPE_BLK`: Block device.
- `VNODE_TYPE_FIFO`: Named pipe.
- `VNODE_TYPE_SOCK`: Network socket.
- `VNODE_TYPE_LNK`: Symbolic link.

---

## 8. Status and Error Codes
The VFS uses the following status codes for operation results:

| Constant | Value | Description |
| :--- | :--- | :--- |
| `VFS_OK` | `0` | Operation completed successfully. |
| `VFS_ERR_BUSY` | `-1` | Resource is busy. |
| `VFS_ENOENT` | `-2` | No such file or directory. |
| `VFS_ERR` | `-5` | General VFS error. |
| `VFS_DEV_NOT_FOUND` | `-7` | Underlying device not found. |
| `VFS_FS_NOT_FOUND` | `-8` | Requested filesystem type not registered. |

## 9. File Handles (`struct file`)
While VNodes represent the object on disk/memory, the `file` structure represents an open file instance (file descriptor level).

- `ops`: Pointer to `file_operations_t` (e.g., `read`).
- `private_data`: Instance-specific data for the open file.
