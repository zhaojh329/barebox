
#define HEADER_LEN 0x1000	/* length of the blank area + IVT + DCD */

/*
 * ============================================================================
 * i.MX flash header v1 handling. Found on i.MX35 and i.MX51
 * ============================================================================
 */
#define DCD_BARKER       0xb17219e9

struct imx_flash_header {
	uint32_t app_code_jump_vector;
	uint32_t app_code_barker;
	uint32_t app_code_csf;
	uint32_t dcd_ptr_ptr;
	uint32_t super_root_key;
	uint32_t dcd;
	uint32_t app_dest;
	uint32_t dcd_barker;
	uint32_t dcd_block_len;
} __attribute__((packed));

struct imx_boot_data {
	uint32_t start;
	uint32_t size;
	uint32_t plugin;
} __attribute__((packed));

struct imx_dcd_rec_v1 {
	uint32_t type;
	uint32_t addr;
	uint32_t val;
} __attribute__((packed));

#define TAG_IVT_HEADER	0xd1
#define IVT_VERSION	0x40
#define TAG_DCD_HEADER	0xd2
#define DCD_VERSION	0x40
#define TAG_UNLOCK	0xb2
#define TAG_NOP		0xc0
#define TAG_WRITE	0xcc
#define TAG_CHECK	0xcf
#define PARAMETER_FLAG_MASK	(1 << 3)
#define PARAMETER_FLAG_SET	(1 << 4)

struct imx_ivt_header {
	uint8_t tag;
	uint16_t length;
	uint8_t version;
} __attribute__((packed));

struct imx_flash_header_v2 {
	struct imx_ivt_header header;

	uint32_t entry;
	uint32_t reserved1;
	uint32_t dcd_ptr;
	uint32_t boot_data_ptr;
	uint32_t self;
	uint32_t csf;
	uint32_t reserved2;

	struct imx_boot_data boot_data;
	struct imx_ivt_header dcd_header;
} __attribute__((packed));

struct config_data {
	uint32_t image_load_addr;
	uint32_t image_dcd_offset;
	uint32_t image_size;
	uint32_t load_size;
	char *outfile;
	char *srkfile;
	int header_version;
	int cpu_type;
	int (*check)(const struct config_data *data, uint32_t cmd,
		     uint32_t addr, uint32_t mask);
	int (*write_mem)(const struct config_data *data, uint32_t addr,
			 uint32_t val, int width, int set_bits, int clear_bits);
	int (*nop)(const struct config_data *data);
	int csf_space;
	char *csf;
};

#define MAX_RECORDS_DCD_V2 1024
struct imx_dcd_v2_write_rec {
	uint32_t addr;
	uint32_t val;
} __attribute__((packed));

struct imx_dcd_v2_write {
	uint8_t tag;
	uint16_t length;
	uint8_t param;
	struct imx_dcd_v2_write_rec data[MAX_RECORDS_DCD_V2];
} __attribute__((packed));

struct imx_dcd_v2_check {
	uint8_t tag;
	uint16_t length;
	uint8_t param;
	uint32_t addr;
	uint32_t mask;
	uint32_t count;
} __attribute__((packed));

enum imx_dcd_v2_check_cond {
	until_all_bits_clear = 0, /* until ((*address & mask) == 0) { ...} */
	until_any_bit_clear = 1, /* until ((*address & mask) != mask) { ...} */
	until_all_bits_set = 2, /* until ((*address & mask) == mask) { ...} */
	until_any_bit_set = 3, /* until ((*address & mask) != 0) { ...} */
} __attribute__((packed));

int parse_config(struct config_data *data, const char *filename);
