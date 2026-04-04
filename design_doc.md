# Mini-UnionFS Design Document

**Course:** Cloud Computing – UE23CS351B  
**Team:** 3  
**Members:** Shubhangi Srivastava, Shuchika Joy, Sinchana Rathnakar, Siri Basavaraj  

---

## 1. System Overview

Mini-UnionFS is a simplified Union File System implemented using FUSE (Filesystem in Userspace). It simulates the layered filesystem model used in container technologies like Docker, where multiple directory layers are merged into a single unified view without duplicating files.

The system stacks two directories — a read-only lower layer and a writable upper layer — and presents them as one merged filesystem at a mount point.

```
USER APPLICATION (ls, cat, echo, rm ...)
            |
     LINUX KERNEL → FUSE kernel module
            |
     mini_unionfs program (userspace)
            |
     ┌──────────────────────────────────────┐
     │      MERGED VIEW  /mnt/       	    │  ← what the user sees
     ├──────────────────────────────────────┤
     │      UPPER DIR  ./upper/  [rw]       │  ← all writes land here
     │      LOWER DIR  ./lower/  [ro]       │  ← base image, never modified
     └──────────────────────────────────────┘

     Whiteout: upper/.wh.<filename> → hides lower/<filename> from merged view
```

When a user application performs any filesystem operation (read, write, delete, list), the Linux kernel forwards the call through the FUSE kernel module to our `mini_unionfs` userspace program. Our program applies union logic and responds accordingly.

---

## 2. Data Structures

### 2.1 Global State: `mini_unionfs_state`

The entire program revolves around one central struct defined in `unionfs.c`:

```c
struct mini_unionfs_state {
    char *lower_dir;   // absolute path to the lower (read-only) directory
    char *upper_dir;   // absolute path to the upper (writable) directory
};
```

This struct is populated at startup in `main()` using `realpath()` to resolve absolute paths, then passed into FUSE as private data:

```c
return fuse_main(fuse_argc, fuse_argv, &unionfs_oper, state);
```

Every FUSE callback retrieves this state via the `UNIONFS_DATA` macro:

```c
#define UNIONFS_DATA ((struct mini_unionfs_state *) fuse_get_context()->private_data)
```

This allows any function anywhere in the codebase to access `lower_dir` and `upper_dir` without passing them as parameters.

### 2.2 FUSE Operations Table: `fuse_operations`

FUSE requires registering all filesystem callbacks in a `fuse_operations` struct:

```c
static struct fuse_operations unionfs_oper = {
    .getattr = unionfs_getattr,   // file metadata (ls -l, stat)
    .readdir = unionfs_readdir,   // directory listing (ls)
    .open    = unionfs_open,      // open a file
    .read    = unionfs_read,      // read file contents (cat)
    .write   = unionfs_write,     // write to a file (echo)
    .create  = unionfs_create,    // create a new file
    .mkdir   = unionfs_mkdir,     // create a directory
    .unlink  = unionfs_unlink,    // delete a file
    .rmdir   = unionfs_rmdir,     // delete a directory
};
```

---

## 3. Core Logic: `resolve_path()`

Every single FUSE callback begins by calling `resolve_path()`. It is the backbone of the entire system. Its job is to answer: **"For this virtual path, where does the file actually live on disk?"**

### 3.1 Resolution Algorithm

```
Input path (e.g. /config.txt)
         |
         ▼
Does upper/.wh.config.txt exist?
         |
    YES  |  NO
         |
    return -ENOENT          Does upper/config.txt exist?
    (file is hidden)                 |
                               YES  |  NO
                                    |
                           return   |          Does lower/config.txt exist?
                           upper    |                   |
                           path     |              YES  |  NO
                                    |                   |
                                    |        return     |   return -ENOENT
                                    |        lower      |   (not found)
                                    |        path
```

### 3.2 Priority Rules

| Priority | Condition | Result |
|----------|-----------|--------|
| 1st | Whiteout file exists in upper | File treated as deleted → `-ENOENT` |
| 2nd | File exists in upper | Use upper version |
| 3rd | File exists in lower | Use lower version |
| 4th | Neither | File not found → `-ENOENT` |

