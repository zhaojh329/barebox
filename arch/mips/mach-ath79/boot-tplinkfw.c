/*
 * Copyright (C) 2018 Jianhui Zhao <jianhuizhao329@gmail.com>
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

#include <boot.h>
#include <bootm.h>
#include <common.h>
#include <libfile.h>
#include <malloc.h>
#include <init.h>
#include <fs.h>
#include <errno.h>
#include <binfmt.h>
#include <restart.h>
#include <asm/byteorder.h>

#include <boot-tplinkfw.h>

static int do_bootm_linux(struct image_data *data)
{
    int ret;
    void (*kernel)(void) = (void *)data->os_address;

    ret = bootm_load_os(data, data->os_address);
    if (ret)
        return ret;

    shutdown_barebox();

    kernel();
    return 0;
}

static void print_tpl_ih(u32 hw_id, u32 fw_length, u32 kernel_len,
    u32 rootfs_len, u32 kernel_la, u32 kernel_ep)
{
    printf("   Hardware ID:    %#X\n", hw_id);
    printf("   Total size:     %s\n", size_human_readable(fw_length));
    printf("   Kernel size:    %s\n", size_human_readable(kernel_len));
    printf("   Rootfs size:    %s\n", size_human_readable(rootfs_len));
    printf("   Load Address:   %#08X\n", kernel_la);
    printf("   Entry Point:    %#08X\n", kernel_ep);
    puts("\n");
}

/* Prints information about TP-Link firmware format from header v1 */
static inline void print_tpl_ih_v1(struct fw_header_v1 *hdr)
{
    printf("\n   Vender/version: %s-%s\n", hdr->vendor_name, hdr->fw_version);
    print_tpl_ih(hdr->hw_id, hdr->fw_length, hdr->kernel_len, hdr->rootfs_len,
        hdr->kernel_la, hdr->kernel_ep);
}

/* Prints information about TP-Link firmware format from header */
static inline void print_tpl_ih_v2(struct fw_header_v2 *hdr)
{
    printf("\n   version: %s\n", hdr->fw_version);
    print_tpl_ih(hdr->hw_id, hdr->fw_length, hdr->kernel_len, hdr->rootfs_len,
        hdr->kernel_la, hdr->kernel_ep);
}

static void tpl_to_uboot_header(image_header_t *hdr, const char *name,
    u32 kernel_ep, u32 kernel_len, u32 kernel_load)
{
    strncpy(hdr->ih_name, name, sizeof(hdr->ih_name));
    hdr->ih_hcrc = 0;
    hdr->ih_dcrc = 0;

    hdr->ih_ep   = be32_to_cpu(kernel_ep);
    hdr->ih_size = be32_to_cpu(kernel_len);
    hdr->ih_load = be32_to_cpu(kernel_load);

    hdr->ih_os   = IH_OS_LINUX;
    hdr->ih_arch = IH_ARCH_MIPS;
    hdr->ih_type = IH_TYPE_KERNEL;
    hdr->ih_comp = IH_COMP_LZMA;
}

/* Converts TP-Link header to stanard U-Boot image format header v1 */
static inline void tpl_to_uboot_header_v1(image_header_t *hdr,
    struct fw_header_v1 *tpl_hdr)
{
    tpl_to_uboot_header(hdr, tpl_hdr->vendor_name, tpl_hdr->kernel_ep,
        tpl_hdr->kernel_len, tpl_hdr->kernel_la);
}

static inline void tpl_to_uboot_header_v2(image_header_t *hdr,
    struct fw_header_v2 *tpl_hdr)
{
    tpl_to_uboot_header(hdr, "", tpl_hdr->kernel_ep,
        tpl_hdr->kernel_len, tpl_hdr->kernel_la);
}

static int do_bootm_tplinkfw(struct image_data *data)
{
    int fd, ret = -EINVAL;
    struct uimage_handle *handle = NULL;
    image_header_t *header = NULL;
    void *tpl_hdr = NULL;
    u32 h_ver;
    int tpl_hdr_size = MAX(sizeof(struct fw_header_v1),
        sizeof(struct fw_header_v1));

    fd = open(data->os_file, O_RDONLY);
    if (fd < 0) {
        printf("could not open: %s\n", errno_str());
        return fd;
    }

    tpl_hdr = xzalloc(tpl_hdr_size);
    if (read(fd, tpl_hdr, tpl_hdr_size) < 0) {
        printf("could not read: %s\n", errno_str());
        goto err_out;
    }

    handle = xzalloc(sizeof(struct uimage_handle));
    header = &handle->header;

    h_ver = be32_to_cpu(*((u32 *)tpl_hdr));
    switch (h_ver) {
    case HEADER_VERSION_V1:
        tpl_hdr_size = sizeof(struct fw_header_v1);
        print_tpl_ih_v1(tpl_hdr);
        tpl_to_uboot_header_v1(header, tpl_hdr);
        break;
    case HEADER_VERSION_V2:
        tpl_hdr_size = sizeof(struct fw_header_v2);
        print_tpl_ih_v2(tpl_hdr);
        tpl_to_uboot_header_v2(header, tpl_hdr);
        break;
    default:
        printf("Unsupported image header\n");
        goto err_out;
    }

    handle->fd = fd;
    handle->ihd[0].offset = 0;
    handle->ihd[0].len = header->ih_size;
    handle->nb_data_entries = 1;
    handle->data_offset = tpl_hdr_size;

    if (header->ih_arch != IH_ARCH) {
        printf("Unsupported Architecture 0x%x\n",
               data->os->header.ih_arch);
        goto err_out;
    }

    data->os_address = header->ih_load;
    data->os = handle;

    ret = do_bootm_linux(data);

err_out:
    close(fd);

    if (tpl_hdr)
        free(tpl_hdr);

    if (handle)
        free(handle);

    return ret;
}

static struct image_handler tplinkfw_handler = {
    .name = "MIPS Linux tplinkfw",
    .bootm = do_bootm_tplinkfw,
    .filetype = filetype_tplinkfw,
    .ih_os = IH_OS_LINUX
};

static struct binfmt_hook binfmt_tplinkfw_hook = {
    .type = filetype_tplinkfw,
    .exec = "bootm"
};

static int mips_register_image_handler(void)
{
    register_image_handler(&tplinkfw_handler);
    binfmt_register(&binfmt_tplinkfw_hook);

    return 0;
}
late_initcall(mips_register_image_handler);
