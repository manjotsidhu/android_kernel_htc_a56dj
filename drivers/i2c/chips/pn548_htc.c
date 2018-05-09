/******************************************************************************
 *
 *  This is the implementation file for the PN548 NFC customization Functions
 *
 ******************************************************************************/

#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/types.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include "pn548_htc.h"

#if NFC_READ_RFSKUID
#define HAS_NFC_CHIP 0x7000000
#endif //NFC_READ_RFSKUID


#if NFC_GET_BOOTMODE
#include <mach/devices_cmdline.h>
#endif //NFC_GET_BOOTMODE



#define D(x...)	\
	if (is_debug) \
		printk(KERN_DEBUG "[NFC] " x)
#define I(x...) printk(KERN_INFO "[NFC] " x)
#define E(x...) printk(KERN_ERR "[NFC] [Err] " x)


// for off mode charging ++
#if NFC_OFF_MODE_CHARGING_LOAD_SWITCH
static unsigned int   pvdd_gpio;
#endif //NFC_OFF_MODE_CHARGING_LOAD_SWITCH
// for off mode charging --

static  struct regulator *regulator;
static   unsigned int NFC_I2C_SCL;
static   unsigned int NFC_I2C_SDA;
static   unsigned int NFC_PVDD_GPIO;
int is_regulator = 0;
int is_pvdd_gpio = 0;
const char *regulator_name;

/******************************************************************************
 *
 *  Function pn548_htc_check_rfskuid:
 *  Return With(1)/Without(0) NFC chip if this SKU can get RFSKUID in kernal
 *  Return is_alive(original value) by default.
 *
 ******************************************************************************/
int pn548_htc_check_rfskuid(int in_is_alive){
#if NFC_READ_RFSKUID
        int nfc_rfbandid_size = 0;
        int i;
        unsigned int *nfc_rfbandid_info;
        struct device_node *nfc_rfbandid;
        nfc_rfbandid = of_find_node_by_path("/chosen/mfg");
        if (nfc_rfbandid){
                nfc_rfbandid_info = (unsigned int *) of_get_property(nfc_rfbandid,"skuid.rf_id",&nfc_rfbandid_size);
                if (!nfc_rfbandid_info){
                        E("%s:Get null pointer of rfbandid\n",__func__);
                        return 1;
                }
        }else {
                E("%s:Get skuid.rf_id fail keep NFC by default\n",__func__);
                return 1;
        }
        if(nfc_rfbandid_size != 32) {  //32bytes = 4 bytes(int) * 8 rfbandid_info
                E("%s:Get skuid.rf_id size error keep NFC by default\n",__func__);
                return 1;
        }

        for ( i = 0; i < 8; i++) {
                if (nfc_rfbandid_info[i] == HAS_NFC_CHIP) {
                        I("%s: Check skuid.rf_id done device has NFC chip\n",__func__);
                        return 1;
                }
        }
        I("%s: Check skuid.rf_id done remove NFC\n",__func__);
        return 0;
#else //NFC_READ_RFSKUID
        return in_is_alive;
#endif //NFC_READ_RFSKUID

}


/******************************************************************************
 *
 *  Function pn548_htc_get_bootmode:
 *  Return  NFC_BOOT_MODE_NORMAL            0
 *          NFC_BOOT_MODE_FTM               1
 *          NFC_BOOT_MODE_DOWNLOAD          2
 *          NFC_BOOT_MODE_OFF_MODE_CHARGING 5
 *  Return 	NFC_BOOT_MODE_NORMAL by default
 *          if there's no bootmode infomation available
 *
 *          Bootmode enum is defined in
 *          kernel/include/htc/devices_cmdline.h
 *  enum {
 *	MFG_MODE_NORMAL,
 *	MFG_MODE_FACTORY2,
 *	MFG_MODE_RECOVERY,
 *	MFG_MODE_CHARGE,
 *	MFG_MODE_POWER_TEST,
 *	MFG_MODE_OFFMODE_CHARGING,
 *	MFG_MODE_MFGKERNEL_DIAG58,
 *	MFG_MODE_GIFT_MODE,
 *	MFG_MODE_MFGKERNEL,
 *	MFG_MODE_MINI,
 *	};
 ******************************************************************************/
int pn548_htc_get_bootmode(void) {
	int bootmode = NFC_BOOT_MODE_NORMAL;
#if NFC_GET_BOOTMODE
	bootmode = board_mfg_mode();
	if (bootmode == MFG_MODE_OFFMODE_CHARGING) {
		I("%s: Check bootmode done NFC_BOOT_MODE_OFF_MODE_CHARGING\n",__func__);
		return NFC_BOOT_MODE_OFF_MODE_CHARGING;
	} else {
		I("%s: Check bootmode done NFC_BOOT_MODE_NORMAL mode = %d\n",__func__,bootmode);
		return NFC_BOOT_MODE_NORMAL;
	}
#else
	return bootmode;
#endif  //NFC_GET_BOOTMODE
}


/******************************************************************************
 *
 *  Function pn548_htc_get_bootmode:
 *  Get platform required GPIO number from device tree
 *  For Power off sequence and OFF_MODE_CHARGING
 *
 ******************************************************************************/
