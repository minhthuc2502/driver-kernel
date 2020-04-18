/**
 * @file    raspchar.c
 * @author  PHAM Minh Thuc
 * @date    7 April 2020
 * @version 0.1
 * @brief   a character driver allows programme in user space read/write to a device virtual. 
 * The programme in user space can communicate with device file in /dev and the module in kernel space can receive data and send it to device.
*/

#include <linux/init.h>             // Macros used to mark up functions e.g., __init __exit
#include <linux/module.h>           // Core header for loading LKMs into the kernel
#include <linux/kernel.h>           // Contains types, macros, functions for the kernel
#include <linux/fs.h>               // structure file for file opened and closed in call system
#include <linux/device.h>           // Header to support the kernel Driver Model
#include <linux/uaccess.h>          // Required for the copy to user function
#include <linux/mutex.h>            // Required for the mutex functionality
#include <linux/slab.h>             // Use for KMalloc, KFree
#include "raspchar.h"
#define DEVICE_NAME "raspberrychar"
#define CLASS_NAME  "rasp"
#define TINY_TTY_MAJOR 240
#define TINY_TTY_MINOR  225

/**********************************************Some informations about developpement driver kernel************************************************************/
// inode is the structure for file disk (fd). When we call open file system in user space, it return a fd.

// struct file is the data structure used in device driver. It represents an open file. Open file
// is created in kernel space and passed to any function that operates on the file until close.

// the difference between struct inode and struct file is that an inode does not track the current position
// within the file or the current mode. It only contains stuff that helps the OS find the contents of the underlying file structure (pipe, directory, regular disk file, block/character device file)
// struct file contains pointer to struct inode and also...

// if read device file:
// Block device : like device storage and memory. Data in block can be cached in memory and read back from cache. writes can be buffered
// character device : like pipes, serial ports. Write and reading to them is an immediate action. Writing a byte to a character device might cause it to be displayed on screen, output on a serial port, converted into a sound, ... Reading a byte from a device might cause the serial port to wait for input, might return a random byte

// if read regular file:
// regular file like file exutable, text ... : the data is handled by a filesystem driver. 

// Driver handles the operations and device file is the interface for communication (udev).

// To add permission for device file. this helps some group or user have the permission to do the operations on this device file and also protect file system
// In /etc/udev/rules.d Add a rules for driver with KERNEL, SUBSYSTEM and MODE or the other priorities of device connected.
/*****************************************************************the end***************************************************************************************/ 

MODULE_LICENSE("GPL");              ///< The license type -- this affects runtime behavior
MODULE_AUTHOR("PHAM Minh Thuc");      ///< The author -- visible when you use modinfo
MODULE_DESCRIPTION("Simple driver replace arm ALD5");  ///< The description -- see modinfo
MODULE_VERSION("0.1");              ///< The version of the module

static char message[256] = {0};  /// Memory for the string that is passed from userspace
static short size_of_message;    ///< Used to remember the size of the string stored
static int numberOpens = 0;      // number of times the device opened
static DEFINE_MUTEX(raspchar_mutex);         // macro to define mutex
static int ret = 0;
//module_param(major, int, 0); ///< Param desc. charp = char ptr, S_IRUGO can be read/not changed
//MODULE_PARM_DESC(major, "major number");  ///< parameter description

typedef struct raspchar_dev {
   unsigned char * control_regs;
   unsigned char * status_regs;
   unsigned char * data_regs;
} raspchar_dev_t;

struct _raspchar_drv {
   int major;
   struct class *raspcharClass;
   struct device *raspcharDevice;
   raspchar_dev_t *raspchar_hw;
} raspchar_drv;

/********************************** Device specific***********************************/
int raspchar_hw_init(raspchar_dev_t *hw)
{
   char * buf;
   buf = kzalloc(NUM_DEV_REGS * REG_SIZE, GFP_KERNEL);
   if(!buf)
   {
      return -ENOMEM; 
   }
   hw->control_regs = buf;
   hw->status_regs = hw->control_regs + NUM_CTRL_REGS;
   hw->data_regs = hw->status_regs + NUM_STS_REGS;

   hw->control_regs[CONTROL_ACCESS_REG] = 0x03;
   hw->status_regs[DEVICE_STATUS_REG] = 0x03;
   return 0;
}

