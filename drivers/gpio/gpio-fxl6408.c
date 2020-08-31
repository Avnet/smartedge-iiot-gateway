/*
 *  Copyright (C) 2016 Broadcom Limited.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 */

/**
 * DOC: FXL6408 I2C to GPIO expander.
 *
 * This chip has has 8 GPIO lines out of it, and is controlled by an
 * I2C bus (a pair of lines), providing 4x expansion of GPIO lines.
 * It also provides an interrupt line out for notifying of
 * statechanges.
 *
 * Any preconfigured state will be left in place until the GPIO lines
 * get activated.  At power on, everything is treated as an input.
 *
 * Documentation can be found at:
 * https://www.fairchildsemi.com/datasheets/FX/FXL6408.pdf
 */
#define DEBUG
#include <linux/module.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/of_platform.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/stat.h>
#include <linux/fcntl.h>


#define FXL6408_DEVICE_ID		0x01
#define FXL6408_RST_INT		BIT(1)
#define FXL6408_SW_RST			BIT(0)

/* Bits set here indicate that the GPIO is an output. */
#define FXL6408_IO_DIR			0x03
/* Bits set here, when the corresponding bit of IO_DIR is set, drive
 * the output high instead of low.
 */
#define FXL6408_OUTPUT			0x05
/* Bits here make the output High-Z, instead of the OUTPUT value. */
#define FXL6408_OUTPUT_HIGH_Z		0x07
/* Bits here define the expected input state of the GPIO.
 * INTERRUPT_STAT bits will be set when the INPUT transitions away
 * from this value.
 */
#define FXL6408_INPUT_DEFAULT_STATE	0x09
/* Bits here enable either pull up or pull down according to
 * FXL6408_PULL_DOWN.
 */
#define FXL6408_PULL_ENABLE		0x0b
/* Bits set here (when the corresponding PULL_ENABLE is set) enable a
 * pull-up instead of a pull-down.
 */
#define FXL6408_PULL_UP			0x0d
/* Returns the current status (1 = HIGH) of the input pins. */
#define FXL6408_INPUT_STATUS		0x0f
/* Mask of pins which can generate interrupts. */
#define FXL6408_INTERRUPT_MASK		0x11
/* Mask of pins which have generated an interrupt.  Cleared on read. */
#define FXL6408_INTERRUPT_STAT		0x13

struct fxl6408_chip {
	struct gpio_chip gpio_chip;
	struct i2c_client *client;
	struct mutex i2c_lock;
        struct mutex irq_lock; 
};


struct of_properties {
  char *property;
  u8 reg;
};

struct of_properties properties[] = {
  {"io-direction", FXL6408_IO_DIR },
  {"io-output", FXL6408_OUTPUT},
  {"io-output-high-z", FXL6408_OUTPUT_HIGH_Z},
  {"io-input-default-state", FXL6408_INPUT_DEFAULT_STATE},
  {"io-pull-enable", FXL6408_PULL_ENABLE},
  {"io-pull-up", FXL6408_PULL_UP},
  {"io-irq-enable", FXL6408_INTERRUPT_MASK}
};

u8 toggle_default_state = 0;

static void fxl6408_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct fxl6408_chip *chip = gpiochip_get_data(gc);
	unsigned int pos = d->hwirq & 7;
	u8 reg;
        
	reg = i2c_smbus_read_byte_data(chip->client, FXL6408_INTERRUPT_MASK);
	reg |= BIT(pos);
	i2c_smbus_write_byte_data(chip->client, FXL6408_INTERRUPT_MASK, reg);
}

static void fxl6408_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct fxl6408_chip *chip = gpiochip_get_data(gc);
	unsigned int pos = d->hwirq & 7;
	u8 reg;
        
	reg = i2c_smbus_read_byte_data(chip->client, FXL6408_INTERRUPT_MASK);
	reg &= ~BIT(pos);
	i2c_smbus_write_byte_data(chip->client, FXL6408_INTERRUPT_MASK, reg);
}


