#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

/// Version info ///

// Software library version
// Major (top-nibble), incremented on backwards incompatible changes
// Minor (bottom-nibble), incremented on feature additions
constexpr uint32_t LFS_VERSION = 0x00020005;
constexpr uint32_t LFS_VERSION_MAJOR = (0xffff & (LFS_VERSION >> 16));
constexpr uint32_t LFS_VERSION_MINOR = (0xffff & (LFS_VERSION >>  0));

// Version of On-disk data structures
// Major (top-nibble), incremented on backwards incompatible changes
// Minor (bottom-nibble), incremented on feature additions
constexpr uint32_t LFS_DISK_VERSION = 0x00020000;
constexpr uint32_t LFS_DISK_VERSION_MAJOR = (0xffff & (LFS_DISK_VERSION >> 16));
constexpr uint32_t LFS_DISK_VERSION_MINOR = (0xffff & (LFS_DISK_VERSION >>  0));


/// Definitions ///

// Type definitions
typedef uint64_t lfs_size_t;
typedef uint64_t lfs_off_t;

typedef int64_t  lfs_ssize_t;
typedef int64_t  lfs_soff_t;

typedef uint64_t lfs_block_t;

// operations on 32-bit entry tags
typedef uint32_t lfs_tag_t;
typedef int32_t lfs_stag_t;


// Maximum name size in bytes, may be redefined to reduce the size of the
// info struct. Limited to <= 1022. Stored in superblock and must be
// respected by other littlefs drivers.
constexpr uint32_t LFS_NAME_MAX = 255;

// Maximum size of a file in bytes, may be redefined to limit to support other
// drivers. Limited on disk to <= 4294967296. However, above 2147483647 the
// functions lfs_file_seek, lfs_file_size, and lfs_file_tell will return
// incorrect values due to using signed integers. Stored in superblock and
// must be respected by other littlefs drivers.
constexpr uint64_t LFS_FILE_MAX = 0x7fffffffffffffff;

// Maximum size of custom attributes in bytes, may be redefined, but there is
// no real benefit to using a smaller LFS_ATTR_MAX. Limited to <= 1022.
constexpr uint64_t LFS_ATTR_MAX = 1022;

// some constants used throughout the code
constexpr lfs_block_t LFS_BLOCK_NULL = ((lfs_block_t)-1);
constexpr lfs_block_t LFS_BLOCK_INLINE = ((lfs_block_t)-2);

enum {
    LFS_OK_RELOCATED = 1,
    LFS_OK_DROPPED = 2,
    LFS_OK_ORPHANED = 3,
};

enum {
    LFS_CMP_EQ = 0,
    LFS_CMP_LT = 1,
    LFS_CMP_GT = 2,
};

// Possible error codes, these are negative to allow
// valid positive return values
enum lfs_error {
    LFS_ERR_OK = 0,    // No error
    LFS_ERR_IO = -5,   // Error during device operation
    LFS_ERR_CORRUPT = -84,  // Corrupted
    LFS_ERR_NOENT = -2,   // No directory entry
    LFS_ERR_EXIST = -17,  // Entry already exists
    LFS_ERR_NOTDIR = -20,  // Entry is not a dir
    LFS_ERR_ISDIR = -21,  // Entry is a dir
    LFS_ERR_NOTEMPTY = -39,  // Dir is not empty
    LFS_ERR_BADF = -9,   // Bad file number
    LFS_ERR_FBIG = -27,  // File too large
    LFS_ERR_INVAL = -22,  // Invalid parameter
    LFS_ERR_NOSPC = -28,  // No space left on device
    LFS_ERR_NOMEM = -12,  // No more memory available
    LFS_ERR_NOATTR = -61,  // No data/attr available
    LFS_ERR_NAMETOOLONG = -36,  // File name too long
};

// File types
enum lfs_type {
    // file types
    LFS_TYPE_REG = 0x001,           //
    LFS_TYPE_DIR = 0x002,           //

    // internally used types
    LFS_FROM_NOOP = 0x000,          //
    LFS_TYPE_NAME = 0x000,          //
    LFS_TYPE_FROM = 0x100,          //
    LFS_FROM_MOVE = 0x101,          //
    LFS_FROM_USERATTRS = 0x102,     //

    LFS_TYPE_STRUCT = 0x200,        //
    LFS_TYPE_USERATTR = 0x300,      //
    LFS_TYPE_SPLICE = 0x400,        //
    LFS_TYPE_CRC = 0x500,           //
    LFS_TYPE_TAIL = 0x600,          //
    LFS_TYPE_GLOBALS = 0x700,       //

