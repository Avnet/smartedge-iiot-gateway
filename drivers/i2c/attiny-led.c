#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>

#include "attiny.h"

/* Set LED brightness level
 * Must not sleep. Use brightness_set_blocking for drivers
 * that can sleep while setting brightness.
 */
static void attiny_led_set(struct led_classdev *cled,
			   enum led_brightness value)
{
	struct attiny_dev *attiny = dev_get_drvdata(cled->dev->parent);
	u8 id = cled->name[0] == 'r' ? 0x20 : 0x10;
	u8 led_status;

	attiny->lock(attiny);
	attiny->read(attiny, I2C_LED_STATE, &led_status);
	if (value)
		led_status |= id;
	else
		led_status &= ~id;
	// Need to deactivate blinking if value == LED_OFF
	attiny->write(attiny, I2C_LED_STATE, led_status);
	attiny->write(attiny, I2C_LED_DUTY, 0x7F);
	attiny->unlock(attiny);
}


#if 1
/*
 * Activate hardware accelerated blink, delays are in milliseconds
 * and if both are zero then a sensible default should be chosen.
 * The call should adjust the timings in that case and if it can't
 * match the values specified exactly.
 * Deactivate blinking again when the brightness is set to LED_OFF
 * via the brightness_set() callback.
 */
static int attiny_led_blink(struct led_classdev *cled,
			    unsigned long *delay_on,
			    unsigned long *delay_off)
{
	struct attiny_dev *attiny = dev_get_drvdata(cled->dev->parent);
	
	pr_err("%s: on %ld, off %ld led %s\n", __func__, *delay_on, *delay_off, cled->name);
	// Rates are Off, 1Hz, 2Hz, 4Hz, 8Hz, and On
	// So times in ms are 1000, 500, 250, 128
	// DUTY is ratio of on/off times 0..255
	// So.  _on + _off should be to the closest rate?
	// and _off/_on should be to the closest DUTY?

	attiny->lock(attiny);
	// attiny->write(attiny, I2C_LED_STATE, ??);
	// attiny->write(attiny, I2C_LED_DUTY, ??);
	attiny->unlock(attiny);
	return 0;
}
#else
#define attiny_led_blink 0
#endif


// "devicename:color:function"
struct led_classdev attiny_led_red = {
	.name           = "red",
	.brightness_set = attiny_led_set,
	.blink_set      = attiny_led_blink,
};

struct led_classdev attiny_led_green = {
	.name           = "green",
	.brightness_set = attiny_led_set,
	.blink_set      = attiny_led_blink,
};

static int attiny_led_probe(struct platform_device *pdev)
{
	struct attiny_dev *attiny = dev_get_drvdata(pdev->dev.parent);
	int ret;
	
	ret = led_classdev_register(pdev->dev.parent, &attiny_led_red);
	if (ret < 0)
		return ret;
	ret = led_classdev_register(pdev->dev.parent, &attiny_led_green);
	if (ret < 0)
		return ret;

	attiny->lock(attiny);
	attiny->write(attiny, I2C_LED_STATE, 0x07);  // Solid
	attiny->write(attiny, I2C_LED_DUTY, 0x7F);   // 50 %
	attiny->unlock(attiny);
	return 0;
}

static int attiny_led_remove(struct platform_device *pdev)
{
	led_classdev_unregister(&attiny_led_green);
	led_classdev_unregister(&attiny_led_red);
 	return 0;
}

static struct platform_driver attiny_led_driver = {
	.driver = {
		.name = "attiny-led",
	},
	.probe = attiny_led_probe,
	.remove = attiny_led_remove,
};

module_platform_driver(attiny_led_driver);

MODULE_AUTHOR("Dale P. Smith <dales@avid-tech.com>");
MODULE_DESCRIPTION("ATtiny LED driver");
MODULE_LICENSE("GPL");
