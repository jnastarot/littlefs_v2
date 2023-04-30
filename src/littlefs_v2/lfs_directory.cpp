#include "lfs.h"


/// Top level directory operations ///
int lfs_dir_rawcreate(lfs_t* lfs, const char* path) {

    // deorphan if we haven't yet, needed at most once after poweron
    int err = lfs_fs_forceconsistency(lfs);

    if (err) {

        return err;
    }

    lfs_metadata_list_t cwd;
    cwd.next = lfs->metadata_list;
    uint16_t id;
    err = lfs_dir_find(lfs, &cwd.metadata, &path, &id);

    if (!(err == LFS_ERR_NOENT && id != 0x3ff)) {

        return (err < 0) ? err : LFS_ERR_EXIST;
    }

    // check that name fits
    lfs_size_t nlen = strlen(path);
    if (nlen > lfs->name_max_length) {

        return LFS_ERR_NAMETOOLONG;
    }

    // build up new directory
    lfs_alloc_ack(lfs);
    lfs_metadata_dir_t dir;
    err = lfs_dir_alloc(lfs, &dir);

    if (err) {

        return err;
    }

    // find end of list
    lfs_metadata_dir_t pred = cwd.metadata;
    while (pred.split) {

        err = lfs_dir_fetch(lfs, &pred, pred.tail);

        if (err) {

            return err;
        }
    }

    // setup dir
    lfs_pair_tole64(pred.tail);

    lfs_metadata_attribute_t attr[] = {
        { LFS_MKTAG(LFS_TYPE_SOFTTAIL, 0x3ff, sizeof(pred.tail)), pred.tail }
    };

    err = lfs_dir_commit(lfs, &dir, attr, _countof(attr));

    lfs_pair_fromle64(pred.tail);

    if (err) {

        return err;
    }

    // current block not end of list?
    if (cwd.metadata.split) {

        // update tails, this creates a desync
        err = lfs_fs_preporphans(lfs, +1);
        if (err) {

            return err;
        }

        // it's possible our predecessor has to be relocated, and if
        // our parent is our predecessor's predecessor, this could have
        // caused our parent to go out of date, fortunately we can hook
        // ourselves into littlefs to catch this
        cwd.type = 0;
        cwd.id = 0;
        lfs->metadata_list = &cwd;

        lfs_pair_tole64(dir.pair);

        lfs_metadata_attribute_t attr[] = {
            { LFS_MKTAG(LFS_TYPE_SOFTTAIL, 0x3ff, sizeof(dir.pair)), dir.pair }
        };

        err = lfs_dir_commit(lfs, &pred, attr, _countof(attr));

        lfs_pair_fromle64(dir.pair);

        if (err) {

            lfs->metadata_list = cwd.next;
            return err;
        }

        lfs->metadata_list = cwd.next;
        err = lfs_fs_preporphans(lfs, -1);

        if (err) {

            return err;
        }
    }

    // now insert into our parent block
    lfs_pair_tole64(dir.pair);

    {
        lfs_metadata_attribute_t attr[] = {
            { LFS_MKTAG(LFS_TYPE_CREATE, id, 0), NULL },
            { LFS_MKTAG(LFS_TYPE_DIR, id, nlen), path },
            { LFS_MKTAG(LFS_TYPE_DIRSTRUCT, id, sizeof(dir.pair)), dir.pair },
            { LFS_MKTAG_IF(!cwd.metadata.split, LFS_TYPE_SOFTTAIL, 0x3ff, sizeof(dir.pair)), dir.pair }
        };

        err = lfs_dir_commit(lfs, &cwd.metadata, attr, _countof(attr));
    }

    lfs_pair_fromle64(dir.pair);

    if (err) {

        return err;
    }

    return LFS_ERR_OK;
}

