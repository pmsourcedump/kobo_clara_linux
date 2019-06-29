/*
 * Copyright (C) 2010 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

/*!
 * @file pmic/core/sy7636.c
 * @brief This file contains SY7636 specific PMIC code. This implementaion
 * may differ for each PMIC chip.
 *
 * @ingroup PMIC_CORE
 */

/*
 * Includes
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>

#include <linux/platform_device.h>
#include <linux/regulator/machine.h>
#include <linux/pmic_status.h>
#include <linux/mfd/core.h>
#include <linux/mfd/sy7636.h>
#include <asm/mach-types.h>


#define GDEBUG 1
#include <linux/gallen_dbg.h>


/*
 * EPDC PMIC I2C address
 */
#define EPDC_PMIC_I2C_ADDR 0x66



static int sy7636_detect(struct i2c_client *client, struct i2c_board_info *info);
static struct regulator *gpio_regulator;

static struct mfd_cell sy7636_devs[] = {
	{ .name = "sy7636-pmic", },
	{ .name = "sy7636-sns", },
};

static const unsigned short normal_i2c[] = {EPDC_PMIC_I2C_ADDR, I2C_CLIENT_END};



int sy7636_reg_read(struct sy7636 *sy7636,int reg_num, unsigned int *reg_val)
{
	int result;
	struct i2c_client *sy7636_client = sy7636->i2c_client;
	int iCurrentEN_State=gpio_get_value(sy7636->gpio_pmic_powerup);

	if(0==iCurrentEN_State) {
		dev_warn(&sy7636_client->dev,"sy7636 EN is disabled, force turn on !\n");
		dump_stack();
		gpio_set_value(sy7636->gpio_pmic_powerup,1);
		msleep(SY7636_READY_MS);
	}

	if (sy7636_client == NULL) {
		dev_err(&sy7636_client->dev,
			"sy7636 I2C adaptor not ready !\n");
		return PMIC_ERROR;
	}

	result = i2c_smbus_read_byte_data(sy7636_client, reg_num);
	if (result < 0) {
		dev_err(&sy7636_client->dev,
			"Unable to read sy7636 register%d via I2C\n",reg_num);
		return PMIC_ERROR;
	}

	*reg_val = result;

	//if(0==iCurrentEN_State) {
		// restore current en state .
		//gpio_set_value(sy7636->gpio_pmic_powerup,0);
	//}

	return PMIC_SUCCESS;
}

int sy7636_reg_write(struct sy7636 *sy7636,int reg_num, const unsigned int reg_val)
{
	int result;
	struct i2c_client *sy7636_client=sy7636->i2c_client;
	int iCurrentEN_State=gpio_get_value(sy7636->gpio_pmic_powerup);

	if(0==iCurrentEN_State) {
		dev_warn(&sy7636_client->dev,"sy7636 EN is disabled, force turn on !\n");
		dump_stack();
		gpio_set_value(sy7636->gpio_pmic_powerup,1);
		msleep(SY7636_READY_MS);
	}

	if (sy7636_client == NULL) {
		dev_err(&sy7636_client->dev,
			"sy7636 I2C adaptor not ready !\n");
		return PMIC_ERROR;
	}


	result = i2c_smbus_write_byte_data(sy7636_client, reg_num, reg_val);
	if (result < 0) {
		dev_err(&sy7636_client->dev,
			"Unable to write TPS6518x register%d via I2C\n",reg_num);
		return PMIC_ERROR;
	}

	//if(0==iCurrentEN_State) {
		// restore current en state .
		//gpio_set_value(sy7636->gpio_pmic_powerup,0);
	//}

	return PMIC_SUCCESS;
}


int sy7636_chip_power(struct sy7636 *sy7636,int iIsON)
{
	int iPwrallCurrentStat=-1;
	int iRet = 0;

	if(!sy7636) {
		printk(KERN_ERR"%s(): object error !! \n",__FUNCTION__);
		return -1;
	}

	if (!gpio_is_valid(sy7636->gpio_pmic_pwrall)) {
		printk(KERN_ERR"%s(): no epdc pmic pwrall pin available\n",__FUNCTION__);
		return -2;
	}

	//iPwrallCurrentStat = sy7636->gpio_pmic_pwrall_stat;
	iPwrallCurrentStat = gpio_get_value(sy7636->gpio_pmic_pwrall);

	if(iIsON) {
		gpio_set_value(sy7636->gpio_pmic_pwrall,1);
		gpio_set_value(sy7636->gpio_pmic_powerup,1);;
	}
	else {
		gpio_set_value(sy7636->gpio_pmic_powerup,0);
		gpio_set_value(sy7636->gpio_pmic_pwrall,0);
		sy7636->vcom_setup = 0;//need setup vcom again .
	}


	if(iPwrallCurrentStat!=iIsON) {
		if(iIsON) {
			// state change and turn on .
			mdelay(2);
			iRet = 1;
		}
		else {
			// state change and turn off .
			iRet = 2;
		}
	}


	if(iIsON) {
		sy7636_regulator_hwinit(sy7636);
	}

	return iRet;
}


