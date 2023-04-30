#include "lfs.h"

/// Mapping from logical to physical erase size ///

static int lfs_bd_rawread(lfs_t* lfs, lfs_block_t block, lfs_off_t off, void* buffer, lfs_size_t size) {
    LFS_ASSERT(block < lfs->block_count);
    LFS_ASSERT(off + size <= lfs->block_size);
    LFS_ASSERT(size % lfs->cfg->read_size == 0);

    // adjust to physical erase size
    block = (block * (lfs->block_size / lfs->erase_size)) + (off / lfs->erase_size);
    off = off % lfs->erase_size;

    uint8_t* buffer_ = (uint8_t*)buffer;

    // read in erase_size chunks
    while (size > 0) {

        lfs_size_t delta = lfs_min(size, off + lfs->erase_size);
        
        LFS_ASSERT(off + size <= lfs->erase_size);
        LFS_ASSERT(size % lfs->cfg->read_size == 0);
        
        int err = lfs->cfg->read(lfs->cfg, block, off, buffer_, delta);
        
        LFS_ASSERT(err <= 0);
        
        if (err) {
            return err;
        }

        off += delta;
        
        if (off == lfs->erase_size) {
        
            block += 1;
            off = 0;
        }
        
        size -= delta;
    }

    return LFS_ERR_OK;
}

static int lfs_bd_rawprog(lfs_t* lfs, lfs_block_t block, lfs_off_t off, void* buffer, lfs_size_t size) {

    LFS_ASSERT(block < lfs->block_count);
    LFS_ASSERT(off + size <= lfs->block_size);
    LFS_ASSERT(size % lfs->cfg->write_size == 0);

    // adjust to physical erase size
    block = (block * (lfs->block_size / lfs->erase_size)) + (off / lfs->erase_size);
    off = off % lfs->erase_size;

    uint8_t* buffer_ = (uint8_t*)buffer;

    // prog in erase_size chunks
    while (size > 0) {

        lfs_size_t delta = lfs_min(size, off + lfs->erase_size);
        
        LFS_ASSERT(off + size <= lfs->erase_size);
        LFS_ASSERT(size % lfs->cfg->write_size == 0);
        
        int err = lfs->cfg->write(lfs->cfg, block, off, buffer_, delta);
        
        LFS_ASSERT(err <= 0);
        
        if (err) {
        
            return err;
        }

        off += delta;
        if (off == lfs->erase_size) {
            block += 1;
            off = 0;
        }
        size -= delta;
    }

    return LFS_ERR_OK;
}

int lfs_bd_read(lfs_t* lfs,
    const lfs_cache_t* write_cache, lfs_cache_t* read_cache, lfs_size_t hint,
    lfs_block_t block, lfs_off_t offset,
    void* buffer, lfs_size_t size) {

    uint8_t* data = (uint8_t *)buffer;

    if (block >= lfs->block_count || offset + size > lfs->block_size) {

        return LFS_ERR_CORRUPT;
    }

    while (size > 0) {

        lfs_size_t diff = size;

        if (write_cache && block == write_cache->block && offset < write_cache->offset + write_cache->size) {

            if (offset >= write_cache->offset) {

                // is already in write_cache?
                diff = lfs_min(diff, write_cache->size - (offset - write_cache->offset));
                memcpy(data, &write_cache->buffer[offset - write_cache->offset], diff);

                data += diff;
                offset += diff;
                size -= diff;
                continue;
            }

            // write_cache takes priority
            diff = lfs_min(diff, write_cache->offset - offset);
        }

        if (block == read_cache->block && offset < read_cache->offset + read_cache->size) {

            if (offset >= read_cache->offset) {

                // is already in read_cache?
                diff = lfs_min(diff, read_cache->size - (offset - read_cache->offset));
                memcpy(data, &read_cache->buffer[offset - read_cache->offset], diff);

                data += diff;
                offset += diff;
                size -= diff;
                continue;
            }

            // read_cache takes priority
            diff = lfs_min(diff, read_cache->offset - offset);
        }

        if (size >= hint && offset % lfs->cfg->read_size == 0 && size >= lfs->cfg->read_size) {

            // bypass cache?
            diff = lfs_aligndown(diff, lfs->cfg->read_size);

            int err = lfs_bd_rawread(lfs, block, offset, data, diff);

            if (err) {
                return err;
            }

            data += diff;
            offset += diff;
            size -= diff;
            continue;
        }

        // load to cache, first condition can no longer fail
        read_cache->block = block;
        read_cache->offset = lfs_aligndown(offset, lfs->cfg->read_size);
        read_cache->size = lfs_min(
                            lfs_min(lfs_alignup(offset + hint, lfs->cfg->read_size), lfs->block_size) - read_cache->offset, 
                            lfs->cfg->cache_size
                        );

        int err = lfs_bd_rawread(lfs, read_cache->block, read_cache->offset, read_cache->buffer, read_cache->size);

        LFS_ASSERT(err <= 0);

        if (err) {

            return err;
        }
    }

    return LFS_ERR_OK;
}

