#define DEBUG
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
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
#include <linux/gpio.h>
#include <linux/power_supply.h>
#include <vendor/common/zte_misc.h>
#include <linux/file.h>
#include <asm/uaccess.h>

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
#include <linux/of_device.h>
#endif
#include <linux/of_irq.h>

#define DRIVER_VERSION "1.0"
#define MAX_BUFFER_SIZE 512
#define NXPWLC_HPD_RESET _IOW('l', 0x01, __u32)
#define EVENT_STRING_LENGTH 64

#define TOUCH_PEN_ON "touch_pen_event_on=true"
#define TOUCH_PEN_OFF "touch_pen_event_off=true"

static bool enable_debug_log = true;
static char data_buffer[128];

char fwVersionStr[20];
bool isFullBattery = true;
int pen_full = 0;
/* Started by AICoder, pid:laecdd315de722e1427b09374015e402a1b1683a */
static struct kobject *nxpwlc_kobj;
/* Ended by AICoder, pid:laecdd315de722e1427b09374015e402a1b1683a */

struct nxpwlc_device
{
    struct i2c_client *client;
    struct miscdevice nxpwlc_device;
    bool device_open; /* Is device open? */
    bool irq_enabled;
    bool irq_suspend;
    bool irq_is_attached;
    spinlock_t irq_enabled_lock;
    struct mutex suspend_mutex;
    struct mutex write_mutex;
    struct mutex irq_complete;
    struct wakeup_source *irq_wake_lock;
    struct platform_device *uevent_device;
    wait_queue_head_t read_wq;
    /* GPIO for IRQ pin (input) */
    struct gpio_desc *gpiod_irq;
    /* GPIO for Reset and Boost pin (output) */
    struct gpio_desc *gpiod_reset;
    struct gpio_desc *gpiod_boost;
    struct gpio_desc *gpiod_enable;

    struct delayed_work interrupt_work;
    int battery_level;
    int charging_status;
};

static void nxpwlc_disable_irq(struct nxpwlc_device *nxpwlc_dev)
{
    unsigned long flags;
    spin_lock_irqsave(&nxpwlc_dev->irq_enabled_lock, flags);
    pr_debug("%s : enter\n", __func__);
    if (nxpwlc_dev->irq_enabled)
    {
        disable_irq_nosync(nxpwlc_dev->client->irq);
        nxpwlc_dev->irq_enabled = false;
    }
    spin_unlock_irqrestore(&nxpwlc_dev->irq_enabled_lock, flags);
}

static void nxpwlc_enable_irq(struct nxpwlc_device *nxpwlc_dev)
{
    unsigned long flags;
    spin_lock_irqsave(&nxpwlc_dev->irq_enabled_lock, flags);
    pr_debug("%s : enter\n", __func__);
    if (!nxpwlc_dev->irq_enabled)
    {
        pr_debug("%s : enable_irq enter\n", __func__);
        nxpwlc_dev->irq_enabled = true;
        enable_irq(nxpwlc_dev->client->irq);
    }
    spin_unlock_irqrestore(&nxpwlc_dev->irq_enabled_lock, flags);
}

static irqreturn_t nxpwlc_dev_irq_handler(int irq, void *dev_id)
{
    struct nxpwlc_device *nxpwlc_dev = dev_id;
    pr_info("%s enter! power info changed!\n", __func__);
    __pm_wakeup_event(nxpwlc_dev->irq_wake_lock, 2000);
    schedule_delayed_work(&nxpwlc_dev->interrupt_work, msecs_to_jiffies(800));
    // nxpwlc_disable_irq(nxpwlc_dev);
    wake_up(&nxpwlc_dev->read_wq);
    return IRQ_HANDLED;
}

static ssize_t nxpwlc_dev_read(
    struct file *filp, char __user *buf, size_t count, loff_t *offset)
{
    struct nxpwlc_device *nxpwlc_dev =
        container_of(filp->private_data,
                     struct nxpwlc_device, nxpwlc_device);
    int ret;
    uint8_t buffer[MAX_BUFFER_SIZE];
    if (count == 0)
        return 0;
    if (count > MAX_BUFFER_SIZE)
        count = MAX_BUFFER_SIZE;
    /* Read data */
    ret = i2c_master_recv(nxpwlc_dev->client, buffer, count);
    if (enable_debug_log)
    {
        char tmp_Str[521] = {0x00};
        int i = 0;
        for (i = 0; i < ret; i++)
        {
            snprintf(tmp_Str + 2 * i, 3, "%02hhx", buffer[i]);
        }
        pr_debug("%s : reading %zu bytes.ret = %d.DATA:%s\n", __func__, count, ret, tmp_Str);
    }
    if (ret < 0)
    {
        pr_err("%s: i2c_master_recv returned %d\n", __func__, ret);
        return ret;
    }
    if (ret > count)
    {
        pr_err("%s: received too many bytes from i2c (%d)\n",
               __func__, ret);
        return -EIO;
    }
    if (copy_to_user(buf, buffer, ret))
    {
        pr_warn("%s : failed to copy to user space\n", __func__);
        return -EFAULT;
    }
    return ret;
}