int sy7636_get_FaultFlags(struct sy7636 *sy7636,int iIsGetCached)
{
	unsigned int dwReg;
	int iRet ;

	if(!sy7636) {
		ERR_MSG("%s():error object !\n",__FUNCTION__);
		return -2;
	}

	if(iIsGetCached) {
		iRet = (int)sy7636->bFaultFlags;
	}
	else {
		if(sy7636_reg_read(sy7636,REG_SY7636_FAULTFLAGS,&dwReg)>=0) {
			sy7636->bFaultFlags = (unsigned char)dwReg;
			iRet = (int)sy7636->bFaultFlags;
		}
		else {
			iRet = -1;
			ERR_MSG("%s():reading fault flags failed !\n",__FUNCTION__);
		}
	}
	return iRet;
}

int sy7636_get_power_status(struct sy7636 *sy7636,int iIsGetCached,int *O_piPG,unsigned char *O_pbFaults)
{
	int iRet;
	unsigned char bFaultFlags;
	unsigned char bFaults;
	int iPG;

	iRet = sy7636_get_FaultFlags(sy7636,iIsGetCached);

	if(iRet<0) {
		bFaults = 0;
		bFaultFlags = 0;
		iRet = -1;
		return -1;
	}
	else {
		bFaultFlags = (unsigned char)iRet;
		bFaults = BITFEXT(bFaultFlags,FAULTS);
		iPG = BITFEXT(bFaultFlags,PG);
		if(bFaults) {
			iRet = 1;
		}
		else {
			iRet = 0;
		}
	}

	if(O_piPG) {
		*O_piPG = iPG;
	}

	if(O_pbFaults) {
		*O_pbFaults = bFaults;
	}

	return iRet;
}

#ifdef CONFIG_OF
static struct sy7636_platform_data *sy7636_i2c_parse_dt_pdata(
					struct device *dev)
{
	struct sy7636_platform_data *pdata;
	struct device_node *pmic_np;
	struct sy7636 *sy7636 = dev_get_drvdata(dev);
	int ret;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(dev, "could not allocate memory for pdata\n");
		return ERR_PTR(-ENOMEM);
	}

	pmic_np = of_node_get(dev->of_node);
	if (!pmic_np) {
		dev_err(dev, "could not find pmic sub-node\n");
		return ERR_PTR(-ENODEV);
	}

	sy7636->gpio_pmic_pwrall = of_get_named_gpio(pmic_np,
					"gpio_pmic_pwrall", 0);
	if (!gpio_is_valid(sy7636->gpio_pmic_pwrall)) {
		dev_err(dev, "no epdc pmic pwrall pin available\n");
		return ERR_PTR(-ENODEV);
	}
	else {
		printk("%s():gpio_pmic_pwrall=%d\n",__FUNCTION__,sy7636->gpio_pmic_pwrall);
	}
	ret = devm_gpio_request_one(dev, sy7636->gpio_pmic_pwrall,
				GPIOF_OUT_INIT_HIGH, "epdc-pmic-pwrall");
	if (ret < 0) {
		dev_err(dev, "request pwrall gpio failed (%d)!\n",ret);
		//goto err;
	}
	else {
		printk("%s():gpio_pmic_pwrall init set 1\n",__FUNCTION__);
		//sy7636->gpio_pmic_pwrall_stat = 1;
		//msleep(20);
	}

	

	sy7636->gpio_pmic_vcom_ctrl = of_get_named_gpio(pmic_np,
					"gpio_pmic_vcom_ctrl", 0);
	if (!gpio_is_valid(sy7636->gpio_pmic_vcom_ctrl)) {
		dev_err(dev, "no epdc pmic vcom_ctrl pin available\n");
	}
	ret = devm_gpio_request_one(dev, sy7636->gpio_pmic_vcom_ctrl,
				GPIOF_OUT_INIT_LOW, "epdc-vcom");
	if (ret < 0) {
		dev_err(dev, "request vcom gpio failed (%d)!\n",ret);
	}

	sy7636->gpio_pmic_powerup = of_get_named_gpio(pmic_np,
					"gpio_pmic_powerup", 0);
	if (!gpio_is_valid(sy7636->gpio_pmic_powerup)) {
		dev_err(dev, "no epdc pmic powerup pin available\n");
	}
	else {
		printk("%s():gpio_pmic_powerup=%d\n",__FUNCTION__,sy7636->gpio_pmic_powerup);
	}
	ret = devm_gpio_request_one(dev, sy7636->gpio_pmic_powerup,
				GPIOF_OUT_INIT_LOW, "epdc-powerup");
	if (ret < 0) {
		dev_err(dev, "request powerup gpio failed (%d)!\n",ret);
	}
	else {
		//sy7636->gpio_pmic_powerup_stat = 0;
	}


	return pdata;
}
#else
static struct sy7636_platform_data *sy7636_i2c_parse_dt_pdata(
					struct device *dev)
{
	return NULL;
}
#endif	/* !CONFIG_OF */