static int fxl6408_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct fxl6408_chip *chip = gpiochip_get_data(gc);
	unsigned int pos = d->hwirq & 7;
	// rework to set default state
        u8 reg;
        u8 current_input = 0;
        reg = i2c_smbus_read_byte_data(chip->client, FXL6408_INPUT_DEFAULT_STATE);
	if (type == IRQ_TYPE_LEVEL_HIGH)
          {
		reg &= ~BIT(pos);
          }      
	if (type == IRQ_TYPE_EDGE_RISING)
          {
		reg &= ~BIT(pos);
          }      
	if (type == IRQ_TYPE_LEVEL_LOW)
          {
		reg |= BIT(pos);
         }
  	if (type == IRQ_TYPE_EDGE_FALLING)
          {
		reg |= BIT(pos);
         }
         if ((type & IRQ_TYPE_EDGE_BOTH) == IRQ_TYPE_EDGE_BOTH)
          {
            current_input = i2c_smbus_read_byte_data(chip->client, FXL6408_INPUT_STATUS);
            if ((current_input & BIT(pos)) == 0)
              reg &= ~BIT(pos);
            else
              reg |= BIT(pos);
            toggle_default_state |= BIT(pos);

          }
        i2c_smbus_write_byte_data(chip->client, FXL6408_INPUT_DEFAULT_STATE, reg);
        
	reg = i2c_smbus_read_byte_data(chip->client, FXL6408_INTERRUPT_MASK);
	reg &= ~BIT(pos);
	i2c_smbus_write_byte_data(chip->client, FXL6408_INTERRUPT_MASK, reg);
	return 0;
}

static void fxl6408_irq_bus_lock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct fxl6408_chip *chip = gpiochip_get_data(gc);

	mutex_lock(&chip->irq_lock);
}

static void fxl6408_irq_bus_unlock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct fxl6408_chip *chip = gpiochip_get_data(gc);
        
	mutex_unlock(&chip->irq_lock);
}

static struct irq_chip fxl6408_irq_chip = {
	.name = "gpio-fxl6408",
	.irq_mask = fxl6408_irq_mask,
	.irq_unmask = fxl6408_irq_unmask,
	.irq_set_type = fxl6408_irq_set_type,
	.irq_bus_lock = fxl6408_irq_bus_lock,
	.irq_bus_sync_unlock = fxl6408_irq_bus_unlock,
};

static int fxl6408_gpio_direction_input(struct gpio_chip *gc, unsigned off)
{
	struct fxl6408_chip *chip = gpiochip_get_data(gc);
	u8 reg;
	mutex_lock(&chip->i2c_lock);
	reg = i2c_smbus_read_byte_data(chip->client, FXL6408_IO_DIR);
	reg &= ~BIT(off);
	i2c_smbus_write_byte_data(chip->client, FXL6408_IO_DIR,
				  reg);
        i2c_smbus_write_byte_data(chip->client, FXL6408_OUTPUT_HIGH_Z, ~reg);
        
	mutex_unlock(&chip->i2c_lock);
	return 0;
}

static int fxl6408_gpio_direction_output(struct gpio_chip *gc,
		unsigned off, int val)
{
struct fxl6408_chip *chip = gpiochip_get_data(gc);
        u8 reg;
	mutex_lock(&chip->i2c_lock);
	reg = i2c_smbus_read_byte_data(chip->client, FXL6408_IO_DIR);
	reg |= BIT(off);
	i2c_smbus_write_byte_data(chip->client, FXL6408_IO_DIR,
				  reg);
        i2c_smbus_write_byte_data(chip->client, FXL6408_OUTPUT_HIGH_Z, ~reg);
	mutex_unlock(&chip->i2c_lock);
	return 0;
}

static int fxl6408_gpio_get_direction(struct gpio_chip *gc, unsigned off)
{
        u8 reg;
	struct fxl6408_chip *chip = gpiochip_get_data(gc);
	reg = i2c_smbus_read_byte_data(chip->client, FXL6408_IO_DIR);

	return (reg & BIT(off)) == 0;
}

static int fxl6408_gpio_get_value(struct gpio_chip *gc, unsigned off)
{
	struct fxl6408_chip *chip = gpiochip_get_data(gc);
	u8 reg;
	mutex_lock(&chip->i2c_lock);
	reg = i2c_smbus_read_byte_data(chip->client, FXL6408_IO_DIR);
	if ((reg & BIT(off)) == BIT(off))
	  {
	    reg = i2c_smbus_read_byte_data(chip->client, FXL6408_OUTPUT);
	  }
	else
	  {
	    reg = i2c_smbus_read_byte_data(chip->client, FXL6408_INPUT_STATUS);
	  }
	mutex_unlock(&chip->i2c_lock);
	return (reg & BIT(off)) != 0;
}

