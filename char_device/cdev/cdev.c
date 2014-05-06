/*****************************************************************
*Author: Benn.ou
*Date   : 2014-5-5   
*Version: v1.0
*Description: This is an example driver for you to learn how character device works.
*			 it acts as a file, you can write something to it and then read it.
******************************************************************/
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/semaphore.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <asm-generic/errno.h>
#include <asm/uaccess.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include "cdev.h"


/************************Debug print*******************************/

/*when you insmod module, you can add debug information by command 
"insmod cdev.ko cdev_print_flag=1"*/
bool cdev_print_flag = 0;
module_param(cdev_print_flag, bool, S_IRUGO);

#define CDEV_PR(fmt, args...)    	if (cdev_print_flag) printk(KERN_INFO fmt, ##args)								
#define CDEV_WRN(fmt, args...)    	printk(KERN_WARNING fmt, ##args)


/**********************CDEV FILE STRUCT***************************/

/*global parameter*/
static PCDEV_FILE pst_cdev_file = NULL;	//character device file


/*******************************************************************
Description: according the pos, to find out page position
(1) input: 
	pos: how many bytes from the start of page to where you want to find
(2) output:
	page_info: find out which page index and page offset 
(3) return:
	0: translate offset to page position info successfully
	others: translate offset to page position info fail
********************************************************************/
static int offset_to_page_index(loff_t pos, PPAGE_POS page_info)
{
	PCDEV_PAGE page = NULL;
	unsigned long page_size_sum = 0;
	unsigned long index = 0;
	int ret = -1;
	
	if (NULL == page_info) {
		CDEV_WRN("%s: input parameter is null\n", __func__);
		return -1;
	}

	if (down_interruptible(&(pst_cdev_file->sem))) {
		CDEV_WRN("%s: get sem fail\n", __func__);
		return -3;
	}

	CDEV_PR("%s: offset = %lld\n", __func__, pos);

	if (pst_cdev_file->file_total_size <= pos) {
		//pos larger than file size, so the index is the last page
		if (0 != pst_cdev_file->page_total_num) {
			page_info->page_index = pst_cdev_file->page_total_num - 1;
			page_info->page_offset = pst_cdev_file->list_tail->page_size;
		} else {
			//file is empty
			page_info->page_index = 0;
			page_info->page_offset = 0;
		}
		ret = 0;
		goto trans_finish;
	} else {
		//search all page
		page = pst_cdev_file->list_head;
		while (NULL != page) {
			page_size_sum = page_size_sum + page->page_size;

			//if the sum of page size from page(0) to page(index) is larger than pos, then the pos is 
			//pointing to page(index)
			if (pos <= page_size_sum) {
				page_info->page_index = index;
				page_info->page_offset = page->page_size - (page_size_sum - pos);
				ret = 0;
				goto trans_finish;
			}
			page = page->next;
			index++;
		}

		ret = -4;
		CDEV_WRN("%s: [alert] index not found, index=%ld, page total num=%ld\n", \
			__func__, index, pst_cdev_file->page_total_num);
	}

trans_finish:
	up(&(pst_cdev_file->sem));
	return ret;
}

/*******************************************************************
Description: according the start position and copy size, to find out start page and end page
(1) input: 
	offset: how many bytes from the start of page to where you want to find
	size: how many bytes you want to caculate
(2) output:
	start_page: find out the start page index and page offset 
	end_page: find out the end page index and page offset 
(3) return:
	0: get copy range successfully
	others: get range fail
********************************************************************/
static int get_copy_range(size_t size, loff_t offset, PPAGE_POS start_page, PPAGE_POS end_page)
{
	int ret = -1;

	if ((NULL == start_page) || (NULL == end_page)) {
		CDEV_WRN("%s: input pointer is null\n", __func__);
		return -1;
	}

	//get the start page information
	ret = offset_to_page_index(offset, start_page);
	if (0 != ret) {
		CDEV_WRN("%s: get the start page fail, ret=%d\n", __func__, ret);
		return ret;
	}

	//get the end page information
	ret = offset_to_page_index(offset + size, end_page);
	if (0 != ret) {
		CDEV_WRN("%s: get the end page fail, ret=%d\n", __func__, ret);
		return ret;
	}

	return 0;
}

