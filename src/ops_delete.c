#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <libgen.h>   // dirname(), basename()
#include <limits.h>   // PATH_MAX

// ─── Shared state ────────────────────────
struct mini_unionfs_state {
    char *lower_dir;
    char *upper_dir;
};
#define UNIONFS_DATA ((struct mini_unionfs_state *) fuse_get_context()->private_data)

int unionfs_unlink(const char *path);
int unionfs_rmdir(const char *path);

// ─── Helper: build whiteout path ────────────────────────────────────────────
static void build_whiteout_path(const char *path, char *out) {
    char tmp[PATH_MAX];
    strncpy(tmp, path, PATH_MAX);

    char *dir  = dirname(tmp);   
    char *base = NULL;

    char tmp2[PATH_MAX];
    strncpy(tmp2, path, PATH_MAX);
    base = basename(tmp2);      

    // upper_dir + dir + "/.wh." + basename
    if (strcmp(dir, "/") == 0) {
        snprintf(out, PATH_MAX, "%s/.wh.%s", UNIONFS_DATA->upper_dir, base);
    } else {
        snprintf(out, PATH_MAX, "%s%s/.wh.%s", UNIONFS_DATA->upper_dir, dir, base);
    }
}


// ─── Helper: ensure parent dirs exist in upper ──────────────────────────────
static void ensure_upper_parent(const char *path) {
    char tmp[PATH_MAX];
    strncpy(tmp, path, PATH_MAX);
    char *dir = dirname(tmp);   

    char upper_parent[PATH_MAX];
    snprintf(upper_parent, PATH_MAX, "%s%s", UNIONFS_DATA->upper_dir, dir);

    // mkdir -p equivalent 
    struct stat st;
    if (stat(upper_parent, &st) != 0) {
        mkdir(upper_parent, 0755);
    }
}


// ─── unionfs_unlink ──────────────────────────────────────────────────────────
int unionfs_unlink(const char *path) {
    char upper_path[PATH_MAX];
    char lower_path[PATH_MAX];
    char wh_path[PATH_MAX];

    snprintf(upper_path, PATH_MAX, "%s%s", UNIONFS_DATA->upper_dir, path);
    snprintf(lower_path, PATH_MAX, "%s%s", UNIONFS_DATA->lower_dir, path);

    // ── Case 1: File exists in upper → physically delete it ──────────────
    if (access(upper_path, F_OK) == 0) {
        if (unlink(upper_path) == 0) return 0;
        return -errno;
    }

    // ── Case 2: File exists in lower → create whiteout in upper ──────────
    if (access(lower_path, F_OK) == 0) {
        ensure_upper_parent(path);
        build_whiteout_path(path, wh_path);

        // Create an empty whiteout marker file
        int fd = open(wh_path, O_CREAT | O_WRONLY, 0000);
        if (fd < 0) return -errno;
        close(fd);
        return 0;
    }

    return -ENOENT;
}


// ─── unionfs_rmdir ───────────────────────────────────────────────────────────
int unionfs_rmdir(const char *path) {
    char upper_path[PATH_MAX];
    char lower_path[PATH_MAX];
    char wh_path[PATH_MAX];

    snprintf(upper_path, PATH_MAX, "%s%s", UNIONFS_DATA->upper_dir, path);
    snprintf(lower_path, PATH_MAX, "%s%s", UNIONFS_DATA->lower_dir, path);

    // ── Case 1: Directory exists in upper → remove it ─────────────────────
    if (access(upper_path, F_OK) == 0) {
        if (rmdir(upper_path) == 0) return 0;
        return -errno;   // e.g. ENOTEMPTY if not empty
    }

    // ── Case 2: Directory exists only in lower → whiteout marker ─────────
    if (access(lower_path, F_OK) == 0) {
        ensure_upper_parent(path);
        build_whiteout_path(path, wh_path);

        int fd = open(wh_path, O_CREAT | O_WRONLY, 0000);
        if (fd < 0) return -errno;
        close(fd);
        return 0;
    }

    return -ENOENT;
}
