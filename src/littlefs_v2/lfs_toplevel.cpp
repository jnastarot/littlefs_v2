#include "lfs.h"

/// Public API wrappers ///

// Here we can add tracing/thread safety easily

// Thread-safe wrappers if enabled
#ifdef LFS_THREADSAFE
#define LFS_LOCK(cfg)   cfg->lock(cfg)
#define LFS_UNLOCK(cfg) cfg->unlock(cfg)
#else
#define LFS_LOCK(cfg)   ((void)cfg, 0)
#define LFS_UNLOCK(cfg) ((void)cfg)
#endif

// Public API
int lfs_format(lfs_t* lfs, lfs_config_t* cfg) {

    int err = LFS_LOCK(cfg);
    if (err) {

        return err;
    }

    LFS_TRACE("lfs_format(%p, %p {.context=%p, "
        ".read=%p, .prog=%p, .erase=%p, .sync=%p, "
        ".read_size=%"PRIu32", .prog_size=%"PRIu32", "
        ".block_size=%"PRIu32", .block_count=%"PRIu32", "
        ".block_cycles=%"PRIu32", .cache_size=%"PRIu32", "
        ".lookahead_size=%"PRIu32", .read_buffer=%p, "
        ".prog_buffer=%p, .lookahead_buffer=%p, "
        ".name_max_length=%"PRIu32", .file_max_size=%"PRIu32", "
        ".attr_max_size=%"PRIu32"})",
        (void*)lfs, (void*)cfg, cfg->context,
        (void*)(uintptr_t)cfg->read, (void*)(uintptr_t)cfg->prog,
        (void*)(uintptr_t)cfg->erase, (void*)(uintptr_t)cfg->sync,
        cfg->read_size, cfg->prog_size, cfg->block_size, cfg->block_count,
        cfg->block_cycles, cfg->cache_size, cfg->lookahead_size,
        cfg->read_buffer, cfg->write_buffer, cfg->lookahead_buffer,
        cfg->name_max_length, cfg->file_max_size, cfg->attr_max_size);

    err = lfs_raw_format(lfs, cfg);

    LFS_TRACE("lfs_format -> %d", err);
    LFS_UNLOCK(cfg);
    return err;
}

int lfs_mount(lfs_t* lfs, lfs_config_t* cfg) {

    int err = LFS_LOCK(cfg);
    if (err) {

        return err;
    }

    LFS_TRACE("lfs_mount(%p, %p {.context=%p, "
        ".read=%p, .prog=%p, .erase=%p, .sync=%p, "
        ".read_size=%"PRIu32", .prog_size=%"PRIu32", "
        ".block_size=%"PRIu32", .block_count=%"PRIu32", "
        ".block_cycles=%"PRIu32", .cache_size=%"PRIu32", "
        ".lookahead_size=%"PRIu32", .read_buffer=%p, "
        ".prog_buffer=%p, .lookahead_buffer=%p, "
        ".name_max_length=%"PRIu32", .file_max_size=%"PRIu32", "
        ".attr_max_size=%"PRIu32"})",
        (void*)lfs, (void*)cfg, cfg->context,
        (void*)(uintptr_t)cfg->read, (void*)(uintptr_t)cfg->prog,
        (void*)(uintptr_t)cfg->erase, (void*)(uintptr_t)cfg->sync,
        cfg->read_size, cfg->prog_size, cfg->block_size, cfg->block_count,
        cfg->block_cycles, cfg->cache_size, cfg->lookahead_size,
        cfg->read_buffer, cfg->write_buffer, cfg->lookahead_buffer,
        cfg->name_max_length, cfg->file_max_size, cfg->attr_max_size);

    err = lfs_raw_mount(lfs, cfg);

    LFS_TRACE("lfs_mount -> %d", err);
    LFS_UNLOCK(cfg);
    return err;
}

int lfs_unmount(lfs_t* lfs) {

    int err = LFS_LOCK(lfs->cfg);

    if (err) {

        return err;
    }

    LFS_TRACE("lfs_unmount(%p)", (void*)lfs);

    err = lfs_raw_unmount(lfs);

    LFS_TRACE("lfs_unmount -> %d", err);
    LFS_UNLOCK(lfs->cfg);
    return err;
}

