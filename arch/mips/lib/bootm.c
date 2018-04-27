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

static int do_bootm_barebox(struct image_data *data)
{
	void (*barebox)(void);

	barebox = read_file(data->os_file, NULL);
	if (!barebox)
		return -EINVAL;

	if (data->dryrun) {
		free(barebox);
		return 0;
	}

	shutdown_barebox();

	barebox();

	restart_machine();
}

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

#define TPL_IH_VERSION_V1	0x01000000
#define TPL_IH_VERSION_V2	0x02000000
#define TPL_IH_VERSION_V3	0x03000000

/* For v1 version of header version */
typedef struct tplink_image_header {		/* ofs (size): what                     */
	uint32_t ih_version;					/* 0x0    (4): header version           */
	char     ih_vendor[24];					/* 0x4   (24): vendor name		       */
	char     ih_fw_ver[36];					/* 0x1C  (36): firmware version  		*/
	uint32_t ih_hw_id;						/* 0x40   (4): hardware ID              */
	uint32_t ih_hw_rev;						/* 0x44   (4): hardware revision        */
	uint32_t ih_region;						/* 0x48   (4): region code              */
	uint8_t  ih_md5sum1[16];				/* 0x4C  (16): MD5 sum #1               */
	uint32_t ih_ignore1;					/* 0x5C   (4): unknown                  */
	uint8_t  ih_md5sum2[16];				/* 0x60  (16): MD5 sum #2               */
	uint32_t ih_ignore2;					/* 0x70   (4): unknown                  */
	uint32_t ih_kernel_load;				/* 0x74   (4): kernel load address      */
	uint32_t ih_kernel_ep;					/* 0x78   (4): kernel entry point       */
	uint32_t ih_fw_len;						/* 0x7C   (4): image size (with header) */
	uint32_t ih_kernel_ofs;					/* 0x80   (4): kernel data offset       */
	uint32_t ih_kernel_len;					/* 0x84   (4): kernel data size         */
	uint32_t ih_rootfs_ofs;					/* 0x88   (4): rootfs data offset       */
	uint32_t ih_rootfs_len;					/* 0x8C   (4): rootfs data size         */
	uint32_t ih_boot_ofs;					/* 0x90   (4): bootloader data offset   */
	uint32_t ih_boot_len;					/* 0x94   (4): bootloader data size     */
	uint16_t ih_fw_ver_hi;					/* 0x98   (2): firmware version hi      */
	uint16_t ih_fw_ver_mid;					/* 0x9A   (2): firmware version mid     */
	uint16_t ih_fw_ver_lo;					/* 0x9C   (2): firmware version lo      */
	uint8_t  ih_pad[354];					/* 0x9E (354): padding, not used...     */
} tplink_image_header_t;

/* Prints information about TP-Link firmware format from header */
static void print_tpl_ih_v1(tplink_image_header_t *hdr)
{
	printf("\n");

	printf("   Vender/version: %s-%s\n", hdr->ih_vendor, hdr->ih_fw_ver);
	printf("   Hardware ID:    %#X\n", hdr->ih_hw_id);
	printf("   Total size:     %s\n", size_human_readable(hdr->ih_fw_len));
	printf("   Kernel size:    %s\n", size_human_readable(hdr->ih_kernel_len));
	printf("   Rootfs size:    %s\n", size_human_readable(hdr->ih_rootfs_len));
	printf("   Load Address:   %#08X\n", hdr->ih_kernel_load);
	printf("   Entry Point:    %#08X\n", hdr->ih_kernel_ep);

	puts("\n");
}

