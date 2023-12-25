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

#include "aesdchar.h"
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/types.h>
int aesd_major = 0; // use dynamic major
int aesd_minor = 0;

MODULE_AUTHOR("Sreekanth Pasumarthy"); /** DONE: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp) {
  struct aesd_dev *adevicePtr;
  PDEBUG("open");
  adevicePtr = container_of(inode->i_cdev, struct aesd_dev, cdev);
  adevicePtr->aesd_working_entry.size = 0;
  adevicePtr->aesd_working_entry.buffptr = NULL;
  filp->private_data = adevicePtr;
  PDEBUG("open done");
  return 0;
}

int aesd_release(struct inode *inode, struct file *filp) {
  struct aesd_dev *devp;
  PDEBUG("release");
  devp = (struct aesd_dev *)filp->private_data;
  mutex_destroy(&devp->aesd_dev_buf_lock);
  PDEBUG("release done");
  return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                  loff_t *f_pos) {
  ssize_t retval = 0;
  struct aesd_dev *devp;
  struct aesd_buffer_entry *readEntryPtr;
  size_t offWithinEntry;
  devp = (struct aesd_dev *)filp->private_data;
  PDEBUG(
      "Read: %zu bytes with offset %lld filp->fpos=%lld devp->readoffset=%ld",
      count, *f_pos, filp->f_pos, devp->readOffset);
  PDEBUG("Read: mutex locking");
  mutex_lock(&devp->aesd_dev_buf_lock);
  PDEBUG("Read: obtained read mutex");

  if (devp->numEntriesInBuffer <= 0) {
    PDEBUG("Read: Buffer empty, return 0");
    devp->numEntriesInBuffer = 0;
    devp->readOffset = 0;
    devp->writeOffset = 0;
    retval = 0;
    goto ret_done;
  }
  readEntryPtr = aesd_circular_buffer_find_entry_offset_for_fpos(
      &devp->aesd_dev_buffer, (size_t)(devp->readOffset), &offWithinEntry);
  if (readEntryPtr == NULL) {
    PDEBUG("Read: not able to read from circular buffer returning 0");
    retval = 0;
    goto ret_done;
  } else {
    size_t retsize = readEntryPtr->size - offWithinEntry;
    PDEBUG("Read: readEntry size = %ld offwithinentry = %ld retsize = %ld",
           readEntryPtr->size, offWithinEntry, retsize);
    PDEBUG("Read: returning %s", (readEntryPtr->buffptr + offWithinEntry));
    if (copy_to_user(buf, readEntryPtr->buffptr + offWithinEntry, retsize)) {
      retval = -EFAULT;
      PDEBUG("Read: error copying to user");
      goto ret_done;
    }
    PDEBUG("Read: Copy to user done");
    devp->numEntriesInBuffer -= 1;
    devp->readOffset += retsize;
    filp->f_pos = devp->readOffset;
    PDEBUG("Read: updated fpos to %ld", devp->readOffset);
    retval = retsize;
    kfree(readEntryPtr->buffptr);
    PDEBUG("Read: num valid entries in buffer %d", devp->numEntriesInBuffer);
  }
ret_done:
  PDEBUG("Read: mutex unlocking");
  mutex_unlock(&devp->aesd_dev_buf_lock);
  PDEBUG("Read: returning %ld ", retval);
  return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                   loff_t *f_pos) {
  ssize_t retval = -ENOMEM;
  struct aesd_dev *devp;
  const char *to;
  size_t memSzReqd, memSzNotCopiedFromUser, totalBufSize;

  PDEBUG("Write: %zu bytes with offset %lld", count, *f_pos);
  devp = (struct aesd_dev *)filp->private_data;

  PDEBUG("Write: mutex locking.");
  mutex_lock(&devp->aesd_dev_buf_lock);
  PDEBUG("Write: obtained write mutex ");

  memSzReqd = devp->aesd_working_entry.size + count;
  PDEBUG("Write: allocating %ld bytes of kernel memory", memSzReqd);
  devp->aesd_working_entry.buffptr =
      krealloc(devp->aesd_working_entry.buffptr, memSzReqd, GFP_KERNEL);
  if (devp->aesd_working_entry.buffptr == NULL) {
    PDEBUG("Write: error allocating memory for buffer size %ld",
           memSzReqd);
    goto ret_done;
  }
  PDEBUG("Write: mem allocation succesful");

  // append user data to current buffer, starting from where we left off last
  // time
  to = devp->aesd_working_entry.buffptr + devp->aesd_working_entry.size;
  memSzNotCopiedFromUser = copy_from_user((void *)to, buf, count);
  if (memSzNotCopiedFromUser != 0) {
    // Only partially copied. Since ret were not copied,
    // implies count -ret were copied
    retval = count - memSzNotCopiedFromUser;
  } else {
    // successfully copied all count bytes from user
    retval = count;
  }
  PDEBUG("Write: num not copied from user is %ld, asked is %ld, retval is %ld",
         memSzNotCopiedFromUser, count, retval);
  PDEBUG("Write: user msg is %s", to);

  totalBufSize = devp->aesd_working_entry.size + retval;
  devp->aesd_working_entry.size = totalBufSize;
  if (devp->aesd_working_entry.buffptr[totalBufSize - 1] == '\n') {
    // received new line, so move working contents to the
    // circular buffer
    struct aesd_buffer_entry *retPtr;
    retPtr = aesd_circular_buffer_add_entry(&devp->aesd_dev_buffer,
                                            &devp->aesd_working_entry);
    if (retPtr != NULL) {
      PDEBUG("Write: buffer entry was overwritten. releasing memory");
      PDEBUG("Write: lost %s", retPtr->buffptr);
      devp->writeOffset -= retPtr->size;
      // devp->readOffset -= retPtr->size;
      kfree(retPtr->buffptr);
    }
    // reset working entry as we stored this in circular buffer now
    devp->aesd_working_entry.size = 0;
    devp->aesd_working_entry.buffptr = NULL;
    devp->numEntriesInBuffer += 1;
    PDEBUG("Write: successfully added to circular buffer. Valid entries=%d",
           devp->numEntriesInBuffer);
  }
  // else {
  // Not adding to circular buffer as no new line, stored only in the working
  // entry
  //}
  devp->writeOffset += retval;
  filp->f_pos = devp->writeOffset;
  PDEBUG("Write: updated fpos to %ld", devp->writeOffset);

ret_done:
  PDEBUG("Write: unlocking mutex..");
  mutex_unlock(&devp->aesd_dev_buf_lock);
  PDEBUG("Write: returning %ld", retval);
  return retval;
}

struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev) {
  int err, devno = MKDEV(aesd_major, aesd_minor);

  cdev_init(&dev->cdev, &aesd_fops);
  dev->cdev.owner = THIS_MODULE;
  dev->cdev.ops = &aesd_fops;
  err = cdev_add(&dev->cdev, devno, 1);
  if (err) {
    printk(KERN_ERR "Error %d adding aesd cdev", err);
  }
  return err;
}

int aesd_init_module(void) {
  dev_t dev = 0;
  int result;

  result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
  if (result < 0) {
    printk(KERN_WARNING "Can't get major %d\n", aesd_major);
    return result;
  }
  aesd_major = MAJOR(dev);

  memset(&aesd_device, 0, sizeof(struct aesd_dev));
  aesd_circular_buffer_init(&aesd_device.aesd_dev_buffer);
  mutex_init(&aesd_device.aesd_dev_buf_lock);

  result = aesd_setup_cdev(&aesd_device);
  if (result) {
    unregister_chrdev_region(dev, 1);
  }

  return result;
}

void aesd_cleanup_module(void) {
  dev_t devno = MKDEV(aesd_major, aesd_minor);
  unregister_chrdev_region(devno, 1);
  cdev_del(&aesd_device.cdev);
  mutex_destroy(&aesd_device.aesd_dev_buf_lock);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
