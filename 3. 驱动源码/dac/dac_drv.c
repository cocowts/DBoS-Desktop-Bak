#include <linux/module.h>

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>
#include <linux/spi/spi.h>

static unsigned int dac_major;
static struct class *dac_class;
static struct spi_device *dac_spi;

ssize_t dac_write (struct file *file, const char __user *user, size_t size, loff_t *offset)
{
	unsigned char ker_buf[2];
	unsigned short val;
	
	if (size != 2) {
		return -EINVAL;
	}

	if (copy_from_user(&val, user, 2)) {
		return -EFAULT;
	}

	val <<= 2;
	val = val & 0x0FFF;
	
	ker_buf[0] = val >> 8;
	ker_buf[1] = val;
	
	if (spi_write(dac_spi, ker_buf, 2)) {
		return EIO;
	}

	return 2;
}

struct file_operations dac_fops = {
	.owner = THIS_MODULE,
	.write = dac_write
};

static int dac_spi_probe(struct spi_device *spi)
{
	struct device *dev;

	dac_spi = spi;

	dac_major = register_chrdev(0, "dac", &dac_fops);
	if (dac_major < 0) {
		return dac_major;
	}

	dac_class = class_create(THIS_MODULE, "dac_class");
 	if (IS_ERR(dac_class)) {
		unregister_chrdev(dac_major, "dac");
		return PTR_ERR(dac_class);
	}

	dev = device_create(dac_class, NULL, MKDEV(dac_major, 0), NULL, "dac");
	if (IS_ERR(dev)) {
		class_destroy(dac_class);
		unregister_chrdev(dac_major, "dac");
		return PTR_ERR(dev);
	}

	return 0;
}

static int dac_spi_remove(struct spi_device *spi)
{
	device_destroy(dac_class, MKDEV(dac_major, 0));

	class_destroy(dac_class);

	unregister_chrdev(dac_major, "dac");

	return 0;
}

static const struct of_device_id dac_dt_ids[] = {
	{ .compatible = "100ask,dac" },
	{},
};
MODULE_DEVICE_TABLE(of, dac_dt_ids);

static const struct spi_device_id dac_id[] = {
	{}
};
MODULE_DEVICE_TABLE(spi, dac_id);

static struct spi_driver dac_driver = {
	.driver = {
		.name = "100ask,dac",
		.of_match_table = of_match_ptr(dac_dt_ids),
	},
	.probe   = dac_spi_probe,
	.remove  = dac_spi_remove,
	.id_table = dac_id,
};

static int __init dac_init(void)
{
	return spi_register_driver(&dac_driver);
}

static void __exit dac_exit(void)
{
	spi_unregister_driver(&dac_driver);
}

module_init(dac_init);
module_exit(dac_exit);

MODULE_LICENSE("GPL");

