/*
 *  RouterBoot helper routines
 *
 *  Copyright (C) 2012 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2018 Chris Schimp <silverchris@gmail.com>
 *  Copyright (C) 2019 Robert Marko <robimarko@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#define pr_fmt(fmt) "rb: " fmt

#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/routerboot.h>
#include <linux/rle.h>
#include <linux/lzo.h>

#include "routerboot.h"

#define RB_BLOCK_SIZE		0x1000
#define RB_ART_SIZE		0x10000
#define RB_MAGIC_ERD		0x00455244	/* extended radio data */

/* Used on some newer boards (ipq40xx).  This fixed data is concatenated
   with data extracted from the hard_config partition, then decompressed
   with LZO.  There may also be a second decoding step with RLE. */
const u8 lzo_prefix[] = {
    0x00, 0x05, 0x4c, 0x4c, 0x44, 0x00, 0x34, 0xfe,
    0xfe, 0x34, 0x11, 0x3c, 0x1e, 0x3c, 0x2e, 0x3c,
    0x4c, 0x34, 0x00, 0x52, 0x62, 0x92, 0xa2, 0xb2,
    0xc3, 0x2a, 0x14, 0x00, 0x00, 0x05, 0xfe, 0x6a,
    0x3c, 0x16, 0x32, 0x16, 0x11, 0x1e, 0x12, 0x46,
    0x32, 0x46, 0x11, 0x4e, 0x12, 0x36, 0x32, 0x36,
    0x11, 0x3e, 0x12, 0x5a, 0x9a, 0x64, 0x00, 0x04,
    0xfe, 0x10, 0x3c, 0x00, 0x01, 0x00, 0x00, 0x28,
    0x0c, 0x00, 0x0f, 0xfe, 0x14, 0x00, 0x24, 0x24,
    0x23, 0x24, 0x24, 0x23, 0x25, 0x22, 0x21, 0x21,
    0x23, 0x22, 0x21, 0x22, 0x21, 0x2d, 0x38, 0x00,
    0x0c, 0x25, 0x25, 0x24, 0x25, 0x25, 0x24, 0x23,
    0x22, 0x21, 0x20, 0x23, 0x21, 0x21, 0x22, 0x21,
    0x2d, 0x38, 0x00, 0x28, 0xb0, 0x00, 0x00, 0x22,
    0x00, 0x00, 0xc0, 0xfe, 0x03, 0x00, 0xc0, 0x00,
    0x62, 0xff, 0x62, 0xff, 0xfe, 0x06, 0x00, 0xbb,
    0xff, 0xba, 0xff, 0xfe, 0x08, 0x00, 0x9e, 0xff,
    0xfe, 0x0a, 0x00, 0x53, 0xff, 0xfe, 0x02, 0x00,
    0x20, 0xff, 0xb1, 0xfe, 0xfe, 0xb2, 0xfe, 0xfe,
    0xed, 0xfe, 0xfe, 0xfe, 0x04, 0x00, 0x3a, 0xff,
    0x3a, 0xff, 0xde, 0xfd, 0x5f, 0x04, 0x33, 0xff,
    0x4c, 0x74, 0x03, 0x05, 0x05, 0xff, 0x6d, 0xfe,
    0xfe, 0x6d, 0xfe, 0xfe, 0xaf, 0x08, 0x63, 0xff,
    0x64, 0x6f, 0x08, 0xac, 0xff, 0xbf, 0x6d, 0x08,
    0x7a, 0x6d, 0x08, 0x96, 0x74, 0x04, 0x00, 0x08,
    0x79, 0xff, 0xda, 0xfe, 0xfe, 0xdb, 0xfe, 0xfe,
    0x56, 0xff, 0xfe, 0x04, 0x00, 0x5e, 0xff, 0x5e,
    0xff, 0x6c, 0xfe, 0xfe, 0xfe, 0x06, 0x00, 0x41,
    0xff, 0x7f, 0x74, 0x03, 0x00, 0x11, 0x44, 0xff,
    0xa9, 0xfe, 0xfe, 0xa9, 0xfe, 0xfe, 0xa5, 0x8f,
    0x01, 0x00, 0x08, 0x01, 0x01, 0x02, 0x04, 0x08,
    0x02, 0x04, 0x08, 0x08, 0x01, 0x01, 0xfe, 0x22,
    0x00, 0x4c, 0x60, 0x64, 0x8c, 0x90, 0xd0, 0xd4,
    0xd8, 0x5c, 0x10, 0x09, 0xd8, 0xff, 0xb0, 0xff,
    0x00, 0x00, 0xba, 0xff, 0x14, 0x00, 0xba, 0xff,
    0x64, 0x00, 0x00, 0x08, 0xfe, 0x06, 0x00, 0x74,
    0xff, 0x42, 0xff, 0xce, 0xff, 0x60, 0xff, 0x0a,
    0x00, 0xb4, 0x00, 0xa0, 0x00, 0xa0, 0xfe, 0x07,
    0x00, 0x0a, 0x00, 0xb0, 0xff, 0x96, 0x4d, 0x00,
    0x56, 0x57, 0x18, 0xa6, 0xff, 0x92, 0x70, 0x11,
    0x00, 0x12, 0x90, 0x90, 0x76, 0x5a, 0x54, 0x54,
    0x4c, 0x46, 0x38, 0x00, 0x10, 0x10, 0x08, 0xfe,
    0x05, 0x00, 0x38, 0x29, 0x25, 0x23, 0x22, 0x22,
    0x1f, 0x00, 0x00, 0x00, 0xf6, 0xe1, 0xdd, 0xf8,
    0xfe, 0x00, 0xfe, 0x15, 0x00, 0x00, 0xd0, 0x02,
    0x74, 0x02, 0x08, 0xf8, 0xe5, 0xde, 0x02, 0x04,
    0x04, 0xfd, 0x00, 0x00, 0x00, 0x07, 0x50, 0x2d,
    0x01, 0x90, 0x90, 0x76, 0x60, 0xb0, 0x07, 0x07,
    0x0c, 0x0c, 0x04, 0xfe, 0x05, 0x00, 0x66, 0x66,
    0x5a, 0x56, 0xbc, 0x01, 0x06, 0xfc, 0xfc, 0xf1,
    0xfe, 0x07, 0x00, 0x24, 0x95, 0x70, 0x64, 0x18,
    0x06, 0x2c, 0xff, 0xb5, 0xfe, 0xfe, 0xb5, 0xfe,
    0xfe, 0xe2, 0x8c, 0x24, 0x02, 0x2f, 0xff, 0x2f,
    0xff, 0xb4, 0x78, 0x02, 0x05, 0x73, 0xff, 0xed,
    0xfe, 0xfe, 0x4f, 0xff, 0x36, 0x74, 0x1e, 0x09,
    0x4f, 0xff, 0x50, 0xff, 0xfe, 0x16, 0x00, 0x70,
    0xac, 0x70, 0x8e, 0xac, 0x40, 0x0e, 0x01, 0x70,
    0x7f, 0x8e, 0xac, 0x6c, 0x00, 0x0b, 0xfe, 0x02,
    0x00, 0xfe, 0x0a, 0x2c, 0x2a, 0x2a, 0x28, 0x26,
    0x1e, 0x1e, 0xfe, 0x02, 0x20, 0x65, 0x20, 0x00,
    0x00, 0x05, 0x12, 0x00, 0x11, 0x1e, 0x11, 0x11,
    0x41, 0x1e, 0x41, 0x11, 0x31, 0x1e, 0x31, 0x11,
    0x70, 0x75, 0x7a, 0x7f, 0x84, 0x89, 0x8e, 0x93,
    0x98, 0x30, 0x20, 0x00, 0x02, 0x00, 0xfe, 0x06,
    0x3c, 0xbc, 0x32, 0x0c, 0x00, 0x00, 0x2a, 0x12,
    0x1e, 0x12, 0x2e, 0x12, 0xcc, 0x12, 0x11, 0x1a,
    0x1e, 0x1a, 0x2e, 0x1a, 0x4c, 0x10, 0x1e, 0x10,
    0x11, 0x18, 0x1e, 0x42, 0x1e, 0x42, 0x2e, 0x42,
    0xcc, 0x42, 0x11, 0x4a, 0x1e, 0x4a, 0x2e, 0x4a,
    0x4c, 0x40, 0x1e, 0x40, 0x11, 0x48, 0x1e, 0x32,
    0x1e, 0x32, 0x2e, 0x32, 0xcc, 0x32, 0x11, 0x3a,
    0x1e, 0x3a, 0x2e, 0x3a, 0x4c, 0x30, 0x1e, 0x30,
    0x11, 0x38, 0x1e, 0x27, 0x9a, 0x01, 0x9d, 0xa2,
    0x2f, 0x28, 0x00, 0x00, 0x46, 0xde, 0xc4, 0xbf,
    0xa6, 0x9d, 0x81, 0x7b, 0x5c, 0x61, 0x40, 0xc7,
    0xc0, 0xae, 0xa9, 0x8c, 0x83, 0x6a, 0x62, 0x50,
    0x3e, 0xce, 0xc2, 0xae, 0xa3, 0x8c, 0x7b, 0x6a,
    0x5a, 0x50, 0x35, 0xd7, 0xc2, 0xb7, 0xa4, 0x95,
    0x7e, 0x72, 0x5a, 0x59, 0x37, 0xfe, 0x02, 0xf8,
    0x8c, 0x95, 0x90, 0x8f, 0x00, 0xd7, 0xc0, 0xb7,
    0xa2, 0x95, 0x7b, 0x72, 0x56, 0x59, 0x32, 0xc7,
    0xc3, 0xae, 0xad, 0x8c, 0x85, 0x6a, 0x63, 0x50,
    0x3e, 0xce, 0xc3, 0xae, 0xa4, 0x8c, 0x7c, 0x6a,
    0x59, 0x50, 0x34, 0xd7, 0xc2, 0xb7, 0xa5, 0x95,
    0x7e, 0x72, 0x59, 0x59, 0x36, 0xfc, 0x05, 0x00,
    0x02, 0xce, 0xc5, 0xae, 0xa5, 0x95, 0x83, 0x72,
    0x5c, 0x59, 0x36, 0xbf, 0xc6, 0xa5, 0xab, 0x8c,
    0x8c, 0x6a, 0x67, 0x50, 0x41, 0x64, 0x07, 0x00,
    0x02, 0x95, 0x8c, 0x72, 0x65, 0x59, 0x3f, 0xce,
    0xc7, 0xae, 0xa8, 0x95, 0x86, 0x72, 0x5f, 0x59,
    0x39, 0xfe, 0x02, 0xf8, 0x8b, 0x7c, 0x0b, 0x09,
    0xb7, 0xc2, 0x9d, 0xa4, 0x83, 0x85, 0x6a, 0x6b,
    0x50, 0x44, 0xb7, 0xc1, 0x64, 0x01, 0x00, 0x06,
    0x61, 0x5d, 0x48, 0x3d, 0xae, 0xc4, 0x9d, 0xad,
    0x7b, 0x85, 0x61, 0x66, 0x48, 0x46, 0xae, 0xc3,
    0x95, 0xa3, 0x72, 0x7c, 0x59, 0x56, 0x38, 0x31,
    0x7c, 0x0b, 0x00, 0x0c, 0x96, 0x91, 0x8f, 0x00,
    0xb7, 0xc0, 0xa5, 0xab, 0x8c, 0x8a, 0x6a, 0x64,
    0x50, 0x3c, 0xb7, 0xc0, 0x9d, 0xa0, 0x83, 0x80,
    0x6a, 0x64, 0x50, 0x3d, 0xb7, 0xc5, 0x9d, 0xa5,
    0x83, 0x87, 0x6c, 0x08, 0x07, 0xae, 0xc0, 0x9d,
    0xa8, 0x83, 0x88, 0x6a, 0x6d, 0x50, 0x46, 0xfc,
    0x05, 0x00, 0x16, 0xbf, 0xc0, 0xa5, 0xa2, 0x8c,
    0x7f, 0x6a, 0x57, 0x50, 0x2f, 0xb7, 0xc7, 0xa5,
    0xb1, 0x8c, 0x8e, 0x72, 0x6d, 0x59, 0x45, 0xbf,
    0xc6, 0xa5, 0xa8, 0x8c, 0x87, 0x6a, 0x5f, 0x50,
    0x37, 0xbf, 0xc2, 0xa5, 0xa4, 0x8c, 0x83, 0x6a,
    0x5c, 0x50, 0x34, 0xbc, 0x05, 0x00, 0x0e, 0x90,
    0x00, 0xc7, 0xc2, 0xae, 0xaa, 0x95, 0x82, 0x7b,
    0x60, 0x61, 0x3f, 0xb7, 0xc6, 0xa5, 0xb1, 0x8c,
    0x8d, 0x72, 0x6b, 0x61, 0x51, 0xbf, 0xc4, 0xa5,
    0xa5, 0x8c, 0x82, 0x72, 0x61, 0x59, 0x39, 0x6c,
    0x26, 0x03, 0x95, 0x82, 0x7b, 0x61, 0x61, 0x40,
    0xfc, 0x05, 0x00, 0x00, 0x7e, 0xd7, 0xc3, 0xb7,
    0xa8, 0x9d, 0x80, 0x83, 0x5d, 0x6a, 0x3f, 0xbf,
    0xc7, 0xa5, 0xa8, 0x8c, 0x84, 0x72, 0x60, 0x61,
    0x46, 0xbf, 0xc2, 0xae, 0xb0, 0x9d, 0x92, 0x83,
    0x6f, 0x6a, 0x50, 0xd7, 0xc3, 0xb7, 0xa7, 0x9d,
    0x80, 0x83, 0x5e, 0x6a, 0x40, 0xfe, 0x02, 0xf8,
    0x8d, 0x96, 0x90, 0x90, 0xfe, 0x05, 0x00, 0x8a,
    0xc4, 0x63, 0xb8, 0x3c, 0xa6, 0x29, 0x97, 0x16,
    0x81, 0x84, 0xb7, 0x5b, 0xa9, 0x33, 0x94, 0x1e,
    0x83, 0x11, 0x70, 0xb8, 0xc2, 0x70, 0xb1, 0x4d,
    0xa3, 0x2a, 0x8d, 0x1b, 0x7b, 0xa8, 0xbc, 0x68,
    0xab, 0x47, 0x9d, 0x27, 0x87, 0x18, 0x75, 0xae,
    0xc6, 0x7d, 0xbb, 0x4d, 0xaa, 0x1c, 0x84, 0x11,
    0x72, 0xa3, 0xbb, 0x6e, 0xad, 0x3c, 0x97, 0x24,
    0x85, 0x16, 0x71, 0x80, 0xb2, 0x57, 0xa4, 0x30,
    0x8e, 0x1c, 0x7c, 0x10, 0x68, 0xbb, 0xbd, 0x75,
    0xac, 0x4f, 0x9e, 0x2b, 0x87, 0x1a, 0x76, 0x96,
    0xc5, 0x5e, 0xb5, 0x3e, 0xa5, 0x1f, 0x8c, 0x12,
    0x7a, 0xc1, 0xc6, 0x42, 0x9f, 0x27, 0x8c, 0x16,
    0x77, 0x0f, 0x67, 0x9d, 0xbc, 0x68, 0xad, 0x36,
    0x95, 0x20, 0x83, 0x11, 0x6d, 0x9b, 0xb8, 0x67,
    0xa8, 0x34, 0x90, 0x1f, 0x7c, 0x10, 0x67, 0x9e,
    0xc9, 0x6a, 0xbb, 0x37, 0xa4, 0x20, 0x90, 0x11,
    0x7b, 0xc6, 0xc8, 0x47, 0xa4, 0x2a, 0x90, 0x18,
    0x7b, 0x10, 0x6c, 0xae, 0xc4, 0x5d, 0xad, 0x37,
    0x9a, 0x1f, 0x85, 0x13, 0x75, 0x70, 0xad, 0x42,
    0x99, 0x25, 0x84, 0x17, 0x74, 0x0b, 0x56, 0x87,
    0xc8, 0x57, 0xb8, 0x2b, 0x9e, 0x19, 0x8a, 0x0d,
    0x74, 0xa7, 0xc8, 0x6e, 0xb9, 0x36, 0xa0, 0x1f,
    0x8b, 0x11, 0x75, 0x94, 0xbe, 0x4b, 0xa5, 0x2a,
    0x92, 0x18, 0x7c, 0x0f, 0x6b, 0xaf, 0xc0, 0x58,
    0xa8, 0x34, 0x94, 0x1d, 0x7d, 0x12, 0x6d, 0x82,
    0xc0, 0x52, 0xb0, 0x25, 0x94, 0x14, 0x7f, 0x0c,
    0x68, 0x84, 0xbf, 0x3e, 0xa4, 0x22, 0x8e, 0x10,
    0x76, 0x0b, 0x65, 0x88, 0xb6, 0x42, 0x9b, 0x26,
    0x87, 0x14, 0x70, 0x0c, 0x5f, 0xc5, 0xc2, 0x3e,
    0x97, 0x23, 0x83, 0x13, 0x6c, 0x0c, 0x5c, 0xb1,
    0xc9, 0x76, 0xbc, 0x4a, 0xaa, 0x20, 0x8d, 0x12,
    0x78, 0x93, 0xbf, 0x46, 0xa3, 0x26, 0x8d, 0x14,
    0x74, 0x0c, 0x62, 0xc8, 0xc4, 0x3b, 0x97, 0x21,
    0x82, 0x11, 0x6a, 0x0a, 0x59, 0xa3, 0xb9, 0x68,
    0xa9, 0x30, 0x8d, 0x1a, 0x78, 0x0f, 0x61, 0xa0,
    0xc9, 0x73, 0xbe, 0x50, 0xb1, 0x30, 0x9f, 0x14,
    0x80, 0x83, 0xb7, 0x3c, 0x9a, 0x20, 0x84, 0x0e,
    0x6a, 0x0a, 0x57, 0xac, 0xc2, 0x68, 0xb0, 0x2e,
    0x92, 0x19, 0x7c, 0x0d, 0x63, 0x93, 0xbe, 0x62,
    0xb0, 0x3c, 0x9e, 0x1a, 0x80, 0x0e, 0x6b, 0xbb,
    0x02, 0xa0, 0x02, 0xa0, 0x02, 0x6f, 0x00, 0x75,
    0x00, 0x75, 0x00, 0x00, 0x00, 0xad, 0x02, 0xb3,
    0x02, 0x6f, 0x00, 0x87, 0x00, 0x85, 0xfe, 0x03,
    0x00, 0xc2, 0x02, 0x82, 0x4d, 0x92, 0x6e, 0x4d,
    0xb1, 0xa8, 0x84, 0x01, 0x00, 0x07, 0x7e, 0x00,
    0xa8, 0x02, 0xa4, 0x02, 0xa4, 0x02, 0xa2, 0x00,
    0xa6, 0x00, 0xa6, 0x00, 0x00, 0x00, 0xb4, 0x02,
    0xb4, 0x02, 0x92, 0x00, 0x96, 0x00, 0x96, 0x46,
    0x04, 0xb0, 0x02, 0x64, 0x02, 0x0a, 0x8c, 0x00,
    0x90, 0x02, 0x98, 0x02, 0x98, 0x02, 0x0e, 0x01,
    0x11, 0x01, 0x11, 0x50, 0xc3, 0x08, 0x88, 0x02,
    0x88, 0x02, 0x19, 0x01, 0x02, 0x01, 0x02, 0x01,
    0xf3, 0x2d, 0x00, 0x00
};




