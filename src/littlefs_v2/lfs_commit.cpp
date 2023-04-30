#include "lfs.h"


// commit logic

int lfs_dir_commit_write(lfs_t* lfs, lfs_commit_t* commit, const void* buffer, lfs_size_t size) {

    int err = lfs_bd_write(lfs,
        &lfs->write_cache, &lfs->read_cache, false,
        commit->block, commit->offset,
        (const uint8_t*)buffer, size);

    if (err) {
        return err;
    }

    commit->crc = lfs_crc(commit->crc, buffer, size);
    commit->offset += size;
    return LFS_ERR_OK;
}

int lfs_dir_commit_attribute(lfs_t* lfs, lfs_commit_t* commit, lfs_tag_t tag, const void* buffer) {

    // check if we fit

    lfs_size_t dsize = lfs_tag_dsize(tag);

    if (commit->offset + dsize > commit->end) {
        return LFS_ERR_NOSPC;
    }

    // write out tag
    lfs_tag_t ntag = lfs_tobe32((tag & 0x7fffffff) ^ commit->ptag);

    int err = lfs_dir_commit_write(lfs, commit, &ntag, sizeof(ntag));

    if (err) {

        return err;
    }

    if (!(tag & 0x80000000)) {
        // from memory
        err = lfs_dir_commit_write(lfs, commit, buffer, dsize - sizeof(tag));

        if (err) {

            return err;
        }

    }
    else {

        // from disk
        const lfs_disk_offset_t* disk = (const lfs_disk_offset_t *)buffer;

        for (lfs_off_t i = 0; i < dsize - sizeof(tag); i++) {

            // rely on caching to make this efficient
            uint8_t dat;
            err = lfs_bd_read(lfs,
                NULL, &lfs->read_cache, dsize - sizeof(tag) - i,
                disk->block, disk->offset + i, &dat, 1);

            if (err) {
                return err;
            }

            err = lfs_dir_commit_write(lfs, commit, &dat, 1);

            if (err) {
                return err;
            }
        }
    }

    commit->ptag = tag & 0x7fffffff;
    return LFS_ERR_OK;
}

int lfs_dir_commit_crc(lfs_t* lfs, lfs_commit_t* commit) {

    // align to program units
    const lfs_off_t end = lfs_alignup(commit->offset + 2 * sizeof(uint64_t), lfs->cfg->write_size);

    lfs_off_t off1 = 0;
    uint32_t crc1 = 0;

    // create crc tags to fill up remainder of commit, note that
    // padding is not crced, which lets fetches skip padding but
    // makes committing a bit more complicated
    while (commit->offset < end) {

        lfs_off_t offset = commit->offset + sizeof(lfs_tag_t);
        lfs_off_t noff = lfs_min(end - offset, 0x3fe) + offset;

        if (noff < end) {
            noff = lfs_min(noff, end - 2 * sizeof(uint64_t));
        }

        // read erased state from next program unit
        lfs_tag_t tag = 0xffffffff;
        int err = lfs_bd_read(lfs,
            NULL, &lfs->read_cache, sizeof(tag),
            commit->block, noff, &tag, sizeof(tag));

        if (err && err != LFS_ERR_CORRUPT) {
            return err;
        }

        // build crc tag
        bool reset = ~lfs_frombe32(tag) >> 31;
        tag = LFS_MKTAG(LFS_TYPE_CRC + reset, 0x3ff, noff - offset);

        // write out crc
        uint32_t footer[2];
        footer[0] = lfs_tobe32(tag ^ commit->ptag);
        commit->crc = lfs_crc(commit->crc, &footer[0], sizeof(footer[0]));
        footer[1] = lfs_tole32(commit->crc);

        err = lfs_bd_write(lfs,
            &lfs->write_cache, &lfs->read_cache, false,
            commit->block, commit->offset, &footer, sizeof(footer));

        if (err) {
            return err;
        }

        // keep track of non-padding checksum to verify
        if (off1 == 0) {
            off1 = commit->offset + sizeof(uint32_t);
            crc1 = commit->crc;
        }

        commit->offset += sizeof(tag) + lfs_tag_size(tag);
        commit->ptag = tag ^ ((lfs_tag_t)reset << 31);
        commit->crc = 0xffffffff; // reset crc for next "commit"
    }

    // flush buffers
    int err = lfs_bd_sync(lfs, &lfs->write_cache, &lfs->read_cache, false);

    if (err) {
        return err;
    }

    // successful commit, check checksums to make sure
    lfs_off_t offset = commit->begin;
    lfs_off_t noff = off1;

    while (offset < end) {

        uint32_t crc = 0xffffffff;

        for (lfs_off_t i = offset; i < noff + sizeof(uint32_t); i++) {

            // check against written crc, may catch blocks that
            // become readonly and match our commit size exactly
            if (i == off1 && crc != crc1) {
                return LFS_ERR_CORRUPT;
            }

            // leave it up to caching to make this efficient
            uint8_t dat;
            err = lfs_bd_read(lfs,
                NULL, &lfs->read_cache, noff + sizeof(uint32_t) - i,
                commit->block, i, &dat, 1);

            if (err) {
                return err;
            }

            crc = lfs_crc(crc, &dat, 1);
        }

        // detected write error?
        if (crc != 0) {
            return LFS_ERR_CORRUPT;
        }

        // skip padding
        offset = lfs_min(end - noff, 0x3fe) + noff;
        if (offset < end) {
            offset = lfs_min(offset, end - 2 * sizeof(uint64_t));
        }
        noff = offset + sizeof(uint32_t);
    }

    return LFS_ERR_OK;
}

