#include<linux/kernel.h> //  main kernel header file
#include<linux/init.h>
#include<linux/module.h>
#include<linux/slab.h>		//kmalloc
#include<linux/usb.h>		//usb operation
#include<linux/blkdev.h>
#include<linux/genhd.h>		// gendisck structure
#include<linux/spinlock.h>
#include<linux/bio.h>
#include<linux/fs.h>		//file operation
#include<linux/interrupt.h>
#include<linux/workqueue.h>		//Work queue creation
#include<linux/sched.h>		//Scheduling bottom half
#include<linux/delay.h>

#define HONOR_VID  0x12D1	//HONOR mobile device treated as usb device	
#define HONOR_PID  0x107E		

#define DEVICE_NAME "Honor"
#define BOMS_GET_MAX_LUN              0xFE			//for getting maximum lun
#define BOMS_REQTYPE_LUN              0xA1			//indicate which type of request
#define READ_CAPACITY_LENGTH	      0x08 			//recived byte number after capacity command
#define USB_ENDPOINT_IN		      0x80 				
#define be_to_int32(buf) (((buf)[0]<<24)|((buf)[1]<<16)|((buf)[2]<<8)|(buf)[3])

#define bio_iovec_idx(bio, idx)	(&((bio)->bi_io_vec[(idx)]))
#define __bio_kmap_atomic(bio, idx, kmtype)				\
	(kmap_atomic(bio_iovec_idx((bio), (idx))->bv_page) +	\
		bio_iovec_idx((bio), (idx))->bv_offset)
#define __bio_kunmap_atomic(addr, kmtype) kunmap_atomic(addr)

/*bool __blk_end_request_cur(struct request * rq, int error);		
void __blk_end_request_all(struct request * rq, int error);
struct request * blk_fetch_request(struct request_queue * q);*/
int capacity(void);
struct gendisk *usb_disk = NULL;
int32_t no_of_sectors;
struct usb_device *udev;
uint8_t endpoint_in , endpoint_out ;		//endpoint adress for bulk only devices

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
	06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,   //0
	06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //1
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //2
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //3
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //4
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //5
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //6
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //7
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //8
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //9
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //A
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //B
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //C
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //D
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //E
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //F
}; 


static struct usb_device_id usbdev_table [] = {
	{USB_DEVICE(HONOR_VID, HONOR_PID)},
	
	{} /*terminating entry*/	
};


struct blkdev_private{
        int size;                       
        u8 *data;                       
        short users;                    
        short media_change;             
        spinlock_t lock;                
        struct request_queue *queue;    
        struct gendisk *gd;             
      };	


struct request *req;

static struct blkdev_private *device = NULL;

static int send_mass_storage_command(struct usb_device *udev,uint8_t endpoint, uint8_t lun,
                         uint8_t *cdb, uint8_t direction, int data_length, uint32_t *ret_tag)
{
	
	static uint32_t tag = 1;
	uint8_t cdb_len;
	int r, size;
	struct command_block_wrapper *cbw;
	cbw=(struct command_block_wrapper *)kmalloc(sizeof(struct command_block_wrapper),GFP_KERNEL);
	
	if (cdb == NULL) {
		return -1;
	}
	if (endpoint & USB_ENDPOINT_IN) {
		printk("Send mass storage command: cannot send command on IN endpoint\n");
		return -1;
	}	
	cdb_len = cdb_length[cdb[0]];
	if ((cdb_len == 0) || (cdb_len > sizeof(cbw->CBWCB))) {
		printk(KERN_INFO "Send mass storage command: don't know how to handle this command (%02X, length %d)\n",
			cdb[0], cdb_len);
		return -1;
	}	

	memset(cbw, 0, sizeof(*cbw));
	cbw->dCBWSignature[0] = 'U';
	cbw->dCBWSignature[1] = 'S';
	cbw->dCBWSignature[2] = 'B';
	cbw->dCBWSignature[3] = 'C';
	*ret_tag = tag;
	cbw->dCBWTag = tag++;
	cbw->dCBWDataTransferLength = data_length;
	cbw->bmCBWFlags = direction;
	cbw->bCBWLUN =0;
	cbw->bCBWCBLength = cdb_len;
	memcpy(cbw->CBWCB, cdb, cdb_len);
	

	r = usb_bulk_msg(udev,usb_sndbulkpipe(udev,endpoint),(void*)cbw,31, &size,1000);
	if(r!=0)
		printk("Failed command transfer %d",r);
	
	return 0;
} 

