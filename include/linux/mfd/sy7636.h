/*
 * Copyright (C) 2010 Freescale Semiconductor, Inc. All Rights Reserved.
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
#ifndef __LINUX_REGULATOR_SY7636_H_
#define __LINUX_REGULATOR_SY7636_H_


#define SY7636_READY_MS		1

#define SY7636_VDROP_PROC_IN_KERNEL		1

/*
 * PMIC Register Addresses
 */
enum {
    REG_SY7636_OPMODE = 0x0,
    REG_SY7636_VCOM_ADJ1,
    REG_SY7636_VCOM_ADJ2,
    REG_SY7636_VLDO_ADJ,
    REG_SY7636_NA1,
    REG_SY7636_NA2,
    REG_SY7636_PWRON_DLY,
    REG_SY7636_FAULTFLAGS,
    REG_SY7636_THERM,
    SY7636_REG_NUM,
};

#define SY7636_MAX_REGISTER   0xFF

/*
 * Bitfield macros that use rely on bitfield width/shift information.
 */
#define BITFMASK(field) (((1U << (field ## _WID)) - 1) << (field ## _LSH))
#define BITFVAL(field, val) ((val) << (field ## _LSH))
#define BITFEXT(var, bit) ((var & BITFMASK(bit)) >> (bit ## _LSH))


/* OP MODE */
#define RAILS_ON_LSH 7
#define RAILS_ON_WID 1
#define VCOM_MANUAL_LSH 6
#define VCOM_MANUAL_WID 1
#define RESERVED_LSH 5
#define RESERVED_WID 1
#define VDDH_DISABLE_LSH 4
#define VDDH_DISABLE_WID 1
#define VEE_DISABLE_LSH 3
#define VEE_DISABLE_WID 1
#define VPOS_DISABLE_LSH 2
#define VPOS_DISABLE_WID 1
#define VNEG_DISABLE_LSH 1
#define VNEG_DISABLE_WID 1
#define VCOM_DISABLE_LSH 0
#define VCOM_DISABLE_WID 1
// all rails bits .
#define RAILS_DISABLE_LSH 0
#define RAILS_DISABLE_WID 5


/* VLDO voltage adjustment control */
#define VLDO_ADJ_LSH	5
#define VLDO_ADJ_WID	3

/* FAULT_FLAGS */
#define FAULTS_LSH 1
#define FAULTS_WID 4
#define PG_LSH 0
#define PG_WID 1

/* Power On Delay Time */
#define TDLY4_LSH 6
#define TDLY4_WID 2
#define TDLY3_LSH 4
#define TDLY3_WID 2
#define TDLY2_LSH 2
#define TDLY2_WID 2
#define TDLY1_LSH 0
#define TDLY1_WID 2

/*
 * VCOM Definitions
 *
 * The register fields accept voltages in the range 0V to -2.75V, but the
 * VCOM parametric performance is only guaranteed from -0.3V to -2.5V.
 */
#define SY7636_VCOM_MIN_uV   -5110000
#define SY7636_VCOM_MAX_uV          0
#define SY7636_VCOM_MIN_SET         0
#define SY7636_VCOM_MAX_SET       511
#define SY7636_VCOM_BASE_uV     10000
#define SY7636_VCOM_STEP_uV     10000



#define SY7636_VCOM_MIN_VAL         0
#define SY7636_VCOM_MAX_VAL       255

#define SY7636_WAKEUP_MS	3


struct regulator_init_data;

typedef void *(SY7636_INTEVT_CB)(int iEVENT);

struct sy7636 {
	/* chip revision */
	struct device *dev;
	struct sy7636_platform_data *pdata;

	/* Platform connection */
	struct i2c_client *i2c_client;

	/* Timings */
	unsigned char on_delay1;
	unsigned char on_delay2;
	unsigned char on_delay3;
	unsigned char on_delay4;

	unsigned int VLDO;

	unsigned char bFaultFlags,bOPMode;

	/* GPIOs */
	int gpio_pmic_pwrgood;
	int gpio_pmic_vcom_ctrl;
	int gpio_pmic_powerup;
	int gpio_pmic_pwrall;
	

	/* SY7636 part variables */
	int vcom_uV;

	/* One-time VCOM setup marker */
	bool vcom_setup;

	/* powerup/powerdown wait time */
	int max_wait;

	/* Dynamically determined polarity for PWRGOOD */
	int pwrgood_polarity;

	//int gpio_pmic_powerup_stat;
	//int gpio_pmic_pwrall_stat;
	int fake_vp3v3_stat;

	//int iNeedReloadVCOM; // VCOM must be loaded again .
	//unsigned regVCOM1,regVCOM2;
	
	int int_state; // interrupt state .
	SY7636_INTEVT_CB *pfnINTCB;

	struct work_struct int_work;
	struct workqueue_struct *int_workqueue;

};

enum {
    /* In alphabetical order */
    SY7636_DISPLAY, /* virtual master enable */
    SY7636_VCOM,
    SY7636_TMST,
		SY7636_VP3V3,
    SY7636_NUM_REGULATORS,
};

/*
 * Declarations
 */
struct regulator_init_data;
struct sy7636_regulator_data;

struct sy7636_platform_data {
	unsigned int on_delay1;
	unsigned int on_delay2;
	unsigned int on_delay3;
	unsigned int on_delay4;

	unsigned int VLDO;

	int gpio_pmic_pwrgood;
	int gpio_pmic_vcom_ctrl;
	int gpio_pmic_powerup;
	int gpio_pmic_pwrall;
	int vcom_uV;

	/* PMIC */
	struct sy7636_regulator_data *regulators;
	int num_regulators;
};

struct sy7636_regulator_data {
	int id;
	struct regulator_init_data *initdata;
	struct device_node *reg_node;
};

int sy7636_reg_write(struct sy7636 *sy7636,int reg_num, const unsigned int reg_val);
int sy7636_reg_read(struct sy7636 *sy7636,int reg_num, unsigned int *reg_val);
int sy7636_get_temperature(struct sy7636 *sy7636,int *O_piTemperature);
int sy7636_get_vcom(struct sy7636 *sy7636,int *O_piVCOMmV);
int sy7636_set_vcom(struct sy7636 *sy7636,int iVCOMmV,int iIsWriteToFlash);
//int sy7636_restore_vcom(struct sy7636 *sy7636);

int sy7636_int_state_clear(struct sy7636 *sy7636);
int sy7636_int_state_get(struct sy7636 *sy7636);

int sy7636_int_callback_setup(struct sy7636 *sy7636,SY7636_INTEVT_CB fnCB);

int sy7636_get_FaultFlags(struct sy7636 *sy7636,int iIsGetCached);
int sy7636_get_power_status(struct sy7636 *sy7636,int iIsGetCached,int *O_PG,unsigned char *O_pbFaults);

int sy7636_regulator_hwinit(struct sy7636 *sy7636);

#endif
