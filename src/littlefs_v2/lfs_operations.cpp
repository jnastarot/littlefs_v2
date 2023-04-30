#include "lfs.h"

/// Filesystem filesystem operations ///
int lfs_fs_rawtraverse(lfs_t* lfs, int (*cb)(void* data, lfs_block_t block), void* data, bool includeorphans) {

    // iterate over metadata pairs
    lfs_metadata_dir_t dir{};
    dir.tail[0] = 0;
    dir.tail[1] = 1;

    lfs_block_t cycle = 0;
    while (!lfs_pair_isnull(dir.tail)) {

        if (cycle >= lfs->block_count / 2) {

            // loop detected
            return LFS_ERR_CORRUPT;
        }

        cycle += 1;

        for (size_t idx = 0; idx < 2; idx++) {

            int err = cb(data, dir.tail[idx]);

            if (err) {

                return err;
            }
        }

        // iterate through ids in directory
        int err = lfs_dir_fetch(lfs, &dir, dir.tail);

        if (err) {

            return err;
        }

        for (uint16_t id = 0; id < dir.count; id++) {

            lfs_ctz_t ctz;
            lfs_stag_t tag = lfs_dir_get(lfs, &dir,
                LFS_MKTAG(LFS_TYPE_GLOBALS, 0x3ff, 0),
                LFS_MKTAG(LFS_TYPE_STRUCT, id, sizeof(ctz)), &ctz);

            if (tag < 0) {

                if (tag == LFS_ERR_NOENT) {

                    continue;
                }

                return tag;
            }

            lfs_ctz_fromle64(&ctz);

            if (lfs_tag_type3(tag) == LFS_TYPE_CTZSTRUCT) {

                err = lfs_ctz_traverse(lfs, NULL, &lfs->read_cache, ctz.head, ctz.size, cb, data);

                if (err) {

                    return err;
                }

            }
            else if (includeorphans && lfs_tag_type3(tag) == LFS_TYPE_DIRSTRUCT) {

                for (int i = 0; i < 2; i++) {

                    err = cb(data, (&ctz.head)[i]);

                    if (err) {
                        return err;
                    }
                }
            }
        }
    }

    // iterate over any open files
    for (lfs_file_t* entry = (lfs_file_t*)lfs->metadata_list; entry; entry = (lfs_file_t*)entry->next) {

        if (entry->type != LFS_TYPE_REG) {

            continue;
        }

        if ((entry->flags & LFS_F_DIRTY) && !(entry->flags & LFS_F_INLINE)) {

            int err = lfs_ctz_traverse(lfs, &entry->cache, &lfs->read_cache, entry->ctz.head, entry->ctz.size, cb, data);

            if (err) {

                return err;
            }
        }

        if ((entry->flags & LFS_F_WRITING) && !(entry->flags & LFS_F_INLINE)) {

            int err = lfs_ctz_traverse(lfs, &entry->cache, &lfs->read_cache, entry->block, entry->pos, cb, data);

            if (err) {

                return err;
            }
        }
    }

    return LFS_ERR_OK;
}

int lfs_fs_pred(lfs_t* lfs, const lfs_block_t pair[2], lfs_metadata_dir_t* pdir) {

    // iterate over all directory directory entries
    pdir->tail[0] = 0;
    pdir->tail[1] = 1;
    lfs_block_t cycle = 0;

    while (!lfs_pair_isnull(pdir->tail)) {

        if (cycle >= lfs->block_count / 2) {
            // loop detected
            return LFS_ERR_CORRUPT;
        }

        cycle += 1;

        if (lfs_pair_cmp(pdir->tail, pair) == 0) {

            return LFS_ERR_OK;
        }

        int err = lfs_dir_fetch(lfs, pdir, pdir->tail);

        if (err) {

            return err;
        }
    }

    return LFS_ERR_NOENT;
}

