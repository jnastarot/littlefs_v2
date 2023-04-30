#include "lfs_interface.h"


#include "file_backend.h"
#include "memory_backend.h"


using namespace fs;

static ErrorCode lfsToHxErrorCode(int err) {

    switch (err) {
    case LFS_ERR_OK: { // No error
        return ErrorCode::kCodeOK;
    }
    case LFS_ERR_IO: // Error during device operation
    case LFS_ERR_CORRUPT: // Corrupted
    case LFS_ERR_BADF: {// Bad file number
        return ErrorCode::kCodeBadDevice;
    }

    case LFS_ERR_FBIG:  // File too large
    case LFS_ERR_NOSPC: { // No space left on device
        return ErrorCode::kCodeNoDeviceSpace;
    }

    case LFS_ERR_INVAL: { // Invalid parameter
        return ErrorCode::kCodeObjectNotCompatible;
    }

    default: {
        return ErrorCode::kCodeUnknownError;
    }
    }
}

lfsVFS::lfsVFS(
    std::shared_ptr<lfs_t> lfs_handle,
    std::shared_ptr<lfs_config_t> lfs_config,
    std::shared_ptr<VFSContext> lfs_context) {

    _lfs_handle = lfs_handle;
    _lfs_config = lfs_config;
    _lfs_context = lfs_context;
}

lfsVFS::~lfsVFS() {
    lfs_unmount(_lfs_handle.get());
}

struct VFSFileObject
    : public IFileObject {
    std::shared_ptr<lfs_t> _lfs_handle;
    lfs_file_t _file_handle;

    VFSFileObject(std::shared_ptr<lfs_t> lfs_handle)
        : _lfs_handle(lfs_handle)
        , _file_handle({}) {}

    ~VFSFileObject() {
        lfs_file_close(_lfs_handle.get(), &_file_handle);
    }

public:
    int64_t read(void* data, uint64_t size) override {
        return lfs_file_read(_lfs_handle.get(), &_file_handle, data, size);
    }
    int64_t write(const void* data, uint64_t size) override {
        return lfs_file_write(_lfs_handle.get(), &_file_handle, data, size);
    }
    int64_t truncate(uint64_t size) override {
        return lfs_file_truncate(_lfs_handle.get(), &_file_handle, size);
    }
    int64_t seek(uint64_t offset, SeekType type) override {

        int whence = 0;

        switch (type) {
        case kSeekSet: {
            whence = LFS_SEEK_SET; break;
        }
        case kSeekCur: {
            whence = LFS_SEEK_CUR; break;
        }
        case kSeekEnd: {
            whence = LFS_SEEK_END; break;
        }
        }

        return lfs_file_seek(_lfs_handle.get(), &_file_handle, offset, whence);
    }
    int64_t tell() override {
        return lfs_file_tell(_lfs_handle.get(), &_file_handle);
    }
    int64_t size() override {
        return lfs_file_size(_lfs_handle.get(), &_file_handle);
    }
    void flush() override {

    }

};

std::vector<Entry> lfsVFS::dir(const std::string& path) {

    std::vector<Entry> entries;

    lfs_dir_t dir;

    int err = lfs_dir_open(_lfs_handle.get(), &dir, path.c_str());

    if (err) {
        return entries;
    }

    struct lfs_info info;

    while (true) {

        int res = lfs_dir_read(_lfs_handle.get(), &dir, &info);

        if (res < 0) {
            break;
        }

        switch (info.type) {
        case LFS_TYPE_REG: {
            entries.push_back(FileEntry(info.name, info.size));
            break;
        }

        case LFS_TYPE_DIR: {

            if (std::string(info.name) == "." ||
                std::string(info.name) == "..") {

                continue;
            }

            entries.push_back(DirectoryEntry(info.name, info.size));
            break;
        }
        }
    }

    lfs_dir_close(_lfs_handle.get(), &dir);
    return entries;
}