int lfs_dir_alloc(lfs_t* lfs, lfs_metadata_dir_t* dir) {

    // allocate pair of dir blocks (backwards, so we write block 1 first)
    for (int i = 0; i < 2; i++) {

        int err = lfs_alloc(lfs, &dir->pair[(i + 1) % 2]);

        if (err) {
            return err;
        }
    }

    // zero for reproducibility in case initial block is unreadable
    dir->revision_count = 0;

    // rather than clobbering one of the blocks we just pretend
    // the revision may be valid
    int err = lfs_bd_read(lfs,
        NULL, &lfs->read_cache, sizeof(dir->revision_count),
        dir->pair[0], 0, &dir->revision_count, sizeof(dir->revision_count));

    dir->revision_count = lfs_fromle32(dir->revision_count);

    if (err && err != LFS_ERR_CORRUPT) {
        return err;
    }

    // to make sure we don't immediately evict, align the new revision count
    // to our block_cycles modulus, see lfs_dir_compact for why our modulus
    // is tweaked this way
    if (lfs->cfg->block_cycles > 0) {

        dir->revision_count = (uint32_t)lfs_alignup(dir->revision_count, ((lfs->cfg->block_cycles + 1) | 1));
    }

    // set defaults
    dir->offset = sizeof(dir->revision_count);
    dir->etag = 0xffffffff;
    dir->count = 0;
    dir->tail[0] = LFS_BLOCK_NULL;
    dir->tail[1] = LFS_BLOCK_NULL;
    dir->erased = false;
    dir->split = false;

    // don't write out yet, let caller take care of that
    return LFS_ERR_OK;
}

int lfs_dir_drop(lfs_t* lfs, lfs_metadata_dir_t* dir, lfs_metadata_dir_t* tail) {

    // steal state
    int err = lfs_dir_getgstate(lfs, tail, &lfs->gdelta);

    if (err) {
        return err;
    }

    // steal tail
    lfs_pair_tole64(tail->tail);

    lfs_metadata_attribute_t attr[] = {
        { LFS_MKTAG(LFS_TYPE_TAIL + tail->split, 0x3ff, sizeof(tail->tail)), tail->tail }
    };

    err = lfs_dir_commit(lfs, dir, attr, _countof(attr));

    lfs_pair_fromle64(tail->tail);

    if (err) {
        return err;
    }

    return LFS_ERR_OK;
}

