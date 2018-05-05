#include <common.h>
#include <command.h>

#include <net.h>
#include <picotcp.h>
#include <pico_dhcp_client.h>

static uint32_t dhcp_xid;
static int dhcp_done;
static int dhcp_code;
static struct pico_device *picodev;

static void callback_dhcpclient(void *arg, int code)
{
	struct pico_device_barebox_eth *t = (struct pico_device_barebox_eth *)picodev;
	struct pico_ip4 address = {}, netmask = {}, gateway = {}, nameserver = {};
	struct eth_device *edev = t->edev;

	if (code == PICO_DHCP_SUCCESS) {
		address = pico_dhcp_get_address(arg);
		netmask = pico_dhcp_get_netmask(arg);
		gateway = pico_dhcp_get_gateway(arg);
		nameserver = pico_dhcp_get_nameserver(arg, 0);

		net_set_ip(edev, address.addr);
		net_set_netmask(edev, netmask.addr);
		net_set_gateway(gateway.addr);
		net_set_nameserver(nameserver.addr);

		dev_info(&edev->dev, "DHCP client bound to address %pI4\n", &address.addr);
	} else {
		printf("DHCP transaction failed.\n");
	}

	dhcp_done = 1;
	dhcp_code = code;
}

static int do_pico_dhcp(int argc, char *argv[])
{
	const char *devname;
	uint64_t dhcp_start;

	if (argc < 2)
		devname = "eth0";
	else
		devname = argv[1];

	dhcp_done = 0;
	dhcp_code = PICO_DHCP_RESET;

	picodev = pico_get_device(devname);
	if (!picodev) {
		printf("No such network device: %s\n", devname);
		return 1;
	}

	if (pico_dhcp_initiate_negotiation(picodev, &callback_dhcpclient, &dhcp_xid) < 0) {
		printf("Failed to send DHCP request.\n");
		return 1;
	}

	dhcp_start = get_time_ns();

	while (!dhcp_done) {
		if (ctrlc()) {
			pico_dhcp_client_abort(dhcp_xid);
			break;
		}

		if (is_timeout(dhcp_start, 3 * SECOND)) {
			dhcp_start = get_time_ns();
			printf("T ");
		}
	}

	if (dhcp_code != PICO_DHCP_SUCCESS) {
		return -EIO;
	}

	return 0;
}

BAREBOX_CMD_START(dhcp)
	.cmd		= do_pico_dhcp,
	BAREBOX_CMD_DESC("DHCP client to obtain IP or boot params")
	BAREBOX_CMD_GROUP(CMD_GRP_NET)
BAREBOX_CMD_END