/*******************************************************************
Description: copy all context of the start page and end page to buffer, and return copy size in byte
(1) input: 
	start_page: the start page index and page offset to copy
	end_page: the end page index and page offset to copy	
(2) output:
	pBuf: all context copy to this buffer
	copy_size: how many bytes have been copied
(3) return:
	0: copy range page to buffer successfully
	others: copy to buffer fail
********************************************************************/
static int copy_range_to_buffer(unsigned char *pBuf, unsigned long *copy_size, PPAGE_POS start_page, PPAGE_POS end_page)
{
	PCDEV_PAGE page = NULL;
	char *src = NULL;
	unsigned long cp_size = 0;
	unsigned long index = 0;
	unsigned long has_cp_size = 0;
	int ret = -1;
	
	if ((NULL == pBuf) || (NULL == copy_size) ||(NULL == start_page) || (NULL == end_page)) {
		CDEV_WRN("%s: input pointer is null\n", __func__);
		return -1;
	}

	if ((start_page->page_index > end_page->page_index) ||
		((start_page->page_index == end_page->page_index) && (start_page->page_offset > end_page->page_offset))) {
		CDEV_WRN("%s: start page is larger than end page\n", __func__);
		return -2;
	}

	if (down_interruptible(&(pst_cdev_file->sem))) {
		CDEV_WRN("%s: get sem fail\n", __func__);
		return -4;
	}

	//file is empty
	if (0 == pst_cdev_file->page_total_num) {
		*copy_size = 0;
		CDEV_WRN("%s: file is empty\n", __func__);
		ret = 0;
		goto copy_finish;
	}

	//start page is out of file, so no data will be copied
	if ((start_page->page_index == (pst_cdev_file->page_total_num -1)) &&
		(start_page->page_offset == (pst_cdev_file->list_tail->page_size))) {
		
		*copy_size = 0;
		ret = 0;
		goto copy_finish;
	}

	//search all pages and copy data you needed
	page = pst_cdev_file->list_head;
	while (NULL != page) {
		if (index == start_page->page_index) {
			//all data is in the same page
			if (start_page->page_index == end_page->page_index) {
				src = (char *)page->p_context + start_page->page_offset;
				cp_size = end_page->page_offset - start_page->page_offset;
			} else {
				//data is in the different page
				src = page->p_context + start_page->page_offset;
				cp_size = page->page_size - start_page->page_offset;
			}
			
			memcpy(pBuf+has_cp_size, src, cp_size);
			has_cp_size = has_cp_size + cp_size;

			CDEV_PR("%s: copy first page, index=%ld\n", __func__, index);
			
			//all data is in the same page, after copy and break
			if (start_page->page_index == end_page->page_index) {
				*copy_size = has_cp_size;
				ret = 0;
				goto copy_finish;
			}
		} else if ((index > start_page->page_index) && (index < end_page->page_index)) {
			//copy all data from start page to end page, the first and the last page are excluded
			src = page->p_context;
			cp_size = page->page_size;

			memcpy(pBuf+has_cp_size, src, cp_size);
			has_cp_size = has_cp_size + cp_size;
		} else if (index == end_page->page_index) {
			//copy the last page and break
			src = page->p_context;
			cp_size = end_page->page_offset;
			
			memcpy(pBuf+has_cp_size, src, cp_size);
			has_cp_size = has_cp_size + cp_size;

			CDEV_PR("%s: last page, index=%ld\n", __func__, index);
			*copy_size = has_cp_size;
			ret = 0;
			goto copy_finish;
		}
		page = page->next;
		index++;
	}

	CDEV_WRN("%s: [alert] it can't be here, index=%ld\n", __func__, index);
	ret = -5;

copy_finish:
	#if 0
	CDEV_PR("%s: print read data, size=%ld", __func__, cp_size);
	if (0 == ret) {
		for (index = 0; index < cp_size; index++) {
			CDEV_PR("%ld: %d", index, *(pBuf + index));
		}	
	}
	#endif
	
	up(&(pst_cdev_file->sem));
	return ret;
}

