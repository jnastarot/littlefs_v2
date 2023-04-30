#include "lfs.h"


/// Top level file operations ///
int lfs_file_rawopencfg(lfs_t* lfs, lfs_file_t* file,
    const char* path, int flags,
    const lfs_file_config_t* cfg) {

    // deorphan if we haven't yet, needed at most once after poweron
    if ((flags & LFS_O_WRONLY) == LFS_O_WRONLY) {

        int err = lfs_fs_forceconsistency(lfs);

        if (err) {
            return err;
        }
    }

    // setup simple file details
    int err;
    file->cfg = cfg;
    file->flags = flags;
    file->pos = 0;
    file->offset = 0;
    file->cache.buffer = NULL;

    // allocate entry for file if it doesn't exist
    lfs_stag_t tag = lfs_dir_find(lfs, &file->metadata, &path, &file->id);

    if (tag < 0 && !(tag == LFS_ERR_NOENT && file->id != 0x3ff)) {

        err = tag;
        goto cleanup;
    }

    // get id, add to list of mdirs to catch update changes
    file->type = LFS_TYPE_REG;
    lfs_mlist_append(lfs, (lfs_metadata_list_t*)file);

    if (tag == LFS_ERR_NOENT) {

        if (!(flags & LFS_O_CREAT)) {

            err = LFS_ERR_NOENT;
            goto cleanup;
        }

        // check that name fits
        lfs_size_t nlen = strlen(path);
        if (nlen > lfs->name_max_length) {

            err = LFS_ERR_NAMETOOLONG;
            goto cleanup;
        }

        // get next slot and create entry to remember name

        lfs_metadata_attribute_t attr[] = {
            { LFS_MKTAG(LFS_TYPE_CREATE, file->id, 0), NULL },
            { LFS_MKTAG(LFS_TYPE_REG, file->id, nlen), path },
            { LFS_MKTAG(LFS_TYPE_INLINESTRUCT, file->id, 0), NULL }
        };

        err = lfs_dir_commit(lfs, &file->metadata, attr, _countof(attr));

        // it may happen that the file name doesn't fit in the metadata blocks, e.g., a 256 byte file name will
        // not fit in a 128 byte block.
        err = (err == LFS_ERR_NOSPC) ? LFS_ERR_NAMETOOLONG : err;

        if (err) {

            goto cleanup;
        }

        tag = LFS_MKTAG(LFS_TYPE_INLINESTRUCT, 0, 0);
    }
    else if (flags & LFS_O_EXCL) {

        err = LFS_ERR_EXIST;
        goto cleanup;

    }
    else if (lfs_tag_type3(tag) != LFS_TYPE_REG) {

        err = LFS_ERR_ISDIR;
        goto cleanup;

    }
    else if (flags & LFS_O_TRUNC) {

        // truncate if requested
        tag = LFS_MKTAG(LFS_TYPE_INLINESTRUCT, file->id, 0);
        file->flags |= LFS_F_DIRTY;

    }
    else {

        // try to load what's on disk, if it's inlined we'll fix it later
        tag = lfs_dir_get(lfs, &file->metadata,
            LFS_MKTAG(LFS_TYPE_GLOBALS, 0x3ff, 0),
            LFS_MKTAG(LFS_TYPE_STRUCT, file->id, sizeof(file->ctz)), &file->ctz);

        if (tag < 0) {
            err = tag;
            goto cleanup;
        }

        lfs_ctz_fromle64(&file->ctz);
    }

    // fetch attrs
    for (unsigned i = 0; i < file->cfg->attr_count; i++) {

        // if opened for read / read-write operations
        if ((file->flags & LFS_O_RDONLY) == LFS_O_RDONLY) {

            lfs_stag_t res = lfs_dir_get(lfs, &file->metadata,
                LFS_MKTAG(LFS_TYPE_MOVESTATE, 0x3ff, 0),
                LFS_MKTAG(LFS_TYPE_USERATTR + file->cfg->attrs[i].type,
                    file->id, file->cfg->attrs[i].size),
                file->cfg->attrs[i].buffer);

            if (res < 0 && res != LFS_ERR_NOENT) {
                err = res;
                goto cleanup;
            }
        }

        // if opened for write / read-write operations
        if ((file->flags & LFS_O_WRONLY) == LFS_O_WRONLY) {
            if (file->cfg->attrs[i].size > lfs->attr_max_size) {
                err = LFS_ERR_NOSPC;
                goto cleanup;
            }

            file->flags |= LFS_F_DIRTY;
        }

    }

    // allocate buffer if needed
    if (file->cfg->buffer) {
        file->cache.buffer = (uint8_t*)file->cfg->buffer;
    }
    else {
        file->cache.buffer = (uint8_t*)malloc(lfs->cfg->cache_size);
        if (!file->cache.buffer) {
            err = LFS_ERR_NOMEM;
            goto cleanup;
        }
    }

    // zero to avoid information leak
    lfs_cache_zero(lfs, &file->cache);

    if (lfs_tag_type3(tag) == LFS_TYPE_INLINESTRUCT) {

        // load inline files
        file->ctz.head = LFS_BLOCK_INLINE;
        file->ctz.size = lfs_tag_size(tag);
        file->flags |= LFS_F_INLINE;
        file->cache.block = file->ctz.head;
        file->cache.offset = 0;
        file->cache.size = lfs->cfg->cache_size;

        // don't always read (may be new/trunc file)
        if (file->ctz.size > 0) {

            lfs_stag_t res = lfs_dir_get(lfs, &file->metadata,
                LFS_MKTAG(LFS_TYPE_GLOBALS, 0x3ff, 0),
                LFS_MKTAG(LFS_TYPE_STRUCT, file->id, lfs_min(file->cache.size, 0x3fe)), file->cache.buffer);

            if (res < 0) {
                err = res;
                goto cleanup;
            }
        }
    }

    return LFS_ERR_OK;

cleanup:
    // clean up lingering resources

    file->flags |= LFS_F_ERRED;

    lfs_file_rawclose(lfs, file);
    return err;
}

