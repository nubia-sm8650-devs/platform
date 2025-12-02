#
# Makefile for kernel Silead Fingerprint drivers.
#

ifdef CONFIG_ARCH_MSM
	 ccflags-y += -DBSP_SIL_PLAT_QCOM
endif

#ifdef CONFIG_ARCH_QCOM
	 ccflags-y += -DBSP_SIL_PLAT_QCOM
#endif

ifdef CONFIG_ARCH_SPRD
	 ccflags-y += -DBSP_SIL_PLAT_SPRD
endif

ifeq ($(ZTE_FINGERPRINT_PANEL_ENABLE),true)
	 ccflags-y += -DZTE_FINGERPRINT_PANEL_ENABLE
endif

ifeq ($(ZTE_FEATURE_SECONDARY_ENABLE),true)
	 ccflags-y += -DZTE_FEATURE_SECONDARY_ENABLE
endif

ifdef CONFIG_ARCH_MTK_PROJECT
   #include $(srctree)/drivers/misc/mediatek/Makefile.custom
   ccflags-y += -DBSP_SIL_PLAT_MTK
   ccflags-y += -I$(srctree)/drivers/spi/mediatek/$(MTK_PLATFORM)
   subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include \
					-I$(srctree)/drivers/misc/mediatek/include/mt-plat
 ifeq ($(CONFIG_TRUSTONIC_TEE_SUPPORT),y)
   subdir-ccflags-y += -I$(srctree)/drivers/spi/trustzone/spi/Tlspi/inc
 endif
 ifeq ($(CONFIG_TRUSTONIC_TEE_SUPPORT),y)
	include $(srctree)/drivers/misc/mediatek/gud/Makefile.include
 endif
endif

ccflags-y += -DCONFIG_SILEAD_FP_PLATFORM
# obj-$(CONFIG_SILEAD_FP)     += silead_fp.o
# obj-$(CONFIG_SILEAD_FP)     += silead_fp_qcom.o

obj-m += fp_silead.o

fp_silead-objs := silead_fp.o
# fp_silead-objs += silead_fp_qcom.o

BOARD_VENDOR_KERNEL_MODULES +=  $(KERNEL_MODULES_OUT)/fp_silead.ko
