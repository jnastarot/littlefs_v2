#pragma once

#include <vector>
#include <string>
#include <memory>

#include "lfs.h"

namespace fs {

    struct IFileObject;
    struct IFileSystemDevice;

    enum ErrorCode {
        kCodeOK,
        kCodeFileNotFound,
        kCodeObjectNotCompatible,
        kCodeBadDevice,
        kCodeNoDeviceSpace,
        kCodeUnknownError
    };

    enum FileOpenFlags {
        kFileRead = 1,
        kFileWrite = 2,
        kFileCreateIfNotExists = 0x0100,
        kFileCreateFailIfExists = 0x0200,
        kFileTruncate = 0x0400,
        kFileAppend = 0x0800,
    };

    struct Entry {
        enum Type {
            kEntryFile,
            kEntryDirectory
        };
    private:
        Type _type;
        std::string _path;
        uint64_t _size;
    protected:
        Entry(Type type, const std::string& path, uint64_t size)
            : _type(type)
            , _path(path)
            , _size(size) {}
    public:
        constexpr Type getType() const {
            return _type;
        }
        constexpr const std::string& getPath() const {
            return _path;
        }
        constexpr uint64_t getSize() const {
            return _size;
        }
    };

    struct FileEntry
        : public Entry {
    public:
        FileEntry(const std::string& path, uint64_t size)
            : Entry(kEntryFile, path, size) {}
    };
    struct DirectoryEntry
        : public Entry {
    public:
        DirectoryEntry(const std::string& path, uint64_t size)
            : Entry(kEntryDirectory, path, size) {}
    };

    struct IFileObject {
    public:
        enum SeekType {
            kSeekSet = 0,
            kSeekCur = 1,
            kSeekEnd = 2,
        };
        std::string _path;

    public:
        virtual int64_t read(void* data, uint64_t size) = 0;
        virtual int64_t write(const void* data, uint64_t size) = 0;

        virtual int64_t truncate(uint64_t size) = 0;
        virtual int64_t seek(uint64_t offset, SeekType type) = 0;
        virtual int64_t tell() = 0;
        virtual int64_t size() = 0;
        virtual void flush() = 0;

    public:
        friend struct IFileDevice;
    };

    struct IFileSystemDevice {
    public:
        virtual std::vector<Entry> dir(const std::string& path) = 0;
        virtual ErrorCode openFile(std::shared_ptr<IFileObject>& handle, const std::string& path, uint32_t flags) = 0;
        virtual ErrorCode existsFile(const std::string& path) = 0;
        virtual ErrorCode deleteFile(const std::string& path) = 0;
        virtual ErrorCode deleteDirectory(const std::string& path) = 0;
    };


    struct lfsVFS
        : public IFileSystemDevice {

        enum Backend {
            kMemoryBackend,
            kFileBackend
        };

        struct VFSContext {};

    private:
        std::shared_ptr<lfs_t> _lfs_handle;
        std::shared_ptr<lfs_config_t> _lfs_config;
        std::shared_ptr<VFSContext> _lfs_context;

    public:
        lfsVFS(
            std::shared_ptr<lfs_t> lfs_handle,
            std::shared_ptr<lfs_config_t> lfs_config,
            std::shared_ptr<VFSContext> lfs_context
        );

        ~lfsVFS();

        std::vector<Entry> dir(const std::string& path) override;
        ErrorCode openFile(std::shared_ptr<IFileObject>& handle, const std::string& path, uint32_t flags) override;
        ErrorCode existsFile(const std::string& path) override;
        ErrorCode deleteFile(const std::string& path) override;
        ErrorCode deleteDirectory(const std::string& path) override;
    };

    ErrorCode openVFS(
        const std::wstring& path,
        std::shared_ptr< IFileSystemDevice>& filesystem,
        lfsVFS::Backend backend = lfsVFS::Backend::kMemoryBackend);
    ErrorCode createVFS(
        const std::wstring& path,
        std::shared_ptr< IFileSystemDevice>& filesystem,
        lfsVFS::Backend backend = lfsVFS::Backend::kMemoryBackend);
}