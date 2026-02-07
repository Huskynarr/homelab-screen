/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 homelab-screen contributors
 */

#ifndef TRLCD_H
#define TRLCD_H

#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef TESTING
#include "mock_libusb.h"
#else
#include <libusb.h>
#endif

#define LCD_W 240
#define LCD_H 320
#define FRAME_SIZE (LCD_W * LCD_H * 2)
#define PACKET_SIZE 512

typedef struct {
    char hostname[64];
    float cpu_temp;
    float cpu_usage;
    uint64_t mem_used;
    uint64_t mem_total;
    float mem_pct;
    float load_1;
    float load_5;
    float load_15;
    uint64_t uptime_secs;
    float net_rx_rate;
    float net_tx_rate;
    char net_iface[32];
} Metrics;

typedef struct {
    int running_vms;
    int total_vms;
    int running_cts;
    int total_cts;
    char pve_version[32];
    char node_name[64];
    int pve_available;
    struct {
        char name[32];
        float used_pct;
        uint64_t used_bytes;
        uint64_t total_bytes;
    } storage[8];
    int storage_count;
} ProxmoxMetrics;

extern uint16_t g_vid;
extern uint16_t g_pid;
extern int g_interval;
extern char g_cli_iface[32];

extern uint16_t framebuffer[LCD_W * LCD_H];
extern volatile sig_atomic_t g_running;

extern Metrics g_metrics;
extern uint64_t last_net_rx;
extern uint64_t last_net_tx;
extern time_t last_net_time;
extern uint64_t last_cpu_idle;
extern uint64_t last_cpu_total;

extern ProxmoxMetrics g_pve_metrics;
extern time_t last_pve_collect;

extern libusb_device_handle *dev_handle;
extern unsigned char g_ep_out;
extern int g_usb_iface;

void signal_handler(int sig);

void detect_network_interface(void);
void get_hostname(char *buf, size_t len);
void collect_metrics(void);

void check_pve_available(void);
void collect_proxmox_metrics(void);

void render_page_overview(void);
void render_page_cpu(void);
void render_page_memory(void);
void render_page_network(void);
void render_page_system(void);
void render_page_proxmox(void);
void render_page_storage(void);

int usb_init(void);
void usb_cleanup(void);
int send_frame(void);

int parse_args(int argc, char **argv);

#endif