    // internally used type specializations
    LFS_TYPE_SUPERBLOCK   = 0x0ff,  //
    LFS_TYPE_DIRSTRUCT    = 0x200,  //
    LFS_TYPE_CTZSTRUCT    = 0x202,  //
    LFS_TYPE_INLINESTRUCT = 0x201,  //
    LFS_TYPE_CREATE       = 0x401,  //
    LFS_TYPE_DELETE       = 0x4ff,  //
    LFS_TYPE_SOFTTAIL     = 0x600,  //
    LFS_TYPE_HARDTAIL     = 0x601,  //
    LFS_TYPE_MOVESTATE    = 0x7ff,  //
    LFS_TYPE_HAS_ORPHANS  = 0x800,  //
};

// File open flags
enum lfs_open_flags {

    // open flags
    LFS_O_RDONLY = 1,         // Open a file as read only
    LFS_O_WRONLY = 2,         // Open a file as write only
    LFS_O_RDWR = 3,         // Open a file as read and write
    LFS_O_CREAT = 0x0100,    // Create a file if it does not exist
    LFS_O_EXCL = 0x0200,    // Fail if a file already exists
    LFS_O_TRUNC = 0x0400,    // Truncate the existing file to zero size
    LFS_O_APPEND = 0x0800,    // Move to end of file on every write

    // internally used flags
    LFS_F_DIRTY = 0x010000, // File does not match storage
    LFS_F_WRITING = 0x020000, // File has been written since last flush
    LFS_F_READING = 0x040000, // File has been read since last flush
    LFS_F_ERRED = 0x080000, // An error occurred during write
    LFS_F_INLINE = 0x100000, // Currently inlined in directory entry
};

// File seek flags
enum lfs_whence_flags {
    LFS_SEEK_SET = 0,   // Seek relative to an absolute position
    LFS_SEEK_CUR = 1,   // Seek relative to the current file position
    LFS_SEEK_END = 2,   // Seek relative to the end of the file
};

struct lfs_config_t;
struct lfs_metadata_attribute_t;
struct lfs_disk_offset_t;
struct lfs_dir_traverse_t;
struct lfs_dir_find_match_t;
struct lfs_commit_t;
struct lfs_dir_commit_commit_t;
struct lfs_fs_parent_match_t;
struct lfs_info;
struct lfs_user_attribute_t;
struct lfs_file_config_t;
struct lfs_cache_t;
struct lfs_metadata_dir_t;
struct lfs_metadata_list_t;
struct lfs_dir_t;
struct lfs_ctz_t;
struct lfs_file_t;
struct lfs_superblock_t;
struct lfs_gstate_t;
struct lfs_free_t;
struct lfs_t;

// Configuration provided during initialization of the littlefs
struct lfs_config_t {
    // Opaque user provided context that can be used to pass
    // information to the block device operations
    void* context;
    bool on_grow;

    // Read a region in a block. Negative error codes are propagated
    // to the user.
    int (*read)(const lfs_config_t* c, lfs_block_t block,
        lfs_off_t offset, void* buffer, lfs_size_t size);

    // Program a region in a block. The block must have previously
    // been erased. Negative error codes are propagated to the user.
    // May return LFS_ERR_CORRUPT if the block should be considered bad.
    int (*write)(const lfs_config_t* c, lfs_block_t block,
        lfs_off_t offset, const void* buffer, lfs_size_t size);

    // Erase a block. A block must be erased before being programmed.
    // The state of an erased block is undefined. Negative error codes
    // are propagated to the user.
    // May return LFS_ERR_CORRUPT if the block should be considered bad.
    int (*erase)(const lfs_config_t* c, lfs_block_t block);

    int (*allocate_block)(lfs_config_t* c);

    // Sync the state of the underlying block device. Negative error codes
    // are propagated to the user.
    int (*sync)(const lfs_config_t* c);

    // Lock the underlying block device. Negative error codes
    // are propagated to the user.
    int (*lock)(const lfs_config_t* c);

    // Unlock the underlying block device. Negative error codes
    // are propagated to the user.
    int (*unlock)(const lfs_config_t* c);

    // Minimum size of a block read in bytes. All read operations will be a
    // multiple of this value.
    lfs_size_t read_size;

    // Minimum size of a block program in bytes. All program operations will be
    // a multiple of this value.
    lfs_size_t write_size;

    // Size of an erase operation in bytes. This must be a multiple of the
    // read and program sizes.
    //
    // If zero, the block_size is used as the erase_size. This is mostly for
    // backwards compatibility.
    lfs_size_t erase_size;

    // Size of an erasable block in bytes. This does not impact ram consumption
    // and may be larger than the physical erase size. However, non-inlined
    // files take up at minimum one block. Must be a multiple of the read and
    // program sizes.
    lfs_size_t block_size;

    // Number of erasable blocks on the device.
    lfs_size_t block_count;

    // Number of erase cycles before littlefs evicts metadata logs and moves
    // the metadata to another block. Suggested values are in the
    // range 100-1000, with large values having better performance at the cost
    // of less consistent wear distribution.
    //
    // Set to -1 to disable block-level wear-leveling.
    int32_t block_cycles;

