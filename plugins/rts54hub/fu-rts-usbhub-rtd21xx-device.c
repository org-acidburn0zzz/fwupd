/*
 * Copyright (C) 2021 Realtek Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-chunk.h"

#include "fu-rts-usbhub-rtd21xx-device.h"
#include "fu-rts54hub-device.h"

struct _FuRtsUsbhubRtd21xxDevice
{
	FuDevice 		parent_instance;
	guint8			target_addr;
	guint8			i2c_speed;
	guint8			register_addr_len;
};

G_DEFINE_TYPE (FuRtsUsbhubRtd21xxDevice, fu_rts_usbhub_rtd21xx_device, FU_TYPE_DEVICE)

#define I2C_DELAY_AFTER_SEND		5000	/* us */

#define UC_FOREGROUND_SLAVE_ADDR	0x3A
#define UC_FOREGROUND_STATUS		0x31
#define UC_FOREGROUND_OPCODE		0x33
#define UC_FOREGROUND_ISP_DATA_OPCODE	0x34

#define ISP_DATA_BLOCKSIZE		30
#define ISP_PACKET_SIZE			32

typedef enum {
	ISP_STATUS_BUSY			= 0xBB,		/* host must wait for device */
	ISP_STATUS_IDLE_SUCCESS		= 0x11,		/* previous command was OK */
	ISP_STATUS_IDLE_FAILURE		= 0x12,		/* previous command failed */
} IspStatus;

typedef enum {
	ISP_CMD_ENTER_FW_UPDATE		= 0x01,
	ISP_CMD_GET_PROJECT_ID_ADDR	= 0x02,
	ISP_CMD_SYNC_IDENTIFY_CODE	= 0x03,
	ISP_CMD_GET_FW_INFO		= 0x04,
	ISP_CMD_FW_UPDATE_START		= 0x05,
	ISP_CMD_FW_UPDATE_ISP_DONE	= 0x06,
	ISP_CMD_FW_UPDATE_EXIT		= 0x07,
	ISP_CMD_FW_UPDATE_RESET		= 0x08,
} IspCmd;

guint8 g_ucSlaveAddr = 0xFF;

static void
fu_rts54hub_module_to_string (FuDevice *module, guint idt, GString *str)
{
	FuRtsUsbhubRtd21xxDevice *self = FU_RTS_USBHUB_RTD21XX_DEVICE (module);
	fu_common_string_append_kx (str, idt, "TargetAddr", self->target_addr);
	fu_common_string_append_kx (str, idt, "I2cSpeed", self->i2c_speed);
	fu_common_string_append_kx (str, idt, "RegisterAddrLen", self->register_addr_len);
}

static gboolean
fu_rts54hub_module_set_quirk_kv (FuDevice *device,
				 const gchar *key,
				 const gchar *value,
				 GError **error)
{
	FuRtsUsbhubRtd21xxDevice *self = FU_RTS_USBHUB_RTD21XX_DEVICE (device);
	/* load target address from quirks */
	if (g_strcmp0 (key, "Rts54TargetAddr") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		if (tmp <= 0xff) {
			self->target_addr = tmp;
			return TRUE;
		}
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid target address");
		return FALSE;
	}

	/* load i2c speed from quirks */
	if (g_strcmp0 (key, "Rts54I2cSpeed") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		if (tmp < I2C_BUS_SPEED_800K) {
			self->i2c_speed = tmp;
			return TRUE;
		}
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid I²C speed");
		return FALSE;
	}

	/* load register address length from quirks */
	if (g_strcmp0 (key, "Rts54RegisterAddrLen") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		if (tmp <= 0xff) {
			self->register_addr_len = tmp;
			return TRUE;
		}
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid register address length");
		return FALSE;
	}

	/* failed */
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "quirk key not supported");
	return FALSE;

}

static gboolean
fu_rts54hub_module_open (FuDevice *device, GError **error)
{
	FuDevice *parent = fu_device_get_parent (device);
	if (parent == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "no parent device");
		return FALSE;
	}
	return fu_device_open (parent, error);
}

static gboolean
fu_rts54hub_module_close (FuDevice *device, GError **error)
{
	FuDevice *parent = fu_device_get_parent (device);
	if (parent == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "no parent device");
		return FALSE;
	}
	return fu_device_close (parent, error);
}

