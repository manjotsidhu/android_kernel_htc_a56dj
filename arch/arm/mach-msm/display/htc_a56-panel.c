#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/i2c.h>
#include <asm/mach-types.h>
#include <mach/msm_memtypes.h>
#include <mach/board.h>
#include <mach/debug_display.h>
#include "../../../../drivers/video/msm/mdss/mdss_dsi.h"

#define PANEL_ID_A56_SEMI_TIANMA_HX8394D 1  //SEMI
#define PANEL_ID_A56_TIANMA_HX8394D 2  //MAIN SOURCE
#define PANEL_ID_A56_TIANMA_HX8394F 3  //MAIN SOURCE
#define PANEL_ID_A56_BITLAND_HX8394D 4  //SECOND SOURCE
#define PANEL_ID_A56_BITLAND_HX8394F 5  //SECOND SOURCE

/* HTC: dsi_power_data overwrite the role of dsi_drv_cm_data
   in mdss_dsi_ctrl_pdata structure */
struct dsi_power_data {
	uint32_t sysrev;          /* system revision info */
	struct regulator *vddio;  /* LCM IO 1.8V */
	struct regulator *vddpll; /* MIPI PLL 1.8V */
	struct regulator *vdda;   /* MIPI 1.2V */

	int lcmp5v;
	int lcmn5v;
	int lcm_bl_en;
};

#ifdef MODULE
extern struct module __this_module;
#define THIS_MODULE (&__this_module)
#else
#define THIS_MODULE ((struct module *)0)
#endif

#define I2C_DEV_INFO(addr) \
	{.dev_addr = addr >> 1, .client = NULL}

struct i2c_dev_info {
	uint8_t				dev_addr;
	struct i2c_client	*client;
};

static int tps65132_status = 0;

static ssize_t tps65132_status_show(struct class *class,
      struct class_attribute *attr, char *buffer)
{
		return scnprintf(buffer, PAGE_SIZE, "%d\n", tps65132_status);
}


static struct class_attribute  tps65132_attribute[] = {
    __ATTR(power_ic, S_IRUGO, tps65132_status_show, NULL),
    __ATTR_NULL,
};

static struct class tps65132_class = {
       .name           = "display",
       .class_attrs    = tps65132_attribute,
};


static inline int platform_write_i2c_block(struct i2c_adapter *i2c_bus
								, u8 page
								, u8 offset
								, u16 count
								, u8 *values
								)
{
	struct i2c_msg msg;
	u8 *buffer;
	int ret;

	buffer = kmalloc(count + 1, GFP_KERNEL);
	if (!buffer) {
		printk("%s:%d buffer allocation failed\n",__FUNCTION__,__LINE__);
		return -ENOMEM;
	}

	buffer[0] = offset;
	memmove(&buffer[1], values, count);

	msg.flags = 0;
	msg.addr = page >> 1;
	msg.buf = buffer;
	msg.len = count + 1;

	ret = i2c_transfer(i2c_bus, &msg, 1);

	kfree(buffer);

	if (ret != 1) {
		printk("%s:%d I2c write failed 0x%02x:0x%02x\n"
				,__FUNCTION__,__LINE__, page, offset);
		ret = -EIO;
	} else {
		ret = 0;
	}

	return ret;
}

/**************************** TPS65132 ****************************/
#define TPS65132_I2C_ADDRESS  0x7C
#define TPS65132_VPOS_ADDRESS 0x00
#define TPS65132_VNEG_ADDRESS 0x01

static struct i2c_adapter *tps65132_i2c_bus_adapter = NULL;

static struct i2c_dev_info tps65132_device_addresses[] = {
	I2C_DEV_INFO(TPS65132_I2C_ADDRESS)
};

static int tps65132_add_i2c(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	int idx;

	tps65132_i2c_bus_adapter = adapter;
	if (tps65132_i2c_bus_adapter == NULL) {
		PR_DISP_ERR("%s() failed to get i2c adapter\n", __func__);
		return ENODEV;
	}

	for (idx = 0; idx < ARRAY_SIZE(tps65132_device_addresses); idx++) {
		if(idx == 0)
			tps65132_device_addresses[idx].client = client;
		else {
			tps65132_device_addresses[idx].client = i2c_new_dummy(tps65132_i2c_bus_adapter,
											tps65132_device_addresses[idx].dev_addr);
			if (tps65132_device_addresses[idx].client == NULL){
				return ENODEV;
			}
		}
	}

	return 0;
}

