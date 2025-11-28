#include "nt36xxx.h"
#include <linux/spi/spi.h>

#ifdef NVT_USB_DETECT_GLOBAL
#include <linux/power_supply.h>
extern bool NVT_USB_detect_flag;
#endif

extern int32_t zte_nvt_selftest_open(void);
extern int32_t nvt_ext_cmd_store(uint8_t u8Cmd, uint8_t u8subCmd1,uint8_t u8subCmd2);

#define SPI_NUM 4

extern int32_t nvt_ts_suspend(struct device *dev);
extern int32_t nvt_ts_resume(struct device *dev);

static int tpd_init_tpinfo(struct ztp_device *cdev)
{
	u8 retry = 0;
	int ret;

	if (atomic_read(&ts->suspended)) {
		NVT_ERR("tp in suspned");
		return -EIO;
	}

	while (retry++ < 5) {
		ret = nvt_get_fw_info();
		if (ret) {
			NVT_ERR("nvt_get_fw_info failed. (%d)\n", ret);
		} else {
			break;
		}
	}

	snprintf(cdev->ic_tpinfo.tp_name, sizeof(cdev->ic_tpinfo.tp_name), "Novatek");
	cdev->ic_tpinfo.chip_model_id = TS_CHIP_NOVATEK;

	cdev->ic_tpinfo.firmware_ver = ts->fw_ver;
	cdev->ic_tpinfo.spi_num = SPI_NUM;

	return 0;
}

int nvt_tp_suspend(void *nvt_data)
{
	struct nvt_ts_data *ts_data = (struct nvt_ts_data *)nvt_data;

	nvt_ts_suspend(&ts_data->client->dev);
	return 0;
}

int nvt_tp_resume(void *nvt_data)
{
	struct nvt_ts_data *ts_data = (struct nvt_ts_data *)nvt_data;

	nvt_ts_resume(&ts_data->client->dev);
	return 0;
}

static int tpd_test_cmd_show(struct ztp_device *cdev, char *buf)
{
	ssize_t num_read_chars = 0;
	int i_len = 0;

	NVT_LOG("%s:enter\n", __func__);
	i_len = snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d", 0, 16,
			37, 0);
	num_read_chars = i_len;
	return num_read_chars;
}

static int tpd_test_cmd_store(struct ztp_device *cdev)
{

	NVT_LOG("%s:enter\n", __func__);

	zte_nvt_selftest_open();

	return 0;
}

static int tpd_get_wakegesture(struct ztp_device *cdev)
{
	cdev->b_gesture_enable = ts->ztec.is_wakeup_gesture;

	return 0;
}

static int tpd_enable_wakegesture(struct ztp_device *cdev, int enable)
{
	ts->ztec.is_set_wakeup_in_suspend = enable;
	ts->ztec.is_wakeup_gesture = enable;

	return 0;
}

