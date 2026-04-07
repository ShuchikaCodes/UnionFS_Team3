// ops_read.c
// Member 2 - Read Operations
// getattr, readdir, open, read

#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <unistd.h>

// ─────────────────────────────────────────────────────────────
// Pulled directly from Member 1's unionfs.c
// Don't redefine — just reference them
// ─────────────────────────────────────────────────────────────

struct mini_unionfs_state {
    char *lower_dir;
    char *upper_dir;
};

#define UNIONFS_DATA ((struct mini_unionfs_state *) fuse_get_context()->private_data)

// Member 1's helpers — defined in unionfs.c, used here
extern int resolve_path(const char *path, char *resolved);
extern void build_path(char *dest, const char *base, const char *path);

// Member 3's CoW function — defined in ops_write.c
extern int cow_copy(const char *path);


// ─────────────────────────────────────────────────────────────
// FUNCTION 1: getattr
// Called by: ls -l, stat
// Uses Member 1's resolve_path() to find the file,
// then calls lstat() to get its metadata (size, perms, etc.)
// ─────────────────────────────────────────────────────────────

int unionfs_getattr(const char *path, struct stat *stbuf,
                    struct fuse_file_info *fi) {
    (void) fi;

    char resolved[PATH_MAX];

    // Ask Member 1's resolver: where does this file actually live?
    int res = resolve_path(path, resolved);
    if (res < 0)
        return res; // whited-out or not found → return -ENOENT

    // Get the real file's metadata and fill stbuf
    if (lstat(resolved, stbuf) == -1)
        return -errno;

    return 0;
}


// ─────────────────────────────────────────────────────────────
// FUNCTION 2: readdir
// Called by: ls
// Must merge upper + lower directory listings with:
//   - No duplicates (upper wins if same file in both)
//   - No whiteout files shown (.wh.filename hidden)
//   - No lower files that have been whited-out
// ─────────────────────────────────────────────────────────────

int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                    off_t offset, struct fuse_file_info *fi,
                    enum fuse_readdir_flags flags) {
    (void) offset;
    (void) fi;
    (void) flags;

    char upper_path[PATH_MAX];
    char lower_path[PATH_MAX];

    // Use Member 1's build_path to construct real dir paths
    build_path(upper_path, UNIONFS_DATA->upper_dir, path);
    build_path(lower_path, UNIONFS_DATA->lower_dir, path);

    // Tracking arrays — files we've already listed, files that are hidden
    #define MAX_ENTRIES 1024
    char seen[MAX_ENTRIES][NAME_MAX];
    char hidden[MAX_ENTRIES][NAME_MAX];
    int seen_count = 0;
    int hidden_count = 0;

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    // ── PASS 1: Scan upper_dir ────────────────────────────────
    // Collect whiteout markers and real entries separately
    DIR *upper_dp = opendir(upper_path);
    if (upper_dp != NULL) {
        struct dirent *de;
        while ((de = readdir(upper_dp)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                continue;

            // If filename starts with ".wh." it's a whiteout marker
            // Record the real name it's hiding (everything after ".wh.")
            if (strncmp(de->d_name, ".wh.", 4) == 0) {
                if (hidden_count < MAX_ENTRIES)
                    strncpy(hidden[hidden_count++], de->d_name + 4, NAME_MAX);
                continue; // don't show .wh. files to the user
            }

            // Normal file/dir — add it and remember we've seen it
            filler(buf, de->d_name, NULL, 0, 0);
            if (seen_count < MAX_ENTRIES)
                strncpy(seen[seen_count++], de->d_name, NAME_MAX);
        }
        closedir(upper_dp);
    }

    // ── PASS 2: Scan lower_dir ────────────────────────────────
    // Only add entries that aren't hidden and aren't already shown
    DIR *lower_dp = opendir(lower_path);
    if (lower_dp != NULL) {
        struct dirent *de;
        while ((de = readdir(lower_dp)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                continue;

            // Skip if whited-out (Member 4 created a .wh. marker for it)
            int is_hidden = 0;
            for (int i = 0; i < hidden_count; i++) {
                if (strcmp(hidden[i], de->d_name) == 0) {
                    is_hidden = 1;
                    break;
                }
            }
            if (is_hidden) continue;

            // Skip if upper already has this file (upper always wins)
            int already_seen = 0;
            for (int i = 0; i < seen_count; i++) {
                if (strcmp(seen[i], de->d_name) == 0) {
                    already_seen = 1;
                    break;
                }
            }
            if (already_seen) continue;

            filler(buf, de->d_name, NULL, 0, 0);
        }
        closedir(lower_dp);
    }

    return 0;
}


// ─────────────────────────────────────────────────────────────
// FUNCTION 3: open
// Called by: any program opening a file
// Read mode  → just verify the file exists
// Write mode → if file is lower-only, trigger CoW first
//              so writes land in upper and never touch lower
// ─────────────────────────────────────────────────────────────

int unionfs_open(const char *path, struct fuse_file_info *fi) {
    char resolved[PATH_MAX];

    int res = resolve_path(path, resolved);
    if (res < 0)
        return res; // not found or whited-out

    // Check if opened for writing
    int write_mode = (fi->flags & O_WRONLY) || (fi->flags & O_RDWR);

    if (write_mode) {
        // Check if file already exists in upper
        // Use Member 1's build_path for consistency
        char upper_path[PATH_MAX];
        build_path(upper_path, UNIONFS_DATA->upper_dir, path);

        if (access(upper_path, F_OK) != 0) {
            // File is lower-only — copy it to upper before writing
            // This is Member 3's cow_copy()
            int cow_res = cow_copy(path);
            if (cow_res < 0)
                return cow_res;
        }
    }

    return 0;
}


// ─────────────────────────────────────────────────────────────
// FUNCTION 4: read
// Called by: cat, any file read
// Finds the real file via resolve_path, reads bytes with pread()
// pread() reads at a specific offset — perfect for FUSE
// ─────────────────────────────────────────────────────────────

int unionfs_read(const char *path, char *buf, size_t size,
                 off_t offset, struct fuse_file_info *fi) {
    (void) fi;

    char resolved[PATH_MAX];

    int res = resolve_path(path, resolved);
    if (res < 0)
        return res;

    int fd = open(resolved, O_RDONLY);
    if (fd == -1)
        return -errno;

    // pread reads 'size' bytes at 'offset' without seeking
    int bytes_read = pread(fd, buf, size, offset);
    if (bytes_read == -1)
        bytes_read = -errno;

    close(fd);
    return bytes_read;
}