/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

/** @addtogroup lwext4
 * @{
 */
/**
 * @file  ext4_inline.h
 * @brief Inline data (inline_data feature): small files and directories
 *        stored in the i-node instead of data blocks.
 */

#ifndef EXT4_INLINE_H_
#define EXT4_INLINE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <ext4_config.h>
#include <ext4_types.h>

#include <stdbool.h>
#include <stdint.h>

/**@brief Whether an i-node keeps its data inline: in i_block (60 bytes)
 *        followed by the value of its "system.data" extended attribute in
 *        the i-node body.
 * @param sb    superblock
 * @param inode i-node
 * @return true for inline data*/
bool ext4_inline_has_data(struct ext4_sblock *sb, struct ext4_inode *inode);

/**@brief Read the data of an inline file (or symlink).
 * @param sb    superblock
 * @param inode i-node with inline data
 * @param pos   position in the file
 * @param buf   output buffer
 * @param size  bytes to read at most
 * @param rcnt  bytes read (up to the end of the file)
 * @return EOK, EIO if the inline data is damaged*/
int ext4_inline_read(struct ext4_sblock *sb, struct ext4_inode *inode,
		     uint64_t pos, void *buf, size_t size, size_t *rcnt);

/**@brief One entry of an inline directory.*/
struct ext4_inline_dirent {
	uint32_t inode;
	uint8_t type;      /**< EXT4_DE_* (EXT4_DE_DIR for "." and "..") */
	uint8_t name_len;
	const char *name;  /**< in the i-node, valid while it is loaded */
	const struct ext4_dir_en *en; /**< NULL for "." and ".." */
};

/**@brief Entry of an inline directory at a position, and the position
 *        of the next one. Positions: 0 is ".", 1 is "..", then 2 plus the
 *        byte offset in the entries (the 56 bytes of i_block after the
 *        parent's i-node number, followed by "system.data"). Deleted
 *        entries (i-node 0) are skipped.
 * @param sb    superblock
 * @param self  i-node number of the directory
 * @param inode the directory's i-node
 * @param pos   position; the next position on return
 * @param de    the entry
 * @return EOK, ENOENT at the end, EIO if the directory is damaged*/
int ext4_inline_dir_next(struct ext4_sblock *sb, uint32_t self,
			 struct ext4_inode *inode, uint64_t *pos,
			 struct ext4_inline_dirent *de);

/**@brief Find a name in an inline directory ("." and ".." are not
 *        directory entries there and are not found).
 * @param sb       superblock
 * @param inode    the directory's i-node
 * @param name     name
 * @param name_len its length
 * @param en       the entry, in the i-node (valid while it is loaded)
 * @return EOK, ENOENT, EIO if the directory is damaged*/
int ext4_inline_dir_find(struct ext4_sblock *sb, struct ext4_inode *inode,
			 const char *name, size_t name_len,
			 struct ext4_dir_en **en);

#ifdef __cplusplus
}
#endif

#endif /* EXT4_INLINE_H_ */

/**
 * @}
 */
