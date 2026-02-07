/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 homelab-screen contributors
 */

#include "trlcd.h"

int main(int argc, char **argv) {
    if (parse_args(argc, argv) < 0) {
        return 1;
    }

    printf("homelab-screen - Thermalright AIO Cooler USB LCD System Monitor\n");
    printf("Display: %dx%d, Page interval: %d seconds\n", LCD_W, LCD_H, g_interval);

    /* Install signal handlers for graceful shutdown */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    if (usb_init() < 0) {
        return 1;
    }

    /* Detect network interface once at startup */
    detect_network_interface();
    get_hostname(g_metrics.hostname, sizeof(g_metrics.hostname));
    printf("Network interface: %s\n", g_metrics.net_iface);

    /* Check for Proxmox environment */
    check_pve_available();
    if (g_pve_metrics.pve_available) {
        printf("Proxmox VE detected, enabling PVE pages\n");
        collect_proxmox_metrics();
    }

    int current_page = 0;
    time_t last_page_switch = time(NULL);

    /* Build renderer list: base pages + conditional Proxmox pages */
    void (*renderers[8])(void);
    int num_pages = 0;
    renderers[num_pages++] = render_page_overview;
    renderers[num_pages++] = render_page_cpu;
    renderers[num_pages++] = render_page_memory;
    renderers[num_pages++] = render_page_network;
    renderers[num_pages++] = render_page_system;
    if (g_pve_metrics.pve_available) {
        renderers[num_pages++] = render_page_proxmox;
        renderers[num_pages++] = render_page_storage;
    }

    printf("Starting display loop (%d pages, Ctrl+C to exit)...\n", num_pages);

    while (g_running) {
        collect_metrics();
        collect_proxmox_metrics();

        /* Check for page switch */
        time_t now = time(NULL);
        if (now - last_page_switch >= g_interval) {
            current_page = (current_page + 1) % num_pages;
            last_page_switch = now;
            printf("\rPage %d/%d ", current_page + 1, num_pages);
            fflush(stdout);
        }

        /* Render current page */
        renderers[current_page]();

        /* Send to display */
        if (send_frame() < 0) {
            fprintf(stderr, "\nUSB send failed, exiting.\n");
            break;
        }

        /* ~10 FPS for smooth updates */
        struct timespec ts = {0, 100000000}; /* 100ms */
        nanosleep(&ts, NULL);
    }

    printf("\nShutting down...\n");
    usb_cleanup();
    return 0;
}
