#include "lfs.h"

/// Filesystem operations ///
static int lfs_init(lfs_t* lfs, lfs_config_t* cfg);
static int lfs_deinit(lfs_t* lfs);

static int lfs_init(lfs_t* lfs, lfs_config_t* cfg) {

    lfs->cfg = cfg;
    int err = 0;

    // validate that the lfs-cfg sizes were initiated properly before
    // performing any arithmetic logics with them
    LFS_ASSERT(lfs->cfg->read_size != 0);
    LFS_ASSERT(lfs->cfg->write_size != 0);
    LFS_ASSERT(lfs->cfg->cache_size != 0);
    LFS_ASSERT(lfs->cfg->erase_size != 0 || lfs->cfg->block_size != 0);

    // check that block size is a multiple of cache size is a multiple
    // of prog and read sizes
    LFS_ASSERT(lfs->cfg->cache_size % lfs->cfg->read_size == 0);
    LFS_ASSERT(lfs->cfg->cache_size % lfs->cfg->write_size == 0);

    // setup erase_size, this can be zero for backwards compatibility
    lfs->erase_size = lfs->cfg->erase_size;
    if (!lfs->erase_size) {
        lfs->erase_size = lfs->cfg->block_size;
    }

    // check that block_size is a multiple of erase_size is a mulitiple of
    // cache_size, this implies everything is a multiple of read_size and
    // prog_size
    LFS_ASSERT(lfs->erase_size % lfs->cfg->cache_size == 0);

    if (lfs->cfg->block_size) {

        LFS_ASSERT(lfs->cfg->block_size % lfs->erase_size == 0);
    }

    // block_cycles = 0 is no longer supported.
    //
    // block_cycles is the number of erase cycles before littlefs evicts
    // metadata logs as a part of wear leveling. Suggested values are in the
    // range of 100-1000, or set block_cycles to -1 to disable block-level
    // wear-leveling.
    LFS_ASSERT(lfs->cfg->block_cycles != 0);


    // setup read cache
    if (lfs->cfg->read_buffer) {

        lfs->read_cache.buffer = (uint8_t*)lfs->cfg->read_buffer;

    }
    else {

        lfs->read_cache.buffer = (uint8_t*)malloc(lfs->cfg->cache_size);

        if (!lfs->read_cache.buffer) {

            err = LFS_ERR_NOMEM;
            goto cleanup;
        }
    }

    // setup program cache
    if (lfs->cfg->write_buffer) {

        lfs->write_cache.buffer = (uint8_t*)lfs->cfg->write_buffer;

    }
    else {

        lfs->write_cache.buffer = (uint8_t*)malloc(lfs->cfg->cache_size);
        if (!lfs->write_cache.buffer) {

            err = LFS_ERR_NOMEM;
            goto cleanup;
        }
    }

    // zero to avoid information leaks
    lfs_cache_zero(lfs, &lfs->read_cache);
    lfs_cache_zero(lfs, &lfs->write_cache);

    // setup lookahead, must be multiple of 64-bits, 32-bit aligned
    LFS_ASSERT(lfs->cfg->lookahead_size > 0);
    LFS_ASSERT(lfs->cfg->lookahead_size % 8 == 0 &&
        (uintptr_t)lfs->cfg->lookahead_buffer % 4 == 0);

    if (lfs->cfg->lookahead_buffer) {

        lfs->free.buffer = (uint64_t*)lfs->cfg->lookahead_buffer;

    }
    else {

        lfs->free.buffer = (uint64_t*)malloc(lfs->cfg->lookahead_size);

        if (!lfs->free.buffer) {

            err = LFS_ERR_NOMEM;
            goto cleanup;
        }
    }

    // check that the size limits are sane
    LFS_ASSERT(lfs->cfg->name_max_length <= LFS_NAME_MAX);
    lfs->name_max_length = lfs->cfg->name_max_length;
    if (!lfs->name_max_length) {

        lfs->name_max_length = LFS_NAME_MAX;
    }

    LFS_ASSERT(lfs->cfg->file_max_size <= LFS_FILE_MAX);
    lfs->file_max_size = lfs->cfg->file_max_size;
    if (!lfs->file_max_size) {

        lfs->file_max_size = LFS_FILE_MAX;
    }

    LFS_ASSERT(lfs->cfg->attr_max_size <= LFS_ATTR_MAX);
    lfs->attr_max_size = lfs->cfg->attr_max_size;
    if (!lfs->attr_max_size) {

        lfs->attr_max_size = LFS_ATTR_MAX;
    }

    LFS_ASSERT(lfs->cfg->metadata_max <= lfs->cfg->block_size);

    // setup default state
    lfs->root[0] = LFS_BLOCK_NULL;
    lfs->root[1] = LFS_BLOCK_NULL;
    lfs->metadata_list = NULL;
    lfs->seed = 0;
    lfs->gdisk = { 0 };
    lfs->gstate = { 0 };
    lfs->gdelta = { 0 };

    return LFS_ERR_OK;

cleanup:
    lfs_deinit(lfs);
    return err;
}

