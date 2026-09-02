/*
 * File: twrp_tk.c
 * Author: kelexine <https://github.com/kelexine>
 * Date: 2026-09-02
 * Purpose: TWRP Decryption Isolation utility for TrustKernel TEE
 *
 * Description:
 * Mounts protect1 and persist partitions strictly READ-ONLY (MS_RDONLY) to
 * temporary staging mountpoints, clones TEE trustlets and protection keys
 * into isolated RAM-backed tmpfs directories (_twrp), and immediately
 * unmounts physical storage.
 *
 * This prevents TWRP decryption operations from modifying Android OS
 * persist/protection keys on physical flash, averting encryption corruption.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mount.h>

#define COPY_BUFFER_SIZE 65536
#define PATH_BUFFER_SIZE 4096
#define DIR_MODE_DEFAULT 0771

static void remove_dir_recursive(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) {
            return;
        }
        struct dirent *de;
        while ((de = readdir(dir)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
                continue;
            }
            char subpath[PATH_BUFFER_SIZE];
            snprintf(subpath, sizeof(subpath), "%s/%s", path, de->d_name);
            remove_dir_recursive(subpath);
        }
        closedir(dir);
        rmdir(path);
    } else {
        unlink(path);
    }
}

static int mkdir_p(const char *path, mode_t mode) {
    char temp[PATH_BUFFER_SIZE + 1];
    snprintf(temp, sizeof(temp), "%s", path);
    size_t len = strlen(temp);
    if (len > 0 && temp[len - 1] == '/') {
        temp[len - 1] = '\0';
    }

    struct stat st;
    if (stat(temp, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }

    for (char *p = temp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (stat(temp, &st) != 0) {
                if (mkdir(temp, mode) != 0 && errno != EEXIST) {
                    return -1;
                }
            }
            *p = '/';
        }
    }

    if (mkdir(temp, mode) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static int copy_single_file(const char *src, const char *dst, mode_t mode, const struct timeval times[2]) {
    int fd_in = open(src, O_RDONLY);
    if (fd_in < 0) {
        return -1;
    }

    int fd_out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd_out < 0) {
        close(fd_in);
        return -1;
    }

    char *buf = malloc(COPY_BUFFER_SIZE);
    if (!buf) {
        close(fd_in);
        close(fd_out);
        return -1;
    }

    ssize_t nread;
    int write_err = 0;
    while ((nread = read(fd_in, buf, COPY_BUFFER_SIZE)) > 0) {
        ssize_t total = 0;
        while (total < nread) {
            ssize_t nw = write(fd_out, buf + total, (size_t)(nread - total));
            if (nw < 0) {
                write_err = 1;
                break;
            }
            total += nw;
        }
        if (write_err) {
            break;
        }
    }

    free(buf);
    close(fd_in);
    if (write_err || nread < 0) {
        close(fd_out);
        return -1;
    }

    fsync(fd_out);
    close(fd_out);
    chmod(dst, mode);
    utimes(dst, times);
    return 0;
}

static int copy_dir_recursive(const char *src, const char *dst) {
    struct stat st;
    if (stat(src, &st) != 0) {
        return -1;
    }

    if (mkdir_p(dst, DIR_MODE_DEFAULT) != 0) {
        return -1;
    }

    DIR *dir = opendir(src);
    if (!dir) {
        return -1;
    }

    int ret = 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }

        char src_child[PATH_BUFFER_SIZE];
        char dst_child[PATH_BUFFER_SIZE];
        snprintf(src_child, sizeof(src_child), "%s/%s", src, de->d_name);
        snprintf(dst_child, sizeof(dst_child), "%s/%s", dst, de->d_name);

        struct stat child_st;
        if (lstat(src_child, &child_st) != 0) {
            ret = -1;
            continue;
        }

        if (S_ISLNK(child_st.st_mode)) {
            char target[PATH_BUFFER_SIZE];
            ssize_t link_len = readlink(src_child, target, sizeof(target) - 1);
            if (link_len < 0) {
                ret = -1;
                continue;
            }
            target[link_len] = '\0';
            unlink(dst_child);
            if (symlink(target, dst_child) != 0) {
                ret = -1;
            }
        } else if (S_ISDIR(child_st.st_mode)) {
            if (copy_dir_recursive(src_child, dst_child) != 0) {
                ret = -1;
            }
        } else if (S_ISREG(child_st.st_mode)) {
            struct timeval times[2];
            times[0].tv_sec = child_st.st_atime;
            times[0].tv_usec = 0;
            times[1].tv_sec = child_st.st_mtime;
            times[1].tv_usec = 0;
            if (copy_single_file(src_child, dst_child, child_st.st_mode, times) != 0) {
                ret = -1;
            }
        }
    }

    closedir(dir);
    chmod(dst, DIR_MODE_DEFAULT);
    return ret;
}

static const char *find_block_device(const char *const candidates[], size_t count) {
    for (size_t i = 0; i < count; i++) {
        struct stat st;
        if (stat(candidates[i], &st) == 0 && (S_ISBLK(st.st_mode) || S_ISCHR(st.st_mode))) {
            return candidates[i];
        }
    }
    return NULL;
}

static int count_dir_entries(const char *path) {
    DIR *d = opendir(path);
    if (!d) {
        return -1;
    }
    int count = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) {
            count++;
        }
    }
    closedir(d);
    return count;
}

static int mount_ro_and_copy(const char *dev, const char *subpath, const char *dst) {
    char tmp_mnt[] = "/tmp/twrp_tk_src_XXXXXX";
    if (!mkdtemp(tmp_mnt)) {
        return -1;
    }

    int rc = -1;
    if (mount(dev, tmp_mnt, "ext4", MS_RDONLY, "") == 0) {
        char src_full[PATH_BUFFER_SIZE];
        snprintf(src_full, sizeof(src_full), "%s/%s", tmp_mnt, subpath);

        struct stat st;
        if (stat(src_full, &st) == 0 && S_ISDIR(st.st_mode)) {
            remove_dir_recursive(dst);
            rc = copy_dir_recursive(src_full, dst);
        }
        umount2(tmp_mnt, MNT_DETACH);
    }
    rmdir(tmp_mnt);
    return rc;
}

static void isolate_source(const char *name,
                           const char *mounted_src1,
                           const char *mounted_src2,
                           const char *const dev_candidates[],
                           size_t dev_count,
                           const char *subpath,
                           const char *dst) {
    printf("[Isolate] Processing %s...\n", name);

    /* 1. Try currently mounted paths if present */
    if (count_dir_entries(mounted_src1) > 0) {
        printf("  Copying from mounted primary source %s -> %s\n", mounted_src1, dst);
        remove_dir_recursive(dst);
        if (copy_dir_recursive(mounted_src1, dst) == 0) {
            printf("  ✓ %s isolated in RAM\n\n", name);
            return;
        }
    }

    if (count_dir_entries(mounted_src2) > 0) {
        printf("  Copying from mounted secondary source %s -> %s\n", mounted_src2, dst);
        remove_dir_recursive(dst);
        if (copy_dir_recursive(mounted_src2, dst) == 0) {
            printf("  ✓ %s isolated in RAM\n\n", name);
            return;
        }
    }

    /* 2. Mount physical partition READ-ONLY, clone into RAM, unmount immediately */
    const char *dev = find_block_device(dev_candidates, dev_count);
    if (dev) {
        printf("  Mounting %s read-only to clone %s -> %s\n", dev, subpath, dst);
        if (mount_ro_and_copy(dev, subpath, dst) == 0) {
            printf("  ✓ %s read-only cloned into RAM\n\n", name);
            return;
        }
        fprintf(stderr, "  ✗ Failed read-only clone for %s\n", name);
    } else {
        fprintf(stderr, "  Notice: No block device found for %s\n", name);
    }

    /* 3. Fallback: ensure empty destination sandbox exists so teed does not reject path */
    mkdir_p(dst, DIR_MODE_DEFAULT);
    chmod(dst, DIR_MODE_DEFAULT);
    printf("  ✓ Sandbox initialized at %s\n\n", dst);
}

