/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 homelab-screen contributors
 */

#include "trlcd.h"

#define PVE_COLLECT_INTERVAL 10

void check_pve_available(void) {
    g_pve_metrics.pve_available = 0;
    if (access("/usr/bin/pvesh", X_OK) == 0 || access("/usr/sbin/qm", X_OK) == 0) {
        g_pve_metrics.pve_available = 1;
    }
    /* Also grab the node name once */
    gethostname(g_pve_metrics.node_name, sizeof(g_pve_metrics.node_name));
    g_pve_metrics.node_name[sizeof(g_pve_metrics.node_name) - 1] = '\0';
}

static void get_pve_vms(void) {
    g_pve_metrics.running_vms = 0;
    g_pve_metrics.total_vms = 0;

    FILE *fp = popen("qm list 2>/dev/null", "r");
    if (!fp) return;

    char line[256];
    int first = 1;
    while (fgets(line, sizeof(line), fp)) {
        if (first) { first = 0; continue; } /* skip header */
        if (line[0] == '\n' || line[0] == '\0') continue;
        g_pve_metrics.total_vms++;
        if (strstr(line, "running"))
            g_pve_metrics.running_vms++;
    }
    pclose(fp);
}

static void get_pve_cts(void) {
    g_pve_metrics.running_cts = 0;
    g_pve_metrics.total_cts = 0;

    FILE *fp = popen("pct list 2>/dev/null", "r");
    if (!fp) return;

    char line[256];
    int first = 1;
    while (fgets(line, sizeof(line), fp)) {
        if (first) { first = 0; continue; }
        if (line[0] == '\n' || line[0] == '\0') continue;
        g_pve_metrics.total_cts++;
        if (strstr(line, "running"))
            g_pve_metrics.running_cts++;
    }
    pclose(fp);
}

static const char *find_in_range(const char *start, const char *end, const char *needle) {
    size_t needle_len = strlen(needle);
    if (!start || !end || !needle || needle_len == 0 || start >= end) {
        return NULL;
    }
    if ((size_t)(end - start) < needle_len) {
        return NULL;
    }
    for (const char *p = start; p <= end - needle_len; p++) {
        if (memcmp(p, needle, needle_len) == 0) {
            return p;
        }
    }
    return NULL;
}

static const char *find_char_in_range(const char *start, const char *end, char c) {
    if (!start || !end || start >= end) {
        return NULL;
    }
    for (const char *p = start; p < end; p++) {
        if (*p == c) {
            return p;
        }
    }
    return NULL;
}

static int parse_json_string_field(const char *obj_start, const char *obj_end,
                                   const char *field, char *out, size_t out_len) {
    if (!obj_start || !obj_end || !field || !out || out_len == 0 || obj_start >= obj_end) {
        return -1;
    }

    char key[64];
    if (snprintf(key, sizeof(key), "\"%s\"", field) >= (int)sizeof(key)) {
        return -1;
    }

    const char *k = find_in_range(obj_start, obj_end, key);
    if (!k) {
        return -1;
    }
    const char *colon = find_char_in_range(k, obj_end, ':');
    if (!colon) {
        return -1;
    }
    const char *quote_start = find_char_in_range(colon + 1, obj_end, '"');
    if (!quote_start) {
        return -1;
    }
    const char *quote_end = find_char_in_range(quote_start + 1, obj_end, '"');
    if (!quote_end) {
        return -1;
    }

    size_t n = (size_t)(quote_end - (quote_start + 1));
    if (n >= out_len) {
        n = out_len - 1;
    }
    memcpy(out, quote_start + 1, n);
    out[n] = '\0';
    return 0;
}

static int parse_json_u64_field(const char *obj_start, const char *obj_end,
                                const char *field, uint64_t *out) {
    if (!obj_start || !obj_end || !field || !out || obj_start >= obj_end) {
        return -1;
    }

    char key[64];
    if (snprintf(key, sizeof(key), "\"%s\"", field) >= (int)sizeof(key)) {
        return -1;
    }

    const char *k = find_in_range(obj_start, obj_end, key);
    if (!k) {
        return -1;
    }
    const char *colon = find_char_in_range(k, obj_end, ':');
    if (!colon) {
        return -1;
    }

    errno = 0;
    char *endptr = NULL;
    unsigned long long val = strtoull(colon + 1, &endptr, 10);
    if (errno != 0 || endptr == colon + 1 || endptr > obj_end) {
        return -1;
    }
    *out = (uint64_t)val;
    return 0;
}

