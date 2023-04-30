#include "lfs.h"

/// Metadata pair and directory operations ///

lfs_stag_t lfs_dir_getslice(lfs_t* lfs, const lfs_metadata_dir_t* dir,
    lfs_tag_t gmask, lfs_tag_t gtag,
    lfs_off_t goff, void* gbuffer, lfs_size_t gsize) {

    lfs_off_t offset = dir->offset;
    lfs_tag_t ntag = dir->etag;
    lfs_stag_t gdiff = 0;

    if (lfs_gstate_hasmovehere(&lfs->gdisk, dir->pair) &&
        lfs_tag_id(gmask) != 0 && lfs_tag_id(lfs->gdisk.tag) <= lfs_tag_id(gtag)) {

        // synthetic moves
        gdiff -= LFS_MKTAG(0, 1, 0);
    }

    // iterate over dir block backwards (for faster lookups)
    while (offset >= sizeof(lfs_tag_t) + lfs_tag_dsize(ntag)) {

        offset -= lfs_tag_dsize(ntag);

        lfs_tag_t tag = ntag;

        /*
            Read the tag
        */
        int err = lfs_bd_read(lfs, NULL, &lfs->read_cache, sizeof(ntag), dir->pair[0], offset, &ntag, sizeof(ntag));

        if (err) {
            return err;
        }

        ntag = (lfs_frombe32(ntag) ^ tag) & 0x7fffffff;

        if (lfs_tag_id(gmask) != 0 &&
            lfs_tag_type1(tag) == LFS_TYPE_SPLICE &&
            lfs_tag_id(tag) <= lfs_tag_id(gtag - gdiff)) {

            if (tag == (LFS_MKTAG(LFS_TYPE_CREATE, 0, 0) | (LFS_MKTAG(0, 0x3ff, 0) & (gtag - gdiff)))) {

                // found where we were created
                return LFS_ERR_NOENT;
            }

            // move around splices
            gdiff += LFS_MKTAG(0, lfs_tag_splice(tag), 0);
        }

        if ((gmask & tag) == (gmask & (gtag - gdiff))) {

            if (lfs_tag_isdelete(tag)) {

                return LFS_ERR_NOENT;
            }

            lfs_size_t diff = lfs_min(lfs_tag_size(tag), gsize);

            /*
                
            */
            err = lfs_bd_read(lfs, NULL, &lfs->read_cache, diff, dir->pair[0], offset + sizeof(tag) + goff, gbuffer, diff);

            if (err) {
                return err;
            }

            memset((uint8_t*)gbuffer + diff, 0, gsize - diff);

            return tag + gdiff;
        }
    }

    return LFS_ERR_NOENT;
}

lfs_stag_t lfs_dir_get(lfs_t* lfs, const lfs_metadata_dir_t* dir, lfs_tag_t gmask, lfs_tag_t gtag, void* buffer) {

    return lfs_dir_getslice(lfs, dir, gmask, gtag, 0, buffer, lfs_tag_size(gtag));
}

