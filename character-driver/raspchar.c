/**
 * @file    hello.c
 * @author  PHAM Minh Thuc
 * @date    7 April 2020
 * @version 0.1
 * @brief  An introductory "Hello World!" loadable kernel module (LKM) that can display a message
 * in the /var/log/message file when the module is loaded and removed. The module can accept an
 * argument when it is loaded -- the name, which appears in the kernel log files.
*/

#include <linux/init.h>             // Macros used to mark up functions e.g., __init __exit
#include <linux/module.h>           // Core header for loading LKMs into the kernel
#include <linux/kernel.h>           // Contains types, macros, functions for the kernel
#include <linux/fs.h>               // structure file for file opened and closed in call system
#include <linux/device.h>           // Header to support the kernel Driver Model
#include <linux/uaccess.h>          // Required for the copy to user function
#include <linux/mutex.h>            // Required for the mutex functionality
#include <linux/slab.h>             // Use for KMalloc, KFree
#include <linux/ioctl.h>            // Use for entry point ioctl
#include <linux/proc_fs.h>           // create file system in /proc
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/timer.h>            // Support kernel timer
#include <asm/irq_vectors.h>
#include "raspchar.h"

#define IRQ_NUMBER 11
#define DEVICE_NAME "raspberrychar"
#define CLASS_NAME  "rasp"
#define MAGICAL_NUMBER 240
#define RCHAR_CLR_DATA_REGS _IO(MAGICAL_NUMBER, 0)
#define RCHAR_GET_STS_REGS  _IOR(MAGICAL_NUMBER, 1, sts_reg_t *)
#define RCHAR_RD_DATA_REGS  _IOR(MAGICAL_NUMBER, 2, unsigned char *)
#define RCHAR_WR_DATA_REGS  _IOW(MAGICAL_NUMBER, 3, unsigned char *)

typedef struct {
   unsigned char read_count_h_reg;
   unsigned char read_count_l_reg;
   unsigned char write_count_h_reg;
   unsigned char write_count_l_reg;
   unsigned char device_status_reg;
} sts_reg_t;

// inode is the structure for file disk (fd). When we call open file system in user space, it return a fd.
// struct file is the data structure used in device driver. It represents an open file. Open file
// is created in kernel space and passed to any function that operates on the file until close.
// the difference between strict inode and struct file is that an inode does not track the current position
// within the file or the current mode. It only contains stuff that helps the OS find the contents of the underlying file structure (pipe, directory, regular disk file, block/character device file)
// struct file contains pointer to struct inode and also...
// if read device file:
// Block device : like device storage and memory. Data in block can be cached in memory and read back from cache. writes can be buffered
// character device : like pipes, serial ports. Write and reading to them is an immediate action. Writing a byte to a character device might cause it to be displayed on screen, output on a serial port, converted into a sound, ... Reading a byte from a device might cause the serial port to wait for input, might return a random byte
// if read regular file:
// regular file like file exutable, text ... : the data is handled by a filesystem driver. 

// Driver is in the below and user can access driver through file device in file system.
// Driver handles the operations and device file is the interface for communication.
// To add permission for device file. this helps some group or user have the permission to do the operations on this device file and also protect file system
// In /etc/udev/rules.d Add a rules for driver with KERNEL, SUBSYSTEM and MODE 
MODULE_LICENSE("GPL");              ///< The license type -- this affects runtime behavior
MODULE_AUTHOR("PHAM Minh Thuc");      ///< The author -- visible when you use modinfo
MODULE_DESCRIPTION("Simple driver replace arm ALD5");  ///< The description -- see modinfo
MODULE_VERSION("0.1");              ///< The version of the module

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
   volatile uint32_t intr_cnt;
   struct timer_list raspchar_ktimer;
} raspchar_drv;

typedef struct raspchar_ktimer_data {
   int param1;
   int param2;
} raspchar_ktimer_data_t;
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