    // Size of block caches in bytes. Each cache buffers a portion of a block in
    // RAM. The littlefs needs a read cache, a program cache, and one additional
    // cache per file. Larger caches can improve performance by storing more
    // data and reducing the number of disk accesses. Must be a multiple of the
    // read and program sizes, and a factor of the block size.
    lfs_size_t cache_size;

    // Size of the lookahead buffer in bytes. A larger lookahead buffer
    // increases the number of blocks found during an allocation pass. The
    // lookahead buffer is stored as a compact bitmap, so each byte of RAM
    // can track 8 blocks. Must be a multiple of 8.
    lfs_size_t lookahead_size;

    // Optional statically allocated read buffer. Must be cache_size.
    // By default lfs_malloc is used to allocate this buffer.
    void* read_buffer;

    // Optional statically allocated program buffer. Must be cache_size.
    // By default lfs_malloc is used to allocate this buffer.
    void* write_buffer;

    // Optional statically allocated lookahead buffer. Must be lookahead_size
    // and aligned to a 32-bit boundary. By default lfs_malloc is used to
    // allocate this buffer.
    void* lookahead_buffer;

    // Optional upper limit on length of file names in bytes. No downside for
    // larger names except the size of the info struct which is controlled by
    // the LFS_NAME_MAX define. Defaults to LFS_NAME_MAX when zero. Stored in
    // superblock and must be respected by other littlefs drivers.
    lfs_size_t name_max_length;

    // Optional upper limit on files in bytes. No downside for larger files
    // but must be <= LFS_FILE_MAX. Defaults to LFS_FILE_MAX when zero. Stored
    // in superblock and must be respected by other littlefs drivers.
    lfs_size_t file_max_size;

    // Optional upper limit on custom attributes in bytes. No downside for
    // larger attributes size but must be <= LFS_ATTR_MAX. Defaults to
    // LFS_ATTR_MAX when zero.
    lfs_size_t attr_max_size;

    // Optional upper limit on total space given to metadata pairs in bytes. On
    // devices with large blocks (e.g. 128kB) setting this to a low size (2-8kB)
    // can help bound the metadata compaction time. Must be <= block_size.
    // Defaults to block_size when zero.
    lfs_size_t metadata_max;
};

// operations on attributes in attribute lists

//Discarebes metadata attribute
struct lfs_metadata_attribute_t {

    //attribute tag
    lfs_tag_t tag;

    //
    const void* buffer;
};

struct lfs_disk_offset_t {

    lfs_block_t block;
    lfs_off_t offset;
};

struct lfs_dir_traverse_t {

    lfs_metadata_dir_t* dir;
    lfs_off_t offset;
    lfs_tag_t ptag;

    const lfs_metadata_attribute_t* attrs;

    int attrcount;

    lfs_tag_t tmask;
    lfs_tag_t ttag;
    uint16_t begin;
    uint16_t end;
    int16_t diff;

    int (*cb)(void* data, lfs_tag_t tag, const void* buffer);
    void* data;

    lfs_tag_t tag;
    const void* buffer;
    lfs_disk_offset_t disk;

};

struct lfs_dir_find_match_t {

    lfs_t* lfs;
    const void* name;
    lfs_size_t size;

};

struct lfs_commit_t {

    lfs_block_t block;
    lfs_off_t offset;
    lfs_tag_t ptag;
    uint32_t crc;

    lfs_off_t begin;
    lfs_off_t end;

};

struct lfs_dir_commit_commit_t {

    lfs_t* lfs;
    lfs_commit_t* commit;

};

struct lfs_fs_parent_match_t {

    lfs_t* lfs;
    const lfs_block_t pair[2];

};

// File info structure
struct lfs_info {
    // Type of the file, either LFS_TYPE_REG or LFS_TYPE_DIR
    uint8_t type;

    // Size of the file, only valid for REG files. Limited to 32-bits.
    lfs_size_t size;

    // Name of the file stored as a null-terminated string. Limited to
    // LFS_NAME_MAX+1, which can be changed by redefining LFS_NAME_MAX to
    // reduce RAM. LFS_NAME_MAX is stored in superblock and must be
    // respected by other littlefs drivers.
    char name[LFS_NAME_MAX + 1];
};

// Filesystem info structure
//
// Some of these can also be found in lfs_config, but the values here respect
// what was stored in the superblock during lfs_format.
struct lfs_fsinfo {
    // Size of a logical block in bytes.
    lfs_size_t block_size;

    // Number of logical blocks on the block device.
    lfs_size_t block_count;

    // Number of blocks in use, this is the same as lfs_fs_size.
    //
    // Note: Result is best effort. If files share COW structures, the returned
    // size may be larger than the filesystem actually is.
    lfs_size_t block_usage;

    // Upper limit on the length of file names in bytes.
    lfs_size_t name_max;

    // Upper limit on the size of files in bytes.
    lfs_size_t file_max;

    // Upper limit on the size of custom attributes in bytes.
    lfs_size_t attr_max;
};


// Custom attribute structure, used to describe custom attributes
// committed atomically during file writes.
struct lfs_user_attribute_t {
    // 8-bit type of attribute, provided by user and used to
    // identify the attribute
    uint8_t type;

