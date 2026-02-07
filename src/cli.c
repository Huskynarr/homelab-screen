/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 homelab-screen contributors
 */

#include "trlcd.h"

static int parse_hex_u16(const char *arg, uint16_t *out) {
    if (!arg || !out || arg[0] == '\0') {
        return -1;
    }
    errno = 0;
    char *endptr = NULL;
    unsigned long val = strtoul(arg, &endptr, 16);
    if (errno != 0 || endptr == arg || *endptr != '\0' || val > UINT16_MAX) {
        return -1;
    }
    *out = (uint16_t)val;
    return 0;
}

static int parse_positive_int(const char *arg, int *out) {
    if (!arg || !out || arg[0] == '\0') {
        return -1;
    }
    errno = 0;
    char *endptr = NULL;
    long val = strtol(arg, &endptr, 10);
    if (errno != 0 || endptr == arg || *endptr != '\0' || val < 1 || val > INT_MAX) {
        return -1;
    }
    *out = (int)val;
    return 0;
}

static void print_usage(const char *progname) {
    printf("Usage: %s [OPTIONS]\n\n", progname);
    printf("Options:\n");
    printf("  --vid HEX        USB Vendor ID  (default: 0x%04X)\n", 0x0416);
    printf("  --pid HEX        USB Product ID (default: 0x%04X)\n", 0x5302);
    printf("  --interval SECS  Page rotation interval (default: %d)\n", 7);
    printf("  --interface NAME  Network interface (default: auto-detect)\n");
    printf("  --help            Show this help message\n");
}

int parse_args(int argc, char **argv) {
    static struct option long_opts[] = {
        {"vid",       required_argument, NULL, 'V'},
        {"pid",       required_argument, NULL, 'P'},
        {"interval",  required_argument, NULL, 'i'},
        {"interface", required_argument, NULL, 'n'},
        {"help",      no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    optind = 1;
    int opt;
    while ((opt = getopt_long(argc, argv, "h", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'V': {
            uint16_t val;
            if (parse_hex_u16(optarg, &val) != 0) {
                fprintf(stderr, "Invalid VID: %s\n", optarg);
                return -1;
            }
            g_vid = val;
            break;
        }
        case 'P': {
            uint16_t val;
            if (parse_hex_u16(optarg, &val) != 0) {
                fprintf(stderr, "Invalid PID: %s\n", optarg);
                return -1;
            }
            g_pid = val;
            break;
        }
        case 'i': {
            int val;
            if (parse_positive_int(optarg, &val) != 0) {
                fprintf(stderr, "Invalid interval: %s\n", optarg);
                return -1;
            }
            g_interval = val;
            break;
        }
        case 'n':
            if (optarg[0] == '\0' || strlen(optarg) >= sizeof(g_cli_iface)) {
                fprintf(stderr, "Invalid interface: %s\n", optarg);
                return -1;
            }
            snprintf(g_cli_iface, sizeof(g_cli_iface), "%s", optarg);
            break;
        case 'h':
            print_usage(argv[0]);
            exit(0);
        default:
            print_usage(argv[0]);
            return -1;
        }
    }
    return 0;
}
