/*
 * Copyright 2017 Rockchip Inc.
 * Author: Eric Gao <eric.gao@rock-chips.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#define DEBUG
#include <common.h>
#include <dm.h>
#include <dm/device.h>
#include <errno.h>
#include <lcd.h>
#include <mapmem.h>
#include <bmp_layout.h>
#include <rockchip/misc.h>
#include <rockchip/rockchip_logo.h>
#include <video.h>

/*
 *  We use this function to draw a logo on panel, it finally call bmp_dispaly
 *  function, which can get bmp width, bmp height and fb information by self.
 *  So we just need to provide the x, y position for start point and address
 *  of bmp file.
 *  For the bmp file, we use "xxd -i rockchip_logo.bmp rockchip_logo.h" cmd to
 *  convert bmp picture into ascill file, which contain all the bmp header info.
 *  Move it to include/rockchip/ directory and include it in this file. One
 *  more thing, make sure the bmp's pixel width is integral multiple of 32, so
 *  that the data is aligned.
 *  To enable this function, make sure following macro definition is defined:
 *
 *  #define CONFIG_BOARD_COMMON
 *  #define CONFIG_MISC_COMMON
 *  #define CONFIG_MISC_INIT_R
 *  #define CONFIG_CMD_BMP
 *  #define CONFIG_BMP_24BMP
 */
#ifdef CONFIG_CMD_BMP
int draw_logo(void)
{
	int ret;
	int x, y;
	struct udevice *dev;
	u32 panel_width = 0;
	u32 panel_height = 0;
	u32 logo_width, logo_height;
	ulong addr = (ulong)rockchip_logo;
	struct bmp_image *bmp = map_sysmem(addr, 0);

	/* Get panel size */
	ret = uclass_first_device_err(UCLASS_VIDEO, &dev);
	if (ret) {
		debug("%s@%d: Can not find valid video device.\n", __func__, __LINE__);
		return ret;
	}
	struct video_priv *uc_priv = dev_get_uclass_priv(dev);
	if (uc_priv) {
		panel_width = uc_priv->xsize;
		panel_height = uc_priv->ysize;
	}

	/* Get bmp file size */
	logo_width = bmp->header.width;
	logo_height = bmp->header.height;

	debug("%s@%d: Panel: %ux%u, Logo: %ux%u\n", __func__, __LINE__, panel_width
		  , panel_height, logo_width, logo_height);

	/* Calculate logo position */
	if (logo_width > 500 || logo_height > 100) {
		debug("%s@%d: The logo is bigger than 100x500\n", __func__, __LINE__);
		return -EINVAL;
	}
	x = (panel_width - logo_width) >> 1;
	y = (panel_height * 3) / 10 - (logo_height / 2);

	/* Draw the logo picture */
	ret = bmp_display(addr, x, y);
	if (ret) {
		return ret;
	}

	return 0;
}
#endif /* CONFIG_CMD_BMP */