int lfs_dir_split(lfs_t* lfs,
    lfs_metadata_dir_t* dir, const lfs_metadata_attribute_t* attrs, int attrcount,
    lfs_metadata_dir_t* source, uint16_t split, uint16_t end) {

    // create tail metadata pair
    lfs_metadata_dir_t tail{};
    int err = lfs_dir_alloc(lfs, &tail);

    if (err) {
        return err;
    }

    tail.split = dir->split;
    tail.tail[0] = dir->tail[0];
    tail.tail[1] = dir->tail[1];

    // note we don't care about LFS_OK_RELOCATED
    int res = lfs_dir_compact(lfs, &tail, attrs, attrcount, source, split, end);

    if (res < 0) {
        return res;
    }

    dir->tail[0] = tail.pair[0];
    dir->tail[1] = tail.pair[1];
    dir->split = true;

    // update root if needed
    if (lfs_pair_cmp(dir->pair, lfs->root) == 0 && split == 0) {
        lfs->root[0] = tail.pair[0];
        lfs->root[1] = tail.pair[1];
    }

    return LFS_ERR_OK;
}

int lfs_dir_commit_size(void* p, lfs_tag_t tag, const void* buffer) {

    lfs_size_t* size = (lfs_size_t * )p;
    (void)buffer;

    *size += lfs_tag_dsize(tag);
    return LFS_ERR_OK;
}

int lfs_dir_commit_commit(void* p, lfs_tag_t tag, const void* buffer) {

    lfs_dir_commit_commit_t* commit = (lfs_dir_commit_commit_t *)p;
    return lfs_dir_commit_attribute(commit->lfs, commit->commit, tag, buffer);
}

bool lfs_dir_needs_relocation(lfs_t* lfs, lfs_metadata_dir_t* dir) {
    // If our revision count == n * block_cycles, we should force a relocation,
    // this is how littlefs wear-levels at the metadata-pair level. Note that we
    // actually use (block_cycles+1)|1, this is to avoid two corner cases:
    // 1. block_cycles = 1, which would prevent relocations from terminating
    // 2. block_cycles = 2n, which, due to aliasing, would only ever relocate
    //    one metadata block in the pair, effectively making this useless
    return (lfs->cfg->block_cycles > 0 && ((dir->revision_count + 1) % ((lfs->cfg->block_cycles + 1) | 1) == 0));
}

