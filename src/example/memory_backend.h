#pragma once

struct vfs_memory_context : fs::lfsVFS::VFSContext {

    std::shared_ptr<void> _lfs_handle;
    std::vector<uint8_t> mem_fs;

    vfs_memory_context(std::shared_ptr<void> lfs_handle)
        : _lfs_handle(lfs_handle) {}

};

static int vfs_memory_allocate_file_size(lfs_config_t* config, size_t blocks) {

    vfs_memory_context* context = (vfs_memory_context*)(config->context);

    context->mem_fs.resize((config->block_size * blocks));

    return LFS_ERR_OK;
}

static int vfs_memory_block_device_read(const lfs_config_t* config, lfs_block_t block,
    lfs_off_t off, void* buffer, lfs_size_t size) {

    vfs_memory_context* context = (vfs_memory_context*)(config->context);

    memcpy(buffer, &context->mem_fs[config->block_size * block + off], size);

    return LFS_ERR_OK;
}

static int vfs_memory_block_device_prog(const lfs_config_t* config, lfs_block_t block,
    lfs_off_t off, const void* buffer, lfs_size_t size) {

    vfs_memory_context* context = (vfs_memory_context*)(config->context);

    memcpy(&context->mem_fs[config->block_size * block + off], buffer, size);

    return LFS_ERR_OK;
}

static int vfs_memory_block_device_erase(const lfs_config_t* config, lfs_block_t block) {
    return LFS_ERR_OK;
}

static int vfs_memory_allocate_block(lfs_config_t* config) {

    vfs_memory_context* context = (vfs_memory_context*)(config->context);

    if (config->on_grow) {
        return LFS_ERR_NOSPC;
    }

    config->on_grow = true;

    int i = vfs_memory_allocate_file_size(config, config->block_count + 10);
    lfs_fs_grow((lfs_t*)context->_lfs_handle.get(), config->block_count + 10);

    config->on_grow = false;

    return LFS_ERR_OK;
}

static int vfs_memory_block_device_sync(const lfs_config_t* config) {
    return LFS_ERR_OK;
}
