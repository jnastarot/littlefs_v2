#pragma once


struct vfs_file_context : fs::lfsVFS::VFSContext {

    std::shared_ptr<void> _lfs_handle;
    std::shared_ptr<FILE> _file_fs;

    vfs_file_context(std::shared_ptr<void> lfs_handle, std::shared_ptr<FILE> file_fs)
        : _lfs_handle(lfs_handle)
        , _file_fs(file_fs) {}
};


static int vfs_file_allocate_file_size(lfs_config_t* config, size_t blocks) {

    vfs_file_context* context = (vfs_file_context*)(config->context);

    _fseeki64(
        context->_file_fs.get(),
        (config->block_size * blocks) - 1, SEEK_SET);

    uint8_t test = 0;

    int result = fwrite(
        &test,
        1, 1,
        context->_file_fs.get());

    fflush(context->_file_fs.get());

    return result == 1 ? LFS_ERR_OK : LFS_ERR_NOSPC;
}

static int vfs_file_block_device_read(const lfs_config_t* config, lfs_block_t block,
    lfs_off_t off, void* buffer, lfs_size_t size) {

    vfs_file_context* context = (vfs_file_context*)(config->context);

    int i = _fseeki64(
        context->_file_fs.get(),
        (config->block_size * block) + off,
        SEEK_SET);

    i = fread(buffer, size, 1, context->_file_fs.get());

    if (i == 0) {
        __debugbreak();
    }

    return i == 1 ? LFS_ERR_OK : LFS_ERR_CORRUPT;
}

static int vfs_file_block_device_prog(const lfs_config_t* config, lfs_block_t block,
    lfs_off_t off, const void* buffer, lfs_size_t size) {

    vfs_file_context* context = (vfs_file_context*)(config->context);

    int i = _fseeki64(
        context->_file_fs.get(),
        (config->block_size * block) + off,
        SEEK_SET);

    i = fwrite(
        buffer,
        size, 1,
        context->_file_fs.get());

    return i == 1 ? LFS_ERR_OK : LFS_ERR_CORRUPT;
}

static int vfs_file_block_device_erase(const lfs_config_t* config, lfs_block_t block) {
    return LFS_ERR_OK;
}

static int vfs_file_allocate_block(lfs_config_t* config) {

    vfs_file_context* context = (vfs_file_context*)(config->context);

    if (config->on_grow) {
        return LFS_ERR_NOSPC;
    }

    config->on_grow = true;

    int i = vfs_file_allocate_file_size(config, config->block_count + 10);
    lfs_fs_grow((lfs_t*)context->_lfs_handle.get(), config->block_count + 10);

    config->on_grow = false;

    return LFS_ERR_OK;
}

static int vfs_file_block_device_sync(const lfs_config_t* config) {
    return LFS_ERR_OK;
}