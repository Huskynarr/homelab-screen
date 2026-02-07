/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 homelab-screen contributors
 */

#include "trlcd.h"

uint16_t g_vid = 0x0416;
uint16_t g_pid = 0x5302;
int g_interval = 7;
char g_cli_iface[32] = "";

uint16_t framebuffer[LCD_W * LCD_H];
volatile sig_atomic_t g_running = 1;

Metrics g_metrics;
uint64_t last_net_rx = 0;
uint64_t last_net_tx = 0;
time_t last_net_time = 0;
uint64_t last_cpu_idle = 0;
uint64_t last_cpu_total = 0;

ProxmoxMetrics g_pve_metrics;
time_t last_pve_collect = 0;

libusb_device_handle *dev_handle = NULL;
unsigned char g_ep_out = 0x02;
int g_usb_iface = -1;

void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}
