/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

struct _FuRts54HubDevice {
	FuUsbDevice			 parent_instance;
	gboolean			 fw_auth;
	gboolean			 dual_bank;
	gboolean			 running_on_flash;
	guint8				 vendor_cmd;
};
#define FU_TYPE_RTS54HUB_DEVICE (fu_rts54hub_device_get_type ())

#define I2C_BUS_SPEED_100K						0
#define I2C_BUS_SPEED_200K						1
#define I2C_BUS_SPEED_300K						2
#define I2C_BUS_SPEED_400K						3
#define I2C_BUS_SPEED_500K						4
#define I2C_BUS_SPEED_600K						5
#define I2C_BUS_SPEED_700K						6
#define I2C_BUS_SPEED_800K						7

G_DECLARE_FINAL_TYPE (FuRts54HubDevice, fu_rts54hub_device, FU, RTS54HUB_DEVICE, FuUsbDevice)

gboolean fu_rts54hub_device_vendor_cmd (FuRts54HubDevice *self, guint8 value, GError **error);
gboolean fu_rts54hub_device_i2c_config (FuRts54HubDevice *self, guint8 ucSlaveAddr,
	guint8 ucSublen, guint8 ucSpeed, GError **error);
gboolean fu_rts54hub_device_i2c_write (FuRts54HubDevice *self, guint32 sub_addr,
	guint8 *data, gsize datasz, GError **error);
gboolean fu_rts54hub_device_i2c_read (FuRts54HubDevice *self, guint32 sub_addr,
	guint8 *data, gsize datasz, GError **error);
