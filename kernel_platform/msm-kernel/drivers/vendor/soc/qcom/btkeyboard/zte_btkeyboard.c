#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/jiffies.h>
//Reset pad,BT upper uses 11 seconds to open bt
#define W4_BTSOC_AUTO_POWERON_TO        11
#define POWER_ON                        1
#define POWER_OFF                       0
/*
Schedule the work of delay_poweron  every 10s to get btpower gpio_btpower and pogo gpio93
#define W4_BTSOC_AND_POGO_GPIO_TO       10
*/
/*
 * Platform data for the btkeyboard driver.
 */
struct btkeyboard_platform_data {
    struct platform_device *pdev;
    int device_power;
    int btkb_power;
    int btkb_dock;
    int power_state;
    int egpio_state;
 };

static struct btkeyboard_platform_data *btkeyboard_pdata;

static int get_gpio(struct platform_device *pdev, char *node) {
  int gpio = -1;
  gpio = of_get_named_gpio(pdev->dev.of_node, node, 0);
  pr_info("%s: btkeyboard get gpio %d\n", node, gpio);
  if (!gpio_is_valid(gpio))
      pr_err("%s: btkeyboard invalid gpio %d\n", node, gpio);
  return gpio;
}

/*for btkeyboard debug as soon as possibile, set it to 0 while driver debug finished*/
struct delayed_work poweron_work;

int btkeyboard_power_on(int on_or_off)
{
    // struct device_node *gpio_key_node = NULL;
    // struct device_node *child_node = NULL;
    int ret = 0;
    int on = (on_or_off > 0 ? POWER_ON : POWER_OFF);
    pr_info("%s: power on or off ? %d\n", __func__, on);
    if (on == btkeyboard_pdata->power_state) {
        pr_info("%s: alreay expected state, return\n", __func__);
        return 0;
    }
    if (NULL == btkeyboard_pdata) {
        return -EINVAL;
    }
    if (on) {
        // gpio_dock = of_get_named_gpio(of_find_node_by_path("/soc/gpio_keys_zte/key_dock"), "gpios", 0);
        /*******another way to find device node.******************
	gpio_key_node = of_find_compatible_node(NULL, NULL, "gpio-keys");
	if (gpio_key_node) {
		for_each_available_child_of_node(gpio_key_node, child_node) {
			if (!strcmp(child_node->name, "key_dock")) {
				gpio_dock = of_get_named_gpio(child_node, "gpios", 0);
                        	break;
			}
                }
        }
        pr_info("%s, gpio_dock is No.%d\n", __func__, gpio_dock);
        */
        //when dock on(gpio0 is low,ret is 0)
        ret = gpio_get_value(btkeyboard_pdata->btkb_dock);
        pr_info("%s: zte keyboard plugged gpio dock status %d\n", __func__, !ret);
        if (ret) {
            pr_info("%s: zte keyboard not plugged, return", __func__);
            return -ENXIO;
        }
        // gpio_set_value_cansleep(btkeyboard_pdata->device_power, 1);
        if (!btkeyboard_pdata->egpio_state) {
          ret = gpio_direction_output(btkeyboard_pdata->device_power, POWER_ON);
          if (ret) {
            btkeyboard_pdata->egpio_state = 0;
            pr_err("%s: gpio 1 power on failed %d.", __func__, ret);
            return ret;
          } else {
            btkeyboard_pdata->egpio_state = 1;
          }
        }
        ret = gpio_direction_output(get_gpio(btkeyboard_pdata->pdev, "bt-kb-power"), POWER_ON);
        if (ret) {
          pr_err("%s: gpio 62 power on failed %d.", __func__, ret);
          return ret;
        }
        btkeyboard_pdata->power_state = POWER_ON;
    } else {
        ret = gpio_direction_output(get_gpio(btkeyboard_pdata->pdev, "bt-kb-power"), POWER_OFF);
        if (ret) {
          pr_err("%s: gpio 62 power off failed %d.", __func__, ret);
          return ret;
        }
        btkeyboard_pdata->power_state = POWER_OFF;
    }
    pr_info("%s: power_state =%d \n", __func__,btkeyboard_pdata->power_state);
    return 0;
}
EXPORT_SYMBOL(btkeyboard_power_on);

static void delay_poweron(struct work_struct *work)
{
    int ret = 0;
    int gpio_btpower = -1;
    int status = 0;
    pr_info("%s: btkeyboard after bt on ", __func__);
    gpio_btpower = of_get_named_gpio(of_find_node_by_path("/soc/bluetooth/bt_kiwi"), "qcom,bt-reset-gpio", 0);
    ret = gpio_is_valid(gpio_btpower);
    if (ret) {
        ret = gpio_get_value(gpio_btpower);
        pr_info("%s: btkeyboard btpower gpio_btpower status: %d\n", __func__, ret);
        if (ret != 0) {
            status = btkeyboard_power_on(POWER_ON);
            if (status < 0) {
                pr_info("%s: btkeyboard_power_on failed,status: %d\n", __func__, status);
                return;
            }
        }
    } else {
        pr_err("%s: invalid btpower gpio_btpower %d\n", __func__, gpio_btpower);
    }
 }

