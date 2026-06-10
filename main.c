#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <signal.h>

#define RST "\033[0m"
#define RED "\033[31m"
#define GRN "\033[32m"
#define BLU "\033[34m"
#define YEL "\033[33m"

int is_root() {
    return (geteuid() == 0);
}

static int COLOR = 0;

int use_color() {
    char *term = getenv("TERM");
    if (!isatty(1)) return 0;
    if (!term) return 0;
    if (strcmp(term, "dumb") == 0) return 0;
    return 1;
}

#define LOGI(fmt, ...) \
    printf("%s[INFO]%s " fmt "\n", COLOR ? BLU : "", COLOR ? RST : "", ##__VA_ARGS__)

#define LOGS(fmt, ...) \
    printf("%s[OK]%s " fmt "\n", COLOR ? GRN : "", COLOR ? RST : "", ##__VA_ARGS__)

#define LOGE(fmt, ...) \
    printf("%s[ERROR]%s " fmt " | %s\n", COLOR ? RED : "", COLOR ? RST : "", ##__VA_ARGS__, strerror(errno))

#define LOGW(fmt, ...) \
    printf("%s[SKIP]%s " fmt "\n", COLOR ? YEL : "", COLOR ? RST : "", ##__VA_ARGS__)

static void sig_handler(int sig) {
    const char *msg = "";
    switch(sig) {
        case SIGINT:  msg = "\n[CANCEL] Process interrupted by user (SIGINT)\n"; break;
        case SIGTERM: msg = "\n[CANCEL] Process terminated (SIGTERM)\n"; break;
        case SIGSYS:  msg = "\n[ERROR] Bad system call (SIGSYS) - Seccomp violation?\n"; break;
    }
    write(STDERR_FILENO, msg, strlen(msg));
    _exit(128 + sig);
}

void setup_signals() {
    struct sigaction sa;
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGSYS, &sa, NULL);
}

#include "blank_mp3.h"
#include "blank_wav.h"
#include "blank_m4a.h"
#include "blank_ogg.h"
#include "blank_opus.h"
#include "blank_flac.h"
#include "blank_aac.h"

#define MAX_PATH 1024

const char* get_ext(const char *name) {
    if (!name) return NULL;
    const char *dot = strrchr(name, '.');
    return (dot && dot != name && dot[1]) ? dot + 1 : NULL;
}

unsigned char* get_data(const char *ext, unsigned int *len) {
    if (!ext || !len) return NULL;

    if (strcmp(ext, "mp3") == 0)  { *len = blank_mp3_len;  return blank_mp3; }
    if (strcmp(ext, "wav") == 0)  { *len = blank_wav_len;  return blank_wav; }
    if (strcmp(ext, "m4a") == 0)  { *len = blank_m4a_len;  return blank_m4a; }
    if (strcmp(ext, "ogg") == 0)  { *len = blank_ogg_len;  return blank_ogg; }
    if (strcmp(ext, "opus") == 0) { *len = blank_opus_len; return blank_opus; }
    if (strcmp(ext, "flac") == 0) { *len = blank_flac_len; return blank_flac; }
    if (strcmp(ext, "aac") == 0)  { *len = blank_aac_len;  return blank_aac; }

    return NULL;
}

ssize_t write_all(int fd, const void *buf, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, (const char*)buf + written, len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        written += n;
    }
    return (ssize_t)written;
}

int already_patched(const char *path, const unsigned char *data, unsigned int len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;

    struct stat st;
    if (fstat(fd, &st) != 0 || (size_t)st.st_size != len) {
        close(fd);
        return 0;
    }

    unsigned char *buf = malloc(len);
    if (!buf) { close(fd); return 0; }

    ssize_t r = read(fd, buf, len);
    close(fd);

    if (r != (ssize_t)len) { free(buf); return 0; }

    int match = (memcmp(buf, data, len) == 0);
    free(buf);
    return match;
}

int write_file(const char *path, unsigned char *data, unsigned int len) {
    if (!path || !data || len == 0) return -1;

    struct stat st;
    int has_stat = (stat(path, &st) == 0);

    if (already_patched(path, data, len)) {
        LOGW("already patched, skipping: %s", path);
        return 2;
    }

    char tmp[MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        LOGE("failed to open temp file: %s", tmp);
        return -1;
    }

    ssize_t w = write_all(fd, data, len);

    if (w != (ssize_t)len) {
        LOGE("failed to write data to temp file: %s", tmp);
        close(fd); unlink(tmp);
        return -1;
    }

    fsync(fd);
    close(fd);

    if (rename(tmp, path) != 0) {
        LOGE("failed to replace original file: %s", path);
        unlink(tmp);
        return -1;
    }

    if (has_stat) {
        if (chmod(path, st.st_mode) != 0) {
            LOGE("failed to restore file permissions: %s", path);
        }
        if (chown(path, st.st_uid, st.st_gid) != 0) {
            LOGE("failed to restore file ownership: %s", path);
        }
    }

    LOGS("successfully patched: %s", path);
    return 0;
}

int main() {
    COLOR = use_color();
    
    setup_signals();

    LOGI("starting audio patcher");

    if (!is_root()) {
        LOGE("root privileges required");
        return 1;
    }

    const char *dir =
        "/data/data/com.google.android.dialer/files/callrecordingprompt";

    DIR *d = opendir(dir);
    if (!d) {
        LOGE("failed to open directory: %s", dir);
        return 1;
    }

    struct dirent *e;
    int ok = 0, fail = 0, skip = 0;

    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;

        const char *ext = get_ext(e->d_name);
        if (!ext) { 
            LOGW("no valid extension, skipping: %s", e->d_name);
            skip++; 
            continue; 
        }

        unsigned int len = 0;
        unsigned char *data = get_data(ext, &len);

        if (!data) { 
            LOGW("unsupported format (%s), skipping: %s", ext, e->d_name);
            skip++; 
            continue; 
        }

        char path[MAX_PATH];
        int n = snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);

        if (n < 0 || n >= (int)sizeof(path)) {
            LOGE("path length exceeds buffer limit: %s/%s", dir, e->d_name);
            fail++;
            continue;
        }

        int res = write_file(path, data, len);
        if (res == 0) ok++;
        else if (res == 2) skip++;
        else fail++;
    }

    closedir(d);

    LOGI("done | ok=%d fail=%d skip=%d", ok, fail, skip);

    return 0;
}
