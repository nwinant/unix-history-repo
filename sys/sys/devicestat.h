/*
 * Copyright (c) 1997, 1998, 1999 Kenneth D. Merry.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _DEVICESTAT_H
#define _DEVICESTAT_H

#include <sys/queue.h>
#include <sys/time.h>

#define DEVSTAT_NAME_LEN  16

/*
 * ATTENTION:  The devstat version below should be incremented any time a
 * change is made in struct devstat, or any time a change is made in the
 * enumerated types that struct devstat uses.  (Only if those changes
 * would require a recompile -- i.e. re-arranging the order of an
 * enumerated type or something like that.)  This version number is used by
 * userland utilities to determine whether or not they are in sync with the
 * kernel.
 */
#define DEVSTAT_VERSION	   4

/*
 * These flags specify which statistics features are supported or not
 * supported by a particular device.  The default is all statistics are
 * supported.
 */
typedef enum {
	DEVSTAT_ALL_SUPPORTED	= 0x00,
	DEVSTAT_NO_BLOCKSIZE	= 0x01,
	DEVSTAT_NO_ORDERED_TAGS	= 0x02,
	DEVSTAT_BS_UNAVAILABLE	= 0x04
} devstat_support_flags;

typedef enum {
	DEVSTAT_NO_DATA	= 0x00,
	DEVSTAT_READ	= 0x01,
	DEVSTAT_WRITE	= 0x02,
	DEVSTAT_FREE	= 0x03
} devstat_trans_flags;

typedef enum {
	DEVSTAT_TAG_SIMPLE	= 0x00,
	DEVSTAT_TAG_HEAD	= 0x01,
	DEVSTAT_TAG_ORDERED	= 0x02,
	DEVSTAT_TAG_NONE	= 0x03
} devstat_tag_type;

typedef enum {
	DEVSTAT_PRIORITY_MIN	= 0x000,
	DEVSTAT_PRIORITY_OTHER	= 0x020,
	DEVSTAT_PRIORITY_PASS	= 0x030,
	DEVSTAT_PRIORITY_FD	= 0x040,
	DEVSTAT_PRIORITY_WFD	= 0x050,
	DEVSTAT_PRIORITY_TAPE	= 0x060,
	DEVSTAT_PRIORITY_CD	= 0x090,
	DEVSTAT_PRIORITY_DISK	= 0x110,
	DEVSTAT_PRIORITY_ARRAY	= 0x120,
	DEVSTAT_PRIORITY_MAX	= 0xfff
} devstat_priority;

/*
 * These types are intended to aid statistics gathering/display programs.
 * The first 13 types (up to the 'target' flag) are identical numerically
 * to the SCSI device type numbers.  The next 3 types designate the device
 * interface.  Currently the choices are IDE, SCSI, and 'other'.  The last
 * flag specifies whether or not the given device is a passthrough device
 * or not.  If it is a passthrough device, the lower 4 bits specify which
 * type of physical device lies under the passthrough device, and the next
 * 4 bits specify the interface.
 */
typedef enum {
	DEVSTAT_TYPE_DIRECT	= 0x000,
	DEVSTAT_TYPE_SEQUENTIAL	= 0x001,
	DEVSTAT_TYPE_PRINTER	= 0x002,
	DEVSTAT_TYPE_PROCESSOR	= 0x003,
	DEVSTAT_TYPE_WORM	= 0x004,
	DEVSTAT_TYPE_CDROM	= 0x005,
	DEVSTAT_TYPE_SCANNER	= 0x006,
	DEVSTAT_TYPE_OPTICAL	= 0x007,
	DEVSTAT_TYPE_CHANGER	= 0x008,
	DEVSTAT_TYPE_COMM	= 0x009,
	DEVSTAT_TYPE_ASC0	= 0x00a,
	DEVSTAT_TYPE_ASC1	= 0x00b,
	DEVSTAT_TYPE_STORARRAY	= 0x00c,
	DEVSTAT_TYPE_ENCLOSURE	= 0x00d,
	DEVSTAT_TYPE_FLOPPY	= 0x00e,
	DEVSTAT_TYPE_MASK	= 0x00f,
	DEVSTAT_TYPE_IF_SCSI	= 0x010,
	DEVSTAT_TYPE_IF_IDE	= 0x020,
	DEVSTAT_TYPE_IF_OTHER	= 0x030,
	DEVSTAT_TYPE_IF_MASK	= 0x0f0,
	DEVSTAT_TYPE_PASS	= 0x100
} devstat_type_flags;

struct devstat {
	STAILQ_ENTRY(devstat) 	dev_links;
	u_int32_t		device_number;	     /*
						      * Devstat device
						      * number.
						      */
	char			device_name[DEVSTAT_NAME_LEN];
	int			unit_number;
	u_int64_t		bytes_read;	     /*
						      * Total bytes read
						      * from a device.
						      */
	u_int64_t		bytes_written;	     /*
						      * Total bytes written
						      * to a device.
						      */
	u_int64_t		bytes_freed;	     /*
						      * Total bytes freed
						      * from a device.
						      */
	u_int64_t		num_reads;	     /*
						      * Total number of
						      * read requests to 
						      * the device.
						      */
	u_int64_t		num_writes;	     /*
						      * Total number of
						      * write requests to 
						      * the device.
						      */
	u_int64_t		num_frees;	     /*
						      * Total number of
						      * free requests to 
						      * the device.
						      */
	u_int64_t		num_other;	     /*
						      * Total number of
						      * transactions that 
						      * don't read or write 
						      * data.
						      */
	int32_t			busy_count;	     /*
						      * Total number of 
						      * transactions
						      * outstanding for 
						      * the device. 
						      */
	u_int32_t		block_size;	     /* Block size, bytes */
	u_int64_t		tag_types[3];	     /*
						      * The number of
						      * simple, ordered, 
						      * and head of queue 
						      * tags sent.
						      */
	struct timeval		dev_creation_time;   /* 
						      * Time the device was
						      * created.
						      */
	struct timeval 		busy_time;	     /*
						      * Total amount of
						      * time drive has spent 
						      * processing requests.
						      */
	struct timeval		start_time;	     /*
						      * The time when
						      * busy_count was 
						      * last == 0.  Or, the
						      * start of the latest 
						      * busy period.
						      */
	struct timeval		last_comp_time;	     /*
						      * Last time a
						      * transaction was 
						      * completed.
						      */

	devstat_support_flags	flags;		     /*
						      * Which statistics
						      * are supported by a 
						      * given device.
						      */
	devstat_type_flags	device_type;	     /* Device type */
	devstat_priority	priority;	     /* Controls list pos. */
};

#ifdef _KERNEL
struct buf;
void devstat_add_entry(struct devstat *ds, const char *dev_name, 
		       int unit_number, u_int32_t block_size,
		       devstat_support_flags flags,
		       devstat_type_flags device_type,
		       devstat_priority priority);
void devstat_remove_entry(struct devstat *ds);
void devstat_start_transaction(struct devstat *ds);
void devstat_end_transaction(struct devstat *ds, u_int32_t bytes, 
			     devstat_tag_type tag_type,
			     devstat_trans_flags flags);
void devstat_end_transaction_buf(struct devstat *ds, struct buf *);
void devstat_end_transaction_bio(struct devstat *ds, struct bio *);
#endif

#endif /* _DEVICESTAT_H */
