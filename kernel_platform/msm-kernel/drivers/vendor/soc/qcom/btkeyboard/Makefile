# SPDX-License-Identifier: GPL-2.0
#
# Makefile for misc devices that really don't fit anywhere else.
#
#zte add for zte_btkeyboard
$(info "enter into zte keyboard module compile")
obj-$(CONFIG_ZTE_BTKEYBOARD)	+= zte_btkeyboard.o

obj-$(CONFIG_ZTE_KEYBOARD_DOCK)    += gpio_keys-zte.o

# ifeq ($(CONFIG_ZTE_BTKEYBOARD), m)
# $(info "Copy zte_btkeyboard.ko")
# BOARD_VENDOR_KERNEL_MODULES +=  $(KERNEL_MODULES_OUT)/zte_btkeyboard.ko
# endif

# ifeq ($(CONFIG_ZTE_KEYBOARD_DOCK), m)
# $(info "Copy gpio_keys-zte.ko")
# BOARD_VENDOR_KERNEL_MODULES +=  $(KERNEL_MODULES_OUT)/gpio_keys-zte.ko
# endif
