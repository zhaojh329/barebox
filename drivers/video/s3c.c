/*
 * Copyright (C) 2017 Jianhui Zhao
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details. *
 */

#include <common.h>
#include <init.h>
#include <fb.h>
#include <driver.h>
#include <malloc.h>
#include <errno.h>
#include <io.h>
#include <mach/s3c-fb.h>

#define VIDCON0			0x00
#define VIDCON1			0x04
#define VIDTCON0		0x10
#define VIDTCON1		0x14
#define VIDTCON2		0x18
#define VIDTCON3		0x1C
#define WINCON0			0x20
#define SHODOWCON		0x34
#define VIDOSD0A		0x40
#define VIDOSD0B		0x44
#define VIDOSD0C		0x48
#define VIDW00ADD0B0	0xA0
#define VIDW00ADD1B0	0xD0

struct s3cfb_info {
	void __iomem *base;
	unsigned memory_size;
	struct fb_info info;
	struct device_d *hw_dev;
	int passive_display;
	void (*enable)(int enable);
};

/**
 * @param fb_info Framebuffer information
 */
static void s3cfb_enable_controller(struct fb_info *fb_info)
{
	struct s3cfb_info *fbi = fb_info->priv;

	writel(readl(fbi->base + VIDCON0) | 0x3, fbi->base + VIDCON0);
	writel(readl(fbi->base + WINCON0) | 0x1, fbi->base + WINCON0);

	if (fbi->enable)
		fbi->enable(1);
}

/**
 * @param fb_info Framebuffer information
 */
static void s3cfb_disable_controller(struct fb_info *fb_info)
{
	struct s3cfb_info *fbi = fb_info->priv;
	u32 value;
	
	if (fbi->enable)
		fbi->enable(0);

	writel(readl(fbi->base + WINCON0) & ~0x1, fbi->base + WINCON0);
		
	/* see the note in the framebuffer datasheet about why you
	** cannot take both of these bits down at the same time. */
	value = readl(fbi->base + VIDCON0);
	if (value & (1 << 1))
		writel(value & 0x01, fbi->base + VIDCON0);
}

/**
 * Prepare the video hardware for a specified video mode
 * @param fb_info Framebuffer information
 * @return 0 on success
 */
