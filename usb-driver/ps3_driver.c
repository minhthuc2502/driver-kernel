#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>

static int ps3_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
    printk(KERN_INFO "ps3 drive (%04X:%04X) plugged\n", id->idVendor, id->idProduct);
    return 0;
}

static void ps3_disconnect(struct usb_interface *interface)
{
    printk(KERN_INFO "PS3 drive removed\n");
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