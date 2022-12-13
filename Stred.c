#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/semaphore.h>
#define BUFF_SIZE 100
MODULE_LICENSE("Dual BSD/GPL");

dev_t my_dev_id;
static struct class *my_class;
static struct device *my_device;
static struct cdev *my_cdev;

DECLARE_WAIT_QUEUE_HEAD(appendQ);
struct semaphore sem;

char stred[100];
int pos = 0;
int endRead = 0;

int stred_open(struct inode *pinode, struct file *pfile);
int stred_close(struct inode *pinode, struct file *pfile);
ssize_t stred_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset);
ssize_t stred_write(struct file *pfile, const char __user *buffer, size_t length, loff_t *offset);

struct file_operations my_fops =
{
	.owner = THIS_MODULE,
	.open = stred_open,
	.read = stred_read,
	.write = stred_write,
	.release = stred_close,
};


int stred_open(struct inode *pinode, struct file *pfile) 
{
		printk(KERN_INFO "Succesfully opened stred\n");
		return 0;
}

int stred_close(struct inode *pinode, struct file *pfile) 
{
		printk(KERN_INFO "Succesfully closed stred\n");
		return 0;
}

ssize_t stred_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset) 
{
	int ret;
	char buff[BUFF_SIZE];
	long int len = 0;
	if (endRead){
		endRead = 0;
		return 0;
	}

	if(down_interruptible(&sem))
		return -ERESTARTSYS;

	len = scnprintf(buff, BUFF_SIZE, "%s ", stred);
	ret = copy_to_user(buffer, buff, len);
	
	if(ret)
		return -EFAULT;
		
	printk(KERN_INFO "Succesfully read!\n");
	endRead = 1;
	
	up(&sem);

	return len;
}

ssize_t stred_write(struct file *pfile, const char __user *buffer, size_t length, loff_t *offset) 
{
	char buff[BUFF_SIZE];
	char operacija[10];
	char unos[100];
	char upis[100];	
	int ret;
	int temp;

	ret = copy_from_user(buff, buffer, length);
	if(ret)
		return -EFAULT;
	buff[length-1] = '\0';

	if(down_interruptible(&sem))
		return -ERESTARTSYS;

	ret = sscanf(buff,"%10[^ ]s",operacija);
	printk(KERN_INFO "Operation: %s\n", operacija); 
	
	if (ret == 1)
        {
        	if (!(strcmp(operacija, "string=")))
		{
                	sscanf(buff, "%*c%100[^\n]s", unos);
                        strcpy(stred, unos+1);
			pos = (strlen(stred));
                        printk(KERN_INFO "String je upisan!\n");  
                }

		else if(!(strcmp(operacija, "clear")))
		{
			stred[0] = '\0';
			pos = 0;
			printk(KERN_INFO "String je izbrisan!\n");
			wake_up_interruptible(&appendQ);
		}

		else if(!(strcmp(operacija, "shrink")))
		{
			temp = 0;
			while(stred[temp] == ' ')
			{
				temp++;
			}
			strcpy(stred, stred + temp);
			pos -= temp;
			printk(KERN_INFO "Vodeci i prateci SPACE karakteri uklonjeni!\n");
			wake_up_interruptible(&appendQ);
		}

		else if(!(strcmp(operacija, "append=")))
		{
			sscanf(buff, "%*c%100[^\n]s", unos);
			strcpy(upis, unos+1);

			while(pos + strlen(upis) > 99)
			{
				up(&sem);
				if(wait_event_interruptible(appendQ, (pos + strlen(upis) <= 99)))
					return -ERESTARTSYS;
				if(down_interruptible(&sem))
					return -ERESTARTSYS;
			}

			strcat(stred, upis);
			pos += strlen(upis);
			printk(KERN_INFO "String je uspesno dodat!");
		}

		else if(!(strcmp(operacija, "truncate=")))
		{
			sscanf(buff, "%*c%d", &temp);
			if (pos-temp >= 0)
			{
				stred[pos-temp] = '\0';
				printk(KERN_INFO "Uklonjenjo je poslednjih %d karaktera!\n", temp);
				pos -= temp;
				wake_up_interruptible(&appendQ);
			}
			else
			{
				printk(KERN_INFO "Nema toliko karaktera u stringu!\n");
			}
		}

		else if(!(strcmp(operacija, "remove=")))
		{
			sscanf(buff, "%*c%100[^\n]s", unos);
			strcpy(upis, unos+1);

			for(temp = 0; temp <= pos - strlen(upis); temp++)
			{
				if(!(strncmp(stred + temp, upis, strlen(upis))))
				{
					strcpy(stred + temp, stred + temp + strlen(upis));
					pos -= strlen(upis);
					temp--;
				}
			}

			printk(KERN_INFO "Uklonjene su sve \"%s\" sekvence iz stringa!\n", upis);
			wake_up_interruptible(&appendQ);
		}

		else
		{
			printk(KERN_INFO "Nepoznata operacija!\n");
		}
        }
        else
        {
                printk(KERN_WARNING "Pogresan format!\n");
        }

	up(&sem);

	return length;
}

static int __init stred_init(void)
{
   int ret = 0;
   int i=0;

   sema_init(&sem,1);

   //Initialize array
   for (i=0; i<100; i++)
      stred[i] = 0;

   ret = alloc_chrdev_region(&my_dev_id, 0, 1, "stred");
   if (ret){
      printk(KERN_ERR "failed to register char device\n");
      return ret;
   }
   printk(KERN_INFO "char device region allocated\n");

   my_class = class_create(THIS_MODULE, "stred_class");
   if (my_class == NULL){
      printk(KERN_ERR "failed to create class\n");
      goto fail_0;
   }
   printk(KERN_INFO "class created\n");
   
   my_device = device_create(my_class, NULL, my_dev_id, NULL, "stred");
   if (my_device == NULL){
      printk(KERN_ERR "failed to create device\n");
      goto fail_1;
   }
   printk(KERN_INFO "device created\n");

	my_cdev = cdev_alloc();	
	my_cdev->ops = &my_fops;
	my_cdev->owner = THIS_MODULE;
	ret = cdev_add(my_cdev, my_dev_id, 1);
	if (ret)
	{
      printk(KERN_ERR "failed to add cdev\n");
		goto fail_2;
	}
   printk(KERN_INFO "cdev added\n");
   printk(KERN_INFO "Hello world\n");

   return 0;

   fail_2:
      device_destroy(my_class, my_dev_id);
   fail_1:
      class_destroy(my_class);
   fail_0:
      unregister_chrdev_region(my_dev_id, 1);
   return -1;
}

static void __exit stred_exit(void)
{
   cdev_del(my_cdev);
   device_destroy(my_class, my_dev_id);
   class_destroy(my_class);
   unregister_chrdev_region(my_dev_id,1);
   printk(KERN_INFO "Goodbye, cruel world\n");
}


module_init(stred_init);
module_exit(stred_exit);
