#include "lfs.h"

/// Block allocator ///
int lfs_alloc_lookahead(void* p, lfs_block_t block) {

    lfs_t* lfs = (lfs_t*)p;
    lfs_block_t offset = ((block - lfs->free.offset) + lfs->block_count) % lfs->block_count;

    if (offset < lfs->free.size) {

        lfs->free.buffer[offset / 64] |= (uint64_t)1U << (offset % 64);
    }

    return LFS_ERR_OK;
}

// indicate allocated blocks have been committed into the filesystem, this
// is to prevent blocks from being garbage collected in the middle of a
// commit operation
void lfs_alloc_ack(lfs_t* lfs) {
    lfs->free.ack = lfs->block_count;
}

// drop the lookahead buffer, this is done during mounting and failed
// traversals in order to avoid invalid lookahead state
void lfs_alloc_drop(lfs_t* lfs) {

    lfs->free.size = 0;
    lfs->free.i = 0;
    lfs_alloc_ack(lfs);
}

int lfs_alloc(lfs_t* lfs, lfs_block_t* block) {

    while (true) {

        while (lfs->free.i != lfs->free.size) {

            lfs_block_t offset = lfs->free.i;
            lfs->free.i += 1;
            lfs->free.ack -= 1;

            if (!(lfs->free.buffer[offset / 64] & ((uint64_t)1U << (offset % 64)))) {
                // found a free block
                *block = (lfs->free.offset + offset) % lfs->block_count;

                // eagerly find next offset so an alloc ack can
                // discredit old lookahead blocks
                while (lfs->free.i != lfs->free.size &&
                    (lfs->free.buffer[lfs->free.i / 64] & ((uint64_t)1U << (lfs->free.i % 64)))) {

                    lfs->free.i += 1;
                    lfs->free.ack -= 1;
                }

                return LFS_ERR_OK;
            }
        }

        // check if we have looked at all blocks since last ack
        if (lfs->free.ack == 0) {

            if (!lfs->cfg->allocate_block ||
                lfs->cfg->allocate_block((lfs_config_t*)lfs->cfg) == LFS_ERR_NOSPC) {

                LFS_ERROR("No more free space %"PRIu32, lfs->free.i + lfs->free.offset);
                return LFS_ERR_NOSPC;
            }

            lfs_alloc_ack(lfs);
            return lfs_alloc(lfs, block);
        }

        lfs->free.offset = (lfs->free.offset + lfs->free.size) % lfs->block_count;
        lfs->free.size = lfs_min(16 * lfs->cfg->lookahead_size, lfs->free.ack);
        lfs->free.i = 0;

        // find mask of free blocks from tree
        memset(lfs->free.buffer, 0, lfs->cfg->lookahead_size);
        
        int err = lfs_fs_rawtraverse(lfs, lfs_alloc_lookahead, lfs, true);

        if (err) {
        
            lfs_alloc_drop(lfs);
            return err;
        }
    }
}