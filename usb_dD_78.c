#include<linux/kernel.h>
#include<linux/module.h>
#include<linux/usb.h>
#include<linux/timer.h>
#include<linux/slab.h>

#define HDD_1_VID  0x03F0
#define HDD_1_PID  0x5A07

#define HDD_2_VID  0x0bc2
#define HDD_2_PID  0xac19



#define USB_EP_IN                   0x81
#define USB_EP_OUT                  0x02

#define READ_CAPACITY_LENGTH          0x08
#define RETRY_MAX                     5
#define REQUEST_SENSE_LENGTH          0x12

#define BOMS_GET_MAX_LUN              0xFE
#define be_to_int32(buf) (((buf)[0]<<24)|((buf)[1]<<16)|((buf)[2]<<8)|(buf)[3])

#define BOMS_RESET		     0xff
#define BOMS_RESET_REQ_TYPE          0x21

int send_mass_storage_command(struct usb_device*, uint8_t, uint8_t,uint8_t*, uint8_t, int, uint32_t*);
int get_mass_storage_status(struct usb_device*, uint8_t, uint32_t);
int read_capacity(struct usb_device*, uint8_t, uint8_t);

struct command_block_wrapper {
	uint8_t dCBWSignature[4];
	uint32_t dCBWTag;
	uint32_t dCBWDataTransferLength;
	uint8_t bmCBWFlags;
	uint8_t bCBWLUN;
	uint8_t bCBWCBLength;
	uint8_t CBWCB[16];
};


struct command_status_wrapper {
	uint8_t dCSWSignature[4];
	uint32_t dCSWTag;
	uint32_t dCSWDataResidue;
	uint8_t bCSWStatus;
};


static uint8_t cdb_length[256] = {
//	 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
	06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //  0
	06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //  1
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  2
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  3
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  4
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  5
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  6
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  7
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //  8
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //  9
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //  A
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //  B
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  C
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  D
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  E
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  F
};

struct usbdev_private
{
	struct usb_device *udev;
	unsigned char class;
	unsigned char subclass;
	unsigned char protocol;
	unsigned char ep_in;
	unsigned char ep_out;
};

struct usbdev_private *p_usbdev_info;

static void usbdev_disconnect(struct usb_interface *interface)
{
	printk(KERN_INFO "USBDEV Device Removed\n");
	return;
}

static struct usb_device_id usbdev_table [] = {
	{USB_DEVICE(SAN_VID, SAN_PID)},
	{}	
};

int send_mass_storage_command(struct usb_device *device, uint8_t endpoint_out, uint8_t lun,uint8_t *cdb, uint8_t direction, int data_length, uint32_t *ret_tag)  