static ssize_t nxpwlc_dev_write(struct file *filp, const char __user *buf,
                                size_t count, loff_t *offset)
{
    struct nxpwlc_device *nxpwlc_dev =
        container_of(filp->private_data,
                     struct nxpwlc_device, nxpwlc_device);
    char *tmp = NULL;
    ssize_t ret = count;

    tmp = memdup_user(buf, count);
    if (IS_ERR_OR_NULL(tmp))
    {
        pr_err("%s : memdup_user failed\n", __func__);
        return -EFAULT;
    }

    if (enable_debug_log)
    {
        char tmp_Str[521] = {0x00};
        int i = 0;
        for (i = 0; i < ret; i++)
        {
            snprintf(tmp_Str + 2 * i, 3, "%02hhx", tmp[i]);
        }
        pr_debug("%s : writing %zu bytes.DATA:%s\n", __func__, count, tmp_Str);
    }

    /* Write data */
    ret = i2c_master_send(nxpwlc_dev->client, tmp, count);
    pr_err("%s ret: %zd!\n", __func__, ret);
    if (ret <= 0)
    {
        /* retry to handle standby mode */
        pr_err("%s: i2c_master_send returned %zd\n", __func__, ret);
        msleep(5);
        ret = i2c_master_send(nxpwlc_dev->client, tmp, count);
        if (ret <= 0)
        {
            msleep(5);
            ret = i2c_master_send(nxpwlc_dev->client, tmp, count);
            if (ret <= 0)
            {
                pr_err("%s err_ret: %zd!\n", __func__, ret);
            }
        }
    }
    if (ret != count)
    {
        pr_err("%s : i2c_master_send returned %d\n", __func__, ret);
        ret = -EIO;
    }
    kfree(tmp);
    return ret;
}

static int nxpwlc_dev_open(struct inode *inode, struct file *filp)
{
    int ret = 0;
    struct nxpwlc_device *nxpwlc_dev =
        container_of(filp->private_data,
                     struct nxpwlc_device, nxpwlc_device);

    if (enable_debug_log)
        pr_info("%s:%d dev_open", __FILE__, __LINE__);
    if (nxpwlc_dev->device_open)
    {
        ret = -EBUSY;
        pr_err("%s : device already opened ret= %d\n", __func__, ret);
    }
    else
    {
        nxpwlc_dev->device_open = true;
    }
    return ret;
}

static int nxpwlc_release(struct inode *inode, struct file *file)
{
    struct nxpwlc_device *nxpwlc_dev =
        container_of(file->private_data,
                     struct nxpwlc_device, nxpwlc_device);

    /*
    if (nxpwlc_dev->irq_is_attached)
    {
        nxpwlc_disable_irq(nxpwlc_dev);
        devm_free_irq(&nxpwlc_dev->client->dev, nxpwlc_dev->client->irq, nxpwlc_dev);
        nxpwlc_dev->irq_is_attached = false;
    }
    */
    nxpwlc_dev->device_open = false;
    if (enable_debug_log)
        pr_debug("%s : device_open  = false\n", __func__);
    return 0;
}

static long nxpwlc_dev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct nxpwlc_device *nxpwlc_dev =
        container_of(filp->private_data,
                     struct nxpwlc_device, nxpwlc_device);
    int ret = 0;
    switch (cmd)
    {
    case NXPWLC_HPD_RESET:
        pr_info("%s: NXPWLC_HPD_RESET\n", __func__);
        gpiod_set_value(nxpwlc_dev->gpiod_reset, 0);
        udelay(200);
        gpiod_set_value(nxpwlc_dev->gpiod_reset, 1);
        msleep(5);
        pr_info("%s: NXPWLC_HPD_RESET end\n", __func__);
        break;
    default:
        ret = -ENOTTY;
    }
    return ret;
}

