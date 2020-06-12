# Description
Character driver and TTY driver in kernel space attaches a device file. This device file can beused in user space to open, read and write to a virtual device and close.

# Add module driver to kernel
Makefile in tty-driver is used to build kernel module in yocto. another Makefile is built directly and: 
- Compile module driver : `make` to receive a file .ko
- Add module to kernel : `insmod [filename].ko
- For character driver and tty driver, using call system in userspace to accès device file in /dev/[device file] to work with kernel
- Remove module from kernel : `rmmod [filename].ko`

# Log
- Using `dmesg` to log processes

# ps3-driver
This driver is a driver kernel for joystick playstation 3
After inserting the driver to your machine, if in dmesg, the events when we hot plug or hot unplug the PS3 doesn't be catched, Try this:

```
$ rmmod [driver actual]
$ //or
$ echo -n [port of device USB] > /sys/bus/usb/drivers/[driver actual]/unbind
$ echo -n [port of device USB] > /sys/bus/usb/drivers/[new driver]]/bind      
```

- Using dmesg to know port of deivce USB (ex. "3-3:1.0")
- Using cat /sys/kernel/debug/usb/devices to see driver binded