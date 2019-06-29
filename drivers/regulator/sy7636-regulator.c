/*
 * Copyright (C) 2010 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/mfd/sy7636.h>
#include <linux/gpio.h>
#include <linux/pmic_status.h>
#include <linux/of_gpio.h>

#include <linux/input.h>


#define GDEBUG 1
#include <linux/gallen_dbg.h>

#define SY7636_V3P3_ENABLE		1

struct sy7636_data {
	int num_regulators;
	struct sy7636 *sy7636;
	struct regulator_dev **rdev;
};



static int sy7636_vcom = { -2500000 };

static int sy7636_is_power_good(struct sy7636 *sy7636);

static int _sy7636_rals_onoff(struct sy7636 *sy7636,int iIsON);

static ssize_t sy7636_powerup_read(struct device *dev, struct device_attribute *attr,char *buf)
{
	struct sy7636 *sy7636 = dev_get_drvdata(dev);
	unsigned int dwReg;

	//struct sy7636_platform_data *pdata = sy7636->pdata;
	if(PMIC_SUCCESS==sy7636_reg_read(sy7636,REG_SY7636_OPMODE,&dwReg)) {
		unsigned int dwReg;
		sy7636->bOPMode = (unsigned char)dwReg;
		sprintf(buf,"EN=%d,OP=0x%x,RailsEN=%d,RailsDisable=0x%x\n",
				(int)gpio_get_value(sy7636->gpio_pmic_powerup),
				sy7636->bOPMode,(int)BITFEXT(dwReg,RAILS_ON),BITFEXT(dwReg,RAILS_DISABLE) );
	}
	else {

		//sprintf(buf,"%d,%d\n",(int)gpio_get_value(sy7636->gpio_pmic_powerup),sy7636->gpio_pmic_powerup_stat);
		sprintf(buf,"%d\n",(int)gpio_get_value(sy7636->gpio_pmic_powerup));
	}
	return strlen(buf);
}
static ssize_t sy7636_powerup_write(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	struct sy7636 *sy7636 = dev_get_drvdata(dev);
	//struct sy7636_platform_data *pdata = sy7636->pdata;
	int iVal=-1;

	sscanf(buf,"%d\n",&iVal);
	if(1==iVal) {
		//gpio_set_value(sy7636->gpio_pmic_powerup,1);
		//sy7636->gpio_pmic_powerup_stat = 1;
		_sy7636_rals_onoff(sy7636,1);
	}
	else if(0==iVal) {
		//gpio_set_value(sy7636->gpio_pmic_powerup,0);
		//sy7636->gpio_pmic_powerup_stat = 0;
		_sy7636_rals_onoff(sy7636,0);
	}
	else {
		printk(KERN_ERR"%s() invalid parameter !!it must be [1|0] \n",__FUNCTION__);
	}
	return count;
}

static ssize_t sy7636_regs_read(struct device *dev, struct device_attribute *attr,char *buf)
{
	struct sy7636 *sy7636 = dev_get_drvdata(dev);
	int i;
	unsigned int cur_reg_val; /* current register value to modify */

	for (i=0; i<SY7636_REG_NUM; i++) {
		//struct sy7636_platform_data *pdata = sy7636->pdata;
		sy7636_reg_read(sy7636, i ,&cur_reg_val);
		printk ("[%s-%d] reg %d vlaue is %X\n",__func__,__LINE__, i, cur_reg_val);
		sprintf (&buf[i*5],"0x%02X,",(int)cur_reg_val);
	}
	sprintf (&buf[i*16],"\n");
	return strlen(buf);
}
static DEVICE_ATTR (powerup,0644,sy7636_powerup_read,sy7636_powerup_write);
static DEVICE_ATTR (regs,0444,sy7636_regs_read,NULL);

/*
 * to_reg_val(): Creates a register value with new data
 *
 * Creates a new register value for a particular field.  The data
 * outside of the new field is not modified.
 *
 * @cur_reg: current value in register
 * @reg_mask: mask of field bits to be modified
 * @fld_val: new value for register field.
 */
static unsigned int to_reg_val(unsigned int cur_reg, unsigned int fld_mask,
							   unsigned int fld_val)
{
	return (cur_reg & (~fld_mask)) | fld_val;
}

/*
 * Regulator operations
 */
/* Convert uV to the VCOM register bitfield setting */

static int vcom2_rs_to_uV(unsigned int reg_setting)
{
	if (reg_setting <= SY7636_VCOM_MIN_SET)
		return SY7636_VCOM_MIN_uV;
	if (reg_setting >= SY7636_VCOM_MAX_SET)
		return SY7636_VCOM_MAX_uV;
	return -(reg_setting * SY7636_VCOM_STEP_uV);
}

