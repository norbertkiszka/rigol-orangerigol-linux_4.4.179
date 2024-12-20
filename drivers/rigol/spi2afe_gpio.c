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
#include <linux/slab.h>

MODULE_AUTHOR("Norbert Kiszka");
MODULE_DESCRIPTION("spi2afe_gpio_dev");
MODULE_LICENSE("GPL");

#define SPI_AFE_CS_0   76
#define SPI_AFE_CS_1   66
#define SPI_AFE_CS_2   69
#define SPI_AFE_CS_3   68
#define u32SCLK        75
#define u32MOSI        74
#define u32MISO        73

#define SPI_MODE_0 0
#define SPI_MODE_1 1
#define SPI_MODE_2 2
#define SPI_MODE_3 3

static dev_t dev = 0;
static int dev_major_number = 0;
static struct class *spi2afe_class = NULL;
static struct cdev spi2afe_cdev;
//static struct gpio_desc *desc;
static DEFINE_RAW_SPINLOCK(raw_spinlock);
unsigned char spiMode = 0;
unsigned char current_cs = SPI_AFE_CS_0;
unsigned char *afe_read_write_data;

static void inline _spi_afe_set_gpio_high(unsigned int gpio)
{
	gpiod_set_raw_value(gpio_to_desc(gpio), 2); // In a Rigol modules somehow it seams it's 2, not 1 - probably a typo.
}

static void inline _spi_afe_set_gpio_low(unsigned int gpio)
{
	gpiod_set_raw_value(gpio_to_desc(gpio), 0);
}

int _spi_afe_write_byte(unsigned char byte)
{
	unsigned char bit = 7;

	switch(spiMode)
	{
		case SPI_MODE_0:
			_spi_afe_set_gpio_low(u32SCLK);
			while(bit != 255) // unsigned
			{
				if(byte >> bit & 1U)
				{
					_spi_afe_set_gpio_high(u32MOSI);
				}
				else
				{
					_spi_afe_set_gpio_low(u32MOSI);
				}
				bit--;
				_spi_afe_set_gpio_low(u32SCLK);
				udelay(5);
				_spi_afe_set_gpio_high(u32SCLK);
				udelay(5);
			}
		break;
		
		case SPI_MODE_1:
			_spi_afe_set_gpio_low(u32SCLK);
			while(bit != 255) // unsigned
			{
				if(byte >> bit & 1U)
				{
					_spi_afe_set_gpio_high(u32MOSI);
				}
				else
				{
					_spi_afe_set_gpio_low(u32MOSI);
				}
				bit--;
				_spi_afe_set_gpio_high(u32SCLK);
				udelay(5);
				_spi_afe_set_gpio_low(u32SCLK);
				udelay(5);
			}
		break;
		
		case SPI_MODE_2:
			_spi_afe_set_gpio_high(u32SCLK);
			while(bit != 255) // unsigned
			{
				_spi_afe_set_gpio_high(u32SCLK);
				if(byte >> bit & 1U)
				{
					_spi_afe_set_gpio_high(u32MOSI);
				}
				else
				{
					_spi_afe_set_gpio_low(u32MOSI);
				}
				udelay(5);
				bit--;
				_spi_afe_set_gpio_low(u32SCLK);
				udelay(5);
			}
			_spi_afe_set_gpio_high(u32SCLK);
			udelay(5); // Is this needed?
		break;
		
		case SPI_MODE_3:
			_spi_afe_set_gpio_high(u32SCLK);
			while(bit != 255) // unsigned
			{
				if(byte >> bit & 1U)
				{
					_spi_afe_set_gpio_low(u32MOSI);
				}
				else
				{
					_spi_afe_set_gpio_high(u32MOSI);
				}
				bit--;
				_spi_afe_set_gpio_low(u32SCLK);
				udelay(5);
				_spi_afe_set_gpio_high(u32SCLK);
				udelay(5);
			}
		break;
		
		default:
			printk(KERN_ERR "_spi_afe_write_byte: unknown spiMode");
			return -1;
	}
	_spi_afe_set_gpio_low(u32SCLK);
	return 0;
}

