#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/string.h>

enum {
  	lp8556_device,
	AW37504_device,
};

int disp_i2c_driver_init(void);
void disp_i2c_driver_exit(void);
int lp8556_read(u16 offset);
int AW37504_read(u16 offset);
void zte_disp_set_cmds(int offset, int data, int i2c_type);