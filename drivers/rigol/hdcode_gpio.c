#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/gpio.h>

MODULE_AUTHOR("Norbert Kiszka");
MODULE_DESCRIPTION("gpio_hdcode_dev");
MODULE_LICENSE("GPL");

static uint8_t data = 0;
static dev_t dev = 0;
static int dev_major_number = 0;
static struct class *hdcode_class = NULL;
static struct cdev hdcode_cdev;

static int gpio_hdcode_drv_open(struct inode *inode, struct file *file)
{
	return 0;
}

static long gpio_hdcode_drv_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return 0;
}

static ssize_t gpio_hdcode_drv_read(struct file *file, char __user *buf, size_t len, loff_t *f_pos)
{
	if(*f_pos > 0)
	{
		return 0;
	}
	if(copy_to_user(buf, &data, 1))
	{
		return -EFAULT;
	}
	*f_pos += 1; // otherwise user process will not know that anything was read and it will try again and again...
	return 1;
}

static ssize_t gpio_hdcode_drv_write(struct file *file, const char __user *buf, size_t count, loff_t *offset)
{
	if(count <= 0)
	{
		printk(KERN_ERR "Tried to write hdcode_gpio with empty data\n");
		return 0;
	}
	
	if(count > 1)
	{
		printk(KERN_WARNING "hdcode_gpio written with %zu bytes. Should be one byte only.\n", count);
	}
	
	memcpy(&data, (const void __force *)buf, 1);
	printk("hdcode_gpio changed to: %u\n", data);
	
	return count;
}

static int gpio_hdcode_drv_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations gpio_hdcode_fops = {
	.owner           = THIS_MODULE,
	.open            = gpio_hdcode_drv_open,
	.release         = gpio_hdcode_drv_release,
	.unlocked_ioctl  = gpio_hdcode_drv_ioctl,
	.read            = gpio_hdcode_drv_read,
	.write           = gpio_hdcode_drv_write
};

static int hdcode_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	add_uevent_var(env, "DEVMODE=%#o", 0666);
	return 0;
}

static int __init gpio_hdcode_init(void)
{
	int val;
	struct gpio_desc *desc;
	
	data = 0;
	
	if(alloc_chrdev_region(&dev, 0, 1, "hdcode_device") < 0)
	{
		printk(KERN_ERR "hdcode_gpio: can't allocate major number\n");
		return -1;
	}
	
	dev_major_number = MAJOR(dev);
	
	hdcode_class = class_create(THIS_MODULE, "hdcode_device");
	hdcode_class->dev_uevent = hdcode_uevent;
	
	cdev_init(&hdcode_cdev, &gpio_hdcode_fops);
	hdcode_cdev.owner = THIS_MODULE;
	
	if((cdev_add(&hdcode_cdev, MKDEV(dev_major_number, 0), 1)) < 0)
	{
		printk(KERN_ERR "Can't add the device hdcode_gpio to the system\n");
		goto r_class;
	}
	
	if((device_create(hdcode_class, NULL, MKDEV(dev_major_number, 0), NULL, "hdcode_gpio")) < 0)
	{
		printk(KERN_ERR "Register hdcode_gpio device failed!\n");
		goto r_device;
	}
	
	gpio_request(4,"hd_code0");
	desc = gpio_to_desc(4);
	gpiod_direction_input(desc);
	val = gpiod_get_raw_value(desc);
	printk("hd_code0 = %d\n", val);
	if(val)
	{
		data = 1;
	}
	
	gpio_request(8,"hd_code1");
	desc = gpio_to_desc(8);
	gpiod_direction_input(desc);
	val = gpiod_get_raw_value(desc);
	printk("hd_code1 = %d\n", val);
	if(val)
	{
		data += 2;
	}
	
	gpio_request(0xb,"hd_code2");
	desc = gpio_to_desc(0xb);
	gpiod_direction_input(desc);
	val = gpiod_get_raw_value(desc);
	printk("hd_code2 = %d\n", val);
	if(val)
	{
		data += 4;
	}
	
	gpio_request(0xc,"hd_code3");
	desc = gpio_to_desc(0xc);
	gpiod_direction_input(desc);
	val = gpiod_get_raw_value(desc);
	printk("hd_code3 = %d\n", val);
	if(val)
	{
		data += 8;
	}
	
	printk("gpio_hdcode_dev loaded successfully. HW: %u\n", data);

	return 0;
	
r_device:
	class_destroy(hdcode_class);
r_class:
	unregister_chrdev_region(MKDEV(dev_major_number, 0), 1);
	return -1;
}

static void __exit gpio_hdcode_exit(void)
{
	printk(KERN_WARNING "hdcode_gpio removed");
	
	gpio_free(4);
	gpio_free(8);
	gpio_free(0xb);
	gpio_free(0xc);
	
	device_destroy(hdcode_class, MKDEV(dev_major_number, 0));

	class_unregister(hdcode_class);
	class_destroy(hdcode_class);

	unregister_chrdev_region(MKDEV(dev_major_number, 0), 1);
	return;
}

module_init(gpio_hdcode_init);
module_exit(gpio_hdcode_exit);
