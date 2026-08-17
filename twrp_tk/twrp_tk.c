/*
 * File: twrp_tk.c
 * Author: kelexine <https://github.com/kelexine>
 * Date: 2026-08-17
 * Purpose: TWRP Decryption Isolation utility for TrustKernel TEE
 *
 * Description:
 * Reconstructed via Ghidra MCP decompilation of the stripped static AArch64
 * binary at recovery/root/vendor/bin/trustkernel.twrp.
 *
 * Copies and isolates /mnt/vendor/persist/t6 -> /mnt/vendor/persist/t6_twrp
 * and /mnt/vendor/protect_f/tee -> /mnt/vendor/protect_f/tee_twrp so that
 * TWRP decryption operations do not alter Android OS persist/protection keys.
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

#define COPY_BUFFER_SIZE 65536
#define PATH_BUFFER_SIZE 4096

static void remove_dir_recursive(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        if (errno == ENOENT) {
            return;
        }
        fprintf(stderr, "stat failed on %s: %s\n", path, strerror(errno));
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) {
            fprintf(stderr, "opendir failed on %s: %s\n", path, strerror(errno));
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
        if (rmdir(path) != 0) {
            fprintf(stderr, "rmdir failed on %s: %s\n", path, strerror(errno));
        }
    } else {
        if (unlink(path) != 0) {
            fprintf(stderr, "unlink failed on %s: %s\n", path, strerror(errno));
        }
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
                    fprintf(stderr, "mkdir failed on %s: %s\n", temp, strerror(errno));
                    return -1;
                }
            }
            *p = '/';
        }
    }

    if (mkdir(temp, mode) != 0 && errno != EEXIST) {
        fprintf(stderr, "mkdir failed on %s: %s\n", temp, strerror(errno));
        return -1;
    }
    return 0;
}

static int copy_dir_recursive(const char *src, const char *dst) {
    struct stat st;
    if (stat(src, &st) != 0) {
        fprintf(stderr, "stat failed on %s: %s\n", src, strerror(errno));
        return -1;
    }

    if (mkdir_p(dst, st.st_mode) != 0) {
        return -1;
    }
    chown(dst, st.st_uid, st.st_gid);

    DIR *dir = opendir(src);
    if (!dir) {
        fprintf(stderr, "opendir failed on %s: %s\n", src, strerror(errno));
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
            fprintf(stderr, "lstat failed on %s: %s\n", src_child, strerror(errno));
            ret = -1;
            continue;
        }

        if (S_ISLNK(child_st.st_mode)) {
            char target[PATH_BUFFER_SIZE];
            ssize_t link_len = readlink(src_child, target, sizeof(target) - 1);
            if (link_len < 0) {
                fprintf(stderr, "readlink failed on %s: %s\n", src_child, strerror(errno));
                ret = -1;
                continue;
            }
            target[link_len] = '\0';
            if (symlink(target, dst_child) != 0) {
                fprintf(stderr, "symlink failed on %s: %s\n", dst_child, strerror(errno));
                ret = -1;
                continue;
            }
        } else if (S_ISDIR(child_st.st_mode)) {
            if (copy_dir_recursive(src_child, dst_child) != 0) {
                ret = -1;
            }
        } else if (S_ISREG(child_st.st_mode)) {
            struct stat file_st;
            if (stat(src_child, &file_st) != 0) {
                fprintf(stderr, "stat failed on %s: %s\n", src_child, strerror(errno));
                ret = -1;
                continue;
            }

            void *buf = malloc(COPY_BUFFER_SIZE);
            if (!buf) {
                fprintf(stderr, "malloc failed\n");
                ret = -1;
                continue;
            }

            int fd_in = open(src_child, O_RDONLY);
            if (fd_in < 0) {
                fprintf(stderr, "open failed on %s: %s\n", src_child, strerror(errno));
                free(buf);
                ret = -1;
                continue;
            }

            int fd_out = open(dst_child, O_WRONLY | O_CREAT | O_TRUNC, file_st.st_mode);
            if (fd_out < 0) {
                fprintf(stderr, "open failed on %s: %s\n", dst_child, strerror(errno));
                close(fd_in);
                free(buf);
                ret = -1;
                continue;
            }

            ssize_t nread;
            int write_err = 0;
            while ((nread = read(fd_in, buf, COPY_BUFFER_SIZE)) > 0) {
                ssize_t total_written = 0;
                while (total_written < nread) {
                    ssize_t nwritten = write(fd_out, (char *)buf + total_written, (size_t)(nread - total_written));
                    if (nwritten < 0) {
                        fprintf(stderr, "write failed on %s: %s\n", dst_child, strerror(errno));
                        write_err = 1;
                        break;
                    }
                    total_written += nwritten;
                }
                if (write_err) {
                    break;
                }
            }

            if (nread < 0) {
                fprintf(stderr, "read failed on %s: %s\n", src_child, strerror(errno));
                ret = -1;
            }
            if (write_err) {
                ret = -1;
            }

            if (nread >= 0 && !write_err) {
                fsync(fd_out);
                fchown(fd_out, file_st.st_uid, file_st.st_gid);
                fchmod(fd_out, file_st.st_mode);
                close(fd_in);
                close(fd_out);
                free(buf);

                struct timeval times[2];
                times[0].tv_sec = file_st.st_atime;
                times[0].tv_usec = 0;
                times[1].tv_sec = file_st.st_mtime;
                times[1].tv_usec = 0;
                utimes(dst_child, times);
            } else {
                close(fd_in);
                close(fd_out);
                free(buf);
            }
        }
    }

    closedir(dir);
    chmod(dst, st.st_mode);
    return ret;
}

int main(void) {
    puts("TWRP Decryption Isolation by kelexine");
    puts("Isolating decryption files for TWRP...\n");

    puts("[1/2] Processing t6 trustlet files...");
    remove_dir_recursive("/mnt/vendor/persist/t6_twrp");
    if (copy_dir_recursive("/mnt/vendor/persist/t6", "/mnt/vendor/persist/t6_twrp") == 0) {
        puts("✓ t6 files isolated\n");
    } else {
        puts("✗ Failed to isolate t6 files\n");
    }

    puts("[2/2] Processing TEE protection keys...");
    remove_dir_recursive("/mnt/vendor/protect_f/tee_twrp");
    if (copy_dir_recursive("/mnt/vendor/protect_f/tee", "/mnt/vendor/protect_f/tee_twrp") == 0) {
        puts("✓ TEE files isolated\n");
    } else {
        puts("✗ Failed to isolate TEE files\n");
    }

    puts("Done! TWRP can now decrypt without affecting Android OS");
    return 0;
}