static irqreturn_t fxl6408_irq(int irq, void *data)
{
	struct fxl6408_chip *chip = data;
        u8 reg;
        u8 level;
        u8 default_state;
        unsigned int bit;
        unsigned long pending;
	mutex_lock(&chip->i2c_lock);
	level = i2c_smbus_read_byte_data(chip->client, FXL6408_INPUT_STATUS);
        reg = i2c_smbus_read_byte_data(chip->client, FXL6408_INTERRUPT_STAT);
        default_state = i2c_smbus_read_byte_data(chip->client, FXL6408_INPUT_DEFAULT_STATE);
        level = reg;
	mutex_unlock(&chip->i2c_lock);
        pending = level;
        pending = pending & ((u8)(0xff));
      	/* mask out non-pending and disabled interrupts */
        for_each_set_bit(bit, &pending, 8) {
       		unsigned int child_irq;
                child_irq = irq_find_mapping(chip->gpio_chip.irqdomain,
						      bit);
       		handle_nested_irq(child_irq);
                if ((1 << bit) & toggle_default_state)
                  {
                    // Emulate both edges interrupts.
                    if ((1 << bit) & default_state)
                      default_state &= ~(1 << bit);
                    else
                      default_state |= (1 << bit);
                    i2c_smbus_write_byte_data(chip->client, FXL6408_INPUT_DEFAULT_STATE,
				  default_state);                    
                  }
        }
	return IRQ_HANDLED;
}


static void fxl6408_gpio_set_value(struct gpio_chip *gc, unsigned off, int val)
{
	struct fxl6408_chip *chip = gpiochip_get_data(gc);
	u8 reg;
	mutex_lock(&chip->i2c_lock);
	reg = i2c_smbus_read_byte_data(chip->client, FXL6408_OUTPUT);
	
	if (val)
		reg |= BIT(off);
	else
		reg &= ~BIT(off);
	i2c_smbus_write_byte_data(chip->client, FXL6408_OUTPUT,
				  reg);
	mutex_unlock(&chip->i2c_lock);
}


static void fxl6408_gpio_set_multiple(struct gpio_chip *gc,
		unsigned long *mask, unsigned long *bits)
{
	struct fxl6408_chip *chip = gpiochip_get_data(gc);
	u8 reg;
	mutex_lock(&chip->i2c_lock);
		reg = i2c_smbus_read_byte_data(chip->client, FXL6408_IO_DIR);

	reg = (reg & ~mask[0]) | bits[0];
	i2c_smbus_write_byte_data(chip->client, FXL6408_OUTPUT,
				  reg);
	mutex_unlock(&chip->i2c_lock);
}
static int fxl6408_irq_setup(struct fxl6408_chip *chip)
{
	struct gpio_chip *gpiochip = &chip->gpio_chip;
	int err;
        
	mutex_init(&chip->irq_lock);

	/*
	 * Allocate memory to keep track of the current level and trigger
	 * modes of the interrupts. To avoid multiple allocations, a single
	 * large buffer is allocated and pointers are setup to point at the
	 * corresponding offsets. For consistency, the layout of the buffer
	 * is chosen to match the register layout of the hardware in that
	 * each segment contains the corresponding bits for all interrupts.
	 */
	err = gpiochip_irqchip_add_nested(gpiochip,
					  &fxl6408_irq_chip,
					  0,
					  handle_level_irq,
					  IRQ_TYPE_NONE);
	if (err) {
          dev_err(gpiochip->parent,
          "could not connect irqchip to gpiochip\n");
		return err;
	}
	err = devm_request_threaded_irq(gpiochip->parent, chip->client->irq,
					NULL, fxl6408_irq,
					IRQF_SHARED|IRQF_ONESHOT|IRQF_TRIGGER_LOW,
					dev_name(gpiochip->parent), chip);
        
	if (err != 0) {
		dev_err(gpiochip->parent, "can't request IRQ#%d: %d\n",
			chip->client->irq, err);
		return err;
	}

	gpiochip_set_nested_irqchip(gpiochip, &fxl6408_irq_chip, chip->client->irq);
	return 0;
}
          
