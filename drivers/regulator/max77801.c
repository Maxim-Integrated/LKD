/*
 * MAX77801 High Performance Power Management Unit : System Interface Driver
 * (based on rev. 0.01)
 * Copyright 2018 Maxim Integreated
 *
 * Author: Brandon(Hyung sik) <Brandon.shin@maximintegrated.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/regmap.h>
#include <linux/uaccess.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#if defined(CONFIG_OF)
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#endif /* CONFIG_OF */
#undef  __CONST_FFS
#define __CONST_FFS(_x) \
	((_x) & 0x0F ? \
	 ((_x) & 0x03 ? ((_x) & 0x01 ? 0 : 1) : ((_x) & 0x04 ? 2 : 3)) : \
	 ((_x) & 0x30 ? ((_x) & 0x10 ? 4 : 5) : ((_x) & 0x40 ? 6 : 7)))

#undef FFS
#define FFS(_x) \
	((_x) ? __CONST_FFS(_x) : 0)

#undef  BIT_RSVD
#define BIT_RSVD  0

#undef  BITS
#define BITS(_end, _start) \
	((BIT(_end) - BIT(_start)) + BIT(_end))

#undef  __BITS_GET
#define __BITS_GET(_word, _mask, _shift) \
	(((_word) & (_mask)) >> (_shift))

#undef  BITS_GET
#define BITS_GET(_word, _bit) \
	__BITS_GET(_word, _bit, FFS(_bit))

#undef  __BITS_SET
#define __BITS_SET(_word, _mask, _shift, _val) \
	(((_word) & ~(_mask)) | (((_val) << (_shift)) & (_mask)))

#undef  BITS_SET
#define BITS_SET(_word, _bit, _val) \
	__BITS_SET(_word, _bit, FFS(_bit), _val)

#undef  BITS_MATCH
#define BITS_MATCH(_word, _bit) \
	(((_word) & (_bit)) == (_bit))


/*
 * Register address
 */
#define REG_DEVICE_ID			0x0
#define REG_STATUS				0x1
#define REG_CONFIG1				0x2
#define REG_CONFIG2				0x3
#define REG_VOUT_DVS_L			0x4
#define REG_VOUT_DVS_H			0x5

/*
 * REG_DEVICE_ID
 */
#define BIT_VERSION				BITS(6, 3)
#define BIT_CHIP_REV			BITS(2, 0)

/*
 * REG_STATUS
 */
#define BIT_TSHDN				BIT(4)
#define BIT_BB_POKn				BIT(3)
#define BIT_BB_OVP				BIT(1)
#define BIT_BB_OCP				BIT(0)


#define BIT_BB_EN				BIT(6)
#define BIT_BST_VOUT			BITS(6,0)


#define MAX77801_BUCK_LINEAR_OUT_MAX	0x1


#define MAX77801_NAME			"max77801"


#define MAX77801_MIN_uV			2600000
#define MAX77801_MAX_uV			4187500
#define MAX77801_VOL_STEP		12500



/* MAX77801 regulator IDs */
enum max77801_regulators {
	MAX77801_BUCK1 = 0,
	MAX77801_REG_MAX,
};


struct max77801_chip { 
	struct device *dev;
	struct regmap *regmap;
	unsigned int irqmask;
	struct regulator_consumer_supply buck_consumer;
	struct regulator_dev *rdev[MAX77801_BUCK_LINEAR_OUT_MAX];
};

struct voltage_map_desc {
	int min;
	int max;
	int step;
	unsigned int n_bits;
};

				 
#if LINUX_VERSION_CODE > KERNEL_VERSION(3,6,0)
#else
static int max77801_update_bits(struct max77801_chip *pchip, unsigned int reg,
				 unsigned int mask, unsigned int val)
{
   return regmap_update_bits(pchip->regmap, reg, mask, val);
}

static int max77801_read(struct max77801_chip *pchip, unsigned int reg,
		       unsigned int *val)
{
	return regmap_read(pchip->regmap, reg, val);
}


static struct voltage_map_desc buck_voltage_map_desc = {
	.min = MAX77801_MIN_uV,
	.max = MAX77801_MAX_uV,
	.step = MAX77801_VOL_STEP,
	.n_bits = 7,
};


static struct voltage_map_desc *reg_voltage_map[] = {
	[MAX77801_BUCK1] = &buck_voltage_map_desc,
};

static int max77801_list_voltage(struct regulator_dev *rdev,
	 unsigned int selector)
{
	const struct voltage_map_desc *desc;
	int rid = rdev_get_id(rdev);
	int val;

	if (rid >= ARRAY_SIZE(reg_voltage_map) ||
		 rid < 0)
	 return -EINVAL;


	desc = reg_voltage_map[rid];
	if (desc == NULL)
	 return -EINVAL;


	val = desc->min + desc->step * selector;
	if (val > desc->max)
	 return -EINVAL;

	pr_info("%s:  id=%d, val=%d\n",
	   __func__, rdev_get_id(rdev), val);

	return val;
}