static gboolean
fu_rts_usbhub_device_i2c_write (FuRts54HubDevice *self,
				guint8 slave_addr, guint8 sub_addr,
				guint8 *data, gsize datasz,
				GError **error)
{

    if(self->vendor_cmd == 0)
    {
        if(!fu_rts54hub_device_vendor_cmd(self, 1, error))
        {
            return FALSE;
        }
    }
    if(slave_addr != g_ucSlaveAddr)
    {
        if(!fu_rts54hub_device_i2c_config(self, slave_addr, 1, I2C_BUS_SPEED_200K, error))
        {
            return FALSE;
        }
        g_ucSlaveAddr = slave_addr;    
    }
    
	if (!fu_rts54hub_device_i2c_write (self, sub_addr, data, datasz, error)) {
		g_prefix_error (error,
				"failed to write I2C @0x%02x:%02x: ",
				slave_addr, sub_addr);
		return FALSE;
	}
	g_usleep (I2C_DELAY_AFTER_SEND);
	return TRUE;
}

static gboolean
fu_rts_usbhub_device_i2c_read(FuRts54HubDevice *self,
			       guint8 slave_addr, guint8 sub_addr,
			       guint8 *data, gsize datasz,
			       GError **error)
{
	
    if(self->vendor_cmd == 0)
    {
        if(!fu_rts54hub_device_vendor_cmd(self, 1, error))
        {
            return FALSE;
        }
    }
    
    if(slave_addr != g_ucSlaveAddr)
    {
        if(!fu_rts54hub_device_i2c_config(self, slave_addr, 1, I2C_BUS_SPEED_200K, error))
        {
            return FALSE;
        }
        g_ucSlaveAddr = slave_addr;    
    }
    
	if (!fu_rts54hub_device_i2c_read(self, sub_addr, data, datasz, error)) {
		g_prefix_error (error, "failed to read I2C: ");
		return FALSE;
	}
	return TRUE;

}

static gboolean
fu_rts_usbhub_device_rtd21xx_read_status_raw (FuRtsUsbhubRtd21xxDevice *self,
					      guint8 *status,
					      GError **error)
{
	FuRts54HubDevice *parent = FU_RTS54HUB_DEVICE (fu_device_get_parent (FU_DEVICE (self)));
	guint8 buf[] = { 0x00 };
	if (!fu_rts_usbhub_device_i2c_read (parent,
					    UC_FOREGROUND_SLAVE_ADDR,
					    UC_FOREGROUND_STATUS,
					    buf, sizeof(buf),
					    error))
		return FALSE;
	if (status != NULL)
		*status = buf[0];
	return TRUE;
}

static gboolean
fu_rts_usbhub_device_rtd21xx_read_status_cb (FuDevice *device,
					     gpointer user_data,
					     GError **error)
{
	FuRtsUsbhubRtd21xxDevice *self = FU_RTS_USBHUB_RTD21XX_DEVICE (device);
	guint8 status = 0xfd;
	if (!fu_rts_usbhub_device_rtd21xx_read_status_raw (self, &status, error))
		return FALSE;
	if (status == ISP_STATUS_BUSY) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "status was 0x%02x", status);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_rts_usbhub_device_rtd21xx_read_status (FuRtsUsbhubRtd21xxDevice *self,
					  guint8 *status,
					  GError **error)
{
	return fu_device_retry (FU_DEVICE (self),
				fu_rts_usbhub_device_rtd21xx_read_status_cb,
				4200, status, error);
}

static gboolean
fu_rts_usbhub_rtd21xx_ensure_version_unlocked (FuRtsUsbhubRtd21xxDevice *self,
					      GError **error)
{
	FuRts54HubDevice *parent = FU_RTS54HUB_DEVICE (fu_device_get_parent (FU_DEVICE (self)));
	guint8 buf_rep[7] = { 0x00 };
	guint8 buf_req[] = { ISP_CMD_GET_FW_INFO };
	g_autofree gchar *version = NULL;

	if (!fu_rts_usbhub_device_i2c_write (parent,
					     UC_FOREGROUND_SLAVE_ADDR,
					     UC_FOREGROUND_OPCODE,
					     buf_req, sizeof(buf_req),
					     error)) {
		g_prefix_error (error, "failed to get version number: ");
		return FALSE;
	}

	/* wait for device ready */
	g_usleep (300000);
	if (!fu_rts_usbhub_device_i2c_read (parent,
					    UC_FOREGROUND_SLAVE_ADDR,
					    0x00,
					    buf_rep, sizeof(buf_rep),
					    error)) {
		g_prefix_error (error, "failed to get version number: ");
		return FALSE;
	}

	/* set version */
	version = g_strdup_printf ("%u.%u", buf_rep[1], buf_rep[2]);
	fu_device_set_version (FU_DEVICE (self), version);
	return TRUE;
}

