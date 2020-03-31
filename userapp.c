#include<stdio.h>
#include<stdlib.h>
#include<fcntl.h>
#include<linux/ioctl.h>


#define MAGIC_NUMBER 'A'
#define CHANNEL_SELECT_SEQNO 0x01
#define RESULT_ALIGNMENT_SEQNO 0x02

#define CHANNEL_SELECT _IOW(MAGIC_NUMBER, CHANNEL_SELECT_SEQNO, unsigned long)
#define RESULT_ALIGNMENT _IOW(MAGIC_NUMBER, RESULT_ALIGNMENT_SEQNO, unsigned long)


unsigned int chl;
unsigned int align;
unsigned int res;

int main(int arg,char *files[])
{
	int devicefile, userfile;
	if(arg != 3)
	{
		printf("Usage : copy <oldfile> <newfile>\n");
		exit(1);
	}

	devicefile = open(files[1], O_RDONLY);

	if(devicefile == -1)
	{
		printf("cant open source\n");
		exit(1);
	}

	printf("Enter Channel number of ADC(0 to 7) : \n");
        scanf("%d",&chl);       
        ioctl(devicefile, CHANNEL_SELECT, &chl); 
 
        printf("Enter the Alignment(0 or 1) : \n");
	scanf("%d",&align); 
        ioctl(devicefile, RESULT_ALIGNMENT, &align);
       
 
        printf("Reading value from ADC...\n");
	read(devicefile, &res, 2);
	printf("Value from ADC - %d \n", res);
        close(fd);
	exit(0);
}

