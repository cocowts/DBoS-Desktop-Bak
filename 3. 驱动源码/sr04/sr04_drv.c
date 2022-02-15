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

static int sr04_major;
static struct class *sr04_class;
static struct gpio_desc *sr04_trig;
static struct gpio_desc *sr04_echo;

static unsigned int sr04_irq;
static u64 sr04_data_ns;
static wait_queue_head_t sr04_wq;

static irqreturn_t sr04_irq_handler(int irq, void *dev_id)
{
	int val = gpiod_get_value(sr04_echo);

	if (val) {
		sr04_data_ns = ktime_get_ns();	
	}
	else {
		sr04_data_ns = ktime_get_ns() - sr04_data_ns;

		do_div(sr04_data_ns, 10);
	
		wake_up(&sr04_wq);
	}

	return IRQ_HANDLED;
}

static ssize_t sr04_read (struct file *file, char __user *buff, size_t size, loff_t *offset)
{
	gpiod_set_value(sr04_trig, 1);
	udelay(11);
	gpiod_set_value(sr04_trig, 0);

	if (wait_event_interruptible_timeout(sr04_wq, sr04_data_ns, msecs_to_jiffies(1500)) > 0) {
		int ret;
		int len = size < sizeof(4) ? size : sizeof(4);
		int distance_cm;

		do_div(sr04_data_ns, 1000);
		distance_cm = (int)(sr04_data_ns) * 340 / 2 / 100;
		ret = copy_to_user(buff, &distance_cm, len);
		sr04_data_ns = 0;
		
		return ret ? -EFAULT : len;
	}

	return -EAGAIN;
}

static int sr04_open (struct inode *inode, struct file *file)
{
	int ret = 0;
	sr04_irq = gpiod_to_irq(sr04_echo);
	if (sr04_irq < 0) {
		printk(KERN_ERR "aoe: failed to translate GPIO to IRQ\n");
		return sr04_irq;
	}

	ret = request_irq(sr04_irq, sr04_irq_handler, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "sr04_irq", NULL);
	if (ret) {
		printk(KERN_ERR "aoe: failed to request irq\n");
		return ret;
	}

	return ret;
}

static int sr04_release (struct inode *inode, struct file *file)
{
	free_irq(sr04_irq, NULL);
	
	return 0;
}

static const struct file_operations sr04_fops = {
	.read           = sr04_read,
	.open           = sr04_open,
	.release        = sr04_release
};

static int sr04_probe(struct platform_device *pdev)
{
	struct device *dev;

	sr04_trig = gpiod_get(&pdev->dev, "trig", GPIOD_OUT_LOW);
	if (IS_ERR(sr04_trig)) {
		printk(KERN_ERR "aoe: can't get trig-gpios\n");
		return PTR_ERR(sr04_trig);
	}

	sr04_echo = gpiod_get(&pdev->dev, "echo", GPIOD_IN);
	if (IS_ERR(sr04_echo)) {
		gpiod_put(sr04_trig);
		printk(KERN_ERR "aoe: can't get echo-gpios\n");
		return PTR_ERR(sr04_echo);
	}
	
	dev = device_create(sr04_class, NULL, MKDEV(sr04_major, 0), NULL, "sr04");
	if (IS_ERR(dev)) {
		gpiod_put(sr04_trig);
		gpiod_put(sr04_echo);
		printk(KERN_ERR "aoe: can't create device\n");
		return PTR_ERR(dev);
	}
	
	return 0;
}	

static int sr04_remove(struct platform_device *pdev)
{
	device_destroy(sr04_class, MKDEV(sr04_major, 0));

	gpiod_put(sr04_trig);
	gpiod_put(sr04_echo);

	return 0;
}

static const struct of_device_id sr04_of_match[] = {
	{ .compatible = "100ask,sr04", },
	{ },
};

static struct platform_driver sr04_device_driver = {
	.probe		= sr04_probe,
	.remove		= sr04_remove,
	.driver		= {
		.name = "100ask,sr04",
		.of_match_table = of_match_ptr(sr04_of_match),
	}
};

static int __init sr04_init(void)
{
	int ret;

	init_waitqueue_head(&sr04_wq);

	sr04_major = register_chrdev(0, "sr04", &sr04_fops);
	if (sr04_major < 0) {
		return sr04_major;
	}

	sr04_class = class_create(THIS_MODULE, "sr04_class");
	if (IS_ERR(sr04_class)) {
		unregister_chrdev(sr04_major, "sr04");
		return PTR_ERR(sr04_class);
	}

	ret = platform_driver_register(&sr04_device_driver);
	if (ret) {
		unregister_chrdev(sr04_major, "sr04");
		class_destroy(sr04_class);
		return ret;
	}

	return ret;
}

static void __exit sr04_exit(void)
{
	platform_driver_unregister(&sr04_device_driver);

	class_destroy(sr04_class);

	unregister_chrdev(sr04_major, "sr04");
}

module_init(sr04_init);
module_exit(sr04_exit);

MODULE_LICENSE("GPL");

