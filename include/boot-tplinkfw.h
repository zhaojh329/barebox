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

#ifndef __BOOT_TPLINKFW
#define __BOOT_TPLINKFW

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define MD5SUM_LEN 16

#define HEADER_VERSION_V1   0x01000000
#define HEADER_VERSION_V2   0x02000000

struct fw_header_v1 {
    uint32_t    version;    /* header version */
    char        vendor_name[24];
    char        fw_version[36];
    uint32_t    hw_id;      /* hardware id */
    uint32_t    hw_rev;     /* hardware revision */
    uint32_t    region_code;    /* region code */
    uint8_t     md5sum1[MD5SUM_LEN];
    uint32_t    unk2;
    uint8_t     md5sum2[MD5SUM_LEN];
    uint32_t    unk3;
    uint32_t    kernel_la;  /* kernel load address */
    uint32_t    kernel_ep;  /* kernel entry point */
    uint32_t    fw_length;  /* total length of the firmware */
    uint32_t    kernel_ofs; /* kernel data offset */
    uint32_t    kernel_len; /* kernel data length */
    uint32_t    rootfs_ofs; /* rootfs data offset */
    uint32_t    rootfs_len; /* rootfs data length */
    uint32_t    boot_ofs;   /* bootloader data offset */
    uint32_t    boot_len;   /* bootloader data length */
    uint16_t    ver_hi;
    uint16_t    ver_mid;
    uint16_t    ver_lo;
    uint8_t     pad[130];
    char        region_str1[32];
    char        region_str2[32];
    uint8_t     pad2[160];
} __attribute__ ((packed));

struct fw_header_v2 {
    uint32_t    version;            /* 0x00: header version */
    char        fw_version[48];     /* 0x04: fw version string */
    uint32_t    hw_id;              /* 0x34: hardware id */
    uint32_t    hw_rev;             /* 0x38: FIXME: hardware revision? */
    uint32_t    hw_ver_add;         /* 0x3c: additional hardware version */
    uint8_t     md5sum1[MD5SUM_LEN];        /* 0x40 */
    uint32_t    unk2;               /* 0x50: 0x00000000 */
    uint8_t     md5sum2[MD5SUM_LEN];        /* 0x54 */
    uint32_t    unk3;               /* 0x64: 0xffffffff */

    uint32_t    kernel_la;          /* 0x68: kernel load address */
    uint32_t    kernel_ep;          /* 0x6c: kernel entry point */
    uint32_t    fw_length;          /* 0x70: total length of the image */
    uint32_t    kernel_ofs;         /* 0x74: kernel data offset */
    uint32_t    kernel_len;         /* 0x78: kernel data length */
    uint32_t    rootfs_ofs;         /* 0x7c: rootfs data offset */
    uint32_t    rootfs_len;         /* 0x80: rootfs data length */
    uint32_t    boot_ofs;           /* 0x84: bootloader offset */
    uint32_t    boot_len;           /* 0x88: bootloader length */
    uint16_t    unk4;               /* 0x8c: 0x55aa */
    uint8_t     sver_hi;            /* 0x8e */
    uint8_t     sver_lo;            /* 0x8f */
    uint8_t     unk5;               /* 0x90: magic: 0xa5 */
    uint8_t     ver_hi;             /* 0x91 */
    uint8_t     ver_mid;            /* 0x92 */
    uint8_t     ver_lo;             /* 0x93 */
    uint8_t     pad[364];
} __attribute__ ((packed));

#endif