static int spi2afe_open(struct inode *inode, struct file *file)
{
	return 0;
}

static long spi2afe_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	if (cmd == 0x7801)
	{
		if(arg > 3)
		{
			printk(KERN_ERR "spi_afe_ioctl: bad spiMode requested (%lu).\n", arg);
			return -1;
		}
		spiMode = (unsigned char)arg;
	}
	else if (cmd == 0x7802)
	{
		printk(KERN_INFO "xxxxx spi_afe_ioctl DAC_INSW_CONTROL ... do nothing ...\n"); // as in Rigol decompiled module
	}
	else
	{
		if (cmd != 0x7800)
		{
			return -1;
		}
		spiMode = SPI_MODE_0;
		switch(arg)
		{
			case 0:
				current_cs = SPI_AFE_CS_0;
			break;
			
			case 1:
				current_cs = SPI_AFE_CS_1;
			break;
			
			case 2:
				current_cs = SPI_AFE_CS_2;
			break;
			
			case 3:
				current_cs = SPI_AFE_CS_3;
			break;
			
			default:
				printk(KERN_ERR "spi_afe_ioctl: unknown CS requested (%lu).", arg);
				return -1;
		}
	}
	return 0;
}

static ssize_t spi2afe_write(struct file *file, const char __user *buf, size_t count, loff_t *offset)
{
	unsigned short i;
	unsigned char byte;
	ssize_t ret;
	unsigned char is_mode_2_or_3;
	
	raw_spin_lock(&raw_spinlock);
	
	if(count <= 0)
	{
		printk(KERN_ERR "Tried to write spi2afe_gpio with empty data\n");
		ret = 0;
		goto out;
	}
	
	if(count >= USHRT_MAX)
	{
		printk(KERN_ERR "Tried to write spi2afe_gpio with huge data amount (%zu)\n", count);
		ret = 0;
		goto out;
	}
	
	_spi_afe_set_gpio_low(current_cs);
	udelay(5);
	
	for (i = 0; i < (unsigned short)count; i++)
	{
		byte = *(buf+i);
		_spi_afe_write_byte(byte);
	}
	udelay(5);
	_spi_afe_set_gpio_high(current_cs);
	udelay(5);
	
	is_mode_2_or_3 = spiMode >= 2;
	
	if (is_mode_2_or_3)
	{
		_spi_afe_set_gpio_low(u32SCLK);
	}
	else
	{
		_spi_afe_set_gpio_high(u32SCLK);
	}
	udelay(5);
	if (is_mode_2_or_3)
	{
		_spi_afe_set_gpio_high(u32SCLK);
	}
	else
	{
		_spi_afe_set_gpio_low(u32SCLK);
	}
	
	ret = count;
	
out:
	udelay(1);
	raw_spin_unlock(&raw_spinlock);
	
	return ret;
}

