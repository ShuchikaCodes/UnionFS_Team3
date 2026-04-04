#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

// ================= GLOBAL STATE =================

struct mini_unionfs_state {
    char *lower_dir;
    char *upper_dir;
};

#define UNIONFS_DATA ((struct mini_unionfs_state *) fuse_get_context()->private_data)

// ================= HELPER: BUILD PATH =================

void build_path(char *dest, const char *base, const char *path) {
    snprintf(dest, PATH_MAX, "%s%s", base, path);
}

// ================= RESOLVE PATH =================
// Core logic

int resolve_path(const char *path, char *resolved) {
    char upper_path[PATH_MAX];
    char lower_path[PATH_MAX];
    char whiteout_path[PATH_MAX];

    // Extract filename
    const char *filename = strrchr(path, '/');
    filename = filename ? filename + 1 : path;

    // Build whiteout path
    char dir[PATH_MAX];
    strcpy(dir, path);
    char *last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
    } else {
        strcpy(dir, "");
    }

    snprintf(whiteout_path, PATH_MAX, "%s%s/.wh.%s",
             UNIONFS_DATA->upper_dir,
             dir,
             filename);

    // 1. Check whiteout
    if (access(whiteout_path, F_OK) == 0) {
        return -ENOENT;
    }

    // 2. Check upper
    build_path(upper_path, UNIONFS_DATA->upper_dir, path);
    if (access(upper_path, F_OK) == 0) {
        strcpy(resolved, upper_path);
        return 0;
    }

    // 3. Check lower
    build_path(lower_path, UNIONFS_DATA->lower_dir, path);
    if (access(lower_path, F_OK) == 0) {
        strcpy(resolved, lower_path);
        return 0;
    }

    // 4. Not found
    return -ENOENT;
}

// ================= STUB OPERATIONS =================
// These are the functions you guys need to add

static int unionfs_getattr(const char *path, struct stat *stbuf,
                          struct fuse_file_info *fi) {
    memset(stbuf, 0, sizeof(struct stat));
    return -ENOENT;
}

static int unionfs_readdir(const char *path, void *buf,
                          fuse_fill_dir_t filler, off_t offset,
                          struct fuse_file_info *fi,
                          enum fuse_readdir_flags flags) {
    return 0;
}

static int unionfs_open(const char *path, struct fuse_file_info *fi) {
    return 0;
}

static int unionfs_read(const char *path, char *buf, size_t size,
                        off_t offset, struct fuse_file_info *fi) {
    return 0;
}

static int unionfs_write(const char *path, const char *buf, size_t size,
                         off_t offset, struct fuse_file_info *fi) {
    return 0;
}

static int unionfs_create(const char *path, mode_t mode,
                          struct fuse_file_info *fi) {
    return 0;
}

static int unionfs_mkdir(const char *path, mode_t mode) {
    return 0;
}

static int unionfs_unlink(const char *path) {
    return 0;
}

static int unionfs_rmdir(const char *path) {
    return 0;
}

// ================= FUSE OPERATIONS =================

static struct fuse_operations unionfs_oper = {
    .getattr = unionfs_getattr,
    .readdir = unionfs_readdir,
    .open    = unionfs_open,
    .read    = unionfs_read,
    .write   = unionfs_write,
    .create  = unionfs_create,
    .mkdir   = unionfs_mkdir,
    .unlink  = unionfs_unlink,
    .rmdir   = unionfs_rmdir,
};

// ================= MAIN =================

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <lower_dir> <upper_dir> <mountpoint>\n", argv[0]);
        return 1;
    }

    struct mini_unionfs_state *state = malloc(sizeof(struct mini_unionfs_state));

    char lower_real[PATH_MAX];
    char upper_real[PATH_MAX];

    if (!realpath(argv[1], lower_real)) {
        perror("realpath lower_dir failed");
        return 1;
    }

    if (!realpath(argv[2], upper_real)) {
        perror("realpath upper_dir failed");
        return 1;
    }

    state->lower_dir = strdup(lower_real);
    state->upper_dir = strdup(upper_real);

    printf("Lower Dir: %s\n", state->lower_dir);
    printf("Upper Dir: %s\n", state->upper_dir);

    char *fuse_argv[3];
    fuse_argv[0] = argv[0];
    fuse_argv[1] = argv[3];   // mount point
    fuse_argv[2] = "-f";      // run in foreground (IMPORTANT)

    int fuse_argc = 3;

    return fuse_main(fuse_argc, fuse_argv, &unionfs_oper, state);
}