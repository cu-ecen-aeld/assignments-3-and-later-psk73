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

#include "aesd_ioctl.h"
#include "aesdchar.h"
#include <asm/uaccess.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/init.h>
#include <linux/ioctl.h>
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
  PDEBUG("Read: %zu bytes with offset %lld filp->fpos=%lld "
         "devp->nextReadPosition=%ld",
         count, *f_pos, filp->f_pos, devp->nextReadPosition);
  devp->nextReadPosition = *f_pos;
  PDEBUG("Read: mutex locking");
  mutex_lock(&devp->aesd_dev_buf_lock);
  PDEBUG("Read: obtained read mutex");

  readEntryPtr = aesd_circular_buffer_find_entry_offset_for_fpos(
      &devp->aesd_dev_buffer, (size_t)(devp->nextReadPosition),
      &offWithinEntry);
  if (readEntryPtr == NULL) {
    PDEBUG("Read: not able to read from circular buffer returning 0");
    devp->nextReadPosition = 0;
    retval = 0;
    goto ret_done;
  } else {
    size_t retsize = readEntryPtr->size - offWithinEntry;
    // Return only atmost count bytes..
    if (retsize > count) {
      retsize = count;
    }
    PDEBUG("Read: readEntry size = %ld offwithinentry = %ld retsize = %ld",
           readEntryPtr->size, offWithinEntry, retsize);
    PDEBUG("Read: returning first <%ld> of <%s>", retsize,
           (readEntryPtr->buffptr + offWithinEntry));
    if (copy_to_user(buf, readEntryPtr->buffptr + offWithinEntry, retsize)) {
      retval = -EFAULT;
      PDEBUG("Read: error copying to user");
      goto ret_done;
    }
    PDEBUG("Read: Copy to user done");
    devp->nextReadPosition += retsize;
    *f_pos = *f_pos + retsize;
    PDEBUG("Read: updated fpos to %ld", devp->nextReadPosition);
    retval = retsize;
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
  PDEBUG("Write: working entry buffer=%s", devp->aesd_working_entry.buffptr);
  PDEBUG("Write: working entry size=%ld", devp->aesd_working_entry.size);

  memSzReqd = devp->aesd_working_entry.size + count;
  PDEBUG("Write: allocating %ld bytes of kernel memory", memSzReqd);
  devp->aesd_working_entry.buffptr =
      krealloc(devp->aesd_working_entry.buffptr, memSzReqd, GFP_KERNEL);
  if (devp->aesd_working_entry.buffptr == NULL) {
    PDEBUG("Write: error allocating memory for buffer size %ld", memSzReqd);
    goto ret_done;
  }
  PDEBUG("Write: mem allocation succesful");

  // append user data to current buffer, starting from where we left off last
  // time
  to = devp->aesd_working_entry.buffptr + devp->aesd_working_entry.size;
  memset((void *)to, 0, count);
  PDEBUG("Write: after memset to is <<%s>>", to);
  PDEBUG("Write: after buffptr is <<%s>>", devp->aesd_working_entry.buffptr);

  memSzNotCopiedFromUser = copy_from_user((void *)to, buf, count);
  if (memSzNotCopiedFromUser != 0) {
    // Only partially copied. Since ret were not copied,
    // implies count -ret were copied
    retval = count - memSzNotCopiedFromUser;
  } else {
    // successfully copied all count bytes from user
    retval = count;
  }
  totalBufSize = devp->aesd_working_entry.size + retval;
  devp->aesd_working_entry.size = totalBufSize;

  PDEBUG("Write: num not copied from user is %ld, asked is %ld, retval is %ld",
         memSzNotCopiedFromUser, count, retval);
  PDEBUG("Write: cumulative user msg is <<%s>>",
         devp->aesd_working_entry.buffptr);
  if (devp->aesd_working_entry.buffptr[totalBufSize - 1] == '\n') {
    // received new line, so move working contents to the
    // circular buffer
    struct aesd_buffer_entry *retPtr;
    PDEBUG("Write: full message received,adding to circular buffer "
           "totalbufsize=%ld",
           totalBufSize);
    retPtr = aesd_circular_buffer_add_entry(&devp->aesd_dev_buffer,
                                            &devp->aesd_working_entry);
    if (retPtr != NULL) {
      PDEBUG("Write: buffer entry was overwritten. releasing memory");
      PDEBUG("Write: lost %s", retPtr->buffptr);
      kfree(retPtr->buffptr);
      retPtr->buffptr = NULL;
    }

    // reset working entry as we stored this in circular buffer now
    devp->aesd_working_entry.size = 0;
    devp->aesd_working_entry.buffptr = NULL;
  }
  // else {
  // Not adding to circular buffer as no new line, stored only in the working
  // entry
  //}
  *f_pos = *f_pos + retval;

ret_done:
  PDEBUG("Write: unlocking mutex..");
  mutex_unlock(&devp->aesd_dev_buf_lock);
  PDEBUG("Write: returning %ld", retval);
  return retval;
}