static struct rb_info rb_info;

static u32 get_u32(void *buf)
{
	u8 *p = buf;

	return ((u32) p[3] + ((u32) p[2] << 8) + ((u32) p[1] << 16) +
	       ((u32) p[0] << 24));
}

__init int
routerboot_find_magic(u8 *buf, unsigned int buflen, u32 *offset, bool hard)
{
	u32 magic_ref = hard ? RB_MAGIC_HARD : RB_MAGIC_SOFT;
	u32 magic;
	u32 cur = *offset;

	while (cur < buflen) {
		magic = get_u32(buf + cur);
		if (magic == magic_ref) {
			*offset = cur;
			return 0;
		}

		cur += 0x1000;
	}

	return -ENOENT;
}

__init int
routerboot_find_tag(u8 *buf, unsigned int buflen, u16 tag_id,
		    u8 **tag_data, u16 *tag_len)
{
	u16 id;
	u16 len;
	uint32_t magic;
	bool align = false;
	int ret;

	if (buflen < 4)
		return -EINVAL;

	magic = get_u32(buf);
	switch (magic) {
	case RB_MAGIC_LZOR:
		buf += 4;
		buflen -= 4;
		break;
	case RB_MAGIC_ERD:
		align = true;
		/* fall trough */
	case RB_MAGIC_HARD:
		/* skip magic value */
		buf += 4;
		buflen -= 4;
		break;

	case RB_MAGIC_SOFT:
		if (buflen < 8)
			return -EINVAL;

		/* skip magic and CRC value */
		buf += 8;
		buflen -= 8;

		break;

	default:
		return -EINVAL;
	}

	ret = -ENOENT;
	while (buflen > 4) {
		u32 id_and_len = get_u32(buf);
		buf += 4;
		buflen -= 4;
		id = id_and_len & 0xFFFF;
		len = id_and_len >> 16;

		if (align)
			len += (4 - len % 4) % 4;

		if (id == RB_ID_TERMINATOR)
			break;

		if (buflen < len)
			break;

		if (id == tag_id) {
			*tag_len = len;
			*tag_data = buf;
			ret = 0;
			break;
		}

		buf += len;
		buflen -= len;
	}

	return ret;
}

