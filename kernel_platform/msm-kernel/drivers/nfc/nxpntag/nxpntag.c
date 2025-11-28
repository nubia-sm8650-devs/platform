/*
 * SPDX-License-Identifier: GPL-2.0-only
 * NFC Controller Driver
 * Copyright (C) 2020 ST Microelectronics S.A.
 * Copyright (C) 2010 Stollmann E+V GmbH
 * Copyright (C) 2010 Trusted Logic S.A.
 */
#define DEBUG
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/of_gpio.h>
#ifndef LEGACY
#include <linux/workqueue.h>
#include <linux/acpi.h>
#include <linux/gpio/consumer.h>
#include <net/nfc/nci.h>
#include <linux/clk.h>
#else
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_address.h>
#endif
#include <linux/of_irq.h>
#define DRIVER_VERSION "2.2.0.14"
// #define BUFFER_SIZE 6  //BLOCK_SIZE=4 & ADDR_SIZE=2
#define MAX_BUFFER_SIZE 5
#define NXPNTAG_HPD_RESET          _IOW('g', 0x31, __u32)
//#define DOUBLE_CHIP_BOARD_ID  10
extern int request_board_id(void);
static bool enable_debug_log = true;
struct nxpntag_device {
	struct i2c_client *client;
	struct miscdevice nxpntag_device;
	bool device_open; /* Is device open? */
	// bool irq_wake_up;
	bool irq_is_attached;
	spinlock_t irq_enabled_lock;
	/* irq_gpio polarity to be used */
	unsigned int polarity_mode;
	bool irq_enabled;
	wait_queue_head_t read_wq;
	/* GPIO for NFCC IRQ pin (input) */
	struct gpio_desc *gpiod_irq;
	/* GPIO for NFCC Reset and Boost pin (output) */
	struct gpio_desc *gpiod_reset;
	struct gpio_desc *gpiod_boost;
};
static void nxpntag_disable_irq(struct nxpntag_device *nxpntag_dev)
{
	unsigned long flags;
	spin_lock_irqsave(&nxpntag_dev->irq_enabled_lock, flags);
	pr_debug("%s : enter\n", __func__);
	if (nxpntag_dev->irq_enabled) {
		disable_irq_nosync(nxpntag_dev->client->irq);
		nxpntag_dev->irq_enabled = false;
	}
	spin_unlock_irqrestore(&nxpntag_dev->irq_enabled_lock, flags);
}
static void nxpntag_enable_irq(struct nxpntag_device *nxpntag_dev)
{
	unsigned long flags;
	spin_lock_irqsave(&nxpntag_dev->irq_enabled_lock, flags);
	pr_debug("%s : enter\n", __func__);
	if (!nxpntag_dev->irq_enabled) {
		pr_debug("%s : enable_irq enter\n", __func__);
		nxpntag_dev->irq_enabled = true;
		enable_irq(nxpntag_dev->client->irq);
	}
	spin_unlock_irqrestore(&nxpntag_dev->irq_enabled_lock, flags);
}
static irqreturn_t nxpntag_dev_irq_handler(int irq, void *dev_id)
{
	struct nxpntag_device *nxpntag_dev = dev_id;
	pr_debug("%s : enter\n", __func__);
	// if (device_may_wakeup(&nxpntag_dev->client->dev))
	// 	pm_wakeup_event(&nxpntag_dev->client->dev, WAKEUP_SRC_TIMEOUT);
	nxpntag_disable_irq(nxpntag_dev);
	/* Wake up waiting readers */
	wake_up(&nxpntag_dev->read_wq);
	return IRQ_HANDLED;
}
static int nxpntag_loc_set_polaritymode(
	struct nxpntag_device *nxpntag_dev, int mode)
{
	struct i2c_client *client = nxpntag_dev->client;
	struct device *dev = &client->dev;
	unsigned int irq_type;
	int ret;
	if (enable_debug_log)
		pr_info("%s: mode %d", __func__, mode);
	nxpntag_dev->polarity_mode = mode;
	/* setup irq_flags */
	switch (mode) {
	case IRQF_TRIGGER_RISING:
		irq_type = IRQ_TYPE_EDGE_RISING;
		break;
	case IRQF_TRIGGER_LOW:
		irq_type = IRQ_TYPE_LEVEL_LOW;
		break;
	default:
		irq_type = IRQ_TYPE_LEVEL_LOW;
		break;
	}
	if (nxpntag_dev->irq_is_attached) {
		devm_free_irq(dev, client->irq, nxpntag_dev);
		nxpntag_dev->irq_is_attached = false;
	}
	ret = irq_set_irq_type(client->irq, irq_type);
	if (ret) {
		pr_err("%s : set_irq_type failed\n", __func__);
		return -ENODEV;
	}
	/* request irq.  the irq is set whenever the chip has data available
	 * for reading.  it is cleared when all data has been read.
	 */
	if (enable_debug_log)
		pr_debug("%s : requesting IRQ %d\n", __func__, client->irq);
	nxpntag_dev->irq_enabled = true;
	ret = devm_request_irq(dev, client->irq, nxpntag_dev_irq_handler,
						   nxpntag_dev->polarity_mode,
						   client->name, nxpntag_dev);
	if (ret) {
		pr_err("%s : devm_request_irq failed\n", __func__);
		return -ENODEV;
	}
	nxpntag_dev->irq_is_attached = true;
	nxpntag_disable_irq(nxpntag_dev);
	if (enable_debug_log)
		pr_info("%s: ret %d", __func__, ret);
	return ret;
}
static unsigned int nxpntag_poll(struct file *file, poll_table *wait)
{
	struct nxpntag_device *nxpntag_dev =
		container_of(file->private_data,
			struct nxpntag_device, nxpntag_device);
	unsigned int mask = 0;
	int pinlev = 0;
	/* wait for Wake_up_pin == high  */
	poll_wait(file, &nxpntag_dev->read_wq, wait);
	pinlev = gpiod_get_value(nxpntag_dev->gpiod_irq);
	if (pinlev != 1) {
		if (enable_debug_log)
			pr_debug("%s RF field in\n", __func__);
		mask = POLLIN | POLLRDNORM; /* signal data avail */
		nxpntag_disable_irq(nxpntag_dev);
	} else {
		/* Wake_up_pin is low. Activate ISR  */
		if (!nxpntag_dev->irq_enabled) {
			if (enable_debug_log)
				pr_debug("%s RF field off\n", __func__);
			nxpntag_enable_irq(nxpntag_dev);
		} else {
			if (enable_debug_log)
				pr_debug("%s irq already enabled\n", __func__);
		}
	}
	return mask;
}
static ssize_t nxpntag_dev_read(
	struct file *filp, char __user *buf, size_t count, loff_t *offset)
{
	struct nxpntag_device *nxpntag_dev =
		container_of(filp->private_data,
			struct nxpntag_device, nxpntag_device);
	int ret;
	uint8_t buffer[MAX_BUFFER_SIZE];
	if (count == 0)
		return 0;
	if (count > MAX_BUFFER_SIZE)
		count = MAX_BUFFER_SIZE;
	/* Read data */
	ret = i2c_master_recv(nxpntag_dev->client, buffer, count);
	if (enable_debug_log){
		char tmp_Str[521] = { 0x00 };
		int i = 0;
		for(i = 0; i < ret; i++) {
			snprintf(tmp_Str + 2 * i, 3, "%02hhx", buffer[i]);
		}
		pr_debug("%s : reading %zu bytes.ret = %d.DATA:%s\n", __func__, count, ret, tmp_Str);
	}
	if (ret < 0) {
		pr_err("%s: i2c_master_recv returned %d\n", __func__, ret);
		return ret;
	}
	if (ret > count) {
		pr_err("%s: received too many bytes from i2c (%d)\n",
			__func__, ret);
		return -EIO;
	}
	if (copy_to_user(buf, buffer, ret)) {
		pr_warn("%s : failed to copy to user space\n", __func__);
		return -EFAULT;
	}
	return ret;
}
static ssize_t nxpntag_dev_write(struct file *filp, const char __user *buf,
	size_t count, loff_t *offset)
{
	struct nxpntag_device *nxpntag_dev =
		container_of(filp->private_data,
			struct nxpntag_device, nxpntag_device);
	char *tmp = NULL;
	int ret = count;
	// if (count != BUFFER_SIZE){
	// 	pr_err("%s : WRITE size is not BUFFER_SIZE\n", __func__);
	// 	return -EFAULT;
	// }
	tmp = memdup_user(buf, count);
	if (IS_ERR_OR_NULL(tmp)) {
		pr_err("%s : memdup_user failed\n", __func__);
		return -EFAULT;
	}
	if (enable_debug_log) {
		char tmp_Str[521] = { 0x00 };
		int i = 0;
		for(i = 0; i < ret; i++) {
			snprintf(tmp_Str + 2 * i, 3, "%02hhx", tmp[i]);
		}
		pr_debug("%s : writing %zu bytes.DATA:%s\n", __func__, count, tmp_Str);
	}
	/* Write data */
	ret = i2c_master_send(nxpntag_dev->client, tmp, count);
	if (ret != count) {
		pr_err("%s : i2c_master_send returned %d\n", __func__, ret);
		ret = -EIO;
	}
	kfree(tmp);
	return ret;
}
static int nxpntag_dev_open(struct inode *inode, struct file *filp)
{
	int ret = 0;
	struct nxpntag_device *nxpntag_dev =
		container_of(filp->private_data,
			struct nxpntag_device, nxpntag_device);
	if (enable_debug_log)
		pr_info("%s:%d dev_open", __FILE__, __LINE__);
	if (nxpntag_dev->device_open) {
		ret = -EBUSY;
		pr_err("%s : device already opened ret= %d\n", __func__, ret);
	} else {
		nxpntag_dev->device_open = true;
	}
	return ret;
}
static int nxpntag_release(struct inode *inode, struct file *file)
{
	struct nxpntag_device *nxpntag_dev =
		container_of(file->private_data,
			struct nxpntag_device, nxpntag_device);
	nxpntag_dev->device_open = false;
	if (enable_debug_log)
		pr_debug("%s : device_open  = false\n", __func__);
	return 0;
}
static long nxpntag_dev_ioctl(struct file *filp,
	unsigned int cmd, unsigned long arg)
{
	struct nxpntag_device *nxpntag_dev =
		container_of(filp->private_data,
			struct nxpntag_device, nxpntag_device);
	int ret = 0;
	//u32 __user *argp = (u32 __user *)(arg);
	// if (_IOC_DIR(cmd) & _IOC_WRITE) {
	//	ret = get_user(val, argp);
	//	if (ret)
	//		return ret;
	// }
	switch (cmd) {
	case NXPNTAG_HPD_RESET:
		pr_info("%s: NXPNTAG_HPD_RESET\n", __func__);
		gpiod_set_value(nxpntag_dev->gpiod_reset, 1);
		udelay(200);
		gpiod_set_value(nxpntag_dev->gpiod_reset, 0);
		msleep(5);
		pr_info("%s: NXPNTAG_HPD_RESET end\n", __func__);
		break;
	default:
		ret = -ENOTTY;
	}
	return ret;
}
static const struct file_operations nxpntag_dev_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.read = nxpntag_dev_read,
	.write = nxpntag_dev_write,
	.open = nxpntag_dev_open,
	.poll = nxpntag_poll,
	.release = nxpntag_release,
	.unlocked_ioctl = nxpntag_dev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = nxpntag_dev_ioctl