ErrorCode lfsVFS::openFile(std::shared_ptr<IFileObject>& handle, const std::string& path, uint32_t flags) {

    uint32_t lfs_flags = 0;

    if (flags & kFileRead) { lfs_flags |= LFS_O_RDONLY; }
    if (flags & kFileWrite) { lfs_flags |= LFS_O_WRONLY; }
    if (flags & kFileCreateIfNotExists) { lfs_flags |= LFS_O_CREAT; }
    if (flags & kFileCreateFailIfExists) { lfs_flags |= LFS_O_EXCL; }
    if (flags & kFileTruncate) { lfs_flags |= LFS_O_TRUNC; }
    if (flags & kFileAppend) { lfs_flags |= LFS_O_APPEND; }

    std::shared_ptr<IFileObject> file_handle(new VFSFileObject(_lfs_handle));

    int err = lfs_file_open(
        _lfs_handle.get(),
        &static_cast<VFSFileObject*>(file_handle.get())->_file_handle,
        path.c_str(), lfs_flags);

    static_cast<VFSFileObject*>(file_handle.get())->_path = path;

    if (err != LFS_ERR_OK) {
        return lfsToHxErrorCode(err);
    }

    handle = file_handle;

    return lfsToHxErrorCode(err);
}

ErrorCode lfsVFS::existsFile(const std::string& path) {

    lfs_file_t _file_handle;

    int err = lfs_file_open(
        _lfs_handle.get(),
        &_file_handle,
        path.c_str(), LFS_O_RDONLY);

    if (err == LFS_ERR_OK) {

        lfs_file_close(_lfs_handle.get(), &_file_handle);
        return ErrorCode::kCodeOK;
    }

    return ErrorCode::kCodeFileNotFound;
}

ErrorCode lfsVFS::deleteFile(const std::string& path) {
    return lfsToHxErrorCode(lfs_remove(_lfs_handle.get(), path.c_str()));
}

ErrorCode lfsVFS::deleteDirectory(const std::string& path) {
    return lfsToHxErrorCode(lfs_remove(_lfs_handle.get(), path.c_str()));
}

ErrorCode fs::openVFS(const std::wstring& path, std::shared_ptr< IFileSystemDevice>& filesystem, lfsVFS::Backend backend) {

    std::shared_ptr< lfs_t> fs_handle(new lfs_t);
    std::shared_ptr< lfs_config_t> fs_config(new lfs_config_t);
    std::shared_ptr< lfsVFS::VFSContext> fs_context;

    if (backend == lfsVFS::Backend::kFileBackend) {

        FILE* file = nullptr;
        _wfopen_s(&file, path.c_str(), L"r+b");

        if (!file) {
            return ErrorCode::kCodeFileNotFound;
        }

        std::shared_ptr<FILE> file_handle(file, 
            [](FILE* file) {
                fclose(file);
            });

        fs_context = std::shared_ptr< lfsVFS::VFSContext>(
            new vfs_file_context(
                fs_handle,
                file_handle
            )
        );
    }
    else if (backend == lfsVFS::Backend::kMemoryBackend) {
        fs_context = std::shared_ptr< lfsVFS::VFSContext>(new vfs_memory_context(fs_handle));
    }
    else {
        return ErrorCode::kCodeObjectNotCompatible;
    }

    std::shared_ptr< IFileSystemDevice> _fs(
        std::make_shared<lfsVFS>(
            fs_handle,
            fs_config,
            fs_context
        )
    );

    {
        lfs_t* handle = fs_handle.get();

        memset(handle, 0, sizeof(lfs_t));
    }

    { //setup config
        lfs_config_t* config = fs_config.get();

        memset(config, 0, sizeof(lfs_config_t));

        config->context = fs_context.get();

        // block device operations
        if (backend == lfsVFS::Backend::kFileBackend) {
            config->read = vfs_file_block_device_read;
            config->write = vfs_file_block_device_prog;
            config->erase = vfs_file_block_device_erase;
            config->sync = vfs_file_block_device_sync;
            config->allocate_block = vfs_file_allocate_block;
            config->lock = 0;
            config->unlock = 0;
        }
        else if (backend == lfsVFS::Backend::kMemoryBackend) {
            config->read = vfs_memory_block_device_read;
            config->write = vfs_memory_block_device_prog;
            config->erase = vfs_memory_block_device_erase;
            config->sync = vfs_memory_block_device_sync;
            config->allocate_block = vfs_memory_allocate_block;
            config->lock = 0;
            config->unlock = 0;
        }

        // block device configuration
        config->read_size = 1;
        config->write_size = 1;
        config->block_size = (1024 * 64);
        config->block_count = 2;
        config->cache_size = config->block_size;
        config->erase_size = 0;
        config->lookahead_size = config->block_size;
        config->block_cycles = -1;
        config->file_max_size = 0x7fffffffffffffff;
        config->on_grow = false;
    }

    //setup context
    if (backend == lfsVFS::Backend::kFileBackend) {

        vfs_file_context* context = (vfs_file_context*)fs_context.get();
        lfs_config_t* config = fs_config.get();

        context->_lfs_handle = fs_handle;
    }
    else if (backend == lfsVFS::Backend::kMemoryBackend) {

        vfs_memory_context* context = (vfs_memory_context*)fs_context.get();
        lfs_config_t* config = fs_config.get();

        context->_lfs_handle = fs_handle;
    }

    {
        int err = lfs_mount(fs_handle.get(),fs_config.get());

        if (err) {

            return lfsToHxErrorCode(err);
        }
    }

    filesystem = _fs;

    return ErrorCode::kCodeOK;
}

