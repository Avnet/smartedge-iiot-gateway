#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/workqueue.h>

#include "attiny.h"

// Generate fake event every FAKE polls, or none if 0
#define FAKE 0

// Unblocked event read returns EOF when !0
#define READ_EOF 1

// milliseconds between polls
#define POLL_MS (500)

// Maybe "Access" and "Reset" instead?
enum { Button, Reset };


// press data.
struct press {
	int pressed;  // Make atomic?
	wait_queue_head_t queue;
};

// The drvdata is driver specific.  There is only one.
struct attiny_btn_drvdata {
	struct attiny_dev *attiny;
	struct timer_list timer;
	struct work_struct work;
	// These two are here instead of in struct btn becuse the
	// timer handler worker needs access to them.
	struct press press[2];
};

#define work_to_drvdata(x) container_of((x), struct attiny_btn_drvdata, work)

// A btn is an extension of the misc dev.  Points to the drvdata.
struct btn {
	int id;
	struct attiny_btn_drvdata *drvdata;
	struct miscdevice mdev;
};

#define to_btn(x) container_of((x), struct btn, mdev)


#if FAKE
int fake = 0;
#endif

static void worker_func(struct work_struct *work)
{
	struct attiny_btn_drvdata *drvdata = work_to_drvdata(work);
	struct attiny_dev * attiny = drvdata->attiny;
	u8 sticky = 0;
	u8 update = 0;

	attiny->lock(attiny);
	attiny->read(attiny, I2C_STICKY_BITS, &sticky);
#if FAKE
	if (!(++fake % FAKE))
		sticky |= I2C_STICKY_FACTORY_RST_SHORT | I2C_STICKY_FACTORY_RST_LONG;
#endif

	if (sticky & I2C_STICKY_FACTORY_RST_SHORT) {
		drvdata->press[Button].pressed = 1;
		update |= I2C_STICKY_FACTORY_RST_SHORT;
		wake_up_interruptible(&drvdata->press[Button].queue);
	}
	if (sticky & I2C_STICKY_FACTORY_RST_LONG) {
		drvdata->press[Reset].pressed = 1;
		update |= I2C_STICKY_FACTORY_RST_LONG;
		wake_up_interruptible(&drvdata->press[Reset].queue);
	}
	
	if (update)
		attiny->write(attiny, I2C_STICKY_BITS, update);
	attiny->unlock(attiny);
}

static void timer_func(struct timer_list *t)
{
	struct attiny_btn_drvdata *drvdata = from_timer(drvdata, t, timer);

	mod_timer(t, jiffies + msecs_to_jiffies(POLL_MS));
	schedule_work(&drvdata->work);
}

static ssize_t attiny_btn_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct miscdevice *md = file->private_data;
	struct btn *btn = to_btn(md);
	struct press *press = &btn->drvdata->press[btn->id];
#if ! READ_EOF
	static const char data[] = { '1', '\n' };
#endif

#if ! READ_EOF
	if (count < sizeof data)
		return -EINVAL;
#endif

	while (!press->pressed) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(press->queue, press->pressed))
			return -ERESTARTSYS;
	}

#if READ_EOF
	press->pressed = 0;
	return 0;
#else
	if (copy_to_user(buf, data, sizeof data))
		return -EFAULT;
	
	press->pressed = 0;
	*ppos += sizeof data; // Is this needed if we never seek?
	return sizeof data;
#endif
}

static unsigned int attiny_btn_poll(struct file *file, struct poll_table_struct *pt)
{
	struct miscdevice *me = file->private_data;
	struct btn *btn = to_btn(me);
	struct press *press = &btn->drvdata->press[btn->id];
	unsigned int mask = 0;
	
	poll_wait(file, &press->queue, pt);
	if (press->pressed)
		mask |= POLLIN | POLLRDNORM;
	return mask;
}

const struct file_operations attiny_btn_fops = {
	.owner		= THIS_MODULE,
	.read		= attiny_btn_read,
	.poll		= attiny_btn_poll,
	.llseek		= no_llseek,
};


// devm interface to this misc device
static void devm_misc_press_release(struct device *dev, void *res)
{
	struct btn *btn = res;

	misc_deregister(&btn->mdev);
}

static int devm_misc_press(struct device *dev, const char *name, int id)
{
	struct btn *btn;
	int err;

	btn = devres_alloc(devm_misc_press_release, sizeof *btn, GFP_KERNEL);
	if (!btn)
		return -ENOMEM;

	btn->drvdata = dev_get_drvdata(dev);
	
	btn->id = id;
	btn->mdev.minor = MISC_DYNAMIC_MINOR;
	btn->mdev.fops = &attiny_btn_fops;
	btn->mdev.name = name;
	btn->mdev.parent = dev;
	err = misc_register(&btn->mdev);
	if (err)
		devres_free(btn);

	devres_add(dev, btn);
	return err;
}

static int attiny_btn_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct attiny_btn_drvdata *drvdata;

	drvdata = devm_kzalloc(&pdev->dev, sizeof *drvdata, GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->attiny = dev_get_drvdata(pdev->dev.parent);

	timer_setup(&drvdata->timer, timer_func, 0);
	INIT_WORK(&drvdata->work, worker_func);

	init_waitqueue_head(&drvdata->press[Button].queue);
	init_waitqueue_head(&drvdata->press[Reset].queue);
	
	dev_set_drvdata(&pdev->dev, drvdata);

	ret = devm_misc_press(&pdev->dev, "button", Button);
	if (ret)
		return ret;

	ret = devm_misc_press(&pdev->dev, "reset", Reset);
	if (ret)
		return ret;

	mod_timer(&drvdata->timer, jiffies + msecs_to_jiffies(POLL_MS));
	return ret;
}

static int attiny_btn_remove(struct platform_device *pdev)
{
	struct attiny_btn_drvdata *drvdata;

	drvdata = dev_get_drvdata(&pdev->dev);
	del_timer(&drvdata->timer);
	cancel_work_sync(&drvdata->work);
	return 0;
}

static struct platform_driver attiny_btn_driver = {
	.driver = {
		.name = "attiny-btn",
	},
	.probe = attiny_btn_probe,
	.remove = attiny_btn_remove,
};

module_platform_driver(attiny_btn_driver);

MODULE_AUTHOR("Dale P. Smith <dales@avid-tech.com>");
MODULE_DESCRIPTION("ATtiny Button driver");
MODULE_LICENSE("GPL");
