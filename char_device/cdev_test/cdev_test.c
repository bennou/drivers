//#include "cdev_test.h"
#include <stdio.h>
#include <memory.h>
#include <fcntl.h>

#define DEV_NAME	"/dev/cdev"

#define BUFF_SIZE		1025

int main(char **argc, int argv)
{
	int fd = 0;
	unsigned char buff[BUFF_SIZE] = {0};
	unsigned char wbuff[BUFF_SIZE] = {0};
	unsigned long ret = -1;
	unsigned long i = 0;

	memset(buff, 0, BUFF_SIZE);
	
	printf("cdev_test: main function enter, enjoy it!\n");
	fd = open(DEV_NAME, O_RDWR);
	if (fd <= 0)
	{
		printf("Sorry! fail to open %s, fd=%d\n", DEV_NAME,fd);
		return fd;
	}	
	printf("cdev_test: open %s succesfully, fd=%d\n", DEV_NAME, fd);

	ret = read(fd, buff, BUFF_SIZE);
	if (ret < 0) {
		printf("%s: fail to read buffer, ret=%ld\n", __func__, ret);
		return ret;
	}
	printf("%s: read buffer size=%ld\n", __func__, ret);

	for (i = 0; i < ret; i++) {
		printf("%d, ", buff[i]);
	}
	printf("\n");

	for (i = 0; i < BUFF_SIZE; i++) {
		wbuff[i] = BUFF_SIZE - i;
	}
	
	ret = write(fd, wbuff, BUFF_SIZE);
	if (ret < 0) {
		printf("%s: fail to write buffer, ret=%ld\n", __func__, ret);
		return ret;
	}
	printf("%s: write buffer size=%ld\n", __func__, ret);

	memset(buff, 0, BUFF_SIZE);
	ret = read(fd, buff, BUFF_SIZE);
	if (ret < 0) {
		printf("%s: fail to read buffer, ret=%ld\n", __func__, ret);
		return ret;
	}
	printf("%s: read buffer size=%ld\n", __func__, ret);

	for (i = 0; i < ret; i++) {
		printf("%d, ", buff[i]);
	}
	printf("\n");
	
	close(fd);
	printf("cdev_test: bye bye!\n");
	return 0;
}