static int lfs_deinit(lfs_t* lfs) {

    // free allocated memory
    if (!lfs->cfg->read_buffer) {

        free(lfs->read_cache.buffer);
    }

    if (!lfs->cfg->write_buffer) {

        free(lfs->write_cache.buffer);
    }

    if (!lfs->cfg->lookahead_buffer) {

        free(lfs->free.buffer);
    }

    return LFS_ERR_OK;
}

/// General fs operations ///
int lfs_raw_stat(lfs_t* lfs, const char* path, struct lfs_info* info) {

    lfs_metadata_dir_t cwd;
    lfs_stag_t tag = lfs_dir_find(lfs, &cwd, &path, NULL);

    if (tag < 0) {

        return (int)tag;
    }

    return lfs_dir_getinfo(lfs, &cwd, lfs_tag_id(tag), info);
}

int lfs_raw_remove(lfs_t* lfs, const char* path) {

    // deorphan if we haven't yet, needed at most once after poweron
    int err = lfs_fs_forceconsistency(lfs);

    if (err) {
        return err;
    }

    lfs_metadata_dir_t cwd;
    lfs_stag_t tag = lfs_dir_find(lfs, &cwd, &path, NULL);

    if (tag < 0 || lfs_tag_id(tag) == 0x3ff) {

        return (tag < 0) ? (int)tag : LFS_ERR_INVAL;
    }

    lfs_metadata_list_t dir;
    dir.next = lfs->metadata_list;
    if (lfs_tag_type3(tag) == LFS_TYPE_DIR) {

        // must be empty before removal
        lfs_block_t pair[2];
        lfs_stag_t res = lfs_dir_get(lfs, &cwd,
            LFS_MKTAG(LFS_TYPE_GLOBALS, 0x3ff, 0),
            LFS_MKTAG(LFS_TYPE_STRUCT, lfs_tag_id(tag), sizeof(pair)), pair);

        if (res < 0) {

            return (int)res;
        }

        lfs_pair_fromle64(pair);

        err = lfs_dir_fetch(lfs, &dir.metadata, pair);

        if (err) {

            return err;
        }

        if (dir.metadata.count > 0 || dir.metadata.split) {

            return LFS_ERR_NOTEMPTY;
        }

        // mark fs as orphaned
        err = lfs_fs_preporphans(lfs, +1);

        if (err) {

            return err;
        }

        // I know it's crazy but yes, dir can be changed by our parent's
        // commit (if predecessor is child)
        dir.type = 0;
        dir.id = 0;
        lfs->metadata_list = &dir;
    }

    // delete the entry
    lfs_metadata_attribute_t attr[] = {
        { LFS_MKTAG(LFS_TYPE_DELETE, lfs_tag_id(tag), 0), NULL }
    };

    err = lfs_dir_commit(lfs, &cwd, attr, _countof(attr));

    if (err) {
        lfs->metadata_list = dir.next;
        return err;
    }

    lfs->metadata_list = dir.next;
    if (lfs_tag_type3(tag) == LFS_TYPE_DIR) {

        // fix orphan
        err = lfs_fs_preporphans(lfs, -1);
        if (err) {
            return err;
        }

        err = lfs_fs_pred(lfs, dir.metadata.pair, &cwd);
        if (err) {
            return err;
        }

        err = lfs_dir_drop(lfs, &cwd, &dir.metadata);
        if (err) {
            return err;
        }
    }

    return LFS_ERR_OK;
}