//Compare block name, for sorting
int lfs_bd_cmp(lfs_t* lfs,
    const lfs_cache_t* write_cache, lfs_cache_t* read_cache, lfs_size_t hint,
    lfs_block_t block, lfs_off_t offset,
    const void* buffer, lfs_size_t size) {

    const uint8_t* data = (const uint8_t *)buffer;
    lfs_size_t diff = 0;

    for (lfs_off_t idx = 0; idx < size; idx += diff) {

        uint8_t dat[256];

        diff = lfs_min(size - idx, sizeof(dat));

        int res = lfs_bd_read(lfs, write_cache, read_cache, hint - idx, block, offset + idx, &dat, diff);

        if (res) {
            return res;
        }

        res = memcmp(dat, data + idx, diff);

        if (res) {

            return res < 0 ? LFS_CMP_LT : LFS_CMP_GT;
        }
    }

    return LFS_CMP_EQ;
}

int lfs_bd_flush(lfs_t* lfs, lfs_cache_t* write_cache, lfs_cache_t* read_cache, bool validate) {

    if (write_cache->block != LFS_BLOCK_NULL && write_cache->block != LFS_BLOCK_INLINE) {

        lfs_size_t diff = lfs_alignup(write_cache->size, lfs->cfg->write_size);

        int err = lfs_bd_rawprog(lfs, write_cache->block, write_cache->offset, write_cache->buffer, diff);

        LFS_ASSERT(err <= 0);

        if (err) {
            return err;
        }

        if (validate) {

            // check data on disk
            lfs_cache_drop(lfs, read_cache);

            int res = lfs_bd_cmp(lfs, NULL, read_cache, diff, write_cache->block, write_cache->offset, write_cache->buffer, diff);

            if (res < 0) {
                return res;
            }

            if (res != LFS_CMP_EQ) {

                return LFS_ERR_CORRUPT;
            }
        }

        lfs_cache_zero(lfs, write_cache);
    }

    return LFS_ERR_OK;
}

int lfs_bd_sync(lfs_t* lfs, lfs_cache_t* write_cache, lfs_cache_t* read_cache, bool validate) {

    lfs_cache_drop(lfs, read_cache);

    int err = lfs_bd_flush(lfs, write_cache, read_cache, validate);

    if (err) {

        return err;
    }

    err = lfs->cfg->sync(lfs->cfg);
    LFS_ASSERT(err <= 0);
    return err;
}

int lfs_bd_write(lfs_t* lfs,
    lfs_cache_t* write_cache, lfs_cache_t* read_cache, bool validate,
    lfs_block_t block, lfs_off_t offset,
    const void* buffer, lfs_size_t size) {

    const uint8_t* data = (const uint8_t *)buffer;
    LFS_ASSERT(block == LFS_BLOCK_INLINE || block < lfs->block_count);
    LFS_ASSERT(offset + size <= lfs->block_size);

    while (size > 0) {

        if (block == write_cache->block &&
            offset >= write_cache->offset &&
            offset < write_cache->offset + lfs->cfg->cache_size) {

            // already fits in write_cache?
            lfs_size_t diff = lfs_min(size, lfs->cfg->cache_size - (offset - write_cache->offset));

            memcpy(&write_cache->buffer[offset - write_cache->offset], data, diff);

            data += diff;
            offset += diff;
            size -= diff;

            write_cache->size = lfs_max(write_cache->size, offset - write_cache->offset);

            if (write_cache->size == lfs->cfg->cache_size) {

                // eagerly flush out write_cache if we fill up
                int err = lfs_bd_flush(lfs, write_cache, read_cache, validate);

                if (err) {

                    return err;
                }
            }

            continue;
        }

        // write_cache must have been flushed, either by programming and
        // entire block or manually flushing the write_cache
        LFS_ASSERT(write_cache->block == LFS_BLOCK_NULL);

        // prepare write_cache, first condition can no longer fail
        write_cache->block = block;
        write_cache->offset = lfs_aligndown(offset, lfs->cfg->write_size);
        write_cache->size = 0;
    }

    return LFS_ERR_OK;
}

int lfs_bd_erase(lfs_t* lfs, lfs_block_t block) {

    LFS_ASSERT(block < lfs->block_count);

    // adjust to physical erase size
    block = block * (lfs->block_size / lfs->erase_size);

    for (lfs_block_t idx = 0; idx < lfs->block_size / lfs->erase_size; idx++) {
    
        int err = lfs->cfg->erase(lfs->cfg, block + idx);
        
        LFS_ASSERT(err <= 0);
        
        if (err < 0) {

            return err;
        }
    }

    return LFS_ERR_OK;
}