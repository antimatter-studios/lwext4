/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

/** @addtogroup lwext4
 * @{
 */
/**
 * @file  ext4_inline.c
 * @brief Inline data (inline_data feature), from the ext4 disk layout:
 *        the data of a small file or directory is in i_block (60 bytes)
 *        and continues in the value of its "system.data" extended
 *        attribute in the i-node body. An inline directory starts with the
 *        i-node number of its parent (4 bytes) instead of "." and "..";
 *        directory entries follow, in i_block and then in "system.data".
 *        All of it is in the i-node, so nothing here reads blocks.
 */

#include <ext4_config.h>
#include <ext4_types.h>
#include <ext4_misc.h>
#include <ext4_errno.h>
#include <ext4_debug.h>

#include <ext4_super.h>
#include <ext4_inode.h>
#include <ext4_dir.h>
#include <ext4_inline.h>

#include <string.h>

#define EXT4_INLINE_XATTR_MAGIC 0xEA020000
#define EXT4_INLINE_XATTR_SYSTEM 7	/* name index of "system." */
#define EXT4_INLINE_IBLOCK 60		/* bytes of i_block */
#define EXT4_INLINE_DIR_PARENT 4	/* parent i-node number */
#define EXT4_INLINE_DIR_IBLOCK (EXT4_INLINE_IBLOCK - EXT4_INLINE_DIR_PARENT)

static uint32_t ext4_inline_le32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

bool ext4_inline_has_data(struct ext4_sblock *sb, struct ext4_inode *inode)
{
	return ext4_sb_feature_incom(sb, EXT4_FINCOM_INLINE_DATA) &&
	       ext4_inode_has_flag(inode, EXT4_INODE_FLAG_INLINE_DATA);
}

/* The value of "system.data" in the i-node body (all of the i-node's
 * inode_size bytes are loaded); NULL and length 0 if there is none. */
static const uint8_t *ext4_inline_xattr(struct ext4_sblock *sb,
					struct ext4_inode *inode,
					uint32_t *len)
{
	const uint8_t *raw = (const uint8_t *)inode;
	uint32_t isize = ext4_get16(sb, inode_size);
	uint32_t extra, start;
	const uint8_t *first, *end, *e;

	*len = 0;
	if (isize <= EXT4_GOOD_OLD_INODE_SIZE)
		return NULL;
	extra = to_le16(inode->extra_isize);
	start = EXT4_GOOD_OLD_INODE_SIZE + extra;
	if ((extra & 3) || start + 4 > isize ||
	    ext4_inline_le32(raw + start) != EXT4_INLINE_XATTR_MAGIC)
		return NULL;

	/* Entries until four zero bytes; value offsets count from the first
	 * entry */
	first = raw + start + 4;
	end = raw + isize;
	for (e = first; end - e >= 4 && ext4_inline_le32(e) != 0;) {
		size_t esz = (16u + e[0] + 3) & ~(size_t)3;

		if ((size_t)(end - e) < esz)
			return NULL;
		if (e[1] == EXT4_INLINE_XATTR_SYSTEM && e[0] == 4 &&
		    !memcmp(e + 16, "data", 4) && !ext4_inline_le32(e + 4)) {
			uint32_t off = e[2] | e[3] << 8;
			uint32_t size = ext4_inline_le32(e + 8);
			size_t room = (size_t)(end - first);

			if (off > room || size > room - off)
				return NULL;
			*len = size;
			return first + off;
		}
		e += esz;
	}
	return NULL;
}

int ext4_inline_read(struct ext4_sblock *sb, struct ext4_inode *inode,
		     uint64_t pos, void *buf, size_t size, size_t *rcnt)
{
	const uint8_t *ib = (const uint8_t *)inode->blocks;
	uint64_t fsize = ext4_inode_get_size(sb, inode);
	uint8_t *out = buf;
	uint32_t xlen;
	const uint8_t *x = ext4_inline_xattr(sb, inode, &xlen);
	size_t n = 0;

	*rcnt = 0;
	if (fsize > EXT4_INLINE_IBLOCK + (uint64_t)xlen)
		return EIO;
	if (pos >= fsize)
		return EOK;
	if ((uint64_t)size > fsize - pos)
		size = (size_t)(fsize - pos);

	while (n < size) {
		uint64_t p = pos + n;
		size_t len = size - n;

		if (p < EXT4_INLINE_IBLOCK) {
			if (len > EXT4_INLINE_IBLOCK - p)
				len = (size_t)(EXT4_INLINE_IBLOCK - p);
			memcpy(out + n, ib + p, len);
		} else {
			memcpy(out + n, x + (p - EXT4_INLINE_IBLOCK), len);
		}
		n += len;
	}
	*rcnt = n;
	return EOK;
}

