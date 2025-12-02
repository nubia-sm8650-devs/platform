// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2019, 2021 The Linux Foundation. All rights reserved.
 */

/* Started by AICoder, pid:252fb3d569q2c85145070b84600c2b224c682819 */
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/pm.h>
#include <linux/of_address.h>
#include <linux/nvmem-consumer.h>
#include <linux/slab.h>

struct zte_ramdisk_reboot {
	struct device *dev;
	struct kobject kobj;
	struct nvmem_cell *vendor_ramdisk_zlog_nvmem_cell;
};
static struct zte_ramdisk_reboot *ramdisk_rb = NULL;

#define SDAM_RAMDISK_LEN 1  // fixed line
#define RAMDISK_SAVED_STR "2"  // fixed init val, not possible used
#define RAMDISK_READ_STR "6"  // fixed init val, not possible used
#define CONST_TAG_W 'W'  // single char for modem
#define SAVED_RAMDISK_OFFSET 0  // offset for subsystem

u8 saved_ramdisk_buf[SDAM_RAMDISK_LEN] = {0x32};  // char 2
u8 read_ramdisk_buf[SDAM_RAMDISK_LEN] = {0x36};  // char 6
/* Ended by AICoder, pid:252fb3d569q2c85145070b84600c2b224c682819 */

/* Started by AICoder, pid:h5753lca658c9df14a20093e60541e2ae319fc28 */
static void save_ramdisk_data_to_nvmem(struct zte_ramdisk_reboot *reboot)
{
	int ret;

	if (reboot != NULL) {
		if (IS_ERR(reboot->vendor_ramdisk_zlog_nvmem_cell)) {
			ret = PTR_ERR(reboot->vendor_ramdisk_zlog_nvmem_cell);
			pr_err("ztedbg invalid vendor_ramdisk_zlog cell %d\n", ret);
		} else {
			pr_info("ztedbg write vendor_ramdisk_zlog: %x\n", saved_ramdisk_buf[0]);
			nvmem_cell_write(reboot->vendor_ramdisk_zlog_nvmem_cell, &saved_ramdisk_buf, SDAM_RAMDISK_LEN);
		}
	} else {
		pr_err("ztedbg NULL reboot struct in ramdisk reboot save");
	}
}

// return 0 for write ok, or -1 for errors
int zte_ramdisk_reboot_write(u8 *ptr, int len)
{
	if (ramdisk_rb != NULL && ptr != NULL && len == 1) {
		saved_ramdisk_buf[SAVED_RAMDISK_OFFSET] = *ptr;
		save_ramdisk_data_to_nvmem(ramdisk_rb);
		return 0;
	}

	return -1;
}
EXPORT_SYMBOL(zte_ramdisk_reboot_write);
/* Ended by AICoder, pid:h5753lca658c9df14a20093e60541e2ae319fc28 */

/* Started by AICoder, pid:n5482pd3836253a14bca082091d3194e16b1ca06 */
struct ramdiskboot_attribute {
	struct attribute		attr;
	ssize_t (*show)(struct kobject *kobj, struct attribute *attr, char *buf);
	ssize_t (*store)(struct kobject *kobj, struct attribute *attr, const char *buf, size_t count);
};

#define to_ramdiskboot_attr(_attr) \
	container_of(_attr, struct ramdiskboot_attribute, attr)

static ssize_t attr_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct ramdiskboot_attribute *ramdiskboot_attr = to_ramdiskboot_attr(attr);
	ssize_t ret = -EIO;

	if (ramdiskboot_attr->show)
		ret = ramdiskboot_attr->show(kobj, attr, buf);

	return ret;
}

static ssize_t attr_store(struct kobject *kobj, struct attribute *attr, const char *buf, size_t count)
{
	struct ramdiskboot_attribute *ramdiskboot_attr = to_ramdiskboot_attr(attr);
	ssize_t ret = -EIO;

	if (ramdiskboot_attr->store)
		ret = ramdiskboot_attr->store(kobj, attr, buf, count);

	return ret;
}

static const struct sysfs_ops ramdiskboot_sysfs_ops = {
	.show   = attr_show,
	.store  = attr_store,
};

static struct kobj_type ramdisk_nvmem_kobj_type = {
	.sysfs_ops	= &ramdiskboot_sysfs_ops,
};