{
	static uint32_t tag = 1;
	uint8_t cdb_len;
	int i, r, size;
	struct command_block_wrapper *cbw;
	cbw = (struct command_block_wrapper*)kmalloc(sizeof(struct command_block_wrapper), GFP_KERNEL);
	
	if ( cbw == NULL ) {
		printk(KERN_INFO "Cannot allocate memory\n");
		return -1;}

	if (cdb == NULL) {
		return -1;
	}
	if (endpoint_out & USB_EP_IN) 
	{
		printk(KERN_INFO "send_mass_storage_command: cannot send command on IN endpoint\n") ;
		return -1;
	}

	cdb_len = cdb_length[cdb[0]];

	if ((cdb_len == 0) || (cdb_len > sizeof(cbw -> CBWCB))) 
	{
		printk(KERN_INFO "send_mass_storage_command: don't know how to handle this command (%02X, length %d)\n", cdb[0], cdb_len);
		return -1;
	}
	cdb_len = cdb_length[cdb[0]];
	
	memset(cbw,'\0',sizeof(struct command_block_wrapper));
	cbw->dCBWSignature[0] = 'U';
	cbw->dCBWSignature[1] = 'S';
	cbw->dCBWSignature[2] = 'B';
	cbw->dCBWSignature[3] = 'C';
	*ret_tag = tag;
	cbw->dCBWTag = tag++;
	cbw->dCBWDataTransferLength = data_length;
	cbw->bmCBWFlags = direction;
	cbw->bCBWLUN = lun;
	// Subclass is 1 or 6 => cdb_len
	cbw->bCBWCBLength = cdb_len;
	memcpy(cbw->CBWCB, cdb, cdb_len);

	r= usb_bulk_msg(device,usb_sndbulkpipe(device,endpoint_out), (unsigned char*)&cbw, 31, &size, 1000);

	i = 0;
	do {
		// The transfer length must always be exactly 31 bytes.
		r= usb_bulk_msg(device,usb_sndbulkpipe(device,endpoint_out), (unsigned char*)cbw, 31, &size, 1000);
		printk(KERN_INFO"After usb_bulk_msg, r = %d\n",r);
		if (r != 0) 
		{
			usb_clear_halt(device, usb_sndbulkpipe(device,endpoint_out));
				
		}
		i++;
	} while (0);


	printk(KERN_INFO "r=%d \n",r);
	
	if (r !=0) {
		printk(KERN_INFO"   error in send endpoint command\n");
		return -1;
	}
	
	printk(KERN_INFO"   sent %d CDB bytes\n", cdb_len);

	kfree(cbw);
	return 0;
}


int get_mass_storage_status(struct usb_device *device, uint8_t endpoint, uint32_t expected_tag) 
{
	int i, r, size;
	struct command_status_wrapper *csw;

	if( !(csw = (struct command_status_wrapper*)kmalloc(sizeof(struct command_status_wrapper), GFP_KERNEL)) ) 
	{
		printk(KERN_INFO "Cannot allocate memory for command status buffer\n");
		return -1;
	}
	
	i = 0;
	do{
		r = usb_bulk_msg(device, usb_rcvbulkpipe(device,endpoint), (unsigned char*)csw, 13, &size, 1000);
		if (r!=0){
			 usb_clear_halt(device,usb_sndbulkpipe(device,endpoint));
			  }
		i++;
	   } while((r!=0) && (i<5));

	if (r != 0) 
	{
		printk(KERN_INFO "get_mass_storage_status: %d\n",r);
		return -1;
	}

	if (size != 13) 
	{
		printk(KERN_INFO "get_mass_storage_status: received %d bytes (expected 13)\n", size);
		return -1;
	}

	if (csw->dCSWTag != expected_tag) 
	{
		printk(KERN_INFO "get_mass_storage_status: mismatched tags (expected %08X, received %08X)\n",expected_tag, csw->dCSWTag);
		return -1;
	}

	printk(KERN_INFO "Received CSW having status %d\n", csw->bCSWStatus);

	kfree(csw);

	return 0;	
	
}


int read_capacity(struct usb_device *device, uint8_t endpoint_in, uint8_t endpoint_out)
{
	int r, size,read_ep;
	uint8_t lun=0;
	uint32_t expected_tag;
	long max_lba, block_size;
	long device_size;
	uint8_t cdb[16];	// SCSI Command Descriptor Block the SCSI command that we want to send
	uint8_t *buffer=NULL;
	

	if ( !(buffer = (uint8_t *)kmalloc(sizeof(uint8_t)*64, GFP_KERNEL)) ) {
		printk(KERN_INFO"Cannot allocate memory for rcv buffer\n");
		return -1;
	}

	// Read capacity
	printk(KERN_INFO"Reading Capacity:\n");
	
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = 0x16;	// Read Capacity

	r = usb_control_msg(device,usb_sndctrlpipe(device,0),BOMS_RESET,BOMS_RESET_REQ_TYPE,0,0,NULL,0,1000);
	if(r < 0)
		printk(KERN_INFO "Cannot reset\n");
	else
		printk(KERN_INFO "Reset done\n");

	if( send_mass_storage_command(device,endpoint_out,lun,cdb,USB_EP_IN,READ_CAPACITY_LENGTH,&expected_tag) != 0 ) {
		printk(KERN_INFO"Send command error\n");
		return -1;
	}


	//send_mass_storage_command(device, endpoint_out, lun, cdb, USB_EP_IN, READ_CAPACITY_LENGTH, &expected_tag);

	
	read_ep=usb_bulk_msg(device, usb_rcvbulkpipe(device,endpoint_in), (unsigned char*)buffer, READ_CAPACITY_LENGTH, &size, 1000);
	
	

	printk(KERN_INFO"r = %d\n",read_ep);
	//if ( read_ep !=0 ) {
	//	printk(KERN_INFO"Reading endpoint command error\n");
	//	return -1;
	//}
	printk(KERN_INFO"   received %d bytes\n", size);
	max_lba = be_to_int32(buffer);
	block_size = be_to_int32(buffer+4);
	device_size = (max_lba+1)*block_size/(1024*1024*1024);

	printk(KERN_INFO "Device Size: %ld GB\n",device_size);
	kfree(buffer);
	

	get_mass_storage_status(device, endpoint_in,expected_tag);

	return 0;
}



