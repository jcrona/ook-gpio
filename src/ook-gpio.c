/*
 * GPIO-based OOK modulation driver
 *
 * Copyright (C) 2016 Jean-Christophe Rona <jc@rona.fr>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

#define DRV_NAME		"ook-gpio"
#define DRV_DESC		"GPIO-based OOK modulation driver"
#define DRV_VERSION		"0.2"

/* Hard-coded GPIO to use */
/* GPIO 7 is free on TP-Link WR703n */
#define OOK_GPIO_NR		7

enum bit_fmt {
	BIT_FMT_HL =		0,
	BIT_FMT_LH =		1,
	BIT_FMT_MAX
};

struct ook_gpio_platform_data {
	int gpio;
	unsigned int start_l;
	unsigned int start_h;
	unsigned int end_l;
	unsigned int end_h;
	unsigned int bit0_h;
	unsigned int bit0_l;
	unsigned int bit1_h;
	unsigned int bit1_l;
	enum bit_fmt fmt;
	unsigned int count;
};

struct platform_device *ook_gpio_pdev = NULL;

static void ook_send_zero(struct ook_gpio_platform_data *pdata)
{
	/* Send a logical 1 */
	if (pdata->fmt == BIT_FMT_HL) {
		gpio_set_value(pdata->gpio, 1);
		udelay(pdata->bit0_h);

		gpio_set_value(pdata->gpio, 0);
		udelay(pdata->bit0_l);
	} else if (pdata->fmt == BIT_FMT_LH) {
		gpio_set_value(pdata->gpio, 0);
		udelay(pdata->bit0_l);

		gpio_set_value(pdata->gpio, 1);
		udelay(pdata->bit0_h);
	}
}

static void ook_send_one(struct ook_gpio_platform_data *pdata)
{
	/* Send a logical 0 */
	if (pdata->fmt == BIT_FMT_HL) {
		gpio_set_value(pdata->gpio, 1);
		udelay(pdata->bit1_h);

		gpio_set_value(pdata->gpio, 0);
		udelay(pdata->bit1_l);
	} else if (pdata->fmt == BIT_FMT_LH) {
		gpio_set_value(pdata->gpio, 0);
		udelay(pdata->bit1_l);

		gpio_set_value(pdata->gpio, 1);
		udelay(pdata->bit1_h);
	}
}

static void ook_send_start(struct ook_gpio_platform_data *pdata)
{
	/* Send the starting marker */
	if (pdata->fmt == BIT_FMT_HL) {
		gpio_set_value(pdata->gpio, 1);
		udelay(pdata->start_h);

		gpio_set_value(pdata->gpio, 0);
		udelay(pdata->start_l);
	} else if (pdata->fmt == BIT_FMT_LH) {
		gpio_set_value(pdata->gpio, 0);
		udelay(pdata->start_l);

		gpio_set_value(pdata->gpio, 1);
		udelay(pdata->start_h);
	}
}

static void ook_send_end(struct ook_gpio_platform_data *pdata)
{
	/* Send the ending marker */
	if (pdata->fmt == BIT_FMT_HL) {
		gpio_set_value(pdata->gpio, 1);
		udelay(pdata->end_h);

		gpio_set_value(pdata->gpio, 0);
		udelay(pdata->end_l);
	} else if (pdata->fmt == BIT_FMT_LH) {
		gpio_set_value(pdata->gpio, 0);
		udelay(pdata->end_l);

		gpio_set_value(pdata->gpio, 1);
		udelay(pdata->end_h);
	}
}

static int ook_send_frame(struct ook_gpio_platform_data *pdata,
			unsigned char *frame, unsigned int bit_length)
{
	int i, j;
	spinlock_t lock;
	unsigned long flags;

	/* Do not allow anything else until the frame is fully sent
	 * TODO: switch to hrtimers, but that will do for now on
	 * light embedded systems
	 */
	spin_lock_irqsave(&lock, flags);

	for (j = 0; j < pdata->count; j++) {
		ook_send_start(pdata);

		for (i = 0; i < bit_length; i++) {
			if (frame[i/8] & (0x1 << (7 - (i % 8)))) {
				ook_send_one(pdata);
			} else {
				ook_send_zero(pdata);
			}
		}

		ook_send_end(pdata);

		/* Be sure the line is released once a frame has
		 * been sent (mainly for LH bit format)
		 */
		gpio_set_value(pdata->gpio, 0);
	}

	spin_unlock_irqrestore(&lock, flags);

	return 0;
}

static ssize_t show_timings(struct device *dev,
			struct device_attribute *devattr, char *buf)
{
	struct ook_gpio_platform_data *pdata = dev->platform_data;

	return sprintf(buf, "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
			pdata->start_h, pdata->start_l, pdata->end_h, pdata->end_l,
			pdata->bit0_h, pdata->bit0_l, pdata->bit1_h, pdata->bit1_l,
			(unsigned int) pdata->fmt, pdata->count);
}

static ssize_t store_timings(struct device *dev,
			struct device_attribute *devattr,
			const char *buf, size_t count)
{
	struct ook_gpio_platform_data *pdata = dev->platform_data;
	unsigned int bit_fmt;

	if (sscanf(buf, "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
			&pdata->start_h, &pdata->start_l, &pdata->end_h, &pdata->end_l,
			&pdata->bit0_h, &pdata->bit0_l, &pdata->bit1_h, &pdata->bit1_l,
			&bit_fmt, &pdata->count) != 10) {
		printk(KERN_ERR "%s: Unable to parse input !\n", __func__);
		return -1;
	}

	if (bit_fmt >= BIT_FMT_MAX) {
		printk(KERN_ERR "%s: Invalid bit format %u !\n", __func__, bit_fmt);
		return -1;
	}

	pdata->fmt = (enum bit_fmt) bit_fmt;

	return count;
}

