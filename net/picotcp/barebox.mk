ifneq ($(CONFIG_CPU_BIG_ENDIAN),)
  CFLAGS += -DPICO_BIGENDIAN
endif

ifneq ($(CONFIG_NET_PICO_SUPPORT_ETH),)
  include $(srctree)/net/picotcp/rules/eth.mk
endif

ifneq ($(CONFIG_NET_PICO_SUPPORT_IPV4),)
  include $(srctree)/net/picotcp/rules/ipv4.mk
endif

ifneq ($(CONFIG_NET_PICO_SUPPORT_IPV4FRAG),)
 include $(srctree)/net/picotcp/rules/ipv4frag.mk
endif

ifneq ($(CONFIG_NET_PICO_SUPPORT_ICMP4),)
  include $(srctree)/net/picotcp/rules/icmp4.mk
endif

ifneq ($(CONFIG_NET_PICO_SUPPORT_TCP),)
  include $(srctree)/net/picotcp/rules/tcp.mk
endif

ifneq ($(CONFIG_NET_PICO_SUPPORT_UDP),)
  include $(srctree)/net/picotcp/rules/udp.mk
endif

ifneq ($(CONFIG_NET_PICO_SUPPORT_MCAST),)
  include $(srctree)/net/picotcp/rules/mcast.mk
endif

ifneq ($(CONFIG_NET_PICO_SUPPORT_DEVLOOP),)
  include $(srctree)/net/picotcp/rules/devloop.mk
endif

ifneq ($(CONFIG_NET_PICO_SUPPORT_DHCP_CLIENT),)
  include $(srctree)/net/picotcp/rules/dhcp_client.mk
endif

ifneq ($(CONFIG_NET_PICO_SUPPORT_DHCP_SERVER),)
  include $(srctree)/net/picotcp/rules/dhcp_server.mk
endif

ifneq ($(CONFIG_NET_PICO_SUPPORT_DNS_CLIENT),)
  include $(srctree)/net/picotcp/rules/dns_client.mk
endif

ifneq ($(CONFIG_NET_PICO_SUPPORT_TFTP),)
  OPTIONS+=-DPICO_SUPPORT_TFTP
  MOD_OBJ += modules/pico_strings.o modules/pico_tftp.o
endif

CFLAGS += $(OPTIONS)

obj-y = stack/pico_stack.o \
		stack/pico_frame.o \
		stack/pico_device.o \
		stack/pico_protocol.o \
		stack/pico_socket.o \
		stack/pico_socket_multicast.o \
		stack/pico_tree.o

obj-y += $(MOD_OBJ)