void raspchar_hw_exit(raspchar_dev_t *hw)
{
   kfree(hw->control_regs);
}

/* Read data from rasp char device - harware virtual. If we read data from a real device, instead of using memcpy, using function to read data from device (ex: I2C_READ...*/
int raspchar_hw_read_data(raspchar_dev_t *hw, loff_t start_reg, size_t num_regs, char* kbuf)
{
   int read_bytes = num_regs;

   // Verify weather we can read data from data registers
   if ((hw->control_regs[CONTROL_ACCESS_REG] & CTRL_READ_DATA_BIT) == DISABLE)
      return -1;
   // Verify address of kernel buffer   
   if(kbuf == NULL)
      return -1;
   // Verify position the registers
   if(start_reg > NUM_DATA_REGS)
      return -1;
   // Handle the number of registers read
   if(num_regs > (NUM_DATA_REGS - start_reg))
      read_bytes = NUM_DATA_REGS - start_reg;
   // Write data from device to kernel buffer
   memcpy(kbuf, hw->data_regs + start_reg, read_bytes);
   // Update the number reading
   hw->status_regs[READ_COUNT_L_REG] += 1;
   if(hw->status_regs[READ_COUNT_L_REG] == 0)
      hw->status_regs[READ_COUNT_H_REG] += 1;
   return read_bytes;
}

int raspchar_hw_write_data(raspchar_dev_t *hw, loff_t start_reg, size_t num_regs, char* kbuf)
{
   int write_bytes = num_regs;
   // Verify weather we can write to data register
   if ((hw->control_regs[CONTROL_ACCESS_REG] & CTRL_WRITE_DATA_BIT) == DISABLE)
      return -1;
   // Verify address of kernel buffer
   if (kbuf == NULL)
      return -1;
   // Verify position of register to write on data register
   if (start_reg > NUM_DATA_REGS)
      return -1;
   // Handle number of registers can be written to data register
   if (num_regs > NUM_DATA_REGS - start_reg)
   {
      write_bytes = NUM_DATA_REGS - start_reg;
      hw->status_regs[DEVICE_STATUS_REG] |= STS_DATAREGS_OVERFLOW_BIT;
   }
   // Update data from data register to kernel buffer
   memcpy(hw->data_regs + start_reg,kbuf, write_bytes);
   //printk("data in data register %s\n",hw->data_regs + start_reg);
   // Update number writing
   hw->status_regs[WRITE_COUNT_L_REG] += 1;
   if(hw->status_regs[WRITE_COUNT_L_REG] == 0)
      hw->status_regs[WRITE_COUNT_H_REG] += 1;
   return write_bytes; 
}
/********************************** OS specific ***********************************/

static ssize_t read_function(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
   char *kernel_buf = NULL;
   int num_bytes = 0;

   printk(KERN_INFO "Handle read event from %lld, %zu bytes", *ppos, count);
   kernel_buf = kzalloc(count, GFP_KERNEL);
   if(kernel_buf == NULL)
   {
      return 0;
   }
   
   num_bytes = raspchar_hw_read_data(raspchar_drv.raspchar_hw,*ppos,count,kernel_buf);
   if(num_bytes < 0)
      return -EFAULT;
   if (copy_to_user(buf,kernel_buf,num_bytes))
      return -EFAULT;  
   printk(KERN_INFO "RaspChar: sent %d characters to the user\n",num_bytes);
   *ppos += num_bytes; 
   return num_bytes;
}

static ssize_t write_function(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
   char* kernel_buf = NULL;
   int num_bytes = 0;
   printk(KERN_INFO "raspbChar: Recieved %zu letters from the user\n",count);

   kernel_buf = kzalloc(count,GFP_KERNEL);
   if(copy_from_user(kernel_buf,buf,count))
      return -EFAULT;
   num_bytes = raspchar_hw_write_data(raspchar_drv.raspchar_hw,*ppos,count,kernel_buf);
   printk(KERN_INFO "Write %d bytes to hw", num_bytes);
   if(num_bytes < 0)
      return -EFAULT;
   *ppos += num_bytes;
   return num_bytes;
}