static inline int
rb_find_hard_cfg_tag(u16 tag_id, u8 **tag_data, u16 *tag_len)
{
	if (!rb_info.hard_cfg_data ||
	    !rb_info.hard_cfg_size)
		return -ENOENT;

	return routerboot_find_tag(rb_info.hard_cfg_data,
				   rb_info.hard_cfg_size,
				   tag_id, tag_data, tag_len);
}

__init const char *
rb_get_board_name(void)
{
	u16 tag_len;
	u8 *tag;
	int err;
	
	err = rb_find_hard_cfg_tag(RB_ID_BOARD_NAME, &tag, &tag_len);
	if (err)
		return NULL;

	return tag;
}

__init u32
rb_get_hw_options(void)
{
	u16 tag_len;
	u8 *tag;
	int err;

	err = rb_find_hard_cfg_tag(RB_ID_HW_OPTIONS, &tag, &tag_len);
	if (err)
		return 0;

	return get_u32(tag);
}

static void * __init
__rb_get_wlan_data(u16 id)
{
	u16 tag_len;
	u8 *tag;
	u16 erd_tag_len;
	u8 *erd_tag;
	u8 *buf_lzo_in = NULL;
	u8 *buf_lzo_out = NULL;
	u8 *buf_rle_out = NULL;
	int err;
	u32 magic;
	u32 erd_magic;
	u32 erd_offset;
	size_t lzo_out_len;

	err = rb_find_hard_cfg_tag(RB_ID_WLAN_DATA, &tag, &tag_len);
	if (err) {
		pr_err("no calibration data found\n");
		goto err;
	}

	buf_lzo_in = kmalloc(RB_ART_SIZE, GFP_KERNEL);
	if (buf_lzo_in == NULL) {
		pr_err("no memory for calibration data\n");
		goto err;
	}

	buf_rle_out = kmalloc(RB_ART_SIZE, GFP_KERNEL);
	if (buf_rle_out == NULL) {
		pr_err("no memory for calibration data\n");
		goto err_free;
	}

	buf_lzo_out = kmalloc(RB_ART_SIZE, GFP_KERNEL);
	if (buf_lzo_out == NULL) {
		pr_err("no memory for calibration data\n");
		goto err_free;
	}
	
	magic = get_u32(tag);
	if (magic == RB_MAGIC_LZOR) {
		tag += 4;
		tag_len -= 4;
		
		if (tag_len + sizeof(lzo_prefix) > RB_ART_SIZE) {
			pr_err("Calibration data too large\n");
			goto err_free;
		}
		pr_err("Copying fixed LZO prefix (size: %d)\n", sizeof(lzo_prefix));
		memcpy(buf_lzo_in, lzo_prefix, sizeof(lzo_prefix));

		pr_err("Copying input data (size: %d)\n", tag_len);
		memcpy(buf_lzo_in + sizeof(lzo_prefix), tag, tag_len);

		pr_err("Decompressing with LZO\n");
		lzo_out_len = RB_ART_SIZE;
		err = lzo1x_decompress_safe(buf_lzo_in, tag_len + sizeof(lzo_prefix),
					    buf_lzo_out, &lzo_out_len);
		/* For some reason, I get this "input not consumed" error
		 * even though the output is correct, so ignore it. */
		if (err && err != LZO_E_INPUT_NOT_CONSUMED) {
			pr_err("unable to decompress calibration data: %d\n",
			       err);
			goto err_free;
		}

		pr_err("Looking for ERD data in decompressed output\n");
		erd_magic = 0;
		for (erd_offset = 0; erd_offset < lzo_out_len; erd_offset++) {
			erd_magic = get_u32(buf_lzo_out + erd_offset);
			if (erd_magic == RB_MAGIC_ERD)
				break;
		}
		if (erd_magic != RB_MAGIC_ERD) {
			pr_err("no ERD data found\n");
			goto err_free;
		}
		pr_err("Found ERD magic at offset %d\n", erd_offset);

		err = routerboot_find_tag(buf_lzo_out + erd_offset,
					  lzo_out_len - erd_offset,
					  0x1, &erd_tag, &erd_tag_len);
		if (err) {
			pr_err("No ERD chunk found\n");
			goto err_free;
		}

		pr_err("Decompress ERD data with RLE\n");
		err = rle_decode(erd_tag, erd_tag_len, buf_rle_out, RB_ART_SIZE,
				 NULL, NULL);
		if (err) {
			pr_err("unable to decode ERD data\n");
			goto err_free;
		}
	}
	/* Older ath79-based boards directly show the RB_MAGIC_ERD bytes followed by
	the LZO-compressed calibration data with no RLE */
	if (magic == RB_MAGIC_ERD) {
		if (tag_len > RB_ART_SIZE) {
			pr_err("Calibration data too large\n");
			goto err_free;
		}

		err = routerboot_find_tag(tag, tag_len,
					  0x1, &buf_lzo_in, &erd_tag_len);
		if (err) {
			pr_err("No ERD chunk found\n");
			goto err_free;
		}

		pr_err("Decompressing with LZO\n");
		lzo_out_len = RB_ART_SIZE;
		err = lzo1x_decompress_safe(buf_lzo_in, tag_len,
					    buf_lzo_out, &lzo_out_len);
		/* For some reason, I get this "input not consumed" error
		 * even though the output is correct, so ignore it. */
		if (err && err != LZO_E_INPUT_NOT_CONSUMED) {
			pr_err("unable to decompress calibration data: %d\n",
			       err);
			goto err_free;
		}
		
		buf_rle_out = buf_lzo_out;
	} else {
		if (id != 0)
			goto err_free;
    
		err = rle_decode(tag, tag_len, buf_rle_out, RB_ART_SIZE,
					     NULL, NULL);
		if (err) {
    			pr_err("unable to decode calibration data\n");
			goto err_free;
		}

	}
	return buf_rle_out;

err_free:
	kfree(buf_rle_out);
	kfree(buf_lzo_out);
	kfree(buf_lzo_in);
err:
	return NULL;
}

