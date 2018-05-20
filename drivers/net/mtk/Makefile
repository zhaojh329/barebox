#
# Makefile for the Ralink SoCs built-in ethernet macs
#

mtk-eth-soc-y					+= mtk_eth_soc.o

mtk-eth-soc-$(CONFIG_NET_MEDIATEK_RT3050)	+= soc_rt3050.o

obj-$(CONFIG_NET_MEDIATEK_ESW_RT3050)		+= esw_rt3050.o
obj-$(CONFIG_NET_MEDIATEK_SOC)			+= mtk-eth-soc.o