/*******************************************************************
Description: create one page, and copy buffer to page context
(1) input: 
	buff: the start page index and page offset to copy
	size: the end page index and page offset to copy	
(2) output:
	None
(3) return:
	NULL: create one page fail
	NOT NULL: create one page successfully
********************************************************************/
static PCDEV_PAGE create_one_page(unsigned char *buff, unsigned long size)
{
	PCDEV_PAGE page = NULL;
	unsigned long cp_size = 0;
	
	if (NULL == buff) {
		CDEV_WRN("%s: input pointer is null\n", __func__);
		goto page_alloc_fail;
	}

	page = (PCDEV_PAGE)kzalloc(sizeof(struct CDEV_PAGE), GFP_KERNEL);
	if (NULL == page) {
		CDEV_WRN("%s: alloc page fail\n", __func__);
		goto page_alloc_fail;
	}
	
	cp_size = (size > MAX_PAGE_SIZE)  ? MAX_PAGE_SIZE : size;
	
	page->p_context = (void *)kzalloc(cp_size, GFP_KERNEL);
	if (NULL == page->p_context) {
		CDEV_WRN("%s: alloc page context fail\n", __func__);
		goto context_alloc_fail;
	}

	CDEV_PR("%s: copy one page, size=%ld\n", __func__, cp_size);
	memcpy(page->p_context, buff, cp_size);
	page->page_size = cp_size;				//if size != page->page_size, you need to add one more page
	page->next = NULL;
	
	return page;
	
context_alloc_fail:
	if (NULL != page) {
		kfree(page);
	}
page_alloc_fail:
	return NULL;
}


/*******************************************************************
Description: add one page to the tail of list, if the head of list is empty, then create it.
(1) input: 
	page: the page you want to add
(2) output:
	None
(3) return:
	0: add one page to list successfully
	others: add one page to list fail
********************************************************************/
static int add_to_list(PCDEV_PAGE page)
{
	PCDEV_PAGE tmp_page = NULL;
	int ret = -1;
	
	if (NULL == page) {
		CDEV_WRN("%s: input parameter is null\n", __func__);
		return -1;
	}

	if (down_interruptible(&(pst_cdev_file->sem))) {
		CDEV_WRN("%s: get sem fail\n", __func__);
		return -3;
	}

	if (NULL == pst_cdev_file->list_head) {
		//add the first page to the head of list, and the tail of list are pointing to the same page 
		
		pst_cdev_file->list_head = page;		
		pst_cdev_file->list_tail = page;
	} else {
		//add one page to the tail of list
		
		tmp_page = pst_cdev_file->list_tail;
		if (NULL != tmp_page) {
			tmp_page->next = page;
			pst_cdev_file->list_tail = page;
		} else {
			//if list_head is not null and list tail is null, then alert
			CDEV_WRN("%s: list_head is not null and list tail is null\n", __func__);
			ret = -4;
			goto add_list_err;
		}
	}

	pst_cdev_file->file_total_size += page->page_size;
	pst_cdev_file->page_total_num++;
	
	CDEV_PR("%s: page size=%ld, page num=%ld, total=%ld\n", \
		__func__, page->page_size, pst_cdev_file->page_total_num, pst_cdev_file->file_total_size);
	ret = 0;

	#if 0
	//print the page you have added
	unsigned long j = 0;

	if (0 == ret) {
		CDEV_PR("%s: print write data, size=%ld", __func__, pst_cdev_file->list_tail->page_size);
		for (j = 0; j < pst_cdev_file->list_tail->page_size; j++) {
			CDEV_PR("%ld: %d", j, *((unsigned char *)pst_cdev_file->list_tail->p_context + j));
		}
	}
	#endif

add_list_err:
	up(&(pst_cdev_file->sem));

	return ret;
}


/*******************************************************************/
/************************Operaction functions**************************/
/******************************************************************/