static int vcom2_uV_to_rs(int uV)
{
	if (uV <= SY7636_VCOM_MIN_uV)
		return SY7636_VCOM_MIN_SET;
	if (uV >= SY7636_VCOM_MAX_uV)
		return SY7636_VCOM_MAX_SET;
	return (-uV) / SY7636_VCOM_STEP_uV;
}

static int epdc_pwr0_enable(struct regulator_dev *reg)
{
	struct sy7636 *sy7636 = rdev_get_drvdata(reg);
	dbgENTER();

	gpio_set_value(sy7636->gpio_pmic_powerup, 1);
	//sy7636->gpio_pmic_powerup_stat = 1;

	dbgLEAVE();
	return 0;

}

static int epdc_pwr0_disable(struct regulator_dev *reg)
{
	struct sy7636 *sy7636 = rdev_get_drvdata(reg);
	dbgENTER();
	
	gpio_set_value(sy7636->gpio_pmic_powerup, 0);
	//sy7636->gpio_pmic_powerup_stat = 0;

	dbgLEAVE();
	return 0;

}


static int _sy7636_vcom_set_voltage(struct sy7636 *sy7636,
					int minuV, int uV, unsigned *selector)
{
	unsigned int cur_reg_val; /* current register value to modify */
	unsigned int new_reg_val; /* new register value to write */
	int retval;

	dbgENTER();
	

	if(sy7636_set_vcom(sy7636,uV/1000,0)<0) {
		retval = -1;
	}

	if(retval>=0) {
		sy7636->vcom_uV = uV;
	}

	dbgLEAVE();
	return retval;

}

static int sy7636_vcom_set_voltage(struct regulator_dev *reg,
					int minuV, int uV, unsigned *selector)
{
	struct sy7636 *sy7636 = rdev_get_drvdata(reg);
	return _sy7636_vcom_set_voltage(sy7636,minuV,uV,selector);
}


static int _sy7636_vcom_get_voltage(struct sy7636 *sy7636,
		struct regulator_dev *reg)
{
	int vcomValue;
	int iVCOMmV;


	dbgENTER();

	
	sy7636_get_vcom(sy7636,&iVCOMmV);

	vcomValue = iVCOMmV*1000;
	printk("%s() : vcom=%duV\n",__FUNCTION__,vcomValue);
	
	dbgLEAVE();
	return vcomValue;
}
static int sy7636_vcom_get_voltage(struct regulator_dev *reg)
{
	struct sy7636 *sy7636 = rdev_get_drvdata(reg);
	return _sy7636_vcom_get_voltage(sy7636,reg);
}

static int sy7636_vcom_enable(struct regulator_dev *reg)
{
	struct sy7636 *sy7636 = rdev_get_drvdata(reg);
	unsigned int cur_reg_val; /* current register value */

	dbgENTER();
	/*
	 * Check to see if we need to set the VCOM voltage.
	 * Should only be done one time. And, we can
	 * only change vcom voltage if we have been enabled.
	 */
	if (!sy7636->vcom_setup )// && sy7636_is_power_good(sy7636)
	{
		printk("%s():initial vcom=%duV\n ",__FUNCTION__,sy7636->vcom_uV);
		sy7636_vcom_set_voltage(reg,
			sy7636->vcom_uV,
			sy7636->vcom_uV,
			NULL);
		sy7636->vcom_setup = true;
	}

	if (sy7636->bOPMode & 0x40) {
		// vcom controlled by external . 
		gpio_set_value(sy7636->gpio_pmic_vcom_ctrl,1);
	}
	else {
		printk("%s vcom controlled autimatically \n",__FUNCTION__);
	}


	dbgLEAVE();
	return 0;
}

static int sy7636_vcom_disable(struct regulator_dev *reg)
{
	struct sy7636 *sy7636 = rdev_get_drvdata(reg);

	dbgENTER();

	if (sy7636->bOPMode & 0x40) {
		// vcom controlled by external . 
		gpio_set_value(sy7636->gpio_pmic_vcom_ctrl,0);
	}
	else {
		printk("%s vcom controlled autimatically \n",__FUNCTION__);
	}

	dbgLEAVE();
	return 0;
}

static int sy7636_vcom_is_enabled(struct regulator_dev *reg)
{
	struct sy7636 *sy7636 = rdev_get_drvdata(reg);

	int gpio = gpio_get_value(sy7636->gpio_pmic_vcom_ctrl);
	if (gpio == 0)
		return 0;
	else
		return 1;
}


static int sy7636_is_power_good(struct sy7636 *sy7636)
{
	/*
	 * XOR of polarity (starting value) and current
	 * value yields whether power is good.
	 */
	return gpio_get_value(sy7636->gpio_pmic_pwrgood) ^
		sy7636->pwrgood_polarity;
}


