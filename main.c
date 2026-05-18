#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifndef BUILD_DATE
#define BUILD_DATE "unknown"
#endif

static const char build_info[] =
"built by merbah3266 | build date: " BUILD_DATE;

__attribute__((used))
static void keep_build_info(void) {
    asm volatile("" :: "r"(build_info));
}

#define RST "\033[0m"
#define RED "\033[31m"
#define GRN "\033[32m"
#define BLU "\033[34m"

int is_root() {
    return (geteuid() == 0);
}

int COLOR = 0;

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

#include "blank_mp3.h"
#include "blank_wav.h"
#include "blank_m4a.h"
#include "blank_ogg.h"
#include "blank_opus.h"
#include "blank_flac.h"
#include "blank_aac.h"

#define MAX_PATH 1024

const char* get_ext(const char *name) {
    const char *dot = NULL;

    for (int i = 0; name[i]; i++) {
        if (name[i] == '.')
            dot = &name[i];
    }

    return (dot && dot != name) ? dot + 1 : NULL;
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

int write_file(const char *path, unsigned char *data, unsigned int len) {

    if (!path || !data || len == 0)
        return -1;

    int fd = open(path, O_WRONLY | O_TRUNC);
    if (fd < 0) {
        LOGE("open failed: %s", path);
        return -1;
    }

    ssize_t w = write(fd, data, len);

    if (w != (ssize_t)len) {
        LOGE("write failed: %s", path);
        close(fd);
        return -1;
    }

    fsync(fd);
    close(fd);

    LOGS("%s", path);
    return 0;
}

int main() {

    COLOR = use_color();

    LOGI("starting audio patcher");

    if (!is_root()) {
        LOGE("root required");
        return 1;
    }

    const char *dir =
        "/data/data/com.google.android.dialer/files/callrecordingprompt";

    DIR *d = opendir(dir);
    if (!d) {
        LOGE("opendir failed: %s", dir);
        return 1;
    }

    struct dirent *e;

    int ok = 0, fail = 0, skip = 0;

    while ((e = readdir(d)) != NULL) {

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;

        const char *ext = get_ext(e->d_name);
        if (!ext) { skip++; continue; }

        unsigned int len = 0;
        unsigned char *data = get_data(ext, &len);

        if (!data) { skip++; continue; }

        char path[MAX_PATH];

        int n = snprintf(path, sizeof(path),
                         "%s/%s", dir, e->d_name);

        if (n < 0 || n >= (int)sizeof(path)) {
            LOGE("path overflow detected");
            fail++;
            continue;
        }

        if (write_file(path, data, len) == 0)
            ok++;
        else
            fail++;
    }

    closedir(d);

    LOGI("done | ok=%d fail=%d skip=%d", ok, fail, skip);

    return 0;
}