#endif
};
static int nxpntag_probe(struct i2c_client *client,
						 const struct i2c_device_id *id)
{
	int ret;
	struct nxpntag_device *nxpntag_dev;
	struct device *dev = &client->dev;
	//int32_t board_id;
	//bool double_chip_board;
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("%s : need I2C_FUNC_I2C\n", __func__);
		return -ENODEV;
	}
	nxpntag_dev = devm_kzalloc(dev, sizeof(*nxpntag_dev), GFP_KERNEL);
	if (nxpntag_dev == NULL)
		return -ENOMEM;
	nxpntag_dev->client = client;
	// client->adapter->retries = 0;
	//xm acpi
	// ret = acpi_dev_add_driver_gpios(
	// 	ACPI_COMPANION(dev), acpi_nxpntag_gpios);
	// if (ret)
	// 	pr_debug("Unable to add GPIO mapping table\n");
/* QCOM and MTK54 use standard GPIO definition */
	nxpntag_dev->gpiod_reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR_OR_NULL(nxpntag_dev->gpiod_reset)) {
		pr_warn("%s : Unable to request reset-gpios\n", __func__);
		return -ENODEV;
	}
	gpiod_set_value(nxpntag_dev->gpiod_reset, 0);
	nxpntag_dev->gpiod_boost = devm_gpiod_get(dev, "boost", GPIOD_OUT_HIGH);
	if (IS_ERR_OR_NULL(nxpntag_dev->gpiod_boost)) {
		pr_warn("%s : Unable to request boost-gpios\n", __func__);
		return -ENODEV;
	}
	gpiod_set_value(nxpntag_dev->gpiod_boost, 1);
	nxpntag_dev->irq_is_attached = false;