/**
 * Adjust the file offset of @param filp based on command and offset
 * @param write_cmd zero referenced command to locacte
 * @param write_cmd_offset zero referenced offset within  commang
 * @return 0 if success,
 * -ERESTARTSTYS if mutex not obtained
 * -EINVAL on error inputs
 */
static long aesd_adjust_file_offset(struct file *filp, unsigned int writecmd,
                                    unsigned int writecmd_offset) {
  uint8_t index;
  size_t pos = 0;
  struct aesd_buffer_entry *entryPtr;
  bool found = false;
  struct aesd_dev *devp;
  int retval = -EINVAL;

  if (writecmd >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
    PDEBUG("Adjust: Incorrect write command");
    goto r_done;
  }

  devp = (struct aesd_dev *)filp->private_data;

  PDEBUG("Adjust: mutex locking.");
  mutex_lock(&devp->aesd_dev_buf_lock);

  if (!devp->aesd_dev_buffer.full &&
      (devp->aesd_dev_buffer.in_offs == devp->aesd_dev_buffer.out_offs)) {
    PDEBUG("Adjust: Incorrect write command, buffer is empty");
    goto r_done;
  }

  AESD_CIRCULAR_BUFFER_FOREACH(entryPtr, &aesd_device.aesd_dev_buffer, index) {
    if (entryPtr->buffptr) {
      if (index == writecmd) {
        if (writecmd_offset > entryPtr->size) {
          PDEBUG("Adjust: Error incorrect write cmd offset %d",
                 writecmd_offset);
          goto r_done;
        }
        pos += writecmd_offset;
        found = true;
        PDEBUG("Found writecmd=%d writeoffset=%d pos = %ld", writecmd,
               writecmd_offset, pos);
        retval = 0;
        break;
      } else {
        pos += entryPtr->size;
      }
    }
  }

  if (!found) {
    PDEBUG("Adjust: Incorrect write command");
    goto r_done;
  }
  PDEBUG("Setting filepos to %ld", pos);
  filp->f_pos = pos;

r_done:
  PDEBUG("Adjust: mutex unlocking.");
  mutex_unlock(&devp->aesd_dev_buf_lock);
  return retval;
}

static loff_t aesd_llseek(struct file *filp, loff_t off, int whence) {
  struct aesd_dev *devp;
  loff_t retval;
  devp = (struct aesd_dev *)filp->private_data;

  PDEBUG("llseek: called with off=%lld whence=%d buf_len=%ld", off, whence,
         devp->aesd_dev_buffer.buf_len);
  PDEBUG("llseek: mutex locking.");
  mutex_lock(&devp->aesd_dev_buf_lock);

  retval = fixed_size_llseek(filp, off, whence, devp->aesd_dev_buffer.buf_len);

  PDEBUG("llseek: returning fpos as %lld", retval);

  PDEBUG("llseek: mutex unlocking.");
  mutex_unlock(&devp->aesd_dev_buf_lock);

  return retval;
}

static long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
  struct aesd_seekto seekto;
  long retval = -EINVAL;

  PDEBUG("ioctl: cmd is seekto? %d", cmd == AESDCHAR_IOCSEEKTO);
  switch (cmd) {
  case AESDCHAR_IOCSEEKTO:
    if (copy_from_user(&seekto, (const void *)arg, sizeof(seekto)) != 0) {
      PDEBUG("ioctl: error copying from user");
      retval = -ERESTARTSYS;
      return retval;
    }
    PDEBUG("ioctl: adjusting the offset writecmd=%d writecmdoff=%d",
           seekto.write_cmd, seekto.write_cmd_offset);
    retval = aesd_adjust_file_offset(filp, seekto.write_cmd,
                                     seekto.write_cmd_offset);
    break;
  }
  return retval;
}

struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_release,
    .llseek = aesd_llseek,
    .unlocked_ioctl = aesd_ioctl,
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

  PDEBUG("Init");
  result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
  if (result < 0) {
    printk(KERN_WARNING "Can't get major %d\n", aesd_major);
    return result;
  }
  aesd_major = MAJOR(dev);

  memset(&aesd_device, 0, sizeof(struct aesd_dev));
  aesd_circular_buffer_init(&aesd_device.aesd_dev_buffer);
  aesd_device.aesd_working_entry.size = 0;
  aesd_device.aesd_working_entry.buffptr = NULL;
  aesd_device.nextReadPosition = 0;
  mutex_init(&aesd_device.aesd_dev_buf_lock);

  result = aesd_setup_cdev(&aesd_device);
  if (result) {
    unregister_chrdev_region(dev, 1);
  }

  return result;
}

void aesd_cleanup_module(void) {
  dev_t devno = MKDEV(aesd_major, aesd_minor);
  PDEBUG("Cleanup");
  cdev_del(&aesd_device.cdev);
  mutex_destroy(&aesd_device.aesd_dev_buf_lock);
  aesd_circular_buffer_destroy(&aesd_device.aesd_dev_buffer);
  if (aesd_device.aesd_working_entry.buffptr) {
    kfree(aesd_device.aesd_working_entry.buffptr);
  }
  unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