static const struct file_operations nxpwlc_dev_fops = {
    .owner = THIS_MODULE,
    .llseek = no_llseek,
    .read = nxpwlc_dev_read,
    .write = nxpwlc_dev_write,
    .open = nxpwlc_dev_open,
    .release = nxpwlc_release,
    .unlocked_ioctl = nxpwlc_dev_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = nxpwlc_dev_ioctl
#endif
};

static int nxpwlc_capacity_get(char *val, const void *arg)
{
    struct nxpwlc_device *nxpwlc_dev = (struct nxpwlc_device *)arg;
    pr_info("%s enter!\n", __func__);
    if (!nxpwlc_dev)
    {
        pr_info("nxp wlc chg is null\n");
        return snprintf(val, PAGE_SIZE, "nxp wlc arg is null");
    }
    return snprintf(val, PAGE_SIZE, "%u", nxpwlc_dev->battery_level);
}
static struct zte_misc_ops nxpwlc_capacity_node = {
    .node_name = "wls_tx_capacity",
    .set = NULL,
    .get = nxpwlc_capacity_get,
    .free = NULL,
    .arg = NULL,
};
static int nxpwlc_charging_status_get(char *val, const void *arg)
{
    struct nxpwlc_device *nxpwlc_dev = (struct nxpwlc_device *)arg;
    pr_info("%s enter!\n", __func__);
    if (!nxpwlc_dev)
    {
        pr_info("nxp wlc chg is null\n");
        return snprintf(val, PAGE_SIZE, "nxp wlc arg is null");
    }
    return snprintf(val, PAGE_SIZE, "%u", nxpwlc_dev->charging_status);
}
static struct zte_misc_ops nxpwlc_charging_status_node = {
    .node_name = "wls_tx_charging_status",
    .set = NULL,
    .get = nxpwlc_charging_status_get,
    .free = NULL,
    .arg = NULL,
};

void nxpwlc_send_capacity_event(struct nxpwlc_device *nxpwlc_dev)
{
    char event_string[EVENT_STRING_LENGTH];
    char *envp[2] = {event_string, NULL};
    if (nxpwlc_dev->uevent_device == NULL)
    {
        pr_err("nxpwlc capacity send failed\n");
        return;
    }
    pr_err("nxpwlc report capacity=%d\n", nxpwlc_dev->battery_level);
    snprintf(event_string, EVENT_STRING_LENGTH, "wls_tx_capacity=%d", nxpwlc_dev->battery_level);
    kobject_uevent_env(&nxpwlc_dev->uevent_device->dev.kobj, KOBJ_CHANGE, envp);
}

void nxpwlc_send_charging_status_event(struct nxpwlc_device *nxpwlc_dev)
{
    char event_string[EVENT_STRING_LENGTH];
    char *envp[2] = {event_string, NULL};
    if (nxpwlc_dev->uevent_device == NULL)
    {
        pr_err("nxpwlc charging status send failed\n");
        return;
    }
    pr_err("nxpwlc report charging_status=%d\n", nxpwlc_dev->charging_status);
    snprintf(event_string, EVENT_STRING_LENGTH, "wls_tx_charging_status=%d", nxpwlc_dev->charging_status);
    kobject_uevent_env(&(nxpwlc_dev->uevent_device->dev.kobj), KOBJ_CHANGE, envp);
}

void nxpwlc_send_pen_event(struct nxpwlc_device *nxpwlc_dev, char *str)
{
    char *envp[2];
    if (nxpwlc_dev->uevent_device == NULL)
    {
        pr_err("nxpwlc pen event send failed\n");
        return;
    }

    envp[0] = str;
    envp[1] = NULL;

    kobject_uevent_env(&nxpwlc_dev->uevent_device->dev.kobj, KOBJ_CHANGE, envp);
}

static int nxpwlc_transceive_write(struct nxpwlc_device *nxpwlc_dev, char *sendBuffer, int sendLength)
{
    ssize_t ret;
    char tmp_Str[521] = {0x00};
    int i = 0;

    pr_err("%s enter!\n", __func__);

    mutex_lock(&nxpwlc_dev->write_mutex);
    ret = i2c_master_send(nxpwlc_dev->client, sendBuffer, sendLength);
    pr_err("%s ret: %d!\n", __func__, ret);
    if (ret <= 0)
    {
        /* retry to handle standby mode */
        pr_err("%s: i2c_master_send returned %zd\n", __func__, ret);
        msleep(5);
        ret = i2c_master_send(nxpwlc_dev->client, sendBuffer, sendLength);
        if (ret <= 0)
        {
            msleep(5);
            ret = i2c_master_send(nxpwlc_dev->client, sendBuffer, sendLength);
            if (ret <= 0)
            {
                pr_err("%s err_ret: %zd!\n", __func__, ret);
            }
        }
    }
    if (enable_debug_log)
    {
        for (i = 0; i < ret; i++)
        {
            snprintf(tmp_Str + 2 * i, 3, "%02hhx", sendBuffer[i]);
        }
        pr_err("%s : writing %zu bytes.DATA:%s\n", __func__, ret, tmp_Str);
    }
    mutex_unlock(&nxpwlc_dev->write_mutex);
    return ret;
}

