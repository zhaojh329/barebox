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

static int mips_register_image_handler(void)
{
	register_image_handler(&barebox_handler);
	binfmt_register(&binfmt_barebox_hook);

	register_image_handler(&uimage_handler);
	binfmt_register(&binfmt_uimage_hook);

	return 0;
}
late_initcall(mips_register_image_handler);