static void get_pve_storage(void) {
    g_pve_metrics.storage_count = 0;

    char node[64];
    snprintf(node, sizeof(node), "%s",
             g_pve_metrics.node_name[0] ? g_pve_metrics.node_name : "localhost");
    for (size_t i = 0; node[i] != '\0'; i++) {
        char c = node[i];
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '-' || c == '_' || c == '.')) {
            node[i] = '_';
        }
    }

    /* Try pvesh first, fallback to df for common PVE paths */
    char pvesh_cmd[192];
    snprintf(pvesh_cmd, sizeof(pvesh_cmd),
             "pvesh get /nodes/%s/storage --output-format json 2>/dev/null", node);
    FILE *fp = popen(pvesh_cmd, "r");
    if (fp) {
        /* Read entire JSON output */
        char buf[16384];
        size_t total = 0;
        size_t n;
        while ((n = fread(buf + total, 1, sizeof(buf) - total - 1, fp)) > 0) {
            total += n;
            if (total >= sizeof(buf) - 1) {
                break;
            }
        }
        buf[total] = '\0';
        int status = pclose(fp);

        if (status == 0 && total > 2) {
            /* Parse per object to avoid field bleed between entries. */
            const char *buf_end = buf + total;
            const char *p = buf;
            while (g_pve_metrics.storage_count < 8) {
                const char *storage_key = find_in_range(p, buf_end, "\"storage\"");
                if (!storage_key) {
                    break;
                }

                const char *obj_start = storage_key;
                while (obj_start > buf && *obj_start != '{') {
                    obj_start--;
                }
                if (*obj_start != '{') {
                    p = storage_key + 9;
                    continue;
                }
                const char *obj_end = find_char_in_range(storage_key, buf_end, '}');
                if (!obj_end) {
                    break;
                }
                obj_end++; /* exclusive */

                int idx = g_pve_metrics.storage_count;

                if (parse_json_string_field(obj_start, obj_end, "storage",
                                            g_pve_metrics.storage[idx].name,
                                            sizeof(g_pve_metrics.storage[idx].name)) != 0) {
                    p = obj_end;
                    continue;
                }

                uint64_t used_val = 0, total_val = 0;
                if (parse_json_u64_field(obj_start, obj_end, "used", &used_val) == 0 &&
                    parse_json_u64_field(obj_start, obj_end, "total", &total_val) == 0) {
                    g_pve_metrics.storage[idx].used_bytes = used_val;
                    g_pve_metrics.storage[idx].total_bytes = total_val;
                    if (total_val > 0) {
                        g_pve_metrics.storage[idx].used_pct =
                            100.0f * (float)used_val / (float)total_val;
                    } else {
                        g_pve_metrics.storage[idx].used_pct = 0.0f;
                    }
                } else {
                    g_pve_metrics.storage[idx].used_bytes = 0;
                    g_pve_metrics.storage[idx].total_bytes = 0;
                    g_pve_metrics.storage[idx].used_pct = 0.0f;
                }

                g_pve_metrics.storage_count++;
                p = obj_end;
            }

            if (g_pve_metrics.storage_count > 0) {
                return;
            }
        }
    }

    /* Fallback: parse df for common PVE storage paths */
    const char *pve_paths[] = {"/var/lib/vz", "/var/lib/pve/local-btrfs", NULL};
    for (int i = 0; pve_paths[i] && g_pve_metrics.storage_count < 8; i++) {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "df -B1 %s 2>/dev/null", pve_paths[i]);
        fp = popen(cmd, "r");
        if (!fp) continue;

        char line[256];
        int first = 1;
        while (fgets(line, sizeof(line), fp)) {
            if (first) { first = 0; continue; }
            uint64_t total_b = 0, used_b = 0;
            char fs[64];
            if (sscanf(line, "%63s %" SCNu64 " %" SCNu64, fs, &total_b, &used_b) >= 3) {
                int idx = g_pve_metrics.storage_count;
                snprintf(g_pve_metrics.storage[idx].name, sizeof(g_pve_metrics.storage[idx].name),
                         "%s", pve_paths[i] + 9); /* trim /var/lib/ prefix */
                g_pve_metrics.storage[idx].used_bytes = used_b;
                g_pve_metrics.storage[idx].total_bytes = total_b;
                if (total_b > 0)
                    g_pve_metrics.storage[idx].used_pct = 100.0f * (float)used_b / (float)total_b;
                else
                    g_pve_metrics.storage[idx].used_pct = 0.0f;
                g_pve_metrics.storage_count++;
            }
        }
        pclose(fp);
    }
}

static void get_pve_version(void) {
    g_pve_metrics.pve_version[0] = '\0';

    FILE *fp = popen("pveversion 2>/dev/null", "r");
    if (fp) {
        if (fgets(g_pve_metrics.pve_version, sizeof(g_pve_metrics.pve_version), fp)) {
            g_pve_metrics.pve_version[strcspn(g_pve_metrics.pve_version, "\n")] = '\0';
        }
        pclose(fp);
        if (g_pve_metrics.pve_version[0] != '\0') return;
    }

    /* Fallback: try reading /etc/pve/.version */
    FILE *f = fopen("/etc/pve/.version", "r");
    if (f) {
        if (fgets(g_pve_metrics.pve_version, sizeof(g_pve_metrics.pve_version), f)) {
            g_pve_metrics.pve_version[strcspn(g_pve_metrics.pve_version, "\n")] = '\0';
        }
        fclose(f);
    }
}

void collect_proxmox_metrics(void) {
    if (!g_pve_metrics.pve_available) return;

    time_t now = time(NULL);
    if (now - last_pve_collect < PVE_COLLECT_INTERVAL) return;
    last_pve_collect = now;

    get_pve_vms();
    get_pve_cts();
    get_pve_storage();
    get_pve_version();
}