/* Started by AICoder, pid:laecdq315d2722e1427b09374015e412a1b5683a */
static ssize_t nxpwlc_fw_version_show(struct kobject *kobj, struct kobj_attribute *attr,
                                      char *buf)
{
    pr_debug("%s : enter\n", __func__);
    return sprintf(buf, "%s\n", fwVersionStr);
}

static ssize_t nxpwlc_fw_version_store(struct kobject *kobj, struct kobj_attribute *attr,
                                       const char *buf, size_t count)
{
    pr_debug("%s : enter\n", __func__);
    if (count >= sizeof(fwVersionStr))
        return -EINVAL;
    strncpy(fwVersionStr, buf, count);
    fwVersionStr[count] = '\0';
    return count;
}

static struct kobj_attribute fw_version_attribute =
    __ATTR(fw_version, 0664, nxpwlc_fw_version_show, nxpwlc_fw_version_store);
/* Ended by AICoder, pid:laecdq315d2722e1427b09374015e412a1b5683a */

static struct attribute *nxpwlc_attrs[] = {
    &fw_version_attribute.attr,
    NULL,
};

static struct attribute_group nxpwlc_attr_group = {
    .attrs = nxpwlc_attrs,
};

static int nxpwlc_transceive_read(struct nxpwlc_device *nxpwlc_dev, char *receiveBuffer)
{
    int ret;
    int receive_ret1;
    int receive_ret2;
    char Answer[521] = {0x00};

    pr_info("%s enter!\n", __func__);

    mutex_lock(&nxpwlc_dev->write_mutex);
    receive_ret1 = i2c_master_recv(nxpwlc_dev->client, receiveBuffer, 2);

    receive_ret2 = i2c_master_recv(nxpwlc_dev->client, &receiveBuffer[2], receiveBuffer[1]);
    ret = receive_ret1 + receive_ret2;
    if (ret < 0)
    {
        pr_err("%s: i2c_master_recv returned %d\n", __func__, ret);
    }
    if (enable_debug_log)
    {
        int i = 0;
        for (i = 0; i < ret; i++)
        {
            snprintf(Answer + 2 * i, 3, "%02hhx", receiveBuffer[i]);
        }
        pr_debug("%s : reading ret = %d.DATA:%s\n", __func__, ret, Answer);
    }

    if ((ret == 5) && (receiveBuffer[0] == 0x80) && (receiveBuffer[1] == 0x03) && (receiveBuffer[2] == 0x00))
    {
        sprintf(fwVersionStr, "%02X %02X", receiveBuffer[3], receiveBuffer[4]);
        pr_debug("%s :Fw version: %s\n", __func__, fwVersionStr);
    }

    mutex_unlock(&nxpwlc_dev->write_mutex);

    return ret;
}

static int nxpwlc_send_charge_enable(struct nxpwlc_device *nxpwlc_dev)
{
    char buffer[] = {0x10, 0x02, 0x00, 0x02}; // Charge Enable command
    int ret;

    pr_info("%s enter!\n", __func__);

    ret = nxpwlc_transceive_write(nxpwlc_dev, buffer, sizeof(buffer));
    msleep(10);
    ret = nxpwlc_transceive_read(nxpwlc_dev, data_buffer);

    if (ret >= 0)
    {
        dev_err(&nxpwlc_dev->client->dev, "Charge enable command sent successfully\n");
    }
    return 0;
}

static int nxpwlc_send_reset_normal(struct nxpwlc_device *nxpwlc_dev)
{
    char buffer[3] = {0x00, 0x01, 0x00}; // Reset Normal command
    int ret;

    pr_err("%s enter!\n", __func__);

    ret = nxpwlc_transceive_write(nxpwlc_dev, buffer, sizeof(buffer));
    msleep(100);
    ret = nxpwlc_transceive_read(nxpwlc_dev, data_buffer);

    if (ret >= 0)
    {
        dev_err(&nxpwlc_dev->client->dev, "Reset Normal command sent successfully\n");
    }

    return 0;
}

