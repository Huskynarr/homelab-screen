/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 homelab-screen contributors
 */

/*
 * Compatibility translation unit for unit tests.
 *
 * Production builds compile the modular sources in src/.
 * tests/test_homelab_screen.c includes this file directly to access internals.
 */

#include "src/trlcd.h"

#include "src/state.c"
#include "src/metrics.c"
#include "src/proxmox.c"
#include "src/render.c"
#include "src/usb.c"
#include "src/cli.c"
#include "src/main.c"