    // Pointer to buffer containing the attribute
    void* buffer;

    // Size of attribute in bytes, limited to LFS_ATTR_MAX
    lfs_size_t size;
};

// Optional configuration provided during lfs_file_opencfg
struct lfs_file_config_t {

    // Optional statically allocated file buffer. Must be cache_size.
    // By default lfs_malloc is used to allocate this buffer.
    void* buffer;

    // Optional list of custom attributes related to the file. If the file
    // is opened with read access, these attributes will be read from disk
    // during the open call. If the file is opened with write access, the
    // attributes will be written to disk every file sync or close. This
    // write occurs atomically with update to the file's contents.
    //
    // Custom attributes are uniquely identified by an 8-bit type and limited
    // to LFS_ATTR_MAX bytes. When read, if the stored attribute is smaller
    // than the buffer, it will be padded with zeros. If the stored attribute
    // is larger, then it will be silently truncated. If the attribute is not
    // found, it will be created implicitly.
    lfs_user_attribute_t* attrs;

    // Number of custom attributes in the list
    lfs_size_t attr_count;

};


/// internal littlefs data structures ///
struct lfs_cache_t {

    /*
        id of cached block
    */
    lfs_block_t block;

    /*
        offset in block for start of cache
    */
    lfs_off_t offset;

    /*
        Size of cache
    */
    lfs_size_t size;

    /*
        Ptr to buffer with cached 
    */
    uint8_t* buffer;

};

//Discrabes metadata entry
struct lfs_metadata_dir_t {

    lfs_block_t pair[2];

    /*
        revision count, count of entries ?!
    */
    uint32_t revision_count;

    /*
        
    */
    lfs_off_t offset;

    /*
        
    */
    uint32_t etag;

    /*
        Blocks referenced count
    */
    uint16_t count;

    /*
        Is valid entry
    */
    bool erased;

    /*
        Chained with another metadata block
    */
    bool split;

    lfs_block_t tail[2];

};


struct lfs_ctz_t {
    lfs_block_t head;
    lfs_size_t size;
};

// littlefs directory type

struct lfs_metadata_list_t {

    lfs_metadata_list_t* next;

    /*
        entry id
    */
    uint16_t id;

    /*
        entry type (LFS_TYPE_REG || LFS_TYPE_DIR)
    */
    uint8_t type;

    lfs_metadata_dir_t metadata;
};

struct lfs_dir_t :
    public lfs_metadata_list_t {

    lfs_off_t pos;

    //first directory block pair
    lfs_block_t head[2];
};


// littlefs file type
struct lfs_file_t :
    public lfs_metadata_list_t {

    /*
        
    */
    lfs_ctz_t ctz;

    /*
        LFS_F_DIRTY
        LFS_F_WRITING
        LFS_F_READING
        LFS_F_ERRED
        LFS_F_INLINE
    */
    uint32_t flags;

    /*
        
    */
    lfs_off_t pos;

    /*
        block id where hold file data 
    */
    lfs_block_t block;

    /*
        data offset from block
    */
    lfs_off_t offset;

    /*
        file io cache
    */
    lfs_cache_t cache;

    /*
        root config
    */
    const lfs_file_config_t* cfg;
};

struct lfs_superblock_t {

    uint32_t version;
    lfs_size_t block_size;
    lfs_size_t block_count;
    lfs_size_t name_max_length;
    lfs_size_t file_max_size;
    lfs_size_t attr_max_size;
};

struct lfs_gstate_t {

    uint32_t tag;
    lfs_block_t pair[2];

};

struct lfs_free_t {

    lfs_block_t offset;
    lfs_block_t size;
    lfs_block_t i;
    lfs_block_t ack;
    uint64_t* buffer;

};

// The littlefs filesystem type

struct lfs_t {
    
    lfs_cache_t read_cache;
    lfs_cache_t write_cache;

    lfs_block_t root[2];

    // affected metadata list
    lfs_metadata_list_t* metadata_list;

    uint32_t seed;

    lfs_gstate_t gstate;
    lfs_gstate_t gdisk;
    lfs_gstate_t gdelta;

    lfs_free_t free;

    lfs_config_t* cfg;

    lfs_size_t erase_size;
    lfs_size_t block_size;
    lfs_size_t block_count;

    lfs_size_t name_max_length;
    lfs_size_t file_max_size;
    lfs_size_t attr_max_size;
};



#include "lfs_utility.h"

//allocator
int lfs_alloc_lookahead(void* p, lfs_block_t block);
void lfs_alloc_ack(lfs_t* lfs);
void lfs_alloc_drop(lfs_t* lfs);
int lfs_alloc(lfs_t* lfs, lfs_block_t* block);

//device
int lfs_bd_read(lfs_t* lfs,
    const lfs_cache_t* write_cache, lfs_cache_t* read_cache, lfs_size_t hint,
    lfs_block_t block, lfs_off_t offset,
    void* buffer, lfs_size_t size);
