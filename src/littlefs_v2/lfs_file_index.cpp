#include "lfs.h"

/// File index list operations ///
int lfs_ctz_index(lfs_t* lfs, lfs_off_t* offset) {

    lfs_off_t size = *offset;
    lfs_off_t b = lfs->block_size - 2 * sizeof(uint64_t);
    lfs_off_t i = size / b;

    if (i == 0) {
        return LFS_ERR_OK;
    }

    i = (size - sizeof(uint64_t) * (lfs_popc64(i - 1) + 2)) / b;
    *offset = size - b * i - sizeof(uint64_t) * lfs_popc64(i);
    return i;
}

int lfs_ctz_find(lfs_t* lfs,
    const lfs_cache_t* write_cache, lfs_cache_t* read_cache,
    lfs_block_t head, lfs_size_t size,
    lfs_size_t pos, lfs_block_t* block, lfs_off_t* offset) {

    if (size == 0) {

        *block = LFS_BLOCK_NULL;
        *offset = 0;
        return LFS_ERR_OK;
    }

    lfs_size_t noff = size - 1;
    lfs_off_t current = lfs_ctz_index(lfs, &noff);
    lfs_off_t target = lfs_ctz_index(lfs, &pos);

    while (current > target) {

        lfs_size_t skip = lfs_min(lfs_npw2_64(current - target + 1) - 1, lfs_ctz64(current));

        int err = lfs_bd_read(lfs,
            write_cache, read_cache, sizeof(head),
            head, sizeof(uint64_t) * skip, &head, sizeof(head));

        head = lfs_fromle64(head);

        if (err) {

            return err;
        }

        current -= (uint64_t)1 << skip;
    }

    *block = head;
    *offset = pos;
    return LFS_ERR_OK;
}

int lfs_ctz_extend(lfs_t* lfs,
    lfs_cache_t* write_cache, lfs_cache_t* read_cache,
    lfs_block_t head, lfs_size_t size,
    lfs_block_t* block, lfs_off_t* offset) {

    while (true) {

        // go ahead and grab a block
        lfs_block_t nblock;
        int err = lfs_alloc(lfs, &nblock);

        if (err) {

            return err;
        }

        {
            err = lfs_bd_erase(lfs, nblock);
            
            if (err) {
                
                if (err == LFS_ERR_CORRUPT) {
                
                    goto relocate;
                }

                return err;
            }

            if (size == 0) {

                *block = nblock;
                *offset = 0;
                return LFS_ERR_OK;
            }

            lfs_size_t noff = size - 1;
            lfs_off_t index = lfs_ctz_index(lfs, &noff);
            noff = noff + 1;

            // just copy out the last block if it is incomplete
            if (noff != lfs->block_size) {

                for (lfs_off_t idx = 0; idx < noff; idx++) {

                    uint8_t data;
                    err = lfs_bd_read(lfs,
                        NULL, read_cache, noff - idx,
                        head, idx, &data, 1);

                    if (err) {

                        return err;
                    }

                    err = lfs_bd_write(lfs,
                        write_cache, read_cache, true,
                        nblock, idx, &data, 1);

                    if (err) {

                        if (err == LFS_ERR_CORRUPT) {

                            goto relocate;
                        }

                        return err;
                    }
                }

                *block = nblock;
                *offset = noff;
                return LFS_ERR_OK;
            }

            // append block
            index += 1;
            lfs_size_t skips = lfs_ctz64(index) + 1;
            lfs_block_t nhead = head;

            for (lfs_off_t idx = 0; idx < skips; idx++) {

                nhead = lfs_tole64(nhead);
                err = lfs_bd_write(lfs, write_cache, read_cache, true,
                    nblock, sizeof(uint64_t) * idx, &nhead, sizeof(nhead));

                nhead = lfs_fromle64(nhead);

                if (err) {

                    if (err == LFS_ERR_CORRUPT) {

                        goto relocate;
                    }

                    return err;
                }

                if (idx != skips - 1) {

                    err = lfs_bd_read(lfs,
                        NULL, read_cache, sizeof(nhead),
                        nhead, sizeof(uint64_t) * idx, &nhead, sizeof(nhead));

                    nhead = lfs_fromle64(nhead);

                    if (err) {
                        return err;
                    }
                }
            }

            *block = nblock;
            *offset = sizeof(uint64_t) * skips;
            return LFS_ERR_OK;
        }

    relocate:
        LFS_DEBUG("Bad block at 0x%"PRIx32, nblock);

        // just clear cache and try a new block
        lfs_cache_drop(lfs, write_cache);
    }
}

int lfs_ctz_traverse(lfs_t* lfs,
    const lfs_cache_t* write_cache, lfs_cache_t* read_cache,
    lfs_block_t head, lfs_size_t size,
    int (*cb)(void*, lfs_block_t), void* data) {

    if (size == 0) {

        return LFS_ERR_OK;
    }

    lfs_size_t noff = size - 1;
    lfs_off_t index = lfs_ctz_index(lfs, &noff);

    while (true) {

        int err = cb(data, head);

        if (err) {
            return err;
        }

        if (index == 0) {

            return LFS_ERR_OK;
        }

        lfs_block_t heads[2];
        int count = 2 - (index & 1);

        err = lfs_bd_read(lfs,
            write_cache, read_cache, count * sizeof(head),
            head, 0, &heads, count * sizeof(head));

        lfs_pair_fromle64(heads);

        if (err) {
            return err;
        }

        err = cb(data, heads[0]);

        if (err) {
            return err;
        }

        head = heads[count - 1];
        index -= count;
    }
}