//xm0328 todolist:feature control
	//board_id = request_board_id();
	//dev_info(dev, "%s: board_id = %d", __func__, board_id);
	//if (board_id == DOUBLE_CHIP_BOARD_ID) double_chip_board = true;
	//else double_chip_board = false;
	//pr_err("%s : lxd double_chip_board: %d\n", __func__, double_chip_board);
	//dev_info(dev, "lxd double_chip_board: %d", double_chip_board);
	/*
	if (double_chip_board) {
		dev_info(dev, "lxd---true");
		nxpntag_dev->gpiod_irq = devm_gpiod_get(dev, "irq_new", GPIOD_IN);
        } else {
		dev_info(dev, "lxd---false");
		nxpntag_dev->gpiod_irq = devm_gpiod_get(dev, "irq_old", GPIOD_IN);
        }
	*/
	pr_err("%s : lxd nxpntag \n", __func__);
	nxpntag_dev->gpiod_irq = devm_gpiod_get(dev, "irq_new", GPIOD_IN);
	if (IS_ERR_OR_NULL(nxpntag_dev->gpiod_irq)) {
		pr_err("%s : Unable to request irq-gpios\n", __func__);
		return -ENODEV;
	}
	client->irq = gpiod_to_irq(nxpntag_dev->gpiod_irq);
	init_waitqueue_head(&nxpntag_dev->read_wq);
	spin_lock_init(&nxpntag_dev->irq_enabled_lock);
	pr_debug("%s : client-irq =  %d\n", __func__, client->irq);
	nxpntag_loc_set_polaritymode(nxpntag_dev, IRQF_TRIGGER_LOW);
	nxpntag_dev->nxpntag_device.minor = MISC_DYNAMIC_MINOR;
	nxpntag_dev->nxpntag_device.name = "nxpntag";
	nxpntag_dev->nxpntag_device.fops = &nxpntag_dev_fops;
	nxpntag_dev->nxpntag_device.parent = dev;
	i2c_set_clientdata(client, nxpntag_dev);
	ret = misc_register(&nxpntag_dev->nxpntag_device);
	if (ret) {
		pr_err("%s : misc_register failed\n", __func__);
		return ret;
	}
	// device_init_wakeup(&client->dev, true);
	// device_set_wakeup_capable(&client->dev, true);
	// nxpntag_dev->irq_wake_up = false;
	pr_info("%s : nxpntag probe complete\n", __func__);
	return 0;
}
static void nxpntag_remove(struct i2c_client *client)
{
	struct nxpntag_device *nxpntag_dev = i2c_get_clientdata(client);
	misc_deregister(&nxpntag_dev->nxpntag_device);
	//return 0;
}
static const struct i2c_device_id nxpntag_id[] = {{"nxpntag", 0}, {} };
static const struct of_device_id nxpntag_of_match[] = {
	{
		.compatible = "nxp,nxpntag",
	},
	{ } };
MODULE_DEVICE_TABLE(of, nxpntag_of_match);
static struct i2c_driver nxpntag_driver = {
	.id_table = nxpntag_id,
	.probe = nxpntag_probe,
	.remove = nxpntag_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "nxpntag",
		.of_match_table = nxpntag_of_match,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		},
};
/* module load/unload record keeping */
static int __init nxpntag_dev_init(void)
{
	pr_info("Loading nxpntag driver\n");
	return i2c_add_driver(&nxpntag_driver);
}
module_init(nxpntag_dev_init);
static void __exit nxpntag_dev_exit(void)
{
	pr_info("Unloading nxpntag driver\n");
	i2c_del_driver(&nxpntag_driver);
}
module_exit(nxpntag_dev_exit);
MODULE_AUTHOR("00283367");
MODULE_DESCRIPTION("NFC nxp tag driver");
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
