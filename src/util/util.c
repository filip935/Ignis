#define _GNU_SOURCE
#include "util.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <errno.h>

static int exec_impl(ExecResult *out, Error *err, const char *cmd, char *const argv[]) {
    (void)cmd;
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};

    if (out) {
        if (pipe(stdout_pipe) < 0) ERR_RETURN(err, ERR_UNKNOWN, "pipe failed: %s", strerror(errno));
        if (pipe(stderr_pipe) < 0) { close(stdout_pipe[0]); close(stdout_pipe[1]); ERR_RETURN(err, ERR_UNKNOWN, "pipe failed: %s", strerror(errno)); }
    }

    pid_t pid = fork();
    if (pid < 0) {
        if (out) { close(stdout_pipe[0]); close(stdout_pipe[1]); close(stderr_pipe[0]); close(stderr_pipe[1]); }
        ERR_RETURN(err, ERR_UNKNOWN, "fork failed: %s", strerror(errno));
    }

    if (pid == 0) {
        if (out) {
            close(stdout_pipe[0]); dup2(stdout_pipe[1], STDOUT_FILENO); close(stdout_pipe[1]);
            close(stderr_pipe[0]); dup2(stderr_pipe[1], STDERR_FILENO); close(stderr_pipe[1]);
        } else {
            int fd = open("/dev/null", O_WRONLY);
            if (fd >= 0) { dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO); close(fd); }
        }
        execvp(argv[0], argv);
        _exit(127);
    }

    if (out) {
        close(stdout_pipe[1]); close(stderr_pipe[1]);
    }

    struct pollfd fds[2];
    char buf[4096];
    int total_stdout = 0, total_stderr = 0;

    if (out) {
        out->stdout_buf = NULL;
        out->stderr_buf = NULL;
        out->stdout_len = 0;
        out->stderr_len = 0;

        int stdout_done = 0, stderr_done = 0;
        size_t stdout_cap = 4096, stderr_cap = 4096;
        out->stdout_buf = malloc(stdout_cap);
        out->stderr_buf = malloc(stderr_cap);
        if (!out->stdout_buf || !out->stderr_buf) {
            free(out->stdout_buf); free(out->stderr_buf);
            out->stdout_buf = NULL; out->stderr_buf = NULL;
            close(stdout_pipe[0]); close(stderr_pipe[0]);
            waitpid(pid, NULL, 0);
            ERR_RETURN(err, ERR_OUT_OF_MEMORY, "out of memory");
        }

        while (!stdout_done || !stderr_done) {
            fds[0].fd = stdout_pipe[0];
            fds[0].events = stdout_done ? 0 : POLLIN;
            fds[1].fd = stderr_pipe[0];
            fds[1].events = stderr_done ? 0 : POLLIN;

            int ret = poll(fds, 2, 5000);
            if (ret < 0) break;
            if (ret == 0) break;

            if (fds[0].revents & POLLIN) {
                int n = read(stdout_pipe[0], buf, sizeof(buf));
                if (n > 0) {
                    if (total_stdout + n + 1 > (int)stdout_cap) {
                        stdout_cap *= 2;
                        char *tmp = realloc(out->stdout_buf, stdout_cap);
                        if (!tmp) break;
                        out->stdout_buf = tmp;
                    }
                    memcpy(out->stdout_buf + total_stdout, buf, n);
                    total_stdout += n;
                } else stdout_done = 1;
            } else if (fds[0].revents & (POLLHUP | POLLERR)) stdout_done = 1;

            if (fds[1].revents & POLLIN) {
                int n = read(stderr_pipe[0], buf, sizeof(buf));
                if (n > 0) {
                    if (total_stderr + n + 1 > (int)stderr_cap) {
                        stderr_cap *= 2;
                        char *tmp = realloc(out->stderr_buf, stderr_cap);
                        if (!tmp) break;
                        out->stderr_buf = tmp;
                    }
                    memcpy(out->stderr_buf + total_stderr, buf, n);
                    total_stderr += n;
                } else stderr_done = 1;
            } else if (fds[1].revents & (POLLHUP | POLLERR)) stderr_done = 1;
        }

        out->stdout_buf[total_stdout] = '\0';
        out->stderr_buf[total_stderr] = '\0';
        out->stdout_len = total_stdout;
        out->stderr_len = total_stderr;

        close(stdout_pipe[0]); close(stderr_pipe[0]);
    }

    int status;
    waitpid(pid, &status, 0);

    if (out) out->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