int lfs_dir_rawopen(lfs_t* lfs, lfs_dir_t* dir, const char* path) {

    lfs_stag_t tag = lfs_dir_find(lfs, &dir->metadata, &path, NULL);

    if (tag < 0) {

        return tag;
    }

    if (lfs_tag_type3(tag) != LFS_TYPE_DIR) {

        return LFS_ERR_NOTDIR;
    }

    lfs_block_t pair[2];
    if (lfs_tag_id(tag) == 0x3ff) {

        // handle root dir separately
        pair[0] = lfs->root[0];
        pair[1] = lfs->root[1];
    }
    else {

        // get dir pair from parent
        lfs_stag_t res = lfs_dir_get(lfs, &dir->metadata,
            LFS_MKTAG(LFS_TYPE_GLOBALS, 0x3ff, 0),
            LFS_MKTAG(LFS_TYPE_STRUCT, lfs_tag_id(tag), sizeof(pair)), pair);

        if (res < 0) {

            return res;
        }

        lfs_pair_fromle64(pair);
    }

    // fetch first pair
    int err = lfs_dir_fetch(lfs, &dir->metadata, pair);

    if (err) {

        return err;
    }

    // setup entry
    dir->head[0] = dir->metadata.pair[0];
    dir->head[1] = dir->metadata.pair[1];
    dir->id = 0;
    dir->pos = 0;

    // add to list of mdirs
    dir->type = LFS_TYPE_DIR;
    lfs_mlist_append(lfs, (lfs_metadata_list_t*)dir);

    return LFS_ERR_OK;
}

int lfs_dir_rawclose(lfs_t* lfs, lfs_dir_t* dir) {

    // remove from list of mdirs
    lfs_mlist_remove(lfs, (lfs_metadata_list_t*)dir);

    return LFS_ERR_OK;
}

int lfs_dir_rawread(lfs_t* lfs, lfs_dir_t* dir, struct lfs_info* info) {

    memset(info, 0, sizeof(*info));

    // special offset for '.' and '..'
    if (dir->pos == 0) {

        info->type = LFS_TYPE_DIR;
        strcpy_s(info->name, sizeof(info->name), ".");
        dir->pos += 1;
        return LFS_ERR_OK;

    }
    else if (dir->pos == 1) {

        info->type = LFS_TYPE_DIR;
        strcpy_s(info->name, sizeof(info->name), "..");
        dir->pos += 1;
        return LFS_ERR_OK;
    }

    while (true) {

        if (dir->id == dir->metadata.count) {

            //has splited next block
            if (!dir->metadata.split) {

                return LFS_ERR_NOENT;
            }

            //get next block
            int err = lfs_dir_fetch(lfs, &dir->metadata, dir->metadata.tail);

            if (err) {

                return err;
            }

            dir->id = 0;
        }

        //get meta data info by id (index)
        int err = lfs_dir_getinfo(lfs, &dir->metadata, dir->id, info);

        if (err && err != LFS_ERR_NOENT) {

            return err;
        }

        dir->id += 1;

        if (err != LFS_ERR_NOENT) {

            break;
        }
    }

    dir->pos += 1;
    return LFS_ERR_OK;
}

//seek by indexes in directory
int lfs_dir_rawseek(lfs_t* lfs, lfs_dir_t* dir, lfs_off_t offset) {

    // simply walk from head dir
    int err = lfs_dir_rawrewind(lfs, dir);

    if (err) {
        return err;
    }

    // first two for ./..
    dir->pos = lfs_min(2, offset);
    offset -= dir->pos;

    // skip superblock entry
    dir->id = (offset > 0 && lfs_pair_cmp(dir->head, lfs->root) == 0);

    while (offset > 0) {

        if (dir->id == dir->metadata.count) {

            if (!dir->metadata.split) {
                return LFS_ERR_INVAL;
            }

            err = lfs_dir_fetch(lfs, &dir->metadata, dir->metadata.tail);

            if (err) {
                return err;
            }

            dir->id = 0;
        }

        lfs_off_t diff = lfs_min(dir->metadata.count - dir->id, offset);

        dir->id += diff;
        dir->pos += diff;
        offset -= diff;
    }

    return LFS_ERR_OK;
}

//total entries in directory
lfs_soff_t lfs_dir_rawtell(lfs_t* lfs, lfs_dir_t* dir) {
    (void)lfs;
    return dir->pos;
}

int lfs_dir_rawrewind(lfs_t* lfs, lfs_dir_t* dir) {

    // reload the head dir
    int err = lfs_dir_fetch(lfs, &dir->metadata, dir->head);

    if (err) {

        return err;
    }

    dir->id = 0;
    dir->pos = 0;

    return LFS_ERR_OK;
}