// capacity
int capacity()
{
	int r=0, r1=0, r2=0, size;
	uint8_t cdb[16];	
	uint8_t *lun=(uint8_t *)kmalloc(sizeof(uint8_t),GFP_KERNEL);
	uint32_t expected_tag;
	uint8_t *buffer=(uint8_t *)kmalloc(64*sizeof(uint8_t),GFP_KERNEL);
	size = 0;
	r = usb_control_msg(udev, usb_sndctrlpipe(udev,0), BOMS_GET_MAX_LUN, BOMS_REQTYPE_LUN, 0, 0, (void*)lun, 1, 1000);
	printk(KERN_INFO "\nReading Capacity:\n");
	memset(buffer, 0, sizeof(buffer));
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = 0x25;	
	send_mass_storage_command(udev,endpoint_out,*lun,cdb,USB_ENDPOINT_IN,READ_CAPACITY_LENGTH,&expected_tag);
	r2 = usb_bulk_msg(udev,usb_rcvbulkpipe(udev,endpoint_in),(void*)buffer, 24,&size, 5000);
	no_of_sectors = be_to_int32(&buffer[0]);
	printk(KERN_INFO"\nMax LBA: %ld\n", no_of_sectors);

	return 0;
}

//get status of received data
static int get_mass_storage_status(struct usb_device *udev, uint8_t endpoint, uint32_t expected_tag)
{	
	int r, size;
	struct command_status_wrapper *csw;
	csw=(struct command_status_wrapper *)kmalloc(sizeof(struct command_status_wrapper),GFP_KERNEL);
	r = usb_bulk_msg(udev,usb_rcvbulkpipe(udev,endpoint),(void*)csw,13, &size, 1000);
	if(r<0)
		printk("RECIEVING STATUS MESG ERROR %d",r);
	
	if (size != 13) {
		printk(" get_mass_storage_status: received %d bytes (expected 13)\n", size);
		return -1;
	}
	if (csw->dCSWTag != expected_tag) {
		printk(" get_mass_storage_status: mismatched tags (expected %08X, received %08X)\n",
			expected_tag, csw->dCSWTag);
		return -1;
	}	
	printk(KERN_INFO "Mass Storage Status: %02X (%s)\n", csw->bCSWStatus, csw->bCSWStatus?"FAILED":"Success");
	return 0;
}  

//read
static int read_fun(sector_t start_sector,sector_t sectors,char *page_address)
{
	int result;
	unsigned int size;
	uint8_t cdb[16];	// SCSI Command Descriptor block
	uint8_t *lun = (uint8_t *)kmalloc(sizeof(uint8_t),GFP_KERNEL);
	uint32_t expected_tag;
	size = 0;

	memset(cdb,0,sizeof(cdb));
	cdb[0] = 0x28;	// Read(10)
	cdb[2] = (start_sector>>24) & 0xFF;
	cdb[3] = (start_sector>>16) & 0xFF;
	cdb[4] = (start_sector>>8) & 0xFF;
	cdb[5] = (start_sector>>0) & 0xFF;
	cdb[7] = (sectors>>8) & 0xFF;
	cdb[8] = (sectors>>0) & 0xFF;	

	send_mass_storage_command(udev,endpoint_out,*lun,cdb,USB_ENDPOINT_IN,(sectors*512),&expected_tag);
	result = usb_bulk_msg(udev,usb_rcvbulkpipe(udev,endpoint_in),(unsigned char*)(page_address),(sectors*512),&size, 5000);

	get_mass_storage_status(udev, endpoint_in, expected_tag);  
	return 0;
}