int lfs_dir_compact(lfs_t* lfs,
    lfs_metadata_dir_t* dir, const lfs_metadata_attribute_t* attrs, int attrcount,
    lfs_metadata_dir_t* source, uint16_t begin, uint16_t end) {

    // save some state in case block is bad
    bool relocated = false;
    bool tired = lfs_dir_needs_relocation(lfs, dir);

    // increment revision count
    dir->revision_count += 1;

    const lfs_block_t pair[2] = { 0, 1 };

    if (tired && lfs_pair_cmp(dir->pair, pair) != 0) {
        // we're writing too much, time to relocate
        goto relocate;
    }

    // begin loop to commit compaction to blocks until a compact sticks
    while (true) {

        {
            // setup commit state
            lfs_commit_t commit = {
                dir->pair[1],
                0,
                0xffffffff,
                0xffffffff,

                0,
                (lfs->cfg->metadata_max ? lfs->cfg->metadata_max : lfs->block_size) - sizeof(lfs_block_t[2]),
            };

            // erase block to write to
            int err = lfs_bd_erase(lfs, dir->pair[1]);

            if (err) {

                if (err == LFS_ERR_CORRUPT) {

                    goto relocate;
                }

                return err;
            }

            // write out header
            dir->revision_count = lfs_tole32(dir->revision_count);
            err = lfs_dir_commit_write(lfs, &commit, &dir->revision_count, sizeof(dir->revision_count));
            dir->revision_count = lfs_fromle32(dir->revision_count);

            if (err) {

                if (err == LFS_ERR_CORRUPT) {

                    goto relocate;
                }

                return err;
            }

            // traverse the directory, this time writing out all unique tags

            lfs_dir_commit_commit_t _commit{lfs, &commit };

            err = lfs_dir_traverse(lfs,
                source, 0, 0xffffffff, attrs, attrcount,
                LFS_MKTAG(LFS_TYPE_SPLICE, 0x3ff, 0),
                LFS_MKTAG(LFS_TYPE_NAME, 0, 0),
                begin, end, -begin,
                lfs_dir_commit_commit, &_commit);

            if (err) {

                if (err == LFS_ERR_CORRUPT) {

                    goto relocate;
                }

                return err;
            }

            // commit tail, which may be new after last size check
            if (!lfs_pair_isnull(dir->tail)) {

                lfs_pair_tole64(dir->tail);

                err = lfs_dir_commit_attribute(lfs, &commit,
                    LFS_MKTAG(LFS_TYPE_TAIL + dir->split, 0x3ff, sizeof(dir->tail)), dir->tail);

                lfs_pair_fromle64(dir->tail);

                if (err) {

                    if (err == LFS_ERR_CORRUPT) {

                        goto relocate;
                    }

                    return err;
                }
            }

            // bring over gstate?
            lfs_gstate_t delta = { 0 };

            if (!relocated) {
                lfs_gstate_xor(&delta, &lfs->gdisk);
                lfs_gstate_xor(&delta, &lfs->gstate);
            }

            lfs_gstate_xor(&delta, &lfs->gdelta);
            delta.tag &= ~LFS_MKTAG(0, 0, 0x3ff);

            err = lfs_dir_getgstate(lfs, dir, &delta);

            if (err) {

                return err;
            }

            if (!lfs_gstate_iszero(&delta)) {

                lfs_gstate_tole64(&delta);

                err = lfs_dir_commit_attribute(lfs, &commit,
                        LFS_MKTAG(LFS_TYPE_MOVESTATE, 0x3ff, sizeof(delta)), &delta);

                if (err) {

                    if (err == LFS_ERR_CORRUPT) {

                        goto relocate;
                    }

                    return err;
                }
            }

            // complete commit with crc
            err = lfs_dir_commit_crc(lfs, &commit);

            if (err) {

                if (err == LFS_ERR_CORRUPT) {

                    goto relocate;
                }

                return err;
            }

            // successful compaction, swap dir pair to indicate most recent
            LFS_ASSERT(commit.offset % lfs->cfg->write_size == 0);
            lfs_pair_swap(dir->pair);
            dir->count = end - begin;
            dir->offset = commit.offset;
            dir->etag = commit.ptag;

            // update gstate
            lfs->gdelta = { 0 };

            if (!relocated) {
                lfs->gdisk = lfs->gstate;
            }
        }

        break;

    relocate:
        // commit was corrupted, drop caches and prepare to relocate block
        relocated = true;
        lfs_cache_drop(lfs, &lfs->write_cache);

        if (!tired) {
            LFS_DEBUG("Bad block at 0x%"PRIx32, dir->pair[1]);
        }

        // can't relocate superblock, filesystem is now frozen

        lfs_block_t _blocks[2] = { 0, 1 };

        if (lfs_pair_cmp(dir->pair, _blocks) == 0) {

            LFS_WARN("Superblock 0x%"PRIx32" has become unwritable", dir->pair[1]);
            return LFS_ERR_NOSPC;
        }

        // relocate half of pair
        int err = lfs_alloc(lfs, &dir->pair[1]);

        if (err && (err != LFS_ERR_NOSPC || !tired)) {
            return err;
        }

        tired = false;
        continue;
    }

    return relocated ? LFS_OK_RELOCATED : 0;
}

