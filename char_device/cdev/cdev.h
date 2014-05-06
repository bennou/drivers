#ifndef CHAR_DEV_H
#define CHAR_DEV_H

/************************Driver information**************************/
#define AUTHOR	"Benn.ou"
#define VER		"1.0"

/**********************CDEV FILE STRUCT***************************/
#define MAX_PAGE_SIZE	128			//max byte in one page

/*struct of page position*/
typedef struct ST_PAGE_POS {
	unsigned long page_index;	//index of page
	unsigned long page_offset;	//how many bytes from the start of page to the current position
}PAGE_POS, *PPAGE_POS;

/*struct of page*/
typedef struct CDEV_PAGE {
	void *p_context;			//context in this page
	unsigned long page_size;	//size of page(in byte) 
	struct CDEV_PAGE *next;	//the address of next page
}*PCDEV_PAGE;

/***********************************************************************
*struct of file, this file contains many page, and each page contain many data, but the size
*of page must less than MAX_PAGE_SIZE
************************************************************************/
typedef struct ST_CDEV_FILE {
	PCDEV_PAGE list_head;		//list head: pointing to the start of first page
	PCDEV_PAGE list_tail;			//list tail: pointing to the start of last page
	
	unsigned long file_total_size;		//total size of file(in byte), equal to the value that sum each page size up
	unsigned long page_total_num;	//total page num
	
	unsigned long page_index;		//which page that the file is pointing to 
	unsigned long page_offset;		//which position that the file is pointing to in one page
	
	struct semaphore sem;			//semaphore for this file struct, preventing error occure when read and write at the same time
}CDEV_FILE, *PCDEV_FILE;


#endif
