#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/usb.h>

// a device can have multiple interfaces
// a interface bind to a driver specific
// when we develope a driver usb, if a device is binded to a driver default in linux, we have 2 methods to bind device to new driver:
// 1. rmmod driver actual
// 2. unbind device to the driver: echo -n "3-3:1.0" > /sys/bus/usb/drivers/usbhid/unbind and then
// echo -n "3-3:1.0" > /sys/bus/usb/drivers/ps3_driver/bind
// to see info of device usb, easier: lsusb
// or : cat /sys/kernel/debug/usb/devices

#define MIN(a, b) ((a <= b) ? a : b)
#define INT_EP_IN 0x81
#define INT_EP_OUT 0x02
#define MAX_PKT_SIZE 64

static struct usb_device *device;
static struct usb_class_driver class;       // identifies driver usb
static unsigned char interrupt_buf[MAX_PKT_SIZE];

static int ps3_open(struct inode *ind, struct file *f) {
    return 0;
}

static int ps3_close(struct inode *ind, struct file *f) {
    return 0;
}

static ssize_t ps3_read(struct file *f, char __user *buf, size_t cnt, loff_t *off) {    // ssize_t for return also error code
    int retval;
    int read_cnt;

    /* Read the data from interrupt endpoint */
    retval = usb_interrupt_msg(device, usb_rcvintpipe(device, INT_EP_IN), interrupt_buf, MAX_PKT_SIZE, &read_cnt, 5000);
    if (retval) {
        printk(KERN_ERR "interrupt message returned %d\n", retval);
        return retval;
    }
    if (copy_to_user(buf, interrupt_buf, MIN(cnt, read_cnt))) {
        printk(KERN_ERR "failed to copy data to user space %d\n", -EFAULT);
        return -EFAULT;
    }

    return MIN(cnt, read_cnt);
}

static ssize_t ps3_write(struct file *f, const char __user *buf, size_t cnt, loff_t *off)
{
    int retval;
    int wrote_cnt = MIN(cnt, MAX_PKT_SIZE);

    if (copy_from_user(interrupt_buf, buf, wrote_cnt)) {
        printk(KERN_ERR "failed to copy data from user space %d\n", -EFAULT);
        return -EFAULT;
    }

    /* Write data into interrupt endpoint*/
    retval = usb_interrupt_msg(device, usb_sndintpipe(device, INT_EP_OUT), interrupt_buf, MIN(cnt, MAX_PKT_SIZE), &wrote_cnt, 5000);
    if (retval) {
        printk(KERN_ERR "interrupt message returned %d\n", retval);
        return retval;
    }

    return wrote_cnt;
}

static struct file_operations fops = {
    .open = ps3_open,
    .release = ps3_close,
    .read = ps3_read,
    .write = ps3_write,
};

static int ps3_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
    struct usb_host_interface *iface_desc;
    struct usb_endpoint_descriptor *endpoint;
    int i;
    int retval;

    iface_desc = interface->cur_altsetting;
    printk(KERN_INFO "ps3 %d interface now pugged : (%04X:%04X)\n", iface_desc->desc.bInterfaceNumber, id->idVendor, id->idProduct);
    printk(KERN_INFO "Num Enpoints: %02X\n", iface_desc->desc.bNumEndpoints);
    printk(KERN_INFO "Interface class: %02X\n", iface_desc->desc.bInterfaceClass);
    for (i = 0; i < iface_desc->desc.bNumEndpoints; i++) {
        endpoint = &iface_desc->endpoint[i].desc;
        printk(KERN_INFO "Endpoint [%d] address %02X\n", i, endpoint->bEndpointAddress);
        printk(KERN_INFO "Endpoint [%d] attribute %02X\n", i, endpoint->bmAttributes);
        printk(KERN_INFO "Endpoint [%d] max packet size %04X (%d)\n", i, endpoint->wMaxPacketSize, endpoint->wMaxPacketSize);
    }

    device = interface_to_usbdev(interface);
    class.name = "usb/ps3%d";
    class.fops = &fops;
    if ((retval = usb_register_dev(interface, &class)) < 0) {
        printk(KERN_ERR "Not able to assign a minor for device usb: %d", retval);
    } else {
        printk(KERN_INFO "Minor obtained: %d\n", interface->minor);
    }
    return retval;
}

static void ps3_disconnect(struct usb_interface *interface)
{
    usb_deregister_dev(interface, &class);
    printk(KERN_INFO "PS3 i/f %d now disconnected\n", interface->cur_altsetting->desc.bInterfaceNumber);
}

static struct usb_device_id ps3_table[] =
{
    { USB_DEVICE(0x054c, 0x0268) },     // 054c:0268
    {} /* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, ps3_table);   // support hot-plugging, if a device in table is plugged,
                                        // if driver is not inserted, kernel will insert it
static struct usb_driver ps3_driver = {
    .name = "ps3_driver",
    .id_table = ps3_table,
    .probe = ps3_probe,
    .disconnect = ps3_disconnect,
};

static int __init ps3_init(void) {
    return usb_register(&ps3_driver);
}

static void __exit ps3_exit(void) {
    usb_deregister(&ps3_driver);
}

module_init(ps3_init);
module_exit(ps3_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("PHAM Minh Thuc");
MODULE_DESCRIPTION("USB PS3 Registration Driver");