int lfs_fs_parent_match(void* data, lfs_tag_t tag, const void* buffer) {

    lfs_fs_parent_match_t* find = (lfs_fs_parent_match_t*)data;

    lfs_t* lfs = find->lfs;
    const lfs_disk_offset_t* disk = (const lfs_disk_offset_t*)buffer;

    (void)tag;

    lfs_block_t child[2];
    int err = lfs_bd_read(lfs,
        &lfs->write_cache, &lfs->read_cache, lfs->block_size,
        disk->block, disk->offset, &child, sizeof(child));

    if (err) {

        return err;
    }

    lfs_pair_fromle64(child);

    return (lfs_pair_cmp(child, find->pair) == 0) ? LFS_CMP_EQ : LFS_CMP_LT;
}

lfs_stag_t lfs_fs_parent(lfs_t* lfs, const lfs_block_t pair[2], lfs_metadata_dir_t* parent) {

    // use fetchmatch with callback to find pairs
    parent->tail[0] = 0;
    parent->tail[1] = 1;
    lfs_block_t cycle = 0;

    while (!lfs_pair_isnull(parent->tail)) {

        if (cycle >= lfs->block_count / 2) {
            // loop detected
            return LFS_ERR_CORRUPT;
        }

        cycle += 1;

        lfs_fs_parent_match_t _pred = { lfs, { pair[0], pair[1] } };

        lfs_stag_t tag = lfs_dir_fetchmatch(lfs, parent, parent->tail,
            LFS_MKTAG(LFS_TYPE_MOVESTATE, 0, 0x3ff),
            LFS_MKTAG(LFS_TYPE_DIRSTRUCT, 0, sizeof(lfs_block_t[2])),
            NULL,
            lfs_fs_parent_match, &_pred);

        if (tag && tag != LFS_ERR_NOENT) {

            return tag;
        }
    }

    return LFS_ERR_NOENT;
}

int lfs_fs_preporphans(lfs_t* lfs, int8_t orphans) {

    LFS_ASSERT(lfs_tag_size(lfs->gstate.tag) > 0 || orphans >= 0);
    lfs->gstate.tag += orphans;
    lfs->gstate.tag = ((lfs->gstate.tag & ~LFS_MKTAG(LFS_TYPE_HAS_ORPHANS, 0, 0)) |
        ((uint32_t)lfs_gstate_hasorphans(&lfs->gstate) << 31));

    return LFS_ERR_OK;
}

void lfs_fs_prepmove(lfs_t* lfs, uint16_t id, const lfs_block_t pair[2]) {

    lfs->gstate.tag = ((lfs->gstate.tag & ~LFS_MKTAG(LFS_TYPE_MOVESTATE, 0x3ff, 0)) |
        ((id != 0x3ff) ? LFS_MKTAG(LFS_TYPE_DELETE, id, 0) : 0));

    lfs->gstate.pair[0] = (id != 0x3ff) ? pair[0] : 0;
    lfs->gstate.pair[1] = (id != 0x3ff) ? pair[1] : 0;
}

int lfs_fs_demove(lfs_t* lfs) {

    if (!lfs_gstate_hasmove(&lfs->gdisk)) {

        return LFS_ERR_OK;
    }

    // Fix bad moves
    LFS_DEBUG("Fixing move {0x%"PRIx32", 0x%"PRIx32"} 0x%"PRIx16,
        lfs->gdisk.pair[0],
        lfs->gdisk.pair[1],
        lfs_tag_id(lfs->gdisk.tag));

    // fetch and delete the moved entry
    lfs_metadata_dir_t movedir;

    int err = lfs_dir_fetch(lfs, &movedir, lfs->gdisk.pair);

    if (err) {
        return err;
    }

    // prep gstate and delete move id
    uint16_t moveid = lfs_tag_id(lfs->gdisk.tag);
    lfs_fs_prepmove(lfs, 0x3ff, NULL);

    lfs_metadata_attribute_t attr[] = {
        { LFS_MKTAG(LFS_TYPE_DELETE, moveid, 0), NULL }
    };

    err = lfs_dir_commit(lfs, &movedir, attr, _countof(attr));

    if (err) {
        return err;
    }

    return LFS_ERR_OK;
}

