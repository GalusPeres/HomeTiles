/*
 * SPDX-FileCopyrightText: 2026 HomeTiles contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <driver/i2c_master.h>

struct DEV_I2C_Port {
  i2c_master_bus_handle_t bus;
};

DEV_I2C_Port DEV_I2C_Init();