int rchar_hw_clear(raspchar_dev_t *hw)
{
   if((hw->control_regs[CONTROL_ACCESS_REG] & CTRL_WRITE_DATA_BIT) == DISABLE)
      return -1;
   memset(hw->data_regs, 0, NUM_DATA_REGS * REG_SIZE);
   hw->status_regs[DEVICE_STATUS_REG] &= ~STS_DATAREGS_OVERFLOW_BIT;
   return 0;
}

void rchar_hw_get_status(raspchar_dev_t *hw, sts_reg_t *status)
{
   memcpy(status, hw->status_regs, NUM_STS_REGS * REG_SIZE);
}

void vchar_hw_enable_read(raspchar_dev_t *hw, unsigned char isEnable)
{
   if(isEnable == ENABLE)
   {
      // Enable bit inform that data is ready read
      hw->status_regs[DEVICE_STATUS_REG] |= STS_READ_ACCESS_BIT;
      // Enable bit give the permit reading
      hw->control_regs[CONTROL_ACCESS_REG] |= CTRL_READ_DATA_BIT;
   }
   else
   {
      // Disable bit inform that data is ready read
      hw->status_regs[DEVICE_STATUS_REG] &= ~STS_READ_ACCESS_BIT;
      // Disable bit give the permit reading
      hw->control_regs[CONTROL_ACCESS_REG] &= ~CTRL_READ_DATA_BIT;
   }
}

void vchar_hw_enable_write(raspchar_dev_t *hw, unsigned char isEnable)
{
   if(isEnable == ENABLE)
   {
      // Enable bit inform that data is ready written
      hw->status_regs[DEVICE_STATUS_REG] |= STS_WRITE_ACCESS_BIT;
      // Enable bit give the permit write
      hw->control_regs[CONTROL_ACCESS_REG] |= CTRL_WRITE_DATA_BIT;
   }
   else
   {
      // Disable bit inform that data is ready written
      hw->status_regs[DEVICE_STATUS_REG] &= ~STS_WRITE_ACCESS_BIT;
      // Disable bit give the permit writing
      hw->control_regs[CONTROL_ACCESS_REG] &= ~CTRL_WRITE_DATA_BIT;
   }
}

