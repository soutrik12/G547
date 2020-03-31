#include<linux/module.h>
#include<linux/kernel.h>
#include<linux/types.h>
#include<linux/kdev_t.h>
#include<linux/fs.h>
#include<linux/device.h>
#include<linux/cdev.h>
#include<linux/uaccess.h>
#include<linux/ioctl.h>
#include<linux/random.h>




static dev_t first_dev;		

static struct cdev c_dev;	
static struct class *my_class;


#define MAGIC_NUMBER 'A'
#define CHANNEL_SELECT_SEQNO 0x01
#define RESULT_ALIGNMENT_SEQNO 0x02

#define CHANNEL_SELECT _IOW(MAGIC_NUMBER, CHANNEL_SELECT_SEQNO, unsigned long)
#define RESULT_ALIGNMENT _IOW(MAGIC_NUMBER, RESULT_ALIGNMENT_SEQNO, unsigned long)


static unsigned long chl = 0;
static unsigned long align = 0;

//Step 4 : Driver callback functions

static int my_open(struct inode *i, struct file *f)
{
	printk(KERN_INFO "ADC8 device : open()\n");
	return 0;
}

static int my_close(struct inode *i, struct file *f)
{
	printk(KERN_INFO "ADC8 device : close()\n");
	return 0;
}

static ssize_t my_read(struct file *f, char __user *buf, size_t length, loff_t *off)
{
	unsigned int res;
	printk(KERN_INFO "ADC8 device : read()\n");
	get_random_bytes(&res, sizeof(res));	
	printk(KERN_INFO "ADC8 device : Random number generation - %d\n",&res);
	res = res % 1024;
	copy_to_user(buf, res, sizeof(res));
	printk(KERN_INFO "ADC8 device : Value read from Channel - %d is %d\n",&chl, &res);
	return 0;
}

static long my_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{

	switch(cmd){
		case CHANNEL_SELECT:
		printk(KERN_INFO "ADC8 device : IOCTL - CHANNEL_SELECT\n");
		copy_from_user(&chl, arg, sizeof(chl));
		printk(KERN_INFO "ADC8 device : Selected Channel - %d\n",&chl);
		break;
		case RESULT_ALIGNMENT:
		printk(KERN_INFO "ADC8 device : IOCTL - RESULT_ALIGNMENT\n");
		copy_from_user(&align, arg, sizeof(align));
		printk(KERN_INFO "ADC8 device : selected Alignment - %d\n",&align);
		break;
		default:
		return -ENOTTY;
		}
	return 0;
}

static struct file_operations fops = 
				{
				.owner	= THIS_MODULE,
				.open	= my_open,
				.release= my_close,
				.read	= my_read,
				.unlocked_ioctl = my_ioctl
				};

static int __init adc_init(void)
{
	printk(KERN_INFO "8-Channel ADC Device Registration\n\n");

	//Step-1 : Reserve Major ans Minor Numbers for ADC8
	if(alloc_chrdev_region(&first_dev, 0, 1, "adc8") < 0)
	{
		return -1;
	}
	printk(KERN_INFO "<Major , Minor>: <%d, %d>\n", MAJOR(first_dev), MINOR(first_dev));

	//Step-2 : Creation of ADC8 device file
	if((cls = class_create(THIS_MODULE, "chardev")) == NULL)
	{
		unregister_chrdev_region(first_dev,1);
		return -1;
	}
	
	if(device_create(cls, NULL, first_dev, NULL, "adc8") == NULL)
	{
		class_destroy(cls);
		unregister_chrdev_region(first_dev,1);
		return -1;
	}

	//Step-3 : Link fops and cdev to the device node
	cdev_init(&c_dev, &fops);
	if(cdev_add(&c_dev, first_dev, 1) == -1)
	{
		device_destroy(cls, first_dev);
		class_destroy(cls);
		unregister_chrdev_region(first_dev,1);
		return -1;
	}

	return 0;
}

static void __exit adc_exit(void)
{
	cdev_del(&c_dev);
	device_destroy(cls, first_dev);
	class_destroy(cls);
	unregister_chrdev_region(first_dev, 1);
	printk(KERN_INFO "8-Channel ADC Device Unregistrated");	
}

module_init(adc_init);
module_exit(adc_exit);

MODULE_DESCRIPTION("8 Channel ADC");
MODULE_LICENSE("GPL");