int lfs_dir_getread(lfs_t* lfs, const lfs_metadata_dir_t* dir,
    const lfs_cache_t* write_cache, lfs_cache_t* read_cache, lfs_size_t hint,
    lfs_tag_t gmask, lfs_tag_t gtag,
    lfs_off_t offset, void* buffer, lfs_size_t size) {

    uint8_t* data = (uint8_t *)buffer;

    if (offset + size > lfs->block_size) {
        return LFS_ERR_CORRUPT;
    }

    while (size > 0) {

        lfs_size_t diff = size;

        if (write_cache && write_cache->block == LFS_BLOCK_INLINE && offset < write_cache->offset + write_cache->size) {

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

        if (read_cache->block == LFS_BLOCK_INLINE && offset < read_cache->offset + read_cache->size) {

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

        // load to cache, first condition can no longer fail
        read_cache->block = LFS_BLOCK_INLINE;
        read_cache->offset = lfs_aligndown(offset, lfs->cfg->read_size);
        read_cache->size = lfs_min(lfs_alignup(offset + hint, lfs->cfg->read_size), lfs->cfg->cache_size);

        int err = lfs_dir_getslice(lfs, dir, gmask, gtag,
            read_cache->offset, read_cache->buffer, read_cache->size);

        if (err < 0) {
            return err;
        }
    }

    return LFS_ERR_OK;
}

int lfs_dir_traverse_filter(void* p, lfs_tag_t tag, const void* buffer) {

    lfs_tag_t* filtertag = (lfs_tag_t *)p;

    (void)buffer;

    // which mask depends on unique bit in tag structure
    uint32_t mask = (tag & LFS_MKTAG(LFS_TYPE_FROM, 0, 0))
        ? LFS_MKTAG(LFS_TYPE_MOVESTATE, 0x3ff, 0)
        : LFS_MKTAG(LFS_TYPE_GLOBALS, 0x3ff, 0);

    // check for redundancy
    if ((mask & tag) == (mask & *filtertag) ||
        lfs_tag_isdelete(*filtertag) ||
        (LFS_MKTAG(LFS_TYPE_MOVESTATE, 0x3ff, 0) & tag) == (
            LFS_MKTAG(LFS_TYPE_DELETE, 0, 0) |
            (LFS_MKTAG(0, 0x3ff, 0) & *filtertag))) {

        *filtertag = LFS_MKTAG(LFS_FROM_NOOP, 0, 0);

        return true;
    }

    // check if we need to adjust for created/deleted tags
    if (lfs_tag_type1(tag) == LFS_TYPE_SPLICE &&
        lfs_tag_id(tag) <= lfs_tag_id(*filtertag)) {

        *filtertag += LFS_MKTAG(0, lfs_tag_splice(tag), 0);
    }

    return false;
}
// maximum recursive depth of lfs_dir_traverse, the deepest call:
//
// traverse with commit
// '-> traverse with move
//     '-> traverse with filter
//
#define LFS_DIR_TRAVERSE_DEPTH 3


int lfs_dir_traverse(lfs_t* lfs,
    const lfs_metadata_dir_t* dir, lfs_off_t offset, lfs_tag_t ptag,
    const lfs_metadata_attribute_t* attrs, int attrcount,
    lfs_tag_t tmask, lfs_tag_t ttag,
    uint16_t begin, uint16_t end, int16_t diff,
    int (*cb)(void* data, lfs_tag_t tag, const void* buffer), void* data) {

    // This function in inherently recursive, but bounded. To allow tool-based
    // analysis without unnecessary code-cost we use an explicit stack

    lfs_dir_traverse_t stack[LFS_DIR_TRAVERSE_DEPTH - 1];
    size_t stack_index = 0;

    int res;

    // iterate over directory and attrs
    lfs_tag_t tag;
    const void* buffer;
    lfs_disk_offset_t disk = { 0 , 0 };

    while (true) {

        {
            if (offset + lfs_tag_dsize(ptag) < dir->offset) {

                offset += lfs_tag_dsize(ptag);

                /*
                    read the tag
                */
                int err = lfs_bd_read(lfs,
                    NULL, &lfs->read_cache, sizeof(tag),
                    dir->pair[0], offset, &tag, sizeof(tag));

                if (err) {
                    return err;
                }

                tag = (lfs_frombe32(tag) ^ ptag) | 0x80000000;
                disk.block = dir->pair[0];
                disk.offset = offset + sizeof(lfs_tag_t);
                buffer = &disk;
                ptag = tag;

            }
            else if (attrcount > 0) {

                tag = attrs[0].tag;
                buffer = attrs[0].buffer;
                attrs += 1;
                attrcount -= 1;

            }
            else {

                // finished traversal, pop from stack?
                res = 0;
                break;
            }

            // do we need to filter?
            lfs_tag_t mask = LFS_MKTAG(LFS_TYPE_MOVESTATE, 0, 0);
            if ((mask & tmask & tag) != (mask & tmask & ttag)) {

                continue;
            }

            if (lfs_tag_id(tmask) != 0) {

                LFS_ASSERT(stack_index < LFS_DIR_TRAVERSE_DEPTH);
                // recurse, scan for duplicates, and update tag based on
                // creates/deletes
                stack[stack_index] = {
                    (lfs_metadata_dir_t *)dir, offset, ptag,
                    attrs, attrcount,
                    tmask, ttag,
                    begin, end, diff,
                    cb,
                    data,
                    tag, buffer, disk,
                };

                stack_index += 1;

                tmask = 0;
                ttag = 0;
                begin = 0;
                end = 0;
                diff = 0;
                cb = lfs_dir_traverse_filter;
                data = &stack[stack_index - 1].tag;
                continue;
            }
        }

    popped:
        // in filter range?
        if (lfs_tag_id(tmask) != 0 && !(lfs_tag_id(tag) >= begin && lfs_tag_id(tag) < end)) {

            continue;
        }

        // handle special cases for mcu-side operations
        if (lfs_tag_type3(tag) == LFS_FROM_NOOP) {
            // do nothing
        }
        else if (lfs_tag_type3(tag) == LFS_FROM_MOVE) {
            // Without this condition, lfs_dir_traverse can exhibit an
            // extremely expensive O(n^3) of nested loops when renaming.
            // This happens because lfs_dir_traverse tries to filter tags by
            // the tags in the source directory, triggering a second
            // lfs_dir_traverse with its own filter operation.
            //
            // traverse with commit
            // '-> traverse with filter
            //     '-> traverse with move
            //         '-> traverse with filter
            //
            // However we don't actually care about filtering the second set of
            // tags, since duplicate tags have no effect when filtering.
            //
            // This check skips this unnecessary recursive filtering explicitly,
            // reducing this runtime from O(n^3) to O(n^2).
            if (cb == lfs_dir_traverse_filter) {

                continue;
            }

            // recurse into move
            lfs_dir_traverse_t dir_trav{};


            stack[stack_index] = {
                (lfs_metadata_dir_t*)dir, offset, ptag,
                attrs, attrcount,
                tmask, ttag,
                begin, end, diff,
                cb, data,
                LFS_MKTAG(LFS_FROM_NOOP, 0, 0), 0, 0
            };

            stack_index += 1;

            uint16_t fromid = (uint16_t)lfs_tag_size(tag);
            uint16_t toid = (uint16_t)lfs_tag_id(tag);

            dir = (const lfs_metadata_dir_t *)buffer;
            offset = 0;
            ptag = 0xffffffff;
            attrs = NULL;
            attrcount = 0;
            tmask = LFS_MKTAG(LFS_TYPE_TAIL, 0x3ff, 0);
            ttag = LFS_MKTAG(LFS_TYPE_STRUCT, 0, 0);
            begin = fromid;
            end = fromid + 1;
            diff = toid - fromid + diff;

        }
        else if (lfs_tag_type3(tag) == LFS_FROM_USERATTRS) {

            for (unsigned i = 0; i < lfs_tag_size(tag); i++) {

                const lfs_user_attribute_t* a = (const lfs_user_attribute_t*)buffer;

                res = cb(data, LFS_MKTAG(LFS_TYPE_USERATTR + a[i].type, lfs_tag_id(tag) + diff, a[i].size), a[i].buffer);

                if (res < 0) {
                    return res;
                }

                if (res) {
                    break;
                }
            }
        }
        else {

            res = cb(data, tag + LFS_MKTAG(0, diff, 0), buffer);

            if (res < 0) {
                return res;
            }

            if (res) {
                break;
            }
        }
    }

    if (stack_index > 0) {

        // pop from the stack and return, fortunately all pops share
        // a destination
        dir = stack[stack_index - 1].dir;
        offset = stack[stack_index - 1].offset;
        ptag = stack[stack_index - 1].ptag;
        attrs = stack[stack_index - 1].attrs;
        attrcount = stack[stack_index - 1].attrcount;
        tmask = stack[stack_index - 1].tmask;
        ttag = stack[stack_index - 1].ttag;
        begin = stack[stack_index - 1].begin;
        end = stack[stack_index - 1].end;
        diff = stack[stack_index - 1].diff;
        cb = stack[stack_index - 1].cb;
        data = stack[stack_index - 1].data;
        tag = stack[stack_index - 1].tag;
        buffer = stack[stack_index - 1].buffer;
        disk = stack[stack_index - 1].disk;
        stack_index -= 1;

        goto popped;

    }
    else {

        return res;
    }
}

lfs_stag_t lfs_dir_fetchmatch(lfs_t* lfs,
    lfs_metadata_dir_t* dir, const lfs_block_t pair[2],
    lfs_tag_t fmask, lfs_tag_t ftag, uint16_t* id,
    int (*cb)(void* data, lfs_tag_t tag, const void* buffer), void* data) {

    // we can find tag very efficiently during a fetch, since we're already
    // scanning the entire directory
    lfs_stag_t besttag = -1;

    // if either block address is invalid we return LFS_ERR_CORRUPT here,
    // otherwise later writes to the pair could fail
    if (pair[0] >= lfs->block_count || pair[1] >= lfs->block_count) {

        return LFS_ERR_CORRUPT;
    }

    // find the block with the most recent revision
    uint32_t revs[2] = { 0, 0 };
    int r = 0;

    for (int i = 0; i < 2; i++) {

        /*
            Read revision
        */
        int err = lfs_bd_read(lfs, NULL, &lfs->read_cache, sizeof(revs[i]), pair[i], 0, &revs[i], sizeof(revs[i]));

        revs[i] = lfs_fromle32(revs[i]);

        if (err && err != LFS_ERR_CORRUPT) {

            return err;
        }

        if (err != LFS_ERR_CORRUPT && lfs_scmp(revs[i], revs[(i + 1) % 2]) > 0) {

            r = i;
        }
    }

    dir->pair[0] = pair[(r + 0) % 2];
    dir->pair[1] = pair[(r + 1) % 2];
    dir->revision_count = revs[(r + 0) % 2];
    dir->offset = 0; // nonzero = found some commits

    // now scan tags to fetch the actual dir and find possible match
    for (size_t idx = 0; idx < 2; idx++) {

        lfs_off_t offset = 0;
        lfs_tag_t ptag = 0xffffffff;

        uint16_t tempcount = 0;
        lfs_block_t temptail[2] = { LFS_BLOCK_NULL, LFS_BLOCK_NULL };
        bool tempsplit = false;
        lfs_stag_t tempbesttag = besttag;

        dir->revision_count = lfs_tole32(dir->revision_count);
        uint32_t crc = lfs_crc(0xffffffff, &dir->revision_count, sizeof(dir->revision_count));
        dir->revision_count = lfs_fromle32(dir->revision_count);

        while (true) {

            // extract next tag
            lfs_tag_t tag;
            offset += lfs_tag_dsize(ptag);

            //some bug offset+size (recheck)
            int err = lfs_bd_read(lfs, NULL, &lfs->read_cache, lfs->block_size, dir->pair[0], offset, &tag, sizeof(tag));

            if (err) {

                if (err == LFS_ERR_CORRUPT) {

                    // can't continue?
                    dir->erased = false;
                    break;
                }

                return err;
            }

            crc = lfs_crc(crc, &tag, sizeof(tag));
            tag = lfs_frombe32(tag) ^ ptag;

            // next commit not yet programmed or we're not in valid range
            if (!lfs_tag_isvalid(tag)) {

                dir->erased = (lfs_tag_type1(ptag) == LFS_TYPE_CRC && dir->offset % lfs->cfg->write_size == 0);

                break;

            }
            else if (offset + lfs_tag_dsize(tag) > lfs->block_size) {

                dir->erased = false;
                break;
            }

            ptag = tag;

            if (lfs_tag_type1(tag) == LFS_TYPE_CRC) {

                // check the crc attr
                uint32_t dcrc;
                err = lfs_bd_read(lfs, NULL, &lfs->read_cache, lfs->block_size, dir->pair[0], offset + sizeof(tag), &dcrc, sizeof(dcrc));

                if (err) {

                    if (err == LFS_ERR_CORRUPT) {

                        dir->erased = false;
                        break;
                    }

                    return err;
                }

                dcrc = lfs_fromle32(dcrc);

                if (crc != dcrc) {

                    dir->erased = false;
                    break;
                }

                // reset the next bit if we need to
                ptag ^= (lfs_tag_t)(lfs_tag_chunk(tag) & 1U) << 31;

                // toss our crc into the filesystem seed for
                // pseudorandom numbers, note we use another crc here
                // as a collection function because it is sufficiently
                // random and convenient
                lfs->seed = lfs_crc(lfs->seed, &crc, sizeof(crc));

                // update with what's found so far
                besttag = tempbesttag;
                dir->offset = offset + lfs_tag_dsize(tag);
                dir->etag = ptag;
                dir->count = tempcount;
                dir->tail[0] = temptail[0];
                dir->tail[1] = temptail[1];
                dir->split = tempsplit;

                // reset crc
                crc = 0xffffffff;
                continue;
            }

            // crc the entry first, hopefully leaving it in the cache
            for (lfs_off_t j = sizeof(tag); j < lfs_tag_dsize(tag); j++) {

                uint8_t dat;
                err = lfs_bd_read(lfs, NULL, &lfs->read_cache, lfs->block_size, dir->pair[0], offset + j, &dat, 1);

                if (err) {

                    if (err == LFS_ERR_CORRUPT) {

                        dir->erased = false;
                        break;
                    }

                    return err;
                }

                crc = lfs_crc(crc, &dat, 1);
            }

            // directory modification tags?
            if (lfs_tag_type1(tag) == LFS_TYPE_NAME) {

                // increase count of files if necessary
                if (lfs_tag_id(tag) >= tempcount) {

                    tempcount = lfs_tag_id(tag) + 1;
                }

            }
            else if (lfs_tag_type1(tag) == LFS_TYPE_SPLICE) {

                tempcount += lfs_tag_splice(tag);

                if (tag == (LFS_MKTAG(LFS_TYPE_DELETE, 0, 0) | (LFS_MKTAG(0, 0x3ff, 0) & tempbesttag))) {

                    tempbesttag |= 0x80000000;

                }
                else if (tempbesttag != -1 && lfs_tag_id(tag) <= lfs_tag_id(tempbesttag)) {

                    tempbesttag += LFS_MKTAG(0, lfs_tag_splice(tag), 0);
                }
            }
            else if (lfs_tag_type1(tag) == LFS_TYPE_TAIL) {

                tempsplit = (lfs_tag_chunk(tag) & 1);

                err = lfs_bd_read(lfs,
                    NULL, &lfs->read_cache, lfs->block_size,
                    dir->pair[0], offset + sizeof(tag), &temptail, sizeof(temptail));

                if (err) {

                    if (err == LFS_ERR_CORRUPT) {

                        dir->erased = false;
                        break;
                    }

                    return err;
                }

                lfs_pair_fromle64(temptail);
            }

            // found a match for our fetcher?
            if ((fmask & tag) == (fmask & ftag)) {

                lfs_disk_offset_t _diskoff = { dir->pair[0], offset + sizeof(tag) };

                int res = cb(data, tag, &_diskoff);

                if (res < 0) {

                    if (res == LFS_ERR_CORRUPT) {

                        dir->erased = false;
                        break;
                    }

                    return res;
                }

                if (res == LFS_CMP_EQ) {
                    // found a match
                    tempbesttag = tag;
                }
                else if ((LFS_MKTAG(LFS_TYPE_MOVESTATE, 0x3ff, 0) & tag) == (LFS_MKTAG(LFS_TYPE_MOVESTATE, 0x3ff, 0) & tempbesttag)) {

                    // found an identical tag, but contents didn't match
                    // this must mean that our besttag has been overwritten
                    tempbesttag = -1;

                }
                else if (res == LFS_CMP_GT && lfs_tag_id(tag) <= lfs_tag_id(tempbesttag)) {

                    // found a greater match, keep track to keep things sorted
                    tempbesttag = tag | 0x80000000;
                }
            }
        }

        // consider what we have good enough
        if (dir->offset > 0) {

            // synthetic move
            if (lfs_gstate_hasmovehere(&lfs->gdisk, dir->pair)) {

                if (lfs_tag_id(lfs->gdisk.tag) == lfs_tag_id(besttag)) {

                    besttag |= 0x80000000;
                }
                else if (besttag != -1 && lfs_tag_id(lfs->gdisk.tag) < lfs_tag_id(besttag)) {

                    besttag -= LFS_MKTAG(0, 1, 0);
                }
            }

            // found tag? or found best id?
            if (id) {
                *id = (uint16_t)lfs_min(lfs_tag_id(besttag), dir->count);
            }

            if (lfs_tag_isvalid(besttag)) {

                return besttag;

            }
            else if (lfs_tag_id(besttag) < dir->count) {

                return LFS_ERR_NOENT;

            }
            else {

                return LFS_ERR_OK;
            }
        }

        // failed, try the other block?
        lfs_pair_swap(dir->pair);
        dir->revision_count = revs[(r + 1) % 2];
    }

    if (!lfs_pair_isnull(lfs->root)) {
        LFS_ERROR("Corrupted dir pair at {0x%"PRIx32", 0x%"PRIx32"}", dir->pair[0], dir->pair[1]);
    }

    return LFS_ERR_CORRUPT;
}

int lfs_dir_fetch(lfs_t* lfs, lfs_metadata_dir_t* dir, const lfs_block_t pair[2]) {

    // note, mask=-1, tag=-1 can never match a tag since this
    // pattern has the invalid bit set
    return (int)lfs_dir_fetchmatch(lfs, dir, pair, (lfs_tag_t)-1, (lfs_tag_t)-1, NULL, NULL, NULL);
}
 
int lfs_dir_getgstate(lfs_t* lfs, const lfs_metadata_dir_t* dir, lfs_gstate_t* gstate) {

    lfs_gstate_t temp;
    lfs_stag_t res = lfs_dir_get(lfs, dir, 
        LFS_MKTAG(LFS_TYPE_MOVESTATE, 0, 0),
        LFS_MKTAG(LFS_TYPE_MOVESTATE, 0, sizeof(temp)), &temp);

    if (res < 0 && res != LFS_ERR_NOENT) {

        return res;
    }

    if (res != LFS_ERR_NOENT) {

        // xor together to find resulting gstate
        lfs_gstate_fromle64(&temp);
        lfs_gstate_xor(gstate, &temp);
    }

    return LFS_ERR_OK;
}

int lfs_dir_getinfo(lfs_t* lfs, lfs_metadata_dir_t* dir, uint16_t id, struct lfs_info* info) {

    if (id == 0x3ff) {

        // special case for root
        strcpy_s(info->name, sizeof(info->name), "/");
        info->type = LFS_TYPE_DIR;
        return LFS_ERR_OK;
    }

    lfs_stag_t tag = lfs_dir_get(lfs, dir, 
            LFS_MKTAG(0x780, 0x3ff, 0), 
            LFS_MKTAG(LFS_TYPE_NAME, id, lfs->name_max_length + 1), 
            info->name);

    if (tag < 0) {
        return (int)tag;
    }

    info->type = (uint8_t)lfs_tag_type3(tag);

    lfs_ctz_t ctz;
    tag = lfs_dir_get(lfs, dir, 
            LFS_MKTAG(LFS_TYPE_GLOBALS, 0x3ff, 0),
            LFS_MKTAG(LFS_TYPE_STRUCT, id, sizeof(ctz)), 
            &ctz);

    if (tag < 0) {
        return (int)tag;
    }

    lfs_ctz_fromle64(&ctz);

    if (lfs_tag_type3(tag) == LFS_TYPE_CTZSTRUCT) {

        info->size = ctz.size;
    }
    else if (lfs_tag_type3(tag) == LFS_TYPE_INLINESTRUCT) {

        info->size = lfs_tag_size(tag);
    }

    return LFS_ERR_OK;
}

int lfs_dir_find_match(void* data, lfs_tag_t tag, const void* buffer) {

    lfs_dir_find_match_t* name = (lfs_dir_find_match_t *)data;
    lfs_t* lfs = name->lfs;
    const lfs_disk_offset_t* disk = (const lfs_disk_offset_t *)buffer;

    // compare with disk
    lfs_size_t diff = lfs_min(name->size, lfs_tag_size(tag));

    int res = lfs_bd_cmp(lfs, NULL, &lfs->read_cache, diff, disk->block, disk->offset, name->name, diff);

    if (res != LFS_CMP_EQ) {

        return res;
    }

    // only equal if our size is still the same
    if (name->size != lfs_tag_size(tag)) {

        return (name->size < lfs_tag_size(tag)) ? LFS_CMP_LT : LFS_CMP_GT;
    }

    // found a match!
    return LFS_CMP_EQ;
}

lfs_stag_t lfs_dir_find(lfs_t* lfs, lfs_metadata_dir_t* dir, const char** path, uint16_t* id) {

    // we reduce path to a single name if we can find it
    const char* name = *path;

    if (id) {
        *id = 0x3ff;
    }

    // default to root dir
    lfs_stag_t tag = LFS_MKTAG(LFS_TYPE_DIR, 0x3ff, 0);
    dir->tail[0] = lfs->root[0];
    dir->tail[1] = lfs->root[1];

    while (true) {
    nextname:
        // skip slashes
        name += strspn(name, "/");
        lfs_size_t namelen = strcspn(name, "/");

        // skip '.' and root '..'
        if ((namelen == 1 && memcmp(name, ".", 1) == 0) ||
            (namelen == 2 && memcmp(name, "..", 2) == 0)) {

            name += namelen;
            goto nextname;
        }

        // skip if matched by '..' in name
        const char* suffix = name + namelen;
        lfs_size_t sufflen;
        int depth = 1;

        while (true) {

            suffix += strspn(suffix, "/");
            sufflen = strcspn(suffix, "/");

            if (sufflen == 0) {

                break;
            }

            if (sufflen == 2 && memcmp(suffix, "..", 2) == 0) {

                depth -= 1;

                if (depth == 0) {

                    name = suffix + sufflen;
                    goto nextname;
                }
            }
            else {
                depth += 1;
            }

            suffix += sufflen;
        }

        // found path
        if (name[0] == '\0') {
            return tag;
        }

        // update what we've found so far
        *path = name;

        // only continue if we hit a directory
        if (lfs_tag_type3(tag) != LFS_TYPE_DIR) {

            return LFS_ERR_NOTDIR;
        }

        // grab the entry data
        if (lfs_tag_id(tag) != 0x3ff) {

            lfs_stag_t res = lfs_dir_get(lfs, dir, 
                LFS_MKTAG(LFS_TYPE_GLOBALS, 0x3ff, 0),
                LFS_MKTAG(LFS_TYPE_STRUCT, lfs_tag_id(tag), sizeof(dir->tail)), dir->tail);

            if (res < 0) {

                return res;
            }

            lfs_pair_fromle64(dir->tail);
        }

        // find entry matching name
        while (true) {

            lfs_dir_find_match_t _pred = { lfs, name, namelen };

            tag = lfs_dir_fetchmatch(lfs, dir, dir->tail,
                LFS_MKTAG(0x780, 0, 0),
                LFS_MKTAG(LFS_TYPE_NAME, 0, namelen),
                // are we last name?
                (strchr(name, '/') == NULL) ? id : NULL,
                lfs_dir_find_match, &_pred);

            if (tag < 0) {

                return tag;
            }

            if (tag) {
                break;
            }

            if (!dir->split) {

                return LFS_ERR_NOENT;
            }
        }

        // to next name
        name += namelen;
    }
}