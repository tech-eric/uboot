/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the SPDX General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#define DEBUG
#include <common.h>
#include <clk.h>
#include <display.h>
#include <dm.h>
#include <fdtdec.h>
#include <panel.h>
#include <regmap.h>
#include <syscon.h>
#include <asm/gpio.h>
#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/arch/mipi_rk3399.h>
#include <asm/arch/clock.h>
#include <asm/arch/grf_rk3399.h>
#include <asm/arch/cru_rk3399.h>
#include <linux/kernel.h>
#include <dt-bindings/clock/rk3288-cru.h>
#include <dm/uclass-internal.h>

DECLARE_GLOBAL_DATA_PTR;

struct mipi_dsi {
	u32 ref_clk;
	u32 sys_clk;
	u32 pix_clk;
	u32 phy_clk;
	u32 txbyte_clk;
	u32 txesc_clk;
};

struct rk_mipi_priv {
	void __iomem *regs;
	struct rk3399_grf_regs *grf;
	struct udevice *panel;
	struct mipi_dsi *dsi;
};

static int rk_mipi_read_timing(struct udevice *dev,
			       struct display_timing *timing)
{
	if (fdtdec_decode_display_timing
			(gd->fdt_blob, dev_of_offset(dev), 0, timing)) {
		debug("%s: Failed to decode display timing\n", __func__);
		return -EINVAL;
	}

	return 0;
}
/*
 * register write function used only for mipi dsi controller.
 * parameter:
 *  reg: combination of regaddr(16bit)|bitswidth(8bit)|offset(8bit)
 *       you can use define in rk_mipi.h directly for this parameter
 *  val: value that will be write to specified bits of register
 */
static void rk_mipi_dsi_write(struct udevice *dev, u32 reg, u32 val)
{
	#define OFFSET		(reg & 0xff)
	#define BITS	    ((reg >> 8) & 0xff)
	#define ADDR		((reg >> 16) + priv->regs)

	u32 dat;
	u32 mask;
	struct rk_mipi_priv *priv = dev_get_priv(dev);

	/* Mask for specifiled bits,the corresponding bits will be clear */
	mask = (~((0xffffffff << OFFSET) &
		(0xffffffff >> (32 - OFFSET - BITS))));

	/* Make sure val in the available range */
	val &= (~(0xffffffff << BITS));

	/* Get register's original val */
	dat = readl(ADDR);

	/* Clear specified bits */
	dat &= mask;

	/* Fill specified bits */
	dat |= (val << OFFSET);

	writel(dat, ADDR);
}

