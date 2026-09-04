/*
 * File: twrp_tk.c
 * Author: kelexine <https://github.com/kelexine>
 * Date: 2026-09-04
 * Purpose: TWRP Decryption Isolation utility for TrustKernel TEE
 *
 * Description:
 * Probes candidate mountpoints for TrustKernel TEE keys (/protect_f/tee and /persist/t6).
 * If partitions are mounted with valid files, isolates them into RAM tmpfs to prevent
 * Android OS flash mutation during recovery decryption. If missing or unmounted,
 * fails fast with detailed audit logs to stdout, stderr, and /dev/kmsg.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
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
#define CANARY_FILENAME  "out"

#define DEST_PERSIST_TWRP "/mnt/vendor/persist/t6_twrp"
#define DEST_PROTECT_TWRP "/mnt/vendor/protect_f/tee_twrp"

static const char *const PROTECT_CANDIDATES[] = {
    "/protect_f/tee",
    "/mnt/vendor/protect_f/tee"
};

static const char *const PERSIST_CANDIDATES[] = {
    "/persist/t6",
    "/mnt/vendor/persist/t6"
};

static void audit_log(const char *level, const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    FILE *out = (strcmp(level, "ERR") == 0) ? stderr : stdout;
    fprintf(out, "[twrp_tk] [%s] %s\n", level, buf);
    fflush(out);

    int kmsg_fd = open("/dev/kmsg", O_WRONLY | O_NOCTTY);
    if (kmsg_fd >= 0) {
        char kmsg_buf[1150];
        int prio = (strcmp(level, "ERR") == 0) ? 3 : 5;
        snprintf(kmsg_buf, sizeof(kmsg_buf), "<%d>twrp_tk: [%s] %s\n", prio, level, buf);
        write(kmsg_fd, kmsg_buf, strlen(kmsg_buf));
        close(kmsg_fd);
    }
}

static void remove_dir_recursive(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return;
    }

    if (!S_ISDIR(st.st_mode)) {
        unlink(path);
        return;
    }

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
}

static int mkdir_p(const char *path, mode_t mode) {
    char temp[PATH_BUFFER_SIZE];
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
            if (stat(temp, &st) != 0 && mkdir(temp, mode) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    return (mkdir(temp, mode) != 0 && errno != EEXIST) ? -1 : 0;
}

static int copy_file_content(int fd_in, int fd_out) {
    char *buf = malloc(COPY_BUFFER_SIZE);
    if (!buf) {
        return -1;
    }

    ssize_t nread;
    int status = 0;
    while ((nread = read(fd_in, buf, COPY_BUFFER_SIZE)) > 0) {
        ssize_t written = 0;
        while (written < nread) {
            ssize_t res = write(fd_out, buf + written, (size_t)(nread - written));
            if (res < 0) {
                status = -1;
                break;
            }
            written += res;
        }
        if (status < 0) {
            break;
        }
    }
    free(buf);
    return (nread < 0 || status < 0) ? -1 : 0;
}

static int copy_single_file(const char *src, const char *dst, mode_t mode, uid_t uid, gid_t gid) {
    int fd_in = open(src, O_RDONLY);
    if (fd_in < 0) {
        return -1;
    }

    int fd_out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd_out < 0) {
        close(fd_in);
        return -1;
    }

    int ret = copy_file_content(fd_in, fd_out);
    if (ret == 0) {
        fchown(fd_out, uid, gid);
        fchmod(fd_out, mode);
    }
    close(fd_in);
    close(fd_out);
    return ret;
}

static int copy_dir_recursive(const char *src, const char *dst) {
    struct stat st;
    if (stat(src, &st) != 0 || mkdir_p(dst, st.st_mode) != 0) {
        return -1;
    }
    chown(dst, st.st_uid, st.st_gid);

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

        struct stat cst;
        if (lstat(src_child, &cst) != 0) {
            ret = -1;
            continue;
        }

        if (S_ISDIR(cst.st_mode)) {
            if (copy_dir_recursive(src_child, dst_child) != 0) {
                ret = -1;
            }
        } else if (S_ISREG(cst.st_mode)) {
            if (copy_single_file(src_child, dst_child, cst.st_mode, cst.st_uid, cst.st_gid) != 0) {
                ret = -1;
            }
        }
    }
    closedir(dir);
    return ret;
}

static int probe_directory(const char *path, char *resolved, size_t res_sz) {
    struct stat st;
    if (stat(path, &st) != 0) {
        audit_log("DEBUG", "probe: %s not accessible (%s)", path, strerror(errno));
        return 0;
    }
    if (!S_ISDIR(st.st_mode)) {
        audit_log("WARN", "probe: %s is not a directory", path);
        return 0;
    }

    DIR *d = opendir(path);
    if (!d) {
        audit_log("WARN", "probe: opendir failed on %s (%s)", path, strerror(errno));
        return 0;
    }

    int count = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) {
            count++;
        }
    }
    closedir(d);

    if (count == 0) {
        audit_log("WARN", "probe: %s exists but is empty", path);
        return 0;
    }

    snprintf(resolved, res_sz, "%s", path);
    audit_log("INFO", "probe: confirmed %s (%d files)", path, count);
    return 1;
}

static const char *resolve_source(const char *const candidates[], size_t num, char *out, size_t sz) {
    for (size_t i = 0; i < num; i++) {
        if (probe_directory(candidates[i], out, sz)) {
            return out;
        }
    }
    return NULL;
}

static void write_canary(const char *dir) {
    char path[PATH_BUFFER_SIZE];
    snprintf(path, sizeof(path), "%s/%s", dir, CANARY_FILENAME);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0660);
    if (fd >= 0) {
        write(fd, "OK\n", 3);
        close(fd);
    }
}

static void restart_teed_if_running(void) {
    char line[128];
    FILE *fp = popen("getprop init.svc.teed 2>/dev/null", "r");
    if (!fp) {
        return;
    }
    if (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "running", 7) == 0) {
            audit_log("INFO", "teed is currently running, restarting...");
            system("setprop ctl.restart teed");
        }
    }
    pclose(fp);
}

int main(void) {
    audit_log("INFO", "TWRP TrustKernel Decryption Isolation starting");

    char protect_src[PATH_BUFFER_SIZE];
    const char *p_src = resolve_source(PROTECT_CANDIDATES, sizeof(PROTECT_CANDIDATES) / sizeof(char *),
                                       protect_src, sizeof(protect_src));

    char persist_src[PATH_BUFFER_SIZE];
    const char *t_src = resolve_source(PERSIST_CANDIDATES, sizeof(PERSIST_CANDIDATES) / sizeof(char *),
                                       persist_src, sizeof(persist_src));

    if (!p_src && !t_src) {
        audit_log("ERR", "FATAL: Neither protect_f nor persist partitions are mounted with valid files");
        audit_log("ERR", "Ensure partitions are mounted (e.g. via mounttodecrypt=1 or TWRP mount menu)");
        return 1;
    }

    if (t_src) {
        audit_log("INFO", "Isolating persist trustlets: %s -> %s", t_src, DEST_PERSIST_TWRP);
        remove_dir_recursive(DEST_PERSIST_TWRP);
        if (copy_dir_recursive(t_src, DEST_PERSIST_TWRP) == 0) {
            write_canary(DEST_PERSIST_TWRP);
            audit_log("INFO", "persist trustlets successfully isolated");
        } else {
            audit_log("ERR", "Failed to isolate persist trustlets from %s", t_src);
        }
    }

    if (p_src) {
        audit_log("INFO", "Isolating protect_f TEE keys: %s -> %s", p_src, DEST_PROTECT_TWRP);
        remove_dir_recursive(DEST_PROTECT_TWRP);
        if (copy_dir_recursive(p_src, DEST_PROTECT_TWRP) == 0) {
            write_canary(DEST_PROTECT_TWRP);
            audit_log("INFO", "protect_f TEE keys successfully isolated");
        } else {
            audit_log("ERR", "Failed to isolate protect_f TEE keys from %s", p_src);
        }
    }

    restart_teed_if_running();
    audit_log("INFO", "Isolation complete. Secure storage ready for TWRP decryption");
    return 0;
}