static void nxpwlc_interrupt_workfunc(struct work_struct *work)
{
    int count;
    char Answer[521] = {0x00};
    int PayloadLen = 0;
    int PayloadCounter = 0;
    int pen_capacity;

    struct nxpwlc_device *nxpwlc_dev = container_of(work, struct nxpwlc_device, interrupt_work.work);

    mutex_lock(&nxpwlc_dev->irq_complete);
    pr_err("%s schedule enter\n", __func__);
    if (nxpwlc_dev->irq_suspend)
    {
        pr_err("%s system not resume.delay 200ms!\n", __func__);
        mdelay(200);
    }

    count = nxpwlc_transceive_read(nxpwlc_dev, Answer);

    // reset and charge enable
    if ((count == 4) && (Answer[0] == 0x80) && (Answer[1] == 0x02) && (Answer[2] == 0x01))
    {
        pr_err("%s : Reset Download\n", __func__);
    }
    if ((count == 5) && (Answer[0] == 0x80) && (Answer[1] == 0x03) && (Answer[2] == 0x00))
    {
        pr_err("%s : Normal Reset\n", __func__);
    }

    // charge enable event
    if ((count == 3) && (Answer[0] == 0x50) && (Answer[2] == 0x00))
    {
        pr_err("%s : charge enable response Status OK\n", __func__);
    }
    if ((count == 10) && (Answer[0] == 0x92) && (Answer[1] == 0x8) && (Answer[2] == 0x00))
    {
        pr_err("%s : charge enable response DEVICE_DETECTED\n", __func__);
    }
    else if (Answer[0] == 0x92 && (Answer[2] == 0x1))
    {
        pr_debug("%s : charge enable response DEVICE_DEACTIVATED\n", __func__);
    }
    else if (Answer[0] == 0x92 && (Answer[2] == 0x2))
    {
        // Move the pen away
        pr_err("%s : charge enable response DEVICE_LOST\n", __func__);
#ifdef ZTE_CONFIG_NXPWLC_FULL
        if (isFullBattery && pen_full >=90) {
            // nxpwlc_send_pen_event(nxpwlc_dev, TOUCH_PEN_OFF);
            pr_debug("%s : charge enable pen_full\n", __func__);
        } else {
            isFullBattery = false;
        }
#else
        nxpwlc_send_pen_event(nxpwlc_dev, TOUCH_PEN_OFF);
#endif
    }
    else if (Answer[0] == 0x92 && (Answer[2] == 0x3))
    {
        pr_err("%s : charge enable response DEVICE_VERSION_MISMATCH\n", __func__);
    }
    else if (Answer[0] == 0x92 && (Answer[2] == 0x4))
    {
        // pen is attached
        pr_err("%s : charge enable response DEVICE_DOCKED\n", __func__);
#ifdef ZTE_CONFIG_NXPWLC_FULL
        if (isFullBattery) {
            nxpwlc_send_pen_event(nxpwlc_dev, TOUCH_PEN_ON);
        }
#else
        nxpwlc_send_pen_event(nxpwlc_dev, TOUCH_PEN_ON);
#endif
    }
    else if (Answer[0] == 0x92 && (Answer[2] == 0x5))
    {
        pr_err("%s : charge enable response DEVICE_UNDOCKED\n", __func__);
    }
    else if (Answer[0] == 0x92 && (Answer[2] == 0x6))
    {
        pr_err("%s : charge enable response FO_PRESENT\n", __func__);
    }
    else if (Answer[0] == 0x92 && (Answer[2] == 0x7))
    {
        nxpwlc_dev->charging_status = POWER_SUPPLY_STATUS_FULL;
        nxpwlc_send_charging_status_event(nxpwlc_dev);
        pr_err("%s : charge enable response BATTERY_FULL\n", __func__);
    }
    else if (Answer[0] == 0x94 && (Answer[2] == 0x0))
    {
        nxpwlc_dev->charging_status = POWER_SUPPLY_STATUS_CHARGING;
        nxpwlc_send_charging_status_event(nxpwlc_dev);
        pr_err("%s : charge enable response Charging Started\n", __func__);
    }
    else if (Answer[0] == 0x94 && (Answer[2] == 0x1))
    {
        // Normal end of charging
        nxpwlc_dev->charging_status = POWER_SUPPLY_STATUS_DISCHARGING;
        nxpwlc_send_charging_status_event(nxpwlc_dev);
        pr_err("%s : charge enable response Charging Ended\n", __func__);
    }
    else if (Answer[0] == 0x94 && (Answer[2] == 0x2))
    {
        // Abnormal end of charging
        nxpwlc_dev->charging_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
        nxpwlc_send_charging_status_event(nxpwlc_dev);
        pr_err("%s : charge enable response Charging Stopped\n", __func__);
    }
    else if (Answer[0] == 0x95)
    {
        pr_err("%s : charge enable response Battery percentage: %d\n", __func__, Answer[2]);
        /* Started by AICoder, pid:le2aevf2ccl19cf144ed0a3540d9f70f4751a3d5 */
        pen_capacity = Answer[2];
        /* Ended by AICoder, pid:le2aevf2ccl19cf144ed0a3540d9f70f4751a3d5 */
        pr_err("%s : pen_capacity: %d\n", __func__, pen_capacity);
        if (pen_capacity > 100 || pen_capacity < 0)
        {
            nxpwlc_dev->battery_level = 100;
            pr_err("%s : pen_cap_level not normal,set cap to 100!\n", __func__);
        }
        else
        {
            nxpwlc_dev->battery_level = pen_capacity;
            nxpwlc_send_capacity_event(nxpwlc_dev);
            pen_full = pen_capacity;
            isFullBattery = true;
        }
        if (pen_capacity == 100)
        {
            nxpwlc_dev->charging_status = POWER_SUPPLY_STATUS_FULL;
            nxpwlc_send_charging_status_event(nxpwlc_dev);
            pr_err("%s : pen_cap_level response BATTERY_FULL\n", __func__);
        }
    }
    else if (Answer[0] == 0x9F)
    {
        PayloadLen = Answer[1];
        PayloadCounter = sizeof(Answer) - PayloadLen;
        do
        {
            pr_debug("%s: Optional NDEF: %d\n", __func__, Answer[PayloadCounter]);
            PayloadCounter++;
        } while (PayloadCounter < sizeof(Answer));
    }

    mutex_unlock(&nxpwlc_dev->irq_complete);
    pr_err("nxpwlc interrupt fun end\n");
    return;
}