static int sy7636_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct sy7636 *sy7636;
	struct sy7636_platform_data *pdata = client->dev.platform_data;
	struct device_node *np = client->dev.of_node;
	int ret = 0;

	printk("sy7636_probe calling\n");

	if (!np)
		return -ENODEV;


/*	
	gpio_regulator = devm_regulator_get(&client->dev, "SENSOR");
	if (!IS_ERR(gpio_regulator)) {
		ret = regulator_enable(gpio_regulator);
		if (ret) {
			dev_err(&client->dev, "PMIC power on failed !\n");
			return ret;
		}
	}
*/


	/* Create the PMIC data structure */
	sy7636 = kzalloc(sizeof(struct sy7636), GFP_KERNEL);
	if (sy7636 == NULL) {
		kfree(client);
		return -ENOMEM;
	}

	//sy7636->gpio_pmic_powerup_stat = -1;
	//sy7636->gpio_pmic_pwrall_stat = -1;
	sy7636->fake_vp3v3_stat = 0;
	sy7636->vcom_setup = 0;// need setup vcom again .
	sy7636->bOPMode = 0xff;

	sy7636->on_delay1 = 0xff;
	sy7636->on_delay2 = 0xff;
	sy7636->on_delay3 = 0xff;
	sy7636->on_delay4 = 0xff;

	sy7636->VLDO = (unsigned int)-1;

	/* Initialize the PMIC data structure */
	i2c_set_clientdata(client, sy7636);
	sy7636->dev = &client->dev;
	sy7636->i2c_client = client;


	if (sy7636->dev->of_node) {
		pdata = sy7636_i2c_parse_dt_pdata(sy7636->dev);
		if (IS_ERR(pdata)) {
			ret = PTR_ERR(pdata);
			goto err1;
		}

	}


	if (!gpio_is_valid(sy7636->gpio_pmic_pwrall)) {
		dev_err(&client->dev, "pwrall gpio not available !!\n");
		goto err1;
	}

	//if(1!=sy7636->gpio_pmic_pwrall_stat) 
	if(1!=gpio_get_value(sy7636->gpio_pmic_pwrall)) 
	{
		dev_info(&client->dev, "PMIC chip off, turning on the PMIC chip power ...\n");
		gpio_set_value(sy7636->gpio_pmic_pwrall,1);
		//sy7636->gpio_pmic_pwrall_stat = 1;
		mdelay(2);
	}

	ret = sy7636_detect(client, NULL);
	if (ret)
		goto err1;

	mfd_add_devices(sy7636->dev, -1, sy7636_devs,
			ARRAY_SIZE(sy7636_devs),
			NULL, 0, NULL);

	sy7636->pdata = pdata;

	dev_info(&client->dev, "PMIC SY7636 for eInk display\n");

	printk("sy7636_probe success\n");

	return ret;

err2:
	mfd_remove_devices(sy7636->dev);
err1:
	kfree(sy7636);

	return ret;
}


static int sy7636_remove(struct i2c_client *i2c)
{
	struct sy7636 *sy7636 = i2c_get_clientdata(i2c);

	mfd_remove_devices(sy7636->dev);

	return 0;
}

extern int gSleep_Mode_Suspend;

static int sy7636_suspend_late(struct device *dev)
{
	return 0;
}


static int sy7636_resume_early(struct device *dev)
{
	return 0;
}

