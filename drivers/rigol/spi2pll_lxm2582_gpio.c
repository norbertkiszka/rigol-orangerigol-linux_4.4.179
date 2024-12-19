#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/spinlock.h>
#include <linux/delay.h>

MODULE_AUTHOR("Norbert Kiszka");
MODULE_DESCRIPTION("spi2pll_lxm2582_gpio_dev");
MODULE_LICENSE("GPL");

#define u32LE   6
#define u32SCLK 3
#define u32SDIO 2

static dev_t dev = 0;
static int dev_major_number = 0;
static struct class *spi_pll_class = NULL;
static struct cdev spi_pll_cdev;
//static struct gpio_desc *desc;
static DEFINE_RAW_SPINLOCK(raw_spinlock);


static int spi_pll_open(struct inode *inode, struct file *file)
{
	return 0;
}

static long spi_pll_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return 0;
}

static void inline _spi_pll_set_gpio_high(unsigned int gpio)
{
	gpiod_set_raw_value(gpio_to_desc(gpio), 2);
}

static void inline _spi_pll_set_gpio_low(unsigned int gpio)
{
	gpiod_set_raw_value(gpio_to_desc(gpio), 0);
}

static ssize_t spi_pll_write(struct file *file, const char __user *buf, size_t count, loff_t *offset)
{
	unsigned short i;
	unsigned char byte, bit;
	ssize_t ret;
	
	raw_spin_lock(&raw_spinlock);
	
	if(count <= 0)
	{
		printk(KERN_ERR "Tried to write spi2pll_lxm2582_gpio with empty data\n");
		ret = 0;
		goto out;
	}
	
	if(count >= USHRT_MAX)
	{
		printk(KERN_ERR "Tried to write spi2pll_lxm2582_gpio with huge data amount (%zu)\n", count);
		ret = 0;
		goto out;
	}
	
	_spi_pll_set_gpio_low(u32LE);
	udelay(1);
	
	for (i = 0; i < (unsigned short)count; i++)
	{
		bit = 7;
		byte = *(buf+i);
		_spi_pll_set_gpio_low(u32SCLK);
		
		while(bit != 255) // unsigned
		{
			if(byte >> bit & 1U)
			{
				_spi_pll_set_gpio_high(u32SDIO);
			}
			else
			{
				_spi_pll_set_gpio_low(u32SDIO);
			}
			_spi_pll_set_gpio_low(u32SCLK);
			udelay(1);
			_spi_pll_set_gpio_high(u32SCLK);
			udelay(1);
			bit--;
		}
		_spi_pll_set_gpio_low(u32SCLK);
	}
	udelay(1);
	_spi_pll_set_gpio_high(u32LE);
	udelay(1);
	_spi_pll_set_gpio_low(u32LE);
	
	ret = 0; // same as in decompiled original spi2pll_lxm2582_gpio from Rigol...
	
out:
	udelay(1);
	raw_spin_unlock(&raw_spinlock);
	
	return ret;
}

static const struct file_operations spi_pll_fops = {
	.owner           = THIS_MODULE,
	.open            = spi_pll_open,
	.unlocked_ioctl  = spi_pll_ioctl,
	.write           = spi_pll_write
};

static int spi_pll_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	add_uevent_var(env, "DEVMODE=%#o", 0666);
	return 0;
}


static int _spi_pll_set_gpio_direction(unsigned int gpio, char *label/*, int dir*/)
{
	struct gpio_desc *desc;
	int ret;
  
	if(gpio < 0x200)
	{
		ret = gpio_request(gpio, label);
		if(ret == 0)
		{
			desc = gpio_to_desc(gpio);
			gpiod_direction_output_raw(desc, 0);
		}
		else
		{
			printk("xxxxx request gpio:%u failed!\n",gpio);
			gpio_free(gpio);
		}
	}
	else
	{
		ret = 0; // same as in decompiled original spi2pll_lxm2582_gpio from Rigol...
	}
	return ret;
}

static int __init spi_pll_init(void)
{
	printk(KERN_INFO "xxxxx spi_pll_init ...\n"); // same as in Rigol decompiled module...
	
	if(alloc_chrdev_region(&dev, 0, 1, "spi_pll_device") < 0)
	{
		printk(KERN_ERR "spi_pll: can't allocate major number\n");
		return -1;
	}
	
	dev_major_number = MAJOR(dev);
	
	spi_pll_class = class_create(THIS_MODULE, "spi_pll_device");
	spi_pll_class->dev_uevent = spi_pll_uevent;
	
	cdev_init(&spi_pll_cdev, &spi_pll_fops);
	spi_pll_cdev.owner = THIS_MODULE;
	
	if((cdev_add(&spi_pll_cdev, MKDEV(dev_major_number, 0), 1)) < 0)
	{
		printk(KERN_ERR "Can't add the device spi_pll_cdev to the system\n");
		goto r_class;
	}
	
	if((device_create(spi_pll_class, NULL, MKDEV(dev_major_number, 0), NULL, "spi_3wires_lxm2582")) < 0)
	{
		printk(KERN_ERR "Register spi_3wires_lxm2582 device failed!\n");
		goto r_device;
	}
	
	_spi_pll_set_gpio_direction(u32LE, "spi_le");
	_spi_pll_set_gpio_high(u32LE);
	
	_spi_pll_set_gpio_direction(u32SCLK, "spi_sclk");
	_spi_pll_set_gpio_direction(u32SDIO, "spi_sdio");
	
	printk("xxxxx SPI driver initialize successful!\n"); // same as in Rigol decompiled module...
	return 0;

r_device:
	class_destroy(spi_pll_class);
r_class:
	unregister_chrdev_region(MKDEV(dev_major_number, 0), 1);
	return -1;
}

static void __exit spi_pll_exit(void)
{
	printk(KERN_WARNING "spi2pll_lxm2582_gpio removed");
	
	gpio_free(u32LE);
	gpio_free(u32SCLK);
	gpio_free(u32SDIO);
	
	device_destroy(spi_pll_class, MKDEV(dev_major_number, 0));

	class_unregister(spi_pll_class);
	class_destroy(spi_pll_class);

	unregister_chrdev_region(MKDEV(dev_major_number, 0), 1);
}

module_init(spi_pll_init);
module_exit(spi_pll_exit);