static DEVICE_ATTR(timings, S_IWUSR | S_IRUGO, show_timings,
			  store_timings);

static ssize_t show_frame(struct device *dev,
			struct device_attribute *devattr, char *buf)
{
	return 0;
}

static ssize_t store_frame(struct device *dev,
			struct device_attribute *devattr,
			const char *buf, size_t count)
{
	struct ook_gpio_platform_data *pdata = dev->platform_data;
	unsigned char *frame;
	char *startptr, *endptr;
	int bit_count;
	size_t byte_count;
	int i;

	bit_count = simple_strtoul(buf, &endptr, 0);
	if (bit_count < 0) {
		printk(KERN_ERR "%s: unable to parse bit count !\n", __func__);
		return count;
	}

	byte_count = (bit_count + 7)/8;
	frame = (unsigned char *) kmalloc(sizeof(unsigned char) * byte_count, GFP_KERNEL);

	for (i = 0; i < byte_count; i++) {
		if (*endptr != ',') {
			printk(KERN_ERR "%s: missing \",\" after byte %d !\n", __func__, i);
			goto exit;
		}

		startptr = endptr + 1;
		frame[i] = simple_strtoul(startptr, &endptr, 0);
		if (startptr == endptr) {
			printk(KERN_ERR "%s: unable to parse byte %d !\n", __func__, i);
			goto exit;
		}
	}

	ook_send_frame(pdata, frame, (size_t) bit_count);

exit:
	kfree(frame);

	return count;
}

static DEVICE_ATTR(frame, S_IWUSR | S_IRUGO, show_frame,
			  store_frame);

static struct attribute *ook_sysfs_attributes[] = {
	&dev_attr_timings.attr,
	&dev_attr_frame.attr,
	NULL
};

static const struct attribute_group ook_sysfs_group = {
	.attrs = ook_sysfs_attributes,
};

static int ook_gpio_remove(struct platform_device *pdev)
{
	struct ook_gpio_platform_data *pdata = pdev->dev.platform_data;

	sysfs_remove_group(&pdev->dev.kobj, &ook_sysfs_group);
	gpio_free(pdata->gpio);

	return 0;
}

static int ook_gpio_probe(struct platform_device *pdev)
{
	int ret;
	struct ook_gpio_platform_data *pdata;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		printk(KERN_ERR "%s: Missing platform data !\n", __func__);
		return -EINVAL;
	}

	ret = gpio_request_one(pdata->gpio, GPIOF_OUT_INIT_LOW, "OOK GPIO");
	if (ret) {
		printk(KERN_ERR "%s: Could not request GPIO %d !\n", __func__, pdata->gpio);
		goto error_gpio;
	}

	ret = sysfs_create_group(&pdev->dev.kobj, &ook_sysfs_group);
	if (ret) {
		printk(KERN_ERR "%s: Could not create sysfs files !\n", __func__);
		goto error_create_sysfs;
	}

	printk(KERN_INFO "OOK modulation driver properly registered on GPIO %d\n", pdata->gpio);

	return 0;

error_create_sysfs:
	gpio_free(pdata->gpio);
error_gpio:

	return ret;
}

static struct platform_driver ook_gpio_driver = {
	.driver = {
		   .name = DRV_NAME,
		   .owner = THIS_MODULE,
		   },
	.probe		= ook_gpio_probe,
	.remove		= ook_gpio_remove,
};

static int __init ook_gpio_init(void)
{
	struct platform_device *pdev;
	struct ook_gpio_platform_data pdata;
	int ret;

	printk(KERN_INFO "%s version %s\n", DRV_DESC, DRV_VERSION);

	ret = platform_driver_register(&ook_gpio_driver);
	if (ret) {
		printk(KERN_ERR "%s: Could not register driver !\n", __func__);
		goto error_register;
	}

	pdev = platform_device_alloc(DRV_NAME, 0);
	if (!pdev) {
		printk(KERN_ERR "%s: Could not allocate device !\n", __func__);
		ret = -ENOMEM;
		goto error_alloc;
	}

	pdata.gpio = OOK_GPIO_NR;
	pdata.start_h = 0;
	pdata.start_l = 0;
	pdata.end_h = 0;
	pdata.end_l = 0;
	pdata.bit0_h = 0;
	pdata.bit0_l = 0;
	pdata.bit1_h = 0;
	pdata.bit1_l = 0;
	pdata.count = 0;

	ret = platform_device_add_data(pdev, &pdata, sizeof(pdata));
	if (ret) {
		printk(KERN_ERR "%s: Could not add platform data !\n", __func__);
		goto error_add_data;
	}

	ret = platform_device_add(pdev);
	if (ret) {
		printk(KERN_ERR "%s: Could not add device !\n", __func__);
		goto error_add_device;
	}

	ook_gpio_pdev = pdev;

	return 0;

error_add_device:
error_add_data:
	platform_device_put(pdev);
error_alloc:
	platform_driver_unregister(&ook_gpio_driver);
error_register:

	return ret;;
}
module_init(ook_gpio_init);

static void __exit ook_gpio_exit(void)
{
	platform_device_del(ook_gpio_pdev);
	platform_device_put(ook_gpio_pdev);
	platform_driver_unregister(&ook_gpio_driver);
}
module_exit(ook_gpio_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jean-Christophe Rona <jc@rona.fr>");
MODULE_DESCRIPTION(DRV_DESC);
MODULE_VERSION(DRV_VERSION);
