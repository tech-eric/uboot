/*
 * Copyright 2017 Rockchip Inc.
 * Author: Eric Gao <eric.gao@rock-chips.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <common.h>
#include <rockchip/misc.h>

#ifdef CONFIG_MISC_INIT_R
int misc_init_r(void)
{
	#ifdef CONFIG_CMD_BMP
	draw_logo();
	#endif

	return 0;
}
#endif
