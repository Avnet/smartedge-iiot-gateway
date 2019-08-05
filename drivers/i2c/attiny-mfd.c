#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/mfd/core.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/delay.h>

#include <linux/regmap.h>

#include "attiny.h"
struct mutex mutex;

static const struct mfd_cell attiny_devs[] = {
	{ .name = "attiny-wdt", },
	{ .name = "attiny-led", },
	{ .name = "attiny-btn", },
};

static const char *sterr(int er)
{
	switch (er) {
#define X(x) case x: return " (" #x ")"
		X(-EIO);      // 5
		X(-E2BIG);    // 7
		X(-EAGAIN);   // 11
		X(-ENOMEM);   // 12
		X(-EBUSY);    // 16
		X(-EINVAL);   // 22
		X(-ENOTSUPP); // 
	}
	return "";
}

static void attiny_nop(struct attiny_dev *attiny)
{
}
static void attiny_lock(struct attiny_dev *attiny)
{
  //printk("ATLOCK");
  mutex_lock(&mutex);
}
static void attiny_unlock(struct attiny_dev *attiny)
{
  //printk("ATUNLOCK");
  mutex_unlock(&mutex);
}

static int attiny_read(struct attiny_dev *attiny, u8 reg, u8 *data)
{
	int ret = 0;
	
#ifdef USE_PLATFORM_DEVICE
	pr_err("%s: reg %d\n", __func__, reg);
	return ret;
#else
	ret = i2c_smbus_read_byte_data(attiny->client, reg);
	if (ret < 0) {
		pr_err("%s: reg %d, ret %d%s\n", __func__, reg, ret, sterr(ret));
		return ret;
	}
	*data = (u8) ret;
	return 0;
#endif
}

static int attiny_write(struct attiny_dev *attiny, u8 reg, u8 data)
{
	int ret = 0;
	int retrys = 3;
#ifdef USE_PLATFORM_DEVICE
	pr_err("%s: reg %d, data %02X, ret %d\n", __func__, reg, data, ret);
#else
	ret = 1;
	while ((ret != 0) && (retrys !=0))
	  {
	    ret = i2c_smbus_write_byte_data(attiny->client, reg, data);
	    if (ret < 0)
		pr_err("%s: reg %d, ret %d%s\n", __func__, reg, ret, sterr(ret));
	    retrys--;
	    msleep(100);
	  }
#endif
	return ret;
}

#ifndef USE_PLATFORM_DEVICE

const struct regmap_config attiny_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = I2C_HW_REV,
};

static int attiny_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct attiny_dev *attiny;

	pr_err("%s: %s addr 0x%02x\n", __func__, id->name, client->addr);
	attiny = devm_kzalloc(&client->dev, sizeof *attiny, GFP_KERNEL);
	if (attiny == NULL)
		return -ENOMEM;

	i2c_set_clientdata(client, attiny);
	
	attiny->client = client;
	attiny->read = attiny_read;
	attiny->write = attiny_write;
	attiny->lock = attiny_lock;
	attiny->unlock = attiny_unlock;
	mutex_init(&mutex);
	return devm_mfd_add_devices(&client->dev, PLATFORM_DEVID_AUTO,
				    attiny_devs, ARRAY_SIZE(attiny_devs),
				    NULL, 0, NULL);
}

static const struct i2c_device_id attiny_i2c_id[] = {
	{ .name = "attiny" },
};

static const struct of_device_id attiny_of_ids[] = {
	{ .compatible = "avid,attiny" },
	{  }
};

static struct i2c_driver attiny_i2c_driver = {
	.probe = attiny_i2c_probe,
	.id_table = attiny_i2c_id,
	.driver = {
		.name = "attiny",
		.of_match_table = attiny_of_ids,
	},
	
};

static int __init attiny_init(void)
{
	int ret;
	ret = i2c_add_driver(&attiny_i2c_driver);
	if (ret != 0)
		pr_err("ATtiny registration failed %d\n", ret);
	
	return ret;
}

static void __exit attiny_exit(void)
{
	i2c_del_driver(&attiny_i2c_driver);
}

#else

static int attiny_probe(struct platform_device *pdev)
{
	struct attiny_dev *attiny;

	pr_err("%s:\n", __func__);
	attiny = devm_kzalloc(&pdev->dev, sizeof *attiny, GFP_KERNEL);
	if (attiny == NULL)
		return -ENOMEM;

	attiny->read = attiny_read;
	attiny->write = attiny_write;
	attiny->lock = attiny_nop;
	attiny->unlock = attiny_nop;
	//attiny->regmap = devm_regmap_init_i2c(client, &attiny_regmap);
	
	dev_set_drvdata(&pdev->dev, attiny);
	return devm_mfd_add_devices(&pdev->dev, PLATFORM_DEVID_AUTO,
				    attiny_devs, ARRAY_SIZE(attiny_devs),
				    NULL, 0, NULL);
}


#if 0
static int attiny_remove(struct platform_device *pdev)
{
	pr_err("%s: pdev: %p\n", __func__, pdev);
	return 0;
}
#else
#define attiny_remove 0
#endif

static struct platform_device *pd;

static struct platform_driver attiny_driver = {
	.driver = {
		.name = "attiny",
	},
	.probe = attiny_probe,
	.remove = attiny_remove,
};

static const struct platform_device_info attiny_dev_info = {
	.name = "attiny",
	.id   = -1,
};


static int __init attiny_init(void)
{
	int err;

	err = platform_driver_register(&attiny_driver);
	if (unlikely(err < 0))
		return err;

	pd = platform_device_register_full(&attiny_dev_info);
	return 0;
}

static void __exit attiny_exit(void)
{
  //platform_device_unregister(pd);
  //platform_driver_unregister(&attiny_driver);
}
#endif

module_init(attiny_init);
module_exit(attiny_exit);

MODULE_DESCRIPTION("AVID ATtiny multi-function driver");
MODULE_LICENSE("GPL");
