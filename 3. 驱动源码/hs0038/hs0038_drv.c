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
#include <linux/input.h>

static struct gpio_desc *hs0038_gpio;
static unsigned int hs0038_irq;

static unsigned int hs0038_pre_data;

static u64 hs0038_edge_time[100];
static int hs0038_edge_cnt;

static struct input_dev *hs0038_input_dev;

/*
 * 0 成功, *val 中保存数据
 * -1 未接收完成
 * -2 解析错误
 * -3 接收超时
 */

int hs0038_parse_data(unsigned int *val)
{
	hs0038_edge_time[hs0038_edge_cnt++] = ktime_get_boot_ns();

	if ((hs0038_edge_cnt >= 2) && (hs0038_edge_time[hs0038_edge_cnt-1] - hs0038_edge_time[hs0038_edge_cnt-2] > 30000000)) {
		hs0038_edge_time[0] = hs0038_edge_time[hs0038_edge_cnt-1];
		hs0038_edge_cnt = 1;
		return -3;
	}

	if (hs0038_edge_cnt == 3) {
		u64 tmp1 = hs0038_edge_time[1] - hs0038_edge_time[0];
		u64 tmp2 = hs0038_edge_time[2] - hs0038_edge_time[1];
		if ((tmp1 > 8000000 && tmp1 < 10000000) && (tmp2 < 3000000)) {
			hs0038_edge_cnt = 0;
			*val = hs0038_pre_data;
			return 0;
		}
	}
	else if (hs0038_edge_cnt >= 68) {
		unsigned char data[4] = {0};
		int i, j;
		int index = 3;

		hs0038_edge_cnt = 0;

		for (i=0; i<4; ++i) {
			for (j=0; j<8; j++) {
				if ((hs0038_edge_time[index+1] - hs0038_edge_time[index]) > 1000000) {
					data[i] |= 1 << j;
				}
				index += 2;
			}
		}

		data[1] = ~data[1];
		data[3] = ~data[3];
		
		if (data[0] != data[1] || data[2] != data[3]) {
			return -2;
		}

		*val = data[0] << 8 | data[2];
		hs0038_pre_data = *val;

		return 0;
	}

	return -1;
}

static irqreturn_t hs0038_irq_handler(int irq, void *dev_id)
{
	unsigned int val = 0;

	if (hs0038_parse_data(&val) == 0) {
		if (val == 0x5e)      val = KEY_1;
		else if (val == 0x5a) val = KEY_2;
		else if (val == 0x1c) val = KEY_3;
		else if (val == 0x0c) val = KEY_4;
	
		input_event(hs0038_input_dev, EV_KEY, val, 1);
		input_event(hs0038_input_dev, EV_KEY, val, 0);
		input_sync(hs0038_input_dev);
	}

	return IRQ_HANDLED;
}

static int hs0038_probe(struct platform_device *pdev)
{
	int ret;

	hs0038_gpio = gpiod_get(&pdev->dev, NULL, 0);
	if (IS_ERR(hs0038_gpio)) {
		return PTR_ERR(hs0038_gpio);
	}

	hs0038_irq = gpiod_to_irq(hs0038_gpio);
	if (hs0038_irq < 0) {
		gpiod_put(hs0038_gpio);
		return hs0038_irq;
	}

	ret = request_irq(hs0038_irq, hs0038_irq_handler, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "myhs0038_irq", NULL);
	if (ret) {
		gpiod_put(hs0038_gpio);
		return ret;
	}

	hs0038_input_dev = devm_input_allocate_device(&pdev->dev);
	hs0038_input_dev->name = "hs0038";
	hs0038_input_dev->phys = "hs0038";

	__set_bit(EV_KEY, hs0038_input_dev->evbit);
	__set_bit(EV_REP, hs0038_input_dev->evbit);
	memset(hs0038_input_dev->keybit, 0xff, sizeof(hs0038_input_dev->keybit));

	ret = input_register_device(hs0038_input_dev);
	if (ret) {
		free_irq(hs0038_irq, NULL);
		gpiod_put(hs0038_gpio);
		return ret;
	}
	
	return 0;
}	

static int hs0038_remove(struct platform_device *pdev)
{
	input_unregister_device(hs0038_input_dev);

	free_irq(hs0038_irq, NULL);

	gpiod_put(hs0038_gpio);

	return 0;
}

static const struct of_device_id hs0038_of_match[] = {
	{ .compatible = "100ask,hs0038", },
	{ },
};

static struct platform_driver hs0038_driver = {
	.probe		= hs0038_probe,
	.remove		= hs0038_remove,
	.driver		= {
		.name = "100ask,hs0038",
		.of_match_table = of_match_ptr(hs0038_of_match),
	}
};

static int __init hs0038_init(void)
{
	return platform_driver_register(&hs0038_driver);
}

static void __exit hs0038_exit(void)
{
	platform_driver_unregister(&hs0038_driver);
}

module_init(hs0038_init);
module_exit(hs0038_exit);

MODULE_LICENSE("GPL");