int lfs_remove(lfs_t* lfs, const char* path) {

    int err = LFS_LOCK(lfs->cfg);

    if (err) {

        return err;
    }

    LFS_TRACE("lfs_remove(%p, \"%s\")", (void*)lfs, path);

    err = lfs_raw_remove(lfs, path);

    LFS_TRACE("lfs_remove -> %d", err);
    LFS_UNLOCK(lfs->cfg);
    return err;
}

int lfs_rename(lfs_t* lfs, const char* oldpath, const char* newpath) {

    int err = LFS_LOCK(lfs->cfg);
    if (err) {

        return err;
    }

    LFS_TRACE("lfs_rename(%p, \"%s\", \"%s\")", (void*)lfs, oldpath, newpath);

    err = lfs_raw_rename(lfs, oldpath, newpath);

    LFS_TRACE("lfs_rename -> %d", err);
    LFS_UNLOCK(lfs->cfg);
    return err;
}

int lfs_stat(lfs_t* lfs, const char* path, struct lfs_info* info) {

    int err = LFS_LOCK(lfs->cfg);

    if (err) {

        return err;
    }

    LFS_TRACE("lfs_stat(%p, \"%s\", %p)", (void*)lfs, path, (void*)info);

    err = lfs_raw_stat(lfs, path, info);

    LFS_TRACE("lfs_stat -> %d", err);
    LFS_UNLOCK(lfs->cfg);
    return err;
}

lfs_ssize_t lfs_get_attribute(lfs_t* lfs, const char* path, uint8_t type, void* buffer, lfs_size_t size) {

    int err = LFS_LOCK(lfs->cfg);

    if (err) {

        return err;
    }

    LFS_TRACE("lfs_getattr(%p, \"%s\", %"PRIu8", %p, %"PRIu32")",
        (void*)lfs, path, type, buffer, size);

    lfs_ssize_t res = lfs_raw_get_attribute(lfs, path, type, buffer, size);

    LFS_TRACE("lfs_getattr -> %"PRId32, res);
    LFS_UNLOCK(lfs->cfg);
    return res;
}

int lfs_set_attribute(lfs_t* lfs, const char* path, uint8_t type, const void* buffer, lfs_size_t size) {

    int err = LFS_LOCK(lfs->cfg);

    if (err) {

        return err;
    }

    LFS_TRACE("lfs_setattr(%p, \"%s\", %"PRIu8", %p, %"PRIu32")",
        (void*)lfs, path, type, buffer, size);

    err = lfs_raw_set_attribute(lfs, path, type, buffer, size);

    LFS_TRACE("lfs_setattr -> %d", err);
    LFS_UNLOCK(lfs->cfg);
    return err;
}

int lfs_remove_attribute(lfs_t* lfs, const char* path, uint8_t type) {

    int err = LFS_LOCK(lfs->cfg);

    if (err) {
        return err;
    }

    LFS_TRACE("lfs_removeattr(%p, \"%s\", %"PRIu8")", (void*)lfs, path, type);

    err = lfs_raw_remove_attribute(lfs, path, type);

    LFS_TRACE("lfs_removeattr -> %d", err);
    LFS_UNLOCK(lfs->cfg);
    return err;
}

#ifndef LFS_NO_MALLOC
int lfs_file_open(lfs_t* lfs, lfs_file_t* file, const char* path, int flags) {

    int err = LFS_LOCK(lfs->cfg);

    if (err) {

        return err;
    }

    LFS_TRACE("lfs_file_open(%p, %p, \"%s\", %x)", (void*)lfs, (void*)file, path, flags);
    LFS_ASSERT(!lfs_mlist_isopen(lfs->metadata_list, (lfs_metadata_list_t*)file));

    err = lfs_file_rawopen(lfs, file, path, flags);

    LFS_TRACE("lfs_file_open -> %d", err);
    LFS_UNLOCK(lfs->cfg);
    return err;
}
#endif