static inline int max77801_get_voltage_proper_val(
	 const struct voltage_map_desc *desc,
	 int min_vol, int max_vol)
{
	int i = 0;

	if (desc == NULL)
	 return -EINVAL;

	if (max_vol < desc->min || min_vol > desc->max)
	 return -EINVAL;

	while (desc->min + desc->step * i < min_vol &&
		 desc->min + desc->step * i < desc->max)
	 i++;

	if (desc->min + desc->step * i > max_vol)
	 return -EINVAL;

	if (i >= (1 << desc->n_bits))
	 return -EINVAL;

	return i;
}

static int max77801_set_voltage(struct regulator_dev *rdev,
	 int min_uV, int max_uV, unsigned *selector)
{
	 struct max77801_chip *max77801 = rdev_get_drvdata(rdev);
	 const struct voltage_map_desc *desc;
	 int rid = rdev_get_id(rdev);
	 int reg, ret;
	 int i = 0;
	 int org;

	 desc = reg_voltage_map[rid];

	 i = max77801_get_voltage_proper_val(desc, min_uV, max_uV);
	 if (i < 0)
		 return i;


	 reg = REG_VOUT_DVS_L;
	 /* TODO: If GPIO-LOW is being used, this won't work. */
	 max77801_read(max77801, reg, &org);

	 pr_info("%s:  id=%d, reg=%x, org=%x, val=%x\n",
	 	__func__, rdev_get_id(rdev), reg, org, i);

	 ret = max77801_update_bits(max77801, reg, 0x7f, i);
	 
	 *selector = i;

	 return ret;
}

static int max77801_get_voltage(struct regulator_dev *rdev)
{
	 struct max77801_chip *max77801 = rdev_get_drvdata(rdev);
	 int reg, ret;
	 int rid = rdev_get_id(rdev);
	 int val;

	 reg = REG_VOUT_DVS_L;	 
	 ret = max77801_read(max77801, reg, &val);
	 if (ret < 0)
		 return ret;
	 
	 val &= BIT_BST_VOUT;
	 pr_info("%s: id=%d, reg=%x, val=%x\n",
		 __func__, rid, reg, val);

	 return max77801_list_voltage(rdev, val);
}


static int max77801_reg_is_enabled(struct regulator_dev *rdev)
{
	struct max77801_chip *max77801 = rdev_get_drvdata(rdev);
	int ret, val;

	ret = max77801_read(max77801, REG_CONFIG2, &val);
	if (ret < 0)
		return ret;
	pr_info("%s: reg=%x, val=%x\n",
		__func__, REG_CONFIG2, val);
	
	ret = (val & BIT_BB_EN) >> FFS(BIT_BB_EN);
	return ret;
}


static int max77801_reg_enable(struct regulator_dev *rdev)
{
	struct max77801_chip *max77801 = rdev_get_drvdata(rdev);
	int val;
	val = BIT_BB_EN;
	return max77801_update_bits(max77801, REG_CONFIG2, BIT_BB_EN, val);
}

static int max77801_reg_disable(struct regulator_dev *rdev)
{
	struct max77801_chip *max77801 = rdev_get_drvdata(rdev);
	int val;
	val = 0x0;	
	return max77801_update_bits(max77801, REG_CONFIG2, BIT_BB_EN, val);
}

static int max77801_reg_do_nothing(struct regulator_dev *rdev)
{
	return 0;
}
#endif

static struct regulator_ops max77801_buck_ops = {
#if LINUX_VERSION_CODE > KERNEL_VERSION(3,6,0)
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.is_enabled 	= regulator_is_enabled_regmap,
	.enable 		= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
#else
	.list_voltage		= max77801_list_voltage,
	.is_enabled		= max77801_reg_is_enabled,
	.enable			= max77801_reg_enable,
	.disable		= max77801_reg_disable,
	.get_voltage		= max77801_get_voltage,
	.set_voltage		= max77801_set_voltage,
	/* Interpret suspend_enable as "keep on if it was enabled." */
	.set_suspend_enable	= max77801_reg_do_nothing,
	.set_suspend_disable	= max77801_reg_do_nothing,
#endif
};