int lfs_bd_cmp(lfs_t* lfs,
    const lfs_cache_t* write_cache, lfs_cache_t* read_cache, lfs_size_t hint,
    lfs_block_t block, lfs_off_t offset,
    const void* buffer, lfs_size_t size);
int lfs_bd_flush(lfs_t* lfs, lfs_cache_t* write_cache, lfs_cache_t* read_cache, bool validate);
int lfs_bd_sync(lfs_t* lfs, lfs_cache_t* write_cache, lfs_cache_t* read_cache, bool validate);
int lfs_bd_write(lfs_t* lfs,
    lfs_cache_t* write_cache, lfs_cache_t* read_cache, bool validate,
    lfs_block_t block, lfs_off_t offset,
    const void* buffer, lfs_size_t size);
int lfs_bd_erase(lfs_t* lfs, lfs_block_t block);

//metadata
lfs_stag_t lfs_dir_getslice(lfs_t* lfs, const lfs_metadata_dir_t* dir,
    lfs_tag_t gmask, lfs_tag_t gtag,
    lfs_off_t goff, void* gbuffer, lfs_size_t gsize);
lfs_stag_t lfs_dir_get(lfs_t* lfs, const lfs_metadata_dir_t* dir, lfs_tag_t gmask, lfs_tag_t gtag, void* buffer);
int lfs_dir_getread(lfs_t* lfs, const lfs_metadata_dir_t* dir,
    const lfs_cache_t* write_cache, lfs_cache_t* read_cache, lfs_size_t hint,
    lfs_tag_t gmask, lfs_tag_t gtag,
    lfs_off_t offset, void* buffer, lfs_size_t size);
int lfs_dir_traverse_filter(void* p, lfs_tag_t tag, const void* buffer);
int lfs_dir_traverse(lfs_t* lfs,
    const lfs_metadata_dir_t* dir, lfs_off_t offset, lfs_tag_t ptag,
    const lfs_metadata_attribute_t* attrs, int attrcount,
    lfs_tag_t tmask, lfs_tag_t ttag,
    uint16_t begin, uint16_t end, int16_t diff,
    int (*cb)(void* data, lfs_tag_t tag, const void* buffer), void* data);
lfs_stag_t lfs_dir_fetchmatch(lfs_t* lfs,
    lfs_metadata_dir_t* dir, const lfs_block_t pair[2],
    lfs_tag_t fmask, lfs_tag_t ftag, uint16_t* id,
    int (*cb)(void* data, lfs_tag_t tag, const void* buffer), void* data);
int lfs_dir_fetch(lfs_t* lfs, lfs_metadata_dir_t* dir, const lfs_block_t pair[2]);
int lfs_dir_getgstate(lfs_t* lfs, const lfs_metadata_dir_t* dir, lfs_gstate_t* gstate);
int lfs_dir_getinfo(lfs_t* lfs, lfs_metadata_dir_t* dir, uint16_t id, lfs_info* info);
int lfs_dir_find_match(void* data, lfs_tag_t tag, const void* buffer);
lfs_stag_t lfs_dir_find(lfs_t* lfs, lfs_metadata_dir_t* dir, const char** path, uint16_t* id);


//commit
int lfs_dir_commit_write(lfs_t* lfs, lfs_commit_t* commit, const void* buffer, lfs_size_t size);
int lfs_dir_commit_attribute(lfs_t* lfs, lfs_commit_t* commit, lfs_tag_t tag, const void* buffer);
int lfs_dir_commit_crc(lfs_t* lfs, lfs_commit_t* commit);
int lfs_dir_alloc(lfs_t* lfs, lfs_metadata_dir_t* dir);
int lfs_dir_drop(lfs_t* lfs, lfs_metadata_dir_t* dir, lfs_metadata_dir_t* tail);
int lfs_dir_split(lfs_t* lfs,
    lfs_metadata_dir_t* dir, const lfs_metadata_attribute_t* attrs, int attrcount,
    lfs_metadata_dir_t* source, uint16_t split, uint16_t end);
int lfs_dir_commit_size(void* p, lfs_tag_t tag, const void* buffer);
int lfs_dir_commit_commit(void* p, lfs_tag_t tag, const void* buffer);
bool lfs_dir_needs_relocation(lfs_t* lfs, lfs_metadata_dir_t* dir);
int lfs_dir_compact(lfs_t* lfs,
    lfs_metadata_dir_t* dir, const lfs_metadata_attribute_t* attrs, int attrcount,
    lfs_metadata_dir_t* source, uint16_t begin, uint16_t end);
int lfs_dir_splittingcompact(lfs_t* lfs, lfs_metadata_dir_t* dir,
    const lfs_metadata_attribute_t* attrs, int attrcount,
    lfs_metadata_dir_t* source, uint16_t begin, uint16_t end);