int lfs_raw_rename(lfs_t* lfs, const char* oldpath, const char* newpath) {

    // deorphan if we haven't yet, needed at most once after poweron
    int err = lfs_fs_forceconsistency(lfs);
    if (err) {

        return err;
    }

    // find old entry
    lfs_metadata_dir_t oldcwd;
    lfs_stag_t oldtag = lfs_dir_find(lfs, &oldcwd, &oldpath, NULL);

    if (oldtag < 0 || lfs_tag_id(oldtag) == 0x3ff) {

        return (oldtag < 0) ? (int)oldtag : LFS_ERR_INVAL;
    }

    // find new entry
    lfs_metadata_dir_t newcwd;
    uint16_t newid;
    lfs_stag_t prevtag = lfs_dir_find(lfs, &newcwd, &newpath, &newid);

    if ((prevtag < 0 || lfs_tag_id(prevtag) == 0x3ff) &&
        !(prevtag == LFS_ERR_NOENT && newid != 0x3ff)) {

        return (prevtag < 0) ? (int)prevtag : LFS_ERR_INVAL;
    }

    // if we're in the same pair there's a few special cases...
    bool samepair = (lfs_pair_cmp(oldcwd.pair, newcwd.pair) == 0);
    uint16_t newoldid = lfs_tag_id(oldtag);

    lfs_metadata_list_t prevdir;
    prevdir.next = lfs->metadata_list;

    if (prevtag == LFS_ERR_NOENT) {

        // check that name fits
        lfs_size_t nlen = strlen(newpath);
        if (nlen > lfs->name_max_length) {
            return LFS_ERR_NAMETOOLONG;
        }

        // there is a small chance we are being renamed in the same
        // directory/ to an id less than our old id, the global update
        // to handle this is a bit messy
        if (samepair && newid <= newoldid) {
            newoldid += 1;
        }

    }
    else if (lfs_tag_type3(prevtag) != lfs_tag_type3(oldtag)) {

        return LFS_ERR_ISDIR;

    }
    else if (samepair && newid == newoldid) {

        // we're renaming to ourselves??
        return LFS_ERR_OK;

    }
    else if (lfs_tag_type3(prevtag) == LFS_TYPE_DIR) {

        // must be empty before removal
        lfs_block_t prevpair[2];
        lfs_stag_t res = lfs_dir_get(lfs, &newcwd,
            LFS_MKTAG(LFS_TYPE_GLOBALS, 0x3ff, 0),
            LFS_MKTAG(LFS_TYPE_STRUCT, newid, sizeof(prevpair)), prevpair);

        if (res < 0) {
            return (int)res;
        }

        lfs_pair_fromle64(prevpair);

        // must be empty before removal
        err = lfs_dir_fetch(lfs, &prevdir.metadata, prevpair);
        if (err) {
            return err;
        }

        if (prevdir.metadata.count > 0 || prevdir.metadata.split) {
            return LFS_ERR_NOTEMPTY;
        }

        // mark fs as orphaned
        err = lfs_fs_preporphans(lfs, +1);
        if (err) {
            return err;
        }

        // I know it's crazy but yes, dir can be changed by our parent's
        // commit (if predecessor is child)
        prevdir.type = 0;
        prevdir.id = 0;
        lfs->metadata_list = &prevdir;
    }

    if (!samepair) {

        lfs_fs_prepmove(lfs, newoldid, oldcwd.pair);
    }

    // move over all attributes
    {
        lfs_metadata_attribute_t attr[] = {
            { LFS_MKTAG_IF(prevtag != LFS_ERR_NOENT, LFS_TYPE_DELETE, newid, 0), NULL },
            { LFS_MKTAG(LFS_TYPE_CREATE, newid, 0), NULL },
            { LFS_MKTAG(lfs_tag_type3(oldtag), newid, strlen(newpath)), newpath },
            { LFS_MKTAG(LFS_FROM_MOVE, newid, lfs_tag_id(oldtag)), &oldcwd },
            { LFS_MKTAG_IF(samepair, LFS_TYPE_DELETE, newoldid, 0), NULL }
        };

        err = lfs_dir_commit(lfs, &newcwd, attr, _countof(attr));
    }

    if (err) {

        lfs->metadata_list = prevdir.next;
        return err;
    }

    // let commit clean up after move (if we're different! otherwise move
    // logic already fixed it for us)
    if (!samepair && lfs_gstate_hasmove(&lfs->gstate)) {

        // prep gstate and delete move id
        lfs_fs_prepmove(lfs, 0x3ff, NULL);

        lfs_metadata_attribute_t attr[] = {
            { LFS_MKTAG(LFS_TYPE_DELETE, lfs_tag_id(oldtag), 0), NULL }
        };

        err = lfs_dir_commit(lfs, &oldcwd, attr, _countof(attr));

        if (err) {

            lfs->metadata_list = prevdir.next;
            return err;
        }
    }

    lfs->metadata_list = prevdir.next;
    if (prevtag != LFS_ERR_NOENT && lfs_tag_type3(prevtag) == LFS_TYPE_DIR) {

        // fix orphan

        err = lfs_fs_preporphans(lfs, -1);
        if (err) {

            return err;
        }

        err = lfs_fs_pred(lfs, prevdir.metadata.pair, &newcwd);
        if (err) {

            return err;
        }

        err = lfs_dir_drop(lfs, &newcwd, &prevdir.metadata);
        if (err) {

            return err;
        }
    }

    return LFS_ERR_OK;
}

