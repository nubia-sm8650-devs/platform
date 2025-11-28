#
# Makefile for NXP transceiver CTN730
#

$(info "lxd nxpnpen.ko enter")

ifeq ($(ZTE_CONFIG_NXPWLC_FULL),true)
	 ccflags-y += -DZTE_CONFIG_NXPWLC_FULL
endif

obj-m += nxpwlc.o
BOARD_VENDOR_KERNEL_MODULES +=  $(KERNEL_MODULES_OUT)/nxpwlc.ko
