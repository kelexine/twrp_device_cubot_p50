/*
 * File: twrp_tk.c
 * Author: kelexine <https://github.com/kelexine>
 * Date: 2026-08-19
 * Purpose: TWRP Decryption Isolation utility for TrustKernel TEE
 *
 * Description:
 * Clones and isolates on-disk persist (/persist/t6) and protection (/protect_f/tee)
 * partitions into in-memory sandbox directories (/mnt/vendor/persist/t6_twrp and
 * /mnt/vendor/protect_f/tee_twrp on rootfs tmpfs). Enforces AID_SYSTEM (1000)
 * ownership and proper permission bits so that teed (running as UID 1000)
 * performs all key operations and RTC baseline creation in RAM, guaranteeing
 * zero modification or corruption to on-disk Android OS key stores.
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

#define AID_SYSTEM 1000
#define COPY_BUFFER_SIZE 65536
#define PATH_BUFFER_SIZE 4096
#define DIR_MODE 0771
#define BASE_FILE_MODE 0660

static void remove_dir_recursive(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        if (errno == ENOENT) {
            return;
        }
        fprintf(stderr, "[twrp_tk] lstat failed on %s: %s\n", path, strerror(errno));
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) {
            fprintf(stderr, "[twrp_tk] opendir failed on %s: %s\n", path, strerror(errno));
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
        if (rmdir(path) != 0 && errno != ENOENT) {
            fprintf(stderr, "[twrp_tk] rmdir failed on %s: %s\n", path, strerror(errno));
        }
    } else {
        if (unlink(path) != 0 && errno != ENOENT) {
            fprintf(stderr, "[twrp_tk] unlink failed on %s: %s\n", path, strerror(errno));
        }
    }
}

static int mkdir_p(const char *path, mode_t mode, uid_t uid, gid_t gid) {
    char temp[PATH_BUFFER_SIZE + 1];
    snprintf(temp, sizeof(temp), "%s", path);
    size_t len = strlen(temp);
    if (len > 0 && temp[len - 1] == '/') {
        temp[len - 1] = '\0';
    }

    struct stat st;
    if (stat(temp, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            if (chown(temp, uid, gid) != 0) {
                fprintf(stderr, "[twrp_tk] chown %s to %d:%d failed: %s\n", temp, uid, gid, strerror(errno));
            }
            if (chmod(temp, mode) != 0) {
                fprintf(stderr, "[twrp_tk] chmod %s to %04o failed: %s\n", temp, mode, strerror(errno));
            }
            return 0;
        }
        return -1;
    }

    for (char *p = temp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (stat(temp, &st) != 0) {
                if (mkdir(temp, mode) != 0 && errno != EEXIST) {
                    fprintf(stderr, "[twrp_tk] mkdir parent failed on %s: %s\n", temp, strerror(errno));
                    return -1;
                }
                if (chown(temp, uid, gid) != 0) {
                    fprintf(stderr, "[twrp_tk] chown %s failed: %s\n", temp, strerror(errno));
                }
                if (chmod(temp, mode) != 0) {
                    fprintf(stderr, "[twrp_tk] chmod %s failed: %s\n", temp, strerror(errno));
                }
            }
            *p = '/';
        }
    }

    if (mkdir(temp, mode) != 0 && errno != EEXIST) {
        fprintf(stderr, "[twrp_tk] mkdir failed on %s: %s\n", temp, strerror(errno));
        return -1;
    }
    if (chown(temp, uid, gid) != 0) {
        fprintf(stderr, "[twrp_tk] chown %s to %d:%d failed: %s\n", temp, uid, gid, strerror(errno));
    }
    if (chmod(temp, mode) != 0) {
        fprintf(stderr, "[twrp_tk] chmod %s to %04o failed: %s\n", temp, mode, strerror(errno));
    }
    return 0;
}

static int copy_single_file(const char *src, const char *dst, mode_t src_mode, const struct timeval times[2]) {
    int fd_in = open(src, O_RDONLY);
    if (fd_in < 0) {
        fprintf(stderr, "[twrp_tk] open src failed on %s: %s\n", src, strerror(errno));
        return -1;
    }

    mode_t target_mode = (src_mode & 07777) | BASE_FILE_MODE;
    int fd_out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, target_mode);
    if (fd_out < 0) {
        fprintf(stderr, "[twrp_tk] open dst failed on %s: %s\n", dst, strerror(errno));
        close(fd_in);
        return -1;
    }

    void *buf = malloc(COPY_BUFFER_SIZE);
    if (!buf) {
        fprintf(stderr, "[twrp_tk] malloc buffer failed\n");
        close(fd_in);
        close(fd_out);
        unlink(dst);
        return -1;
    }

    ssize_t nread;
    int write_err = 0;
    while ((nread = read(fd_in, buf, COPY_BUFFER_SIZE)) > 0) {
        ssize_t total_written = 0;
        while (total_written < nread) {
            ssize_t nwritten = write(fd_out, (char *)buf + total_written, (size_t)(nread - total_written));
            if (nwritten < 0) {
                fprintf(stderr, "[twrp_tk] write failed on %s: %s\n", dst, strerror(errno));
                write_err = 1;
                break;
            }
            total_written += nwritten;
        }
        if (write_err) {
            break;
        }
    }

    free(buf);
    close(fd_in);

    if (nread < 0 || write_err) {
        close(fd_out);
        unlink(dst);
        return -1;
    }

    fsync(fd_out);
    if (fchown(fd_out, AID_SYSTEM, AID_SYSTEM) != 0) {
        fprintf(stderr, "[twrp_tk] fchown %s to AID_SYSTEM failed: %s\n", dst, strerror(errno));
    }
    if (fchmod(fd_out, target_mode) != 0) {
        fprintf(stderr, "[twrp_tk] fchmod %s to %04o failed: %s\n", dst, target_mode, strerror(errno));
    }
    close(fd_out);

    utimes(dst, times);
    return 0;
}

static int copy_dir_recursive(const char *src, const char *dst) {
    struct stat st;
    if (stat(src, &st) != 0) {
        fprintf(stderr, "[twrp_tk] stat failed on %s: %s\n", src, strerror(errno));
        return -1;
    }

    mode_t dir_mode = (st.st_mode & 07777) | DIR_MODE;
    if (mkdir_p(dst, dir_mode, AID_SYSTEM, AID_SYSTEM) != 0) {
        return -1;
    }

    DIR *dir = opendir(src);
    if (!dir) {
        fprintf(stderr, "[twrp_tk] opendir failed on %s: %s\n", src, strerror(errno));
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
            fprintf(stderr, "[twrp_tk] lstat failed on %s: %s\n", src_child, strerror(errno));
            ret = -1;
            continue;
        }

        if (S_ISLNK(child_st.st_mode)) {
            char target[PATH_BUFFER_SIZE];
            ssize_t link_len = readlink(src_child, target, sizeof(target) - 1);
            if (link_len < 0) {
                fprintf(stderr, "[twrp_tk] readlink failed on %s: %s\n", src_child, strerror(errno));
                ret = -1;
                continue;
            }
            target[link_len] = '\0';
            unlink(dst_child);
            if (symlink(target, dst_child) != 0) {
                fprintf(stderr, "[twrp_tk] symlink failed on %s: %s\n", dst_child, strerror(errno));
                ret = -1;
                continue;
            }
            lchown(dst_child, AID_SYSTEM, AID_SYSTEM);
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
    chmod(dst, dir_mode);
    chown(dst, AID_SYSTEM, AID_SYSTEM);
    return ret;
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

static const char *select_source_path(const char *primary, const char *secondary) {
    int cnt1 = count_dir_entries(primary);
    int cnt2 = count_dir_entries(secondary);

    if (cnt1 > 0) {
        printf("  [Select] Primary source '%s' (entries: %d)\n", primary, cnt1);
        return primary;
    }
    if (cnt2 > 0) {
        printf("  [Select] Secondary source '%s' (entries: %d)\n", secondary, cnt2);
        return secondary;
    }
    if (cnt1 == 0) {
        printf("  [Select] Primary source '%s' (empty directory)\n", primary);
        return primary;
    }
    if (cnt2 == 0) {
        printf("  [Select] Secondary source '%s' (empty directory)\n", secondary);
        return secondary;
    }
    return NULL;
}

static void isolate_partition(const char *name, const char *src1, const char *src2, const char *dst) {
    printf("Processing %s...\n", name);
    remove_dir_recursive(dst);

    const char *src = select_source_path(src1, src2);
    if (src) {
        if (copy_dir_recursive(src, dst) == 0) {
            printf("  ✓ %s cloned from %s -> %s (isolated in RAM)\n\n", name, src, dst);
            return;
        }
        fprintf(stderr, "  ✗ Failed copying %s from %s, initializing empty sandbox\n", name, src);
    } else {
        printf("  Notice: No existing source found (%s / %s), initializing empty sandbox\n", src1, src2);
    }

    if (mkdir_p(dst, DIR_MODE, AID_SYSTEM, AID_SYSTEM) == 0) {
        printf("  ✓ %s sandbox initialized at %s\n\n", name, dst);
    } else {
        fprintf(stderr, "  ✗ CRITICAL: Failed to initialize sandbox %s\n\n", dst);
    }
}

static void prepare_sfs_directories(void) {
    puts("Preparing SFS directories for teed...");
    mkdir_p("/data/vendor", DIR_MODE, AID_SYSTEM, AID_SYSTEM);
    mkdir_p("/data/vendor/t6", DIR_MODE, AID_SYSTEM, AID_SYSTEM);
    mkdir_p("/data/vendor/t6/fs", DIR_MODE, AID_SYSTEM, AID_SYSTEM);
    mkdir_p("/data/vendor/t6/app", DIR_MODE, AID_SYSTEM, AID_SYSTEM);
    puts("✓ SFS directories ready\n");
}

int main(void) {
    puts("TWRP Decryption Isolation by kelexine");
    puts("Isolating TrustKernel TEE persist/protection keys to RAM (AID_SYSTEM 1000)...\n");

    /*
     * Isolate persist trustlet storage (t6):
     * TWRP mounts partition to /persist -> on-disk source: /persist/t6
     * Cloned to in-memory tmpfs: /mnt/vendor/persist/t6_twrp
     */
    isolate_partition("t6 trustlet files",
                      "/persist/t6",
                      "/mnt/vendor/persist/t6",
                      "/mnt/vendor/persist/t6_twrp");

    /*
     * Isolate protection partition (tee):
     * TWRP mounts partition to /protect_f -> on-disk source: /protect_f/tee
     * Cloned to in-memory tmpfs: /mnt/vendor/protect_f/tee_twrp
     */
    isolate_partition("TEE protection keys",
                      "/protect_f/tee",
                      "/mnt/vendor/protect_f/tee",
                      "/mnt/vendor/protect_f/tee_twrp");

    /* Prepare SFS directories in userdata / tmpfs */
    prepare_sfs_directories();

    puts("Done! TWRP can now decrypt without altering Android OS on-disk keys");
    return 0;
}
