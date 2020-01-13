#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/watchdog.h>
#include <uapi/linux/watchdog.h>

#include "attiny.h"
static int heartbeat = 45;

// The routine for starting the watchdog device.
static int attiny_wdt_start(struct watchdog_device *wdd)
{
	struct attiny_dev *attiny = watchdog_get_drvdata(wdd);
	int ret;

	attiny->lock(attiny);
	ret = attiny->write(attiny, I2C_WDT_TIME_RST, 45);
	if (ret)
	  dev_err(wdd->parent, "ATTiny Write failed");
	ret = attiny->write(attiny, I2C_WDT_CMDSTS, WDT_CMD_START);
	if (ret)
	  dev_err(wdd->parent, "ATTiny Write failed");
	ret = attiny->write(attiny, I2C_WDT_CMDSTS, WDT_CMD_ENA_RESET);
	if (ret)
	  dev_err(wdd->parent, "ATTiny Write failed");
	attiny->unlock(attiny);
	return ret;
}

// The routine for stopping the watchdog device.
static int attiny_wdt_stop(struct watchdog_device *wdd)
{
	struct attiny_dev *attiny = watchdog_get_drvdata(wdd);
	int ret;

	attiny->lock(attiny);
	ret = attiny->write(attiny, I2C_WDT_TIME_RST, 45);
	if (ret)
	  dev_err(wdd->parent, "ATTiny Write failed");
	ret = attiny->write(attiny, I2C_WDT_CMDSTS, WDT_CMD_STOP);
	if (ret)
	  dev_err(wdd->parent, "ATTiny Write failed");
	attiny->unlock(attiny);
	return ret;
}

// The routine that sends a keepalive ping to the watchdog device.
static int attiny_wdt_ping(struct watchdog_device *wdd)
{
	struct attiny_dev *attiny = watchdog_get_drvdata(wdd);
	int ret;
	return 0;
	attiny->lock(attiny);
	ret = attiny->write(attiny, I2C_WDT_CMDSTS, WDT_CMD_RESET);
	if (ret)
	  dev_err(wdd->parent, "ATTiny Write failed");
	attiny->unlock(attiny);
	return ret;
}


// The routine for setting the watchdog devices timeout value (in seconds).
static int attiny_wdt_set_timeout(struct watchdog_device *wdd, unsigned int timeout)
{
	struct attiny_dev *attiny = watchdog_get_drvdata(wdd);
	int ret;

	if (timeout > 255) {
		pr_err("%s: timeout %d to large. Max 255\n", __func__, timeout);
		return -EINVAL;
	}

	attiny->lock(attiny);
	ret = attiny->write(attiny, I2C_WDT_TIME_RST, 45);
	if (ret)
	  dev_err(wdd->parent, "ATTiny Write failed");
	attiny->unlock(attiny);
	return ret;
}

#if 1
// The routine for restarting the machine.
static int attiny_wdt_restart(struct watchdog_device *wdd, unsigned long action, void *data)
{
	struct attiny_dev *attiny = watchdog_get_drvdata(wdd);

	attiny->lock(attiny);
	attiny->write(attiny, I2C_WDT_CMDSTS, WDT_CMD_STOP);
	attiny->write(attiny, I2C_WDT_TIME_RST, 45);
	attiny->write(attiny, I2C_WDT_CMDSTS, WDT_CMD_START);
	attiny->unlock(attiny);
	return 0;
}
#else
#define attiny_wdt_restart 0
#endif


#if 1
static unsigned int attiny_wdt_status(struct watchdog_device *wdd)
{
	struct attiny_dev *attiny = watchdog_get_drvdata(wdd);
	u8 sticky = 0;
	u8 status = 0;
	unsigned int ret = 0;

	attiny->lock(attiny);
	attiny->read(attiny, I2C_WDT_CMDSTS, &status);
#if 0
	if (status & WDT_STATUS_TIMED_OUT_RST)
		ret |= ??;
#endif
	
	attiny->read(attiny, I2C_STICKY_BITS, &sticky);
	if (sticky & I2C_STICKY_PWR_LOW_DETECTED)
		ret |= WDIOF_POWERUNDER;
	if (sticky & I2C_STICKY_RESET_DETECTED)
		ret |= WDIOF_CARDRESET;
	attiny->unlock(attiny);
	return ret;
}
#else
#define attiny_wdt_status 0
#endif

static const struct watchdog_info attiny_wdt_info = {
	.options  = WDIOF_SETTIMEOUT
	           |  WDIOF_KEEPALIVEPING,
	.identity = "ATtiny Watchdog",
};

static const struct watchdog_ops attiny_wdt_ops = {
	.owner       = THIS_MODULE,
	.start       = attiny_wdt_start,
	.stop        = attiny_wdt_stop,
	.ping        = attiny_wdt_ping,
	.set_timeout = attiny_wdt_set_timeout,
	.status      = attiny_wdt_status,
	.restart     = attiny_wdt_restart,
};
#if 0
static struct watchdog_device attiny_wdt_wdd = {
	.info =		&attiny_wdt_info,
	.ops =		&attiny_wdt_ops,
	.min_timeout =	1,
	.max_timeout =	WDOG_TICKS_TO_SECS(PM_WDOG_TIME_SET),
	.timeout =	WDOG_TICKS_TO_SECS(PM_WDOG_TIME_SET),
};
#endif


int attiny_wdt_probe(struct platform_device *pdev)
{
	struct attiny_dev *attiny = dev_get_drvdata(pdev->dev.parent);
	struct watchdog_device *wdd;
	int err;

	wdd = devm_kzalloc(&pdev->dev, sizeof *wdd, GFP_KERNEL);
	if (!wdd)
		return -ENOMEM;

	wdd->info = &attiny_wdt_info;
	wdd->ops = &attiny_wdt_ops;
	wdd->min_timeout = 0;
	wdd->max_timeout = 255;
	wdd->timeout = 1;
	wdd->parent = &pdev->dev;
//        wdd->status = WATCHDOG_NOWAYOUT_INIT_STATUS;
	watchdog_set_drvdata(wdd, attiny);
	heartbeat = 45;
	watchdog_init_timeout(wdd, 45,&pdev->dev);
// can not stop watchdog.
	//watchdog_set_nowayout(wdd, 1);
	err = devm_watchdog_register_device(&pdev->dev, wdd);
	if (err) {
	  dev_err(&pdev->dev, "Failed to register watchdog attiny");
	  return err;
	}

	attiny_wdt_start(wdd);
        dev_err(&pdev->dev, "Watchdog ID %d", wdd->id);
	dev_err(&pdev->dev, "Attiny watchdog enabled");
	return 0;
}


static struct platform_driver attiny_wdt_driver = {
	.driver = {
		.name = "attiny-wdt",
	},
	.probe = attiny_wdt_probe,
};
MODULE_PARM_DESC(heartbeat, "Initial watchdog heartbeat in seconds");

//module_param(nowayout, bool, 1);
//MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
//				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

module_platform_driver(attiny_wdt_driver);
MODULE_AUTHOR("Dale P. Smith <dales@avid-tech.com>");
MODULE_DESCRIPTION("ATtiny Watchdog Driver");
MODULE_LICENSE("GPL");
