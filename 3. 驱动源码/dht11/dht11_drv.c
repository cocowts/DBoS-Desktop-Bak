#include <linux/module.h>

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/math64.h>
#include <linux/uaccess.h>

static int dht11_major;
static struct class *dht11_class;
static struct gpio_desc *dht11_gpio;

static void dht11_reset(void)
{
	gpiod_direction_output(dht11_gpio, 1);
}

static void dht11_start(void)
{
	gpiod_direction_output(dht11_gpio, 0);
	mdelay(18);
	gpiod_set_value(dht11_gpio, 1);
	udelay(30);
}

static int dht11_wait_for_ready(void)
{
	int timeout_us = 200;

	gpiod_direction_input(dht11_gpio);

	/* 等待低电平 */
	timeout_us = 200; 
	while (gpiod_get_value(dht11_gpio) && --timeout_us) {
		udelay(1);
	}

	if (timeout_us == 0) {
		return -1;
	}

	/* 等待高电平 */
	timeout_us = 200;
	while (!gpiod_get_value(dht11_gpio) && --timeout_us) {
		udelay(1);
	}
	
	if (timeout_us == 0) {
		return -1;
	}

	/* 等待低电平 */
	timeout_us = 200;
	while (gpiod_get_value(dht11_gpio) && --timeout_us) {
		udelay(1);
	}

	if (timeout_us == 0) {
		return -1;
	}

	return 0;
}

static int dht11_read_byte(unsigned char *buf)
{
	int i;
	int timeout_us = 200;
	u64 sustained_time_ns = 0;
	unsigned char data = 0;

	for (i=0; i<8; ++i) {
		
		timeout_us = 200;
		while (!gpiod_get_value(dht11_gpio) && --timeout_us) {  /* 等待高电平 */
			udelay(1);
		}
		if (timeout_us == 0) {
			return -1;
		}

		timeout_us = 200;
		sustained_time_ns = ktime_get_ns();
		while (gpiod_get_value(dht11_gpio) && --timeout_us) {  /* 等待低电平 */
			udelay(1);
		}
		if (timeout_us == 0) {
			return -1;
		}
		sustained_time_ns = ktime_get_ns() - sustained_time_ns;

		data <<= 1;
		if (sustained_time_ns > 40000) {
			data |=  1;
		}
	}

	*buf = data;
	
	return 0;
}

static int dht11_wait_for_end(void)
{
	int timeout_us = 200;
	/* 等待高电平 */
	timeout_us = 200;
	while (!gpiod_get_value(dht11_gpio) && --timeout_us) {
		udelay(1);
	}

	if (timeout_us == 0) {
		return -1;
	}

	return 0;
}

static ssize_t dht11_read (struct file *file, char __user *buff, size_t size, loff_t *offset)
{
	int ret;
	unsigned long flags;
	unsigned char data[5];
	int len = size < 4 ? size : 4;
	int i;

	local_irq_save(flags);

	dht11_start();

	if (dht11_wait_for_ready()) {
		dht11_reset();
		local_irq_restore(flags);
		return -1;
	}

	for (i=0; i<5; ++i) {
		if (dht11_read_byte(&data[i])) {
			dht11_reset();
			local_irq_restore(flags);
			return -1;
		}
	}

	if (dht11_wait_for_end()) {
		dht11_reset();
		local_irq_restore(flags);
		return -1;
	}

	if (data[4] != (unsigned char)(data[0] + data[1] + data[2] + data[3])) {
		dht11_reset();
		local_irq_restore(flags);
		return -1;
	}

	dht11_reset();

	local_irq_restore(flags);

	ret = copy_to_user(buff, &data, len);
	
	return ret ? -EFAULT : len;
}

static const struct file_operations dht11_fops = {
	.read           = dht11_read,
};

static int dht11_probe(struct platform_device *pdev)
{
	struct device *dev;

	dht11_gpio = gpiod_get(&pdev->dev, NULL, GPIOD_OUT_HIGH);
	if (IS_ERR(dht11_gpio)) {
		printk(KERN_ERR "aoe: can't get dht11_gpio\n");
		return PTR_ERR(dht11_gpio);
	}

	dev = device_create(dht11_class, NULL, MKDEV(dht11_major, 0), NULL, "mydht11");
	if (IS_ERR(dev)) {
		gpiod_put(dht11_gpio);
		printk(KERN_ERR "aoe: can't create device\n");
		return PTR_ERR(dev);
	}
	
	return 0;
}	

static int dht11_remove(struct platform_device *pdev)
{
	device_destroy(dht11_class, MKDEV(dht11_major, 0));

	gpiod_put(dht11_gpio);

	return 0;
}

static const struct of_device_id dht11_of_match[] = {
	{ .compatible = "100ask,dht11", },
	{ },
};

static struct platform_driver dht11_driver = {
	.probe		= dht11_probe,
	.remove		= dht11_remove,
	.driver		= {
		.name = "100ask,dht11",
		.of_match_table = of_match_ptr(dht11_of_match),
	}
};

static int __init dht11_init(void)
{
	int ret;

	dht11_major = register_chrdev(0, "dht11", &dht11_fops);
	if (dht11_major < 0) {
		printk(KERN_ERR "aoe: can't register char device\n");
		return dht11_major;
	}

	dht11_class = class_create(THIS_MODULE, "dht11_class");
	if (IS_ERR(dht11_class)) {
		unregister_chrdev(dht11_major, "dht11");
		printk(KERN_ERR "aoe: can't create class device\n");
		return PTR_ERR(dht11_class);
	}

	ret = platform_driver_register(&dht11_driver);
	if (ret) {
		unregister_chrdev(dht11_major, "dht11");
		class_destroy(dht11_class);
		printk(KERN_ERR "aoe: can't register platform driver\n");
		return ret;
	}

	return ret;
}

static void __exit dht11_exit(void)
{
	platform_driver_unregister(&dht11_driver);

	class_destroy(dht11_class);

	unregister_chrdev(dht11_major, "dht11");
}

module_init(dht11_init);
module_exit(dht11_exit);

MODULE_LICENSE("GPL");