int lfs_dir_splittingcompact(lfs_t* lfs, lfs_metadata_dir_t* dir,
    const lfs_metadata_attribute_t* attrs, int attrcount,
    lfs_metadata_dir_t* source, uint16_t begin, uint16_t end) {

    while (true) {

        // find size of first split, we do this by halving the split until
        // the metadata is guaranteed to fit
        //
        // Note that this isn't a true binary search, we never increase the
        // split size. This may result in poorly distributed metadata but isn't
        // worth the extra code size or performance hit to fix.
        uint16_t split = begin;

        while (end - split > 1) {

            lfs_size_t size = 0;
            int err = lfs_dir_traverse(lfs,
                source, 0, 0xffffffff, 
                attrs, attrcount,
                LFS_MKTAG(LFS_TYPE_SPLICE, 0x3ff, 0), LFS_MKTAG(LFS_TYPE_NAME, 0, 0),
                split, end, int16_t(0 - split),
                lfs_dir_commit_size, &size);

            if (err) {

                return err;
            }

            // space is complicated, we need room for:
            //
            // - tail:         8+2*8 = 24 bytes
            // - gstate:       8+3*8 = 32 bytes
            // - move delete:  8     = 8 bytes
            // - crc:          4+4   = 8 bytes
            //                 total = 72 bytes
            //
            // And we cap at half a block to avoid degenerate cases with
            // nearly-full metadata blocks.
            //
            if (end - split < 0xff
                && size <= lfs_min(lfs->block_size - 72,
                    lfs_alignup(
                        (lfs->cfg->metadata_max
                            ? lfs->cfg->metadata_max
                            : lfs->block_size) / 2,
                        lfs->cfg->write_size))) {

                break;
            }

            split = split + ((end - split) / 2);
        }

        if (split == begin) {

            // no split needed
            break;
        }

        // split into two metadata pairs and continue
        int err = lfs_dir_split(lfs, dir, attrs, attrcount, source, split, end);

        if (err && err != LFS_ERR_NOSPC) {

            return err;
        }

        if (err) {

            // we can't allocate a new block, try to compact with degraded
            // performance
            LFS_WARN("Unable to split {0x%"PRIx32", 0x%"PRIx32"}", dir->pair[0], dir->pair[1]);
            break;

        }
        else {

            end = split;
        }
    }
    
    lfs_block_t _blocks[2] = { 0, 1 };

    if (lfs_dir_needs_relocation(lfs, dir) && lfs_pair_cmp(dir->pair, _blocks) == 0) {

        // oh no! we're writing too much to the superblock,
        // should we expand?

        lfs_ssize_t size = lfs_fs_rawsize(lfs);

        if (size < 0) {

            return int(size);
        }

        // do we have extra space? littlefs can't reclaim this space
        // by itself, so expand cautiously
        if ((lfs_size_t)size < lfs->block_count / 2) {

            LFS_DEBUG("Expanding superblock at revision_count %"PRIu32, dir->revision_count);

            int err = lfs_dir_split(lfs, dir, attrs, attrcount, source, begin, end);

            if (err && err != LFS_ERR_NOSPC) {

                return err;
            }

            if (err) {
                // welp, we tried, if we ran out of space there's not much
                // we can do, we'll error later if we've become frozen
                LFS_WARN("Unable to expand superblock");

            }
            else {

                end = begin;
            }
        }
    }

    return lfs_dir_compact(lfs, dir, attrs, attrcount, source, begin, end);
}