// Function handle interrupt
irqreturn_t raspchar_hw_isr(int irq, void *dev)
{
   /*Handle the stuff of top-half*/
   raspchar_drv.intr_cnt++;
   /*Handle the stuff of bottom-half*/

   return IRQ_HANDLED;
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

static long ioctl_function(struct file *file, unsigned int cmd, unsigned long arg)
{
   unsigned char isReadEnable;
   unsigned char isWriteEnable;
   sts_reg_t status;
   ret = 0;
   printk(KERN_INFO "Handle event ioctl (cmd: %u)\n", cmd);
   switch(cmd) {
      case RCHAR_CLR_DATA_REGS:
         ret = rchar_hw_clear(raspchar_drv.raspchar_hw);
         if (ret < 0)
            printk(KERN_INFO "Raspchar: Can not clear data on data registers");
         else
            printk(KERN_INFO "Raspchar: Data registers are cleared");
         break;
      case RCHAR_GET_STS_REGS:
         rchar_hw_get_status(raspchar_drv.raspchar_hw,&status);
         (void)copy_to_user((sts_reg_t*)arg, &status, sizeof(status));
         printk(KERN_INFO "Raspchar: Got information status register");
         break;
      case RCHAR_RD_DATA_REGS: 
         (void)copy_from_user(&isReadEnable, (unsigned char *)arg, sizeof(isReadEnable));
         vchar_hw_enable_read(raspchar_drv.raspchar_hw,isReadEnable);
         printk(KERN_INFO "Raspchar: changed permit of reading");
         break;
      case RCHAR_WR_DATA_REGS: 
         (void)copy_from_user(&isWriteEnable, (unsigned char *)arg, sizeof(isWriteEnable));
         vchar_hw_enable_write(raspchar_drv.raspchar_hw,isWriteEnable);
         printk(KERN_INFO "Raspchar: changed permit of writing");
         break;
      default:
         break;
   }
   return ret;
}

static void *raspchar_seq_start(struct seq_file *s, loff_t *off)
{
   char *msg = kmalloc(256, GFP_KERNEL);
   if(msg == NULL)
   {
      printk(KERN_ERR "seq start: Can't allocate the memoire for seq file");
      return NULL; 
   }

   sprintf(msg, "message(%lld): size(%zu), from(%zu), count(%zu), index(%lld), read_pos(%lld)", *off, s->size, s->from, s->count, s->index, s->read_pos);
   printk(KERN_INFO "seq_start: *pos(%lld)", *off);
   return msg;
}

static int raspchar_seq_show(struct seq_file *s, void *pdata)
{
   char *msg = pdata;
   // write message to buffer of seq file
   seq_printf(s, "%s\n", msg);
   printk(KERN_INFO "seq_show: %s\n", msg);
   return 0;
}

static void *raspchar_seq_next(struct seq_file *s, void *pdata, loff_t *off)
{
   char *msg = pdata;
   ++*off; 
   printk(KERN_INFO "seq_next: *pos(%lld)\n", *off);
   sprintf(msg, "message(%lld): size(%zu), from(%zu), count(%zu), index(%lld), read_pos(%lld)", *off, s->size, s->from, s->count, s->index, s->read_pos);
   return msg;
}

static void raspchar_seq_stop(struct seq_file *s, void *pdata)
{
   printk(KERN_INFO "seq_stop\n");
   kfree(pdata);
}

static struct seq_operations seq_ops = {
   .start = raspchar_seq_start,
   .next = raspchar_seq_next,
   .stop = raspchar_seq_stop,
   .show = raspchar_seq_show
};

static int raspchar_proc_open(struct inode *inode, struct file *file)
{
   printk(KERN_INFO "Handle event open on proc file\n");
   return seq_open(file, &seq_ops);
}

static int raspchar_proc_release(struct inode * inode, struct file *file)
{
   printk(KERN_INFO "Handle event close on proc file\n");
   return seq_release(inode, file);   
}

static ssize_t raspchar_proc_read(struct file *file, char __user *buf, size_t count, loff_t *off)
{
   /**This is the code for read procfs in case we don't care about the case that if char driver return to user space a big reponse.
    * For example if buffer of user is 1024 bytes but the reponse is 1050 bytes => error. So we need to use sequence file to devide the reponse into many reponse in sequence and return to user**/
   /*
   char tmp[256];
   unsigned int idx = 0;
   unsigned int count_read, count_write;
   sts_reg_t status_reg;

   printk(KERN_INFO "Handle event read on proc file from %lld and %zu bytes\n",*off,count);
   if(*off > 0)
      return 0;
   rchar_hw_get_status(raspchar_drv.raspchar_hw,&status_reg);
   count_read = status_reg.read_count_h_reg << 8 | status_reg.read_count_l_reg;
   count_write = status_reg.write_count_h_reg << 8 | status_reg.write_count_l_reg;

   idx += sprintf(tmp + idx, "Read_count: %u\n",count_read);
   idx += sprintf(tmp + idx, "Write_count: %u\n",count_write);
   idx += sprintf(tmp + idx, "device_status: 0x%02x\n",status_reg.device_status_reg);

   copy_to_user(buf,tmp,idx);
   *off += idx;
   return idx;*/
   printk(KERN_INFO "Hqndle reading event on proc file at %lld and %zu bytes",*off, count);
   if (*off >= 131072) //user buffer of cat in user space is 131072 bytes
      printk(KERN_INFO "Don't worry about the size of buffer user");
   return seq_read(file, buf, count, off); 
}


/*
static ssize_t raspchar_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *off)
{
   printk(KERN_INFO "No action to handle writing event, procfs read-only\n");
   return count;
}
*/

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
   release: release_function,
   unlocked_ioctl: ioctl_function
};