ErrorCode fs::createVFS(const std::wstring& path, std::shared_ptr< IFileSystemDevice>& filesystem, lfsVFS::Backend backend) {

    std::shared_ptr< lfs_t> fs_handle(new lfs_t);
    std::shared_ptr< lfs_config_t> fs_config(new lfs_config_t);
    std::shared_ptr< lfsVFS::VFSContext> fs_context;

    if (backend == lfsVFS::Backend::kFileBackend) {

        FILE* file = nullptr;
        _wfopen_s(&file, path.c_str(), L"w+b");

        if (!file) {
            return ErrorCode::kCodeFileNotFound;
        }

        std::shared_ptr<FILE> file_handle(file, [](FILE* file) {
            fclose(file);
            });

        fs_context = std::shared_ptr< lfsVFS::VFSContext>(
            new vfs_file_context(
                fs_handle,
                file_handle
            )
        );
    }
    else if (backend == lfsVFS::Backend::kMemoryBackend) {
        fs_context = std::shared_ptr< lfsVFS::VFSContext>(new vfs_memory_context(fs_handle));
    }
    else {
        return ErrorCode::kCodeObjectNotCompatible;
    }

    std::shared_ptr< IFileSystemDevice> _fs(
        std::make_shared<lfsVFS>(
            fs_handle,
            fs_config,
            fs_context
        )
    );

    {
        lfs_t* handle = fs_handle.get();

        memset(handle, 0, sizeof(lfs_t));
    }

    { //setup config
        lfs_config_t* config = fs_config.get();

        memset(config, 0, sizeof(lfs_config_t));

        config->context = fs_context.get();

        // block device operations
        if (backend == lfsVFS::Backend::kFileBackend) {
            config->read = vfs_file_block_device_read;
            config->write = vfs_file_block_device_prog;
            config->erase = vfs_file_block_device_erase;
            config->sync = vfs_file_block_device_sync;
            config->allocate_block = vfs_file_allocate_block;
            config->lock = 0;
            config->unlock = 0;
        }
        else if (backend == lfsVFS::Backend::kMemoryBackend) {
            config->read = vfs_memory_block_device_read;
            config->write = vfs_memory_block_device_prog;
            config->erase = vfs_memory_block_device_erase;
            config->sync = vfs_memory_block_device_sync;
            config->allocate_block = vfs_memory_allocate_block;
            config->lock = 0;
            config->unlock = 0;
        }

        // block device configuration
        config->read_size = 1;
        config->write_size = 1;
        config->block_size = (1024 * 64);
        config->block_count = 2;
        config->cache_size = config->block_size;
        config->erase_size = 0;
        config->lookahead_size = config->block_size;
        config->block_cycles = -1;
        config->file_max_size = 0x7fffffffffffffff;
        config->on_grow = false;
    }

    //setup context
    if (backend == lfsVFS::Backend::kFileBackend) {

        vfs_file_context* context = (vfs_file_context*)fs_context.get();
        lfs_config_t* config = fs_config.get();

        context->_lfs_handle = fs_handle;

        vfs_file_allocate_file_size(config, 2);
    }
    else if (backend == lfsVFS::Backend::kMemoryBackend) {

        vfs_memory_context* context = (vfs_memory_context*)fs_context.get();
        lfs_config_t* config = fs_config.get();

        context->_lfs_handle = fs_handle;

        vfs_memory_allocate_file_size(config, 2);
    }

    {
        int err = lfs_format(fs_handle.get(), fs_config.get());

        if (err) {

            return lfsToHxErrorCode(err);
        }

        err = lfs_mount(fs_handle.get(),fs_config.get());

        if (err) {

            return lfsToHxErrorCode(err);
        }
    }

    filesystem = _fs;

    return ErrorCode::kCodeOK;
}