static void sy7636_int_func(struct work_struct *work)
{
	//unsigned int dwReg;
	struct sy7636 *sy7636 = container_of(work, struct sy7636, int_work);
	int iForceStanbyState = 0;
	unsigned char bFaults;
	int iChk ;

	iChk = sy7636_get_power_status(sy7636,0,0,&bFaults);
	if(iChk<0) {
		ERR_MSG("%s(),get power status failed !\n",__FUNCTION__);
		return ;
	}

	if(bFaults) {
		printk(KERN_ERR"SY7636 faults occured !!,faults=0x%x\n",bFaults);
		sy7636->int_state = MSC_RAW_EPD_UNKOWN_ERROR;
#ifdef SY7636_VDROP_PROC_IN_KERNEL//[
#else //][!SY7636_VDROP_PROC_IN_KERNEL
		ntx_report_event(EV_MSC,MSC_RAW,sy7636->int_state);
#endif //]SY7636_VDROP_PROC_IN_KERNEL
		if(iForceStanbyState) {
			//gpio_set_value(sy7636->gpio_pmic_powerup,0);
//			gpio_set_value(sy7636->gpio_pmic_wakeup,0);
			//msleep (10);
			//gpio_set_value(sy7636->gpio_pmic_powerup,1);
		}

		if(sy7636->pfnINTCB) {
			sy7636->pfnINTCB(sy7636->int_state);
		}
	}


	
}

// get latest interrupt state of sy7636 .
int sy7636_int_state_get(struct sy7636 *sy7636)
{
	if(sy7636->int_state>0) {
		return sy7636->int_state;
	}
	return -1;
}

// clear latest interrupt state of sy7636 .
int sy7636_int_state_clear(struct sy7636 *sy7636)
{
	sy7636->int_state=0;
	return 0;
}

int sy7636_int_callback_setup(struct sy7636 *sy7636,SY7636_INTEVT_CB fnCB)
{
	sy7636->pfnINTCB=fnCB;

	return 0;
}


static int sy7636_wait_power_good(struct sy7636 *sy7636)
{
	int i;
	int iChk;
	int iPG;
	unsigned char bFaults;

	msleep(1);

	//for (i = 0; i < sy7636->max_wait * 3; i++) 
	for (i = 0; i < sy7636->max_wait * 300; i++) 
	{
		//sy7636_int_func(&sy7636->int_work);
		queue_work(sy7636->int_workqueue,&sy7636->int_work);
		if (sy7636_is_power_good(sy7636)) {
			DBG_MSG("%s():cnt=%d,PG=%d\n",__FUNCTION__,i,gpio_get_value(sy7636->gpio_pmic_pwrgood));
			return 0;
		}

		msleep(1);
	}

	iChk = sy7636_get_power_status(sy7636,0,&iPG,&bFaults);
	if(iChk>=0) {
		printk(KERN_ERR"%s():faults=0x%x,PG=%d\n",__FUNCTION__,iChk,bFaults,iPG);
	}

	printk(KERN_ERR"%s():waiting(%d) for PG(%d) timeout\n",__FUNCTION__,i,gpio_get_value(sy7636->gpio_pmic_pwrgood));
	return -ETIMEDOUT;
}