static struct file_operations proc_fs = {
   .open = raspchar_proc_open,
   .release = raspchar_proc_release,
   .read = raspchar_proc_read,
   //.write = raspchar_proc_write,
};

static void handle_timer(struct timer_list *ktimer)
{
   /*
   if(!pdata) {
      printk(KERN_ERR "can not handle a NULL pointer");
      return;
   }
   // Handle data
   ++pdata->param1;
   --pdata->param2;
   */
   asm("int $0x38");
   printk(KERN_INFO "[CPU %d] interrupt counter %d\n",smp_processor_id(), raspchar_drv.intr_cnt);
   mod_timer(&raspchar_drv.raspchar_ktimer, jiffies + 10*HZ);
}

static void configure_timer(struct timer_list *ktimer)
{
   /*
   static raspchar_ktimer_data_t data = {
      .param1 = 0,
      .param2 = 0
   };*/
   ktimer->expires = jiffies + 10*HZ;
}

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
   ret = request_irq(IRQ_NUMBER, raspchar_hw_isr,IRQF_SHARED,"raspchar_dev",&raspchar_drv.raspcharDevice);
   if (ret)
   {
      raspchar_hw_exit(raspchar_drv.raspchar_hw);
      kfree(raspchar_drv.raspchar_hw);
      device_destroy(raspchar_drv.raspcharClass, MKDEV(raspchar_drv.major,0));
      class_destroy(raspchar_drv.raspcharClass);
      unregister_chrdev(raspchar_drv.major,DEVICE_NAME);
      printk(KERN_ERR "Failed to register IRQ\n");
      return ret;
   }
   // Create proc file 
   if(NULL == proc_create("raspchar_proc",0666,NULL,&proc_fs))
   {
      printk(KERN_ERR "Failed to create file in procfs\n");
      free_irq(IRQ_NUMBER,&raspchar_drv.raspcharDevice);
      raspchar_hw_exit(raspchar_drv.raspchar_hw);
      kfree(raspchar_drv.raspchar_hw);
      device_destroy(raspchar_drv.raspcharClass, MKDEV(raspchar_drv.major,0));
      class_destroy(raspchar_drv.raspcharClass);
      unregister_chrdev(raspchar_drv.major,DEVICE_NAME);
   }

   timer_setup(&raspchar_drv.raspchar_ktimer,handle_timer,TIMER_IRQSAFE);
   configure_timer(&raspchar_drv.raspchar_ktimer);
   add_timer(&raspchar_drv.raspchar_ktimer);
   printk(KERN_INFO "RaspChar: device class is created sucessfully\n");
   mutex_init(&raspchar_mutex);                 /// Initialize the mutex lock
   return 0;
}

static void __exit kernel_module_cleanup(void)
{
   printk(KERN_INFO "Raspchar: Exit raspchar driver");
   del_timer(&raspchar_drv.raspchar_ktimer);
   remove_proc_entry("raspchar_proc",NULL);
   free_irq(IRQ_NUMBER,&raspchar_drv.raspcharDevice);
   raspchar_hw_exit(raspchar_drv.raspchar_hw);                             // clear device physic
   kfree(raspchar_drv.raspchar_hw);                                        // free data structure
   device_destroy(raspchar_drv.raspcharClass, MKDEV(raspchar_drv.major,0)); //remove device
   //class_unregister(raspchar_drv.raspcharClass);               //unregister the device class
   class_destroy(raspchar_drv.raspcharClass);                  //remove the device class
   unregister_chrdev(raspchar_drv.major,DEVICE_NAME);
   mutex_destroy(&raspchar_mutex);
}

module_init(kernel_module_init);
module_exit(kernel_module_cleanup);