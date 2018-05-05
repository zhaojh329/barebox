#ifndef _PICOTCP_H
#define _PICOTCP_H

#include <pico_device.h>

struct pico_device_barebox_eth {
	struct pico_device dev;
	struct eth_device *edev;
};

void pico_adapter_init(struct eth_device *edev);

#endif
