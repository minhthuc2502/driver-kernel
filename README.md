# Description
Character driver in kernel space attaches a device file. This device file can beused in user space to open, read and write to a virtual device and close.

# Add module driver to kernel
- Compile module driver : `make` to receive a file .ko
- Add module to kernel : `insmod [filename].ko`
- Using call system in userspace to accès device file in /dev/raspchar
- Remove module from kernel : `rmmod [filename].ko`

# Log
- Using `dmesg` to log processes 
