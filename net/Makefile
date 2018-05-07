obj-y			+= lib.o
obj-$(CONFIG_NET)	+= eth.o
obj-$(CONFIG_NET)	+= net.o
obj-$(CONFIG_NET_NFS)	+= nfs.o
obj-$(CONFIG_NET_DHCP)	+= dhcp.o
obj-$(CONFIG_NET_SNTP)	+= sntp.o
obj-$(CONFIG_CMD_PING)	+= ping.o
obj-$(CONFIG_NET_RESOLV)+= dns.o
obj-$(CONFIG_NET_NETCONSOLE) += netconsole.o
obj-$(CONFIG_NET_IFUP)	+= ifup.o

obj-$(CONFIG_NET_PICOTCP) += picotcp.o picotcp/

obj-$(CONFIG_CMD_PICO_DHCP)  += pico_dhcp.o
ifneq ($(CONFIG_NET_PICO_SUPPORT_UDP),)
CFLAGS += -DPICO_SUPPORT_UDP
endif

obj-$(CONFIG_CMD_PICO_PING)  += pico_ping.o
obj-$(CONFIG_CMD_PICO_WGET)  += pico_wget.o
obj-$(CONFIG_CMD_PICO_IFCONFIG)  += pico_ifconfig.o
obj-$(CONFIG_CMD_PICO_HTTPD)  += pico_httpd.o