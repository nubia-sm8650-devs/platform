#include "zte_disp_i2c.h"

struct i2c_client *i2c_lp8556_client = NULL;
struct i2c_client *i2c_aw37504_client = NULL;

bool tilp8556_probe = false;
bool aw37504_probe = false;

int zte_disp_read_reg(struct i2c_client *client, u8 reg, u8 *data)
{
	struct i2c_msg msgs[2];
	int ret;
	u8 retries = 0;

	msgs[0].flags = !I2C_M_RD;
	msgs[0].addr  = client->addr;
	msgs[0].len   = 1;
	msgs[0].buf   = &reg;

	msgs[1].flags = I2C_M_RD;
	msgs[1].addr  = client->addr;
	msgs[1].len   = 1;
	msgs[1].buf   = data;

	while (retries < 3) {
		ret = i2c_transfer(client->adapter, msgs, 2);
		if (ret == 2)
			break;
		retries++;
		msleep_interruptible(5);
	}
	pr_info("[MSM_LCD] lp8556 read reg retries=%d,ret=%d\n", retries, ret);
	if (ret != 2) {
		pr_err("lp8556 read transfer error\n");
		ret = -1;
	}

	return ret;
}

int zte_disp_write_reg(struct i2c_client *client, u8 *buf, int len)
{
	int err;
	int tries = 0;

	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = len + 1,
			.buf = buf,
		},
	};

	do {
		err = i2c_transfer(client->adapter, msgs, 1);
		if (err != 1)
			msleep_interruptible(5);
	} while ((err != 1) && (++tries < 3));

	pr_info("[MSM_LCD] lp8556 write reg tries=%d,ret=%d\n", tries, err);
	if (err != 1) {
		pr_err("lp8556 write transfer error\n");
		err = -1;
	}

	return err;
}

int lp8556_read(u16 offset)
{
	u8 data = 0x0;
	if(zte_disp_read_reg(i2c_lp8556_client, offset, &data)) {
		pr_info("[MSM_LCD] lp8556 read offset = %x, data = %x\n", offset, data);
	} else {
		pr_info("[MSM_LCD] lp8556 read fail\n");
	}
	return data;
}

int AW37504_read(u16 offset)
{
	u8 data = 0x0;
	if(zte_disp_read_reg(i2c_aw37504_client, offset, &data)) {
		pr_info("[MSM_LCD] aw37504 read offset = %x, data = %x\n", offset, data);
	} else {
		pr_info("[MSM_LCD] aw37504 read fail\n");
	}
	return data;
}

void zte_disp_set_cmds(int offset, int data, int i2c_type)
{
	u8 buf[2] = {offset, data};

	switch (i2c_type)
	{
	case lp8556_device:
		if (tilp8556_probe) {
			pr_info("[MSM_LCD] lp8556 write reg offset = %x, data = %x\n", buf[0], buf[1]);
			zte_disp_write_reg(i2c_lp8556_client, buf, 1);
                        /* Started by AICoder, pid:w3d25nc613h14ee146c40b63c0c3a80c75569e5c */
                        usleep_range(1000, 1100);    // Delay for 1000us to 1100us
                        /* Ended by AICoder, pid:w3d25nc613h14ee146c40b63c0c3a80c75569e5c */
		}
		break;
	case AW37504_device:
		if (aw37504_probe) {
			pr_info("[MSM_LCD] aw37504 write reg offset = %x, data = %x\n", buf[0], buf[1]);
			zte_disp_write_reg(i2c_aw37504_client, buf, 1);
                        /* Started by AICoder, pid:w3d25nc613h14ee146c40b63c0c3a80c75569e5c */
                        usleep_range(1000, 1100);    // Delay for 1000us to 1100us
                        /* Ended by AICoder, pid:w3d25nc613h14ee146c40b63c0c3a80c75569e5c */
		}
		break;
	default:
		break;
	}
}

static int disp_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("tilp8556_probe,client not i2c capable\n");
		return -EIO;
	}

	switch (id->driver_data)
	{
	case lp8556_device:
		i2c_lp8556_client = client;
		tilp8556_probe = true;
		pr_info("[MSM_LCD] tilp8556_probe ok\n");
		break;
	case AW37504_device:
		i2c_aw37504_client = client;
		aw37504_probe = true;
		pr_info("[MSM_LCD] aw37504_probe ok\n");
		break;
	default:
		break;
	}
	return 0;
}

static void disp_remove(struct i2c_client *client)
{
	kfree(i2c_get_clientdata(client));
	return;
}

static const struct i2c_device_id disp_i2c_id_table[] = {
	{"lp8556",  lp8556_device},
	{"aw37504", AW37504_device},
};

static const struct of_device_id disp_of_id_table[] = {
	{.compatible = "ti,lp8556"},
	{.compatible = "awinic,aw37504"},
	{ },
};

static struct i2c_driver disp_i2c_driver = {
	.probe = disp_probe,
	.remove = disp_remove,
	.id_table = disp_i2c_id_table,
	.driver = {
		.name = "zte_disp_i2c",
		.owner = THIS_MODULE,
		.of_match_table = disp_of_id_table,
	},
};

int disp_i2c_driver_init(void)
{
	return i2c_add_driver(&disp_i2c_driver);
}
EXPORT_SYMBOL(disp_i2c_driver_init);

void disp_i2c_driver_exit(void)
{
	i2c_del_driver(&disp_i2c_driver);
}
EXPORT_SYMBOL(disp_i2c_driver_exit);