/*******************************************************************
Description: driver operate function for "llseek" system call.
********************************************************************/
static loff_t cdev_llseek(struct file *pFile, loff_t offset, int num)
{
	CDEV_PR("%s: enter\n", __func__);
	CDEV_PR("%s: exit\n", __func__);
	return 0;
}

/*******************************************************************
Description: driver operate function for "read" system call.
(1) input: 
	pFile: file struct pointer
	pBuf: user buffer to store data that read from character device
	size: read data size
	pOffset: the current position of file
(2) output:
	None
(3) return:
	nagetive: read fail
	others: size of data you have read
********************************************************************/
static ssize_t cdev_read(struct file *pFile, char __user *pBuf, size_t size, loff_t *pOffset)
{
	ssize_t ret = -1;
	char *buf = NULL;
	unsigned long cp_size = 0;	
	PAGE_POS start_page, end_page;
	
	if ((NULL == pFile) || (NULL == pBuf) || (NULL == pOffset)) {
		CDEV_WRN("%s: input pointer is null\n", __func__);
		return -1;
	}
	CDEV_PR("\n\n%s: start to read(size=%ld, offset=%lld)\n", __func__, size, *pOffset);

	ret = get_copy_range(size, *pOffset, &start_page, &end_page);	
	if (0 != ret) {
		CDEV_WRN("%s: get copy range fail, ret=%ld\n", __func__, ret);
		return -2;
	}

	buf = kzalloc(size, GFP_KERNEL);
	if (NULL == buf) {
		CDEV_WRN("%s: alloc memory fail, size=%ld\n", __func__, size);
		return -3;
	}
	
	ret = copy_range_to_buffer(buf, &cp_size, &start_page, &end_page);
	if (0 != ret) {
		CDEV_WRN("%s: copy range fail, ret=%ld\n", __func__, ret);
		kfree(buf);
		return -4;
	}

	if (0 != copy_to_user(pBuf, buf, cp_size)) {
		CDEV_WRN("%s: copy to user fail, size=%ld\n", __func__, cp_size);
		kfree(buf);
		return -5;
	}

	kfree(buf);
	CDEV_PR("%s: have read size=%ld\n", __func__, cp_size);
	
	return cp_size;
}

/*******************************************************************
Description: driver operate function for "write" system call.
(1) input: 
	pFile: file struct pointer
	pBuf: user buffer to store data that write to character device
	size: write data size
	pOffset: the current position of file
(2) output:
	None
(3) return:
	nagetive: read fail
	others: size of data you have write
********************************************************************/
static ssize_t cdev_write(struct file *pFile, const char __user *pBuf, size_t size, loff_t *pOffset)
{
	char *tmp_buff = NULL;
	char *pointer = NULL;
	size_t cp_size = 0;
	PCDEV_PAGE page = NULL;
	
	if ((NULL == pFile) || (NULL == pBuf) || (NULL == pOffset)) {
		CDEV_WRN("%s: input pointer is null\n", __func__);
		return -1;
	}
	
	CDEV_PR("\n\n%s: start to write(size=%ld, offset=%lld)\n", __func__, size, *pOffset);

	tmp_buff = kzalloc(size, GFP_KERNEL);
	if (NULL == tmp_buff) {
		CDEV_WRN("%s: alloc buff fail, size=%ld\n", __func__, size);
		return -2;
	}

	if (copy_from_user(tmp_buff, pBuf, size)) {
		CDEV_WRN("%s: copy from user buffer fail, size=%ld\n", __func__, size);
		kfree(tmp_buff);
		return -3;
	}

	cp_size = 0;
	pointer = tmp_buff;
	while (cp_size < size) {
		page = create_one_page(pointer, size -cp_size);
		if (NULL !=  page) {
			if (0 != add_to_list(page)) {
				CDEV_WRN("%s: add page to page list fail, has copy size=%ld\n", __func__, cp_size);
				break;
			} else {
				//add one page to page list successfully
				cp_size = cp_size + page->page_size;
				pointer = pointer + page->page_size;
				CDEV_PR("%s: copy one page, size=%ld, total size=%ld\n", __func__, page->page_size, cp_size);
			}
		} else {
			CDEV_WRN("%s: create one page fail, has copy size=%ld\n", __func__, cp_size);
			break;
		}
	}

	kfree(tmp_buff);
	
	CDEV_PR("%s: have write size=%ld\n", __func__, cp_size);
	return cp_size;
}