lfs_ssize_t lfs_raw_get_attribute(lfs_t* lfs, const char* path, uint8_t type, void* buffer, lfs_size_t size) {

    lfs_metadata_dir_t cwd;

    lfs_stag_t tag = lfs_dir_find(lfs, &cwd, &path, NULL);

    if (tag < 0) {

        return tag;
    }

    uint16_t id = lfs_tag_id(tag);
    if (id == 0x3ff) {
        // special case for root
        id = 0;
        int err = lfs_dir_fetch(lfs, &cwd, lfs->root);
        if (err) {
            return err;
        }
    }

    tag = lfs_dir_get(lfs, &cwd,
        LFS_MKTAG(LFS_TYPE_MOVESTATE, 0x3ff, 0),
        LFS_MKTAG(LFS_TYPE_USERATTR + type, id, lfs_min(size, lfs->attr_max_size)), buffer);

    if (tag < 0) {

        if (tag == LFS_ERR_NOENT) {
            return LFS_ERR_NOATTR;
        }

        return tag;
    }

    return lfs_tag_size(tag);
}

int lfs_commit_attribute(lfs_t* lfs, const char* path, uint8_t type, const void* buffer, lfs_size_t size) {

    lfs_metadata_dir_t cwd;
    lfs_stag_t tag = lfs_dir_find(lfs, &cwd, &path, NULL);

    if (tag < 0) {

        return tag;
    }

    uint16_t id = lfs_tag_id(tag);

    if (id == 0x3ff) {

        // special case for root
        id = 0;

        int err = lfs_dir_fetch(lfs, &cwd, lfs->root);

        if (err) {
            return err;
        }
    }

    lfs_metadata_attribute_t attr[] = {
        { LFS_MKTAG(LFS_TYPE_USERATTR + type, id, size), buffer }
    };

    return lfs_dir_commit(lfs, &cwd, attr, _countof(attr));
}

int lfs_raw_set_attribute(lfs_t* lfs, const char* path, uint8_t type, const void* buffer, lfs_size_t size) {

    if (size > lfs->attr_max_size) {

        return LFS_ERR_NOSPC;
    }

    return lfs_commit_attribute(lfs, path, type, buffer, size);
}

int lfs_raw_remove_attribute(lfs_t* lfs, const char* path, uint8_t type) {

    return lfs_commit_attribute(lfs, path, type, NULL, 0x3ff);
}

int lfs_raw_format(lfs_t* lfs, lfs_config_t* cfg) {

    int err = 0;

    {
        err = lfs_init(lfs, cfg);

        if (err) {
            return err;
        }

        // if block_size not specified, assume equal to erase blocks
        lfs->block_size = lfs->cfg->block_size;

        if (!lfs->block_size) {
            lfs->block_size = lfs->erase_size;
        }

        LFS_ASSERT(lfs->cfg->block_count != 0);

        lfs->block_count = lfs->cfg->block_count;

        // check that the block size is large enough to fit ctz pointers
        LFS_ASSERT(sizeof(uint64_t) * lfs_npw2_64(0xffffffff / (lfs->block_size - 2 * sizeof(uint64_t))) <= lfs->block_size);

        // create free lookahead
        memset(lfs->free.buffer, 0, lfs->cfg->lookahead_size);
        lfs->free.offset = 0;
        lfs->free.size = lfs_min(sizeof(lfs_block_t[2]) * lfs->cfg->lookahead_size, lfs->block_count); //recheck

        lfs->free.i = 0;
        lfs_alloc_ack(lfs);

        // create root dir
        lfs_metadata_dir_t root;
        err = lfs_dir_alloc(lfs, &root);

        if (err) {

            goto cleanup;
        }

        // write one superblock
        lfs_superblock_t superblock{};
        superblock.version = LFS_DISK_VERSION;
        superblock.block_size = lfs->block_size;
        superblock.block_count = lfs->block_count;
        superblock.name_max_length = lfs->name_max_length;
        superblock.file_max_size = lfs->file_max_size;
        superblock.attr_max_size = lfs->attr_max_size;

        lfs_superblock_tole64(&superblock);

        lfs_metadata_attribute_t attr[] = {
            { LFS_MKTAG(LFS_TYPE_CREATE, 0, 0), NULL },
            { LFS_MKTAG(LFS_TYPE_SUPERBLOCK, 0, 8), "littlefs" },
            { LFS_MKTAG(LFS_TYPE_INLINESTRUCT, 0, sizeof(superblock)), &superblock }
        };

        err = lfs_dir_commit(lfs, &root, attr, _countof(attr));

        if (err) {

            goto cleanup;
        }

        // force compaction to prevent accidentally mounting any
        // older version of littlefs that may live on disk
        root.erased = false;
        err = lfs_dir_commit(lfs, &root, NULL, 0);

        if (err) {

            goto cleanup;
        }

        // sanity check that fetch works
        const lfs_block_t pair[2] = { 0, 1 };

        err = lfs_dir_fetch(lfs, &root, pair);

        if (err) {

            goto cleanup;
        }
    }

cleanup:
    lfs_deinit(lfs);
    return err;

}