static int fxl6408_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device_node *np = client->dev.of_node;
	struct device *dev = &client->dev;
	struct fxl6408_chip *chip;
	struct gpio_chip *gc;
	int ret;
	u8 device_id;
        int err;
        u32 override;
        const char *string;
        int i;
        char line_name[128];
        
	/* Check the device ID register to see if it's responding.
	 * This also clears RST_INT as a side effect, so we won't get
	 * the "we've been power cycled" interrupt once we enable
	 * interrupts.
	 */
	device_id = i2c_smbus_read_byte_data(client, FXL6408_DEVICE_ID);
	if (device_id < 0) {
		dev_err(dev, "FXL6408 probe returned %d\n", device_id);
		return device_id;
	} else if (device_id >> 5 != 5) {
		dev_err(dev, "FXL6408 probe returned DID: 0x%02x\n", device_id);
		return -ENODEV;
	}
	chip = devm_kzalloc(dev, sizeof(struct fxl6408_chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	chip->client = client;
	mutex_init(&chip->i2c_lock);
        
	gc = &chip->gpio_chip;
	gc->direction_input  = fxl6408_gpio_direction_input;
	gc->direction_output = fxl6408_gpio_direction_output;
	gc->get_direction = fxl6408_gpio_get_direction;
	gc->get = fxl6408_gpio_get_value;
	gc->set = fxl6408_gpio_set_value;
	gc->set_multiple = fxl6408_gpio_set_multiple;
	gc->can_sleep = true;
        gc->base = -1;
	gc->ngpio = 8;
        // Check for device tree specified base and use this.
	if (of_find_property(np, "gpio-base", NULL)) {
          of_property_read_u32(np, "gpio-base", &gc->base);
        }
        for(i = 0; i < 7; i++)
          {
            // Check for chip initial condition overrides.
            if (of_find_property(np, properties[i].property, NULL)) {
              if (!of_property_read_u32(np, properties[i].property ,&override))
                {
                  i2c_smbus_write_byte_data(client, properties[i].reg, override);
                }
            }
          }
                
	gc->label = chip->client->name;
	gc->parent = dev;
	gc->owner = THIS_MODULE;
	if (of_find_property(np, "interrupt-controller", NULL)) {
		err = fxl6408_irq_setup(chip);
		if (err)
			return err;
	}

	ret = gpiochip_add_data(gc, chip);
	if (ret)
          return ret;

	i2c_set_clientdata(client, chip);

        // get line names and export them
        for(i=0; i <8; i++)
          {
            sprintf(line_name, "p%d-line-name", i + gc->base);
            if (!of_property_read_string(np, line_name, &string))
              {
                if(!gpio_request(gc->base+i, string))
                  {
                    struct gpio_desc *desc;
                    desc = gpio_to_desc(gc->base+i);
                    gpiod_export(desc, true);
                    gpiod_export_link(dev, string, desc);
                  }
              }            
          }
        return 0;
}
        



static int fxl6408_remove(struct i2c_client *client)
{
	struct fxl6408_chip *chip = i2c_get_clientdata(client);

	gpiochip_remove(&chip->gpio_chip);

	return 0;
}

static const struct of_device_id fxl6408_dt_ids[] = {
	{ .compatible = "fcs,fxl6408" },
	{ }
};

MODULE_DEVICE_TABLE(of, fxl6408_dt_ids);

static const struct i2c_device_id fxl6408_id[] = {
	{ "fxl6408", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, fxl6408_id);

static struct i2c_driver fxl6408_driver = {
	.driver = {
		.name	= "fxl6408",
		.of_match_table = fxl6408_dt_ids,
	},
	.probe		= fxl6408_probe,
	.remove		= fxl6408_remove,
	.id_table	= fxl6408_id,
};

module_i2c_driver(fxl6408_driver);

MODULE_AUTHOR("Eric Anholt <eric at anholt.net>");
MODULE_DESCRIPTION("GPIO expander driver for FXL6408");
MODULE_LICENSE("GPL");

