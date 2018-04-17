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
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation.
 *
 */

#include <common.h>
#include <malloc.h>
#include <bbu.h>
#include <fs.h>
#include <fcntl.h>

static int nand_update(struct bbu_data *data)
{
    int fd, ret;

    ret = bbu_confirm(data);
    if (ret)
        return ret;

    fd = open(data->devicefile, O_WRONLY);
    if (fd < 0)
        return fd;

    debug("%s: eraseing %s from 0 to 0x%08x\n", __func__,
            data->devicefile, data->len);
    ret = erase(fd, data->len, 0);
    if (ret) {
        printf("erasing %s failed with %s\n", data->devicefile,
                strerror(-ret));
        goto err_close;
    }

    ret = write(fd, data->image, data->len);
    if (ret < 0) {
        printf("writing update to %s failed with %s\n", data->devicefile,
                strerror(-ret));
        goto err_close;
    }

    ret = 0;

err_close:
    close(fd);

    return ret;
}

static int nand_update_barebox(struct bbu_handler *handler, struct bbu_data *data)
{
    if (file_detect_type(data->image + 16, data->len - 16) != filetype_arm_barebox &&
            !bbu_force(data, "Not an ARM barebox image"))
        return -EINVAL;

    return nand_update(data);
}

static int nand_update_kernel(struct bbu_handler *handler, struct bbu_data *data)
{
    if (file_detect_type(data->image, data->len) != filetype_uimage &&
            !bbu_force(data, "Not an U-Boot uImage"))
        return -EINVAL;

    return nand_update(data);
}

/*
 * Register a s5pcxx update handler for NAND
 */
int s5pcxx_bbu_nand_register_handler(void)
{
    struct bbu_handler *handler;
    int ret;

    handler = xzalloc(sizeof(*handler));
    handler->devicefile = "/dev/self0";
    handler->name = "barebox";
    handler->handler = nand_update_barebox;
    handler->flags = BBU_HANDLER_FLAG_DEFAULT;

    ret = bbu_register_handler(handler);
    if (ret)
        free(handler);

    handler = xzalloc(sizeof(*handler));
    handler->devicefile = "/dev/nand0.kernel";
    handler->name = "kernel";
    handler->handler = nand_update_kernel;

    ret = bbu_register_handler(handler);
    if (ret)
        free(handler);

    return ret;
}
