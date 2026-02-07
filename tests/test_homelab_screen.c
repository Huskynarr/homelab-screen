/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 homelab-screen contributors
 */

#define _GNU_SOURCE
#define TESTING 1

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* ===== libc passthrough wrappers (defined before macro overrides) ===== */

static FILE *libc_fopen(const char *path, const char *mode) { return fopen(path, mode); }
static DIR *libc_opendir(const char *path) { return opendir(path); }
static struct dirent *libc_readdir(DIR *d) { return readdir(d); }
static int libc_closedir(DIR *d) { return closedir(d); }
static FILE *libc_popen(const char *cmd, const char *type) { return popen(cmd, type); }
static int libc_pclose(FILE *fp) { return pclose(fp); }
static int libc_access(const char *path, int mode) { return access(path, mode); }
static int libc_gethostname(char *name, size_t len) { return gethostname(name, len); }
static time_t libc_time(time_t *tloc) { return time(tloc); }
static struct tm *libc_localtime_r(const time_t *timer, struct tm *result) {
    return localtime_r(timer, result);
}
static int libc_nanosleep(const struct timespec *req, struct timespec *rem) {
    return nanosleep(req, rem);
}
static int libc_vsnprintf(char *str, size_t size, const char *fmt, va_list ap) {
    return vsnprintf(str, size, fmt, ap);
}

/* ===== test double state ===== */

#define MAX_MOCK_FILES 64
#define MAX_MOCK_CMDS 32
#define MAX_MOCK_PROCS 32
#define MAX_MOCK_ACCESS 16
#define MAX_MOCK_TIMES 32

typedef struct {
    int enabled;
    char path[256];
    char content[4096];
    int fail_open;
} MockFile;

typedef struct {
    int enabled;
    int prefix_match;
    char cmd[256];
    char output[16384];
    int status;
} MockCmd;

typedef struct {
    FILE *fp;
    int status;
} MockProc;

typedef struct {
    int enabled;
    char path[128];
    int rc;
} MockAccess;

static MockFile g_mock_files[MAX_MOCK_FILES];
static MockCmd g_mock_cmds[MAX_MOCK_CMDS];
static MockProc g_mock_procs[MAX_MOCK_PROCS];
static MockAccess g_mock_access[MAX_MOCK_ACCESS];

static int g_mock_fs_enabled = 0;
static int g_mock_proc_enabled = 0;
static int g_mock_access_enabled = 0;
static int g_mock_hostname_enabled = 0;
static int g_mock_hostname_rc = 0;
static char g_mock_hostname_value[128] = "test-host";

static int g_mock_time_enabled = 0;
static time_t g_mock_times[MAX_MOCK_TIMES];
static int g_mock_time_count = 0;
static int g_mock_time_idx = 0;

static int g_mock_localtime_force_null = 0;
static int g_mock_localtime_null_once = 0;

static int g_mock_nanosleep_enabled = 0;
static int g_mock_nanosleep_calls = 0;
static int g_mock_nanosleep_stop_after = 0;

static int g_mock_snprintf_fail_enabled = 0;
static int g_mock_snprintf_fail_once = 0;
static char g_mock_snprintf_fail_substr[128] = "";

static int g_expect_exit = 0;
static int g_exit_called = 0;
static int g_exit_code = -1;
static jmp_buf g_exit_jmp;

static int g_use_mock_net_dir = 0;
static char g_mock_net_dir[PATH_MAX] = "";

extern volatile sig_atomic_t g_running;

/* ===== test doubles used by production code ===== */

static FILE *make_read_stream(const char *content) {
    FILE *f = tmpfile();
    if (!f) {
        return NULL;
    }
    if (content && content[0] != '\0') {
        (void)fputs(content, f);
    }
    rewind(f);
    return f;
}

static FILE *test_fopen(const char *path, const char *mode) {
    if (!g_mock_fs_enabled) {
        return libc_fopen(path, mode);
    }

    for (int i = 0; i < MAX_MOCK_FILES; i++) {
        if (g_mock_files[i].enabled && strcmp(g_mock_files[i].path, path) == 0) {
            if (g_mock_files[i].fail_open) {
                return NULL;
            }
            return make_read_stream(g_mock_files[i].content);
        }
    }

    return NULL;
}

static DIR *test_opendir(const char *path) {
    if (g_use_mock_net_dir && strcmp(path, "/sys/class/net") == 0) {
        return libc_opendir(g_mock_net_dir);
    }
    return libc_opendir(path);
}

static struct dirent *test_readdir(DIR *d) {
    return libc_readdir(d);
}

static int test_closedir(DIR *d) {
    return libc_closedir(d);
}

static int cmd_matches(const MockCmd *m, const char *cmd) {
    if (!m->enabled) {
        return 0;
    }
    if (m->prefix_match) {
        size_t n = strlen(m->cmd);
        return strncmp(cmd, m->cmd, n) == 0;
    }
    return strcmp(m->cmd, cmd) == 0;
}

static FILE *test_popen(const char *cmd, const char *type) {
    (void)type;

    if (!g_mock_proc_enabled) {
        return libc_popen(cmd, type);
    }

    for (int i = 0; i < MAX_MOCK_CMDS; i++) {
        if (!cmd_matches(&g_mock_cmds[i], cmd)) {
            continue;
        }

        FILE *f = make_read_stream(g_mock_cmds[i].output);
        if (!f) {
            return NULL;
        }

        for (int j = 0; j < MAX_MOCK_PROCS; j++) {
            if (g_mock_procs[j].fp == NULL) {
                g_mock_procs[j].fp = f;
                g_mock_procs[j].status = g_mock_cmds[i].status;
                return f;
            }
        }

        fclose(f);
        return NULL;
    }

    return NULL;
}

static int test_pclose(FILE *fp) {
    if (!g_mock_proc_enabled) {
        return libc_pclose(fp);
    }

    for (int i = 0; i < MAX_MOCK_PROCS; i++) {
        if (g_mock_procs[i].fp == fp) {
            int status = g_mock_procs[i].status;
            g_mock_procs[i].fp = NULL;
            g_mock_procs[i].status = 0;
            fclose(fp);
            return status;
        }
    }

    return -1;
}

static int test_access(const char *path, int mode) {
    if (!g_mock_access_enabled) {
        return libc_access(path, mode);
    }
    for (int i = 0; i < MAX_MOCK_ACCESS; i++) {
        if (g_mock_access[i].enabled && strcmp(g_mock_access[i].path, path) == 0) {
            (void)mode;
            return g_mock_access[i].rc;
        }
    }
    return -1;
}

static int test_gethostname(char *name, size_t len) {
    if (!g_mock_hostname_enabled) {
        return libc_gethostname(name, len);
    }
    if (g_mock_hostname_rc != 0) {
        return g_mock_hostname_rc;
    }
    if (len == 0) {
        return -1;
    }
    snprintf(name, len, "%s", g_mock_hostname_value);
    return 0;
}

static time_t test_time(time_t *tloc) {
    time_t now;
    if (!g_mock_time_enabled || g_mock_time_count == 0) {
        now = libc_time(NULL);
    } else if (g_mock_time_idx < g_mock_time_count) {
        now = g_mock_times[g_mock_time_idx++];
    } else {
        now = g_mock_times[g_mock_time_count - 1];
    }

    if (tloc) {
        *tloc = now;
    }
    return now;
}

static struct tm *test_localtime_r(const time_t *timer, struct tm *result) {
    if (g_mock_localtime_force_null) {
        if (g_mock_localtime_null_once) {
            g_mock_localtime_force_null = 0;
        }
        return NULL;
    }
    return libc_localtime_r(timer, result);
}