static struct regulator_desc regulators[] = {
	{
		.name = "BUCK",
		.id = MAX77801_BUCK1,
		.ops = &max77801_buck_ops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.n_voltages = BIT_BST_VOUT + 1,
#if LINUX_VERSION_CODE > KERNEL_VERSION(3,6,0)
		.min_uV		= MAX77801_MIN_uV,
		.uV_step	= MAX77801_VOL_STEP,
		.vsel_reg	= REG_VOUT_DVS_L,
		.vsel_mask	= BIT_BST_VOUT,
		.enable_reg = REG_CONFIG2,
		.enable_mask	= BIT_BB_EN,
#endif
	},
};

static const struct regmap_config max77801_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xFF,
};

static struct regulator_init_data max77801_reg_data = {
		.constraints	= {
				.name			= "BUCK",
				.min_uV 		= MAX77801_MIN_uV,
				.max_uV 		= MAX77801_MAX_uV,
				.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
										REGULATOR_CHANGE_STATUS,
				.valid_modes_mask = REGULATOR_MODE_NORMAL,
		},
		.num_consumer_supplies = 1,
};


static int max77801_regulator_init(struct max77801_chip *pchip)
{
	int ret;
#if LINUX_VERSION_CODE > KERNEL_VERSION(3,6,0)
	struct regulator_config rconfig;
	memset(&rconfig,0,sizeof(struct regulator_config));
	rconfig.dev = pchip->dev;	
	rconfig.regmap = pchip->regmap;
	rconfig.driver_data = pchip;
	rconfig.init_data = &max77801_reg_data;
	pchip->rdev[0] = regulator_register(&regulators[0], &rconfig);
#else
	pchip->rdev[0] = regulator_register(&regulators[0], pchip->dev,
							&max77801_reg_data,
							pchip, NULL);
#endif

	if (IS_ERR(pchip->rdev[0])) {
		ret = PTR_ERR(pchip->rdev[0]);
		pchip->rdev[0] = NULL;
		pr_info("%s: regulator init failed: buck.\n",__func__);
		return ret;
	}
	return 0;
}



static int max77801_i2c_probe(struct i2c_client *client,
			 const struct i2c_device_id *dev_id)
{
	struct max77801_chip *pchip;
	int ret = 0;


	pr_info("%s: calling.\n",__func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_info("%s: i2c functionality check fail.\n",__func__);
		return -EOPNOTSUPP;
	}

	pchip =  devm_kzalloc(&client->dev,
			     sizeof(struct max77801_chip), GFP_KERNEL);
	if(!pchip)
		return -ENOMEM;
	
	pchip->dev = &client->dev;

	pchip->regmap = devm_regmap_init_i2c(client, &max77801_regmap);
	if (IS_ERR(pchip->regmap)) {
		ret = PTR_ERR(pchip->regmap);
		pr_info("%s: fail to allocate regmap %d .\n",__func__, ret);
		return ret;
	}

	i2c_set_clientdata(client, pchip);
	pchip->buck_consumer.dev_name = dev_name(pchip->dev);
	pchip->buck_consumer.supply = "BUCK";
	max77801_reg_data.consumer_supplies = &pchip->buck_consumer;

	ret = max77801_regulator_init(pchip);
	if (ret < 0) {
		pr_info("%s: fail to initialize regulators.\n",__func__);
		goto err;
	}
	
	return 0;	
err:
	if (pchip->rdev[0])
		regulator_unregister(pchip->rdev[0]);	
	kfree(pchip);
	return ret;

}

 #if defined(CONFIG_OF)
 static const struct i2c_device_id max77801_buck_id[] = {
 		 { "max77801", 0 },
 		 {}
 };
 static const struct of_device_id max77801_match[] = {
 		 { .compatible = "max77801" },
 		 { }
 };
 #endif /* CONFIG_OF */


 static int max77801_remove(struct i2c_client *client)
 {
	struct max77801_chip *pchip = i2c_get_clientdata(client);
	struct regulator_dev **rdev = pchip->rdev;

	if (rdev[0])
		regulator_unregister(rdev[0]); 

 	 return 0;
 }

static struct i2c_driver max77801_i2c_driver = {
 .driver = {
		.name = MAX77801_NAME,
#if defined(CONFIG_OF)
        .of_match_table = max77801_match,
#endif /* CONFIG_OF */			
		},
 .probe = max77801_i2c_probe,
 .remove = max77801_remove, 
 .id_table = max77801_buck_id,  
};
				 
static int __init max77801_init(void)
{
 return i2c_add_driver(&max77801_i2c_driver);
}

subsys_initcall(max77801_init);

static void __exit max77801_exit(void)
{
 i2c_del_driver(&max77801_i2c_driver);
}

module_exit(max77801_exit);

MODULE_DESCRIPTION("Maxim integrated max77801 driver");
MODULE_AUTHOR("Brandon Shin <Brandon.shin@maximintegrated.com>");
MODULE_LICENSE("GPL");