static int sy7636_suspend(struct device *dev)
{
	struct i2c_client *client = i2c_verify_client(dev);    
	struct sy7636 *sy7636 = i2c_get_clientdata(client);

	//gpio_set_value(sy7636->gpio_pmic_vcom_ctrl,0);


	if (gSleep_Mode_Suspend) {
		sy7636_chip_power(sy7636,0);
	}
	
	
	return 0;
}

static int sy7636_resume(struct device *dev)
{
	struct i2c_client *client = i2c_verify_client(dev);    
	struct sy7636 *sy7636 = i2c_get_clientdata(client);
	int iChk;
	unsigned int dwReg;
	
	if(gSleep_Mode_Suspend) {
		iChk = sy7636_chip_power(sy7636,1);
	}


	//gpio_set_value(sy7636->gpio_pmic_vcom_ctrl,0);

	if(PMIC_SUCCESS==sy7636_reg_read(sy7636,REG_SY7636_OPMODE,&dwReg)) {
		sy7636->bOPMode = (unsigned char)dwReg;
		DBG_MSG("%s() : OP=0x%x,RailsEN=%d,RailsDisable=0x%x\n",__FUNCTION__,
			sy7636->bOPMode,(int)BITFEXT(dwReg,RAILS_ON),BITFEXT(dwReg,RAILS_DISABLE));
	}
	else {
		return -1;
	}



	return 0;
}

/* Return 0 if detection is successful, -ENODEV otherwise */
static int sy7636_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	//struct sy7636_platform_data *pdata = client->dev.platform_data;
	struct i2c_adapter *adapter = client->adapter;
	struct sy7636 *sy7636 = i2c_get_clientdata(client);

	const int iMaxRetryCnt = 100;
	int iRetryN;
	int iIsDeviceReady;

	int iStatus ;

	printk("sy7636_detect calling\n");

	if(1!=gpio_get_value(sy7636->gpio_pmic_powerup)) 
	{
		dev_info(&client->dev, "EN is off, turnning on ...\n");
		gpio_set_value(sy7636->gpio_pmic_powerup,1);//sy7636->gpio_pmic_powerup_stat = 1;
		mdelay(2);
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&adapter->dev,"I2C adapter error! \n");
		return -ENODEV;
	}

	for (iIsDeviceReady=0,iRetryN=1;iRetryN<=iMaxRetryCnt;iRetryN++)
	{
		/* identification */

		iStatus = i2c_smbus_read_byte_data(client,REG_SY7636_OPMODE);
		if(iStatus>=0) {
			iIsDeviceReady = 1;
			sy7636->bOPMode = (unsigned char)iStatus;
			break;
		}
		else {
			msleep(2);
			dev_err(&adapter->dev,
					"Device probe no ACK , retry %d/%d ... \n",iRetryN,iMaxRetryCnt);
		}
	}

	if(!iIsDeviceReady) {
		dev_err(&adapter->dev,
		    "Device no ACK and retry %d times failed \n",iMaxRetryCnt);
		return -ENODEV;
	}

	printk("%s():opmode=0x%x\n",__FUNCTION__,sy7636->bOPMode);


	if (info) {
		strlcpy(info->type, "sy7636_sensor", I2C_NAME_SIZE);
	}

	printk("sy7636_detect success\n");
	return 0;
}

static const struct i2c_device_id sy7636_id[] = {
	{ "sy7636", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sy7636_id);

static const struct of_device_id sy7636_dt_ids[] = {
	{
		.compatible = "Silergy,sy7636",
		.data = (void *) &sy7636_id[0],
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, sy7636_dt_ids);

static const struct dev_pm_ops sy7636_dev_pm= {
	.suspend_late = sy7636_suspend_late,
	.resume_early = sy7636_resume_early,
	.suspend = sy7636_suspend,
	.resume = sy7636_resume,
};


static struct i2c_driver sy7636_driver = {
	.driver = {
		   .name = "sy7636",
		   .owner = THIS_MODULE,
		   .of_match_table = sy7636_dt_ids,
		   .pm = (&sy7636_dev_pm),
	},
	.probe = sy7636_probe,
	.remove = sy7636_remove,
	.id_table = sy7636_id,
	.detect = sy7636_detect,
	.address_list = &normal_i2c[0],
};

static int __init sy7636_init(void)
{
	return i2c_add_driver(&sy7636_driver);
}

static void __exit sy7636_exit(void)
{
	i2c_del_driver(&sy7636_driver);
}



/*
 * Module entry points
 */
subsys_initcall(sy7636_init);
module_exit(sy7636_exit);