static int  open_function(struct inode *inode, struct file *file)
{
   if(!mutex_trylock(&raspchar_mutex))
   {
      printk(KERN_ALERT "RaspChar: device in use by another process");
      return -EBUSY;
   }
   numberOpens++;
   printk(KERN_INFO "RaspChar: device has been opened %d times\n",numberOpens);
   return 0;
}

static int  release_function(struct inode *inode, struct file *file)
{
   mutex_unlock(&raspchar_mutex);
   printk(KERN_INFO "RaspChar: device has been closed\n");
   return 0;
}

static struct file_operations fops =
{
   read: read_function,
   write: write_function,
   open: open_function,
   release: release_function
};

static int __init kernel_module_init(void)
{
   printk(KERN_INFO "Initializing the RaspberryChar LKM\n");
   // try to dynamically allocate a mojor number
   raspchar_drv.major = register_chrdev(raspchar_drv.major,DEVICE_NAME, &fops);
   if (raspchar_drv.major < 0) {
      printk(KERN_WARNING "Problem with major\n");
      return raspchar_drv.major;
   }
   printk(KERN_INFO "driver arm is charged succesfully with major number %d\n",raspchar_drv.major);
   raspchar_drv.raspcharClass = class_create(THIS_MODULE, CLASS_NAME);
   if (IS_ERR(raspchar_drv.raspcharClass)) {
      unregister_chrdev(raspchar_drv.major,DEVICE_NAME);
      printk(KERN_ALERT "Failed to register device class\n");
      return PTR_ERR(raspchar_drv.raspcharClass);
   }
   printk(KERN_INFO "RaspChar: device class registered correctly\n");
   // Register the device driver
   raspchar_drv.raspcharDevice = device_create(raspchar_drv.raspcharClass, NULL, MKDEV(raspchar_drv.major,0), NULL, DEVICE_NAME);
   if (IS_ERR(raspchar_drv.raspcharDevice)) {
      class_destroy(raspchar_drv.raspcharClass);
      unregister_chrdev(raspchar_drv.major,DEVICE_NAME);
      printk(KERN_ALERT "Failed to create a device\n");
      return PTR_ERR(raspchar_drv.raspcharDevice);
   }

   // Allocate memory for data structure of and initialize driver
   raspchar_drv.raspchar_hw = kzalloc(sizeof(raspchar_dev_t),GFP_KERNEL);
   if(!raspchar_drv.raspchar_hw)
   {
      device_destroy(raspchar_drv.raspcharClass, MKDEV(raspchar_drv.major,0));
      class_destroy(raspchar_drv.raspcharClass);
      unregister_chrdev(raspchar_drv.major,DEVICE_NAME);
      printk(KERN_ERR "failed to allocate data structure of the driver");
      return -ENOMEM;
   }
   ret = raspchar_hw_init(raspchar_drv.raspchar_hw);
   if(ret < 0)
   {
      kfree(raspchar_drv.raspchar_hw);
      device_destroy(raspchar_drv.raspcharClass, MKDEV(raspchar_drv.major,0));
      class_destroy(raspchar_drv.raspcharClass);
      unregister_chrdev(raspchar_drv.major,DEVICE_NAME);
      return ret;
   }
   printk(KERN_INFO "RaspChar: device class is created sucessfully\n");

   mutex_init(&raspchar_mutex);                 /// Initialize the mutex lock
   return 0;
}

static void __exit kernel_module_cleanup(void)
{
   raspchar_hw_exit(raspchar_drv.raspchar_hw);                             // clear device physic
   kfree(raspchar_drv.raspchar_hw);                                        // free data structure
   device_destroy(raspchar_drv.raspcharClass, MKDEV(raspchar_drv.major,0)); //remove device
   class_unregister(raspchar_drv.raspcharClass);               //unregister the device class
   class_destroy(raspchar_drv.raspcharClass);                  //remove the device class
   unregister_chrdev(raspchar_drv.major,DEVICE_NAME);
   mutex_destroy(&raspchar_mutex);
   printk(KERN_INFO "Exit raspchar driver");
}

module_init(kernel_module_init);
module_exit(kernel_module_cleanup);