bool pn548_htc_parse_dt(struct device *dev) {
	struct property *prop;
	int ret;
	I("%s:\n", __func__);

	NFC_I2C_SCL = of_get_named_gpio_flags(dev->of_node, "nfc_i2c_scl", 0, NULL);
	if(!gpio_is_valid(NFC_I2C_SCL)) {
		I("%s: invalid nfc_i2c_scl pin\n", __func__);
		return false;
        }
	NFC_I2C_SDA = of_get_named_gpio_flags(dev->of_node, "nfc_i2c_sda", 0, NULL);
	if(!gpio_is_valid(NFC_I2C_SDA)) {
		I("%s: invalid nfc_i2c_sda pin\n", __func__);
		return false;
	}
	prop = of_find_property(dev->of_node, "nfc_pvdd_regulator", NULL);
	if(prop)
	{
		ret = of_property_read_string(dev->of_node,"nfc_pvdd_regulator",&regulator_name);
		if(ret <0)
			return false;
		is_regulator = 1;
	}

	NFC_PVDD_GPIO = of_get_named_gpio_flags(dev->of_node, "nfc_pvdd_gpio", 0, NULL);
	if(!gpio_is_valid(NFC_PVDD_GPIO)) {
		is_pvdd_gpio = 0;
		I("%s: do not have NFC_PVDD_GPIO\n", __func__);
	} else {
		is_pvdd_gpio = 1;
		I("%s: NFC_PVDD_GPIO:%d\n", __func__, NFC_PVDD_GPIO);
	}

	I("%s: End, NFC_I2C_SCL:%d, NFC_I2C_SDA:%d, is_regulator:%d, is_pvdd_gpio:%d\n", __func__, NFC_I2C_SCL, NFC_I2C_SDA, is_regulator, is_pvdd_gpio);

	return true;
}

/*******************************************************************************
*
* Function pn548_htc_turn_off_pvdd
* Factory can cat 99 to turn off NFC pvdd
*
********************************************************************************/

void pn548_htc_turn_off_pvdd (void) {
	int ret;

	if(is_regulator)
	{
		ret = regulator_disable(regulator);
		I("%s : %s workaround regulator_disable\n", __func__, regulator_name);
		I("%s : %s workaround regulator_is_enabled = %d\n", __func__, regulator_name, regulator_is_enabled(regulator));
		if (ret < 0) {
			E("%s : %s workaround regulator_disable fail\n", __func__, regulator_name);
		}
	}

	if(is_pvdd_gpio)
	{
		ret = gpio_direction_output(NFC_PVDD_GPIO, 0);
		I("%s : NFC_PVDD_GPIO set 0 ret:%d, chk nfc_pvdd:%d \n", __func__,ret, gpio_get_value(NFC_PVDD_GPIO));
	}

}

/*********************************************************************************
*
* Function pn548_htc_power_off_sequence
* Correct power off sequence between scl and sda
*
*********************************************************************************/
void pn548_htc_power_off_sequence(int is_alive)
{
        if (is_alive) {
        int ret;

        ret = gpio_request(NFC_I2C_SCL , "nfc_i2c_scl");
        if(ret) {
                E("%s request scl error\n",__func__);
        }
        ret = gpio_request(NFC_I2C_SDA , "nfc_i2c_sda");
        if(ret){
                E("%s request sda error\n",__func__);
        }

        ret = gpio_direction_output(NFC_I2C_SCL, 0);
        I("%s : NFC_I2C_SCL set 0 %d \n", __func__,ret);
        mdelay(1);
        ret = gpio_direction_output(NFC_I2C_SDA, 0);
        I("%s : NFC_I2C_SDA set 0 %d \n", __func__,ret);
        mdelay(50);
        }
}

/***********************************************************************************
*
* Function pn548_htc_turn_on_pvdd
* turn on NFC pvdd while driver probing
*
***********************************************************************************/
bool pn548_htc_turn_on_pvdd (struct i2c_client *client)
{
	int ret;

	if(is_regulator)
	{
		if(!regulator) {
			regulator = regulator_get(&client->dev, regulator_name);
			I("%s : %s workaround regulator_get\n", __func__, regulator_name);
			if (regulator < 0) {
				E("%s : %s workaround regulator_get fail\n", __func__, regulator_name);
				return false;
			}
		}
		ret = regulator_set_voltage(regulator, 1800000, 1800000);
		I("%s : %s workaround regulator_set_voltage\n", __func__, regulator_name);
		if (ret < 0) {
			E("%s : %s workaround regulator_set_voltage fail\n", __func__, regulator_name);
			return false;
		}
		ret = regulator_enable(regulator);
		I("%s : %s workaround regulator_enable\n", __func__, regulator_name);
		I("%s : %s workaround regulator_is_enabled = %d\n", __func__, regulator_name, regulator_is_enabled(regulator));
		if (ret < 0) {
			E("%s : %s workaround regulator_enable fail\n", __func__, regulator_name);
			return false;
		}
		return true;
	}

	if(is_pvdd_gpio)
	{
		ret = gpio_direction_output(NFC_PVDD_GPIO, 1);
		I("%s : NFC_PVDD_GPIO set 1 ret:%d, chk nfc_pvdd:%d \n", __func__,ret, gpio_get_value(NFC_PVDD_GPIO));
	}

	return true;
}

/***************************************************************************************
*
* Function pn548_htc_regulator_status
* print out if i2c fail casued by PVDD
*
****************************************************************************************/
int pn548_htc_regulator_status(void)
{
	if(is_regulator)
	{
		E("%s : %s workaround regulator_is_enabled = %d\n", __func__, regulator_name, regulator_is_enabled(regulator));
		return regulator_is_enabled(regulator);
	}

	if(is_pvdd_gpio)
	{
		E("%s : chk is_pvdd_gpio = %d, pvdd_gpio value :%d\n", __func__, is_pvdd_gpio, gpio_get_value(NFC_PVDD_GPIO));
		return gpio_get_value(NFC_PVDD_GPIO);
	}
	return 0;
}