static ssize_t spi2afe_read(struct file *file, char __user *buf, size_t len, loff_t *f_pos)
{
	unsigned char is_mode_2_or_3;
	unsigned int byte_num;
	unsigned char bit_num;
	unsigned char bit, byte;
	
	raw_spin_lock(&raw_spinlock);
	
	if(*f_pos > 0)
	{
		return 0;
	}
	
	// if(len > 0x100)
	// {
	// 	len = 0x100;
	// }
	
	if(copy_from_user(afe_read_write_data, buf, 1) != 0)
	{
		printk(KERN_ERR "spi2afe copy_from_user failed...\n");
		return -1;
	}
	is_mode_2_or_3 = spiMode >= 2;
	_spi_afe_set_gpio_low(current_cs);
	udelay(5);
	_spi_afe_write_byte(*afe_read_write_data);
	
	for (byte_num = 0; byte_num < (unsigned int)len; byte_num++)
	{
		byte = 0;
		switch(spiMode)
		{
			case SPI_MODE_0:
				_spi_afe_set_gpio_low(u32SCLK);
				bit_num = 0;
				while(bit_num < 8)
				{
					_spi_afe_set_gpio_low(u32SCLK);
					udelay(5);
					_spi_afe_set_gpio_high(u32SCLK);
					bit = gpiod_get_raw_value(gpio_to_desc(u32MISO));
					byte = byte | (bit & 1) << bit_num;
					udelay(5);
					bit_num++;
				}
			break;
			
			case SPI_MODE_1:
				_spi_afe_set_gpio_low(u32SCLK);
				bit_num = 0;
				while(bit_num < 8)
				{
					_spi_afe_set_gpio_high(u32SCLK);
					udelay(5);
					_spi_afe_set_gpio_low(u32SCLK);
					bit = gpiod_get_raw_value(gpio_to_desc(u32MISO));
					byte = byte | (bit & 1) << bit_num;
					udelay(5);
					bit_num++;
				}
			break;
			
			case SPI_MODE_2:
				_spi_afe_set_gpio_high(u32SCLK);
				bit_num = 0;
				while(bit_num < 8)
				{
					_spi_afe_set_gpio_high(u32SCLK);
					udelay(5);
					_spi_afe_set_gpio_low(u32SCLK);
					bit = gpiod_get_raw_value(gpio_to_desc(u32MISO));
					byte = byte | (bit & 1) << bit_num;
					udelay(5);
					bit_num++;
				}
				_spi_afe_set_gpio_high(u32SCLK);
			break;
			
			case SPI_MODE_3:
				_spi_afe_set_gpio_high(u32SCLK);
				bit_num = 0;
				while(bit_num < 8)
				{
					_spi_afe_set_gpio_low(u32SCLK);
					udelay(5);
					_spi_afe_set_gpio_high(u32SCLK);
					bit = gpiod_get_raw_value(gpio_to_desc(u32MISO));
					byte = byte | (bit & 1) << bit_num;
					udelay(5);
					bit_num++;
				}
				_spi_afe_set_gpio_high(u32SCLK);
			break;
			
			default:
				byte = 0;
		}
		_spi_afe_set_gpio_low(u32SCLK);
		afe_read_write_data[byte_num] = byte;
	}
	udelay(5);
	_spi_afe_set_gpio_high(current_cs);
	udelay(5);
	if(is_mode_2_or_3)
	{
		_spi_afe_set_gpio_low(u32SCLK);
	}
	else
	{
		_spi_afe_set_gpio_high(u32SCLK);
	}
	udelay(5);
	if(is_mode_2_or_3)
	{
		_spi_afe_set_gpio_high(u32SCLK);
	}
	else
	{
		_spi_afe_set_gpio_low(u32SCLK);
	}
	
	if(copy_to_user(buf, afe_read_write_data, len) != 0)
	{
		printk(KERN_ERR "spi2afe copy_to_user failed...\n");
		return -1;
	}
	
	*f_pos += len; // otherwise user process will not know that anything was read and it will try again and again...
	
	raw_spin_unlock(&raw_spinlock);
	
	return len;
}

static const struct file_operations spi2afe_fops = {
	.owner           = THIS_MODULE,
	.open            = spi2afe_open,
	.unlocked_ioctl  = spi2afe_ioctl,
	.read            = spi2afe_read,
	.write           = spi2afe_write
};

static int spi2afe_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	add_uevent_var(env, "DEVMODE=%#o", 0666);
	return 0;
}