Upper always wins over lower. A whiteout always wins over both.

---

## 4. Copy-on-Write (CoW) Flow

Copy-on-Write is the mechanism that allows the lower layer to remain completely untouched even when a user modifies a file through the mount point.

### 4.1 Step-by-Step Flow

```
User runs: echo "modified" >> /mnt/config.txt
         |
         ▼
unionfs_open() called with O_WRONLY flag
         |
         ▼
resolve_path("/config.txt") → found in lower/config.txt
         |
         ▼
Check: does upper/config.txt exist?
         |
    NO   |
         ▼
cow_copy("/config.txt") triggered
  → opens lower/config.txt for reading
  → creates upper/config.txt for writing
  → copies all bytes from lower → upper
  → preserves file permissions (fchmod)
  → closes both file descriptors
         |
         ▼
Write now happens on upper/config.txt
         |
         ▼
lower/config.txt is completely untouched ✓
```

### 4.2 Why CoW Matters

Without CoW, writing to a lower-layer file would be impossible (it's read-only) or would require duplicating the entire filesystem upfront. CoW makes writes lazy — files are only copied when actually modified, which saves storage and time.

---

## 5. Whiteout Flow

Since the lower directory is read-only, files cannot be physically deleted from it. Instead, deletion is simulated using **whiteout marker files**.

### 5.1 Step-by-Step Flow

```
User runs: rm /mnt/config.txt
         |
         ▼
unionfs_unlink() called
         |
         ▼
Does upper/config.txt exist?
         |
    YES  |  NO (file is lower-only)
         |
physically      Create whiteout marker:
delete it       upper/.wh.config.txt (empty file)
         |               |
         ▼               ▼
File gone        Next resolve_path("/config.txt"):
from mount       → sees .wh.config.txt in upper
                 → returns -ENOENT immediately
                 → file appears deleted to user ✓

lower/config.txt still physically exists on disk (untouched)
```

### 5.2 Whiteout in Directory Listing

When `unionfs_readdir()` scans the upper directory, any entry starting with `.wh.` is:
1. Added to a `hidden[]` tracking array (recording what it hides)
2. Not shown in the merged listing itself

Then when scanning lower, any file whose name appears in `hidden[]` is skipped entirely.

---

## 6. Edge Cases Handled

| Scenario | Behaviour |
|----------|-----------|
| File exists in both upper and lower | Upper version always shown; lower silently ignored |
| File only in lower, opened for write | CoW triggers automatically; file copied to upper before write |
| File only in upper | Read/written directly from upper; lower not consulted |
| File deleted (whiteout), then recreated | New file created in upper; whiteout marker removed |
| Directory listed with whited-out files | `.wh.` entries filtered from view; hidden files not shown |
| Same filename in upper and lower, `ls` called | Only one entry shown (upper); no duplicates |
| File not in either layer | `resolve_path()` returns `-ENOENT`; operation fails cleanly |
| Write attempted on lower-only directory | Parent dirs created in upper to mirror lower path structure |

---

## 7. File Structure

```
mini-unionfs/
├── src/
│   ├── unionfs.c       ← Member 1: main(), global state, resolve_path(), FUSE boilerplate
│   ├── ops_read.c      ← Member 2: getattr, readdir, open, read
│   ├── ops_write.c     ← Member 3: write, create, mkdir, cow_copy()
│   └── ops_delete.c    ← Member 4: unlink, rmdir, whiteout logic
├── tests/
│   └── test_unionfs.sh ← Member 4: automated test suite
├── docs/
│   ├── design_doc.md   ← this document
│   └── README.md       ← build and run instructions
└── Makefile            ← Member 1
```

---

## 8. Build & Run Summary

```bash
# Install dependencies
sudo apt install libfuse-dev fuse build-essential

# Build
make

# Mount
make mount        # mounts with ./lower as base, ./upper as writable, ./mnt as view

# Unmount
make unmount

# Run tests
bash tests/test_unionfs.sh
```