static int usbdev_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	int i;
	unsigned char epAddr, epAttr;
        unsigned char endpoint_in = 0, endpoint_out = 0;
	struct usb_device *device;
	struct usb_endpoint_descriptor *ep_desc;
	
        device = interface_to_usbdev(interface);

	if((id->idProduct == HDD_1_PID))
	{
		printk(KERN_INFO " HDD_1 drive detected.\n");
	}
	else if((id->idProduct == HDD_2_PID))
	{
		printk(KERN_INFO " HDD_2 drive detected.\n");
	}
	
	else{
		printk(KERN_INFO "Unknown drive detected.\n");
	}

	printk(KERN_INFO "USB DEVICE CLASS : %x", interface->cur_altsetting->desc.bInterfaceClass);
	printk(KERN_INFO "USB DEVICE SUB CLASS : %x", interface->cur_altsetting->desc.bInterfaceSubClass);
	printk(KERN_INFO "USB DEVICE Protocol : %x", interface->cur_altsetting->desc.bInterfaceProtocol);

	printk(KERN_INFO "No. of Altsettings = %d\n",interface->num_altsetting);
	printk(KERN_INFO "No. of Endpoints = %d\n", interface->cur_altsetting->desc.bNumEndpoints);

	for(i=0;i<interface->cur_altsetting->desc.bNumEndpoints;i++)
	{
		ep_desc = &interface->cur_altsetting->endpoint[i].desc;
		epAddr = ep_desc->bEndpointAddress;
		epAttr = ep_desc->bmAttributes;
	
		if((epAttr & USB_ENDPOINT_XFERTYPE_MASK)==USB_ENDPOINT_XFER_BULK)
		{
			if(epAddr & 0x80){
				endpoint_in = ep_desc->bEndpointAddress;
				printk(KERN_INFO "EP %d is Bulk IN - %d \n", i, endpoint_in);
			}
			else{
				endpoint_out = ep_desc->bEndpointAddress;
				printk(KERN_INFO "EP %d is Bulk OUT - %d \n", i, endpoint_out);
			}
		}
	}

	if(read_capacity(device,endpoint_in,endpoint_out)!=0)
	{ 
		printk(KERN_INFO"error \n");
		return -1;
	}

return 0;
}



/*Operations structure*/
static struct usb_driver usbdev_driver = {
	name: "bruce_usb",  
	probe: usbdev_probe, 
	disconnect: usbdev_disconnect, 
	id_table: usbdev_table, 
};


int device_init(void)
{
	usb_register(&usbdev_driver);
	return 0;
}

void device_exit(void)
{
	usb_deregister(&usbdev_driver);
}

module_init(device_init);
module_exit(device_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Soutrik Mukhopadhyay<h20191400078@hyderabad.bits-pilani.ac.in> ");
MODULE DESCRIPTION(" USB DEVICE DETECTOR");