int lfs_file_rawopen(lfs_t* lfs, lfs_file_t* file, const char* path, int flags) {

    static const lfs_file_config_t defaults = { 0 };

    return lfs_file_rawopencfg(lfs, file, path, flags, &defaults);
}

int lfs_file_rawclose(lfs_t* lfs, lfs_file_t* file) {

    int err = lfs_file_rawsync(lfs, file);

    // remove from list of mdirs
    lfs_mlist_remove(lfs, (lfs_metadata_list_t*)file);

    // clean up memory
    if (!file->cfg->buffer) {
        free(file->cache.buffer);
    }

    return err;
}

int lfs_file_relocate(lfs_t* lfs, lfs_file_t* file) {

    while (true) {

        // just relocate what exists into new block
        lfs_block_t nblock;
        int err = lfs_alloc(lfs, &nblock);

        if (err) {

            return err;
        }

        err = lfs_bd_erase(lfs, nblock);

        if (err) {

            if (err == LFS_ERR_CORRUPT) {

                goto relocate;
            }

            return err;
        }

        // either read from dirty cache or disk
        for (lfs_off_t i = 0; i < file->offset; i++) {

            uint8_t data;

            if (file->flags & LFS_F_INLINE) {

                err = lfs_dir_getread(lfs, &file->metadata,
                    // note we evict inline files before they can be dirty
                    NULL, &file->cache, file->offset - i,
                    LFS_MKTAG(0xfff, 0x1ff, 0),
                    LFS_MKTAG(LFS_TYPE_INLINESTRUCT, file->id, 0), i, &data, 1);

                if (err) {

                    return err;
                }
            }
            else {

                err = lfs_bd_read(lfs,
                    &file->cache, &lfs->read_cache, file->offset - i,
                    file->block, i, &data, 1);

                if (err) {

                    return err;
                }
            }

            err = lfs_bd_write(lfs,
                &lfs->write_cache, &lfs->read_cache, true,
                nblock, i, &data, 1);

            if (err) {

                if (err == LFS_ERR_CORRUPT) {

                    goto relocate;
                }

                return err;
            }
        }

        // copy over new state of file
        memcpy(file->cache.buffer, lfs->write_cache.buffer, lfs->cfg->cache_size);
        file->cache.block = lfs->write_cache.block;
        file->cache.offset = lfs->write_cache.offset;
        file->cache.size = lfs->write_cache.size;
        lfs_cache_zero(lfs, &lfs->write_cache);

        file->block = nblock;
        file->flags |= LFS_F_WRITING;
        return LFS_ERR_OK;

    relocate:
        LFS_DEBUG("Bad block at 0x%"PRIx32, nblock);

        // just clear cache and try a new block
        lfs_cache_drop(lfs, &lfs->write_cache);
    }
}