int zte_set_display_rotation(int mrotation)
{
	int level = ts->ztec.rotation_limit_level;
	int ret = 0;

	switch (mrotation) {
		case mRotatin_0:
			ret = nvt_ext_cmd_store(EDGE_REJECT_VERTICAL, 0x00, 0x00);
			break;
		case mRotatin_180:
			ret = nvt_ext_cmd_store(EDGE_REJECT_REVERSE_VERTICAL, 0x00, 0x00);
			break;
		case mRotatin_90:
			ret = nvt_ext_cmd_store(EDGE_REJECT_RIGHT_UP, 0x00, 0x00);
			if (level == rotation_limit_level_0) {
				ret = nvt_ext_cmd_store(NVT_EXT_CMD, NVT_EXT_CMD_EDGE_REJECT_LEVEL, EDGE_REJECT_LEVEL_0);
				NVT_LOG("success in rotation_limit_level_0");
			} else if (level == rotation_limit_level_1) {
				ret = nvt_ext_cmd_store(NVT_EXT_CMD, NVT_EXT_CMD_EDGE_REJECT_LEVEL, EDGE_REJECT_LEVEL_1);
				NVT_LOG("success in rotation_limit_level_1");
			} else if (level == rotation_limit_level_2) {
				ret = nvt_ext_cmd_store(NVT_EXT_CMD, NVT_EXT_CMD_EDGE_REJECT_LEVEL, EDGE_REJECT_LEVEL_2);
				NVT_LOG("success in rotation_limit_level_2");
			} else if (level == rotation_limit_level_3) {
				ret = nvt_ext_cmd_store(NVT_EXT_CMD, NVT_EXT_CMD_EDGE_REJECT_LEVEL, EDGE_REJECT_LEVEL_3);
				NVT_LOG("success in rotation_limit_level_3");
			} else {
				NVT_ERR("level is %d error!", level);
			}
			break;
		case mRotatin_270:
			ret = nvt_ext_cmd_store(EDGE_REJECT_LEFT_UP, 0x00, 0x00);
			if (level == rotation_limit_level_0) {
				ret = nvt_ext_cmd_store(NVT_EXT_CMD, NVT_EXT_CMD_EDGE_REJECT_LEVEL, EDGE_REJECT_LEVEL_0);
				NVT_LOG("success in rotation_limit_level_0");
			} else if (level == rotation_limit_level_1) {
				ret = nvt_ext_cmd_store(NVT_EXT_CMD, NVT_EXT_CMD_EDGE_REJECT_LEVEL, EDGE_REJECT_LEVEL_1);
				NVT_LOG("success in rotation_limit_level_1");
			} else if (level == rotation_limit_level_2) {
				ret = nvt_ext_cmd_store(NVT_EXT_CMD, NVT_EXT_CMD_EDGE_REJECT_LEVEL, EDGE_REJECT_LEVEL_2);
				NVT_LOG("success in rotation_limit_level_2");
			} else if (level == rotation_limit_level_3) {
				ret = nvt_ext_cmd_store(NVT_EXT_CMD, NVT_EXT_CMD_EDGE_REJECT_LEVEL, EDGE_REJECT_LEVEL_3);
				NVT_LOG("success in rotation_limit_level_3");
			} else {
				NVT_ERR("level is %d error!", level);
			}
			break;
		default:
			NVT_ERR("mrotation is %d error!", mrotation);
			break;
	}

	return 0;
}

static int tpd_set_rotation_limit_level(struct ztp_device *cdev, int level)
{
	int ret = 0;

	if (level > 3)
		level = 3;
	ts->ztec.rotation_limit_level = level;

	ret = zte_set_display_rotation(cdev->display_rotation);

	return 0;
}

static int tpd_get_rotation_limit_level(struct ztp_device *cdev)
{
	cdev->rotation_limit_level = ts->ztec.rotation_limit_level;

	return 0;
}

static int tpd_set_display_rotation(struct ztp_device *cdev, int mrotation)
{
	int ret = -1;

	if (atomic_read(&ts->suspended)) {
		NVT_ERR("%s:error, change set in suspend!", __func__);
		return 0;
	}
	cdev->display_rotation = mrotation;
	ts->ztec.display_rotation = mrotation;
	NVT_LOG("%s:display_rotation=%d", __func__, cdev->display_rotation);

	ret = zte_set_display_rotation(cdev->display_rotation);

	if (ret) {
		NVT_ERR("Write display rotation failed!");
	}
	return cdev->display_rotation;
}

static int tpd_set_tp_report_rate(struct ztp_device *cdev, int tp_report_rate_level)
{
	int ret = 0;

	if (tp_report_rate_level > 4)
		tp_report_rate_level = 4;
	ts->ztec.tp_report_rate = tp_report_rate_level;
	if (atomic_read(&ts->suspended)) {
		NVT_ERR("%s: error, change set in suspend!", __func__);
	} else {
		if(tp_report_rate_level == 0) {
			ret = nvt_ext_cmd_store(NVT_EXT_CMD, NVT_EXT_CMD_REPORT_RATE, REPORT_RATE_NORMAL);
			NVT_LOG("success in tp_report_rate_level_0");
		} else if(tp_report_rate_level == 1) {
			ret = nvt_ext_cmd_store(NVT_EXT_CMD, NVT_EXT_CMD_REPORT_RATE, REPORT_RATE_HIGH);
			NVT_LOG("success in tp_report_rate_level_1");
		}
		if (ret)
			NVT_ERR("set report rate mode failed!");
	}

	return 0;
}

static int tpd_get_tp_report_rate(struct ztp_device *cdev)
{
	cdev->tp_report_rate = ts->ztec.tp_report_rate;

	return 0;
}