int lfs_file_opencfg(lfs_t* lfs, lfs_file_t* file, const char* path, int flags, const lfs_file_config_t* cfg) {

    int err = LFS_LOCK(lfs->cfg);

    if (err) {
        return err;
    }

    LFS_TRACE("lfs_file_opencfg(%p, %p, \"%s\", %x, %p {"
        ".buffer=%p, .attrs=%p, .attr_count=%"PRIu32"})",
        (void*)lfs, (void*)file, path, flags,
        (void*)cfg, cfg->buffer, (void*)cfg->attrs, cfg->attr_count);
    LFS_ASSERT(!lfs_mlist_isopen(lfs->metadata_list, (lfs_metadata_list_t*)file));

    err = lfs_file_rawopencfg(lfs, file, path, flags, cfg);

    LFS_TRACE("lfs_file_opencfg -> %d", err);
    LFS_UNLOCK(lfs->cfg);
    return err;
}

int lfs_file_close(lfs_t* lfs, lfs_file_t* file) {

    int err = LFS_LOCK(lfs->cfg);
    if (err) {
        return err;
    }

    LFS_TRACE("lfs_file_close(%p, %p)", (void*)lfs, (void*)file);
    LFS_ASSERT(lfs_mlist_isopen(lfs->metadata_list, (lfs_metadata_list_t*)file));

    err = lfs_file_rawclose(lfs, file);

    LFS_TRACE("lfs_file_close -> %d", err);
    LFS_UNLOCK(lfs->cfg);
    return err;
}

int lfs_file_sync(lfs_t* lfs, lfs_file_t* file) {

    int err = LFS_LOCK(lfs->cfg);

    if (err) {
        return err;
    }

    LFS_TRACE("lfs_file_sync(%p, %p)", (void*)lfs, (void*)file);
    LFS_ASSERT(lfs_mlist_isopen(lfs->metadata_list, (lfs_metadata_list_t*)file));

    err = lfs_file_rawsync(lfs, file);

    LFS_TRACE("lfs_file_sync -> %d", err);
    LFS_UNLOCK(lfs->cfg);
    return err;
}

lfs_ssize_t lfs_file_read(lfs_t* lfs, lfs_file_t* file, void* buffer, lfs_size_t size) {

    int err = LFS_LOCK(lfs->cfg);

    if (err) {
        return err;
    }

    LFS_TRACE("lfs_file_read(%p, %p, %p, %"PRIu32")",
        (void*)lfs, (void*)file, buffer, size);
    LFS_ASSERT(lfs_mlist_isopen(lfs->metadata_list, (lfs_metadata_list_t*)file));

    lfs_ssize_t res = lfs_file_rawread(lfs, file, buffer, size);

    LFS_TRACE("lfs_file_read -> %"PRId32, res);
    LFS_UNLOCK(lfs->cfg);
    return res;
}

lfs_ssize_t lfs_file_write(lfs_t* lfs, lfs_file_t* file, const void* buffer, lfs_size_t size) {

    int err = LFS_LOCK(lfs->cfg);

    if (err) {
        return err;
    }

    LFS_TRACE("lfs_file_write(%p, %p, %p, %"PRIu32")", (void*)lfs, (void*)file, buffer, size);
    LFS_ASSERT(lfs_mlist_isopen(lfs->metadata_list, (lfs_metadata_list_t*)file));

    lfs_ssize_t res = lfs_file_rawwrite(lfs, file, buffer, size);

    LFS_TRACE("lfs_file_write -> %"PRId32, res);
    LFS_UNLOCK(lfs->cfg);
    return res;
}

lfs_soff_t lfs_file_seek(lfs_t* lfs, lfs_file_t* file, lfs_soff_t off, int whence) {

    int err = LFS_LOCK(lfs->cfg);

    if (err) {
        return err;
    }

    LFS_TRACE("lfs_file_seek(%p, %p, %"PRId32", %d)", (void*)lfs, (void*)file, off, whence);
    LFS_ASSERT(lfs_mlist_isopen(lfs->metadata_list, (lfs_metadata_list_t*)file));

    lfs_soff_t res = lfs_file_rawseek(lfs, file, off, whence);

    LFS_TRACE("lfs_file_seek -> %"PRId32, res);
    LFS_UNLOCK(lfs->cfg);
    return res;
}