static int _sy7636_rals_onoff(struct sy7636 *sy7636,int iIsON)
{
	unsigned int cur_reg_val,new_reg_val; /* current register value to modify */
	unsigned int fld_mask;	  /* register mask for bitfield to modify */
	unsigned int fld_val;	  /* new bitfield value to write */
	unsigned char bOP;

	if(!sy7636) {
		ERR_MSG("%s() : error object !\n",__FUNCTION__);
		return -1;
	}

	if(gpio_get_value(sy7636->gpio_pmic_powerup)==0) {
		WARNING_MSG("%s() : WARNING : EN gpio is low \n",__FUNCTION__);
		gpio_set_value(sy7636->gpio_pmic_powerup,1);
		msleep(SY7636_READY_MS);
	}

	sy7636_reg_read(sy7636,REG_SY7636_OPMODE,&cur_reg_val);
	sy7636->bOPMode = bOP = (unsigned char)cur_reg_val;
	if(iIsON) {
		/*
		if( BITFEXT(bOP,RAILS_ON) && BITFEXT(bOP,RAILS_DISABLE)!=0 ) 
		{
			DBG_MSG("%s() : SY7636 says rails already on,op=0x%x \n",
					__FUNCTION__,bOP);
#if 0
			if(gpio_get_value(sy7636->gpio_pmic_powerup)==0) {
				WARNING_MSG("%s() : But powerup gpio is low \n",__FUNCTION__);

				DBG_MSG("%s() : powerup set 1 automatically \n",__FUNCTION__);
				gpio_set_value(sy7636->gpio_pmic_powerup,1);sy7636->gpio_pmic_powerup_stat = 1;
			}
#endif
		}
		else 
		*/
		{

#if 0
			gpio_set_value(sy7636->gpio_pmic_powerup,1);sy7636->gpio_pmic_powerup_stat = 1;
#endif

			fld_mask = BITFMASK(RAILS_ON) | BITFMASK(VCOM_MANUAL) | 
				BITFMASK(RAILS_DISABLE) | BITFMASK(RESERVED);
			fld_val = BITFVAL(RAILS_ON,true) | BITFVAL(VCOM_MANUAL,false) | 
				BITFVAL(RAILS_DISABLE,0) | BITFVAL(RESERVED,0);
			new_reg_val = to_reg_val(cur_reg_val, fld_mask, fld_val);
			DBG_MSG("%s() : set SY7636 OP mode ON 0x%x->0x%x \n",
					__FUNCTION__,cur_reg_val,new_reg_val);
			sy7636_reg_write(sy7636,REG_SY7636_OPMODE,new_reg_val);
			sy7636->bOPMode = (unsigned char)new_reg_val;
		}
	}
	else {
#if 0
		if(!BITFEXT(bOP,RAILS_ON)||BITFEXT(bOP,RAILS_DISABLE)) {
			DBG_MSG("%s() : SY7636 says rails already off,OP=0x%x \n",
					__FUNCTION__,bOP);

#if 0
			if(gpio_get_value(sy7636->gpio_pmic_powerup)==1) {
				WARNING_MSG("%s() : But powerup gpio is high \n",__FUNCTION__);

				DBG_MSG("%s() : powerup set 0 automatically \n",__FUNCTION__);
				gpio_set_value(sy7636->gpio_pmic_powerup,0);sy7636->gpio_pmic_powerup_stat = 0;
			}
#endif

		}
		else 
#endif
		{

			fld_mask = BITFMASK(RAILS_ON)| BITFMASK(VCOM_MANUAL) | 
				BITFMASK(RAILS_DISABLE) | BITFMASK(RESERVED) ;
			fld_val = BITFVAL(RAILS_ON,false) | BITFVAL(VCOM_MANUAL,false) | 
				BITFVAL(RAILS_DISABLE,0) | BITFVAL(RESERVED,0) ;
			new_reg_val = to_reg_val(cur_reg_val, fld_mask, fld_val);
			DBG_MSG("%s() : set SY7636 OP mode OFF 0x%x->0x%x \n",
					__FUNCTION__,cur_reg_val,new_reg_val);
			sy7636_reg_write(sy7636,REG_SY7636_OPMODE,new_reg_val);
			sy7636->bOPMode = (unsigned char)new_reg_val;

#if 0
			gpio_set_value(sy7636->gpio_pmic_powerup,0);sy7636->gpio_pmic_powerup_stat = 0;
#endif
		}


	}

	return 0;
}



static int sy7636_display_enable(struct regulator_dev *reg)
{
	struct sy7636 *sy7636 = rdev_get_drvdata(reg);

	dbgENTER();

	_sy7636_rals_onoff(sy7636,1);

	dbgLEAVE();
	return sy7636_wait_power_good(sy7636);
}

static int sy7636_display_disable(struct regulator_dev *reg)
{
#if 0
	printk(KERN_ERR"%s():skipped !!\n",__FUNCTION__);
	return 0;
#else
	struct sy7636 *sy7636 = rdev_get_drvdata(reg);

	dbgENTER();
	_sy7636_rals_onoff(sy7636,0);
	dbgLEAVE();
	return 0;
#endif
}

static int sy7636_display_is_enabled(struct regulator_dev *reg)
{
	struct sy7636 *sy7636 = rdev_get_drvdata(reg);
	unsigned int cur_reg; /* current register value */
#if 1
	if(gpio_get_value(sy7636->gpio_pmic_powerup)==0) {
		WARNING_MSG("%s() : SY7636 EN is low !\n",__FUNCTION__);
	}
#endif

	sy7636_reg_read(sy7636,REG_SY7636_OPMODE,&cur_reg);
	sy7636->bOPMode = (unsigned char)cur_reg;

	DBG_MSG("%s() : OP=0x%x,RailsEN=%d,RailsDisable=0x%x\n",__FUNCTION__,
			sy7636->bOPMode,(int)BITFEXT(cur_reg,RAILS_ON),BITFEXT(cur_reg,RAILS_DISABLE));

	if(!BITFEXT(cur_reg,RAILS_ON) || BITFEXT(cur_reg,RAILS_DISABLE)) {
		return 0;
	}
	else {
		return 1;
	}
}




