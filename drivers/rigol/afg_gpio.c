#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/gpio.h>

MODULE_AUTHOR("Norbert Kiszka");
MODULE_DESCRIPTION("afg_gpio_dev");
MODULE_LICENSE("GPL");

// Naming convention from decompiled Rigol module: afg_in1, afg_in2...
#define AFG_IN1 0x7a
#define AFG_IN2 0x7b
#define AFG_IN3 0x7c
#define AFG_IN4 0x7d
#define AFG_IN5 0x7e

static dev_t dev = 0;
static int dev_major_number = 0;
static struct class *afg_class = NULL;
static struct cdev afg_cdev;

static struct gpio_desc *desc_in1;
static struct gpio_desc *desc_in2;
static struct gpio_desc *desc_in3;
static struct gpio_desc *desc_in4;
static struct gpio_desc *desc_in5;

static int afg_open(struct inode *inode, struct file *file)
{
	return 0;
}

static long afg_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return 0;
}

static ssize_t afg_write(struct file *file, const char __user *buf, size_t count, loff_t *offset)
{
	if(count <= 0)
	{
		printk(KERN_ERR "Tried to write afg_gpio with empty data\n");
		return 0;
	}
	
	if(count > 1)
	{
		printk(KERN_ERR "Tried to write afg_gpio with more than one byte (%zu)\n", count);
	}
	
	gpiod_direction_output_raw(desc_in1, *buf & 1);
	gpiod_direction_output_raw(desc_in2, *buf & 2);
	gpiod_direction_output_raw(desc_in3, *buf & 4);
	gpiod_direction_output_raw(desc_in4, *buf & 8);
	gpiod_direction_output_raw(desc_in5, *buf & 16);
	
	return count;
}

static ssize_t afg_read(struct file *file, char __user *buf, size_t len, loff_t *f_pos)
{
	int in1, in2, in3, in4, in5;
	char c;
	
	if(*f_pos > 0)
	{
		return 0;
	}
	
	in1 = gpiod_get_raw_value(desc_in1);
	in2 = gpiod_get_raw_value(desc_in2);
	in3 = gpiod_get_raw_value(desc_in3);
	in4 = gpiod_get_raw_value(desc_in4);
	in5 = gpiod_get_raw_value(desc_in5);
	
	c = (in1 & 1) | (in2 & 1) << 1 | (in3 & 1) << 2 | (in4 & 1) << 3 | (in5 & 1) << 4;
	
	if(copy_to_user(buf, &c, 1))
	{
		return -EFAULT;
	}
	
	*f_pos += 1; // otherwise user process will not know that anything was read and it will try again and again...
	return 1;
}

static const struct file_operations afg_fops = {
	.owner           = THIS_MODULE,
	.open            = afg_open,
	.unlocked_ioctl  = afg_ioctl,
	.read            = afg_read,
	.write           = afg_write
};

static int afg_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	add_uevent_var(env, "DEVMODE=%#o", 0666);
	return 0;
}

static int __init afg_init(void)
{
	printk(KERN_INFO "xxxxx afg_init ...\n"); // same as in Rigol decompiled module...
	
	if(alloc_chrdev_region(&dev, 0, 1, "afg_device") < 0)
	{
		printk(KERN_ERR "afg_gpio: can't allocate major number\n");
		return -1;
	}
	
	dev_major_number = MAJOR(dev);
	
	afg_class = class_create(THIS_MODULE, "afg_device");
	afg_class->dev_uevent = afg_uevent;
	
	cdev_init(&afg_cdev, &afg_fops);
	afg_cdev.owner = THIS_MODULE;
	
	if((cdev_add(&afg_cdev, MKDEV(dev_major_number, 0), 1)) < 0)
	{
		printk(KERN_ERR "Can't add the device afg_cdev to the system\n");
		goto r_class;
	}
	
	if((device_create(afg_class, NULL, MKDEV(dev_major_number, 0), NULL, "afg_gpio")) < 0)
	{
		printk(KERN_ERR "Register afg_gpio device failed!\n");
		goto r_device;
	}
	
	if(gpio_request(AFG_IN1, "afg_in1"))
	{
		printk(KERN_ERR "gpio %d  afg_in1 request failed!\n", AFG_IN1); // Same message as in original Rigol module.
		gpio_free(AFG_IN1);
		goto r_device;
	}
	if(gpio_request(AFG_IN2, "afg_in2"))
	{
		printk(KERN_ERR "gpio %d  afg_in2 request failed!\n", AFG_IN2);
		gpio_free(AFG_IN1);
		gpio_free(AFG_IN2);
		goto r_device;
	}
	if(gpio_request(AFG_IN3, "afg_in3"))
	{
		printk(KERN_ERR "gpio %d  afg_in3 request failed!\n", AFG_IN3);
		gpio_free(AFG_IN1);
		gpio_free(AFG_IN2);
		gpio_free(AFG_IN3);
		goto r_device;
	}
	if(gpio_request(AFG_IN4, "afg_in4"))
	{
		printk(KERN_ERR "gpio %d  afg_in4 request failed!\n", AFG_IN4);
		gpio_free(AFG_IN1);
		gpio_free(AFG_IN2);
		gpio_free(AFG_IN3);
		gpio_free(AFG_IN4);
		goto r_device;
	}
	if(gpio_request(AFG_IN5, "afg_in5"))
	{
		printk(KERN_ERR "gpio %d  afg_in5 request failed!\n", AFG_IN5);
		gpio_free(AFG_IN1);
		gpio_free(AFG_IN2);
		gpio_free(AFG_IN3);
		gpio_free(AFG_IN4);
		gpio_free(AFG_IN5);
		goto r_device;
	}
	
	desc_in1 = gpio_to_desc(AFG_IN1);
	desc_in2 = gpio_to_desc(AFG_IN2);
	desc_in3 = gpio_to_desc(AFG_IN3);
	desc_in4 = gpio_to_desc(AFG_IN4);
	desc_in5 = gpio_to_desc(AFG_IN5);
	
	gpiod_direction_output_raw(desc_in1, 0);
	gpiod_direction_output_raw(desc_in2, 0);
	gpiod_direction_output_raw(desc_in3, 0);
	gpiod_direction_output_raw(desc_in4, 0);
	gpiod_direction_output_raw(desc_in5, 0);
	
	// Same as in Rigol decompiled module...
	printk(KERN_INFO "afg config ok\n");
	printk(KERN_INFO " gpio_afg_dev register successfully\n");
	return 0;

r_device:
	class_destroy(afg_class);
r_class:
	unregister_chrdev_region(MKDEV(dev_major_number, 0), 1);
	return -1;
}

static void __exit afg_exit(void)
{
	printk(KERN_WARNING "afg_gpio removed");
	
	gpiod_direction_output_raw(desc_in1, 0);
	gpiod_direction_output_raw(desc_in2, 0);
	gpiod_direction_output_raw(desc_in3, 0);
	gpiod_direction_output_raw(desc_in4, 0);
	gpiod_direction_output_raw(desc_in5, 0);
	
	gpio_free(AFG_IN1);
	gpio_free(AFG_IN2);
	gpio_free(AFG_IN3);
	gpio_free(AFG_IN4);
	gpio_free(AFG_IN5);
	
	device_destroy(afg_class, MKDEV(dev_major_number, 0));

	class_unregister(afg_class);
	class_destroy(afg_class);

	unregister_chrdev_region(MKDEV(dev_major_number, 0), 1);
}

module_init(afg_init);
module_exit(afg_exit);