__init void *
rb_get_wlan_data(void)
{
	return __rb_get_wlan_data(0);
}

__init void *
rb_get_ext_wlan_data(u16 id)
{
	return __rb_get_wlan_data(id);
}

__init const struct rb_info *
rb_init_info(void *data, unsigned int size)
{
	unsigned int offset;

	if (size == 0 || (size % RB_BLOCK_SIZE) != 0)
		return NULL;

	for (offset = 0; offset < size; offset += RB_BLOCK_SIZE) {
		u32 magic;

		magic = get_u32(data + offset);
		switch (magic) {
		case RB_MAGIC_HARD:
			rb_info.hard_cfg_offs = offset;
			break;

		case RB_MAGIC_SOFT:
			rb_info.soft_cfg_offs = offset;
			break;
		}
	}

	if (!rb_info.hard_cfg_offs) {
		pr_err("could not find a valid RouterBOOT hard config\n");
		return NULL;
	}

	if (!rb_info.soft_cfg_offs) {
		pr_err("could not find a valid RouterBOOT soft config\n");
		return NULL;
	}

	rb_info.hard_cfg_size = RB_BLOCK_SIZE;
	rb_info.hard_cfg_data = kmemdup(data + rb_info.hard_cfg_offs,
					RB_BLOCK_SIZE, GFP_KERNEL);
	if (!rb_info.hard_cfg_data)
		return NULL;

	rb_info.board_name = rb_get_board_name();
	rb_info.hw_options = rb_get_hw_options();

	return &rb_info;
}