static int test_nanosleep(const struct timespec *req, struct timespec *rem) {
    if (!g_mock_nanosleep_enabled) {
        return libc_nanosleep(req, rem);
    }
    (void)req;
    (void)rem;
    g_mock_nanosleep_calls++;
    if (g_mock_nanosleep_stop_after > 0 && g_mock_nanosleep_calls >= g_mock_nanosleep_stop_after) {
        g_running = 0;
    }
    return 0;
}

static int test_snprintf(char *str, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int rc = libc_vsnprintf(str, size, fmt, ap);
    va_end(ap);

    if (g_mock_snprintf_fail_enabled) {
        int matched = (g_mock_snprintf_fail_substr[0] == '\0') ||
            (str && strstr(str, g_mock_snprintf_fail_substr) != NULL);
        if (matched) {
            if (!g_mock_snprintf_fail_once || g_mock_snprintf_fail_once == 1) {
                if (g_mock_snprintf_fail_once == 1) {
                    g_mock_snprintf_fail_once = 2;
                }
                return -1;
            }
        }
    }

    return rc;
}

__attribute__((noreturn))
static void test_exit(int code) {
    g_exit_called = 1;
    g_exit_code = code;
    if (g_expect_exit) {
        longjmp(g_exit_jmp, 1);
    }
    abort();
}

/* Suppress main() from homelab-screen.c and redirect libc calls to test doubles */
#define main homelab_screen_main
#define fopen test_fopen
#define opendir test_opendir
#define readdir test_readdir
#define closedir test_closedir
#define popen test_popen
#define pclose test_pclose
#define access test_access
#define gethostname test_gethostname
#define time test_time
#define localtime_r test_localtime_r
#define nanosleep test_nanosleep
#ifdef snprintf
#undef snprintf
#endif
#define snprintf test_snprintf
#define exit test_exit

#include "../homelab-screen.c"

#undef exit
#undef snprintf
#undef sigaction
#undef sigemptyset
#undef nanosleep
#undef localtime_r
#undef time
#undef gethostname
#undef access
#undef pclose
#undef popen
#undef closedir
#undef readdir
#undef opendir
#undef fopen
#undef main

/* ===== minimal test harness ===== */

static int g_pass = 0;
static int g_fail = 0;

#define TEST(name) static void test_##name(void)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_fail++; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_STREQ(a, b) ASSERT(strcmp((a), (b)) == 0)
#define ASSERT_FLOAT_NEAR(a, b, eps) ASSERT(fabsf((a) - (b)) <= (eps))