static ssize_t power_on_store(struct device *dev, struct device_attribute *attr, const char *buff, size_t size)
{
    unsigned int on = 0;
    int status = 0;
    sscanf(buff, "%u", &on);
    pr_info("%s: sleep 150ms, btkeyboard get on = %u \n", __func__,on);
    msleep(150);
    status = btkeyboard_power_on(on);
    if (status < 0) {
        return status;
    }
    return strnlen(buff, size);
}

static ssize_t power_on_show(struct device *dev, struct device_attribute *attr, char *buff)
{
    if (NULL == btkeyboard_pdata) {
        return -EINVAL;
    }
    return sprintf(buff, "%d\n", btkeyboard_pdata->power_state);
}

static DEVICE_ATTR(power_on, S_IRUGO | S_IWUSR, power_on_show, power_on_store);

static struct attribute  *btkeyboard_attrs[] = {
        &dev_attr_power_on.attr,
        NULL,
};

static struct attribute_group btkeyboard_attr_grp = {
        .name = "zte-btkeyboard",
        .attrs = btkeyboard_attrs
};

static int btkeyboard_populate_dt_pinfo(struct platform_device *pdev)
{
    pr_info("%s   \n", __func__);
    if (!btkeyboard_pdata)
        return -ENOMEM;
    if (pdev->dev.of_node) {
        btkeyboard_pdata->device_power = get_gpio(pdev, "bt-kb-gpio");
        btkeyboard_pdata->btkb_power = get_gpio(pdev, "bt-kb-power");
        btkeyboard_pdata->btkb_dock = get_gpio(pdev, "bt-kb-dock");
    } else {
        pr_err("%s: dev.of_node is null\n",__func__);
        return -EINVAL;
    }
    return 0;
}

 static const struct of_device_id zte_btkeyboard_of_match[] = {
   { .compatible = "zte-btkeyboard", },
   {},
};

static int zte_keyboard_egpio_pinctrl(struct device *dev) {
    struct pinctrl *pinctrl;
    struct pinctrl_state *gpio_state_init;

    pinctrl = devm_pinctrl_get(dev);
    if (!pinctrl) {
        pr_err("Can`t find pinctrl");
        return -1;
    }
    gpio_state_init = pinctrl_lookup_state(pinctrl, "zte_nfc_keyboard");
    if (!gpio_state_init) {
        pr_err("Can`t find pinctrl state");
        return -1;
    }
    pinctrl_select_state(pinctrl, gpio_state_init);

    return 0;
}

MODULE_DEVICE_TABLE(of, zte_btkeyboard_of_match);

static int zte_btkeyboard_probe(struct platform_device *pdev)
{
    int ret = 0;
    pr_info("%s   enter...\n", __func__);
    btkeyboard_pdata = kzalloc(sizeof(*btkeyboard_pdata), GFP_KERNEL);
    if (!btkeyboard_pdata)
        return -ENOMEM;
    btkeyboard_pdata->pdev = pdev;
    btkeyboard_pdata->power_state = 0;
    ret = zte_keyboard_egpio_pinctrl(&pdev->dev);
    if (ret < 0) {
        pr_err("%s: failed to set pinctrl state", __func__);
    }
    ret = btkeyboard_populate_dt_pinfo(pdev);
    if(ret < 0) {
        pr_err("%s:failed to populate device tree info: ret = %d\n", __func__, ret);
        goto out;
    }
    pdev->dev.platform_data = btkeyboard_pdata;
    ret = sysfs_create_group(&pdev->dev.kobj, &btkeyboard_attr_grp);
    if (ret) {
        pr_err("%s:failed to create the btkeyboard attr group；%d\n", __func__, ret);
        goto out;
    }
    ret = gpio_direction_output(btkeyboard_pdata->device_power, POWER_ON);
    if (ret) {
        btkeyboard_pdata->egpio_state = 0;
        pr_err("%s: gpio 1 power on failed %d.", __func__, ret);
    } else {
        btkeyboard_pdata->egpio_state = 1;
    }
    INIT_DELAYED_WORK(&poweron_work, delay_poweron);
    schedule_delayed_work(&poweron_work, msecs_to_jiffies(W4_BTSOC_AUTO_POWERON_TO*1000));
    pr_info("%s   exit\n", __func__);
    return 0;
out:
    kfree((void *)btkeyboard_pdata);
    btkeyboard_pdata = NULL;
    return ret;
}

static int  zte_btkeyboard_remove(struct platform_device *pdev)
{
    pr_info("%s   \n", __func__);
    if (NULL != btkeyboard_pdata)
        kfree((void *)btkeyboard_pdata);
    return 0;
}

static struct platform_driver zte_btkeyboard_driver = {
        .probe          = zte_btkeyboard_probe,
        .remove         = zte_btkeyboard_remove,
        .driver         = {
                .name   = "zte-btkeyboard",
                .owner  = THIS_MODULE,
                .of_match_table = zte_btkeyboard_of_match,
        }
};

static int __init zte_btkeyboard_init (void)
{
    pr_info("%s:   enter!!\n", __func__);
    return platform_driver_register(&zte_btkeyboard_driver);
}

static void __exit zte_btkeyboard_exit(void)
{
    pr_info("%s:   enter!!\n", __func__);
    return platform_driver_unregister(&zte_btkeyboard_driver);
}

late_initcall(zte_btkeyboard_init);
module_exit(zte_btkeyboard_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ZTE light Inc.");