static gboolean
fu_rts_usbhub_device_rtd21xx_detach_raw (FuRtsUsbhubRtd21xxDevice *self, GError **error)
{
    FuRts54HubDevice *parent = FU_RTS54HUB_DEVICE (fu_device_get_parent (FU_DEVICE (self)));
	guint8 buf[] = { 0x03 };
	if (!fu_rts_usbhub_device_i2c_write (parent, 0x6A, 0x31, buf, sizeof(buf), error)) {
		g_prefix_error (error, "failed to detach: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_rts_usbhub_device_rtd21xx_detach_cb (FuDevice *device,
					gpointer user_data,
					GError **error)
{
	FuRtsUsbhubRtd21xxDevice *self = FU_RTS_USBHUB_RTD21XX_DEVICE (device);
	guint8 status = 0xfe;
	if (!fu_rts_usbhub_device_rtd21xx_detach_raw (self, error))
		return FALSE;
	if (!fu_rts_usbhub_device_rtd21xx_read_status_raw (self, &status, error))
		return FALSE;
	if (status != ISP_STATUS_IDLE_SUCCESS) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "detach status was 0x%02x", status);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_rts_usbhub_rtd21xx_device_write_firmware (FuDevice *device,
					     FuFirmware *firmware,
					     FwupdInstallFlags flags,
					     GError **error)
{
    FuRts54HubDevice *parent = FU_RTS54HUB_DEVICE (fu_device_get_parent (device));
	FuRtsUsbhubRtd21xxDevice *self = FU_RTS_USBHUB_RTD21XX_DEVICE (device);
	const guint8 *fwbuf;
	gsize fwbufsz = 0;
	guint32 project_addr;
	guint8 project_id_count;
	guint8 read_buf[10] = { 0 };
	guint8 write_buf[ISP_PACKET_SIZE] = {0};
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* open device */
	locker = fu_device_locker_new (parent, error);
	if (locker == NULL)
		return FALSE;

	/* simple image */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;
	fwbuf = g_bytes_get_data (fw, &fwbufsz);

	/* enable ISP high priority */
	write_buf[0] = ISP_CMD_ENTER_FW_UPDATE;
	write_buf[1] = 0x01;
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_BUSY);
	if (!fu_rts_usbhub_device_i2c_write (parent,
					     UC_FOREGROUND_SLAVE_ADDR,
					     UC_FOREGROUND_OPCODE,
					     write_buf,
					     2,
					     error)) {
		g_prefix_error (error, "failed to enable ISP: ");
		return FALSE;
	}
	if (!fu_rts_usbhub_device_rtd21xx_read_status (self, NULL, error))
		return FALSE;

	/* get project ID address */
	write_buf[0] = ISP_CMD_GET_PROJECT_ID_ADDR;
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_READ);
	if (!fu_rts_usbhub_device_i2c_write (parent,
					     UC_FOREGROUND_SLAVE_ADDR,
					     UC_FOREGROUND_OPCODE,
					     write_buf, 1,
					     error)) {
		g_prefix_error (error, "failed to get project ID address: ");
		return FALSE;
	}

	/* read back 6 bytes data */
	g_usleep (I2C_DELAY_AFTER_SEND * 40);
	if (!fu_rts_usbhub_device_i2c_read (parent,
					    UC_FOREGROUND_SLAVE_ADDR,
					    UC_FOREGROUND_STATUS,
					    read_buf, 6,
					    error)) {
		g_prefix_error (error, "failed to read project ID: ");
		return FALSE;
	}
	if (read_buf[0] != ISP_STATUS_IDLE_SUCCESS) {
		g_prefix_error (error, "failed project ID with error 0x%02x: ", read_buf[0]);
		return FALSE;
	}

	/* verify project ID */
	project_addr = fu_common_read_uint32 (read_buf + 1, G_BIG_ENDIAN);
	project_id_count = read_buf[5];
	write_buf[0] = ISP_CMD_SYNC_IDENTIFY_CODE;
	if (!fu_memcpy_safe (write_buf, sizeof(write_buf), 0x1, /* dst */
			     fwbuf, fwbufsz, project_addr,	/* src */
			     project_id_count, error)) {
		g_prefix_error (error, "failed to write project ID from 0x%04x: ", project_addr);
		return FALSE;
	}
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_BUSY);
	if (!fu_rts_usbhub_device_i2c_write (parent,
					     UC_FOREGROUND_SLAVE_ADDR,
					     UC_FOREGROUND_OPCODE,
					     write_buf,
					     project_id_count + 1,
					     error)) {
		g_prefix_error (error, "failed to send fw update start cmd: ");
		return FALSE;
	}
	if (!fu_rts_usbhub_device_rtd21xx_read_status (self, NULL, error))
		return FALSE;

	/* background FW update start command */
	write_buf[0] = ISP_CMD_FW_UPDATE_START;
	fu_common_write_uint16 (write_buf + 1, ISP_DATA_BLOCKSIZE, G_BIG_ENDIAN);
	if (!fu_rts_usbhub_device_i2c_write (parent,
					     UC_FOREGROUND_SLAVE_ADDR,
					     UC_FOREGROUND_OPCODE,
					     write_buf, 3,
					     error)) {
		g_prefix_error (error, "failed to send fw update start cmd: ");
		return FALSE;
	}

	/* send data */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	chunks = fu_chunk_array_new_from_bytes (fw,
						0x00,	/* start addr */
						0x00,	/* page_sz */
						ISP_DATA_BLOCKSIZE);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		if (!fu_rts_usbhub_device_rtd21xx_read_status (self, NULL, error))
			return FALSE;
		if (!fu_rts_usbhub_device_i2c_write (parent,
						     UC_FOREGROUND_SLAVE_ADDR,
						     UC_FOREGROUND_ISP_DATA_OPCODE,
						     (guint8 *) chk->data,
						     chk->data_sz,
						     error)) {
			g_prefix_error (error, "failed to write @0x%04x: ", chk->address);
			return FALSE;
		}

		/* update progress */
		fu_device_set_progress_full (device, (gsize) i, (gsize) chunks->len);
	}

	/* update finish command */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_BUSY);
	if (!fu_rts_usbhub_device_rtd21xx_read_status (self, NULL, error))
		return FALSE;
	write_buf[0] = ISP_CMD_FW_UPDATE_ISP_DONE;
	if (!fu_rts_usbhub_device_i2c_write (parent,
					     UC_FOREGROUND_SLAVE_ADDR,
					     UC_FOREGROUND_OPCODE,
					     write_buf, 1,
					     error)) {
		g_prefix_error (error, "failed update finish cmd: ");
		return FALSE;
	}

	/* exit background-fw mode */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	fu_device_set_progress (device, 0);
	if (!fu_rts_usbhub_device_rtd21xx_read_status (self, NULL, error))
		return FALSE;
	write_buf[0] = ISP_CMD_FW_UPDATE_EXIT;
	if (!fu_rts_usbhub_device_i2c_write (parent,
					     UC_FOREGROUND_SLAVE_ADDR,
					     UC_FOREGROUND_OPCODE,
					     write_buf, 1,
					     error)) {
		g_prefix_error (error, "FwUpdate exit: ");
		return FALSE;
	}

	/* the device needs some time to restart with the new firmware before
	 * it can be queried again */
	fu_device_sleep_with_progress (device, 20);

	/* success */
	return TRUE;
}