int lfs_dir_relocating_commit(lfs_t* lfs, lfs_metadata_dir_t* dir,
    const lfs_block_t pair[2], const lfs_metadata_attribute_t* attrs, int attrcount, lfs_metadata_dir_t* pdir) {

    int state = 0;

    // calculate changes to the directory
    bool hasdelete = false;

    for (int i = 0; i < attrcount; i++) {

        if (lfs_tag_type3(attrs[i].tag) == LFS_TYPE_CREATE) {

            dir->count += 1;

        }
        else if (lfs_tag_type3(attrs[i].tag) == LFS_TYPE_DELETE) {

            LFS_ASSERT(dir->count > 0);
            dir->count -= 1;
            hasdelete = true;

        }
        else if (lfs_tag_type1(attrs[i].tag) == LFS_TYPE_TAIL) {

            dir->tail[0] = ((lfs_block_t*)attrs[i].buffer)[0];
            dir->tail[1] = ((lfs_block_t*)attrs[i].buffer)[1];
            dir->split = (lfs_tag_chunk(attrs[i].tag) & 1);
            lfs_pair_fromle64(dir->tail);
        }
    }

    // should we actually drop the directory block?
    if (hasdelete && dir->count == 0) {

        LFS_ASSERT(pdir);

        int err = lfs_fs_pred(lfs, dir->pair, pdir);

        if (err && err != LFS_ERR_NOENT) {

            return err;
        }

        if (err != LFS_ERR_NOENT && pdir->split) {

            state = LFS_OK_DROPPED;
            goto fixmlist;
        }
    }

    if (dir->erased) {

        // try to commit
        lfs_commit_t commit = {
            dir->pair[0],
            dir->offset,
            dir->etag,
            0xffffffff,

            dir->offset,
            (lfs->cfg->metadata_max ?
                lfs->cfg->metadata_max : lfs->block_size) - sizeof(lfs_block_t[2]),
        };

        // traverse attrs that need to be written out
        lfs_pair_tole64(dir->tail);

        lfs_dir_commit_commit_t _commit = { lfs, &commit };

        int err = lfs_dir_traverse(lfs,
            dir, dir->offset, dir->etag, 
            attrs, attrcount,
            0, 0, 
            0, 0, 0,
            lfs_dir_commit_commit, &_commit);

        lfs_pair_fromle64(dir->tail);

        if (err) {

            if (err == LFS_ERR_NOSPC || err == LFS_ERR_CORRUPT) {

                goto compact;
            }

            return err;
        }

        // commit any global diffs if we have any
        lfs_gstate_t delta = { 0 };
        lfs_gstate_xor(&delta, &lfs->gstate);
        lfs_gstate_xor(&delta, &lfs->gdisk);
        lfs_gstate_xor(&delta, &lfs->gdelta);
        delta.tag &= ~LFS_MKTAG(0, 0, 0x3ff);

        if (!lfs_gstate_iszero(&delta)) {

            err = lfs_dir_getgstate(lfs, dir, &delta);

            if (err) {

                return err;
            }

            lfs_gstate_tole64(&delta);

            err = lfs_dir_commit_attribute(lfs, &commit,
                LFS_MKTAG(LFS_TYPE_MOVESTATE, 0x3ff,
                    sizeof(delta)), &delta);

            if (err) {

                if (err == LFS_ERR_NOSPC || err == LFS_ERR_CORRUPT) {

                    goto compact;
                }

                return err;
            }
        }

        // finalize commit with the crc
        err = lfs_dir_commit_crc(lfs, &commit);

        if (err) {

            if (err == LFS_ERR_NOSPC || err == LFS_ERR_CORRUPT) {

                goto compact;
            }

            return err;
        }

        // successful commit, update dir
        LFS_ASSERT(commit.offset % lfs->cfg->write_size == 0);
        dir->offset = commit.offset;
        dir->etag = commit.ptag;
        // and update gstate
        lfs->gdisk = lfs->gstate;
        lfs->gdelta = { 0 };

        goto fixmlist;
    }

compact:
    // fall back to compaction
    lfs_cache_drop(lfs, &lfs->write_cache);

    state = lfs_dir_splittingcompact(lfs, dir, attrs, attrcount, dir, 0, dir->count);

    if (state < 0) {

        return state;
    }

    goto fixmlist;

fixmlist:;
    // this complicated bit of logic is for fixing up any active
    // metadata-pairs that we may have affected
    //
    // note we have to make two passes since the mdir passed to
    // lfs_dir_commit could also be in this list, and even then
    // we need to copy the pair so they don't get clobbered if we refetch
    // our mdir.
    lfs_block_t oldpair[2] = { pair[0], pair[1] };
    for (lfs_metadata_list_t* entry = lfs->metadata_list; entry; entry = entry->next) {

        if (lfs_pair_cmp(entry->metadata.pair, oldpair) == 0) {

            entry->metadata = *dir;

            if (entry->metadata.pair != pair) {

                for (int i = 0; i < attrcount; i++) {

                    if (lfs_tag_type3(attrs[i].tag) == LFS_TYPE_DELETE && entry->id == lfs_tag_id(attrs[i].tag)) {

                        entry->metadata.pair[0] = LFS_BLOCK_NULL;
                        entry->metadata.pair[1] = LFS_BLOCK_NULL;
                    }
                    else if (lfs_tag_type3(attrs[i].tag) == LFS_TYPE_DELETE && entry->id > lfs_tag_id(attrs[i].tag)) {

                        entry->id -= 1;

                        if (entry->type == LFS_TYPE_DIR) {

                            ((lfs_dir_t*)entry)->pos -= 1;
                        }
                    }
                    else if (lfs_tag_type3(attrs[i].tag) == LFS_TYPE_CREATE && entry->id >= lfs_tag_id(attrs[i].tag)) {

                        entry->id += 1;

                        if (entry->type == LFS_TYPE_DIR) {

                            ((lfs_dir_t*)entry)->pos += 1;
                        }
                    }
                }
            }

            while (entry->id >= entry->metadata.count && entry->metadata.split) {

                // we split and id is on tail now
                entry->id -= entry->metadata.count;
                int err = lfs_dir_fetch(lfs, &entry->metadata, entry->metadata.tail);

                if (err) {
                    return err;
                }
            }
        }
    }

    return state;
}