static int tpd_set_sensibility_level(struct ztp_device *cdev, u8 tp_sensibility_level)
{
	int ret = 0;

	if (tp_sensibility_level > 4)
		tp_sensibility_level = 4;
	ts->ztec.sensibility_level = tp_sensibility_level;
	if (atomic_read(&ts->suspended)) {
		NVT_ERR("%s: error, change set in suspend!", __func__);
	} else {
		if(tp_sensibility_level == 0) {
			ret = nvt_ext_cmd_store(NVT_EXT_CMD, NVT_EXT_CMD_HOVER_LEVEL, HOVER_LEVEL_1);
			NVT_LOG("success in tp_sensibility_level_0");
		} else if(tp_sensibility_level == 1) {
			ret = nvt_ext_cmd_store(NVT_EXT_CMD, NVT_EXT_CMD_HOVER_LEVEL, HOVER_LEVEL_2);
			NVT_LOG("success in tp_sensibility_level_1");
		} else if(tp_sensibility_level == 2) {
			ret = nvt_ext_cmd_store(NVT_EXT_CMD, NVT_EXT_CMD_HOVER_LEVEL, HOVER_LEVEL_3);
			NVT_LOG("success in tp_sensibility_level_2");
		} else if(tp_sensibility_level == 3) {
			ret = nvt_ext_cmd_store(NVT_EXT_CMD, NVT_EXT_CMD_HOVER_LEVEL, HOVER_LEVEL_4);
			NVT_LOG("success in tp_sensibility_level_3");
		} else if(tp_sensibility_level == 4) {
			ret = nvt_ext_cmd_store(NVT_EXT_CMD, NVT_EXT_CMD_HOVER_LEVEL, HOVER_LEVEL_5);
			NVT_LOG("success in tp_sensibility_level_4");
		}
		if (ret)
			NVT_ERR("set sensibility_level mode failed!");
	}

	return 0;
}

static int tpd_get_sensibility_level(struct ztp_device *cdev)
{
	cdev->sensibility_level = ts->ztec.sensibility_level;

	return 0;
}

static int tpd_set_follow_hand_level(struct ztp_device *cdev, int tp_follow_hand_level)
{
	int ret = 0;

	if (tp_follow_hand_level > 4)
		tp_follow_hand_level = 4;
	ts->ztec.follow_hand_level = tp_follow_hand_level;
	if (atomic_read(&ts->suspended)) {
		NVT_ERR("%s: error, change set in suspend!", __func__);
	} else {
		if(tp_follow_hand_level == 0) {
			ret = nvt_ext_cmd_store(NVT_EXT_CMD, NVT_EXT_CMD_FILTER_LEVEL, FILTER_LEVEL_1);
			NVT_LOG("success in tp_follow_hand_level_0");
		} else if(tp_follow_hand_level == 1) {
			ret = nvt_ext_cmd_store(NVT_EXT_CMD, NVT_EXT_CMD_FILTER_LEVEL, FILTER_LEVEL_2);
			NVT_LOG("success in tp_follow_hand_level_1");
		} else if(tp_follow_hand_level == 2) {
			ret = nvt_ext_cmd_store(NVT_EXT_CMD, NVT_EXT_CMD_FILTER_LEVEL, FILTER_LEVEL_3);
			NVT_LOG("success in tp_follow_hand_level_2");
		} else if(tp_follow_hand_level == 3) {
			ret = nvt_ext_cmd_store(NVT_EXT_CMD, NVT_EXT_CMD_FILTER_LEVEL, FILTER_LEVEL_4);
			NVT_LOG("success in tp_follow_hand_level_3");
		} else if(tp_follow_hand_level == 4) {
			ret = nvt_ext_cmd_store(NVT_EXT_CMD, NVT_EXT_CMD_FILTER_LEVEL, FILTER_LEVEL_5);
			NVT_LOG("success in tp_follow_hand_level_4");
		}
		if (ret)
			NVT_ERR("set follow_hand_level mode failed!");
	}

	return 0;
}

static int tpd_get_follow_hand_level(struct ztp_device *cdev)
{
	cdev->follow_hand_level = ts->ztec.follow_hand_level;

	return 0;
}

