/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 homelab-screen contributors
 */

/*
 * mock_libusb.h - Minimal libusb stubs for unit testing
 * Provides just enough types and functions so homelab-screen.c compiles
 * without linking against the real libusb.
 */
#ifndef MOCK_LIBUSB_H
#define MOCK_LIBUSB_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct libusb_device { int dummy; } libusb_device;
typedef struct libusb_device_handle { int dummy; } libusb_device_handle;

/* Endpoint descriptor */
struct libusb_endpoint_descriptor {
    uint8_t bEndpointAddress;
};

/* Interface descriptor */
struct libusb_interface_descriptor {
    int bInterfaceNumber;
    int bNumEndpoints;
    const struct libusb_endpoint_descriptor *endpoint;
};

/* Interface (array of altsettings) */
struct libusb_interface {
    const struct libusb_interface_descriptor *altsetting;
    int num_altsetting;
};

/* Config descriptor */
struct libusb_config_descriptor {
    int bNumInterfaces;
    const struct libusb_interface *interface;
};

/* Tunable mock state */
static int mock_libusb_init_rc = 0;
static int mock_libusb_open_ok = 1;
static int mock_libusb_set_auto_detach_rc = 0;
static int mock_libusb_claim_interface_rc = 0;
static int mock_libusb_release_interface_rc = 0;
static int mock_libusb_get_active_config_rc = 0;
static int mock_libusb_force_cfg_on_error = 0;
static int mock_libusb_bulk_transfer_rc = 0;
static int mock_libusb_bulk_short_write = 0;
static int mock_libusb_bulk_short_write_after = -1;
static int mock_libusb_bulk_fail_after = -1; /* fail after N successful calls, -1 means never */

static int mock_libusb_claimed_iface = -1;
static int mock_libusb_released_iface = -1;
static int mock_libusb_bulk_calls = 0;

static int mock_libusb_has_out_endpoint = 1;
static int mock_libusb_interface_number = 0;
static unsigned char mock_libusb_endpoint_addr = 0x02;

static inline void mock_libusb_reset(void) {
    mock_libusb_init_rc = 0;
    mock_libusb_open_ok = 1;
    mock_libusb_set_auto_detach_rc = 0;
    mock_libusb_claim_interface_rc = 0;
    mock_libusb_release_interface_rc = 0;
    mock_libusb_get_active_config_rc = 0;
    mock_libusb_force_cfg_on_error = 0;
    mock_libusb_bulk_transfer_rc = 0;
    mock_libusb_bulk_short_write = 0;
    mock_libusb_bulk_short_write_after = -1;
    mock_libusb_bulk_fail_after = -1;
    mock_libusb_claimed_iface = -1;
    mock_libusb_released_iface = -1;
    mock_libusb_bulk_calls = 0;
    mock_libusb_has_out_endpoint = 1;
    mock_libusb_interface_number = 0;
    mock_libusb_endpoint_addr = 0x02;
}

static inline int libusb_init(void *ctx) {
    (void)ctx;
    return mock_libusb_init_rc;
}

static inline void libusb_exit(void *ctx) {
    (void)ctx;
}

static inline libusb_device_handle *libusb_open_device_with_vid_pid(
    void *ctx, uint16_t vid, uint16_t pid) {
    (void)ctx; (void)vid; (void)pid;
    static libusb_device_handle handle;
    return mock_libusb_open_ok ? &handle : NULL;
}

static inline int libusb_set_auto_detach_kernel_driver(
    libusb_device_handle *dev, int enable) {
    (void)dev; (void)enable;
    return mock_libusb_set_auto_detach_rc;
}

static inline int libusb_claim_interface(
    libusb_device_handle *dev, int iface) {
    (void)dev;
    mock_libusb_claimed_iface = iface;
    return mock_libusb_claim_interface_rc;
}

static inline int libusb_release_interface(
    libusb_device_handle *dev, int iface) {
    (void)dev;
    mock_libusb_released_iface = iface;
    return mock_libusb_release_interface_rc;
}

static inline void libusb_close(libusb_device_handle *dev) {
    (void)dev;
}

static inline libusb_device *libusb_get_device(libusb_device_handle *dev) {
    (void)dev;
    static libusb_device device;
    return &device;
}

static inline int libusb_get_active_config_descriptor(
    libusb_device *dev, struct libusb_config_descriptor **cfg) {
    (void)dev;
    if (!cfg) {
        return -1;
    }
    static struct libusb_endpoint_descriptor ep_desc;
    static struct libusb_interface_descriptor iface_desc;
    static struct libusb_interface iface;
    static struct libusb_config_descriptor config;

    if (mock_libusb_get_active_config_rc != 0) {
        if (mock_libusb_force_cfg_on_error) {
            config.bNumInterfaces = 0;
            config.interface = NULL;
            *cfg = &config;
        }
        return mock_libusb_get_active_config_rc;
    }

    ep_desc.bEndpointAddress = mock_libusb_has_out_endpoint
        ? mock_libusb_endpoint_addr
        : (unsigned char)(mock_libusb_endpoint_addr | 0x80);

    iface_desc.bInterfaceNumber = mock_libusb_interface_number;
    iface_desc.bNumEndpoints = 1;
    iface_desc.endpoint = &ep_desc;

    iface.altsetting = &iface_desc;
    iface.num_altsetting = 1;

    config.bNumInterfaces = 1;
    config.interface = &iface;

    *cfg = &config;
    return 0;
}

static inline void libusb_free_config_descriptor(
    struct libusb_config_descriptor *cfg) {
    (void)cfg;
}

static inline int libusb_bulk_transfer(
    libusb_device_handle *dev, unsigned char endpoint,
    unsigned char *data, int length, int *transferred, unsigned int timeout) {
    (void)dev; (void)endpoint; (void)data; (void)length; (void)timeout;

    mock_libusb_bulk_calls++;

    if (mock_libusb_bulk_fail_after >= 0 &&
        mock_libusb_bulk_calls > mock_libusb_bulk_fail_after) {
        if (transferred) {
            *transferred = 0;
        }
        return (mock_libusb_bulk_transfer_rc != 0) ? mock_libusb_bulk_transfer_rc : -1;
    }

    int short_write = mock_libusb_bulk_short_write ||
        (mock_libusb_bulk_short_write_after >= 0 &&
         mock_libusb_bulk_calls > mock_libusb_bulk_short_write_after);

    if (transferred) {
        *transferred = short_write ? (length - 1) : length;
    }
    return mock_libusb_bulk_transfer_rc;
}

static inline const char *libusb_error_name(int code) {
    (void)code;
    return "MOCK_ERROR";
}

#endif /* MOCK_LIBUSB_H */