int lfs_dir_relocating_commit(lfs_t* lfs, lfs_metadata_dir_t* dir,
    const lfs_block_t pair[2], const lfs_metadata_attribute_t* attrs, int attrcount, lfs_metadata_dir_t* pdir);
int lfs_dir_orphaning_commit(lfs_t* lfs, lfs_metadata_dir_t* dir, const lfs_metadata_attribute_t* attrs, int attrcount);
int lfs_dir_commit(lfs_t* lfs, lfs_metadata_dir_t* dir, const lfs_metadata_attribute_t* attrs, int attrcount);

//directory
int lfs_dir_rawcreate(lfs_t* lfs, const char* path);
int lfs_dir_rawopen(lfs_t* lfs, lfs_dir_t* dir, const char* path);
int lfs_dir_rawclose(lfs_t* lfs, lfs_dir_t* dir);
int lfs_dir_rawread(lfs_t* lfs, lfs_dir_t* dir, lfs_info* info);
int lfs_dir_rawseek(lfs_t* lfs, lfs_dir_t* dir, lfs_off_t offset);
lfs_soff_t lfs_dir_rawtell(lfs_t* lfs, lfs_dir_t* dir);
int lfs_dir_rawrewind(lfs_t* lfs, lfs_dir_t* dir);

//file index
int lfs_ctz_index(lfs_t* lfs, lfs_off_t* offset);
int lfs_ctz_find(lfs_t* lfs,
    const lfs_cache_t* write_cache, lfs_cache_t* read_cache,
    lfs_block_t head, lfs_size_t size,
    lfs_size_t pos, lfs_block_t* block, lfs_off_t* offset);
int lfs_ctz_extend(lfs_t* lfs,
    lfs_cache_t* write_cache, lfs_cache_t* read_cache,
    lfs_block_t head, lfs_size_t size,
    lfs_block_t* block, lfs_off_t* offset);
int lfs_ctz_traverse(lfs_t* lfs,
    const lfs_cache_t* write_cache, lfs_cache_t* read_cache,
    lfs_block_t head, lfs_size_t size,
    int (*cb)(void*, lfs_block_t), void* data);

//file
int lfs_file_rawopencfg(lfs_t* lfs, lfs_file_t* file, const char* path, int flags, const lfs_file_config_t* cfg);
int lfs_file_rawopen(lfs_t* lfs, lfs_file_t* file, const char* path, int flags);
int lfs_file_rawclose(lfs_t* lfs, lfs_file_t* file);
int lfs_file_relocate(lfs_t* lfs, lfs_file_t* file);
int lfs_file_outline(lfs_t* lfs, lfs_file_t* file);
int lfs_file_flush(lfs_t* lfs, lfs_file_t* file);
int lfs_file_rawsync(lfs_t* lfs, lfs_file_t* file);
lfs_ssize_t lfs_file_flushedread(lfs_t* lfs, lfs_file_t* file, void* buffer, lfs_size_t size);
lfs_ssize_t lfs_file_rawread(lfs_t* lfs, lfs_file_t* file, void* buffer, lfs_size_t size);
lfs_ssize_t lfs_file_flushedwrite(lfs_t* lfs, lfs_file_t* file, const void* buffer, lfs_size_t size);
lfs_ssize_t lfs_file_rawwrite(lfs_t* lfs, lfs_file_t* file, const void* buffer, lfs_size_t size);
lfs_soff_t lfs_file_rawseek(lfs_t* lfs, lfs_file_t* file, lfs_soff_t offset, int whence);
lfs_soff_t lfs_file_rawtruncate(lfs_t* lfs, lfs_file_t* file, lfs_off_t size);
lfs_soff_t lfs_file_rawtell(lfs_t* lfs, lfs_file_t* file);
lfs_soff_t lfs_file_rawrewind(lfs_t* lfs, lfs_file_t* file);
lfs_soff_t lfs_file_rawsize(lfs_t* lfs, lfs_file_t* file);

//general
int lfs_raw_stat(lfs_t* lfs, const char* path, lfs_info* info);
int lfs_raw_remove(lfs_t* lfs, const char* path);
int lfs_raw_rename(lfs_t* lfs, const char* oldpath, const char* newpath);
lfs_ssize_t lfs_raw_get_attribute(lfs_t* lfs, const char* path, uint8_t type, void* buffer, lfs_size_t size);
int lfs_commit_attribute(lfs_t* lfs, const char* path, uint8_t type, const void* buffer, lfs_size_t size);
int lfs_raw_set_attribute(lfs_t* lfs, const char* path, uint8_t type, const void* buffer, lfs_size_t size);
int lfs_raw_remove_attribute(lfs_t* lfs, const char* path, uint8_t type);
int lfs_raw_format(lfs_t* lfs, lfs_config_t* cfg);
int lfs_raw_mount(lfs_t* lfs, lfs_config_t* cfg);
int lfs_raw_unmount(lfs_t* lfs);

