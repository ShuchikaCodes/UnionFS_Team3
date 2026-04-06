#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>
#include <libgen.h>

// accessing global state from Member 1
#define UNIONFS_DATA ((struct mini_unionfs_state *) fuse_get_context()->private_data)

// helper to create parent directories
void ensure_parent_dirs(const char *path) {
    char temp[PATH_MAX];
    snprintf(temp, PATH_MAX, "%s", path);

    for (char *p = temp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(temp, 0755);
            *p = '/';
        }
    }
}

// copy on write logic
int cow_copy(const char *path) {
    char lower_path[PATH_MAX];
    char upper_path[PATH_MAX];
    char dir_path[PATH_MAX];

    snprintf(lower_path, PATH_MAX, "%s%s", UNIONFS_DATA->lower_dir, path);
    snprintf(upper_path, PATH_MAX, "%s%s", UNIONFS_DATA->upper_dir, path);

    // creating parent dirs
    snprintf(dir_path, PATH_MAX, "%s", upper_path);
    dirname(dir_path);
    ensure_parent_dirs(dir_path);

    int src = open(lower_path, O_RDONLY);
    if (src < 0) return -errno;

    int dest = open(upper_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (dest < 0) {
        close(src);
        return -errno;
    }

    char buffer[4096];
    ssize_t bytes;

    while ((bytes = read(src, buffer, sizeof(buffer))) > 0) {
        if (write(dest, buffer, bytes) != bytes) {
            close(src);
            close(dest);
            return -EIO;
        }
    }

    // copying permissions
    struct stat st;
    if (stat(lower_path, &st) == 0) {
        fchmod(dest, st.st_mode);
    }

    close(src);
    close(dest);

    return 0;
}

// write implementation
static int unionfs_write(const char *path, const char *buf,
                         size_t size, off_t offset,
                         struct fuse_file_info *fi) {

    char upper_path[PATH_MAX];
    char lower_path[PATH_MAX];

    snprintf(upper_path, PATH_MAX, "%s%s", UNIONFS_DATA->upper_dir, path);
    snprintf(lower_path, PATH_MAX, "%s%s", UNIONFS_DATA->lower_dir, path);

    // trigger CoW if needed
    if (access(upper_path, F_OK) != 0) {
        if (access(lower_path, F_OK) == 0) {
            int res = cow_copy(path);
            if (res < 0) return res;
        }
    }

    int fd = open(upper_path, O_WRONLY);
    if (fd < 0) return -errno;

    ssize_t res = pwrite(fd, buf, size, offset);
    close(fd);

    if (res < 0) return -errno;
    return res;
}

// create file implementation
static int unionfs_create(const char *path, mode_t mode,
                          struct fuse_file_info *fi) {

    char upper_path[PATH_MAX];
    char dir_path[PATH_MAX];

    snprintf(upper_path, PATH_MAX, "%s%s", UNIONFS_DATA->upper_dir, path);

    snprintf(dir_path, PATH_MAX, "%s", upper_path);
    dirname(dir_path);
    ensure_parent_dirs(dir_path);

    int fd = open(upper_path, O_CREAT | O_WRONLY | O_TRUNC, mode);
    if (fd < 0) return -errno;

    close(fd);
    return 0;
}

// mkdir implementation
static int unionfs_mkdir(const char *path, mode_t mode) {
    char upper_path[PATH_MAX];

    snprintf(upper_path, PATH_MAX, "%s%s", UNIONFS_DATA->upper_dir, path);

    int res = mkdir(upper_path, mode);
    if (res < 0) return -errno;

    return 0;
}

// exporting operations
struct fuse_operations write_ops = {
    .write  = unionfs_write,
    .create = unionfs_create,
    .mkdir  = unionfs_mkdir,
};