static int tpd_set_stability_level(struct ztp_device *cdev, int tp_stability_level)
{
	int ret = 0;

	if (tp_stability_level > 4)
		tp_stability_level = 4;
	ts->ztec.stability_level = tp_stability_level;
	if (atomic_read(&ts->suspended)) {
		NVT_ERR("%s: error, change set in suspend!", __func__);
	} else {
/* Started by AICoder, pid:w4e17ne8f7l7151149010897a029742b37d02d6c */
		if(tp_stability_level == 0) {
    		ret = nvt_ext_cmd_store(NVT_EXT_CMD, NVT_EXT_CMD_JITTER_LEVEL, JITTER_LEVEL_1);
    		NVT_LOG("success in tp_stability_level_0");
		} else if(tp_stability_level == 1) {
    		ret = nvt_ext_cmd_store(NVT_EXT_CMD, NVT_EXT_CMD_JITTER_LEVEL, JITTER_LEVEL_2);
    		NVT_LOG("success in tp_stability_level_1");
		} else if(tp_stability_level == 2) {
    		ret = nvt_ext_cmd_store(NVT_EXT_CMD, NVT_EXT_CMD_JITTER_LEVEL, JITTER_LEVEL_3);
    		NVT_LOG("success in tp_stability_level_2");
		} else if(tp_stability_level == 3) {
    		ret = nvt_ext_cmd_store(NVT_EXT_CMD, NVT_EXT_CMD_JITTER_LEVEL, JITTER_LEVEL_4);
    		NVT_LOG("success in tp_stability_level_3");
		} else if(tp_stability_level == 4) {
    		ret = nvt_ext_cmd_store(NVT_EXT_CMD, NVT_EXT_CMD_JITTER_LEVEL, JITTER_LEVEL_5);
    		NVT_LOG("success in tp_stability_level_4");
		}
/* Ended by AICoder, pid:w4e17ne8f7l7151149010897a029742b37d02d6c */
	}

	return 0;
}

static int tpd_get_stability_level(struct ztp_device *cdev)
{
	cdev->stability_level = ts->ztec.stability_level;

	return 0;
}

static int tpd_set_play_game(struct ztp_device *cdev, int enable)
{
	int ret;

	if (atomic_read(&ts->suspended)) {
		/* we can not play game in black screen */
		ts->ztec.is_play_game = enable;
		NVT_ERR("%s: error, change set in suspend!", __func__);
	} else {
		if (ts->ztec.is_play_game == enable) {
			NVT_LOG("play no need reset");
			return 0;
		} else {
			ts->ztec.is_play_game = enable;
			if(enable == 0)
				ret = nvt_ext_cmd_store(CMD_GAME_MODE, GAME_MODE_DISABLE, 0x00);
			else if(enable == 1)
				ret = nvt_ext_cmd_store(CMD_GAME_MODE, GAME_MODE_ENABLE, 0x00);
			NVT_LOG("play_game set %d success\n", enable);
		}
	}

	return 0;
}

static int tpd_get_play_game(struct ztp_device *cdev)
{
	cdev->play_game_enable = ts->ztec.is_play_game;

	return 0;
}

#ifdef NVT_USB_DETECT_GLOBAL
/* Started by AICoder, pid:7cd4d161ffw5063148580bdf504c662cb0964a25 */
int32_t nvt_ac_power_plug_set(uint8_t status) {
    int ret = 0;

    if(status == 0) {
        ret = nvt_ext_cmd_store(AC_POWER_PLUG_DISABLE, 0x00, 0x00);
        NVT_LOG("success disable charger\n");
    } else if(status == 1) {
        ret = nvt_ext_cmd_store(AC_POWER_PLUG_ENABLE, 0x00, 0x00);
        NVT_LOG("success enable charger\n");
    }

    return ret;
}
/* Ended by AICoder, pid:7cd4d161ffw5063148580bdf504c662cb0964a25 */

static bool nvt_get_charger_status(void)
{
	static struct power_supply *batt_psy;
	union power_supply_propval val = { 0, };
	bool status = false;

	if (batt_psy == NULL)
		batt_psy = power_supply_get_by_name("battery");
	if (batt_psy) {
		batt_psy->desc->get_property(batt_psy, POWER_SUPPLY_PROP_STATUS, &val);
	}
	if ((val.intval == POWER_SUPPLY_STATUS_CHARGING) ||
		(val.intval == POWER_SUPPLY_STATUS_FULL)) {
		status = true;
	} else {
		status = false;
	}
	NVT_LOG("charger status:%d", status);
	return status;
}