int lfs_file_outline(lfs_t* lfs, lfs_file_t* file) {

    file->offset = file->pos;
    lfs_alloc_ack(lfs);

    int err = lfs_file_relocate(lfs, file);
    if (err) {

        return err;
    }

    file->flags &= ~LFS_F_INLINE;
    return LFS_ERR_OK;
}

int lfs_file_flush(lfs_t* lfs, lfs_file_t* file) {

    if (file->flags & LFS_F_READING) {

        if (!(file->flags & LFS_F_INLINE)) {

            lfs_cache_drop(lfs, &file->cache);
        }

        file->flags &= ~LFS_F_READING;
    }

    if (file->flags & LFS_F_WRITING) {

        lfs_off_t pos = file->pos;

        if (!(file->flags & LFS_F_INLINE)) {

            // copy over anything after current branch
            lfs_file_t orig{};
            orig.ctz.head = file->ctz.head;
            orig.ctz.size = file->ctz.size;
            orig.flags = LFS_O_RDONLY;
            orig.pos = file->pos;
            orig.cache = lfs->read_cache;

            lfs_cache_drop(lfs, &lfs->read_cache);

            while (file->pos < file->ctz.size) {

                // copy over a byte at a time, leave it up to caching
                // to make this efficient
                uint8_t data;
                lfs_ssize_t res = lfs_file_flushedread(lfs, &orig, &data, 1);

                if (res < 0) {

                    return res;
                }

                res = lfs_file_flushedwrite(lfs, file, &data, 1);

                if (res < 0) {

                    return res;
                }

                // keep our reference to the read_cache in sync
                if (lfs->read_cache.block != LFS_BLOCK_NULL) {
                    lfs_cache_drop(lfs, &orig.cache);
                    lfs_cache_drop(lfs, &lfs->read_cache);
                }
            }

            // write out what we have
            while (true) {

                int err = lfs_bd_flush(lfs, &file->cache, &lfs->read_cache, true);

                if (err) {

                    if (err == LFS_ERR_CORRUPT) {
                        goto relocate;
                    }

                    return err;
                }

                break;

            relocate:
                LFS_DEBUG("Bad block at 0x%"PRIx32, file->block);
                err = lfs_file_relocate(lfs, file);
                if (err) {
                    return err;
                }
            }
        }
        else {
            file->pos = lfs_max(file->pos, file->ctz.size);
        }

        // actual file updates
        file->ctz.head = file->block;
        file->ctz.size = file->pos;
        file->flags &= ~LFS_F_WRITING;
        file->flags |= LFS_F_DIRTY;

        file->pos = pos;
    }

    return LFS_ERR_OK;
}

int lfs_file_rawsync(lfs_t* lfs, lfs_file_t* file) {

    if (file->flags & LFS_F_ERRED) {
        // it's not safe to do anything if our file errored
        return LFS_ERR_OK;
    }

    int err = lfs_file_flush(lfs, file);

    if (err) {

        file->flags |= LFS_F_ERRED;
        return err;
    }

    if ((file->flags & LFS_F_DIRTY) && !lfs_pair_isnull(file->metadata.pair)) {

        // update dir entry
        uint16_t type;
        const void* buffer;
        lfs_size_t size;
        lfs_ctz_t ctz;

        if (file->flags & LFS_F_INLINE) {

            // inline the whole file
            type = LFS_TYPE_INLINESTRUCT;
            buffer = file->cache.buffer;
            size = file->ctz.size;
        }
        else {

            // update the ctz reference
            type = LFS_TYPE_CTZSTRUCT;
            // copy ctz so alloc will work during a relocate
            ctz = file->ctz;
            lfs_ctz_tole64(&ctz);
            buffer = &ctz;
            size = sizeof(ctz);
        }

        // commit file data and attributes
        lfs_metadata_attribute_t attr[] = {
            { LFS_MKTAG(type, file->id, size), buffer },
            { LFS_MKTAG(LFS_FROM_USERATTRS, file->id, file->cfg->attr_count), file->cfg->attrs }
        };

        err = lfs_dir_commit(lfs, &file->metadata, attr, _countof(attr));

        if (err) {
            file->flags |= LFS_F_ERRED;
            return err;
        }

        file->flags &= ~LFS_F_DIRTY;
    }

    return LFS_ERR_OK;
}

