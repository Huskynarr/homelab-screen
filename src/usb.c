/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 homelab-screen contributors
 */

#include "trlcd.h"

int usb_init(void) {
    struct libusb_config_descriptor *cfg = NULL;
    int rc = libusb_init(NULL);
    if (rc < 0) {
        fprintf(stderr, "Failed to init libusb\n");
        return -1;
    }

    dev_handle = libusb_open_device_with_vid_pid(NULL, g_vid, g_pid);
    if (!dev_handle) {
        fprintf(stderr, "Device not found (VID:%04X PID:%04X)\n", g_vid, g_pid);
        libusb_exit(NULL);
        return -1;
    }

    g_ep_out = 0;
    g_usb_iface = -1;

    /* Find OUT endpoint together with its interface number. */
    libusb_device *dev = libusb_get_device(dev_handle);
    rc = libusb_get_active_config_descriptor(dev, &cfg);
    if (rc < 0 || !cfg) {
        fprintf(stderr, "Failed to read USB configuration: %s\n", libusb_error_name(rc));
        goto fail;
    }

    for (int i = 0; i < cfg->bNumInterfaces; i++) {
        const struct libusb_interface *iface = &cfg->interface[i];
        for (int a = 0; a < iface->num_altsetting; a++) {
            const struct libusb_interface_descriptor *alt = &iface->altsetting[a];
            for (int e = 0; e < alt->bNumEndpoints; e++) {
                const struct libusb_endpoint_descriptor *ep = &alt->endpoint[e];
                if ((ep->bEndpointAddress & 0x80) == 0) { /* OUT endpoint */
                    g_ep_out = ep->bEndpointAddress;
                    g_usb_iface = alt->bInterfaceNumber;
                    goto endpoint_found;
                }
            }
        }
    }

endpoint_found:
    if (cfg) {
        libusb_free_config_descriptor(cfg);
        cfg = NULL;
    }

    if (g_ep_out == 0 || g_usb_iface < 0) {
        fprintf(stderr, "No usable USB OUT endpoint found.\n");
        goto fail;
    }

    rc = libusb_set_auto_detach_kernel_driver(dev_handle, 1);
    if (rc < 0) {
        fprintf(stderr, "Warning: could not auto-detach kernel driver: %s\n", libusb_error_name(rc));
    }

    rc = libusb_claim_interface(dev_handle, g_usb_iface);
    if (rc < 0) {
        fprintf(stderr, "Failed to claim interface %d: %s\n", g_usb_iface, libusb_error_name(rc));
        goto fail;
    }

    printf("Found OUT endpoint: 0x%02X on interface %d\n", g_ep_out, g_usb_iface);
    printf("USB device opened successfully\n");
    return 0;

fail:
    if (cfg) {
        libusb_free_config_descriptor(cfg);
    }
    if (dev_handle) {
        libusb_close(dev_handle);
        dev_handle = NULL;
    }
    g_ep_out = 0;
    g_usb_iface = -1;
    libusb_exit(NULL);
    return -1;
}

void usb_cleanup(void) {
    if (dev_handle) {
        if (g_usb_iface >= 0) {
            libusb_release_interface(dev_handle, g_usb_iface);
        }
        libusb_close(dev_handle);
        dev_handle = NULL;
    }
    g_ep_out = 0;
    g_usb_iface = -1;
    libusb_exit(NULL);
}

static void build_header(uint8_t hdr[PACKET_SIZE]) {
    memset(hdr, 0, PACKET_SIZE);
    hdr[0] = 0xDA; hdr[1] = 0xDB; hdr[2] = 0xDC; hdr[3] = 0xDD; /* magic */
    hdr[4] = 0x02; hdr[5] = 0x00;   /* ver=2 */
    hdr[6] = 0x01; hdr[7] = 0x00;   /* cmd=1 */
    hdr[8] = 0xF0; hdr[9] = 0x00;   /* H=240 */
    hdr[10] = 0x40; hdr[11] = 0x01; /* W=320 */
    hdr[12] = 0x02; hdr[13] = 0x00; /* fmt=2 (RGB565) */
    hdr[22] = 0x00; hdr[23] = 0x58; hdr[24] = 0x02; hdr[25] = 0x00; /* frame_len = 0x00025800 */
    hdr[26] = 0x00; hdr[27] = 0x00; hdr[28] = 0x00; hdr[29] = 0x08; /* extra */
}

int send_frame(void) {
    uint8_t packet[PACKET_SIZE];
    int transferred;
    int rc;

    /* Send header packet first */
    build_header(packet);
    rc = libusb_bulk_transfer(dev_handle, g_ep_out, packet, PACKET_SIZE, &transferred, 1000);
    if (rc < 0 || transferred != PACKET_SIZE) {
        if (rc < 0) {
            fprintf(stderr, "USB header transfer failed: %s\n", libusb_error_name(rc));
        } else {
            fprintf(stderr, "USB header transfer short write: %d/%d\n", transferred, PACKET_SIZE);
        }
        return -1;
    }

    /* Convert framebuffer (portrait 240x320) and send */
    static uint8_t frame_data[FRAME_SIZE];

    for (int y = 0; y < LCD_H; y++) {
        for (int x = 0; x < LCD_W; x++) {
            uint16_t pixel = framebuffer[y * LCD_W + x];
            int idx = (y * LCD_W + x) * 2;
            frame_data[idx] = pixel & 0xFF;      /* Low byte first */
            frame_data[idx + 1] = pixel >> 8;    /* High byte second */
        }
    }

    /* Send pixel data in 512-byte packets */
    for (int offset = 0; offset < FRAME_SIZE; offset += PACKET_SIZE) {
        int chunk = FRAME_SIZE - offset;
        if (chunk > PACKET_SIZE) chunk = PACKET_SIZE;

        memset(packet, 0, PACKET_SIZE);
        memcpy(packet, frame_data + offset, chunk);

        rc = libusb_bulk_transfer(dev_handle, g_ep_out, packet, PACKET_SIZE, &transferred, 1000);
        if (rc < 0 || transferred != PACKET_SIZE) {
            if (rc < 0) {
                fprintf(stderr, "USB data transfer failed: %s\n", libusb_error_name(rc));
            } else {
                fprintf(stderr, "USB data transfer short write at offset %d: %d/%d\n",
                        offset, transferred, PACKET_SIZE);
            }
            return -1;
        }
    }

    return 0;
}