int lfs_fs_deorphan(lfs_t* lfs, bool powerloss) {

    if (!lfs_gstate_hasorphans(&lfs->gstate)) {

        return LFS_ERR_OK;
    }

    int8_t found = 0;
restart:

    // Check for orphans in two separate passes:
    // - 1 for half-orphans (relocations)
    // - 2 for full-orphans (removes/renames)
    //
    // Two separate passes are needed as half-orphans can contain outdated
    // references to full-orphans, effectively hiding them from the deorphan
    // search.
    //
    for (int pass = 0; pass < 2; pass++) {
        // Fix any orphans
        lfs_metadata_dir_t pdir{};
        pdir.split = true;
        pdir.tail[0] = 0;
        pdir.tail[1] = 1;

        lfs_metadata_dir_t dir;

        // iterate over all directory directory entries
        while (!lfs_pair_isnull(pdir.tail)) {

            int err = lfs_dir_fetch(lfs, &dir, pdir.tail);

            if (err) {

                return err;
            }

            // check head blocks for orphans
            if (!pdir.split) {

                // check if we have a parent
                lfs_metadata_dir_t parent;
                lfs_stag_t tag = lfs_fs_parent(lfs, pdir.tail, &parent);

                if (tag < 0 && tag != LFS_ERR_NOENT) {

                    return tag;
                }

                if (pass == 0 && tag != LFS_ERR_NOENT) {

                    lfs_block_t pair[2];
                    lfs_stag_t state = lfs_dir_get(lfs, &parent, LFS_MKTAG(LFS_TYPE_MOVESTATE, 0x3ff, 0), tag, pair);

                    if (state < 0) {
                        return state;
                    }

                    lfs_pair_fromle64(pair);

                    if (!lfs_pair_sync(pair, pdir.tail)) {
                        // we have desynced
                        LFS_DEBUG("Fixing half-orphan {0x%"PRIx32", 0x%"PRIx32"} -> {0x%"PRIx32", 0x%"PRIx32"}", pdir.tail[0], pdir.tail[1], pair[0], pair[1]);

                        // fix pending move in this pair? this looks like an
                        // optimization but is in fact _required_ since
                        // relocating may outdate the move.
                        uint16_t moveid = 0x3ff;
                        if (lfs_gstate_hasmovehere(&lfs->gstate, pdir.pair)) {

                            moveid = lfs_tag_id(lfs->gstate.tag);
                            
                            LFS_DEBUG("Fixing move while fixing orphans {0x%"PRIx32", 0x%"PRIx32"} 0x%"PRIx16"\n", pdir.pair[0], pdir.pair[1], moveid); lfs_fs_prepmove(lfs, 0x3ff, NULL);
                        }

                        lfs_pair_tole64(pair);

                        lfs_metadata_attribute_t attr[] = {
                            { LFS_MKTAG_IF(moveid != 0x3ff, LFS_TYPE_DELETE, moveid, 0), NULL },
                            { LFS_MKTAG(LFS_TYPE_SOFTTAIL, 0x3ff, sizeof(pair)), pair }
                        };

                        state = lfs_dir_orphaning_commit(lfs, &pdir, attr, _countof(attr));

                        lfs_pair_fromle64(pair);
                        if (state < 0) {
                            return state;
                        }

                        found += 1;

                        // did our commit create more orphans?
                        if (state == LFS_OK_ORPHANED) {
                            goto restart;
                        }

                        // refetch tail
                        continue;
                    }
                }

                // note we only check for full orphans if we may have had a
                // power-loss, otherwise orphans are created intentionally
                // during operations such as lfs_mkdir
                if (pass == 1 && tag == LFS_ERR_NOENT && powerloss) {

                    // we are an orphan
                    LFS_DEBUG("Fixing orphan {0x%"PRIx32", 0x%"PRIx32"}", pdir.tail[0], pdir.tail[1]);

                    // steal state
                    err = lfs_dir_getgstate(lfs, &dir, &lfs->gdelta);
                    if (err) {

                        return err;
                    }

                    // steal tail
                    lfs_pair_tole64(dir.tail);

                    lfs_metadata_attribute_t attr[] = {
                        { LFS_MKTAG(LFS_TYPE_TAIL + dir.split, 0x3ff, sizeof(dir.tail)), dir.tail }
                    };

                    int state = lfs_dir_orphaning_commit(lfs, &pdir, attr, _countof(attr));

                    lfs_pair_fromle64(dir.tail);

                    if (state < 0) {
                        return state;
                    }

                    found += 1;

                    // did our commit create more orphans?
                    if (state == LFS_OK_ORPHANED) {
                        goto restart;
                    }

                    // refetch tail
                    continue;
                }

            }

            pdir = dir;
        }
    }

    // mark orphans as fixed
    return lfs_fs_preporphans(lfs, 0 - lfs_min(lfs_gstate_getorphans(&lfs->gstate), found));
}