static int s3cfb_activate_var(struct fb_info *fb_info)
{
	struct s3cfb_info *fbi = fb_info->priv;
	struct fb_videomode *mode = fb_info->mode;
	unsigned size, div;

	if (fbi->passive_display != 0) {
		dev_err(fbi->hw_dev, "Passive displays are currently not supported\n");
		return -EINVAL;
	}

	/*
	 * we need at least this amount of memory for the framebuffer
	 */
	size = mode->xres * mode->yres * (fb_info->bits_per_pixel >> 3);
	if (fbi->memory_size != size || fb_info->screen_base == NULL) {
		if (fb_info->screen_base)
			free(fb_info->screen_base);
		fbi->memory_size = 0;
		fb_info->screen_base = malloc(size);
		if (! fb_info->screen_base)
			return -ENOMEM;
		memset(fb_info->screen_base, 0, size);
		fbi->memory_size = size;
	}

	switch (fb_info->bits_per_pixel) {
	case 32:
		fb_info->red.offset = 16;
		fb_info->red.length = 8;
		fb_info->green.offset = 8;
		fb_info->green.length = 8;
		fb_info->blue.offset = 0;
		fb_info->blue.length = 8;
		break;
	default:
		printf("Invalid bits per pixel value\n");
		dev_err(fbi->hw_dev, "Invalid bits per pixel value: %u\n", fb_info->bits_per_pixel);
		return -EINVAL;
	}

	/*
	 * bit[2] = 0		Selects HCLK(HCLK_DSYS = 166750 KHz) as the video clock source.
	 * bit[4] = 1		Divided by CLKVAL_F
	 * bit[13:6]		CLKVAL = HCLK / VCLK - 1
	 */
	div = 166750 / PICOS2KHZ(mode->pixclock) - 1;
	writel((0 << 2) | (1 << 4) | (div << 6), fbi->base + VIDCON0);

	/* According to the LCD manual specifies the HSYNC and VCLK pulse polarity. */
	writel((1 << 6) | (1 << 5), fbi->base + VIDCON1);

	/* timings */
	writel((mode->upper_margin << 16) | (mode->lower_margin << 8) | (mode->vsync_len << 0), 
			fbi->base + VIDTCON0);
	writel((mode->left_margin << 16) | (mode->right_margin << 8) | (mode->hsync_len << 0),
			fbi->base + VIDTCON1);

	/* Set vertical size and horizontal size of display */
	writel(((mode->yres - 1) << 11) | ((mode->xres - 1) << 0), fbi->base + VIDTCON2);

	/* Enables VSYNC Signal Output */
	writel(0x1 << 31, fbi->base + VIDTCON3);

    /*
     * bit[15] = 1		Specifies the Word swap control bit
     * bit[5:2] = 0xB	Unpacked 24 bpp ( non-palletized R:8-G:8-B:8 )
     */
	writel((0xB<<2) | (1<<15), fbi->base + WINCON0);

	/* Sets the upper left coordinates of the screen */
	writel((0<<11) | (0 << 0), fbi->base + VIDOSD0A);

	/* Set the right lower coordinates of the screen */
	writel(((mode->xres - 1) <<11) | ((mode->yres - 1) << 0), fbi->base + VIDOSD0B);

	/* Specifies the Window Size(Number of Word) */
	writel(fbi->memory_size / 4, fbi->base + VIDOSD0C);

	/* the start address for Video frame buffer */
	writel((u32)fb_info->screen_base, fbi->base + VIDW00ADD0B0);

	/* the end address for Video frame buffer */
	writel((u32)fb_info->screen_base + fbi->memory_size, fbi->base + VIDW00ADD1B0);

	/* Enables Channel 0 */
	writel(0x01, fbi->base + SHODOWCON);
	
	return 0;
}

/*
 * There is only one video hardware instance available.
 * It makes no sense to dynamically allocate this data
 */
static struct fb_ops s3cfb_ops = {
	.fb_activate_var = s3cfb_activate_var,
	.fb_enable = s3cfb_enable_controller,
	.fb_disable = s3cfb_disable_controller,
};

static struct s3cfb_info fbi = {
	.info = {
		.fbops = &s3cfb_ops,
	},
};

static int s3cfb_probe(struct device_d *hw_dev)
{
	struct resource *iores;
	struct s3c_fb_platform_data *pdata = hw_dev->platform_data;
	int ret;

	if (! pdata)
		return -ENODEV;

	iores = dev_request_mem_resource(hw_dev, 0);
	if (IS_ERR(iores))
		return PTR_ERR(iores);
	fbi.base = IOMEM(iores->start);

	/* just init */
	fbi.info.priv = &fbi;

	/* add runtime hardware info */
	fbi.hw_dev = hw_dev;
	hw_dev->priv = &fbi;

	/* add runtime video info */
	fbi.info.modes.modes = pdata->mode_list;
	fbi.info.modes.num_modes = pdata->mode_cnt;
	fbi.info.mode = fbi.info.modes.modes;
	fbi.info.xres = fbi.info.mode->xres;
	fbi.info.yres = fbi.info.mode->yres;
	fbi.info.bits_per_pixel = pdata->bits_per_pixel;

	fbi.passive_display = pdata->passive_display;
	fbi.enable = pdata->enable;

	/* Display path selection:RGB=FIMD I80=FIMD ITU=FIMD */
	writel(0x2, 0xE0107008);
	
	ret = register_framebuffer(&fbi.info);
	if (ret != 0) {
		dev_err(hw_dev, "Failed to register framebuffer\n");
		return -EINVAL;
	}

	return 0;
}

static struct driver_d s3cfb_driver = {
	.name	= "s3c_fb",
	.probe	= s3cfb_probe,
};
device_platform_driver(s3cfb_driver);