static void
fu_rts_usbhub_rtd21xx_device_init (FuRtsUsbhubRtd21xxDevice *self)
{
	//fu_device_add_icon (FU_DEVICE (self), "video-display");
	//fu_device_set_protocol (FU_DEVICE (self), "com.realtek.rts54hub.i2c");
	//fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	//fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_NO_GUID_MATCHING);
	//fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	//fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_PAIR);
	//fu_device_set_install_duration (FU_DEVICE (self), 100); /* seconds */
	//fu_device_set_logical_id (FU_DEVICE (self), "I2C");
	//fu_device_retry_set_delay (FU_DEVICE (self), 30); /* ms */
}

static void
fu_rts_usbhub_rtd21xx_device_class_init (FuRtsUsbhubRtd21xxDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);	
	klass_device->write_firmware = fu_rts_usbhub_rtd21xx_device_write_firmware;
	klass_device->to_string = fu_rts54hub_module_to_string;
	klass_device->set_quirk_kv = fu_rts54hub_module_set_quirk_kv;
	klass_device->open = fu_rts54hub_module_open;
	klass_device->close = fu_rts54hub_module_close;
}

FuRtsUsbhubRtd21xxDevice *
fu_rts_usbhub_rtd21xx_device_new (void)
{
	FuRtsUsbhubRtd21xxDevice *self = NULL;
	self = g_object_new (FU_TYPE_RTS_USBHUB_RTD21XX_DEVICE, NULL);
	return self;
}
