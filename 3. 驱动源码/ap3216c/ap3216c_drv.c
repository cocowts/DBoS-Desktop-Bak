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
#include <linux/version.h>
#include <linux/i2c.h>

static struct i2c_client *ap3216c_client;
static int ap3216c_major;
static struct class *ap3216c_class;

static ssize_t ap3216c_read (struct file *file, char __user *buf, size_t size, loff_t *offset)
{
	unsigned char  reg_buff[6];
	unsigned short convert_buff[3];

	if (size != 6)
		return -EINVAL;

	reg_buff[0] = i2c_smbus_read_byte_data(ap3216c_client, 0xA);
	reg_buff[1] = i2c_smbus_read_byte_data(ap3216c_client, 0xB);

	reg_buff[2] = i2c_smbus_read_byte_data(ap3216c_client, 0xC);
	reg_buff[3] = i2c_smbus_read_byte_data(ap3216c_client, 0xD);

	reg_buff[4] = i2c_smbus_read_byte_data(ap3216c_client, 0xE);
	reg_buff[5] = i2c_smbus_read_byte_data(ap3216c_client, 0xF);

	// ir 红外值
	if (reg_buff[0] & 0x80)  
		convert_buff[0] = 0;
	else
		convert_buff[0] = (unsigned short)((reg_buff[1] << 2)|(reg_buff[0] & 0x03));

	// als  光强
	convert_buff[1] = (unsigned short)(((reg_buff[3] << 8) | reg_buff[2]) * 35 / 100);

	// ps 距离
	if(reg_buff[5] & 0x40)
		convert_buff[2]=0;
	else
		convert_buff[2]= (unsigned short)(((reg_buff[5] & 0x3F) << 4) | (reg_buff[4] & 0x0F));

	if (copy_to_user(buf, convert_buff, size))
		return -EFAULT;
	
	return size;
}


static int ap3216c_open (struct inode *inode, struct file *file)
{
	if (i2c_smbus_write_byte_data(ap3216c_client, 0x00, 0x04) < 0)
		return ENXIO;
	
	schedule_timeout(msecs_to_jiffies(15));
	
	if (i2c_smbus_write_byte_data(ap3216c_client, 0x00, 0x03) < 0)
		return ENXIO;

	schedule_timeout(msecs_to_jiffies(15));
	
	return 0;
}

static int ap3216c_release (struct inode *inode, struct file *file)
{
	if (i2c_smbus_write_byte_data(ap3216c_client, 0x00, 0x04) < 0)
		return ENXIO;
	
	return 0;
}

static const struct file_operations ap3216c_fops = {
	.owner   = THIS_MODULE,
	.read    = ap3216c_read,
	.open    = ap3216c_open,
	.release = ap3216c_release
};

static int ap3216c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device *dev;

	ap3216c_client = client;

	ap3216c_major = register_chrdev(0, "ap3216c", &ap3216c_fops);
	if (ap3216c_major < 0) {
		return ap3216c_major;
	}

	ap3216c_class = class_create(THIS_MODULE, "ap3216c_class");
	if (IS_ERR(ap3216c_class)) {
		unregister_chrdev(ap3216c_major, "ap3216c");
		return PTR_ERR(ap3216c_class);
	}

	dev = device_create(ap3216c_class, NULL, MKDEV(ap3216c_major, 0), NULL, "ap3216c");
	if (IS_ERR(dev)) {
		class_destroy(ap3216c_class);
		unregister_chrdev(ap3216c_major, "ap3216c");
		return PTR_ERR(dev);
	}

	return 0;
}	

static int ap3216c_remove(struct i2c_client *client)
{
	device_destroy(ap3216c_class, MKDEV(ap3216c_major, 0));

	class_destroy(ap3216c_class);

	unregister_chrdev(ap3216c_major, "ap3216c");

	return 0;
}

static const struct i2c_device_id ap3216c_i2c_id[] = {
	{}
};

static const struct of_device_id ap3216c_dt_ids[] = {
	{ .compatible = "100ask,ap3216c", NULL },
	{ /* sentinel */ }
};

static struct i2c_driver ap3216c_driver = {
	.driver = {
		.name = "ap3216c",
		.of_match_table = ap3216c_dt_ids
	},
	.probe    = ap3216c_probe,
	.remove   = ap3216c_remove,
	.id_table = ap3216c_i2c_id,
};

static int __init ap3216c_init(void)
{
	return i2c_add_driver(&ap3216c_driver);
}

static void __exit ap3216c_exit(void)
{
	i2c_del_driver(&ap3216c_driver);
}

module_init(ap3216c_init);
module_exit(ap3216c_exit);

MODULE_LICENSE("GPL");