//writes into the sector of usb
static int write_fun(sector_t start_sector,sector_t sectors,char *page_address)
{   
	int result;
	unsigned int size;
	uint8_t cdb[16];	// SCSI Command Descriptor Block
	uint8_t *lun = (uint8_t *)kmalloc(sizeof(uint8_t),GFP_KERNEL);
	uint32_t expected_tag;
	
	printk(KERN_INFO "Write into sector:");
	memset(cdb,0,sizeof(cdb));
	cdb[0] = 0x2A;
	cdb[2] = (start_sector>>24)&0xFF;
	cdb[3] = (start_sector>>16)&0xFF;
	cdb[4] = (start_sector>>8)&0xFF;
	cdb[5] = (start_sector>>0)&0xFF;
	cdb[7] = (sectors>>8)&0xFF;
	cdb[8] = (sectors>>0)&0xFF;	// 1 block
	//cdb[8]=0x01;
	send_mass_storage_command(udev,endpoint_out,*lun,cdb,USB_DIR_OUT,sectors*512,&expected_tag);
	result = usb_bulk_msg(udev,usb_sndbulkpipe(udev,endpoint_out),(unsigned char*)page_address,sectors*512,&size, 1000);
	get_mass_storage_status(udev, endpoint_in, expected_tag); 
	return 0;

}  

//function for transfer data from and to the sector
static void rb_transfer(sector_t sector,sector_t nsect, char *buffer, int write)
{
   
     if (write)
        write_fun(sector,nsect,buffer);
    else
        read_fun(sector,nsect,buffer);
    return; 
} 

//requests for sector information
static int send_req(struct request *req)
{
    
    int i;
    sector_t address;
    int dir = rq_data_dir(req);
    struct bio_vec bvec;
    struct req_iterator iter;
    sector_t sector_offset;
    unsigned int sectors;
    sector_t start_sector = blk_rq_pos(req);
    unsigned int sector_cnt = blk_rq_sectors(req); /* no of sector 
							*on which operation to be done*/
    sector_t sector = req->bio->bi_iter.bi_sector;
    sector_offset = 0;
   

    rq_for_each_segment(bvec,req,iter){
        sectors = bvec.bv_len / 512;
		address = start_sector+sector_offset;
    	char *buffer = __bio_kmap_atomic(req->bio, i, KM_USER0);
    	rb_transfer(address,sectors,buffer, dir==WRITE);
    	sector_offset += sectors;
    	__bio_kunmap_atomic(req->bio, KM_USER0);
	printk(KERN_DEBUG "My disk: Start Sector: %llu, Sector Offset: %llu; Buffer: %p; Length: %u sectors\n",\
		(unsigned long long)(start_sector), (unsigned long long)(sector_offset), buffer, sectors);
    }
    return 0;  
}  

static struct workqueue_struct *my_queue=NULL;
	struct usb_work{   
	struct work_struct work; 
	struct request *req;
 };

static void delayed_work(struct work_struct *work)
{
	struct usb_work *usb_work=container_of(work,struct usb_work,work);
	send_req(usb_work->req);
	__blk_end_request_cur(usb_work->req,0);
	kfree(usb_work);
	return;
}

void usb_request(struct request_queue *q)  
{
	struct request *req;  
	int sectors_xferred;
	struct usb_work *usb_work = NULL;
  
	

	while((req = blk_fetch_request(q)) != NULL)
	{
		if(req == NULL && !blk_rq_is_passthrough(req)) 
		{
			printk(KERN_INFO "non FS request");
			__blk_end_request_all(req, -EIO);
			continue;
		}
		

		usb_work=(struct usb_work *)kmalloc(sizeof(struct usb_work),GFP_ATOMIC);
		if(usb_work==NULL){

			printk("Memory Allocation to deferred work failed");
			__blk_end_request_all(req, 0);
			continue;
		}

		usb_work->req = req;
		INIT_WORK(&usb_work->work,delayed_work);
		queue_work(my_queue,&usb_work->work);
		
	}	
} 

static int my_open(struct block_device *bdev, fmode_t mode)
{
    struct blkdev_private *dev = bdev->bd_disk->private_data;
    spin_lock(&dev->lock);
    if (!dev->users) 
        check_disk_change(bdev);	
    dev->users++;
    spin_unlock(&dev->lock);
    return 0;
}

static void my_release(struct gendisk *gdisk, fmode_t mode)
{
    struct blkdev_private *dev = gdisk->private_data;
    spin_lock(&dev->lock);
    dev->users--;
    spin_unlock(&dev->lock);

    return 0;
}

static struct block_device_operations blkdev_ops =
{
	.owner= THIS_MODULE,
	.open=my_open,
	.release=my_release
};