/* Converts TP-Link header to stanard U-Boot image format header */
static void tpl_to_uboot_header(image_header_t *hdr, tplink_image_header_t *tpl_hdr)
{
	strncpy(hdr->ih_name, tpl_hdr->ih_vendor, sizeof(hdr->ih_name));
	hdr->ih_hcrc = 0;
	hdr->ih_dcrc = 0;

	hdr->ih_ep   = tpl_hdr->ih_kernel_ep;
	hdr->ih_size = tpl_hdr->ih_kernel_len;
	hdr->ih_load = tpl_hdr->ih_kernel_load;

	hdr->ih_os   = IH_OS_LINUX;
	hdr->ih_arch = IH_ARCH_MIPS;
	hdr->ih_type = IH_TYPE_KERNEL;
	hdr->ih_comp = IH_COMP_LZMA;
}

static int do_bootm_tplinkfw(struct image_data *data)
{
	int fd, ret = -EINVAL;
	struct uimage_handle *handle = NULL;
	image_header_t *header = NULL;
	tplink_image_header_t *tpl_hdr = NULL;

	fd = open(data->os_file, O_RDONLY);
	if (fd < 0) {
		printf("could not open: %s\n", errno_str());
		return fd;
	}

	tpl_hdr = xzalloc(sizeof(tplink_image_header_t));

	if (read(fd, tpl_hdr, sizeof(*tpl_hdr)) < 0) {
		printf("could not read: %s\n", errno_str());
		goto err_out;
	}

	switch (uimage_to_cpu(tpl_hdr->ih_version))
	{
	case TPL_IH_VERSION_V1:
		print_tpl_ih_v1(tpl_hdr);
		break;

	case TPL_IH_VERSION_V2:
	case TPL_IH_VERSION_V3:
	default:
		printf("Unsupported image header\n");
		goto err_out;
	}

	/* convert header to cpu native endianess */
	tpl_hdr->ih_version = uimage_to_cpu(tpl_hdr->ih_version);
	tpl_hdr->ih_hw_id= uimage_to_cpu(tpl_hdr->ih_hw_id);
	tpl_hdr->ih_hw_rev = uimage_to_cpu(tpl_hdr->ih_hw_rev);
	tpl_hdr->ih_kernel_load = uimage_to_cpu(tpl_hdr->ih_kernel_load);
	tpl_hdr->ih_kernel_ep = uimage_to_cpu(tpl_hdr->ih_kernel_ep);
	tpl_hdr->ih_fw_len = uimage_to_cpu(tpl_hdr->ih_fw_len);
	tpl_hdr->ih_kernel_len = uimage_to_cpu(tpl_hdr->ih_kernel_len);
	tpl_hdr->ih_rootfs_len = uimage_to_cpu(tpl_hdr->ih_rootfs_len);

	handle = xzalloc(sizeof(struct uimage_handle));
	header = &handle->header;

	/* Convert to general format */
	tpl_to_uboot_header(header, tpl_hdr);

	handle->ihd[0].offset = 0;
	handle->ihd[0].len = header->ih_size;
	handle->nb_data_entries = 1;
	handle->data_offset = sizeof(tplink_image_header_t);

	/* fd is now at the first data word */
	handle->fd = fd;

	uimage_print_contents(handle);

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

static struct image_handler barebox_handler = {
	.name = "MIPS barebox",
	.bootm = do_bootm_barebox,
	.filetype = filetype_mips_barebox,
};

static struct binfmt_hook binfmt_barebox_hook = {
	.type = filetype_mips_barebox,
	.exec = "bootm",
};

static struct image_handler uimage_handler = {
	.name = "MIPS Linux uImage",
	.bootm = do_bootm_linux,
	.filetype = filetype_uimage,
	.ih_os = IH_OS_LINUX,
};

static struct binfmt_hook binfmt_uimage_hook = {
	.type = filetype_uimage,
	.exec = "bootm",
};

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
	register_image_handler(&barebox_handler);
	binfmt_register(&binfmt_barebox_hook);

	register_image_handler(&uimage_handler);
	binfmt_register(&binfmt_uimage_hook);

	register_image_handler(&tplinkfw_handler);
	binfmt_register(&binfmt_tplinkfw_hook);

	return 0;
}
late_initcall(mips_register_image_handler);