lfs_ssize_t lfs_file_flushedread(lfs_t* lfs, lfs_file_t* file, void* buffer, lfs_size_t size) {

    uint8_t* data = (uint8_t*)buffer;
    lfs_size_t nsize = size;

    if (file->pos >= file->ctz.size) {
        // eof if past end
        return LFS_ERR_OK;
    }

    size = lfs_min(size, file->ctz.size - file->pos);
    nsize = size;

    while (nsize > 0) {

        // check if we need a new block
        if (!(file->flags & LFS_F_READING) || file->offset == lfs->block_size) {

            if (!(file->flags & LFS_F_INLINE)) {

                int err = lfs_ctz_find(lfs, NULL, &file->cache,
                    file->ctz.head, file->ctz.size,
                    file->pos, &file->block, &file->offset);

                if (err) {
                    return err;
                }
            }
            else {

                file->block = LFS_BLOCK_INLINE;
                file->offset = file->pos;
            }

            file->flags |= LFS_F_READING;
        }

        // read as much as we can in current block
        lfs_size_t diff = lfs_min(nsize, lfs->block_size - file->offset);

        if (file->flags & LFS_F_INLINE) {

            int err = lfs_dir_getread(lfs, &file->metadata,
                NULL, &file->cache, lfs->block_size,
                LFS_MKTAG(0xfff, 0x1ff, 0),
                LFS_MKTAG(LFS_TYPE_INLINESTRUCT, file->id, 0),
                file->offset, data, diff);

            if (err) {
                return err;
            }
        }
        else {

            int err = lfs_bd_read(lfs,
                NULL, &file->cache, lfs->block_size,
                file->block, file->offset, data, diff);

            if (err) {
                return err;
            }
        }

        file->pos += diff;
        file->offset += diff;
        data += diff;
        nsize -= diff;
    }

    return size;
}

lfs_ssize_t lfs_file_rawread(lfs_t* lfs, lfs_file_t* file, void* buffer, lfs_size_t size) {

    LFS_ASSERT((file->flags & LFS_O_RDONLY) == LFS_O_RDONLY);

    if (file->flags & LFS_F_WRITING) {

        // flush out any writes
        int err = lfs_file_flush(lfs, file);

        if (err) {

            return err;
        }
    }

    return lfs_file_flushedread(lfs, file, buffer, size);
}