lfs_soff_t lfs_file_truncate(lfs_t* lfs, lfs_file_t* file, lfs_off_t size) {

    lfs_soff_t err = LFS_LOCK(lfs->cfg);

    if (err) {
        return err;
    }

    LFS_TRACE("lfs_file_truncate(%p, %p, %"PRIu32")", (void*)lfs, (void*)file, size);
    LFS_ASSERT(lfs_mlist_isopen(lfs->metadata_list, (lfs_metadata_list_t*)file));

    err = lfs_file_rawtruncate(lfs, file, size);

    LFS_TRACE("lfs_file_truncate -> %d", err);
    LFS_UNLOCK(lfs->cfg);
    return err;
}

lfs_soff_t lfs_file_tell(lfs_t* lfs, lfs_file_t* file) {

    lfs_soff_t err = LFS_LOCK(lfs->cfg);

    if (err) {
        return err;
    }

    LFS_TRACE("lfs_file_tell(%p, %p)", (void*)lfs, (void*)file);
    LFS_ASSERT(lfs_mlist_isopen(lfs->metadata_list, (lfs_metadata_list_t*)file));

    lfs_soff_t res = lfs_file_rawtell(lfs, file);

    LFS_TRACE("lfs_file_tell -> %"PRId32, res);
    LFS_UNLOCK(lfs->cfg);
    return res;
}

lfs_soff_t lfs_file_rewind(lfs_t* lfs, lfs_file_t* file) {

    lfs_soff_t err = LFS_LOCK(lfs->cfg);

    if (err) {
        return err;
    }

    LFS_TRACE("lfs_file_rewind(%p, %p)", (void*)lfs, (void*)file);

    err = lfs_file_rawrewind(lfs, file);

    LFS_TRACE("lfs_file_rewind -> %d", err);
    LFS_UNLOCK(lfs->cfg);
    return err;
}

lfs_soff_t lfs_file_size(lfs_t* lfs, lfs_file_t* file) {

    lfs_soff_t err = LFS_LOCK(lfs->cfg);

    if (err) {
        return err;
    }

    LFS_TRACE("lfs_file_size(%p, %p)", (void*)lfs, (void*)file);
    LFS_ASSERT(lfs_mlist_isopen(lfs->metadata_list, (lfs_metadata_list_t*)file));

    lfs_soff_t res = lfs_file_rawsize(lfs, file);

    LFS_TRACE("lfs_file_size -> %"PRId32, res);
    LFS_UNLOCK(lfs->cfg);
    return res;
}

int lfs_mkdir(lfs_t* lfs, const char* path) {

    int err = LFS_LOCK(lfs->cfg);

    if (err) {
        return err;
    }

    LFS_TRACE("lfs_mkdir(%p, \"%s\")", (void*)lfs, path);

    err = lfs_dir_rawcreate(lfs, path);

    LFS_TRACE("lfs_mkdir -> %d", err);
    LFS_UNLOCK(lfs->cfg);
    return err;
}

int lfs_dir_open(lfs_t* lfs, lfs_dir_t* dir, const char* path) {

    int err = LFS_LOCK(lfs->cfg);

    if (err) {
        return err;
    }

    LFS_TRACE("lfs_dir_open(%p, %p, \"%s\")", (void*)lfs, (void*)dir, path);
    LFS_ASSERT(!lfs_mlist_isopen(lfs->metadata_list, (lfs_metadata_list_t*)dir));

    err = lfs_dir_rawopen(lfs, dir, path);

    LFS_TRACE("lfs_dir_open -> %d", err);
    LFS_UNLOCK(lfs->cfg);
    return err;
}

int lfs_dir_close(lfs_t* lfs, lfs_dir_t* dir) {

    int err = LFS_LOCK(lfs->cfg);

    if (err) {
        return err;
    }

    LFS_TRACE("lfs_dir_close(%p, %p)", (void*)lfs, (void*)dir);

    err = lfs_dir_rawclose(lfs, dir);

    LFS_TRACE("lfs_dir_close -> %d", err);
    LFS_UNLOCK(lfs->cfg);
    return err;
}