#ifdef SY7636_V3P3_ENABLE//[

static int sy7636_v3p3_enable(struct regulator_dev *reg)
{
	struct sy7636 *sy7636 = rdev_get_drvdata(reg);
	dbgENTER();
	sy7636->fake_vp3v3_stat = 1;
	dbgLEAVE();
	return 0;
}

static int sy7636_v3p3_disable(struct regulator_dev *reg)
{
	struct sy7636 *sy7636 = rdev_get_drvdata(reg);
	dbgENTER();
	sy7636->fake_vp3v3_stat = 0;
	dbgLEAVE();
	return 0;

}
static int sy7636_v3p3_is_enabled(struct regulator_dev *reg)
{
	struct sy7636 *sy7636 = rdev_get_drvdata(reg);

	return sy7636->fake_vp3v3_stat;
}

#endif //] SY7636_V3P3_ENABLE

static int sy7636_tmst_get_temperature(struct regulator_dev *reg)
{
	struct sy7636 *sy7636 = rdev_get_drvdata(reg);
	const int iDefaultTemp = 25;
	int iTemperature = iDefaultTemp;

	if(sy7636_get_temperature(sy7636,&iTemperature)<0) { 
		iTemperature = iDefaultTemp;
	}

	return iTemperature;
}

/*
 * Regulator operations
 */

static struct regulator_ops sy7636_display_ops = {
	.enable = sy7636_display_enable,
	.disable = sy7636_display_disable,
	.is_enabled = sy7636_display_is_enabled,
};

static struct regulator_ops sy7636_vcom_ops = {
	.enable = sy7636_vcom_enable,
	.disable = sy7636_vcom_disable,
	.get_voltage = sy7636_vcom_get_voltage,
	.set_voltage = sy7636_vcom_set_voltage,
	.is_enabled = sy7636_vcom_is_enabled,
};

static struct regulator_ops sy7636_v3p3_ops = {
	.enable = sy7636_v3p3_enable,
	.disable = sy7636_v3p3_disable,
	.is_enabled = sy7636_v3p3_is_enabled,
};

static struct regulator_ops sy7636_tmst_ops = {
	.get_voltage = sy7636_tmst_get_temperature,
};


/*
 * Regulator descriptors
 */
static struct regulator_desc sy7636_reg[] = {
{
	.name = "DISPLAY",
	.id = SY7636_DISPLAY,
	.ops = &sy7636_display_ops,
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
},
{
	.name = "VCOM",
	.id = SY7636_VCOM,
	.ops = &sy7636_vcom_ops,
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
},
#ifdef SY7636_V3P3_ENABLE//[
{
	.name = "V3P3",
	.id = SY7636_VP3V3,
	.ops = &sy7636_v3p3_ops,
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
},
#endif //]SY7636_V3P3_ENABLE
{
	.name = "TMST",
	.id = SY7636_TMST,
	.ops = &sy7636_tmst_ops,
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
},
};

static void sy7636_setup_timings(struct sy7636 *sy7636)
{

	unsigned char bOnDly1, bOnDly2, bOnDly3, bOnDly4;
	unsigned int cur_reg,new_reg_val;
	unsigned int fld_mask = 0;	  /* register mask for bitfield to modify */
	unsigned int fld_val = 0;	  /* new bitfield value to write */

	if( 0xff==sy7636->on_delay1 &&
			0xff==sy7636->on_delay2 &&
			0xff==sy7636->on_delay3 &&
			0xff==sy7636->on_delay4)
	{
		// nothing to do .
		DBG_MSG("%s(),skipped !\n",__FUNCTION__);
		return ;
	}

	sy7636_reg_read(sy7636,REG_SY7636_PWRON_DLY, &cur_reg);

	bOnDly1 = BITFEXT(cur_reg,TDLY1);
	bOnDly2 = BITFEXT(cur_reg,TDLY2);
	bOnDly3 = BITFEXT(cur_reg,TDLY3);
	bOnDly4 = BITFEXT(cur_reg,TDLY4);
	
	if(0xff!=sy7636->on_delay1 && bOnDly1!=sy7636->on_delay1) {
		fld_mask |= BITFMASK(TDLY1);
		fld_val |= BITFVAL(TDLY1, sy7636->on_delay1);
	}
	if(0xff!=sy7636->on_delay2 && bOnDly1!=sy7636->on_delay2) {
		fld_mask |= BITFMASK(TDLY2);
		fld_val |= BITFVAL(TDLY2, sy7636->on_delay2);
	}
	if(0xff!=sy7636->on_delay3 && bOnDly1!=sy7636->on_delay3) {
		fld_mask |= BITFMASK(TDLY3);
		fld_val |= BITFVAL(TDLY3, sy7636->on_delay3);
	}
	if(0xff!=sy7636->on_delay4 && bOnDly1!=sy7636->on_delay4) {
		fld_mask |= BITFMASK(TDLY4);
		fld_val |= BITFVAL(TDLY4, sy7636->on_delay4);
	}
	new_reg_val = to_reg_val(cur_reg, fld_mask, fld_val);
	if(cur_reg!=new_reg_val) {
		DBG_MSG("%s(),poweron delay=0x%x\n",__FUNCTION__,new_reg_val);
		sy7636_reg_write(sy7636,REG_SY7636_PWRON_DLY, new_reg_val);
	}
}