int exec_cmd_vargs(ExecResult *out, Error *err, const char *cmd, va_list args) {
    int n_args = 1;
    va_list count_args;
    va_copy(count_args, args);
    while (va_arg(count_args, const char *)) n_args++;
    va_end(count_args);

    char **argv = malloc((n_args + 1) * sizeof(char *));
    if (!argv) ERR_RETURN(err, ERR_OUT_OF_MEMORY, "out of memory");

    argv[0] = (char *)cmd;
    va_list copy;
    va_copy(copy, args);
    for (int i = 1; i < n_args; i++) argv[i] = va_arg(copy, char *);
    argv[n_args] = NULL;
    va_end(copy);

    int ret = exec_impl(out, err, cmd, argv);
    free(argv);
    return ret;
}

int exec_cmd(ExecResult *out, Error *err, const char *cmd, ...) {
    va_list args;
    va_start(args, cmd);
    int ret = exec_cmd_vargs(out, err, cmd, args);
    va_end(args);
    return ret;
}

void exec_result_free(ExecResult *res) {
    if (res) {
        free(res->stdout_buf);
        free(res->stderr_buf);
        memset(res, 0, sizeof(*res));
    }
}

int path_exists(const char *path) {
    return access(path, F_OK) == 0;
}

int mkdir_p(const char *path, mode_t mode) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, mode);
            *p = '/';
        }
    }
    return mkdir(tmp, mode);
}

char *temp_dir_template(const char *prefix) {
    char *template = NULL;
    if (asprintf(&template, "/tmp/winflash-%s-XXXXXX", prefix) < 0) return NULL;
    return template;
}

int wait_for_device(const char *path, int timeout_ms) {
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (path_exists(path)) return 0;
        usleep(100000);
        elapsed += 100;
    }
    return -1;
}

int rm_rf(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;

    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d) return -1;
        struct dirent *ent;
        while ((ent = readdir(d))) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            char child[4096];
            snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
            rm_rf(child);
        }
        closedir(d);
        rmdir(path);
    } else {
        unlink(path);
    }
    return 0;
}

int copy_file(const char *src, const char *dst) {
    int src_fd = open(src, O_RDONLY);
    if (src_fd < 0) return -1;

    struct stat st;
    if (stat(src, &st) != 0) { close(src_fd); return -1; }

    int dst_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 07777);
    if (dst_fd < 0) { close(src_fd); return -1; }

    char buf[65536];
    ssize_t n;
    while ((n = read(src_fd, buf, sizeof(buf))) > 0) {
        ssize_t total_written = 0;
        while (total_written < n) {
            ssize_t w = write(dst_fd, buf + total_written, n - total_written);
            if (w < 0) { close(src_fd); close(dst_fd); return -1; }
            total_written += w;
        }
    }
    close(src_fd);
    close(dst_fd);
    return (n < 0) ? -1 : 0;
}

int cp_count(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return 0;
    if (!S_ISDIR(st.st_mode)) return 1;

    int count = 1;
    DIR *d = opendir(path);
    if (!d) return 1;

    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char child[4096];
        snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
        count += cp_count(child);
    }
    closedir(d);
    return count;
}

int cp_progress(const char *src, const char *dst,
                volatile int *cancel,
                void (*progress_cb)(int percent, void *user_data),
                void *progress_user) {
    if (cancel && *cancel) return -1;

    int total = cp_count(src);
    if (total <= 0) return -1;
    if (total < 5) total = 5;

    int pipefd[2];
    if (pipe(pipefd) < 0) return -1;

    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return -1; }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execlp("stdbuf", "stdbuf", "-oL", "cp", "-av", src, dst, NULL);
        execlp("cp", "cp", "-av", src, dst, NULL);
        _exit(127);
    }

    close(pipefd[1]);

    FILE *f = fdopen(pipefd[0], "r");
    if (!f) { close(pipefd[0]); kill(pid, SIGTERM); waitpid(pid, NULL, 0); return -1; }

    int last_pct = -1;
    int count = 0;
    char buf[4096];

    while (fgets(buf, sizeof(buf), f)) {
        if (cancel && *cancel) {
            kill(pid, SIGTERM);
            fclose(f);
            waitpid(pid, NULL, 0);
            return -1;
        }
        count++;
        int pct = count * 100 / total;
        if (pct > 99) pct = 99;
        if (pct != last_pct) {
            last_pct = pct;
            if (progress_cb) progress_cb(pct, progress_user);
        }
    }

    fclose(f);

    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        if (progress_cb) progress_cb(100, progress_user);
        return 0;
    }
    return -1;
}