int lfs_raw_mount(lfs_t* lfs, lfs_config_t* cfg) {

    int err = lfs_init(lfs, cfg);

    if (err) {
        return err;
    }

    // if block_size is unknown we need to search for it
    lfs->block_size = lfs->cfg->block_size;
    lfs_size_t block_size_limit = lfs->cfg->block_size;

    if (!lfs->block_size) {

        lfs->block_size = lfs->erase_size;

        // make sure this doesn't overflow
        if (!lfs->cfg->block_count || 
            (lfs->cfg->block_count / 2 > ((lfs_size_t)-1) / lfs->erase_size)) {

            block_size_limit = ((lfs_size_t)-1);
        }
        else {

            block_size_limit = (lfs->cfg->block_count / 2) * lfs->erase_size;
        }
    }

    // search for the correct block_size
    while (true) {

        // setup block_size/count so underlying operations work
        lfs->block_count = lfs->cfg->block_count;

        if (!lfs->block_count) {

            lfs->block_count = (lfs_size_t)-1;
        }
        else if (!lfs->cfg->block_size) {

            lfs->block_count /= lfs->block_size / lfs->erase_size;
        }

        // make sure cached data from a different block_size doesn't cause
        // problems, we should never visit the same mdir twice here anyways
        lfs_cache_drop(lfs, &lfs->read_cache);

        // scan directory blocks for superblock and any global updates
        lfs_metadata_dir_t dir{};
        dir.tail[0] = 0;
        dir.tail[1] = 1;

        lfs_block_t cycle = 0;

        while (!lfs_pair_isnull(dir.tail)) {

            if (cycle >= lfs->block_count / 2) {
                // loop detected
                err = LFS_ERR_CORRUPT;
                goto cleanup;
            }
            cycle += 1;

            lfs_dir_find_match_t _pred = { lfs, "littlefs", 8 };
            // fetch next block in tail list
            lfs_stag_t tag = lfs_dir_fetchmatch(lfs, &dir, dir.tail,
                LFS_MKTAG(LFS_TYPE_MOVESTATE, 0x3ff, 0),
                LFS_MKTAG(LFS_TYPE_SUPERBLOCK, 0, 8),
                NULL,
                lfs_dir_find_match, &_pred);

            if (tag < 0) {
                if (tag == LFS_ERR_CORRUPT) {
                    // maybe our block_size is wrong
                    goto next_block_size;
                }
                err = tag;
                goto cleanup;
            }

            // has superblock?
            if (tag && !lfs_tag_isdelete(tag)) {

                // grab superblock
                lfs_superblock_t superblock;
                tag = lfs_dir_get(lfs, &dir,
                    LFS_MKTAG(LFS_TYPE_MOVESTATE, 0x3ff, 0),
                    LFS_MKTAG(LFS_TYPE_INLINESTRUCT, 0, sizeof(superblock)), &superblock);

                if (tag < 0) {

                    if (tag == LFS_ERR_CORRUPT) {
                        // maybe our block_size is wrong
                        goto next_block_size;
                    }
                    err = tag;
                    goto cleanup;
                }

                lfs_superblock_fromle64(&superblock);

                // we may not be done, first check the stored block_size, it
                // it's different we need to remount in case we found an
                // outdated superblock
                if (superblock.block_size != lfs->block_size) {

                    if (lfs->cfg->block_size
                        || superblock.block_size % lfs->erase_size != 0
                        || superblock.block_size < lfs->block_size) {
                        LFS_ERROR("Invalid block size %"PRIu32,
                            superblock.block_size);
                        err = LFS_ERR_INVAL;
                        goto cleanup;
                    }

                    // remount with correct block_size
                    lfs->block_size = superblock.block_size;
                    goto next_mount;
                }

                if (superblock.block_count != lfs->block_count) {

                    if (lfs->cfg->block_count
                        || superblock.block_count > lfs->block_count) {

                        LFS_ERROR("Invalid block count %"PRIu32, superblock.block_count);
                        err = LFS_ERR_INVAL;
                        goto cleanup;
                    }

                    lfs->block_count = superblock.block_count;
                }

                // check version
                uint16_t major_version = (0xffff & (superblock.version >> 16));
                uint16_t minor_version = (0xffff & (superblock.version >> 0));

                if ((major_version != LFS_DISK_VERSION_MAJOR ||
                    minor_version > LFS_DISK_VERSION_MINOR)) {

                    LFS_ERROR("Invalid version v%"PRIu16".%"PRIu16, major_version, minor_version);
                    err = LFS_ERR_INVAL;
                    goto cleanup;
                }

                // check superblock configuration
                if (superblock.name_max_length) {

                    if (superblock.name_max_length > lfs->name_max_length) {

                        LFS_ERROR("Unsupported name_max %"PRIu32, superblock.name_max);
                        err = LFS_ERR_INVAL;
                        goto cleanup;
                    }

                    lfs->name_max_length = superblock.name_max_length;
                }

                if (superblock.file_max_size) {
                    if (superblock.file_max_size > lfs->file_max_size) {

                        LFS_ERROR("Unsupported file_max %"PRIu32, superblock.file_max);
                        err = LFS_ERR_INVAL;
                        goto cleanup;
                    }

                    lfs->file_max_size = superblock.file_max_size;
                }

                if (superblock.attr_max_size) {
                    if (superblock.attr_max_size > lfs->attr_max_size) {

                        LFS_ERROR("Unsupported attr_max %"PRIu32, superblock.attr_max);
                        err = LFS_ERR_INVAL;
                        goto cleanup;
                    }

                    lfs->attr_max_size = superblock.attr_max_size;
                }

                // update root
                lfs->root[0] = dir.pair[0];
                lfs->root[1] = dir.pair[1];
            }

            // has gstate?
            err = lfs_dir_getgstate(lfs, &dir, &lfs->gstate);
            if (err) {
                goto cleanup;
            }

            // we found a valid superblock, set block_size_limit so block_size
            // will no longer change
            block_size_limit = lfs->block_size;
        }

        break;

    next_block_size:
        lfs->block_size += lfs->erase_size;
        if (lfs->block_size > block_size_limit) {
            err = LFS_ERR_INVAL;
            goto cleanup;
        }

        // if block_count is non-zero, skip block_sizes that aren't a factor,
        // this brings our search down from O(n) to O(d(n)), O(log(n))
        // on average, and O(log(n)) for powers of 2
        if (lfs->cfg->block_count && lfs->cfg->block_count
            % (lfs->block_size / lfs->erase_size) != 0) {

            goto next_block_size;
        }

    next_mount:;
    }

    // found superblock?
    if (lfs_pair_isnull(lfs->root)) {
        err = LFS_ERR_INVAL;
        goto cleanup;
    }

    // update littlefs with gstate
    if (!lfs_gstate_iszero(&lfs->gstate)) {

        LFS_DEBUG("Found pending gstate 0x%08"PRIx32"%08"PRIx32"%08"PRIx32,
            lfs->gstate.tag,
            lfs->gstate.pair[0],
            lfs->gstate.pair[1]);
    }

    lfs->gstate.tag += !lfs_tag_isvalid(lfs->gstate.tag);
    lfs->gdisk = lfs->gstate;

    // setup free lookahead, to distribute allocations uniformly across
    // boots, we start the allocator at a random location
    lfs->free.offset = lfs->seed % lfs->block_count;
    lfs_alloc_drop(lfs);

    return LFS_ERR_OK;

cleanup:
    lfs_raw_unmount(lfs);
    return err;
}

int lfs_raw_unmount(lfs_t* lfs) {
    return lfs_deinit(lfs);
}