static int __devinit tps65132_tx_i2c_probe(struct i2c_client *client,
					      const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	int ret;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		PR_DISP_ERR("%s: Failed to i2c_check_functionality \n", __func__);
		return -EIO;
	}

	if (!client->dev.of_node) {
		PR_DISP_ERR("%s: client->dev.of_node = NULL\n", __func__);
		return -ENOMEM;
	}

	ret = tps65132_add_i2c(client);

	if(ret < 0) {
		PR_DISP_ERR("%s: Failed to tps65132_add_i2c, ret=%d\n", __func__,ret);
		return ret;
	}

	tps65132_status = 1;
    class_register(&tps65132_class);

	return 0;
}

static const struct i2c_device_id tps65132_tx_id[] = {
	{"tps65132", 0}
};

static struct of_device_id tps65132_match_table[] = {
	{.compatible = "disp-tps-65132",}
};

static struct i2c_driver tps65132_tx_i2c_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "tps65132",
		.of_match_table = tps65132_match_table,
		},
	.id_table = tps65132_tx_id,
	.probe = tps65132_tx_i2c_probe,
	.command = NULL,
};

static int htc_a56_regulator_init(struct platform_device *pdev)
{
	int ret = 0;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct dsi_power_data *pwrdata = NULL;

	PR_DISP_INFO("%s\n", __func__);
	if (!pdev) {
		PR_DISP_ERR("%s: invalid input\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = platform_get_drvdata(pdev);
	if (!ctrl_pdata) {
		PR_DISP_ERR("%s: invalid driver data\n", __func__);
		return -EINVAL;
	}

	pwrdata = devm_kzalloc(&pdev->dev,
				sizeof(struct dsi_power_data), GFP_KERNEL);
	if (!pwrdata) {
		PR_DISP_ERR("%s: FAILED to alloc pwrdata\n", __func__);
		return -ENOMEM;
	}

	ctrl_pdata->dsi_pwrctrl_data = pwrdata;

	/* LCM IO 1.8V */
	pwrdata->vddio = devm_regulator_get(&pdev->dev, "vddio");
	if (IS_ERR(pwrdata->vddio)) {
		PR_DISP_ERR("%s: could not get vddio reg, rc=%ld\n",
			__func__, PTR_ERR(pwrdata->vddio));
		return PTR_ERR(pwrdata->vddio);
	}

	/* MIPI PLL 1.8V */
	pwrdata->vddpll = devm_regulator_get(&pdev->dev, "vddpll");
	if (IS_ERR(pwrdata->vddpll)) {
		PR_DISP_ERR("%s: could not get vddpll reg, rc=%ld\n",
			__func__, PTR_ERR(pwrdata->vddpll));
		return PTR_ERR(pwrdata->vddpll);
	}

	/* MIPI 1.2V */
	pwrdata->vdda = devm_regulator_get(&pdev->dev, "vdda");
	if (IS_ERR(pwrdata->vdda)) {
		PR_DISP_ERR("%s: could not get vdda vreg, rc=%ld\n",
			__func__, PTR_ERR(pwrdata->vdda));
		return PTR_ERR(pwrdata->vdda);
	}
	ret = regulator_set_voltage(pwrdata->vdda, 1200000,
		1200000);
	if (ret) {
		PR_DISP_ERR("%s: set voltage failed on vdda vreg, rc=%d\n",
			__func__, ret);
		return ret;
	}

	pwrdata->lcmp5v = of_get_named_gpio(pdev->dev.of_node,
						"htc,lcm_p5v-gpio", 0);
	pwrdata->lcmn5v = of_get_named_gpio(pdev->dev.of_node,
						"htc,lcm_n5v-gpio", 0);
	pwrdata->lcm_bl_en = of_get_named_gpio(pdev->dev.of_node,
						"htc,lcm_bl_en-gpio", 0);

	ret = i2c_add_driver(&tps65132_tx_i2c_driver);
	if (ret < 0) {
		PR_DISP_ERR("%s: FAILED to add i2c_add_driver for tps65132 ret=%x\n",
			__func__, ret);
	}

	return 0;
}

static int htc_a56_regulator_deinit(struct platform_device *pdev)
{
	class_unregister(&tps65132_class);
	/* devm_regulator() will automatically free regulators
	   while dev detach. */
	/* nothing */
	return 0;
}

int htc_a56_panel_reset(struct mdss_panel_data *pdata, int enable)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct dsi_power_data *pwrdata = NULL;

	if (pdata == NULL) {
		PR_DISP_ERR("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	if (!gpio_is_valid(ctrl_pdata->rst_gpio)) {
		PR_DISP_DEBUG("%s:%d, reset line not configured\n",
			   __func__, __LINE__);
		return -EINVAL;
	}

	pwrdata = ctrl_pdata->dsi_pwrctrl_data;

	if (!pwrdata) {
		PR_DISP_ERR("%s: pwrdata not initialized\n", __func__);
		return -EINVAL;
	}

	PR_DISP_DEBUG("%s: enable = %d\n", __func__, enable);

	if (enable) {
		if (pdata->panel_info.first_power_on == 1) {
			PR_DISP_INFO("reset already on in first time\n");
			return 0;
		}

		if ( pdata->panel_info.panel_id == PANEL_ID_A56_TIANMA_HX8394F ||
				pdata->panel_info.panel_id == PANEL_ID_A56_BITLAND_HX8394F ) {
			gpio_set_value((ctrl_pdata->rst_gpio), 0);
			usleep_range(1000,1500);
			gpio_set_value((ctrl_pdata->rst_gpio), 1);
			usleep_range(55000,55500);
		}else{
			gpio_set_value((ctrl_pdata->rst_gpio), 0);
			usleep_range(1000,1500);
			gpio_set_value((ctrl_pdata->rst_gpio), 1);
			usleep_range(120000,120500);
		}
		gpio_set_value(pwrdata->lcm_bl_en, 1);
	} else {
		/* wait for LM3697 stable */
		usleep_range(10000, 10050);

		gpio_set_value(pwrdata->lcm_bl_en, 0);
		gpio_set_value((ctrl_pdata->rst_gpio), 0);
	}

	return 0;
}

static int htc_a56_panel_power_on(struct mdss_panel_data *pdata, int enable)
{
	int ret;
	u8 avdd_level = 0x00;

	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct dsi_power_data *pwrdata = NULL;

	PR_DISP_INFO("%s: en=%d\n", __func__, enable);
	if (pdata == NULL) {
		PR_DISP_ERR("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);
	pwrdata = ctrl_pdata->dsi_pwrctrl_data;

	if (!pwrdata) {
		PR_DISP_ERR("%s: pwrdata not initialized\n", __func__);
		return -EINVAL;
	}

	if (enable) {
		if (pdata->panel_info.panel_id == PANEL_ID_A56_SEMI_TIANMA_HX8394D ||
				pdata->panel_info.panel_id == PANEL_ID_A56_TIANMA_HX8394D ||
				pdata->panel_info.panel_id == PANEL_ID_A56_TIANMA_HX8394F ||
				pdata->panel_info.panel_id == PANEL_ID_A56_BITLAND_HX8394D ||
				pdata->panel_info.panel_id == PANEL_ID_A56_BITLAND_HX8394F) {
			ret = regulator_set_optimum_mode(pwrdata->vddio, 100000);
			if (ret < 0) {
				PR_DISP_ERR("%s: vddio set opt mode failed.\n",
					__func__);
				return ret;
			}

			ret = regulator_set_optimum_mode(pwrdata->vddpll, 100000);
			if (ret < 0) {
				PR_DISP_ERR("%s: vddpll set opt mode failed.\n",
					__func__);
				return ret;
			}

			ret = regulator_set_optimum_mode(pwrdata->vdda, 100000);
			if (ret < 0) {
				PR_DISP_ERR("%s: vdda set opt mode failed.\n",
					__func__);
				return ret;
			}

			/* LCM IO 1.8V */
			ret = regulator_enable(pwrdata->vddio);
			if (ret) {
				PR_DISP_ERR("%s: Failed to enable regulator.\n",
					__func__);
				return ret;
			}

			/* MIPI PLL 1.8V */
			ret = regulator_enable(pwrdata->vddpll);
			if (ret) {
				PR_DISP_ERR("%s: Failed to enable regulator.\n",
					__func__);
				return ret;
			}

			/* MIPI 1.2V */
			ret = regulator_enable(pwrdata->vdda);
			if (ret) {
				PR_DISP_ERR("%s: Failed to enable regulator.\n",
					__func__);
				return ret;
			}

			usleep_range(1000,1500);
			gpio_set_value(pwrdata->lcmp5v, 1);
			avdd_level = 0x0F;
			platform_write_i2c_block(tps65132_i2c_bus_adapter,
					TPS65132_I2C_ADDRESS, TPS65132_VPOS_ADDRESS, 1, &avdd_level);
			platform_write_i2c_block(tps65132_i2c_bus_adapter,
					TPS65132_I2C_ADDRESS, TPS65132_VNEG_ADDRESS, 1, &avdd_level);
			usleep_range(10000,10500);
			gpio_set_value(pwrdata->lcmn5v, 1);
			usleep_range(10000,10500);
		}
	} else {
		if (pdata->panel_info.panel_id == PANEL_ID_A56_SEMI_TIANMA_HX8394D ||
				pdata->panel_info.panel_id == PANEL_ID_A56_TIANMA_HX8394D ||
				pdata->panel_info.panel_id == PANEL_ID_A56_TIANMA_HX8394F ||
				pdata->panel_info.panel_id == PANEL_ID_A56_BITLAND_HX8394D ||
				pdata->panel_info.panel_id == PANEL_ID_A56_BITLAND_HX8394F) {
			htc_a56_panel_reset(pdata, 0);

			/* MIPI 1.2V */
			ret = regulator_disable(pwrdata->vdda);
			if (ret) {
				PR_DISP_ERR("%s: Failed to disable regulator.\n",
					__func__);
				return ret;
			}

			/* MIPI PLL 1.8V */
			ret = regulator_disable(pwrdata->vddpll);
			if (ret) {
				PR_DISP_ERR("%s: Failed to disable regulator.\n",
					__func__);
				return ret;
			}
			usleep_range(5000,5500);

			gpio_set_value(pwrdata->lcmn5v, 0);
			usleep_range(10000,10500);
			gpio_set_value(pwrdata->lcmp5v, 0);
			usleep_range(10000,10500);

			/* LCM IO 1.8V */
			ret = regulator_disable(pwrdata->vddio);
			if (ret) {
				PR_DISP_ERR("%s: Failed to disable regulator.\n",
					__func__);
				return ret;
			}

			ret = regulator_set_optimum_mode(pwrdata->vddio, 100);
			if (ret < 0) {
				PR_DISP_ERR("%s: vdd_io_vreg set opt mode failed.\n",
					__func__);
				return ret;
			}

			ret = regulator_set_optimum_mode(pwrdata->vddpll, 100);
			if (ret < 0) {
				PR_DISP_ERR("%s: vdd_io_vreg set opt mode failed.\n",
					__func__);
				return ret;
			}

			ret = regulator_set_optimum_mode(pwrdata->vdda, 100);
			if (ret < 0) {
				PR_DISP_ERR("%s: vdda_vreg set opt mode failed.\n",
					__func__);
				return ret;
			}
		}
	}

	PR_DISP_INFO("%s: en=%d done\n", __func__, enable);


	return 0;
}

static struct mdss_dsi_pwrctrl dsi_pwrctrl = {
	.dsi_regulator_init = htc_a56_regulator_init,
	.dsi_regulator_deinit = htc_a56_regulator_deinit,
	.dsi_power_on = htc_a56_panel_power_on,
	.dsi_panel_reset = htc_a56_panel_reset,
};

static struct platform_device dsi_pwrctrl_device = {
	.name          = "mdss_dsi_pwrctrl",
	.id            = -1,
	.dev.platform_data = &dsi_pwrctrl,
};

int __init htc_8226_dsi_panel_power_register(void)
{
	PR_DISP_INFO("%s#%d\n", __func__, __LINE__);
	platform_device_register(&dsi_pwrctrl_device);
	return 0;
}
