# barebox

# 步骤

	sudo apt install gcc-arm-none-eabi
	git clone https://git.oschina.net/zhaojh329/barebox.git
	cd barebox
	export ARCH=arm
	export CROSS_COMPILE=arm-none-eabi-
	make embedsky_tq210_defconfig
	make

# SD卡烧写

    将barebox.s5p烧写到SD卡的扇区1，从SD卡启动

# Nand烧写

    首先从SD卡启动，配置网络（使用nv命令），然后通过tftp命令下载barebox.s5p

    tftp barebox.s5p

    然后执行命令barebox_update将下载的barebox.s5p烧写到Nand

    barebox_update -y barebox.s5p

# TFTP文件系统

	TFTP并没有实现一个文件系统，不支持列出目录。也就是说对一个使用TFTP挂载的目录执行ls命令将会显示空，然而文件确实在那里。
	
	barebox:/ mount -t tftp 192.168.23.4 /mnt/tftp
	barebox:/ bootm /mnt/tftp/uImage

# NFS网络文件系统

	barebox:/ mount -t nfs 192.168.23.4:/nfs /mnt/nfs
	barebox:/ ls /mnt/nfs

# 网络控制台
	
	barebox:/ netconsole.ip=192.168.23.2
	barebox:/ netconsole.active=ioe
	此处的IP为PC机的IP，active由“input” (i), “output” (o)和“error” (e)三个域组成，如果被设置，相应的通道被激活。

	在PC段执行脚本连接开发板
	scripts/netconsole <board IP> 6666
	