static char *rb_ext_wlan_data;

static ssize_t
rb_ext_wlan_data_read(struct file *filp, struct kobject *kobj,
		      struct bin_attribute *attr, char *buf,
		      loff_t off, size_t count)
{
         if (off + count > attr->size)
                 return -EFBIG;

         memcpy(buf, &rb_ext_wlan_data[off], count);

         return count;
}

static const struct bin_attribute rb_ext_wlan_data_attr = {
	.attr = {
		.name = "ext_wlan_data",
		.mode = S_IRUSR | S_IWUSR,
	},
	.read = rb_ext_wlan_data_read,
	.size = RB_ART_SIZE,
};

static int __init rb_sysfs_init(void)
{
	struct kobject *rb_kobj;
	int ret;

	rb_ext_wlan_data = rb_get_ext_wlan_data(1);
	if (rb_ext_wlan_data == NULL)
		return -ENOENT;

	rb_kobj = kobject_create_and_add("routerboot", firmware_kobj);
	if (rb_kobj == NULL) {
		ret = -ENOMEM;
		pr_err("unable to create sysfs entry\n");
		goto err_free_wlan_data;
	}

	ret = sysfs_create_bin_file(rb_kobj, &rb_ext_wlan_data_attr);
	if (ret) {
		pr_err("unable to create sysfs file, %d\n", ret);
		goto err_put_kobj;
	}

	return 0;

err_put_kobj:
	kobject_put(rb_kobj);
err_free_wlan_data:
	kfree(rb_ext_wlan_data);
	return ret;
}

late_initcall(rb_sysfs_init);