static int rk_mipi_dsi_enable(struct udevice *dev,
			      const struct display_timing *timing)
{
	int node, timing_node;
	int val;
	struct rk_mipi_priv *priv = dev_get_priv(dev);
	struct display_plat *disp_uc_plat = dev_get_uclass_platdata(dev);
	u32 txbyte_clk = priv->dsi->txbyte_clk;
	u32 txesc_clk = priv->dsi->txesc_clk;

	txesc_clk = txbyte_clk/(txbyte_clk/txesc_clk + 1);

	/* Select the video source */
	switch (disp_uc_plat->source_id) {
	case VOP_B:
		rk_clrsetreg(&priv->grf->soc_con20, GRF_DSI0_VOP_SEL_MASK,
			     GRF_DSI0_VOP_SEL_B << GRF_DSI0_VOP_SEL_SHIFT);
		 break;
	case VOP_L:
		rk_clrsetreg(&priv->grf->soc_con20, GRF_DSI0_VOP_SEL_MASK,
			     GRF_DSI0_VOP_SEL_L << GRF_DSI0_VOP_SEL_SHIFT);
		 break;
	default:
		 return -EINVAL;
	}

	/* Set Controller as TX mode */
	val = GRF_DPHY_TX0_RXMODE_DIS << GRF_DPHY_TX0_RXMODE_SHIFT;
	rk_clrsetreg(&priv->grf->soc_con22, GRF_DPHY_TX0_RXMODE_MASK, val);

	/* Exit tx stop mode */
	val |= GRF_DPHY_TX0_TXSTOPMODE_DIS << GRF_DPHY_TX0_TXSTOPMODE_SHIFT;
	rk_clrsetreg(&priv->grf->soc_con22, GRF_DPHY_TX0_TXSTOPMODE_MASK, val);

	/* Disable turnequest */
	val |= GRF_DPHY_TX0_TURNREQUEST_DIS << GRF_DPHY_TX0_TURNREQUEST_SHIFT;
	rk_clrsetreg(&priv->grf->soc_con22, GRF_DPHY_TX0_TURNREQUEST_MASK, val);

	/* Set Display timing parameter */
	rk_mipi_dsi_write(dev, VID_HSA_TIME, timing->hsync_len.typ);
	rk_mipi_dsi_write(dev, VID_HBP_TIME, timing->hback_porch.typ);
	rk_mipi_dsi_write(dev, VID_HLINE_TIME,
			  (timing->hsync_len.typ +
			  timing->hback_porch.typ +
			  timing->hactive.typ +
			  timing->hfront_porch.typ));
	rk_mipi_dsi_write(dev, VID_VSA_LINES, timing->vsync_len.typ);
	rk_mipi_dsi_write(dev, VID_VBP_LINES, timing->vback_porch.typ);
	rk_mipi_dsi_write(dev, VID_VFP_LINES, timing->vfront_porch.typ);
	rk_mipi_dsi_write(dev, VID_ACTIVE_LINES, timing->vactive.typ);

	/* Set Signal Polarity */
	val = (timing->flags & DISPLAY_FLAGS_HSYNC_LOW) ? 1 : 0;
	rk_mipi_dsi_write(dev, HSYNC_ACTIVE_LOW, val);

	val = (timing->flags & DISPLAY_FLAGS_VSYNC_LOW) ? 1 : 0;
	rk_mipi_dsi_write(dev, VSYNC_ACTIVE_LOW, val);

	val = (timing->flags & DISPLAY_FLAGS_DE_LOW) ? 1 : 0;
	rk_mipi_dsi_write(dev, DISPLAY_FLAGS_DE_LOW, val);

	val = (timing->flags & DISPLAY_FLAGS_PIXDATA_NEGEDGE) ? 1 : 0;
	rk_mipi_dsi_write(dev, COLORM_ACTIVE_LOW, val);

	/* Set video mode */
	rk_mipi_dsi_write(dev, CMD_VIDEO_MODE, VIDEO_MODE);

	/* Set video mode transmission type as burst mode */
	rk_mipi_dsi_write(dev, VID_MODE_TYPE, BURST_MODE);

	/* Set pix num in a video package */
	rk_mipi_dsi_write(dev, VID_PKT_SIZE, 0x4b0);

	/* Set dpi color coding depth 24 bit */
	timing_node = fdt_subnode_offset(gd->fdt_blob,
			dev_of_offset(dev), "display-timings");
	node = fdt_first_subnode(gd->fdt_blob, timing_node);
	val = fdtdec_get_int(gd->fdt_blob, node, "bits-per-pixel", -1);
	switch (val) {
	case 16:
		rk_mipi_dsi_write(dev, DPI_COLOR_CODING, DPI_16BIT_CFG_1);
		break;
	case 24:
		rk_mipi_dsi_write(dev, DPI_COLOR_CODING, DPI_24BIT);
		break;
	case 30:
		rk_mipi_dsi_write(dev, DPI_COLOR_CODING, DPI_30BIT);
		break;
	default:
		rk_mipi_dsi_write(dev, DPI_COLOR_CODING, DPI_24BIT);
	}
	/* Enable low power mode */
	rk_mipi_dsi_write(dev, LP_CMD_EN, 1);
	rk_mipi_dsi_write(dev, LP_HFP_EN, 1);
	rk_mipi_dsi_write(dev, LP_VACT_EN, 1);
	rk_mipi_dsi_write(dev, LP_VFP_EN, 1);
	rk_mipi_dsi_write(dev, LP_VBP_EN, 1);
	rk_mipi_dsi_write(dev, LP_VSA_EN, 1);

	/* Division for timeout counter clk */
	rk_mipi_dsi_write(dev, TO_CLK_DIVISION, 0x0a);

	/* Tx esc clk division from txbyte clk */
	rk_mipi_dsi_write(dev, TX_ESC_CLK_DIVISION, txbyte_clk/txesc_clk);

	/*
	 * Timeout count for hs<->lp
	 * transation between Line period
	 */
	rk_mipi_dsi_write(dev, HSTX_TO_CNT, 0x3e8);

	/* Phy State transfer timing */
	rk_mipi_dsi_write(dev, PHY_STOP_WAIT_TIME, 32);
	rk_mipi_dsi_write(dev, PHY_TXREQUESTCLKHS, 1);
	rk_mipi_dsi_write(dev, PHY_HS2LP_TIME, 0x14);
	rk_mipi_dsi_write(dev, PHY_LP2HS_TIME, 0x10);
	rk_mipi_dsi_write(dev, MAX_RD_TIME, 0x2710);

	/* Power on */
	rk_mipi_dsi_write(dev, SHUTDOWNZ, 1);

	return 0;
}