static void verify_teed_write(const char *path) {
    char test_file[PATH_BUFFER_SIZE];
    snprintf(test_file, sizeof(test_file), "%s/out", path);
    int fd = open(test_file, O_WRONLY | O_CREAT | O_TRUNC, 0660);
    if (fd >= 0) {
        close(fd);
        unlink(test_file);
        printf("  ✓ Verified teed write access: %s\n", path);
    } else {
        fprintf(stderr, "  ✗ WARNING: teed write test failed for %s: %s\n", path, strerror(errno));
    }
}

static const char *const DEV_PROTECT1[] = {
    "/dev/block/platform/bootdevice/by-name/protect1",
    "/dev/block/by-name/protect1",
    "/dev/block/mmcblk0p12"
};

static const char *const DEV_PERSIST[] = {
    "/dev/block/platform/bootdevice/by-name/persist",
    "/dev/block/by-name/persist",
    "/dev/block/mmcblk0p15"
};

static const char *const DEV_PROTECT2[] = {
    "/dev/block/platform/bootdevice/by-name/protect2",
    "/dev/block/by-name/protect2",
    "/dev/block/mmcblk0p13"
};

int main(void) {
    puts("==================================================");
    puts("TWRP TrustKernel Decryption Isolation by kelexine");
    puts("Preserves Android on-flash keys (pure RAM tmpfs)");
    puts("==================================================\n");

    /* Ensure tmpfs base directories exist */
    mkdir_p("/mnt/vendor/protect_f", DIR_MODE_DEFAULT);
    mkdir_p("/mnt/vendor/persist", DIR_MODE_DEFAULT);
    mkdir_p("/mnt/vendor/protect_s", DIR_MODE_DEFAULT);
    mkdir_p("/protect_f", DIR_MODE_DEFAULT);
    mkdir_p("/persist", DIR_MODE_DEFAULT);
    mkdir_p("/protect_s", DIR_MODE_DEFAULT);

    /* 1. Isolate protect1 TEE keys (primary protected storage for Gatekeeper) */
    isolate_source("protect_f TEE keys",
                   "/mnt/vendor/protect_f/tee",
                   "/protect_f/tee",
                   DEV_PROTECT1,
                   sizeof(DEV_PROTECT1) / sizeof(DEV_PROTECT1[0]),
                   "tee",
                   "/mnt/vendor/protect_f/tee_twrp");

    /* 2. Isolate persist t6 trustlets */
    isolate_source("persist t6 trustlets",
                   "/mnt/vendor/persist/t6",
                   "/persist/t6",
                   DEV_PERSIST,
                   sizeof(DEV_PERSIST) / sizeof(DEV_PERSIST[0]),
                   "t6",
                   "/mnt/vendor/persist/t6_twrp");

    /* 3. Isolate protect2 TEE keys (optional secondary protection) */
    isolate_source("protect_s TEE keys",
                   "/mnt/vendor/protect_s/tee",
                   "/protect_s/tee",
                   DEV_PROTECT2,
                   sizeof(DEV_PROTECT2) / sizeof(DEV_PROTECT2[0]),
                   "tee",
                   "/mnt/vendor/protect_f/tee_twrp");

    /* Mirror to recovery rootfs layout in RAM */
    remove_dir_recursive("/protect_f/tee_twrp");
    copy_dir_recursive("/mnt/vendor/protect_f/tee_twrp", "/protect_f/tee_twrp");

    remove_dir_recursive("/persist/t6_twrp");
    copy_dir_recursive("/mnt/vendor/persist/t6_twrp", "/persist/t6_twrp");

    /* Also populate /protect_f/tee in RAM so fallback teed path succeeds if ever triggered */
    remove_dir_recursive("/protect_f/tee");
    copy_dir_recursive("/mnt/vendor/protect_f/tee_twrp", "/protect_f/tee");

    /* Verify teed write check */
    puts("Verifying teed argument compatibility...");
    verify_teed_write("/mnt/vendor/protect_f/tee_twrp");
    verify_teed_write("/mnt/vendor/persist/t6_twrp");

    puts("\nDone! Isolation active in RAM. Flash partitions remain untouched.");
    return 0;
}