#define CHECK_PROPERTY_ERROR_KFREE(prop) \
do { \
	int ret = of_property_read_u32(sy7636->dev->of_node, \
					#prop, &sy7636->prop); \
	if (ret < 0) { \
		return ret;	\
	}	\
} while (0);

#ifdef CONFIG_OF
static int sy7636_pmic_dt_parse_pdata(struct platform_device *pdev,
					struct sy7636_platform_data *pdata)
{
	struct sy7636 *sy7636 = dev_get_drvdata(pdev->dev.parent);
	struct device_node *pmic_np, *regulators_np, *reg_np, *VLDO_np;

	struct sy7636_regulator_data *rdata;
	int i, ret;

	GALLEN_DBGLOCAL_BEGIN();

	pmic_np = of_node_get(sy7636->dev->of_node);
	if (!pmic_np) {
		dev_err(&pdev->dev, "could not find pmic sub-node\n");
		return -ENODEV;
	}


	regulators_np = of_find_node_by_name(pmic_np, "regulators");
	if (!regulators_np) {
		dev_err(&pdev->dev, "could not find regulators sub-node\n");
		return -EINVAL;
	}

	pdata->num_regulators = of_get_child_count(regulators_np);
	dev_info(&pdev->dev, "num_regulators %d\n", pdata->num_regulators);


	//DBG_MSG("%s(%d):skipped !!\n",__FILE__,__LINE__);return -1;

	rdata = devm_kzalloc(&pdev->dev, sizeof(*rdata) *
				pdata->num_regulators, GFP_KERNEL);
	if (!rdata) {
		of_node_put(regulators_np);
		dev_err(&pdev->dev, "could not allocate memory for"
			"regulator data\n");
		return -ENOMEM;
	}

	//DBG_MSG("%s(%d):test ok !!\n",__FILE__,__LINE__);return -1;

	pdata->regulators = rdata;
	for_each_child_of_node(regulators_np, reg_np) {
		DBG_MSG("%s():regulator name=\"%s\"\n",__FUNCTION__,reg_np->name);

		for (i = 0; i < ARRAY_SIZE(sy7636_reg); i++)
			if (!of_node_cmp(reg_np->name, sy7636_reg[i].name))
				break;

		if (i == ARRAY_SIZE(sy7636_reg)) {
			dev_warn(&pdev->dev, "don't know how to configure "
				"regulator %s\n", reg_np->name);
			continue;
		}

		rdata->id = i;
		rdata->initdata = of_get_regulator_init_data(&pdev->dev,
							     reg_np, &sy7636_reg[i]);
		rdata->reg_node = reg_np;
		rdata++;
	}
	of_node_put(regulators_np);


	if(of_property_read_u32(pmic_np,"on_delay1",&sy7636->on_delay1)) {
		sy7636->on_delay1=0xff;
		dev_info(&pdev->dev, "failed to get on_delay1 property,deault will be 0x2\n");
	}
	else {
		dev_info(&pdev->dev, "on_delay1=0x%x\n",sy7636->on_delay1);
	}
	if(of_property_read_u32(pmic_np,"on_delay2",&sy7636->on_delay2)) {
		sy7636->on_delay2=0xff;
		dev_info(&pdev->dev, "failed to get on_delay2 property,deault will be 0x2\n");
	}
	else {
		dev_info(&pdev->dev, "on_delay2=0x%x\n",sy7636->on_delay2);
	}
	if(of_property_read_u32(pmic_np,"on_delay3",&sy7636->on_delay3)) {
		sy7636->on_delay3=0xff;
		dev_info(&pdev->dev, "failed to get on_delay3 property,deault will be 0x2\n");
	}
	else {
		dev_info(&pdev->dev, "on_delay3=0x%x\n",sy7636->on_delay3);
	}
	if(of_property_read_u32(pmic_np,"on_delay4",&sy7636->on_delay4)) {
		sy7636->on_delay4=0xff;
		dev_info(&pdev->dev, "failed to get on_delay4 property,deault will be 0x2\n");
	}
	else {
		dev_info(&pdev->dev, "on_delay4=0x%x\n",sy7636->on_delay4);
	}

	if(of_property_read_u32(pmic_np,"VLDO",&sy7636->VLDO)) {
		sy7636->VLDO = (unsigned int)-1;
		dev_info(&pdev->dev, "failed to get VLDO property,deault will be 0x2\n");
	}
	else {
		dev_info(&pdev->dev, "VLDO=0x%x\n",sy7636->VLDO);
	}


	sy7636->max_wait = (6 + 6 + 6 + 6);


	sy7636->gpio_pmic_vcom_ctrl = of_get_named_gpio(pmic_np,
					"gpio_pmic_vcom_ctrl", 0);
	if (!gpio_is_valid(sy7636->gpio_pmic_vcom_ctrl)) {
		dev_warn(&pdev->dev, "no epdc pmic vcom_ctrl pin available\n");
	}
	else {
		ret = devm_gpio_request_one(&pdev->dev, sy7636->gpio_pmic_vcom_ctrl,
				GPIOF_OUT_INIT_LOW, "epdc-vcom");
		if (ret < 0) {
			dev_err(&pdev->dev, "request vcom gpio failed (%d)!\n",ret);
			//goto err;
		}
	}

	sy7636->gpio_pmic_powerup = of_get_named_gpio(pmic_np,
					"gpio_pmic_powerup", 0);
	if (!gpio_is_valid(sy7636->gpio_pmic_powerup)) {
		dev_err(&pdev->dev, "no epdc pmic powerup pin available\n");
		goto err;
	}
	else {
		DBG_MSG("%s():gpio_pmic_powerup=%d\n",__FUNCTION__,sy7636->gpio_pmic_powerup);
	}
#if 0
	ret = devm_gpio_request_one(&pdev->dev, sy7636->gpio_pmic_powerup,
				GPIOF_IN, "epdc-powerup");
#else 
	ret = devm_gpio_request_one(&pdev->dev, sy7636->gpio_pmic_powerup,
				GPIOF_OUT_INIT_HIGH, "epdc-powerup");
	//sy7636->gpio_pmic_powerup_stat = 1;
#endif
	if (ret < 0) {
		dev_err(&pdev->dev, "request powerup gpio failed (%d)!\n",ret);
		//goto err;
	}


	sy7636->gpio_pmic_pwrgood = of_get_named_gpio(pmic_np,
					"gpio_pmic_pwrgood", 0);
	if (!gpio_is_valid(sy7636->gpio_pmic_pwrgood)) {
		dev_err(&pdev->dev, "no epdc pmic pwrgood pin available\n");
		goto err;
	}
	ret = devm_gpio_request_one(&pdev->dev, sy7636->gpio_pmic_pwrgood,
				GPIOF_IN, "epdc-pwrstat");
	if (ret < 0) {
		dev_err(&pdev->dev, "request pwrstat gpio failed (%d)!\n",ret);
		//goto err;
	}

err:
	GALLEN_DBGLOCAL_END();
	return 0;

}
#else
static int sy7636_pmic_dt_parse_pdata(struct platform_device *pdev,
					struct sy7636 *sy7636)
{
	return 0;
}
#endif	/* !CONFIG_OF */

int sy7636_regulator_hwinit(struct sy7636 *sy7636)
{
	int iRet;

	if (!sy7636->vcom_setup) 
	{
		_sy7636_vcom_set_voltage(sy7636,
			sy7636->vcom_uV,
			sy7636->vcom_uV,
			NULL);
		sy7636->vcom_setup = true;
	}
	
	if(-1 != (int)sy7636->VLDO) {
		unsigned int fld_mask = 0;	  /* register mask for bitfield to modify */
		unsigned int fld_val = 0;	  /* new bitfield value to write */
		unsigned int new_reg_val,cur_reg ;

		sy7636_reg_read(sy7636,REG_SY7636_PWRON_DLY, &cur_reg);
		fld_mask = BITFMASK(VLDO_ADJ);
		fld_val = BITFVAL(VLDO_ADJ, sy7636->VLDO);
		new_reg_val = to_reg_val(cur_reg, fld_mask, fld_val);
		
		if(cur_reg!=new_reg_val) {
			DBG_MSG("%s():VLDO(%d) setting 0x%x...\n",__FUNCTION__,
					(int)sy7636->VLDO,new_reg_val);
			sy7636_reg_write(sy7636,REG_SY7636_VLDO_ADJ, new_reg_val);
		}
	}

	sy7636_setup_timings(sy7636);

	return iRet;
}


/*
 * Regulator init/probing/exit functions
 */
static int sy7636_regulator_probe(struct platform_device *pdev)
{
	struct sy7636 *sy7636 = dev_get_drvdata(pdev->dev.parent);
	struct sy7636_platform_data *pdata = sy7636->pdata;
	struct sy7636_data *priv;
	struct regulator_dev **rdev;
	struct regulator_config config = { };
	int size, i, ret = 0;

	DBG_MSG("%s starting , of_node=%p\n",__FUNCTION__,sy7636->dev->of_node);


	//sy7636->pwrgood_polarity = 1;
	if (sy7636->dev->of_node) {
		
		ret = sy7636_pmic_dt_parse_pdata(pdev, pdata);
		if (ret)
			return ret;
	}
	priv = devm_kzalloc(&pdev->dev, sizeof(struct sy7636_data),
			       GFP_KERNEL);
	if (!priv)
		return -ENOMEM;


	size = sizeof(struct regulator_dev *) * pdata->num_regulators;
	priv->rdev = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
	if (!priv->rdev)
		return -ENOMEM;


	printk("%s(%d): SY7636@%p\n",__FUNCTION__,__LINE__,sy7636);

	rdev = priv->rdev;
	priv->num_regulators = pdata->num_regulators;
	platform_set_drvdata(pdev, priv);

	sy7636->vcom_setup = true;
	sy7636->vcom_uV = sy7636_vcom;

	sy7636_regulator_hwinit(sy7636);

	for (i = 0; i < pdata->num_regulators; i++) {
		int id = pdata->regulators[i].id;

		config.dev = sy7636->dev;
		config.init_data = pdata->regulators[i].initdata;
		config.driver_data = sy7636;
		config.of_node = pdata->regulators[i].reg_node;

		rdev[i] = regulator_register(&sy7636_reg[id], &config);
		if (IS_ERR(rdev[i])) {
			ret = PTR_ERR(rdev[i]);
			dev_err(&pdev->dev, "regulator init failed for %d\n",
					id);
			rdev[i] = NULL;
			goto err;
		}
	}




#if 1
	ret = device_create_file(sy7636->dev, &dev_attr_powerup);
	if (ret < 0) {
		dev_err(sy7636->dev, "fail : sy7636 powerup create.\n");
		return ret;
	}
	ret = device_create_file(sy7636->dev, &dev_attr_regs);
	if (ret < 0) {
		dev_err(sy7636->dev, "fail : sy7636 regs create.\n");
		return ret;
	}
#endif

	INIT_WORK(&sy7636->int_work, sy7636_int_func);
	sy7636->int_workqueue = create_singlethread_workqueue("SY7636_INT");
	if(sy7636->int_workqueue) {
		dev_err(sy7636->dev, "sy7636 int workqueue creating failed !\n");
	}

    DBG_MSG("%s success\n",__FUNCTION__);
	return 0;
err:
	while (--i >= 0)
		regulator_unregister(rdev[i]);
	return ret;
}

static int sy7636_regulator_remove(struct platform_device *pdev)
{
	struct sy7636_data *priv = platform_get_drvdata(pdev);
	struct regulator_dev **rdev = priv->rdev;
	int i;

	for (i = 0; i < priv->num_regulators; i++)
		regulator_unregister(rdev[i]);
	return 0;
}

static const struct platform_device_id sy7636_pmic_id[] = {
	{ "sy7636-pmic", 0},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(platform, sy7636_pmic_id);

static struct platform_driver sy7636_regulator_driver = {
	.probe = sy7636_regulator_probe,
	.remove = sy7636_regulator_remove,
	.id_table = sy7636_pmic_id,
	.driver = {
		.name = "sy7636-pmic",
	},
};

static int __init sy7636_regulator_init(void)
{
	return platform_driver_register(&sy7636_regulator_driver);
}
subsys_initcall_sync(sy7636_regulator_init);

static void __exit sy7636_regulator_exit(void)
{
	platform_driver_unregister(&sy7636_regulator_driver);
}
module_exit(sy7636_regulator_exit);


/*
 * Parse user specified options (`sy7636:')
 * example:
 *   sy7636:pass=2,vcom=-1250000
 */
static int __init sy7636_setup(char *options)
{
	int ret;
	char *opt;
	unsigned long ulResult;
	while ((opt = strsep(&options, ",")) != NULL) {
		if (!*opt)
			continue;
		if (!strncmp(opt, "vcom=", 5)) {
			int offs = 5;
			if (opt[5] == '-')
				offs = 6;
			ret = kstrtoul((const char *)(opt + offs), 0, &ulResult);
			sy7636_vcom = (int) ulResult;
			if (ret < 0)
				return ret;
			sy7636_vcom = -sy7636_vcom;
		}
	}

	return 1;
}

__setup("sy7636:", sy7636_setup);


/* Module information */
MODULE_DESCRIPTION("SY7636 regulator driver");
MODULE_LICENSE("GPL");

