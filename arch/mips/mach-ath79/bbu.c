/*
 * Copyright (c) 2017 Oleksij Rempel <linux@rempel-privat.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <common.h>
#include <bbu.h>
#include <init.h>
#include <fs.h>
#include <fcntl.h>
#include <ioctl.h>
#include <progress.h>
#include <linux/mtd/mtd-abi.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

static int ath79_update_firmware(struct bbu_handler *handler, struct bbu_data *data)
{
	int fd, ret;
	struct stat s;
	size_t size;
	int erase_off;
	enum filetype filetype;
	u32 oflags = O_WRONLY;
	struct mtd_info_user mtdinfo;
	u32 erasesize;

	filetype = file_detect_type(data->image, data->len);
	if ((filetype != filetype_uimage) &&
		(filetype != filetype_tplinkfw)) {
		if (!bbu_force(data, "incorrect image type."))
			return -EINVAL;
	}

	ret = stat(data->devicefile, &s);
	if (ret) {
		oflags |= O_CREAT;
	} else {
		if (!S_ISREG(s.st_mode) && s.st_size < data->len) {
			printf("Image (%zd) is too big for device (%lld)\n",
					data->len, s.st_size);
		}
	}

	ret = bbu_confirm(data);
	if (ret)
		return ret;

	fd = open(data->devicefile, oflags);
	if (fd < 0)
		return fd;

	ret = protect(fd, data->len, 0, 0);
	if (ret && ret != -ENOSYS) {
		printf("unprotecting %s failed with %s\n", data->devicefile,
				strerror(-ret));
		goto err_close;
	}

	ioctl(fd, MEMGETINFO, &mtdinfo);

	erase_off = 0;
	erasesize = mtdinfo.erasesize;

	printf("Erasing...\n");
	init_progression_bar(data->len);

	while (erase_off + erasesize < data->len) {
		ret = erase(fd, erasesize, erase_off);
		if (ret && ret != -ENOSYS) {
			printf("erasing %s failed with %s\n", data->devicefile,
					strerror(-ret));
			goto err_close;
		}

		erase_off += erasesize;

		show_progress(erase_off);
	}

	printf("\nWriting...\n");
	init_progression_bar(data->len);

	size = data->len;

	while (size) {
		ret = write(fd, data->image, MIN(1024, size));
		if (ret < 0)
			goto err_close;
		else if (ret == 0)
			break;

		size -= ret;
		data->image += ret;

		show_progress(data->len - size);
	}

	printf("\n");

	protect(fd, data->len, 0, 1);

	ret = 0;

err_close:
	close(fd);

	return ret;
}

static int ath79_init_bbu(void)
{
	struct bbu_handler *handler;

	bbu_register_std_file_update("barebox", BBU_HANDLER_FLAG_DEFAULT,
					"/dev/spiflash.barebox",
					filetype_mips_barebox);

	handler = xzalloc(sizeof(*handler));
	handler->name = "firmware";
	handler->devicefile = "/dev/spiflash.firmware";
	handler->handler = ath79_update_firmware;

	bbu_register_handler(handler);

	return 0;
}
postcore_initcall(ath79_init_bbu);