static int _spi2afe_set_gpio_direction(unsigned int gpio, char *label, unsigned char dir)
{
	if(gpio < 0x200)
	{
		if(gpio_request(gpio, label) == 0)
		{
			if(dir)
			{
				gpiod_direction_input(gpio_to_desc(gpio));
			}
			else
			{
				gpiod_direction_output_raw(gpio_to_desc(gpio), 0);
			}
		}
		else
		{
			printk(KERN_ERR "xxxxx request gpio:%u failed!\n",gpio);
			gpio_free(gpio);
			return 1;
		}
	}
	else
	{
		return 1;
	}
	return 0;
}

static int __init spi2afe_init(void)
{
	if(alloc_chrdev_region(&dev, 0, 1, "spi2afe_device") < 0)
	{
		printk(KERN_ERR "spi2afe: can't allocate major number\n");
		return -1;
	}
	
	dev_major_number = MAJOR(dev);
	
	spi2afe_class = class_create(THIS_MODULE, "spi2afe_device");
	spi2afe_class->dev_uevent = spi2afe_uevent;
	
	cdev_init(&spi2afe_cdev, &spi2afe_fops);
	spi2afe_cdev.owner = THIS_MODULE;
	
	if((cdev_add(&spi2afe_cdev, MKDEV(dev_major_number, 0), 1)) < 0)
	{
		printk(KERN_ERR "Can't add the device spi2afe_cdev to the system\n");
		goto r_class;
	}
	
	if((device_create(spi2afe_class, NULL, MKDEV(dev_major_number, 0), NULL, "spi_simulate")) < 0)
	{
		printk(KERN_ERR "Register spi_simulate device failed!\n");
		goto r_device;
	}
	
	if(_spi2afe_set_gpio_direction(SPI_AFE_CS_0, "spi_afe_cs_0", 0)){goto r_device;}
	if(_spi2afe_set_gpio_direction(SPI_AFE_CS_1, "spi_afe_cs_1", 0)){goto r_device;}
	if(_spi2afe_set_gpio_direction(SPI_AFE_CS_2, "spi_afe_cs_2", 0)){goto r_device;}
	if(_spi2afe_set_gpio_direction(SPI_AFE_CS_3, "spi_afe_cs_3", 0)){goto r_device;}
	if(_spi2afe_set_gpio_direction(u32SCLK,      "spi_afe_sclk", 0)){goto r_device;}
	if(_spi2afe_set_gpio_direction(u32MOSI,      "spi_afe_mosi", 0)){goto r_device;}
	if(_spi2afe_set_gpio_direction(u32MISO,      "spi_afe_miso", 1)){goto r_device;}
	
	_spi_afe_set_gpio_high(SPI_AFE_CS_0);
	_spi_afe_set_gpio_high(SPI_AFE_CS_1);
	_spi_afe_set_gpio_high(SPI_AFE_CS_2);
	_spi_afe_set_gpio_high(SPI_AFE_CS_3);
	
	//afe_read_write_data = (unsigned char *)devm_kmalloc(spi_afe_dev.this_device,0x100,0x24080c0);
	afe_read_write_data = kmalloc(0x100, GFP_ATOMIC);
	
	return 0;

r_device:
	class_destroy(spi2afe_class);
r_class:
	unregister_chrdev_region(MKDEV(dev_major_number, 0), 1);
	return -1;
}

static void __exit spi2afe_exit(void)
{
	printk(KERN_WARNING "spi2afe_gpio removed");
	
	gpio_free(SPI_AFE_CS_0);
	gpio_free(SPI_AFE_CS_1);
	gpio_free(SPI_AFE_CS_2);
	gpio_free(SPI_AFE_CS_3);
	gpio_free(u32SCLK);
	gpio_free(u32MOSI);
	gpio_free(u32MISO);
	
	device_destroy(spi2afe_class, MKDEV(dev_major_number, 0));

	class_unregister(spi2afe_class);
	class_destroy(spi2afe_class);

	unregister_chrdev_region(MKDEV(dev_major_number, 0), 1);
	
	kfree(afe_read_write_data);
}

module_init(spi2afe_init);
module_exit(spi2afe_exit);
