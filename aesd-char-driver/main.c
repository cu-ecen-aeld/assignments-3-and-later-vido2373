/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/uaccess.h> // copy_*_user()
#include <linux/string.h>
#include <linux/slab.h> // kmalloc()
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Vishnu Dodballapur"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
	struct aesd_dev* aesd_dev_ptr;
	PDEBUG("open");
	/**
	 * TODO: handle open
	 */

	aesd_dev_ptr = container_of(inode->i_cdev, struct aesd_dev, cdev);
	filp->private_data = aesd_dev_ptr;
	return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
	PDEBUG("release");
	/**
	 * TODO: handle release
	 */
	return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
	ssize_t retval = 0;
	struct aesd_dev* my_dev = (struct aesd_dev *)(filp->private_data);
	size_t buf_offs = 0;
	size_t bytes_remaining = count;
	size_t num_bytes_read = 0;
	size_t bytes_to_read = 0;
	struct aesd_buffer_entry* entry = NULL;
	size_t entry_offs;

	PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
	/**
	 * TODO: handle read
	 */

	if (mutex_lock_interruptible(&my_dev->lock)) {
		return -ERESTARTSYS;
	}

	while (bytes_remaining > 0) {
		entry = aesd_circular_buffer_find_entry_offset_for_fpos(&my_dev->queue, *f_pos, &entry_offs);

        // f_pos is beyond length of device
		if (entry == NULL) {
			break;
		}
		bytes_to_read = entry->size - entry_offs;
        if (bytes_to_read > count) {
            bytes_to_read = count;
        }

		num_bytes_read = bytes_to_read - copy_to_user(&buf[buf_offs], &entry->buffptr[entry_offs], bytes_to_read);
		if (num_bytes_read != bytes_to_read) {
			PDEBUG("Wanted %ld bytes, got %ld bytes\n", bytes_to_read, num_bytes_read);
		}
		bytes_remaining -= num_bytes_read;
		*f_pos += num_bytes_read;
		buf_offs += num_bytes_read;
		retval += num_bytes_read;
	}

    // If we are at EOF, reset f_pos to 0
    if (retval == 0) {
        *f_pos = 0;
    }

	mutex_unlock(&my_dev->lock);
	return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
	ssize_t retval = -ENOMEM;
	size_t bytes_missed = 0;
	char* newline_found = NULL;
	const char* overwritten = NULL;
	struct aesd_dev* my_dev = (struct aesd_dev *)(filp->private_data);

	PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
	/**
	 * TODO: handle write
	 */
	if (mutex_lock_interruptible(&my_dev->lock)) {
		return -ERESTARTSYS;
	}

	// If no data saved, malloc new buffer, else realloc
	if (my_dev->temp_entry.size == 0) {
		my_dev->temp_entry.buffptr = kmalloc(count, GFP_KERNEL);
		if (my_dev->temp_entry.buffptr == 0) {
			goto cleanup;
		}
	}
	else {
		my_dev->temp_entry.buffptr = krealloc(my_dev->temp_entry.buffptr, my_dev->temp_entry.size + count, GFP_KERNEL);
		if (my_dev->temp_entry.buffptr == 0) {
			goto cleanup;
		}
	}

	// Save buffer into temp_entry
	bytes_missed = copy_from_user((void *)(&my_dev->temp_entry.buffptr[my_dev->temp_entry.size]), buf, count);
	
	// Check correct number of bytes were saved
	if (bytes_missed != 0) {
		printk(KERN_WARNING "%ld bytes missed on copy_from_user()\n", bytes_missed);
	}
	retval = count - bytes_missed;
	my_dev->temp_entry.size += retval;

	newline_found = (char *)memchr(my_dev->temp_entry.buffptr, '\n', my_dev->temp_entry.size);
	// Write to queue if newline is found
	if (newline_found) {
		// Add entry to queue, free oldest entry if full
		overwritten = aesd_circular_buffer_add_entry(&my_dev->queue, &my_dev->temp_entry);
		if (overwritten) {
			kfree(overwritten);
		}
        my_dev->temp_entry.buffptr = 0;
		my_dev->temp_entry.size = 0;
	}

    // Offset doesn't matter for write, so we set it to the beginning for read ops
    *f_pos = 0;

  cleanup:
	mutex_unlock(&my_dev->lock);
	return retval;
}
struct file_operations aesd_fops = {
	.owner =    THIS_MODULE,
	.read =     aesd_read,
	.write =    aesd_write,
	.open =     aesd_open,
	.release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
	int err, devno = MKDEV(aesd_major, aesd_minor);

	cdev_init(&dev->cdev, &aesd_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &aesd_fops;
	err = cdev_add (&dev->cdev, devno, 1);
	if (err) {
		printk(KERN_ERR "Error %d adding aesd cdev", err);
	}
	return err;
}



int aesd_init_module(void)
{
	dev_t dev = 0;
	int result;
	result = alloc_chrdev_region(&dev, aesd_minor, 1,
			"aesdchar");
	aesd_major = MAJOR(dev);
	if (result < 0) {
		printk(KERN_WARNING "Can't get major %d\n", aesd_major);
		return result;
	}
	memset(&aesd_device,0,sizeof(struct aesd_dev));

	/**
	 * TODO: initialize the AESD specific portion of the device
	 */
	mutex_init(&aesd_device.lock);
	result = aesd_setup_cdev(&aesd_device);

	if( result ) {
		unregister_chrdev_region(dev, 1);
	}
	return result;

}

void aesd_cleanup_module(void)
{
	dev_t devno = MKDEV(aesd_major, aesd_minor);

	cdev_del(&aesd_device.cdev);

	/**
	 * TODO: cleanup AESD specific poritions here as necessary
	 */
	aesd_circular_buffer_free(&aesd_device.queue);
    if (aesd_device.temp_entry.buffptr != 0) {
        kfree(aesd_device.temp_entry.buffptr);
        aesd_device.temp_entry.size = 0;
    }

	unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