int lfs_fs_forceconsistency(lfs_t* lfs) {

    int err = lfs_fs_demove(lfs);

    if (err) {
        return err;
    }

    err = lfs_fs_deorphan(lfs, true);

    if (err) {
        return err;
    }

    return LFS_ERR_OK;
}

int lfs_fs_size_count(void* p, lfs_block_t block) {

    (void)block;
    lfs_size_t* size = (lfs_size_t*)p;
    *size += 1;

    return LFS_ERR_OK;
}

lfs_ssize_t lfs_fs_rawsize(lfs_t* lfs) {

    lfs_size_t size = 0;
    int err = lfs_fs_rawtraverse(lfs, lfs_fs_size_count, &size, false);

    if (err) {

        return err;
    }

    return size;
}


int lfs_fs_rawstat(lfs_t* lfs, struct lfs_fsinfo* fsinfo) {

    lfs_ssize_t usage = lfs_fs_rawsize(lfs);

    if (usage < 0) {

        return usage;
    }

    fsinfo->block_size = lfs->block_size;
    fsinfo->block_count = lfs->block_count;
    fsinfo->block_usage = usage;
    fsinfo->name_max = lfs->name_max_length;
    fsinfo->file_max = lfs->file_max_size;
    fsinfo->attr_max = lfs->attr_max_size;

    return LFS_ERR_OK;
}

int lfs_fs_rawgrow(lfs_t* lfs, lfs_size_t block_count) {

    // shrinking is not supported
    LFS_ASSERT(block_count >= lfs->block_count);

    if (block_count > lfs->block_count) {
        lfs->block_count = block_count;
        lfs->cfg->block_count = block_count;
        // fetch the root
        lfs_metadata_dir_t root;
        int err = lfs_dir_fetch(lfs, &root, lfs->root);

        if (err) {

            return err;
        }

        // update the superblock
        lfs_superblock_t superblock;
        lfs_stag_t tag = lfs_dir_get(lfs, &root, LFS_MKTAG(LFS_TYPE_MOVESTATE, 0x3ff, 0),
            LFS_MKTAG(LFS_TYPE_INLINESTRUCT, 0, sizeof(superblock)), &superblock);

        if (tag < 0) {

            return tag;
        }

        lfs_superblock_fromle64(&superblock);

        superblock.block_count = lfs->block_count;
        
        lfs_superblock_tole64(&superblock);

        lfs_metadata_attribute_t attr[] = {
            { tag, &superblock }
        };

        err = lfs_dir_commit(lfs, &root, attr, _countof(attr));

        if (err) {
            return err;
        }
    }

    return LFS_ERR_OK;
}