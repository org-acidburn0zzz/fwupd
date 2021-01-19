/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_RTS_USBHUB_RTD21XX_DEVICE (fu_rts_usbhub_rtd21xx_device_get_type ())
G_DECLARE_FINAL_TYPE (FuRtsUsbhubRtd21xxDevice, fu_rts_usbhub_rtd21xx_device, FU, RTS_USBHUB_RTD21XX_DEVICE, FuDevice)

FuRtsUsbhubRtd21xxDevice *fu_rts_usbhub_rtd21xx_device_new	(void);