/*
 * rk mipi dphy write function
 */
static void rk_mipi_phy_write(struct udevice *dev, unsigned char test_code,
			      unsigned char *test_data, unsigned char size)
{
	int i = 0;
	/* Write Test code */
	rk_mipi_dsi_write(dev, PHY_TESTCLK, 1);
	rk_mipi_dsi_write(dev, PHY_TESTDIN, test_code);
	rk_mipi_dsi_write(dev, PHY_TESTEN, 1);
	rk_mipi_dsi_write(dev, PHY_TESTCLK, 0);
	rk_mipi_dsi_write(dev, PHY_TESTEN, 0);

	/* Write Test data */
	for (i = 0; i < size; i++) {
		rk_mipi_dsi_write(dev, PHY_TESTCLK, 0);
		rk_mipi_dsi_write(dev, PHY_TESTDIN, test_data[i]);
		rk_mipi_dsi_write(dev, PHY_TESTCLK, 1);
	}
}

/*
 * mipi dphy config function. calculate the suitable prediv,
 * feedback div,fsfreqrang value ,cap ,lpf and so on
 * according to the given pix clk ratthe.and then enable phy
 */
static int rk_mipi_phy_enable(struct udevice *dev)
{
	int i;
	struct rk_mipi_priv *priv = dev_get_priv(dev);
	u64	fbdiv;
	u64 prediv = 1;
	u64 ddr_clk = priv->dsi->phy_clk;
	u32 refclk = priv->dsi->ref_clk;
	u32 remain = refclk;
	unsigned char test_data[2] = {0};

	/*
	 * dphy fsfreqrang
	 * different dphy config is needed for diffenect freq rang
	 * here list the config-freq relation
	 */
	int freq_rang[][2] = {
		{90, 0x01},   {100, 0x10},  {110, 0x20},  {130, 0x01},
		{140, 0x11},  {150, 0x21},  {170, 0x02},  {180, 0x12},
		{200, 0x22},  {220, 0x03},  {240, 0x13},  {250, 0x23},
		{270, 0x04},  {300, 0x14},  {330, 0x05},  {360, 0x15},
		{400, 0x25},  {450, 0x06},  {500, 0x16},  {550, 0x07},
		{600, 0x17},  {650, 0x08},  {700, 0x18},  {750, 0x09},
		{800, 0x19},  {850, 0x29},  {900, 0x39},  {950, 0x0a},
		{1000, 0x1a}, {1050, 0x2a}, {1100, 0x3a}, {1150, 0x0b},
		{1200, 0x1b}, {1250, 0x2b}, {1300, 0x3b}, {1350, 0x0c},
		{1400, 0x1c}, {1450, 0x2c}, {1500, 0x3c}
	};

	/* Shutdown mode */
	rk_mipi_dsi_write(dev, PHY_SHUTDOWNZ, 0);
	rk_mipi_dsi_write(dev, PHY_RSTZ, 0);
	rk_mipi_dsi_write(dev, PHY_TESTCLR, 1);

	/* Pll locking */
	rk_mipi_dsi_write(dev, PHY_TESTCLR, 0);

	/* config cp and lfp */
	test_data[0] = 0x80 | (ddr_clk / (200*MHZ)) << 3 | 0x3;
	rk_mipi_phy_write(dev, 0x10, test_data, 1);

	test_data[0] = 0x8;
	rk_mipi_phy_write(dev, 0x11, test_data, 1);

	test_data[0] = 0x80 | 0x40;
	rk_mipi_phy_write(dev, 0x12, test_data, 1);

	/* select the suitable value for fsfreqrang reg */
	for (i = 0; i < ARRAY_SIZE(freq_rang); i++) {
		if (ddr_clk / (MHZ) >= freq_rang[i][0])
			break;
	}
	test_data[0] = freq_rang[i][1] << 1;
	rk_mipi_phy_write(dev, CODE_HS_RX_LANE0, test_data, 1);

	/*
	 * Calculate the best ddrclk and it's
	 * corresponding div value, If the given
	 * pixelclock is great than 250M, the ddr
	 * clk will be fix 1500M.otherwise , it's
	 * equal to ddr_clk= pixclk*6.
	 * 40MHZ>=refclk/prediv>=5MHZ according to spec
	 */
	#define MAX_FBDIV 512
	#define MAX_PREDIV (refclk/(5*MHZ))
	#define MIN_PREDIV ((refclk/(40*MHZ)) ? (refclk/(40*MHZ) + 1) : 1)

	debug("DEBUG: MAX_PREDIV=%u, MIN_PREDIV=%u\n", MAX_PREDIV, MIN_PREDIV);

	if (MAX_PREDIV < MIN_PREDIV) {
		debug("Err: Invalid refclk value@%s\n", __func__);
		return -EINVAL;
	}

	for (i = MIN_PREDIV; i < MAX_PREDIV; i++) {
		if ((ddr_clk * i % refclk < remain) &&
		    (ddr_clk * i / refclk) < MAX_FBDIV) {
			prediv = i;
			remain = ddr_clk * i % refclk;
		}
	}
	fbdiv	= ddr_clk * prediv / refclk;
	ddr_clk = refclk * fbdiv / prediv;
	priv->dsi->phy_clk = ddr_clk;

	debug("DEBUG:refclk=%u, refclk=%llu, fbdiv=%llu, phyclk=%llu\n",
	      refclk, prediv, fbdiv, ddr_clk);

	/* config prediv and feedback reg */
	test_data[0] = prediv - 1;
	rk_mipi_phy_write(dev, CODE_PLL_INPUT_DIV_RAT, test_data, 1);
	test_data[0] = (fbdiv - 1) & 0x1f;
	rk_mipi_phy_write(dev, CODE_PLL_LOOP_DIV_RAT, test_data, 1);
	test_data[0] = (fbdiv - 1) >> 5 | 0x80;
	rk_mipi_phy_write(dev, CODE_PLL_LOOP_DIV_RAT, test_data, 1);
	test_data[0] = 0x30;
	rk_mipi_phy_write(dev, CODE_PLL_INPUT_LOOP_DIV_RAT, test_data, 1);

	/* rest config */
	test_data[0] = 0x4d;
	rk_mipi_phy_write(dev, 0x20, test_data, 1);

	test_data[0] = 0x3d;
	rk_mipi_phy_write(dev, 0x21, test_data, 1);

	test_data[0] = 0xdf;
	rk_mipi_phy_write(dev, 0x21, test_data, 1);

	test_data[0] =  0x7;
	rk_mipi_phy_write(dev, 0x22, test_data, 1);

	test_data[0] = 0x80 | 0x7;
	rk_mipi_phy_write(dev, 0x22, test_data, 1);

	test_data[0] = 0x80 | 15;
	rk_mipi_phy_write(dev, CODE_HSTXDATALANEREQUSETSTATETIME,
			  test_data, 1);
	test_data[0] = 0x80 | 85;
	rk_mipi_phy_write(dev, CODE_HSTXDATALANEPREPARESTATETIME,
			  test_data, 1);
	test_data[0] = 0x40 | 10;
	rk_mipi_phy_write(dev, CODE_HSTXDATALANEHSZEROSTATETIME,
			  test_data, 1);

	/* enter into stop mode */
	rk_mipi_dsi_write(dev, N_LANES, 0x03);
	rk_mipi_dsi_write(dev, PHY_ENABLECLK, 1);
	rk_mipi_dsi_write(dev, PHY_FORCEPLL, 1);
	rk_mipi_dsi_write(dev, PHY_SHUTDOWNZ, 1);
	rk_mipi_dsi_write(dev, PHY_RSTZ, 1);

	return 0;
}