int lfs_dir_orphaning_commit(lfs_t* lfs, lfs_metadata_dir_t* dir, const lfs_metadata_attribute_t* attrs, int attrcount) {

    // check for any inline files that aren't RAM backed and
    // forcefully evict them, needed for filesystem consistency

    for (lfs_file_t* entry = (lfs_file_t*)lfs->metadata_list; entry; entry = (lfs_file_t *)entry->next) {

        if (dir != &entry->metadata && lfs_pair_cmp(entry->metadata.pair, dir->pair) == 0 &&
            entry->type == LFS_TYPE_REG && (entry->flags & LFS_F_INLINE) &&
            entry->ctz.size > lfs->cfg->cache_size) {

            int err = lfs_file_outline(lfs, entry);

            if (err) {
                return err;
            }

            err = lfs_file_flush(lfs, entry);

            if (err) {
                return err;
            }
        }
    }

    lfs_block_t lpair[2] = { dir->pair[0], dir->pair[1] };
    lfs_metadata_dir_t ldir = *dir;
    lfs_metadata_dir_t pdir;

    int state = lfs_dir_relocating_commit(lfs, &ldir, dir->pair, attrs, attrcount, &pdir);

    if (state < 0) {
        return state;
    }

    // update if we're not in metadata_list, note we may have already been
    // updated if we are in metadata_list
    if (lfs_pair_cmp(dir->pair, lpair) == 0) {
        *dir = ldir;
    }

    // commit was successful, but may require other changes in the
    // filesystem, these would normally be tail recursive, but we have
    // flattened them here avoid unbounded stack usage

    // need to drop?
    if (state == LFS_OK_DROPPED) {

        // steal state
        int err = lfs_dir_getgstate(lfs, dir, &lfs->gdelta);

        if (err) {
            return err;
        }

        // steal tail, note that this can't create a recursive drop
        lpair[0] = pdir.pair[0];
        lpair[1] = pdir.pair[1];
        lfs_pair_tole64(dir->tail);

        lfs_metadata_attribute_t attr[] = {
            { LFS_MKTAG(LFS_TYPE_TAIL + dir->split, 0x3ff, sizeof(dir->tail)), dir->tail }
        };

        state = lfs_dir_relocating_commit(lfs, &pdir, lpair, attr, _countof(attr), NULL);

        lfs_pair_fromle64(dir->tail);

        if (state < 0) {

            return state;
        }

        ldir = pdir;
    }

    // need to relocate?
    bool orphans = false;
    while (state == LFS_OK_RELOCATED) {

        LFS_DEBUG("Relocating {0x%"PRIx32", 0x%"PRIx32"} "
            "-> {0x%"PRIx32", 0x%"PRIx32"}",
            lpair[0], lpair[1], ldir.pair[0], ldir.pair[1]);
        state = 0;

        // update internal root
        if (lfs_pair_cmp(lpair, lfs->root) == 0) {

            lfs->root[0] = ldir.pair[0];
            lfs->root[1] = ldir.pair[1];
        }

        // update internally tracked dirs
        for (lfs_metadata_list_t* entry = lfs->metadata_list; entry; entry = entry->next) {

            if (lfs_pair_cmp(lpair, entry->metadata.pair) == 0) {

                entry->metadata.pair[0] = ldir.pair[0];
                entry->metadata.pair[1] = ldir.pair[1];
            }

            if (entry->type == LFS_TYPE_DIR && lfs_pair_cmp(lpair, ((lfs_dir_t*)entry)->head) == 0) {

                ((lfs_dir_t*)entry)->head[0] = ldir.pair[0];
                ((lfs_dir_t*)entry)->head[1] = ldir.pair[1];
            }
        }

        // find parent
        lfs_stag_t tag = lfs_fs_parent(lfs, lpair, &pdir);

        if (tag < 0 && tag != LFS_ERR_NOENT) {

            return tag;
        }

        bool hasparent = (tag != LFS_ERR_NOENT);

        if (tag != LFS_ERR_NOENT) {

            // note that if we have a parent, we must have a pred, so this will
            // always create an orphan
            int err = lfs_fs_preporphans(lfs, +1);

            if (err) {
                return err;
            }

            // fix pending move in this pair? this looks like an optimization but
            // is in fact _required_ since relocating may outdate the move.
            uint16_t moveid = 0x3ff;
            if (lfs_gstate_hasmovehere(&lfs->gstate, pdir.pair)) {

                moveid = lfs_tag_id(lfs->gstate.tag);
                LFS_DEBUG("Fixing move while relocating "
                    "{0x%"PRIx32", 0x%"PRIx32"} 0x%"PRIx16"\n",
                    pdir.pair[0], pdir.pair[1], moveid);

                lfs_fs_prepmove(lfs, 0x3ff, NULL);

                if (moveid < lfs_tag_id(tag)) {

                    tag -= LFS_MKTAG(0, 1, 0);
                }
            }

            lfs_block_t ppair[2] = { pdir.pair[0], pdir.pair[1] };
            lfs_pair_tole64(ldir.pair);

            lfs_metadata_attribute_t attr[] = {
                { LFS_MKTAG_IF(moveid != 0x3ff, LFS_TYPE_DELETE, moveid, 0), NULL }, 
                { lfs_tag_t(tag), ldir.pair }
            };

            state = lfs_dir_relocating_commit(lfs, &pdir, ppair, attr, _countof(attr), NULL);

            lfs_pair_fromle64(ldir.pair);

            if (state < 0) {

                return state;
            }

            if (state == LFS_OK_RELOCATED) {

                lpair[0] = ppair[0];
                lpair[1] = ppair[1];
                ldir = pdir;
                orphans = true;
                continue;
            }
        }

        // find pred
        int err = lfs_fs_pred(lfs, lpair, &pdir);
        if (err && err != LFS_ERR_NOENT) {

            return err;
        }

        LFS_ASSERT(!(hasparent && err == LFS_ERR_NOENT));

        // if we can't find dir, it must be new
        if (err != LFS_ERR_NOENT) {

            if (lfs_gstate_hasorphans(&lfs->gstate)) {
                // next step, clean up orphans
                err = lfs_fs_preporphans(lfs, 0 - hasparent);

                if (err) {

                    return err;
                }
            }

            // fix pending move in this pair? this looks like an optimization
            // but is in fact _required_ since relocating may outdate the move.
            uint16_t moveid = 0x3ff;
            if (lfs_gstate_hasmovehere(&lfs->gstate, pdir.pair)) {

                moveid = lfs_tag_id(lfs->gstate.tag);
                LFS_DEBUG("Fixing move while relocating "
                    "{0x%"PRIx32", 0x%"PRIx32"} 0x%"PRIx16"\n",
                    pdir.pair[0], pdir.pair[1], moveid);

                lfs_fs_prepmove(lfs, 0x3ff, NULL);
            }

            // replace bad pair, either we clean up desync, or no desync occured
            lpair[0] = pdir.pair[0];
            lpair[1] = pdir.pair[1];
            lfs_pair_tole64(ldir.pair);

            lfs_metadata_attribute_t attr[] = {
                { LFS_MKTAG_IF(moveid != 0x3ff, LFS_TYPE_DELETE, moveid, 0), NULL },
                { LFS_MKTAG(LFS_TYPE_TAIL + pdir.split, 0x3ff, sizeof(ldir.pair)), ldir.pair }
            };

            state = lfs_dir_relocating_commit(lfs, &pdir, lpair, attr, _countof(attr), NULL);

            lfs_pair_fromle64(ldir.pair);

            if (state < 0) {

                return state;
            }

            ldir = pdir;
        }
    }

    return orphans ? LFS_OK_ORPHANED : 0;
}

int lfs_dir_commit(lfs_t* lfs, lfs_metadata_dir_t* dir, const lfs_metadata_attribute_t* attrs, int attrcount) {

    int orphans = lfs_dir_orphaning_commit(lfs, dir, attrs, attrcount);

    if (orphans < 0) {

        return orphans;
    }

    if (orphans) {

        // make sure we've removed all orphans, this is a noop if there
        // are none, but if we had nested blocks failures we may have
        // created some
        int err = lfs_fs_deorphan(lfs, false);

        if (err) {

            return err;
        }
    }

    return LFS_ERR_OK;
}