Code Description:

	This code identifies a USB Drive connected to a computer and does reading and writing of data from and to the USB Drive respectively. This Code is Written by Ashutosh Khare and Soutrik Mukhopadhyay.

/**************READ ME *****************/

1. Type 'make all' command, it will build main_1.ko

2. Remove the default USB drivers already installed in the kernel by writing the commands:
	a. sudo rmmod uas
	b. sudo rmmod usb_storage

3. Then insert the module using 'sudo insmod main_1.ko'

4. $ dmesg -wH to check the kernel log for device Insert, Open, Close and Remove.

5. You can remove the module by using 'sudo rmmod main_1'

6. By restarting your computer, you can reinstall the default USB Drivers already present in the kernel.