//THis fuction detects the USB device plugged into the system, whether its known or unknown
static int usbdev_probe(struct usb_interface *interface, const struct usb_device_id *id)
{	
	int i;
	unsigned char epAddr, epAttr;
	udev=interface_to_usbdev(interface);
	struct usb_endpoint_descriptor *endpoint;
	if(id->idProduct == HONOR_PID && id->idVendor == HONOR_VID )
	{
		printk(KERN_ALERT "Known USB drive detected\n");
		printk(KERN_ALERT "VID: %02X\n",id->idVendor);
		printk(KERN_ALERT "PID: %02X\n",id->idProduct);
	}
	else
	{
		printk(KERN_INFO "\nUnknown device plugged_in\n");
	}
	printk(KERN_ALERT "Interface Class: %02X\n",interface->cur_altsetting->desc.bInterfaceClass);
	printk(KERN_ALERT "Interface SubClass: %02X \n",interface->cur_altsetting->desc.bInterfaceSubClass);
	printk(KERN_ALERT "Interface Protocol: %02X\n",interface->cur_altsetting->desc.bInterfaceProtocol);
	printk(KERN_ALERT "Number of endpoints= %d\n",interface->cur_altsetting->desc.bNumEndpoints);	
		
	for (i = 0; i < interface->cur_altsetting->desc.bNumEndpoints; i++) 
			{
				endpoint = &interface->cur_altsetting->endpoint[i].desc;
			 	epAddr = endpoint->bEndpointAddress;
	        	epAttr = endpoint->bmAttributes;
		        if((epAttr & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK)
		        {
			       if(epAddr & 0x80)
				     {   
				     	 endpoint_in = endpoint->bEndpointAddress;
				         printk(KERN_ALERT "EP %d is Bulk IN\n", i);
				     }
			       else
				      {   
				      	  endpoint_out = endpoint->bEndpointAddress;
    				      printk(KERN_ALERT "EP %d is Bulk OUT\n", i);
				      }
		        } 
		    printk(KERN_INFO "Endpoint[%d]'s address: %02X\n", i, endpoint->bEndpointAddress);
			}

	capacity();
	int c = 0;
	c = register_blkdev(0, "usb_disk");  // Major no. allocation
	if (c < 0) 
		printk(KERN_WARNING "usb_disk: unable to get major number\n");
	device = kmalloc(sizeof(struct blkdev_private),GFP_KERNEL); // Private structre allocation 
	if(!device)
	{
		printk("ENOMEM  at %d\n",__LINE__);
		return 0;
	}
	memset(device, 0, sizeof(struct blkdev_private)); // Initializes all device value as 0
	spin_lock_init(&device->lock);  
	device->queue = blk_init_queue(usb_request, &device->lock);  
	usb_disk = device->gd = alloc_disk(2); 
	usb_disk->major =c;
	usb_disk->first_minor = 0;
	usb_disk->fops = &blkdev_ops;
	usb_disk->queue = device->queue;
	usb_disk->private_data = device;
	strcpy(usb_disk->disk_name, DEVICE_NAME);
	set_capacity(usb_disk,no_of_sectors);  
	add_disk(usb_disk);  

return 0;
}

// for disconnection of USB device
static void usbdev_disconnect(struct usb_interface *interface)
{
	printk(KERN_INFO "USB Device Removed\n");
	struct gendisk *usb_disk = device->gd;
	del_gendisk(usb_disk);
	blk_cleanup_queue(device->queue);
	kfree(device);
	return;
}



static struct usb_driver usbdev_driver = {
	name: "usbdev",  
	probe: usbdev_probe, 
	disconnect: usbdev_disconnect, 
	id_table: usbdev_table, 
};

//initializing block device driver
int block_init(void)
{
	usb_register(&usbdev_driver);
	printk(KERN_NOTICE "UAS READ Capacity Driver Inserted\n");
	printk(KERN_INFO "USB Registered\n"); 
	my_queue=create_workqueue("my_queue"); 
	return 0;	
}

//exiting block device driver kernel
void block_exit(void)
{ 
	usb_deregister(&usbdev_driver);
	printk(KERN_NOTICE "Leaving Kernel\n");
	flush_workqueue(my_queue); 
	destroy_workqueue(my_queue);
	return;
}


module_init(block_init);
module_exit(block_exit);

MODULE_DESCRIPTION("Assignment3");
MODULE_AUTHOR("Ashutosh khare and Soutrik mukhopadhyay");
MODULE_LICENSE("GPL");