lfs_ssize_t lfs_file_flushedwrite(lfs_t* lfs, lfs_file_t* file, const void* buffer, lfs_size_t size) {

    const uint8_t* data = (const uint8_t*)buffer;
    lfs_size_t nsize = size;

    if ((file->flags & LFS_F_INLINE) &&

        lfs_max(file->pos + nsize, file->ctz.size) >
        lfs_min(0x3fe, lfs_min(
            lfs->cfg->cache_size,
            (lfs->cfg->metadata_max ?
                lfs->cfg->metadata_max : lfs->block_size) / sizeof(lfs_block_t[2])))) {

        // inline file doesn't fit anymore
        int err = lfs_file_outline(lfs, file);
        if (err) {

            file->flags |= LFS_F_ERRED;
            return err;
        }
    }

    while (nsize > 0) {

        // check if we need a new block
        if (!(file->flags & LFS_F_WRITING) ||

            file->offset == lfs->block_size) {

            if (!(file->flags & LFS_F_INLINE)) {

                if (!(file->flags & LFS_F_WRITING) && file->pos > 0) {

                    // find out which block we're extending from
                    lfs_off_t _offset = 0;

                    int err = lfs_ctz_find(lfs, NULL, &file->cache,
                        file->ctz.head, file->ctz.size,
                        file->pos - 1, &file->block, &_offset);

                    if (err) {

                        file->flags |= LFS_F_ERRED;
                        return err;
                    }

                    // mark cache as dirty since we may have read data into it
                    lfs_cache_zero(lfs, &file->cache);
                }

                // extend file with new blocks
                lfs_alloc_ack(lfs);

                int err = lfs_ctz_extend(lfs, &file->cache, &lfs->read_cache,
                    file->block, file->pos,
                    &file->block, &file->offset);

                if (err) {

                    file->flags |= LFS_F_ERRED;
                    return err;
                }
            }
            else {
                file->block = LFS_BLOCK_INLINE;
                file->offset = file->pos;
            }

            file->flags |= LFS_F_WRITING;
        }

        // program as much as we can in current block
        lfs_size_t diff = lfs_min(nsize, lfs->block_size - file->offset);

        while (true) {

            int err = lfs_bd_write(lfs, &file->cache, &lfs->read_cache, true,
                file->block, file->offset, data, diff);

            if (err) {

                if (err == LFS_ERR_CORRUPT) {

                    goto relocate;
                }

                file->flags |= LFS_F_ERRED;
                return err;
            }

            break;

        relocate:
            err = lfs_file_relocate(lfs, file);
            if (err) {

                file->flags |= LFS_F_ERRED;
                return err;
            }
        }

        file->pos += diff;
        file->offset += diff;
        data += diff;
        nsize -= diff;

        lfs_alloc_ack(lfs);
    }

    return size;
}

lfs_ssize_t lfs_file_rawwrite(lfs_t* lfs, lfs_file_t* file, const void* buffer, lfs_size_t size) {

    LFS_ASSERT((file->flags & LFS_O_WRONLY) == LFS_O_WRONLY);

    if (file->flags & LFS_F_READING) {
        // drop any reads
        int err = lfs_file_flush(lfs, file);

        if (err) {

            return err;
        }
    }

    if ((file->flags & LFS_O_APPEND) && file->pos < file->ctz.size) {
        file->pos = file->ctz.size;
    }

    if (file->pos + size > lfs->file_max_size) {

        // Larger than file limit?
        return LFS_ERR_FBIG;
    }

    if (!(file->flags & LFS_F_WRITING) && file->pos > file->ctz.size) {

        // fill with zeros
        lfs_off_t pos = file->pos;
        file->pos = file->ctz.size;

        while (file->pos < pos) {

            uint8_t _buffer = 0;
            lfs_ssize_t res = lfs_file_flushedwrite(lfs, file, &_buffer, 1);

            if (res < 0) {

                return res;
            }
        }
    }

    lfs_ssize_t nsize = lfs_file_flushedwrite(lfs, file, buffer, size);

    if (nsize < 0) {

        return nsize;
    }

    file->flags &= ~LFS_F_ERRED;
    return nsize;
}

lfs_soff_t lfs_file_rawseek(lfs_t* lfs, lfs_file_t* file, lfs_soff_t offset, int whence) {

    // find new pos
    lfs_off_t npos = file->pos;
    if (whence == LFS_SEEK_SET) {

        npos = offset;
    }
    else if (whence == LFS_SEEK_CUR) {

        if ((lfs_soff_t)file->pos + offset < 0) {

            return LFS_ERR_INVAL;
        }
        else {

            npos = file->pos + offset;
        }
    }
    else if (whence == LFS_SEEK_END) {

        lfs_soff_t res = lfs_file_rawsize(lfs, file) + offset;

        if (res < 0) {

            return LFS_ERR_INVAL;
        }
        else {
            npos = res;
        }
    }

    if (npos > lfs->file_max_size) {
        // file position out of range
        return LFS_ERR_INVAL;
    }

    if (file->pos == npos) {
        // noop - position has not changed
        return npos;
    }

    // if we're only reading and our new offset is still in the file's cache
    // we can avoid flushing and needing to reread the data
    if (!(file->flags & LFS_F_WRITING)) {

        lfs_off_t _noff = file->pos;
        int oindex = lfs_ctz_index(lfs, &_noff);
        lfs_off_t noff = npos;
        int nindex = lfs_ctz_index(lfs, &noff);

        if (oindex == nindex
            && noff >= file->cache.offset
            && noff < file->cache.offset + file->cache.size) {

            file->pos = npos;
            file->offset = noff;
            return npos;
        }
    }

    // write out everything beforehand, may be noop if rdonly
    int err = lfs_file_flush(lfs, file);
    if (err) {

        return err;
    }

    // update pos
    file->pos = npos;
    return npos;
}

