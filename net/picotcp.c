#include <init.h>
#include <net.h>
#include <poller.h>
#include <picotcp.h>
#include <pico_stack.h>

int pico_adapter_param_set_ip(struct param_d *param, void *priv)
{
	struct eth_device *edev = priv;
	struct pico_device *picodev = edev->picodev;
	struct pico_ip4 address = {.addr = edev->ipaddr};
	struct pico_ip4 netmask = {.addr = edev->netmask};

	pico_ipv4_cleanup_links(picodev);
	pico_ipv4_link_add(picodev, address, netmask);

	return 0;
}

int pico_adapter_param_set_ethaddr(struct param_d *param, void *priv)
{
	struct eth_device *edev = priv;
	struct pico_device *picodev = edev->picodev;

	memcpy(picodev->eth->mac.addr, edev->ethaddr, ETH_ALEN);

	return eth_set_ethaddr(edev, edev->ethaddr);
}

static int pico_adapter_send(struct pico_device *dev, void *buf, int len)
{
	int ret;
	struct pico_device_barebox_eth *t = (struct pico_device_barebox_eth *)dev;

	ret = eth_send(t->edev, buf, len);

	return ret ? ret : len;
}

static int pico_adapter_poll(struct pico_device *dev, int loop_score)
{
	struct pico_device_barebox_eth *t = (struct pico_device_barebox_eth *)dev;
	struct eth_device *edev = t->edev;

	if (edev->active)
		edev->recv(edev);

	/* pico_stack_recv(dev, buf, len) will be called from net_receive */

	return loop_score;
}

void pico_adapter_init(struct eth_device *edev)
{
	struct pico_device_barebox_eth *pif = PICO_ZALLOC(sizeof(struct pico_device_barebox_eth));
	struct pico_device *picodev;

	if (!is_valid_ether_addr(edev->ethaddr)) {
		char str[sizeof("xx:xx:xx:xx:xx:xx")];
		random_ether_addr(edev->ethaddr);
		ethaddr_to_string(edev->ethaddr, str);
		pr_warn("warning: No MAC address set. Using random address %s\n", str);
		eth_set_ethaddr(edev, edev->ethaddr);
	}

	picodev = &pif->dev;
	if (0 != pico_device_init(picodev, edev->devname, edev->ethaddr)) {
		pr_info("pico_adapter_init failed\n");
		pico_device_destroy(picodev);
		return;
	}

	picodev->send = pico_adapter_send;
	picodev->poll = pico_adapter_poll;

	pif->edev = edev;
	edev->picodev = picodev;

	pr_info("Device %s created.\n", picodev->name);
}

static void picotcp_poller_cb(struct poller_struct *poller)
{
	pico_stack_tick();
}

static int picotcp_net_init(void)
{
	static struct poller_struct picotcp_poller = {
		.func = picotcp_poller_cb
	};

	pico_stack_init();

	return poller_register(&picotcp_poller);
}
postcore_initcall(picotcp_net_init);