static ssize_t ramdisk_nvmem_show(struct kobject *kobj, struct attribute *this, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%c\n",
		read_ramdisk_buf[0]);
}

static ssize_t ramdisk_nvmem_store(struct kobject *kobj, struct attribute *this, const char *buf, size_t count)
{
	pr_err("ztedeg not support ramdisk nvmem set\n");
	return -EINVAL;
}

static struct ramdiskboot_attribute attr_ramdisk_nvmem = __ATTR_RW(ramdisk_nvmem);

static struct attribute *qcom_ramdisk_nvmem_attrs[] = {
	&attr_ramdisk_nvmem.attr,
	NULL
};

static struct attribute_group qcom_ramdisk_nvmem_attr_group = {
	.attrs = qcom_ramdisk_nvmem_attrs,
};


static int zte_ramdisk_reboot_probe(struct platform_device *pdev)
{
	struct zte_ramdisk_reboot *reboot;
	u8 *buf;
	int ret;
	size_t len;

	reboot = devm_kzalloc(&pdev->dev, sizeof(*reboot), GFP_KERNEL);
	if (!reboot)
		return -ENOMEM;

	reboot->dev = &pdev->dev;

	ret = kobject_init_and_add(&reboot->kobj, &ramdisk_nvmem_kobj_type, kernel_kobj, "ramdiskboot");
	if (ret) {
		pr_err("%s: Error in creation kobject_add\n", __func__);
		kobject_put(&reboot->kobj);
		return ret;
	}

	ret = sysfs_create_group(&reboot->kobj, &qcom_ramdisk_nvmem_attr_group);
	if (ret) {
		pr_err("%s: Error in creation sysfs_create_group\n", __func__);
		kobject_del(&reboot->kobj);
		return ret;
	}

	reboot->vendor_ramdisk_zlog_nvmem_cell = nvmem_cell_get(reboot->dev, "vendor_ramdisk_zlog");
	if (IS_ERR(reboot->vendor_ramdisk_zlog_nvmem_cell)) {
		ret = PTR_ERR(reboot->vendor_ramdisk_zlog_nvmem_cell);
		pr_err("ztedbg failed to get vendor_ramdisk_zlog cell %d\n", ret);
	} else {
		buf = nvmem_cell_read(reboot->vendor_ramdisk_zlog_nvmem_cell, &len);
		if (IS_ERR(buf)) {
			ret = PTR_ERR(buf);
			pr_err("ztedbg failed to read vendor_ramdisk_zlog %d\n", ret);
		} else {
			if (len >= SDAM_RAMDISK_LEN) {
				read_ramdisk_buf[0] = buf[0];
				pr_info("ztedbg read 1 bytes vendor_ramdisk_zlog: %x\n",
					read_ramdisk_buf[0]);
			} else {
				pr_err("ztedbg unexpected vendor_ramdisk_zlog len: %d r: %d\n", SDAM_RAMDISK_LEN, len);
			}
			kfree(buf);
			// save_ramdisk_data_to_nvmem(reboot);  // will be cleared by late zte reboot ext module
		}
	}

	platform_set_drvdata(pdev, reboot);
	ramdisk_rb = reboot;

	return 0;
}

static int zte_ramdisk_reboot_remove(struct platform_device *pdev)
{
	// struct zte_ramdisk_reboot *reboot = platform_get_drvdata(pdev);
	// do not support module remove now

	return 0;
}

static const struct of_device_id of_zte_ramdisk_reboot_match[] = {
	{ .compatible = "zte,reboot-ramdisk-ext", },
	{},
};
MODULE_DEVICE_TABLE(of, of_zte_ramdisk_reboot_match);

static struct platform_driver zte_ramdisk_reboot_driver = {
	.probe = zte_ramdisk_reboot_probe,
	.remove = zte_ramdisk_reboot_remove,
	.driver = {
		.name = "zte-ramdisk-reboot",
		.of_match_table = of_match_ptr(of_zte_ramdisk_reboot_match),
	},
};

module_platform_driver(zte_ramdisk_reboot_driver);
/* Ended by AICoder, pid:n5482pd3836253a14bca082091d3194e16b1ca06 */

MODULE_DESCRIPTION("ZTE Ramdisk Reboot Driver");
MODULE_LICENSE("GPL v2");
