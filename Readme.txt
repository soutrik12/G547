
This code identifies a USB Drive connected to a computer and does reading and writing of data from and to the USB Drive.

/**************READ ME *****************/

-> type 'make all' command, it will build main.ko

-> remove the default USB drivers already installed in the kernel by writing the commands:
	a. sudo rmmod uas
	b. sudo rmmod usb_storage

-> then insert the module using 'sudo insmod main.ko'

-> $ sudo fdisk -l or lsblk shows you the partitions made on your device.

-> sudo hd -n 512 /dev/dof shows the MBR

-> for writing and reading from the device, go into root mode, by giving 'sudo -s' command

-> $ dmesg -wH to check the kernel log for device Insert, Open, Close and Remove.

-> $ cat > /dev/dof1 to write into the device

-> You can remove the module by using 'sudo rmmod main'