void nxpwlc_reset(struct nxpwlc_device *nxpwlc_dev)
{
    pr_err("%s : nxp wlc reset\n", __func__);
    gpiod_set_value(nxpwlc_dev->gpiod_reset, 0);
    udelay(200);
    gpiod_set_value(nxpwlc_dev->gpiod_reset, 1);
    msleep(5);
}

static int nxpwlc_probe(struct i2c_client *client,
                        const struct i2c_device_id *id)
{
    int ret;
    struct nxpwlc_device *nxpwlc_dev;
    struct device *dev = &client->dev;
    pr_err("%s : enter \n", __func__);

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
    {
        pr_err("%s : need I2C_FUNC_I2C\n", __func__);
        return -ENODEV;
    }

    nxpwlc_dev = devm_kzalloc(dev, sizeof(*nxpwlc_dev), GFP_KERNEL);
    if (nxpwlc_dev == NULL)
        return -ENOMEM;
    nxpwlc_dev->client = client;

    nxpwlc_dev->gpiod_reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
    if (IS_ERR_OR_NULL(nxpwlc_dev->gpiod_reset))
    {
        pr_warn("%s : Unable to request reset-gpios\n", __func__);
        return -ENODEV;
    }

    gpiod_set_value(nxpwlc_dev->gpiod_reset, 0);

    nxpwlc_dev->gpiod_enable = devm_gpiod_get(dev, "enable", GPIOD_OUT_HIGH);
    if (IS_ERR_OR_NULL(nxpwlc_dev->gpiod_enable))
    {
        pr_warn("%s : Unable to request enable-gpios\n", __func__);
        return -ENODEV;
    }
    gpiod_set_value(nxpwlc_dev->gpiod_enable, 1);

    udelay(300);
    pr_err("%s : boost-gpios delay 300us\n", __func__);
    nxpwlc_dev->gpiod_boost = devm_gpiod_get(dev, "boost", GPIOD_OUT_HIGH);
    if (IS_ERR_OR_NULL(nxpwlc_dev->gpiod_boost))
    {
        pr_warn("%s : Unable to request boost-gpios\n", __func__);
        return -ENODEV;
    }
    gpiod_set_value(nxpwlc_dev->gpiod_boost, 1);

    nxpwlc_reset(nxpwlc_dev);

    nxpwlc_dev->irq_is_attached = false;

    nxpwlc_dev->gpiod_irq = devm_gpiod_get(dev, "irq", GPIOD_IN);
    if (IS_ERR_OR_NULL(nxpwlc_dev->gpiod_irq))
    {
        pr_err("%s : Unable to request irq-gpios\n", __func__);
        return -ENODEV;
    }

    client->irq = gpiod_to_irq(nxpwlc_dev->gpiod_irq);
    pr_debug("%s : client-irq =  %d\n", __func__, client->irq);

    init_waitqueue_head(&nxpwlc_dev->read_wq);
    spin_lock_init(&nxpwlc_dev->irq_enabled_lock);
    mutex_init(&nxpwlc_dev->suspend_mutex);
    mutex_init(&nxpwlc_dev->write_mutex);
    mutex_init(&nxpwlc_dev->irq_complete);

    nxpwlc_dev->nxpwlc_device.minor = MISC_DYNAMIC_MINOR;
    nxpwlc_dev->nxpwlc_device.name = "nxpwlc";
    nxpwlc_dev->nxpwlc_device.fops = &nxpwlc_dev_fops;
    nxpwlc_dev->nxpwlc_device.parent = dev;

    i2c_set_clientdata(client, nxpwlc_dev);

    ret = misc_register(&nxpwlc_dev->nxpwlc_device);
    if (ret)
    {
        pr_err("%s : misc_register failed\n", __func__);
        goto err_mutex_destroy;
    }

    /* Started by AICoder, pid:uaecdc315d2722e1427b09374015e402a1b4683a */
    nxpwlc_kobj = kobject_create_and_add("nxpwlc-fw", NULL);
    if (!nxpwlc_kobj)
        return -ENOMEM;
    /* Ended by AICoder, pid:uaecdc315d2722e1427b09374015e402a1b4683a */

    ret = sysfs_create_group(nxpwlc_kobj, &nxpwlc_attr_group);
    if (ret)
    {
        pr_err("%s : sysfs_create_group failed\n", __func__);
    }

    device_init_wakeup(&client->dev, true);
    nxpwlc_dev->irq_suspend = false;

    nxpwlc_dev->irq_wake_lock = wakeup_source_register(&client->dev, "nxpwlc_irq_wake_lock");
    if (!nxpwlc_dev->irq_wake_lock)
    {
        pr_err("%s : irq_wake_lock register failed\n", __func__);
        goto err_irq_clean;
    }

    nxpwlc_dev->charging_status = POWER_SUPPLY_STATUS_UNKNOWN;
    nxpwlc_dev->battery_level = 0;

    zte_misc_register_callback(&nxpwlc_charging_status_node, nxpwlc_dev);
    zte_misc_register_callback(&nxpwlc_capacity_node, nxpwlc_dev);

    INIT_DELAYED_WORK(&nxpwlc_dev->interrupt_work, nxpwlc_interrupt_workfunc);

    nxpwlc_dev->uevent_device = platform_device_alloc("nxpwlc_tx", -1);
    if (!nxpwlc_dev->uevent_device)
    {
        pr_err("%s failed to allocate platform device", __func__);
        goto err_wakeup_source_cleanup;
    }
    ret = platform_device_add(nxpwlc_dev->uevent_device);
    if (ret < 0)
    {
        pr_err("%s failed to add platform device ret=%d", __func__, ret);
        goto err_wakeup_source_cleanup;
    }
    if (client->irq)
    {
        if (nxpwlc_dev->irq_is_attached)
        {
            devm_free_irq(dev, client->irq, nxpwlc_dev);
            nxpwlc_dev->irq_is_attached = false;
        }

        pr_debug("%s : client-irq =  %d\n", __func__, client->irq);
        nxpwlc_dev->irq_enabled = true;
        ret = devm_request_threaded_irq(dev, client->irq, NULL,
                                        nxpwlc_dev_irq_handler,
                                        IRQF_TRIGGER_RISING | IRQF_ONESHOT | IRQF_NO_SUSPEND, client->name, nxpwlc_dev);
        if (ret < 0)
        {
            pr_err("%s : request irq=%d failed, ret=%d\n", __func__, client->irq, ret);
            goto err_nxp_misc_unregister;
        }
        enable_irq_wake(client->irq);
        pr_info("%s : request irq=%d success, ret=%d\n", __func__, client->irq, ret);

        nxpwlc_dev->irq_is_attached = true;
        nxpwlc_disable_irq(nxpwlc_dev);
    }

    pr_info("%s : nxpwlc probe complete\n", __func__);

    nxpwlc_reset(nxpwlc_dev);
    msleep(100);
    nxpwlc_transceive_read(nxpwlc_dev, data_buffer);

    ret = nxpwlc_send_reset_normal(nxpwlc_dev);
    msleep(100);
    nxpwlc_transceive_read(nxpwlc_dev, data_buffer);
    if (ret)
    {
        dev_err(&client->dev, "failed to send reset normal command\n");
        return ret;
    }

    msleep(500);

    ret = nxpwlc_send_charge_enable(nxpwlc_dev);
    if (ret)
    {
        pr_err("failed to send charge enable command\n");
        return ret;
    }
    nxpwlc_enable_irq(nxpwlc_dev);
    return 0;

err_nxp_misc_unregister:
    misc_deregister(&nxpwlc_dev->nxpwlc_device);

err_wakeup_source_cleanup:
    if (nxpwlc_dev->irq_wake_lock)
        wakeup_source_unregister(nxpwlc_dev->irq_wake_lock);

err_irq_clean:
    if (client->irq)
        devm_free_irq(dev, client->irq, nxpwlc_dev);
    i2c_set_clientdata(client, NULL);

err_mutex_destroy:
    mutex_destroy(&nxpwlc_dev->write_mutex);
    pr_err("nxpwlc probe failed\n");
    return 0;
}