static int rk_mipi_enable(struct udevice *dev, int panel_bpp,
			  const struct display_timing *timing)
{
	return 0;
}

static int rk_mipi_ofdata_to_platdata(struct udevice *dev)
{
	struct rk_mipi_priv *priv = dev_get_priv(dev);

	priv->grf = syscon_get_first_range(ROCKCHIP_SYSCON_GRF);
	priv->regs = (void *)dev_get_addr(dev);

	return 0;
}

/*
 * probe function: check panel existence and reading
 * it's timing. then config mipi dsi controller and
 * enable it according to the timing parameter
 */
static int rk_mipi_probe(struct udevice *dev)
{
	struct rk_mipi_priv *priv = dev_get_priv(dev);
	struct display_timing timing;
	int ret;

	ret = uclass_get_device_by_phandle(UCLASS_PANEL, dev, "rockchip,panel",
					   &priv->panel);
	if (ret) {
		debug("Err:Can't find panel@%s, ret = %d\n", __func__, ret);
		return -ENODEV;
	}
	/* Read panel timing,and save to struct timing */
	rk_mipi_read_timing(dev, &timing);

	/* fill the mipi controller parameter */
	priv->dsi->ref_clk = 24*MHZ;
	priv->dsi->sys_clk = priv->dsi->ref_clk;
	priv->dsi->pix_clk = timing.pixelclock.typ;
	priv->dsi->phy_clk = priv->dsi->pix_clk * 6;
	priv->dsi->txbyte_clk = priv->dsi->phy_clk / 8;
	priv->dsi->txesc_clk = 20*MHZ;

	/* config mipi dsi according to timing and enable it */
	ret = rk_mipi_dsi_enable(dev, &timing);
	if (ret) {
		debug("Err: mipi dsi enable fail@%s,ret=%d\n", __func__, ret);
		return ret;
	}

	/* init mipi dsi phy */
	ret = rk_mipi_phy_enable(dev);
	if (ret) {
		debug("Err: mipi phy enable fail@%s,ret=%d\n", __func__, ret);
		return ret;
	}

	/* enable backlight */
	ret = panel_enable_backlight(priv->panel);
	if (ret) {
		debug("Err: fail to enable bg@%s,ret=%d\n", __func__, ret);
		return ret;
	}

	return 0;
}

static const struct dm_display_ops rk_mipi_dsi_ops = {
	.read_timing = rk_mipi_read_timing,
	.enable = rk_mipi_enable,
};

static const struct udevice_id rk_mipi_dsi_ids[] = {
	{ .compatible = "rockchip,rk3399_mipi_dsi" },
	{ }
};

U_BOOT_DRIVER(rk_mipi_dsi) = {
	.name	= "rk_mipi_dsi",
	.id	= UCLASS_DISPLAY,
	.of_match = rk_mipi_dsi_ids,
	.ofdata_to_platdata = rk_mipi_ofdata_to_platdata,
	.probe	= rk_mipi_probe,
	.ops	= &rk_mipi_dsi_ops,
	.priv_auto_alloc_size   = sizeof(struct rk_mipi_priv),
};