static void remove_dir_recursive(const char *path) {
    DIR *d = libc_opendir(path);
    if (!d) {
        return;
    }

    struct dirent *ent;
    while ((ent = libc_readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        char p[PATH_MAX];
        int n = snprintf(p, sizeof(p), "%s/%s", path, ent->d_name);
        if (n <= 0 || (size_t)n >= sizeof(p)) {
            continue;
        }
        unlink(p);
    }
    libc_closedir(d);
    rmdir(path);
}

static int make_temp_dir(char *out, size_t out_len) {
    if (out_len < 8) {
        return -1;
    }
    snprintf(out, out_len, "/tmp/homelab-screen-test-XXXXXX");
    return mkdtemp(out) ? 0 : -1;
}

static int write_file(const char *path, const char *content) {
    FILE *f = libc_fopen(path, "w");
    if (!f) {
        return -1;
    }
    if (content && fputs(content, f) == EOF) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static void clear_fb(void) {
    memset(framebuffer, 0, sizeof(framebuffer));
}

static int fb_has_color(uint16_t color) {
    for (int i = 0; i < LCD_W * LCD_H; i++) {
        if (framebuffer[i] == color) {
            return 1;
        }
    }
    return 0;
}

static int fb_has_any_nonzero(void) {
    for (int i = 0; i < LCD_W * LCD_H; i++) {
        if (framebuffer[i] != 0) {
            return 1;
        }
    }
    return 0;
}

static void mock_set_file(const char *path, const char *content, int fail_open) {
    for (int i = 0; i < MAX_MOCK_FILES; i++) {
        if (!g_mock_files[i].enabled) {
            g_mock_files[i].enabled = 1;
            snprintf(g_mock_files[i].path, sizeof(g_mock_files[i].path), "%s", path);
            snprintf(g_mock_files[i].content, sizeof(g_mock_files[i].content), "%s", content ? content : "");
            g_mock_files[i].fail_open = fail_open;
            return;
        }
    }
}

static void mock_add_cmd(const char *cmd, const char *output, int status, int prefix_match) {
    for (int i = 0; i < MAX_MOCK_CMDS; i++) {
        if (!g_mock_cmds[i].enabled) {
            g_mock_cmds[i].enabled = 1;
            g_mock_cmds[i].prefix_match = prefix_match;
            snprintf(g_mock_cmds[i].cmd, sizeof(g_mock_cmds[i].cmd), "%s", cmd);
            snprintf(g_mock_cmds[i].output, sizeof(g_mock_cmds[i].output), "%s", output ? output : "");
            g_mock_cmds[i].status = status;
            return;
        }
    }
}

static void mock_set_access(const char *path, int rc) {
    for (int i = 0; i < MAX_MOCK_ACCESS; i++) {
        if (!g_mock_access[i].enabled) {
            g_mock_access[i].enabled = 1;
            snprintf(g_mock_access[i].path, sizeof(g_mock_access[i].path), "%s", path);
            g_mock_access[i].rc = rc;
            return;
        }
    }
}

static void mock_set_times(const time_t *times, int count) {
    g_mock_time_enabled = 1;
    g_mock_time_count = count;
    g_mock_time_idx = 0;
    for (int i = 0; i < count && i < MAX_MOCK_TIMES; i++) {
        g_mock_times[i] = times[i];
    }
}

static void setup_mock_net_dir(const char **names, int count) {
    ASSERT(make_temp_dir(g_mock_net_dir, sizeof(g_mock_net_dir)) == 0);
    for (int i = 0; i < count; i++) {
        char p[PATH_MAX];
        int n = snprintf(p, sizeof(p), "%s/%s", g_mock_net_dir, names[i]);
        ASSERT(n > 0 && (size_t)n < sizeof(p));
        ASSERT(write_file(p, "\n") == 0);
    }
    g_use_mock_net_dir = 1;
}

static void cleanup_mock_net_dir(void) {
    if (g_use_mock_net_dir) {
        remove_dir_recursive(g_mock_net_dir);
        g_use_mock_net_dir = 0;
        g_mock_net_dir[0] = '\0';
    }
}

static void reset_test_state(void) {
    g_vid = 0x0416;
    g_pid = 0x5302;
    g_interval = 7;
    g_cli_iface[0] = '\0';

    memset(framebuffer, 0, sizeof(framebuffer));
    g_running = 1;

    memset(&g_metrics, 0, sizeof(g_metrics));
    last_net_rx = 0;
    last_net_tx = 0;
    last_net_time = 0;
    last_cpu_idle = 0;
    last_cpu_total = 0;

    memset(&g_pve_metrics, 0, sizeof(g_pve_metrics));
    last_pve_collect = 0;

    dev_handle = NULL;
    g_ep_out = 0x02;
    g_usb_iface = -1;

    optind = 1;
    mock_libusb_reset();

    memset(g_mock_files, 0, sizeof(g_mock_files));
    memset(g_mock_cmds, 0, sizeof(g_mock_cmds));
    memset(g_mock_procs, 0, sizeof(g_mock_procs));
    memset(g_mock_access, 0, sizeof(g_mock_access));

    g_mock_fs_enabled = 0;
    g_mock_proc_enabled = 0;
    g_mock_access_enabled = 0;
    g_mock_hostname_enabled = 0;
    g_mock_hostname_rc = 0;
    snprintf(g_mock_hostname_value, sizeof(g_mock_hostname_value), "test-host");

    g_mock_time_enabled = 0;
    g_mock_time_count = 0;
    g_mock_time_idx = 0;

    g_mock_localtime_force_null = 0;
    g_mock_localtime_null_once = 0;

    g_mock_nanosleep_enabled = 0;
    g_mock_nanosleep_calls = 0;
    g_mock_nanosleep_stop_after = 0;

    g_mock_snprintf_fail_enabled = 0;
    g_mock_snprintf_fail_once = 0;
    g_mock_snprintf_fail_substr[0] = '\0';

    g_expect_exit = 0;
    g_exit_called = 0;
    g_exit_code = -1;

    cleanup_mock_net_dir();
}

#define RUN(name) do { \
    int last_fail = g_fail; \
    reset_test_state(); \
    printf("  %-48s ", #name); \
    test_##name(); \
    cleanup_mock_net_dir(); \
    if (g_fail == last_fail) { printf("PASS\n"); g_pass++; } \
    else { printf("\n"); } \
} while(0)

/* ===== Rendering primitives and format helpers ===== */

TEST(set_pixel_valid) {
    clear_fb();
    set_pixel(0, 0, 0x1234);
    ASSERT_EQ(framebuffer[0], 0x1234);
    set_pixel(100, 50, 0xABCD);
    ASSERT_EQ(framebuffer[50 * LCD_W + 100], 0xABCD);
}

TEST(set_pixel_out_of_bounds) {
    clear_fb();
    set_pixel(-1, 0, 0xFFFF);
    set_pixel(0, -1, 0xFFFF);
    set_pixel(LCD_W, 0, 0xFFFF);
    set_pixel(0, LCD_H, 0xFFFF);
    ASSERT_EQ(framebuffer[0], 0x0000);
}

TEST(fill_rect_clipping) {
    clear_fb();
    fill_rect(-3, -3, 5, 5, 0x3333);
    ASSERT_EQ(framebuffer[0], 0x3333);
    ASSERT_EQ(framebuffer[2], 0x0000);

    clear_fb();
    fill_rect(LCD_W - 2, LCD_H - 2, 10, 10, 0x2222);
    ASSERT_EQ(framebuffer[(LCD_H - 1) * LCD_W + (LCD_W - 1)], 0x2222);
}

TEST(draw_char_and_strings) {
    clear_fb();
    draw_char(0, 0, '!', 0xAAAA, 1);
    ASSERT_EQ(framebuffer[2 * LCD_W + 3], 0xAAAA);

    clear_fb();
    draw_char(0, 0, '\x01', 0xBBBB, 1);
    ASSERT_EQ(framebuffer[2 * LCD_W + 2], 0xBBBB);

    clear_fb();
    draw_char(0, 0, '!', 0xCCCC, 2);
    ASSERT_EQ(framebuffer[4 * LCD_W + 6], 0xCCCC);

    clear_fb();
    draw_string(0, 0, "AB", 0x1111, 1);
    ASSERT_EQ(framebuffer[2 * LCD_W + 4], 0x1111);

    clear_fb();
    draw_string_centered(0, "AB", 0x1234, 1);
    ASSERT_EQ(framebuffer[2 * LCD_W + 116], 0x1234);

    ASSERT_EQ(string_width("", 1), 0);
    ASSERT_EQ(string_width("AB", 2), 32);
}

TEST(progress_and_circle) {
    clear_fb();
    draw_progress_bar(10, 10, 100, 10, 0.0f, 0x0001, 0x0002);
    ASSERT(fb_has_color(0x0001));

    clear_fb();
    draw_progress_bar(10, 10, 100, 10, 100.0f, 0x0001, 0x0002);
    ASSERT(fb_has_color(0x0002));

    clear_fb();
    draw_circle_progress(60, 60, 20, 5, 50.0f, 0x0003, 0x0004);
    ASSERT(fb_has_color(0x0003));
    ASSERT(fb_has_color(0x0004));
}

TEST(format_bytes_helpers) {
    char buf[32];

    format_bytes_rate(0.0f, buf, sizeof(buf));
    ASSERT_STREQ(buf, "0 B/s");
    format_bytes_rate(1024.0f, buf, sizeof(buf));
    ASSERT_STREQ(buf, "1.0 KB/s");
    format_bytes_rate(1048576.0f, buf, sizeof(buf));
    ASSERT_STREQ(buf, "1.0 MB/s");
    format_bytes_rate(1073741824.0f, buf, sizeof(buf));
    ASSERT_STREQ(buf, "1.0 GB/s");

    format_bytes_human(0, buf, sizeof(buf));
    ASSERT_STREQ(buf, "0B");
    format_bytes_human(1048576, buf, sizeof(buf));
    ASSERT_STREQ(buf, "1.0M");
    format_bytes_human(1073741824ULL, buf, sizeof(buf));
    ASSERT_STREQ(buf, "1.0G");
    format_bytes_human(1099511627776ULL, buf, sizeof(buf));
    ASSERT_STREQ(buf, "1.0T");
}

TEST(render_pages_all_paths) {
    g_metrics.cpu_usage = 95.0f;
    g_metrics.cpu_temp = 85.0f;
    g_metrics.mem_pct = 95.0f;
    g_metrics.mem_used = 8ULL * 1024 * 1024 * 1024;
    g_metrics.mem_total = 16ULL * 1024 * 1024 * 1024;
    g_metrics.net_rx_rate = 250000000.0f;
    g_metrics.net_tx_rate = 50000000.0f;
    snprintf(g_metrics.net_iface, sizeof(g_metrics.net_iface), "vmbr0");
    snprintf(g_metrics.hostname, sizeof(g_metrics.hostname), "node-a");
    g_metrics.uptime_secs = 3 * 86400 + 3600;
    g_metrics.load_1 = 1.1f;
    g_metrics.load_5 = 2.2f;
    g_metrics.load_15 = 3.3f;

    g_pve_metrics.running_vms = 3;
    g_pve_metrics.total_vms = 12;
    g_pve_metrics.running_cts = 5;
    g_pve_metrics.total_cts = 7;
    snprintf(g_pve_metrics.node_name, sizeof(g_pve_metrics.node_name), "pve-node");
    snprintf(g_pve_metrics.pve_version, sizeof(g_pve_metrics.pve_version), "pve-manager/8.3.0");

    g_pve_metrics.storage_count = 4;
    snprintf(g_pve_metrics.storage[0].name, sizeof(g_pve_metrics.storage[0].name), "fast");
    g_pve_metrics.storage[0].used_pct = 95.0f;
    g_pve_metrics.storage[0].used_bytes = 95ULL * 1024 * 1024 * 1024;
    g_pve_metrics.storage[0].total_bytes = 100ULL * 1024 * 1024 * 1024;

    snprintf(g_pve_metrics.storage[1].name, sizeof(g_pve_metrics.storage[1].name), "warm");
    g_pve_metrics.storage[1].used_pct = 70.0f;
    g_pve_metrics.storage[1].used_bytes = 70ULL * 1024 * 1024 * 1024;
    g_pve_metrics.storage[1].total_bytes = 100ULL * 1024 * 1024 * 1024;

    snprintf(g_pve_metrics.storage[2].name, sizeof(g_pve_metrics.storage[2].name), "cold");
    g_pve_metrics.storage[2].used_pct = 50.0f;
    g_pve_metrics.storage[2].used_bytes = 50ULL * 1024 * 1024 * 1024;
    g_pve_metrics.storage[2].total_bytes = 100ULL * 1024 * 1024 * 1024;

    snprintf(g_pve_metrics.storage[3].name, sizeof(g_pve_metrics.storage[3].name), "tiny");
    g_pve_metrics.storage[3].used_pct = 0.0f;
    g_pve_metrics.storage[3].used_bytes = 0;
    g_pve_metrics.storage[3].total_bytes = 100ULL * 1024 * 1024 * 1024;

    clear_fb();
    render_page_overview();
    ASSERT(fb_has_any_nonzero());
    ASSERT(fb_has_color(0xFFFF));

    clear_fb();
    render_page_cpu();
    ASSERT(fb_has_any_nonzero());
    ASSERT(fb_has_color(0xF800));

    g_metrics.cpu_temp = 0.0f;
    clear_fb();
    render_page_cpu();
    ASSERT(fb_has_any_nonzero());

    g_metrics.mem_pct = 80.0f;
    clear_fb();
    render_page_memory();
    ASSERT(fb_has_any_nonzero());

    g_metrics.mem_pct = 20.0f;
    clear_fb();
    render_page_memory();
    ASSERT(fb_has_any_nonzero());

    clear_fb();
    render_page_network();
    ASSERT(fb_has_any_nonzero());

    g_metrics.uptime_secs = 3600 + 120;
    clear_fb();
    render_page_system();
    ASSERT(fb_has_any_nonzero());

    g_metrics.uptime_secs = 3 * 86400 + 3600;
    clear_fb();
    render_page_system();
    ASSERT(fb_has_any_nonzero());

    g_mock_localtime_force_null = 1;
    g_mock_localtime_null_once = 1;
    clear_fb();
    render_page_system();
    ASSERT(fb_has_any_nonzero());

    clear_fb();
    render_page_proxmox();
    ASSERT(fb_has_any_nonzero());

    g_pve_metrics.pve_version[0] = '\0';
    clear_fb();
    render_page_proxmox();
    ASSERT(fb_has_any_nonzero());

    clear_fb();
    render_page_storage();
    ASSERT(fb_has_any_nonzero());

    g_pve_metrics.storage_count = 0;
    clear_fb();
    render_page_storage();
    ASSERT(fb_has_any_nonzero());

    g_metrics.cpu_temp = 0.0f;
    clear_fb();
    render_page_overview();
    ASSERT(fb_has_any_nonzero());
}

/* ===== CLI ===== */

TEST(parse_hex_and_int_helpers) {
    uint16_t u16 = 0;
    int iv = 0;

    ASSERT_EQ(parse_hex_u16("0416", &u16), 0);
    ASSERT_EQ(u16, 0x0416);
    ASSERT_EQ(parse_hex_u16(NULL, &u16), -1);
    ASSERT_EQ(parse_hex_u16("0416", NULL), -1);
    ASSERT_EQ(parse_hex_u16("", &u16), -1);
    ASSERT_EQ(parse_hex_u16("10000", &u16), -1);

    ASSERT_EQ(parse_positive_int("42", &iv), 0);
    ASSERT_EQ(iv, 42);
    ASSERT_EQ(parse_positive_int(NULL, &iv), -1);
    ASSERT_EQ(parse_positive_int("1", NULL), -1);
    ASSERT_EQ(parse_positive_int("", &iv), -1);
    ASSERT_EQ(parse_positive_int("0", &iv), -1);
    ASSERT_EQ(parse_positive_int("-1", &iv), -1);
}

TEST(parse_args_valid_and_invalid) {
    char *argv_ok[] = {"homelab-screen", "--vid", "0417", "--pid", "5303", "--interval", "9", "--interface", "eth1", NULL};
    ASSERT_EQ(parse_args(9, argv_ok), 0);
    ASSERT_EQ(g_vid, 0x0417);
    ASSERT_EQ(g_pid, 0x5303);
    ASSERT_EQ(g_interval, 9);
    ASSERT_STREQ(g_cli_iface, "eth1");

    char *argv_bad_vid[] = {"homelab-screen", "--vid", "ZZZZ", NULL};
    ASSERT_EQ(parse_args(3, argv_bad_vid), -1);

    char *argv_bad_pid[] = {"homelab-screen", "--pid", "ZZZZ", NULL};
    ASSERT_EQ(parse_args(3, argv_bad_pid), -1);

    char *argv_bad_interval[] = {"homelab-screen", "--interval", "0", NULL};
    ASSERT_EQ(parse_args(3, argv_bad_interval), -1);

    char iface[64];
    memset(iface, 'a', sizeof(iface) - 1);
    iface[sizeof(iface) - 1] = '\0';
    char *argv_bad_iface[] = {"homelab-screen", "--interface", iface, NULL};
    ASSERT_EQ(parse_args(3, argv_bad_iface), -1);

    char *argv_unknown[] = {"homelab-screen", "--unknown", NULL};
    ASSERT_EQ(parse_args(2, argv_unknown), -1);
}

TEST(parse_args_help_exits) {
    char *argv_help[] = {"homelab-screen", "--help", NULL};
    g_expect_exit = 1;
    if (setjmp(g_exit_jmp) == 0) {
        (void)parse_args(2, argv_help);
        ASSERT(0);
    }
    ASSERT_EQ(g_exit_called, 1);
    ASSERT_EQ(g_exit_code, 0);
}

/* ===== Metrics ===== */

TEST(compute_counter_rate_cases) {
    ASSERT_FLOAT_NEAR(compute_counter_rate(2000, 1000, 2), 500.0f, 0.001f);
    ASSERT_FLOAT_NEAR(compute_counter_rate(100, 200, 1), 0.0f, 0.001f);
    ASSERT_FLOAT_NEAR(compute_counter_rate(100, 100, 0), 0.0f, 0.001f);
}

TEST(get_cpu_usage_paths) {
    float usage = 12.3f;

    g_mock_fs_enabled = 1;
    ASSERT_EQ(get_cpu_usage(&usage), -1);

    mock_set_file("/proc/stat", "cpu broken\n", 0);
    ASSERT_EQ(get_cpu_usage(&usage), -1);

    memset(g_mock_files, 0, sizeof(g_mock_files));
    mock_set_file("/proc/stat", "cpu 100 0 100 100 0 0 0\n", 0);
    ASSERT_EQ(get_cpu_usage(&usage), 0);
    ASSERT_FLOAT_NEAR(usage, 0.0f, 0.001f);

    memset(g_mock_files, 0, sizeof(g_mock_files));
    mock_set_file("/proc/stat", "cpu 200 0 200 100 0 0 0\n", 0);
    ASSERT_EQ(get_cpu_usage(&usage), 0);
    ASSERT(usage > 0.0f);
}

TEST(get_memory_temp_host_load_uptime_paths) {
    uint64_t used = 0, total = 0;
    float temp = 0.0f;
    char host[64] = "";
    uint64_t up = 0;
    float l1 = -1.0f, l5 = -1.0f, l15 = -1.0f;

    g_mock_fs_enabled = 1;
    ASSERT_EQ(get_memory_info(&used, &total), -1);

    mock_set_file("/proc/meminfo", "MemTotal: 1000 kB\nMemAvailable: 400 kB\n", 0);
    ASSERT_EQ(get_memory_info(&used, &total), 0);
    ASSERT_EQ(total, 1000ULL * 1024ULL);
    ASSERT_EQ(used, 600ULL * 1024ULL);

    memset(g_mock_files, 0, sizeof(g_mock_files));
    mock_set_file("/sys/class/thermal/thermal_zone0/temp", "", 1);
    mock_set_file("/sys/class/hwmon/hwmon0/temp1_input", "55000\n", 0);
    ASSERT_EQ(get_cpu_temp(&temp), 0);
    ASSERT_FLOAT_NEAR(temp, 55.0f, 0.001f);

    memset(g_mock_files, 0, sizeof(g_mock_files));
    mock_set_file("/sys/class/thermal/thermal_zone0/temp", "nan\n", 0);
    ASSERT_EQ(get_cpu_temp(&temp), -1);

    memset(g_mock_files, 0, sizeof(g_mock_files));
    mock_set_file("/sys/class/thermal/thermal_zone0/temp", "", 1);
    mock_set_file("/sys/class/hwmon/hwmon0/temp1_input", "", 1);
    mock_set_file("/sys/class/hwmon/hwmon1/temp1_input", "", 1);
    ASSERT_EQ(get_cpu_temp(&temp), -1);

    memset(g_mock_files, 0, sizeof(g_mock_files));
    mock_set_file("/etc/hostname", "my-host\n", 0);
    get_hostname(host, sizeof(host));
    ASSERT_STREQ(host, "my-host");

    memset(g_mock_files, 0, sizeof(g_mock_files));
    g_mock_hostname_enabled = 1;
    snprintf(g_mock_hostname_value, sizeof(g_mock_hostname_value), "fallback-host");
    get_hostname(host, sizeof(host));
    ASSERT_STREQ(host, "fallback-host");

    memset(g_mock_files, 0, sizeof(g_mock_files));
    mock_set_file("/proc/uptime", "1234.56 0.0\n", 0);
    get_uptime(&up);
    ASSERT_EQ(up, 1234ULL);

    memset(g_mock_files, 0, sizeof(g_mock_files));
    mock_set_file("/proc/loadavg", "1.0 2.0 3.0 0/0 1\n", 0);
    get_load_avg(&l1, &l5, &l15);
    ASSERT_FLOAT_NEAR(l1, 1.0f, 0.001f);

    memset(g_mock_files, 0, sizeof(g_mock_files));
    mock_set_file("/proc/loadavg", "broken\n", 0);
    l1 = l5 = l15 = -1.0f;
    get_load_avg(&l1, &l5, &l15);
    ASSERT_FLOAT_NEAR(l1, 0.0f, 0.001f);
    ASSERT_FLOAT_NEAR(l5, 0.0f, 0.001f);
    ASSERT_FLOAT_NEAR(l15, 0.0f, 0.001f);
}

TEST(detect_network_interface_paths) {
    snprintf(g_cli_iface, sizeof(g_cli_iface), "cli0");
    detect_network_interface();
    ASSERT_STREQ(g_metrics.net_iface, "cli0");

    g_cli_iface[0] = '\0';
    g_use_mock_net_dir = 1;
    snprintf(g_mock_net_dir, sizeof(g_mock_net_dir), "/tmp/does-not-exist-homelab-screen");
    detect_network_interface();
    ASSERT_STREQ(g_metrics.net_iface, "eth0");
    g_use_mock_net_dir = 0;
    g_mock_net_dir[0] = '\0';

    const char *entries1[] = {"lo", "ethA", "ethB"};
    setup_mock_net_dir(entries1, 3);
    g_mock_fs_enabled = 1;
    mock_set_file("/sys/class/net/ethA/carrier", "0\n", 0);
    mock_set_file("/sys/class/net/ethB/carrier", "1\n", 0);
    detect_network_interface();
    ASSERT_STREQ(g_metrics.net_iface, "ethB");

    cleanup_mock_net_dir();
    reset_test_state();

    const char *entries2[] = {"lo", "ethX"};
    setup_mock_net_dir(entries2, 2);
    g_mock_fs_enabled = 1;
    mock_set_file("/sys/class/net/ethX/carrier", "0\n", 0);
    detect_network_interface();
    ASSERT_STREQ(g_metrics.net_iface, "ethX");

    cleanup_mock_net_dir();
    reset_test_state();

    const char *entries3[] = {"this-interface-name-is-way-too-long-for-buffer"};
    setup_mock_net_dir(entries3, 1);
    g_mock_fs_enabled = 1;
    detect_network_interface();
    ASSERT_STREQ(g_metrics.net_iface, "eth0");

    cleanup_mock_net_dir();
    reset_test_state();

    const char *entries4[] = {"ethZ"};
    setup_mock_net_dir(entries4, 1);
    g_mock_fs_enabled = 1;
    g_mock_snprintf_fail_enabled = 1;
    g_mock_snprintf_fail_once = 1;
    snprintf(g_mock_snprintf_fail_substr, sizeof(g_mock_snprintf_fail_substr), "/sys/class/net/");
    detect_network_interface();
    ASSERT_STREQ(g_metrics.net_iface, "eth0");
}

TEST(get_network_rates_and_collect_metrics) {
    g_mock_fs_enabled = 1;
    snprintf(g_metrics.net_iface, sizeof(g_metrics.net_iface), "eth0");
    mock_set_file("/sys/class/net/eth0/statistics/rx_bytes", "2000\n", 0);
    mock_set_file("/sys/class/net/eth0/statistics/tx_bytes", "3000\n", 0);
    last_net_rx = 1000;
    last_net_tx = 1000;
    last_net_time = 10;
    time_t times[] = {12};
    mock_set_times(times, 1);
    get_network_rates();
    ASSERT_FLOAT_NEAR(g_metrics.net_rx_rate, 500.0f, 0.01f);
    ASSERT_FLOAT_NEAR(g_metrics.net_tx_rate, 1000.0f, 0.01f);

    memset(g_mock_files, 0, sizeof(g_mock_files));
    g_metrics.net_iface[0] = '\0';
    float prev_rx_rate = g_metrics.net_rx_rate;
    float prev_tx_rate = g_metrics.net_tx_rate;
    uint64_t prev_last_rx = last_net_rx;
    uint64_t prev_last_tx = last_net_tx;
    time_t prev_last_time = last_net_time;
    get_network_rates();
    ASSERT_FLOAT_NEAR(g_metrics.net_rx_rate, prev_rx_rate, 0.001f);
    ASSERT_FLOAT_NEAR(g_metrics.net_tx_rate, prev_tx_rate, 0.001f);
    ASSERT_EQ(last_net_rx, prev_last_rx);
    ASSERT_EQ(last_net_tx, prev_last_tx);
    ASSERT_EQ(last_net_time, prev_last_time);

    g_mock_fs_enabled = 1;
    mock_set_file("/proc/stat", "cpu 100 0 100 100 0 0 0\n", 0);
    mock_set_file("/sys/class/thermal/thermal_zone0/temp", "42000\n", 0);
    mock_set_file("/proc/meminfo", "MemTotal: 1000 kB\nMemAvailable: 1000 kB\n", 0);
    mock_set_file("/proc/uptime", "100.0 0.0\n", 0);
    mock_set_file("/proc/loadavg", "1.0 2.0 3.0 0/0 1\n", 0);
    snprintf(g_metrics.net_iface, sizeof(g_metrics.net_iface), "eth0");
    mock_set_file("/sys/class/net/eth0/statistics/rx_bytes", "x\n", 0);
    mock_set_file("/sys/class/net/eth0/statistics/tx_bytes", "y\n", 0);
    time_t times2[] = {100};
    mock_set_times(times2, 1);
    collect_metrics();
    ASSERT_FLOAT_NEAR(g_metrics.mem_pct, 0.0f, 0.001f);

    memset(g_mock_files, 0, sizeof(g_mock_files));
    mock_set_file("/proc/stat", "cpu 100 0 100 100 0 0 0\n", 0);
    mock_set_file("/sys/class/thermal/thermal_zone0/temp", "42000\n", 0);
    mock_set_file("/proc/meminfo", "MemTotal: 2000 kB\nMemAvailable: 1000 kB\n", 0);
    mock_set_file("/proc/uptime", "100.0 0.0\n", 0);
    mock_set_file("/proc/loadavg", "1.0 2.0 3.0 0/0 1\n", 0);
    snprintf(g_metrics.net_iface, sizeof(g_metrics.net_iface), "eth0");
    mock_set_file("/sys/class/net/eth0/statistics/rx_bytes", "100\n", 0);
    mock_set_file("/sys/class/net/eth0/statistics/tx_bytes", "200\n", 0);
    time_t times3[] = {200};
    mock_set_times(times3, 1);
    collect_metrics();
    ASSERT(g_metrics.mem_pct > 0.0f);

    reset_test_state();
    g_mock_fs_enabled = 1;
    mock_set_file("/proc/stat", "cpu 100 0 100 100 0 0 0\n", 0);
    mock_set_file("/sys/class/thermal/thermal_zone0/temp", "42000\n", 0);
    mock_set_file("/proc/uptime", "100.0 0.0\n", 0);
    mock_set_file("/proc/loadavg", "1.0 2.0 3.0 0/0 1\n", 0);
    snprintf(g_metrics.net_iface, sizeof(g_metrics.net_iface), "eth0");
    mock_set_file("/sys/class/net/eth0/statistics/rx_bytes", "100\n", 0);
    mock_set_file("/sys/class/net/eth0/statistics/tx_bytes", "200\n", 0);
    time_t times4[] = {300};
    mock_set_times(times4, 1);
    collect_metrics();
    ASSERT_FLOAT_NEAR(g_metrics.mem_pct, 0.0f, 0.001f);
}

/* ===== Proxmox ===== */

TEST(find_helpers_and_json_parse_paths) {
    const char *s = "abcdef";
    ASSERT(find_in_range(s, s + 6, "cd") != NULL);
    ASSERT(find_in_range(NULL, s + 6, "cd") == NULL);
    ASSERT(find_in_range(s + 3, s + 2, "cd") == NULL);
    ASSERT(find_char_in_range(s, s + 6, 'e') != NULL);
    ASSERT(find_char_in_range(NULL, s + 6, 'e') == NULL);

    char out[8];
    uint64_t val = 0;
    const char *obj = "{\"storage\":\"local\",\"used\":123,\"total\":456}";

    ASSERT_EQ(parse_json_string_field(obj, obj + strlen(obj), "storage", out, sizeof(out)), 0);
    ASSERT_STREQ(out, "local");
    ASSERT_EQ(parse_json_string_field(obj, obj + strlen(obj), "missing", out, sizeof(out)), -1);
    const char *s1 = "{\"storage\"}";
    const char *s2 = "{\"storage\":123}";
    const char *s3 = "{\"storage\":\"broken}";
    ASSERT_EQ(parse_json_string_field(s1, s1 + strlen(s1), "storage", out, sizeof(out)), -1);
    ASSERT_EQ(parse_json_string_field(s2, s2 + strlen(s2), "storage", out, sizeof(out)), -1);
    ASSERT_EQ(parse_json_string_field(s3, s3 + strlen(s3), "storage", out, sizeof(out)), -1);

    char tiny[3];
    ASSERT_EQ(parse_json_string_field(obj, obj + strlen(obj), "storage", tiny, sizeof(tiny)), 0);
    ASSERT_STREQ(tiny, "lo");

    char long_field[80];
    memset(long_field, 'a', sizeof(long_field) - 1);
    long_field[sizeof(long_field) - 1] = '\0';
    ASSERT_EQ(parse_json_string_field(obj, obj + strlen(obj), long_field, out, sizeof(out)), -1);

    ASSERT_EQ(parse_json_string_field(NULL, obj + strlen(obj), "storage", out, sizeof(out)), -1);
    ASSERT_EQ(parse_json_u64_field(obj, obj + strlen(obj), "used", &val), 0);
    ASSERT_EQ(val, (uint64_t)123);
    ASSERT_EQ(parse_json_u64_field(obj, obj + strlen(obj), "missing", &val), -1);
    const char *u1 = "{\"used\"}";
    const char *u2 = "{\"used\":}";
    ASSERT_EQ(parse_json_u64_field(u1, u1 + strlen(u1), "used", &val), -1);
    ASSERT_EQ(parse_json_u64_field(u2, u2 + strlen(u2), "used", &val), -1);
    ASSERT_EQ(parse_json_u64_field(obj, obj + strlen(obj), long_field, &val), -1);
    ASSERT_EQ(parse_json_u64_field(NULL, obj + strlen(obj), "used", &val), -1);

    const char *obj2 = "{\"used\":12}";
    ASSERT_EQ(parse_json_u64_field(obj2, obj2 + 8, "used", &val), -1);
}

TEST(check_pve_available_paths) {
    g_mock_access_enabled = 1;
    mock_set_access("/usr/bin/pvesh", -1);
    mock_set_access("/usr/sbin/qm", -1);
    g_mock_hostname_enabled = 1;
    snprintf(g_mock_hostname_value, sizeof(g_mock_hostname_value), "node-x");

    check_pve_available();
    ASSERT_EQ(g_pve_metrics.pve_available, 0);
    ASSERT_STREQ(g_pve_metrics.node_name, "node-x");

    memset(g_mock_access, 0, sizeof(g_mock_access));
    mock_set_access("/usr/bin/pvesh", 0);
    check_pve_available();
    ASSERT_EQ(g_pve_metrics.pve_available, 1);
}

TEST(get_pve_vms_cts_and_version) {
    g_mock_proc_enabled = 1;

    mock_add_cmd("qm list 2>/dev/null", "VMID NAME STATUS\n100 a running\n101 b stopped\n", 0, 0);
    get_pve_vms();
    ASSERT_EQ(g_pve_metrics.total_vms, 2);
    ASSERT_EQ(g_pve_metrics.running_vms, 1);

    memset(g_mock_cmds, 0, sizeof(g_mock_cmds));
    mock_add_cmd("pct list 2>/dev/null", "VMID NAME STATUS\n200 c running\n\n", 0, 0);
    get_pve_cts();
    ASSERT_EQ(g_pve_metrics.total_cts, 1);
    ASSERT_EQ(g_pve_metrics.running_cts, 1);

    memset(g_mock_cmds, 0, sizeof(g_mock_cmds));
    mock_add_cmd("pveversion 2>/dev/null", "pve-manager/8.2\n", 0, 0);
    get_pve_version();
    ASSERT_STREQ(g_pve_metrics.pve_version, "pve-manager/8.2");

    memset(g_mock_cmds, 0, sizeof(g_mock_cmds));
    memset(g_mock_files, 0, sizeof(g_mock_files));
    g_mock_fs_enabled = 1;
    mock_add_cmd("pveversion 2>/dev/null", "", 0, 0);
    mock_set_file("/etc/pve/.version", "8.3.0\n", 0);
    get_pve_version();
    ASSERT_STREQ(g_pve_metrics.pve_version, "8.3.0");
}

TEST(get_pve_storage_json_and_fallback) {
    g_mock_proc_enabled = 1;
    snprintf(g_pve_metrics.node_name, sizeof(g_pve_metrics.node_name), "node?bad");

    mock_add_cmd("pvesh get /nodes/", ""
        "[{\"storage\":\"local\",\"used\":100,\"total\":200},"
        "{\"storage\":\"missing-total\",\"used\":1},"
        "{\"storage\":0,\"used\":1,\"total\":2},"
        "{\"storage\":\"zero\",\"used\":1,\"total\":0},"
        "{\"storage\":\"broken\",\"used\":1]",
        0, 1);
    get_pve_storage();
    ASSERT(g_pve_metrics.storage_count >= 1);
    ASSERT_STREQ(g_pve_metrics.storage[0].name, "local");
    ASSERT(g_pve_metrics.storage[1].used_pct == 0.0f);

    reset_test_state();
    g_mock_proc_enabled = 1;
    snprintf(g_pve_metrics.node_name, sizeof(g_pve_metrics.node_name), "node1");
    mock_add_cmd("pvesh get /nodes/node1/storage --output-format json 2>/dev/null", "", 1, 0);
    mock_add_cmd("df -B1 /var/lib/vz 2>/dev/null", "Filesystem 1B-blocks Used Available Use% Mounted\n/dev/sda 1000 500 500 50% /var/lib/vz\n", 0, 0);
    mock_add_cmd("df -B1 /var/lib/pve/local-btrfs 2>/dev/null", "Filesystem 1B-blocks Used Available Use% Mounted\n/dev/sdb 0 0 0 0% /var/lib/pve/local-btrfs\n", 0, 0);
    get_pve_storage();
    ASSERT(g_pve_metrics.storage_count >= 1);

    reset_test_state();
    g_mock_proc_enabled = 1;
    snprintf(g_pve_metrics.node_name, sizeof(g_pve_metrics.node_name), "node1");
    mock_add_cmd("pvesh get /nodes/node1/storage --output-format json 2>/dev/null", "\"storage\":\"x\"}", 0, 0);
    get_pve_storage();
    ASSERT_EQ(g_pve_metrics.storage_count, 0);

    reset_test_state();
    g_mock_proc_enabled = 1;
    snprintf(g_pve_metrics.node_name, sizeof(g_pve_metrics.node_name), "node1");
    mock_add_cmd("pvesh get /nodes/node1/storage --output-format json 2>/dev/null", "[{\"storage\"}]", 0, 0);
    get_pve_storage();
    ASSERT_EQ(g_pve_metrics.storage_count, 0);

    reset_test_state();
    g_mock_proc_enabled = 1;
    snprintf(g_pve_metrics.node_name, sizeof(g_pve_metrics.node_name), "node1");
    char huge[20000];
    memset(huge, 'a', sizeof(huge) - 1);
    huge[sizeof(huge) - 1] = '\0';
    mock_add_cmd("pvesh get /nodes/node1/storage --output-format json 2>/dev/null", huge, 0, 0);
    get_pve_storage();
    ASSERT_EQ(g_pve_metrics.storage_count, 0);
}

TEST(collect_proxmox_metrics_paths) {
    g_pve_metrics.pve_available = 0;
    collect_proxmox_metrics();
    ASSERT_EQ(g_pve_metrics.total_vms, 0);

    g_pve_metrics.pve_available = 1;
    last_pve_collect = 100;
    g_mock_time_enabled = 1;
    g_mock_times[0] = 105;
    g_mock_time_count = 1;
    collect_proxmox_metrics();
    ASSERT_EQ(last_pve_collect, 100);

    reset_test_state();
    g_pve_metrics.pve_available = 1;
    snprintf(g_pve_metrics.node_name, sizeof(g_pve_metrics.node_name), "node1");
    g_mock_proc_enabled = 1;
    mock_add_cmd("qm list 2>/dev/null", "VMID NAME STATUS\n100 a running\n", 0, 0);
    mock_add_cmd("pct list 2>/dev/null", "VMID NAME STATUS\n200 c stopped\n", 0, 0);
    mock_add_cmd("pvesh get /nodes/node1/storage --output-format json 2>/dev/null", "[{\"storage\":\"local\",\"used\":100,\"total\":200}]", 0, 0);
    mock_add_cmd("pveversion 2>/dev/null", "pve-manager/8.2\n", 0, 0);
    time_t times[] = {200};
    mock_set_times(times, 1);
    collect_proxmox_metrics();
    ASSERT_EQ(g_pve_metrics.total_vms, 1);
    ASSERT_EQ(g_pve_metrics.total_cts, 1);
    ASSERT_EQ(g_pve_metrics.storage_count, 1);
}

/* ===== USB ===== */

TEST(build_header_values) {
    uint8_t hdr[PACKET_SIZE];
    memset(hdr, 0xFF, sizeof(hdr));
    build_header(hdr);
    ASSERT_EQ(hdr[0], 0xDA);
    ASSERT_EQ(hdr[1], 0xDB);
    ASSERT_EQ(hdr[2], 0xDC);
    ASSERT_EQ(hdr[3], 0xDD);
    ASSERT_EQ(hdr[12], 0x02);
    ASSERT_EQ(hdr[23], 0x58);
    ASSERT_EQ(hdr[511], 0x00);
}

TEST(usb_init_error_paths) {
    mock_libusb_init_rc = -1;
    ASSERT_EQ(usb_init(), -1);

    reset_test_state();
    mock_libusb_open_ok = 0;
    ASSERT_EQ(usb_init(), -1);

    reset_test_state();
    mock_libusb_get_active_config_rc = -7;
    ASSERT_EQ(usb_init(), -1);

    reset_test_state();
    mock_libusb_get_active_config_rc = -7;
    mock_libusb_force_cfg_on_error = 1;
    ASSERT_EQ(usb_init(), -1);

    reset_test_state();
    mock_libusb_has_out_endpoint = 0;
    ASSERT_EQ(usb_init(), -1);

    reset_test_state();
    mock_libusb_claim_interface_rc = -5;
    ASSERT_EQ(usb_init(), -1);
}

TEST(usb_init_success_and_cleanup) {
    mock_libusb_set_auto_detach_rc = -1;
    mock_libusb_interface_number = 3;
    mock_libusb_endpoint_addr = 0x04;
    ASSERT_EQ(usb_init(), 0);
    ASSERT_EQ(g_usb_iface, 3);
    ASSERT_EQ(g_ep_out, 0x04);
    ASSERT_EQ(mock_libusb_claimed_iface, 3);

    usb_cleanup();
    ASSERT_EQ(g_usb_iface, -1);
    ASSERT_EQ(g_ep_out, 0);
    ASSERT_EQ(mock_libusb_released_iface, 3);

    dev_handle = (libusb_device_handle *)0x1;
    g_usb_iface = -1;
    usb_cleanup();
    ASSERT_EQ(g_usb_iface, -1);
}

TEST(send_frame_paths) {
    static libusb_device_handle fake_handle;
    dev_handle = &fake_handle;
    g_ep_out = 0x02;

    ASSERT_EQ(send_frame(), 0);
    ASSERT_EQ(mock_libusb_bulk_calls, 301);

    reset_test_state();
    dev_handle = &fake_handle;
    g_ep_out = 0x02;
    mock_libusb_bulk_transfer_rc = -1;
    ASSERT_EQ(send_frame(), -1);
    ASSERT_EQ(mock_libusb_bulk_calls, 1);

    reset_test_state();
    dev_handle = &fake_handle;
    g_ep_out = 0x02;
    mock_libusb_bulk_short_write = 1;
    ASSERT_EQ(send_frame(), -1);
    ASSERT_EQ(mock_libusb_bulk_calls, 1);

    reset_test_state();
    dev_handle = &fake_handle;
    g_ep_out = 0x02;
    mock_libusb_bulk_transfer_rc = -2;
    ASSERT_EQ(send_frame(), -1);
    ASSERT_EQ(mock_libusb_bulk_calls, 1);

    reset_test_state();
    dev_handle = &fake_handle;
    g_ep_out = 0x02;
    mock_libusb_bulk_short_write_after = 1;
    ASSERT_EQ(send_frame(), -1);
    ASSERT_EQ(mock_libusb_bulk_calls, 2);

    reset_test_state();
    dev_handle = &fake_handle;
    g_ep_out = 0x02;
    mock_libusb_bulk_fail_after = 1;
    ASSERT_EQ(send_frame(), -1);
    ASSERT_EQ(mock_libusb_bulk_calls, 2);
}

TEST(mock_libusb_direct_paths) {
    struct libusb_config_descriptor *cfg = NULL;
    ASSERT_EQ(libusb_get_active_config_descriptor(NULL, NULL), -1);

    mock_libusb_get_active_config_rc = -9;
    ASSERT_EQ(libusb_get_active_config_descriptor(NULL, &cfg), -9);

    reset_test_state();
    mock_libusb_has_out_endpoint = 0;
    ASSERT_EQ(libusb_get_active_config_descriptor(NULL, &cfg), 0);
    ASSERT((cfg->interface[0].altsetting[0].endpoint[0].bEndpointAddress & 0x80) != 0);
}

/* ===== main loop and state ===== */

TEST(signal_handler_sets_running_zero) {
    g_running = 1;
    signal_handler(SIGTERM);
    ASSERT_EQ(g_running, 0);
}

TEST(main_parse_failure) {
    char *argv[] = {"homelab-screen", "--interval", "0", NULL};
    ASSERT_EQ(homelab_screen_main(3, argv), 1);
}

TEST(main_usb_init_failure) {
    char *argv[] = {"homelab-screen", "--interface", "eth0", NULL};
    mock_libusb_open_ok = 0;
    ASSERT_EQ(homelab_screen_main(3, argv), 1);
}

TEST(main_success_single_loop_with_page_switch) {
    char *argv[] = {"homelab-screen", "--interface", "eth0", "--interval", "1", NULL};

    g_mock_nanosleep_enabled = 1;
    g_mock_nanosleep_stop_after = 1;

    g_mock_fs_enabled = 1;
    mock_set_file("/proc/stat", "cpu 100 0 100 100 0 0 0\n", 0);
    mock_set_file("/sys/class/thermal/thermal_zone0/temp", "42000\n", 0);
    mock_set_file("/proc/meminfo", "MemTotal: 1000 kB\nMemAvailable: 500 kB\n", 0);
    mock_set_file("/proc/uptime", "100.0 0.0\n", 0);
    mock_set_file("/proc/loadavg", "1.0 2.0 3.0 0/0 1\n", 0);
    mock_set_file("/sys/class/net/eth0/statistics/rx_bytes", "100\n", 0);
    mock_set_file("/sys/class/net/eth0/statistics/tx_bytes", "200\n", 0);

    g_mock_access_enabled = 1;
    mock_set_access("/usr/bin/pvesh", -1);
    mock_set_access("/usr/sbin/qm", -1);

    time_t times[] = {100, 102};
    mock_set_times(times, 2);

    ASSERT_EQ(homelab_screen_main(5, argv), 0);
    ASSERT_EQ(g_mock_nanosleep_calls, 1);
    ASSERT_EQ(g_pve_metrics.pve_available, 0);
    ASSERT_STREQ(g_metrics.net_iface, "eth0");
}

TEST(main_send_frame_failure_and_pve_pages) {
    char *argv[] = {"homelab-screen", "--interface", "eth0", NULL};

    g_mock_nanosleep_enabled = 1;
    g_mock_nanosleep_stop_after = 1;

    g_mock_fs_enabled = 1;
    mock_set_file("/proc/stat", "cpu 100 0 100 100 0 0 0\n", 0);
    mock_set_file("/sys/class/thermal/thermal_zone0/temp", "42000\n", 0);
    mock_set_file("/proc/meminfo", "MemTotal: 1000 kB\nMemAvailable: 500 kB\n", 0);
    mock_set_file("/proc/uptime", "100.0 0.0\n", 0);
    mock_set_file("/proc/loadavg", "1.0 2.0 3.0 0/0 1\n", 0);
    mock_set_file("/sys/class/net/eth0/statistics/rx_bytes", "100\n", 0);
    mock_set_file("/sys/class/net/eth0/statistics/tx_bytes", "200\n", 0);

    g_mock_access_enabled = 1;
    mock_set_access("/usr/bin/pvesh", 0);
    mock_set_access("/usr/sbin/qm", -1);

    g_mock_proc_enabled = 1;
    mock_add_cmd("qm list 2>/dev/null", "VMID NAME STATUS\n", 0, 0);
    mock_add_cmd("pct list 2>/dev/null", "VMID NAME STATUS\n", 0, 0);
    mock_add_cmd("pvesh get /nodes/", "[]", 0, 1);
    mock_add_cmd("pveversion 2>/dev/null", "pve-manager/8.2\n", 0, 0);

    time_t times[] = {100, 100, 111};
    mock_set_times(times, 3);

    mock_libusb_bulk_transfer_rc = -1;
    ASSERT_EQ(homelab_screen_main(3, argv), 0);
    ASSERT_EQ(g_pve_metrics.pve_available, 1);
    ASSERT_EQ(last_pve_collect, (time_t)111);
    ASSERT_EQ(g_mock_nanosleep_calls, 0);
    ASSERT_EQ(mock_libusb_bulk_calls, 1);
}

/* ===== test runner ===== */

int main(void) {
    printf("homelab-screen unit tests\n");
    printf("=======================\n\n");

    printf("[Render]\n");
    RUN(set_pixel_valid);
    RUN(set_pixel_out_of_bounds);
    RUN(fill_rect_clipping);
    RUN(draw_char_and_strings);
    RUN(progress_and_circle);
    RUN(format_bytes_helpers);
    RUN(render_pages_all_paths);

    printf("\n[CLI]\n");
    RUN(parse_hex_and_int_helpers);
    RUN(parse_args_valid_and_invalid);
    RUN(parse_args_help_exits);

    printf("\n[Metrics]\n");
    RUN(compute_counter_rate_cases);
    RUN(get_cpu_usage_paths);
    RUN(get_memory_temp_host_load_uptime_paths);
    RUN(detect_network_interface_paths);
    RUN(get_network_rates_and_collect_metrics);

    printf("\n[Proxmox]\n");
    RUN(find_helpers_and_json_parse_paths);
    RUN(check_pve_available_paths);
    RUN(get_pve_vms_cts_and_version);
    RUN(get_pve_storage_json_and_fallback);
    RUN(collect_proxmox_metrics_paths);

    printf("\n[USB]\n");
    RUN(build_header_values);
    RUN(usb_init_error_paths);
    RUN(usb_init_success_and_cleanup);
    RUN(send_frame_paths);
    RUN(mock_libusb_direct_paths);

    printf("\n[Main]\n");
    RUN(signal_handler_sets_running_zero);
    RUN(main_parse_failure);
    RUN(main_usb_init_failure);
    RUN(main_success_single_loop_with_page_switch);
    RUN(main_send_frame_failure_and_pve_pages);

    printf("\n=======================\n");
    printf("Results: %d passed, %d failed\n", g_pass, g_fail);

    return g_fail > 0 ? 1 : 0;
}