//operations
int lfs_fs_rawtraverse(lfs_t* lfs, int (*cb)(void* data, lfs_block_t block), void* data, bool includeorphans);
int lfs_fs_pred(lfs_t* lfs, const lfs_block_t pair[2], lfs_metadata_dir_t* pdir);
int lfs_fs_parent_match(void* data, lfs_tag_t tag, const void* buffer);
lfs_stag_t lfs_fs_parent(lfs_t* lfs, const lfs_block_t pair[2], lfs_metadata_dir_t* parent);
int lfs_fs_preporphans(lfs_t* lfs, int8_t orphans);
void lfs_fs_prepmove(lfs_t* lfs, uint16_t id, const lfs_block_t pair[2]);
int lfs_fs_demove(lfs_t* lfs);
int lfs_fs_deorphan(lfs_t* lfs, bool powerloss);
int lfs_fs_forceconsistency(lfs_t* lfs);
int lfs_fs_size_count(void* p, lfs_block_t block);
lfs_ssize_t lfs_fs_rawsize(lfs_t* lfs);
int lfs_fs_rawstat(lfs_t* lfs, struct lfs_fsinfo* fsinfo);
int lfs_fs_rawgrow(lfs_t* lfs, lfs_size_t block_count);




/// Filesystem functions ///

// Format a block device with the littlefs
//
// Requires a littlefs object and config struct. This clobbers the littlefs
// object, and does not leave the filesystem mounted. The config struct must
// be zeroed for defaults and backwards compatibility.
//
// Returns a negative error code on failure.
int lfs_format(lfs_t* lfs, lfs_config_t* config);

// Mounts a littlefs
//
// Requires a littlefs object and config struct. Multiple filesystems
// may be mounted simultaneously with multiple littlefs objects. Both
// lfs and config must be allocated while mounted. The config struct must
// be zeroed for defaults and backwards compatibility.
//
// Returns a negative error code on failure.
int lfs_mount(lfs_t* lfs, lfs_config_t* config);

// Unmounts a littlefs
//
// Does nothing besides releasing any allocated resources.
// Returns a negative error code on failure.
int lfs_unmount(lfs_t* lfs);

/// General operations ///

// Removes a file or directory
//
// If removing a directory, the directory must be empty.
// Returns a negative error code on failure.
int lfs_remove(lfs_t* lfs, const char* path);

// Rename or move a file or directory
//
// If the destination exists, it must match the source in type.
// If the destination is a directory, the directory must be empty.
//
// Returns a negative error code on failure.
int lfs_rename(lfs_t* lfs, const char* oldpath, const char* newpath);

// Find info about a file or directory
//
// Fills out the info structure, based on the specified file or directory.
// Returns a negative error code on failure.
int lfs_stat(lfs_t* lfs, const char* path, struct lfs_info* info);

// Get a custom attribute
//
// Custom attributes are uniquely identified by an 8-bit type and limited
// to LFS_ATTR_MAX bytes. When read, if the stored attribute is smaller than
// the buffer, it will be padded with zeros. If the stored attribute is larger,
// then it will be silently truncated. If no attribute is found, the error
// LFS_ERR_NOATTR is returned and the buffer is filled with zeros.
//
// Returns the size of the attribute, or a negative error code on failure.
// Note, the returned size is the size of the attribute on disk, irrespective
// of the size of the buffer. This can be used to dynamically allocate a buffer
// or check for existence.
lfs_ssize_t lfs_get_attribute(lfs_t* lfs, const char* path,
    uint8_t type, void* buffer, lfs_size_t size);

// Set custom attributes
//
// Custom attributes are uniquely identified by an 8-bit type and limited
// to LFS_ATTR_MAX bytes. If an attribute is not found, it will be
// implicitly created.
//
// Returns a negative error code on failure.
int lfs_set_attribute(lfs_t* lfs, const char* path,
    uint8_t type, const void* buffer, lfs_size_t size);

// Removes a custom attribute
//
// If an attribute is not found, nothing happens.
//
// Returns a negative error code on failure.
int lfs_remove_attribute(lfs_t* lfs, const char* path, uint8_t type);

/// File operations ///

// Open a file
//
// The mode that the file is opened in is determined by the flags, which
// are values from the enum lfs_open_flags that are bitwise-ored together.
//
// Returns a negative error code on failure.
int lfs_file_open(lfs_t* lfs, lfs_file_t* file,
    const char* path, int flags);

// if LFS_NO_MALLOC is defined, lfs_file_open() will fail with LFS_ERR_NOMEM
// thus use lfs_file_opencfg() with config.buffer set.

// Open a file with extra configuration
//
// The mode that the file is opened in is determined by the flags, which
// are values from the enum lfs_open_flags that are bitwise-ored together.
//
// The config struct provides additional config options per file as described
// above. The config struct must remain allocated while the file is open, and
// the config struct must be zeroed for defaults and backwards compatibility.
//
// Returns a negative error code on failure.
int lfs_file_opencfg(lfs_t* lfs, lfs_file_t* file,
    const char* path, int flags,
    const lfs_file_config_t* config);

