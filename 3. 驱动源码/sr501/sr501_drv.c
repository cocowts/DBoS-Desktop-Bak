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

static int sr501_major;
static struct class *sr501_class;
static struct gpio_desc *sr501_gpio;
static unsigned int sr501_irq;
static int sr501_data = 0;
static wait_queue_head_t sr501_wq;

static irqreturn_t sr501_irq_handler(int irq, void *dev_id)
{
	sr501_data = 0x80 | gpiod_get_value(sr501_gpio);

	wake_up(&sr501_wq);

	return IRQ_HANDLED;
}

static ssize_t sr501_read (struct file *file, char __user *buff, size_t size, loff_t *offset)
{
	int ret;
	int len = (size < 4) ? size : 4;

	if (file->f_flags & O_NONBLOCK) {
		int value = gpiod_get_value(sr501_gpio);
		ret = copy_to_user(buff, &value, (size < 4) ? size : 4);
		return ret ? -EFAULT : len;
	}
	else if (wait_event_interruptible(sr501_wq, sr501_data) == 0) {
		sr501_data &= ~(0x80);
		ret = copy_to_user(buff, &sr501_data, len);
		sr501_data = 0;

		return ret ? -EFAULT : len;
	}

	return -EAGAIN;
}

static int sr501_open (struct inode *inode, struct file *file)
{
	int ret = 0;

	if (!(file->f_flags & O_NONBLOCK)) {
		
		sr501_irq = gpiod_to_irq(sr501_gpio);
		if (sr501_irq < 0) {
			printk(KERN_ERR "aoe: failed to translate GPIO to IRQ\n");
			return sr501_irq;
		}

		ret = request_irq(sr501_irq, sr501_irq_handler, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "sr501_irq", NULL);
		if (ret) {
			printk(KERN_ERR "aoe: failed to request irq\n");
			return ret;
		}
	}
		
	return ret;
}

static int sr501_release (struct inode *inode, struct file *file)
{
	if (!(file->f_flags & O_NONBLOCK)) {
		free_irq(sr501_irq, NULL);
	}

	return 0;
}

static const struct file_operations sr501_fops = {
	.read           = sr501_read,
	.open           = sr501_open,
	.release        = sr501_release
};

static int sr501_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev;

	sr501_gpio = gpiod_get(&pdev->dev, NULL, 0);
	if (IS_ERR(sr501_gpio)) {
		printk(KERN_ERR "aoe: can't get gpio\n");
		return PTR_ERR(sr501_gpio);
	}
	
	ret = gpiod_direction_input(sr501_gpio);
	if (ret) {
		gpiod_put(sr501_gpio);
		printk(KERN_ERR "aoe: can't set gpio direction\n");
		return ret;
	}

	dev = device_create(sr501_class, NULL, MKDEV(sr501_major, 0), NULL, "sr501");
	if (IS_ERR(dev)) {
		free_irq(sr501_irq, NULL);
		gpiod_put(sr501_gpio);
		printk(KERN_ERR "aoe: can't create device\n");
		return PTR_ERR(dev);
	}
	
	return ret;
}	

static int sr501_remove(struct platform_device *pdev)
{
	device_destroy(sr501_class, MKDEV(sr501_major, 0));

	gpiod_put(sr501_gpio);

	return 0;
}

static const struct of_device_id sr501_of_match[] = {
	{ .compatible = "100ask,sr501", },
	{ },
};

static struct platform_driver sr501_device_driver = {
	.probe		= sr501_probe,
	.remove		= sr501_remove,
	.driver		= {
		.name = "100ask_sr501",
		.of_match_table = of_match_ptr(sr501_of_match),
	}
};

static int __init sr501_init(void)
{
	int ret;

	init_waitqueue_head(&sr501_wq);

	sr501_major = register_chrdev(0, "sr501", &sr501_fops);
	if (sr501_major < 0) {
		printk(KERN_ERR "aoe: can't register char device\n");
		return sr501_major;
	}

	sr501_class = class_create(THIS_MODULE, "sr501_class");
	if (IS_ERR(sr501_class)) {
		unregister_chrdev(sr501_major, "sr501");
		printk(KERN_ERR "aoe: can't create class device\n");
		return PTR_ERR(sr501_class);
	}

	ret = platform_driver_register(&sr501_device_driver);
	if (ret) {
		unregister_chrdev(sr501_major, "sr501");
		class_destroy(sr501_class);
		printk(KERN_ERR "aoe: can't register platform driver\n");
		return ret;
	}

	return ret;
}

static void __exit sr501_exit(void)
{
	platform_driver_unregister(&sr501_device_driver);

	class_destroy(sr501_class);

	unregister_chrdev(sr501_major, "sr501");
}

module_init(sr501_init);
module_exit(sr501_exit);

MODULE_LICENSE("GPL");

