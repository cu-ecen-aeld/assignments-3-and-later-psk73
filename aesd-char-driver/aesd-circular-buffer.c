/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

//#define DEBUGPRINT

#ifdef __KERNEL__

#include <linux/string.h>
#ifdef DEBUGPRINT
#define PRINT printk
#else
#define PRINT(msg, ...)
#endif

#else

#include <stdio.h>
#include <string.h>
#ifdef DEBUGPRINT
#define PRINT printf
#else
#define PRINT(msg, ...)
#endif

#endif

#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary
 * locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing
 * the zero referenced character index if all buffer strings were concatenated
 * end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the
 * byte of the returned aesd_buffer_entry buffptr member corresponding to
 * char_offset.  This value is only set when a matching char_offset is found in
 * aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position
 * described by char_offset, or NULL if this position is not available in the
 * buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(
    struct aesd_circular_buffer *buffer, size_t char_offset,
    size_t *entry_offset_byte_rtn) {
  /**
   * DONE: implement per description
   */
  uint8_t index;
  uint8_t endindex;
  size_t curRunningOffset = 0;
  size_t prevRunningOffset = 0;
  struct aesd_buffer_entry *entryptr;

  index = buffer->out_offs;

  if (buffer->full) {
    // buffer full case
    endindex = buffer->in_offs;
  } else if (buffer->in_offs == buffer->out_offs) {
    // buffer empty case
    return NULL;
  } else {
    // endindex = (buffer->out_offs+1)%AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    endindex = buffer->in_offs;
  }

  PRINT("Search char_offset %d\n", (int)char_offset);
  do {
    PRINT("\t In Index: %d Out Index: %d Cur Index: %d End Index: %d\n",
          buffer->in_offs, buffer->out_offs, index, endindex);
    entryptr = &((buffer)->entry[index]);
    prevRunningOffset = curRunningOffset;
    curRunningOffset += entryptr->size;
    PRINT("\t Char offset: %d Prev: %d  Cur %d\n", (int)char_offset,
          (int)prevRunningOffset, (int)curRunningOffset);

    if (char_offset + 1 <= curRunningOffset) {
      // we found the entry
      // calculate off set to return;
      *entry_offset_byte_rtn = char_offset - prevRunningOffset;
      PRINT("\t found returning offset %d\n", (int)(*entry_offset_byte_rtn));
      PRINT("\t returning buf %s \n\treturning size %ld",entryptr->buffptr,entryptr->size);
      return entryptr;
    }
    index = (index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
  } while (index != endindex);

  PRINT("\t Not found\n");
  return NULL;
}

/**
 * Adds entry @param add_entry to @param buffer in the location specified in
 * buffer->in_offs. If the buffer was already full, overwrites the oldest entry
 * and advances buffer->out_offs to the new start location. Any necessary
 * locking must be handled by the caller Any memory referenced in @param
 * add_entry must be allocated by and/or must have a lifetime managed by the
 * caller. Returns pointer to any entry that may have been overwritten, or NULL
 * if no over write was done
 */
struct aesd_buffer_entry *
aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer,
                               const struct aesd_buffer_entry *add_entry) {
  struct aesd_buffer_entry *retPtr = NULL;
  PRINT("Before: in = %d out = %d full=%d\n", buffer->in_offs, buffer->out_offs,
        buffer->full);
  PRINT("   current in buf[%d]=%s size=%ld\n", buffer->in_offs,
        buffer->entry[buffer->in_offs].buffptr,
        buffer->entry[buffer->in_offs].size);
  PRINT("   adding buf=%s size=%ld", add_entry->buffptr, add_entry->size);

  // Save copy of current in , incase there is an overflow
  buffer->overflow_entry.buffptr = buffer->entry[buffer->in_offs].buffptr;
  buffer->overflow_entry.size = buffer->entry[buffer->in_offs].size;

  // copy new entry to circular buffer and
  // update offsets
  buffer->entry[buffer->in_offs].buffptr = add_entry->buffptr;
  buffer->entry[buffer->in_offs].size = add_entry->size;

  PRINT("new buf[%d]=%s size=%ld\n", buffer->in_offs,
        buffer->entry[buffer->in_offs].buffptr,
        buffer->entry[buffer->in_offs].size);

  buffer->in_offs =
      (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

  if (buffer->full == true) {
    retPtr = &buffer->overflow_entry;
    PRINT("Buffer overflow, lost previous entry %s at %d\n", retPtr->buffptr,
          buffer->out_offs);
    buffer->out_offs = buffer->in_offs;
  } else {
    retPtr = NULL;
    if (buffer->in_offs == buffer->out_offs) {
      PRINT("Buffer full\n ");
      buffer->full = true;
    } else {
      buffer->full = false;
    }
  }
  PRINT("After: in = %d out = %d full=%d\n\n", buffer->in_offs,
        buffer->out_offs, buffer->full);
  return retPtr;
}

/**
 * Initializes the circular buffer described by @param buffer to an empty struct
 */
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer) {
  memset(buffer, 0, sizeof(struct aesd_circular_buffer));
}

#ifdef __KERNEL__
/**
 * Initializes the circular buffer described by @param buffer to an empty struct
 */
void aesd_circular_buffer_destroy(struct aesd_circular_buffer *buffer) {
  uint8_t index;
  struct aesd_buffer_entry *entry;
  AESD_CIRCULAR_BUFFER_FOREACH(entry, buffer, index) {
    if (entry->buffptr)
      kfree(entry->buffptr);
  }
}
#endif