static void nvt_work_charger_detect_work(struct work_struct *work)
{
	int ret = -EINVAL;
	struct delayed_work *charger_work_delay = container_of(work, struct delayed_work, work);
	struct nvt_ts_data *ts = container_of(charger_work_delay, struct nvt_ts_data, charger_work);
	static int status = 0;

	NVT_LOG("into charger detect");
	NVT_USB_detect_flag = nvt_get_charger_status();
	if (NVT_USB_detect_flag && !atomic_read(&ts->suspended) && !status) {
		status = 1;
		ret = nvt_ac_power_plug_set(status);
	} else if (!NVT_USB_detect_flag && !atomic_read(&ts->suspended) && status) {
		status = 0;
		ret = nvt_ac_power_plug_set(status);
	} else if (!NVT_USB_detect_flag && atomic_read(&ts->suspended) && status) {
		status = 0;
	} else if (NVT_USB_detect_flag && atomic_read(&ts->suspended) && !status) {
		status = 1;
	}

}

static int nvt_charger_notify_call(struct notifier_block *nb, unsigned long event, void *data)
{
	struct power_supply *psy = data;

	if (event != PSY_EVENT_PROP_CHANGED) {
		return NOTIFY_DONE;
	}

	if ((strcmp(psy->desc->name, "usb") == 0)
	    || (strcmp(psy->desc->name, "ac") == 0)) {
		queue_delayed_work(ts->charger_wq, &ts->charger_work, msecs_to_jiffies(500));
	}

	return NOTIFY_DONE;
}

static int nvt_init_charger_notifier(void)
{
	int ret = 0;

	NVT_LOG("Init Charger notifier");

	ts->charger_notifier.notifier_call = nvt_charger_notify_call;
	ret = power_supply_reg_notifier(&ts->charger_notifier);
	return ret;
}
#endif/*NVT_USB_DETECT_GLOBAL*/

void tpd_register_fw_class(struct nvt_ts_data *ts)
{
	NVT_LOG("%s: entry", __func__);

#ifdef NVT_USB_DETECT_GLOBAL
	ts->charger_wq = create_singlethread_workqueue("nvt_charger_detect");
	if (!ts->charger_wq) {
		NVT_ERR("allocate charger_wq failed");
	} else  {
		NVT_USB_detect_flag = nvt_get_charger_status();
		INIT_DELAYED_WORK(&ts->charger_work, nvt_work_charger_detect_work);
		nvt_init_charger_notifier();
	}
#endif

	tpd_cdev->get_tpinfo = tpd_init_tpinfo;

	tpd_cdev->tp_data = ts;
	tpd_cdev->tp_resume_func = nvt_tp_resume;
	tpd_cdev->tp_suspend_func = nvt_tp_suspend;

	tpd_cdev->tp_self_test = tpd_test_cmd_store;
	tpd_cdev->get_tp_self_test_result = tpd_test_cmd_show;

	tpd_cdev->get_gesture = tpd_get_wakegesture;
	tpd_cdev->wake_gesture = tpd_enable_wakegesture;

	tpd_cdev->get_tp_report_rate = tpd_get_tp_report_rate;
	tpd_cdev->set_tp_report_rate = tpd_set_tp_report_rate;

	tpd_cdev->get_sensibility = tpd_get_sensibility_level;
	tpd_cdev->set_sensibility = tpd_set_sensibility_level;

	tpd_cdev->get_follow_hand_level = tpd_get_follow_hand_level;
	tpd_cdev->set_follow_hand_level = tpd_set_follow_hand_level;

	tpd_cdev->get_stability_level = tpd_get_stability_level;
	tpd_cdev->set_stability_level = tpd_set_stability_level;

	tpd_cdev->get_rotation_limit_level = tpd_get_rotation_limit_level;
	tpd_cdev->set_rotation_limit_level = tpd_set_rotation_limit_level;

	tpd_cdev->set_display_rotation = tpd_set_display_rotation;

	tpd_cdev->get_play_game = tpd_get_play_game;
	tpd_cdev->set_play_game = tpd_set_play_game;

	/* Started by AICoder, pid:o258d3d703f277d144880b0740f6901dab405dda */
	tpd_cdev->max_x = TOUCH_MAX_WIDTH;
	tpd_cdev->max_y = TOUCH_MAX_HEIGHT;
	/* Ended by AICoder, pid:o258d3d703f277d144880b0740f6901dab405dda */

	NVT_LOG("%s: end", __func__);
}