/*******************************************************************
Description: driver operate function for "open" system call. If you want to operate this
		     character device, you need to call this function first.
(1) input: 
	pInode: file struct pointer
	pFile: file struct pointer
(2) output:
	None
(3) return:
	0: open character device successfully
	others: open character device fail
********************************************************************/
static int cdev_open(struct inode *pInode, struct file *pFile)
{
	pst_cdev_file = (PCDEV_FILE)kzalloc(sizeof(CDEV_FILE), GFP_KERNEL);
	if (NULL == pst_cdev_file) {
		CDEV_WRN("%s: create struct cdev_file fail!\n", __func__);
		return -ENOMEM;
	}
	
	memset(pst_cdev_file, 0, sizeof(CDEV_FILE));
	sema_init(&(pst_cdev_file->sem), 1);
	
	return 0;
}

/*******************************************************************
Description: driver operate function for "close" system call. If you want to close this
		     character device, you need to call this function at the end of your operation.
(1) input: 
	pInode: file struct pointer
	pFile: file struct pointer
(2) output:
	None
(3) return:
	0: open character device successfully
	others: open character device fail
********************************************************************/
static int cdev_release(struct inode *pInode, struct file *pFile)
{
	PCDEV_PAGE pst_temp_page = NULL;
	PCDEV_PAGE pst_next_page = NULL;
	unsigned long i = 0;
	
	if (down_interruptible(&(pst_cdev_file->sem))) {
		CDEV_WRN("%s: get sem fail\n", __func__);
		return -1;
	}
	
	if (NULL != pst_cdev_file) {
		pst_temp_page = pst_cdev_file->list_head;
		CDEV_PR("%s: total page=%ld\n", __func__, pst_cdev_file->page_total_num);

		//release all pages
		for (i = 0; i < pst_cdev_file->page_total_num; i++) {			
			if (NULL != pst_temp_page) {
				if (NULL != pst_temp_page->p_context) {
					kfree(pst_temp_page->p_context);
				}
				pst_temp_page->page_size = 0;
				pst_next_page = pst_temp_page->next;
				kfree(pst_temp_page);
				pst_temp_page = pst_next_page;
			}
		}
		pst_cdev_file->page_total_num = 0;
	}
	up(&(pst_cdev_file->sem));

	if (NULL != pst_cdev_file) {
		kfree(pst_cdev_file);
		pst_cdev_file = NULL;
	}

	return 0;
}


/****************************************************************
*file operation struct, if you want to operate this device, you need to register these
*operate functions first.
*****************************************************************/
static struct file_operations cdev_fops = {
	.owner = THIS_MODULE,
	.llseek = cdev_llseek,
	.read = cdev_read,
	.write = cdev_write,
	.open = cdev_open,
	.release = cdev_release,
};

/*******************************************************************/
/************************Create device********************************/
/******************************************************************/


/**********************CDEV ID***********************************/
#define CDEV_MAJOR_ID		0
#define CDEV_MINOR_ID		0
#define CDEV_NAME		"cdev"

static dev_t cdev_id;								//character device id
static unsigned int cdev_major_id = CDEV_MAJOR_ID;		//major id of character device
static unsigned int cdev_minor_id = CDEV_MINOR_ID;		//minor id of character device

static struct cdev *p_cdev = NULL;				//struct of character device
static struct class *pcdev_class = NULL;		//struct of class, used to create a class file in "/sys/class"
static struct device *pcdev_device = NULL;		//struct of device, used to create a device file in "/dev"