int lfs_dir_read(lfs_t* lfs, lfs_dir_t* dir, struct lfs_info* info) {

    int err = LFS_LOCK(lfs->cfg);

    if (err) {
        return err;
    }

    LFS_TRACE("lfs_dir_read(%p, %p, %p)",
        (void*)lfs, (void*)dir, (void*)info);

    err = lfs_dir_rawread(lfs, dir, info);

    LFS_TRACE("lfs_dir_read -> %d", err);
    LFS_UNLOCK(lfs->cfg);
    return err;
}

int lfs_dir_seek(lfs_t* lfs, lfs_dir_t* dir, lfs_off_t off) {

    int err = LFS_LOCK(lfs->cfg);

    if (err) {
        return err;
    }

    LFS_TRACE("lfs_dir_seek(%p, %p, %"PRIu32")",
        (void*)lfs, (void*)dir, off);

    err = lfs_dir_rawseek(lfs, dir, off);

    LFS_TRACE("lfs_dir_seek -> %d", err);
    LFS_UNLOCK(lfs->cfg);
    return err;
}

lfs_soff_t lfs_dir_tell(lfs_t* lfs, lfs_dir_t* dir) {

    int err = LFS_LOCK(lfs->cfg);

    if (err) {
        return err;
    }

    LFS_TRACE("lfs_dir_tell(%p, %p)", (void*)lfs, (void*)dir);

    lfs_soff_t res = lfs_dir_rawtell(lfs, dir);

    LFS_TRACE("lfs_dir_tell -> %"PRId32, res);
    LFS_UNLOCK(lfs->cfg);
    return res;
}

int lfs_dir_rewind(lfs_t* lfs, lfs_dir_t* dir) {

    int err = LFS_LOCK(lfs->cfg);

    if (err) {
        return err;
    }

    LFS_TRACE("lfs_dir_rewind(%p, %p)", (void*)lfs, (void*)dir);

    err = lfs_dir_rawrewind(lfs, dir);

    LFS_TRACE("lfs_dir_rewind -> %d", err);
    LFS_UNLOCK(lfs->cfg);
    return err;
}

int lfs_fs_stat(lfs_t* lfs, struct lfs_fsinfo* fsinfo) {
    int err = LFS_LOCK(lfs->cfg);
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_fs_stat(%p, %p)", (void*)lfs, (void*)fsinfo);

    err = lfs_fs_rawstat(lfs, fsinfo);

    LFS_TRACE("lfs_fs_stat -> %d", err);
    LFS_UNLOCK(lfs->cfg);
    return err;
}

lfs_ssize_t lfs_fs_size(lfs_t* lfs) {

    int err = LFS_LOCK(lfs->cfg);

    if (err) {
        return err;
    }

    LFS_TRACE("lfs_fs_size(%p)", (void*)lfs);

    lfs_ssize_t res = lfs_fs_rawsize(lfs);

    LFS_TRACE("lfs_fs_size -> %"PRId32, res);
    LFS_UNLOCK(lfs->cfg);
    return res;
}

int lfs_fs_traverse(lfs_t* lfs, int (*cb)(void*, lfs_block_t), void* data) {

    int err = LFS_LOCK(lfs->cfg);

    if (err) {
        return err;
    }

    LFS_TRACE("lfs_fs_traverse(%p, %p, %p)", (void*)lfs, (void*)(uintptr_t)cb, data);

    err = lfs_fs_rawtraverse(lfs, cb, data, true);

    LFS_TRACE("lfs_fs_traverse -> %d", err);
    LFS_UNLOCK(lfs->cfg);
    return err;
}

int lfs_fs_grow(lfs_t* lfs, lfs_size_t block_count) {
    int err = LFS_LOCK(lfs->cfg);
    if (err) {
        return err;
    }
    LFS_TRACE("lfs_fs_grow(%p, %"PRIu32")", (void*)lfs, block_count);

    err = lfs_fs_rawgrow(lfs, block_count);

    LFS_TRACE("lfs_fs_grow -> %d", err);
    LFS_UNLOCK(lfs->cfg);
    return err;
}