static void nxpwlc_remove(struct i2c_client *client)
{
    struct nxpwlc_device *nxpwlc_dev = i2c_get_clientdata(client);
    struct device *dev = &client->dev;

    sysfs_remove_group(nxpwlc_kobj, &nxpwlc_attr_group);
    kobject_put(nxpwlc_kobj);
    misc_deregister(&nxpwlc_dev->nxpwlc_device);
    cancel_delayed_work_sync(&nxpwlc_dev->interrupt_work);
    mutex_destroy(&nxpwlc_dev->write_mutex);
    mutex_destroy(&nxpwlc_dev->suspend_mutex);
    mutex_destroy(&nxpwlc_dev->irq_complete);
    wakeup_source_unregister(nxpwlc_dev->irq_wake_lock);
    devm_free_irq(dev, client->irq, nxpwlc_dev);
}

static int nxpwlc_suspend(struct device *dev)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct nxpwlc_device *nxpwlc_dev = i2c_get_clientdata(client);
    // struct nxpwlc_device *nxpwlc_dev = dev_get_drvdata(dev);
    pr_notice("%s: enter!\n", __func__);
    mutex_lock(&nxpwlc_dev->suspend_mutex);
    enable_irq_wake(client->irq);
    nxpwlc_dev->irq_suspend = true;
    mutex_unlock(&nxpwlc_dev->suspend_mutex);
    return 0;
}
static int nxpwlc_resume(struct device *dev)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct nxpwlc_device *nxpwlc_dev = i2c_get_clientdata(client);
    // struct nxpwlc_device *nxpwlc_dev = dev_get_drvdata(dev);
    pr_notice("%s: enter!\n", __func__);
    mutex_lock(&nxpwlc_dev->suspend_mutex);
    disable_irq_wake(client->irq);
    nxpwlc_dev->irq_suspend = false;
    mutex_unlock(&nxpwlc_dev->suspend_mutex);
    return 0;
}