// Close a file
//
// Any pending writes are written out to storage as though
// sync had been called and releases any allocated resources.
//
// Returns a negative error code on failure.
int lfs_file_close(lfs_t* lfs, lfs_file_t* file);

// Synchronize a file on storage
//
// Any pending writes are written out to storage.
// Returns a negative error code on failure.
int lfs_file_sync(lfs_t* lfs, lfs_file_t* file);

// Read data from file
//
// Takes a buffer and size indicating where to store the read data.
// Returns the number of bytes read, or a negative error code on failure.
lfs_ssize_t lfs_file_read(lfs_t* lfs, lfs_file_t* file, void* buffer, lfs_size_t size);

// Write data to file
//
// Takes a buffer and size indicating the data to write. The file will not
// actually be updated on the storage until either sync or close is called.
//
// Returns the number of bytes written, or a negative error code on failure.
lfs_ssize_t lfs_file_write(lfs_t* lfs, lfs_file_t* file, const void* buffer, lfs_size_t size);

// Change the position of the file
//
// The change in position is determined by the offset and whence flag.
// Returns the new position of the file, or a negative error code on failure.
lfs_soff_t lfs_file_seek(lfs_t* lfs, lfs_file_t* file, lfs_soff_t offset, int whence);

// Truncates the size of the file to the specified size
//
// Returns a negative error code on failure.
lfs_soff_t lfs_file_truncate(lfs_t* lfs, lfs_file_t* file, lfs_off_t size);

// Return the position of the file
//
// Equivalent to lfs_file_seek(lfs, file, 0, LFS_SEEK_CUR)
// Returns the position of the file, or a negative error code on failure.
lfs_soff_t lfs_file_tell(lfs_t* lfs, lfs_file_t* file);

// Change the position of the file to the beginning of the file
//
// Equivalent to lfs_file_seek(lfs, file, 0, LFS_SEEK_SET)
// Returns a negative error code on failure.
lfs_soff_t lfs_file_rewind(lfs_t* lfs, lfs_file_t* file);

// Return the size of the file
//
// Similar to lfs_file_seek(lfs, file, 0, LFS_SEEK_END)
// Returns the size of the file, or a negative error code on failure.
lfs_soff_t lfs_file_size(lfs_t* lfs, lfs_file_t* file);


/// Directory operations ///

// Create a directory
//
// Returns a negative error code on failure.
int lfs_mkdir(lfs_t* lfs, const char* path);

// Open a directory
//
// Once open a directory can be used with read to iterate over files.
// Returns a negative error code on failure.
int lfs_dir_open(lfs_t* lfs, lfs_dir_t* dir, const char* path);

// Close a directory
//
// Releases any allocated resources.
// Returns a negative error code on failure.
int lfs_dir_close(lfs_t* lfs, lfs_dir_t* dir);

// Read an entry in the directory
//
// Fills out the info structure, based on the specified file or directory.
// Returns a positive value on success, 0 at the end of directory,
// or a negative error code on failure.
int lfs_dir_read(lfs_t* lfs, lfs_dir_t* dir, struct lfs_info* info);

// Change the position of the directory
//
// The new off must be a value previous returned from tell and specifies
// an absolute offset in the directory seek.
//
// Returns a negative error code on failure.
int lfs_dir_seek(lfs_t* lfs, lfs_dir_t* dir, lfs_off_t offset);

// Return the position of the directory
//
// The returned offset is only meant to be consumed by seek and may not make
// sense, but does indicate the current position in the directory iteration.
//
// Returns the position of the directory, or a negative error code on failure.
lfs_soff_t lfs_dir_tell(lfs_t* lfs, lfs_dir_t* dir);

// Change the position of the directory to the beginning of the directory
//
// Returns a negative error code on failure.
int lfs_dir_rewind(lfs_t* lfs, lfs_dir_t* dir);



/// Filesystem-level filesystem operations

// Find info about the filesystem
//
// Fills out the fsinfo structure. Returns a negative error code on failure.
int lfs_fs_stat(lfs_t* lfs, struct lfs_fsinfo* fsinfo);

// Finds the current size of the filesystem
//
// Note: Result is best effort. If files share COW structures, the returned
// size may be larger than the filesystem actually is.
//
// Returns the number of allocated blocks, or a negative error code on failure.
lfs_ssize_t lfs_fs_size(lfs_t* lfs);

// Traverse through all blocks in use by the filesystem
//
// The provided callback will be called with each block address that is
// currently in use by the filesystem. This can be used to determine which
// blocks are in use or how much of the storage is available.
//
// Returns a negative error code on failure.
int lfs_fs_traverse(lfs_t* lfs, int (*cb)(void*, lfs_block_t), void* data);

// Grows the filesystem to a new size, updating the superblock with the new
// block count.
//
// Note: This is irreversible.
//
// Returns a negative error code on failure.
int lfs_fs_grow(lfs_t* lfs, lfs_size_t block_count);