/* Where the entries continue at byte offset off: the rest of i_block after
 * the parent, then "system.data". false at the end. */
static bool ext4_inline_dir_at(struct ext4_sblock *sb, struct ext4_inode *inode,
			       uint32_t off, const uint8_t **p, uint32_t *left)
{
	uint32_t xlen;
	const uint8_t *x;

	if (off < EXT4_INLINE_DIR_IBLOCK) {
		*p = (const uint8_t *)inode->blocks + EXT4_INLINE_DIR_PARENT +
		     off;
		*left = EXT4_INLINE_DIR_IBLOCK - off;
		return true;
	}
	x = ext4_inline_xattr(sb, inode, &xlen);
	off -= EXT4_INLINE_DIR_IBLOCK;
	if (!x || off >= xlen)
		return false;
	*p = x + off;
	*left = xlen - off;
	return true;
}

/* The entry at byte offset *off (deleted ones included), and the offset of
 * the next. ENOENT at the end, EIO for an entry that does not fit. */
static int ext4_inline_dir_entry(struct ext4_sblock *sb,
				 struct ext4_inode *inode, uint32_t *off,
				 struct ext4_dir_en **en)
{
	const uint8_t *p;
	uint32_t left, rec, name_len;

	if (!ext4_inline_dir_at(sb, inode, *off, &p, &left))
		return ENOENT;
	if (left < sizeof(struct ext4_fake_dir_entry))
		return EIO;

	*en = (struct ext4_dir_en *)p;
	rec = ext4_dir_en_get_entry_len(*en);
	name_len = ext4_dir_en_get_name_len(sb, *en);
	if (rec < sizeof(struct ext4_fake_dir_entry) || (rec & 3) ||
	    rec > left || name_len + sizeof(struct ext4_fake_dir_entry) > rec)
		return EIO;
	*off += rec;
	return EOK;
}

int ext4_inline_dir_next(struct ext4_sblock *sb, uint32_t self,
			 struct ext4_inode *inode, uint64_t *pos,
			 struct ext4_inline_dirent *de)
{
	struct ext4_dir_en *en;
	uint32_t off;
	int r;

	memset(de, 0, sizeof(*de));
	de->type = EXT4_DE_DIR;
	if (*pos == 0) {
		de->inode = self;
		de->name = ".";
		de->name_len = 1;
		*pos = 1;
		return EOK;
	}
	if (*pos == 1) {
		de->inode = ext4_inline_le32((const uint8_t *)inode->blocks);
		de->name = "..";
		de->name_len = 2;
		*pos = 2;
		return EOK;
	}

	if (*pos - 2 > UINT32_MAX)
		return ENOENT;
	off = (uint32_t)(*pos - 2);
	while ((r = ext4_inline_dir_entry(sb, inode, &off, &en)) == EOK) {
		if (!ext4_dir_en_get_inode(en))
			continue;
		de->inode = ext4_dir_en_get_inode(en);
		de->type = ext4_dir_en_get_inode_type(sb, en);
		de->name_len = (uint8_t)ext4_dir_en_get_name_len(sb, en);
		de->name = (const char *)en->name;
		de->en = en;
		*pos = 2 + (uint64_t)off;
		return EOK;
	}
	*pos = 2 + (uint64_t)off;
	return r;
}

int ext4_inline_dir_find(struct ext4_sblock *sb, struct ext4_inode *inode,
			 const char *name, size_t name_len,
			 struct ext4_dir_en **en)
{
	uint32_t off = 0;
	int r;

	while ((r = ext4_inline_dir_entry(sb, inode, &off, en)) == EOK) {
		if (ext4_dir_en_get_inode(*en) &&
		    ext4_dir_en_get_name_len(sb, *en) == name_len &&
		    !memcmp((*en)->name, name, name_len))
			return EOK;
	}
	*en = NULL;
	return r;
}

/**
 * @}
 */