static const struct i2c_device_id nxpwlc_id[] = {{"nxpwlc", 0}, {}};
static const struct of_device_id nxpwlc_of_match[] = {
    {
        .compatible = "nxp,nxpwlc",
    },
    {}};
MODULE_DEVICE_TABLE(of, nxpwlc_of_match);

static const struct dev_pm_ops nxpwlc_pm_ops = {
    .resume = nxpwlc_resume,
    .suspend = nxpwlc_suspend,
};

static struct i2c_driver nxpwlc_driver = {
    .id_table = nxpwlc_id,
    .probe = nxpwlc_probe,
    .remove = nxpwlc_remove,
    .driver = {
        .owner = THIS_MODULE,
        .name = "nxpwlc",
        .of_match_table = nxpwlc_of_match,
        .probe_type = PROBE_PREFER_ASYNCHRONOUS,
        .pm = &nxpwlc_pm_ops, // Power management Operations of devices
    },
};

/* module load/unload record keeping */
static int __init nxpwlc_dev_init(void)
{
    pr_info("Loading nxpwlc driver\n");
    return i2c_add_driver(&nxpwlc_driver);
}
module_init(nxpwlc_dev_init);
static void __exit nxpwlc_dev_exit(void)
{
    pr_info("Unloading nxpwlc driver\n");
    i2c_del_driver(&nxpwlc_driver);
}
module_exit(nxpwlc_dev_exit);
MODULE_AUTHOR("00336624");
MODULE_DESCRIPTION("NFC nxp pen CTN73x charging chip driver");
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
