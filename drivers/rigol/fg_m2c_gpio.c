#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/gpio.h>

MODULE_AUTHOR("Norbert Kiszka");
MODULE_DESCRIPTION("fg_m2_dev");
MODULE_LICENSE("GPL");

static dev_t dev = 0;
static int dev_major_number = 0;
static struct class *fg_m2c_class = NULL;
static struct cdev fg_m2_cdev;
static struct gpio_desc *desc;

static int fg_m2_gpio_drv_open(struct inode *inode, struct file *file)
{
	return 0;
}

static long fg_m2_gpio_drv_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return 0;
}

static ssize_t fg_m2_gpio_drv_read(struct file *file, char __user *buf, size_t len, loff_t *f_pos)
{
	if(*f_pos > 0)
	{
		return 0;
	}
	*buf = gpiod_get_raw_value(desc);
	*f_pos += 1; // otherwise user process will not know that anything was read and it will try again and again...
	return 1;
}

static ssize_t fg_m2_gpio_drv_write(struct file *file, const char __user *buf, size_t count, loff_t *offset)
{
	if(count <= 0)
	{
		printk(KERN_ERR "Tried to write fg_m2_gpio with empty data\n");
		return 0;
	}
	
	if(count > 1)
	{
		printk(KERN_WARNING "fg_m2_gpio written with %zu bytes. Should be one byte only.\n", count);
	}
	
	gpiod_direction_output_raw(desc, *buf != '\0');
	
	return count;
}

static int fg_m2_gpio_drv_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations fg_m2c_gpio_fops = {
	.owner           = THIS_MODULE,
	.open            = fg_m2_gpio_drv_open,
	.release         = fg_m2_gpio_drv_release,
	.unlocked_ioctl  = fg_m2_gpio_drv_ioctl,
	.read            = fg_m2_gpio_drv_read,
	.write           = fg_m2_gpio_drv_write
};

static int fg_m2c_gpio_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	add_uevent_var(env, "DEVMODE=%#o", 0666);
	return 0;
}

static int __init fg_m2c_gpio_init(void)
{
	if(alloc_chrdev_region(&dev, 0, 1, "fg_m2c_device") < 0)
	{
		printk(KERN_ERR "fg_m2c_gpio: can't allocate major number\n");
		return -1;
	}
	
	dev_major_number = MAJOR(dev);
	
	fg_m2c_class = class_create(THIS_MODULE, "fg_m2c_gpio_device");
	fg_m2c_class->dev_uevent = fg_m2c_gpio_uevent;
	
	cdev_init(&fg_m2_cdev, &fg_m2c_gpio_fops);
	fg_m2_cdev.owner = THIS_MODULE;
	
	if((cdev_add(&fg_m2_cdev, MKDEV(dev_major_number, 0), 1)) < 0)
	{
		printk(KERN_ERR "Can't add the device fg_m2c_gpio to the system\n");
		goto r_class;
	}
	
	if((device_create(fg_m2c_class, NULL, MKDEV(dev_major_number, 0), NULL, "fg_m2c_gpio")) < 0)
	{
		printk(KERN_ERR "Register fg_m2c_gpio device failed!\n");
		goto r_device;
	}
	
	if(gpio_request(0x95,"FG_M2C_GPIO") == 0)
	{
		desc = gpio_to_desc(0x95);
		gpiod_direction_output_raw(desc, 0);
		printk("fg_m2c_gpio config ok\n");
		printk(" fg_m2c_gpio_dev register successfully\n");
		return 0;
	}
	else
	{
		printk(KERN_ERR "gpio %d  FG_M2C_GPIO request failed!\n", 0x95);
		gpio_free(0x95);
		printk(KERN_ERR "fg_m2c_gpio config gpio failed!\n");
    }

r_device:
	class_destroy(fg_m2c_class);
r_class:
	unregister_chrdev_region(MKDEV(dev_major_number, 0), 1);
	return -1;
}

static void __exit fg_m2c_gpio_exit(void)
{
	printk(KERN_WARNING "fg_m2c_gpio removed");
	
	gpio_free(0x95);
	
	device_destroy(fg_m2c_class, MKDEV(dev_major_number, 0));

	class_unregister(fg_m2c_class);
	class_destroy(fg_m2c_class);

	unregister_chrdev_region(MKDEV(dev_major_number, 0), 1);
}

module_init(fg_m2c_gpio_init);
module_exit(fg_m2c_gpio_exit);
