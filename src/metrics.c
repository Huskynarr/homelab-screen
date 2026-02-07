/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 homelab-screen contributors
 */

#include "trlcd.h"

static int get_cpu_usage(float *usage) {
    *usage = 0.0f;
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;

    uint64_t user, nice, system, idle, iowait, irq, softirq;
    if (fscanf(f, "cpu %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64
               " %" SCNu64 " %" SCNu64 " %" SCNu64,
               &user, &nice, &system, &idle, &iowait, &irq, &softirq) != 7) {
        fclose(f);
        return -1;
    }
    fclose(f);

    uint64_t total_idle = idle + iowait;
    uint64_t total = user + nice + system + idle + iowait + irq + softirq;

    if (last_cpu_total > 0) {
        uint64_t d_idle = total_idle - last_cpu_idle;
        uint64_t d_total = total - last_cpu_total;
        if (d_total > 0) {
            *usage = 100.0f * (1.0f - (float)d_idle / (float)d_total);
        }
    }
    last_cpu_idle = total_idle;
    last_cpu_total = total;
    return 0;
}

static int get_memory_info(uint64_t *used, uint64_t *total) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;

    uint64_t mem_total = 0, mem_avail = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemTotal:", 9) == 0)
            sscanf(line + 9, " %" SCNu64, &mem_total);
        else if (strncmp(line, "MemAvailable:", 13) == 0)
            sscanf(line + 13, " %" SCNu64, &mem_avail);
    }
    fclose(f);

    *total = mem_total * 1024;
    *used = (mem_total - mem_avail) * 1024;
    return 0;
}

static int get_cpu_temp(float *temp) {
    /* Try common thermal zone paths */
    const char *paths[] = {
        "/sys/class/thermal/thermal_zone0/temp",
        "/sys/class/hwmon/hwmon0/temp1_input",
        "/sys/class/hwmon/hwmon1/temp1_input",
        NULL
    };

    for (int i = 0; paths[i]; i++) {
        FILE *f = fopen(paths[i], "r");
        if (f) {
            int t;
            if (fscanf(f, "%d", &t) == 1) {
                *temp = t / 1000.0f;
                fclose(f);
                return 0;
            }
            fclose(f);
        }
    }
    return -1;
}

void get_hostname(char *buf, size_t len) {
    FILE *f = fopen("/etc/hostname", "r");
    if (f) {
        if (fgets(buf, len, f)) {
            buf[strcspn(buf, "\n")] = 0;
        }
        fclose(f);
    } else {
        gethostname(buf, len);
        buf[len - 1] = '\0';
    }
}

static void get_uptime(uint64_t *secs) {
    FILE *f = fopen("/proc/uptime", "r");
    if (f) {
        double up;
        if (fscanf(f, "%lf", &up) == 1) {
            *secs = (uint64_t)up;
        }
        fclose(f);
    }
}

static void get_load_avg(float *l1, float *l5, float *l15) {
    FILE *f = fopen("/proc/loadavg", "r");
    if (f) {
        if (fscanf(f, "%f %f %f", l1, l5, l15) != 3) {
            *l1 = *l5 = *l15 = 0.0f;
        }
        fclose(f);
    }
}

/* ========== Network Metrics ========== */

void detect_network_interface(void) {
    /* If CLI override is set, use that */
    if (g_cli_iface[0] != '\0') {
        snprintf(g_metrics.net_iface, sizeof(g_metrics.net_iface), "%s", g_cli_iface);
        return;
    }

    DIR *d = opendir("/sys/class/net");
    if (!d) {
        snprintf(g_metrics.net_iface, sizeof(g_metrics.net_iface), "eth0");
        return;
    }

    char best[32] = "";
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.' || strcmp(ent->d_name, "lo") == 0)
            continue;

        /* Interface names are expected to fit into our runtime buffer. */
        char iface[sizeof(best)];
        size_t iface_len = strnlen(ent->d_name, sizeof(iface));
        if (iface_len == 0 || iface_len >= sizeof(iface)) {
            continue;
        }
        memcpy(iface, ent->d_name, iface_len);
        iface[iface_len] = '\0';

        char path[128];
        int n = snprintf(path, sizeof(path), "/sys/class/net/%s/carrier", iface);
        if (n <= 0 || (size_t)n >= sizeof(path)) {
            continue;
        }
        FILE *f = fopen(path, "r");
        if (f) {
            int carrier = 0;
            if (fscanf(f, "%d", &carrier) == 1 && carrier == 1) {
                strncpy(best, iface, sizeof(best) - 1);
                best[sizeof(best) - 1] = '\0';
                fclose(f);
                break;
            }
            fclose(f);
        }
        /* Remember first non-lo interface as fallback */
        if (best[0] == '\0') {
            strncpy(best, iface, sizeof(best) - 1);
            best[sizeof(best) - 1] = '\0';
        }
    }
    closedir(d);

    if (best[0] != '\0') {
        snprintf(g_metrics.net_iface, sizeof(g_metrics.net_iface), "%s", best);
    } else {
        snprintf(g_metrics.net_iface, sizeof(g_metrics.net_iface), "eth0");
    }
}

static float compute_counter_rate(uint64_t current, uint64_t previous, time_t dt) {
    if (dt <= 0 || current < previous) {
        return 0.0f;
    }
    return (float)(current - previous) / (float)dt;
}

static void get_network_rates(void) {
    if (g_metrics.net_iface[0] == '\0') return;

    char path[128];
    uint64_t rx = 0, tx = 0;

    snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/rx_bytes",
             g_metrics.net_iface);
    FILE *f = fopen(path, "r");
    if (f) {
        if (fscanf(f, "%" SCNu64, &rx) != 1) rx = 0;
        fclose(f);
    }

    snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/tx_bytes",
             g_metrics.net_iface);
    f = fopen(path, "r");
    if (f) {
        if (fscanf(f, "%" SCNu64, &tx) != 1) tx = 0;
        fclose(f);
    }

    time_t now = time(NULL);
    if (last_net_time > 0 && now > last_net_time) {
        time_t dt = now - last_net_time;
        g_metrics.net_rx_rate = compute_counter_rate(rx, last_net_rx, dt);
        g_metrics.net_tx_rate = compute_counter_rate(tx, last_net_tx, dt);
    }

    last_net_rx = rx;
    last_net_tx = tx;
    last_net_time = now;
}

void collect_metrics(void) {
    get_cpu_usage(&g_metrics.cpu_usage);
    get_cpu_temp(&g_metrics.cpu_temp);
    get_memory_info(&g_metrics.mem_used, &g_metrics.mem_total);
    if (g_metrics.mem_total > 0) {
        g_metrics.mem_pct = 100.0f * g_metrics.mem_used / g_metrics.mem_total;
    } else {
        g_metrics.mem_pct = 0.0f;
    }
    get_uptime(&g_metrics.uptime_secs);
    get_load_avg(&g_metrics.load_1, &g_metrics.load_5, &g_metrics.load_15);
    get_network_rates();
}