/*******************************************************************
Description: module init, this function will be call when you insmod module
(1) input: 
	None
(2) output:
	None
(3) return:
	0: module initial successfully
	others: module initial fail
********************************************************************/
static int __init cdev_module_init(void)
{
	int ret = 0;

	CDEV_WRN("\n********************CDEV init**********************\n");
	CDEV_WRN("Author: %s\n", AUTHOR);
	CDEV_WRN("Version: %s\n", VER);
	CDEV_WRN("********************************************************\n");
	
	if (cdev_major_id) {
		//static alloc major id
		cdev_id = MKDEV(cdev_major_id, cdev_minor_id);
		ret = register_chrdev_region(cdev_id, 1, CDEV_NAME);
	} else {
		//static alloc major id
		ret = alloc_chrdev_region(&cdev_id, 1, 0, CDEV_NAME);
		cdev_major_id = MAJOR(cdev_id);
		cdev_minor_id = MINOR(cdev_id);
	}
	
	if (0 != ret) {
		CDEV_WRN("%s: fail to register/alloc a character device id\n", __func__);
		goto register_chrdev_region_fail;
	}

	CDEV_PR("%s: major id=%d, minor id=%d\n", __func__, cdev_major_id, cdev_minor_id);

	//t will create a class file in "/sys/class"
	pcdev_class = class_create(THIS_MODULE, CDEV_NAME);
	if (NULL == pcdev_class) {
		CDEV_WRN("%s: fail to create a class\n", __func__);
		ret = -1;
		goto cdev_create_class_fail;
	}

	//it will create a device file in "/dev", according to the major/minor id in "/sys/class/cdev/cdev/dev"
	pcdev_device = device_create(pcdev_class, NULL, cdev_id, 0, CDEV_NAME);
	if (NULL == pcdev_device) {
		CDEV_WRN("%s: fail to create a device\n", __func__);
		ret = -2;
		goto cdev_create_device_fail;
	}

	//alloc a character device struct, and initial operate function
	p_cdev = cdev_alloc();
	if (NULL == p_cdev) {	
		CDEV_WRN("%s: fail to alloc a character device\n", __func__);
		ret = -ENOMEM;
		goto cdev_alloc_fail;
	}	
	p_cdev->owner = THIS_MODULE;
	p_cdev->ops = &cdev_fops;

	//add this character device to kernel
	ret = cdev_add(p_cdev, cdev_id, 1);
	if (0 != ret) {
		CDEV_WRN("%s: Add character device to kernel fail\n", __func__);
		goto cdev_add_fail;
	}

	CDEV_WRN("%s: module initial OK\n", __func__);
	return 0;

cdev_add_fail:
	kfree(p_cdev);
cdev_alloc_fail:
	device_destroy(pcdev_class, cdev_id);
cdev_create_device_fail:
	class_destroy(pcdev_class);	
cdev_create_class_fail:
	unregister_chrdev_region(cdev_id, 1);
register_chrdev_region_fail:	
	return ret;
}

/*******************************************************************
Description: module exit, this function will be call when you rmmod module
(1) input: 
	None
(2) output:
	None
(3) return:
	None
********************************************************************/
static void __exit cdev_module_exit(void)
{
	if (NULL != pcdev_device) {
		device_destroy(pcdev_class, cdev_id);
		CDEV_PR("%s: destroy device from kernel\n", __func__);
	}

	if (NULL != pcdev_class) {
		class_destroy(pcdev_class);
		CDEV_PR("%s: destroy class from kernel\n", __func__);
	}
	
	if (NULL != p_cdev) {
		cdev_del(p_cdev);
		CDEV_PR("%s: delete character device from kernel\n", __func__);
	}
	
	if (cdev_id) {
		unregister_chrdev_region(cdev_id, 1);
		CDEV_PR("%s: unregister character device id=%d\n", __func__, MAJOR(cdev_id));
	}	

	CDEV_WRN("\n********************CDEV exit**********************\n");
		
	return;
}

module_init(cdev_module_init);
module_exit(cdev_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Benn.ou");
MODULE_VERSION("V0.1");
MODULE_DESCRIPTION("This is a char device driver");
