/*
 * mouse_event.c
 *
 *  Created on: Jan 27, 2023
 *      Author: zkmike
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <vector>
#include <map>
#include <fstream>
#include <iomanip>
#include <cstring>
#include <float.h>
#include <atomic>
#include <semaphore.h>
#include <math.h>
#include <linux/input.h>

#define MOUSEFILE "/dev/input/event0"
#define MOUSE_TIMEOUT               (5)

static int fd;
static struct input_event ie;
//


int mouse_int (void ) {
	if((fd = open(MOUSEFILE, O_RDONLY)) == -1) {
		printf("Device open ERROR\n");
		return (-1);
	}
	return 0;
}

int mouse_getClickevent ( void ) {

	struct timespec tv;
	fd_set rfds;

	/*Setup pselect settings*/
	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);
	tv.tv_sec = MOUSE_TIMEOUT;
	tv.tv_nsec = 0;
	printf("Wait For Mouse Click");
	/*Wait Till The DRP-AI Ends*/
	pselect(fd+1, &rfds, NULL, NULL, &tv, NULL);

	read(fd, &ie, sizeof(struct input_event));

	unsigned char *ptr = (unsigned char*)&ie;
	unsigned char button = 0;
	unsigned char bLeft = 0;
	unsigned char bMiddle = 0;
	unsigned char bRight = 0;
	//
	button=ptr[18];
	bMiddle = (button & 0x02) > 0;
	bRight = button & 0x01;
	bLeft = (button & 0x10) > 0 && (bRight == 0) && (bMiddle == 0);

#if 0
	printf("\nButton:%x\n", button);
	printf("bLEFT:%d, bMIDDLE: %d, bRIGHT: %d \n",bLeft,bMiddle,bRight);
	for(int i=0; i<sizeof(ie); i++)
	{
		printf("%02X ", *ptr++);
	}
	printf("\n");
#endif

	if ( (bLeft == 1) && (bMiddle == 0) && (bRight == 0) )
		return 1;

	return 0;
}

void mouse_close (void ) {
	printf("Mouse Device Closed\n");
	close(fd);
}