lfs_soff_t lfs_file_rawtruncate(lfs_t* lfs, lfs_file_t* file, lfs_off_t size) {

    LFS_ASSERT((file->flags & LFS_O_WRONLY) == LFS_O_WRONLY);

    if (size > LFS_FILE_MAX) {

        return LFS_ERR_INVAL;
    }

    lfs_off_t pos = file->pos;
    lfs_off_t oldsize = lfs_file_rawsize(lfs, file);

    if (size < oldsize) {

        if (size <= lfs_min(0x3fe, 
            lfs_min(lfs->cfg->cache_size,
                        (lfs->cfg->metadata_max ? lfs->cfg->metadata_max : lfs->cfg->block_size) / 8))) {

            // flush+seek to head
            lfs_soff_t res = lfs_file_rawseek(lfs, file, 0, LFS_SEEK_SET);
            
            if (res < 0) {

                return res;
            }

            // read our data into rcache temporarily
            lfs_cache_drop(lfs, &lfs->read_cache);
            res = lfs_file_flushedread(lfs, file, lfs->read_cache.buffer, size);

            if (res < 0) {
            
                return res;
            }

            file->ctz.head = LFS_BLOCK_INLINE;
            file->ctz.size = size;
            file->flags |= LFS_F_DIRTY | LFS_F_READING | LFS_F_INLINE;
            file->cache.block = file->ctz.head;
            file->cache.offset = 0;
            file->cache.size = lfs->cfg->cache_size;

            memcpy(file->cache.buffer, lfs->read_cache.buffer, size);
        }
        else {

            // need to flush since directly changing metadata
            int err = lfs_file_flush(lfs, file);
         
            if (err) {
            
                return err;
            }

            // lookup new head in ctz skip list
            lfs_off_t _offset = 0;
            err = lfs_ctz_find(lfs, NULL, &file->cache,
                file->ctz.head, file->ctz.size,
                size - 1, &file->block, &_offset);

            if (err) {

                return err;
            }

            // need to set pos/block/off consistently so seeking back to
            // the old position does not get confused
            file->pos = size;
            file->ctz.head = file->block;
            file->ctz.size = size;
            file->flags |= LFS_F_DIRTY | LFS_F_READING;
        }
    }
    else if (size > oldsize) {

        // flush+seek if not already at end
        lfs_soff_t res = lfs_file_rawseek(lfs, file, 0, LFS_SEEK_END);
        
        if (res < 0) {

            return res;
        }

        // fill with zeros
        while (file->pos < size) {

            uint8_t _buffer = 0;
            res = lfs_file_rawwrite(lfs, file, &_buffer, 1);

            if (res < 0) {

                return res;
            }
        }
    }

    // restore pos
    lfs_soff_t res = lfs_file_rawseek(lfs, file, pos, LFS_SEEK_SET);

    if (res < 0) {

        return res;
    }

    return LFS_ERR_OK;
}

lfs_soff_t lfs_file_rawtell(lfs_t* lfs, lfs_file_t* file) {
    (void)lfs;
    return file->pos;
}

lfs_soff_t lfs_file_rawrewind(lfs_t* lfs, lfs_file_t* file) {

    lfs_soff_t res = lfs_file_rawseek(lfs, file, 0, LFS_SEEK_SET);

    if (res < 0) {

        return res;
    }

    return LFS_ERR_OK;
}

lfs_soff_t lfs_file_rawsize(lfs_t* lfs, lfs_file_t* file) {
    (void)lfs;

    if (file->flags & LFS_F_WRITING) {
        return lfs_max(file->pos, file->ctz.size);
    }

    return file->ctz.size;
}
