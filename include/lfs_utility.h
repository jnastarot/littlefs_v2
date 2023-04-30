#pragma once


#if defined(_MSC_VER)
// Bit builtin's make these assumptions when calling _BitScanForward/Reverse
// etc. These assumptions are expected to be true for Win32/Win64 which this
// file supports.
static_assert(sizeof(unsigned long long) == 8, "");
static_assert(sizeof(unsigned long) == 4, "");
static_assert(sizeof(unsigned int) == 4, "");
__forceinline int __builtin_popcount(unsigned int x)
{
    // Binary: 0101...
    static const unsigned int m1 = 0x55555555;
    // Binary: 00110011..
    static const unsigned int m2 = 0x33333333;
    // Binary:  4 zeros,  4 ones ...
    static const unsigned int m4 = 0x0f0f0f0f;
    // The sum of 256 to the power of 0,1,2,3...
    static const unsigned int h01 = 0x01010101;
    // Put count of each 2 bits into those 2 bits.
    x -= (x >> 1) & m1;
    // Put count of each 4 bits into those 4 bits.
    x = (x & m2) + ((x >> 2) & m2);
    // Put count of each 8 bits into those 8 bits.
    x = (x + (x >> 4)) & m4;
    // Returns left 8 bits of x + (x<<8) + (x<<16) + (x<<24).
    return (x * h01) >> 24;
}
__forceinline int __builtin_popcountl(unsigned long x)
{
    return __builtin_popcount(static_cast<int>(x));
}
__forceinline int __builtin_popcountll(unsigned long long x)
{
    // Binary: 0101...
    static const unsigned long long m1 = 0x5555555555555555;
    // Binary: 00110011..
    static const unsigned long long m2 = 0x3333333333333333;
    // Binary:  4 zeros,  4 ones ...
    static const unsigned long long m4 = 0x0f0f0f0f0f0f0f0f;
    // The sum of 256 to the power of 0,1,2,3...
    static const unsigned long long h01 = 0x0101010101010101;
    // Put count of each 2 bits into those 2 bits.
    x -= (x >> 1) & m1;
    // Put count of each 4 bits into those 4 bits.
    x = (x & m2) + ((x >> 2) & m2);
    // Put count of each 8 bits into those 8 bits.
    x = (x + (x >> 4)) & m4;
    // Returns left 8 bits of x + (x<<8) + (x<<16) + (x<<24) + ...
    return static_cast<int>((x * h01) >> 56);
}
// Returns the number of trailing 0-bits in x, starting at the least significant
// bit position. If x is 0, the result is undefined.
__forceinline int __builtin_ctzll(unsigned long long mask)
{
    unsigned long where;
    // Search from LSB to MSB for first set bit.
    // Returns zero if no set bit is found.
#if defined(_WIN64)
    if (_BitScanForward64(&where, mask))
        return static_cast<int>(where);
#elif defined(_WIN32)
  // Win32 doesn't have _BitScanForward64 so emulate it with two 32 bit calls.
  // Scan the Low Word.
    if (_BitScanForward(&where, static_cast<unsigned long>(mask)))
        return static_cast<int>(where);
    // Scan the High Word.
    if (_BitScanForward(&where, static_cast<unsigned long>(mask >> 32)))
        return static_cast<int>(where + 32); // Create a bit offset from the LSB.
#else
#error "Implementation of __builtin_ctzll required"
#endif
    return 64;
}
__forceinline int __builtin_ctzl(unsigned long mask)
{
    unsigned long where;
    // Search from LSB to MSB for first set bit.
    // Returns zero if no set bit is found.
    if (_BitScanForward(&where, mask))
        return static_cast<int>(where);
    return 32;
}
__forceinline int __builtin_ctz(unsigned int mask)
{
    // Win32 and Win64 expectations.
    static_assert(sizeof(mask) == 4, "");
    static_assert(sizeof(unsigned long) == 4, "");
    return __builtin_ctzl(static_cast<unsigned long>(mask));
}
// Returns the number of leading 0-bits in x, starting at the most significant
// bit position. If x is 0, the result is undefined.
__forceinline int __builtin_clzll(unsigned long long mask)
{
    unsigned long where;
    // BitScanReverse scans from MSB to LSB for first set bit.
    // Returns 0 if no set bit is found.
#if defined(_WIN64)
    if (_BitScanReverse64(&where, mask))
        return static_cast<int>(63 - where);
#elif defined(_WIN32)
  // Scan the high 32 bits.
    if (_BitScanReverse(&where, static_cast<unsigned long>(mask >> 32)))
        return static_cast<int>(63 -
            (where + 32)); // Create a bit offset from the MSB.
// Scan the low 32 bits.
    if (_BitScanReverse(&where, static_cast<unsigned long>(mask)))
        return static_cast<int>(63 - where);
#else
#error "Implementation of __builtin_clzll required"
#endif
    return 64; // Undefined Behavior.
}
__forceinline int __builtin_clzl(unsigned long mask)
{
    unsigned long where;
    // Search from LSB to MSB for first set bit.
    // Returns zero if no set bit is found.
    if (_BitScanReverse(&where, mask))
        return static_cast<int>(31 - where);
    return 32; // Undefined Behavior.
}
__forceinline int __builtin_clz(unsigned int x)
{
    return __builtin_clzl(x);
}
#endif // _LIBCPP_MSVC

// Macros, may be replaced by system specific wrappers. Arguments to these
// macros must not have side-effects as the macros can be removed for a smaller
// code footprint

// Logging functions
#ifndef LFS_TRACE
#ifdef LFS_YES_TRACE
#define LFS_TRACE_(fmt, ...) \
    printf("%s:%d:trace: " fmt "%s\n", __FILE__, __LINE__, __VA_ARGS__)
#define LFS_TRACE(...) LFS_TRACE_(__VA_ARGS__, "")
#else
#define LFS_TRACE(...)
#endif
#endif

#ifndef LFS_DEBUG
#ifndef LFS_NO_DEBUG
#define LFS_DEBUG_(fmt, ...) \
    printf(__VA_ARGS__) //"%s:%d:debug: " fmt "%s\n", __FILE__, __LINE__, __VA_ARGS__)
#define LFS_DEBUG(...) LFS_DEBUG_(__VA_ARGS__, "")
#else
#define LFS_DEBUG(...)
#endif
#endif

#ifndef LFS_WARN
#ifndef LFS_NO_WARN
#define LFS_WARN_(fmt, ...) \
    printf(__VA_ARGS__) //"%s:%d:warn: " fmt "%s\n", __FILE__, __LINE__, __VA_ARGS__)
#define LFS_WARN(...) LFS_WARN_(__VA_ARGS__, "")
#else
#define LFS_WARN(...)
#endif
#endif

#ifndef LFS_ERROR
#ifndef LFS_NO_ERROR
#define LFS_ERROR_(fmt, ...) \
    printf(__VA_ARGS__) //"%s:%d:error: " fmt "%s\n", __FILE__, __LINE__, __VA_ARGS__)
#define LFS_ERROR(...) LFS_ERROR_(__VA_ARGS__, "")
#else
#define LFS_ERROR(...)
#endif
#endif

// Runtime assertions
#ifndef LFS_ASSERT
#ifndef LFS_NO_ASSERT
#define LFS_ASSERT(test) assert(test)
#else
#define LFS_ASSERT(test)
#endif
#endif


// Builtin functions, these may be replaced by more efficient
// toolchain-specific implementations. LFS_NO_INTRINSICS falls back to a more
// expensive basic C implementation for debugging purposes

// Min/max functions for unsigned 32-bit numbers
constexpr uint64_t lfs_max(uint64_t a, uint64_t b) {
    return (a > b) ? a : b;
}

constexpr uint64_t lfs_min(uint64_t a, uint64_t b) {
    return (a < b) ? a : b;
}

// Align to nearest multiple of a size
constexpr uint64_t lfs_aligndown(uint64_t a, uint64_t alignment) {
    return a - (a % alignment);
}

static inline uint64_t lfs_alignup(uint64_t a, uint64_t alignment) {
    return lfs_aligndown(a + alignment - 1, alignment);
}

// Find the smallest power of 2 greater than or equal to a
constexpr uint32_t lfs_npw2_32(uint32_t a) {

#if !defined(LFS_NO_INTRINSICS) && (defined(__GNUC__) || defined(__CC_ARM))
    return 32 - __builtin_clz(a - 1);
#else

    uint32_t r = 0;
    uint32_t s = 0;
    a -= 1;
    s = (a > 0xffff) << 4; a >>= s; r |= s;
    s = (a > 0xff) << 3; a >>= s; r |= s;
    s = (a > 0xf) << 2; a >>= s; r |= s;
    s = (a > 0x3) << 1; a >>= s; r |= s;
    return (r | (a >> 1)) + 1;
#endif
}

inline uint64_t lfs_npw2_64(uint64_t a) {

    if (a == 1) {
        return 1;
    }

    return 64 - __builtin_clzll(a - 1);
}

// Count the number of trailing binary zeros in a
// lfs_ctz(0) may be undefined
constexpr uint32_t lfs_ctz32(uint32_t a) {

#if !defined(LFS_NO_INTRINSICS) && defined(__GNUC__)
    return __builtin_ctz(a);
#else
    return lfs_npw2_32((a & (0 - a)) + 1) - 1;
#endif
}

inline uint64_t lfs_ctz64(uint64_t a) {

    uint64_t result = 0;

    for (result = 0; result < 64; result++) {

        if (a & ((uint64_t)1 << result)) {
            return result;
        }
    }

    return 0;
}

// Count the number of binary ones in a
constexpr size_t lfs_popc64(uint64_t V) {

    V -= ((V >> 1) & 0x5555555555555555);
    V = (V & 0x3333333333333333) + ((V >> 2) & 0x3333333333333333);
    return ((V + (V >> 4) & 0xF0F0F0F0F0F0F0F) * 0x101010101010101) >> 56;
}

// Find the sequence comparison of a and b, this is the distance
// between a and b ignoring overflow
constexpr int lfs_scmp(uint32_t a, uint32_t b) {
    return (int)(unsigned)(a - b);
}

// Convert between 32-bit little-endian and native order
constexpr uint32_t lfs_fromle32(uint32_t a) {
#if (defined(  BYTE_ORDER  ) && defined(  ORDER_LITTLE_ENDIAN  ) &&   BYTE_ORDER   ==   ORDER_LITTLE_ENDIAN  ) || \
    (defined(__BYTE_ORDER  ) && defined(__ORDER_LITTLE_ENDIAN  ) && __BYTE_ORDER   == __ORDER_LITTLE_ENDIAN  ) || \
    (defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    return a;
#else
    return (((uint8_t*)&a)[0] << 0) |
        (((uint8_t*)&a)[1] << 8) |
        (((uint8_t*)&a)[2] << 16) |
        (((uint8_t*)&a)[3] << 24);
#endif
}

constexpr uint64_t lfs_fromle64(uint64_t a) {
#if (defined(  BYTE_ORDER  ) && defined(  ORDER_LITTLE_ENDIAN  ) &&   BYTE_ORDER   ==   ORDER_LITTLE_ENDIAN  ) || \
    (defined(__BYTE_ORDER  ) && defined(__ORDER_LITTLE_ENDIAN  ) && __BYTE_ORDER   == __ORDER_LITTLE_ENDIAN  ) || \
    (defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    return a;
#else
    return (uint64_t(((uint8_t*)&a)[0]) << (8 * 0)) |
        (uint64_t(((uint8_t*)&a)[1]) << (8 * 1)) |
        (uint64_t(((uint8_t*)&a)[2]) << (8 * 2)) |
        (uint64_t(((uint8_t*)&a)[3]) << (8 * 3)) |
        (uint64_t(((uint8_t*)&a)[4]) << (8 * 4)) |
        (uint64_t(((uint8_t*)&a)[5]) << (8 * 5)) |
        (uint64_t(((uint8_t*)&a)[6]) << (8 * 6)) |
        (uint64_t(((uint8_t*)&a)[7]) << (8 * 7));
#endif
}

constexpr uint32_t lfs_tole32(uint32_t a) {
    return lfs_fromle32(a);
}

constexpr uint64_t lfs_tole64(uint64_t a) {
    return lfs_fromle64(a);
}


// Convert between 32-bit big-endian and native order
constexpr uint32_t lfs_frombe32(uint32_t a) {
#if(defined(  BYTE_ORDER  ) && defined(  ORDER_BIG_ENDIAN  ) &&   BYTE_ORDER   ==   ORDER_BIG_ENDIAN  ) || \
    (defined(__BYTE_ORDER  ) && defined(__ORDER_BIG_ENDIAN  ) && __BYTE_ORDER   == __ORDER_BIG_ENDIAN  ) || \
    (defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    return a;
#else
    return (((uint8_t*)&a)[0] << 24) |
        (((uint8_t*)&a)[1] << 16) |
        (((uint8_t*)&a)[2] << 8) |
        (((uint8_t*)&a)[3] << 0);
#endif
}

constexpr uint64_t lfs_frombe64(uint64_t a) {
#if(defined(  BYTE_ORDER  ) && defined(  ORDER_BIG_ENDIAN  ) &&   BYTE_ORDER   ==   ORDER_BIG_ENDIAN  ) || \
    (defined(__BYTE_ORDER  ) && defined(__ORDER_BIG_ENDIAN  ) && __BYTE_ORDER   == __ORDER_BIG_ENDIAN  ) || \
    (defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    return a;
#else
    return (uint64_t(((uint8_t*)&a)[0]) << (8 * 7)) |
        (uint64_t(((uint8_t*)&a)[1]) << (8 * 6)) |
        (uint64_t(((uint8_t*)&a)[2]) << (8 * 5)) |
        (uint64_t(((uint8_t*)&a)[3]) << (8 * 4)) |
        (uint64_t(((uint8_t*)&a)[4]) << (8 * 3)) |
        (uint64_t(((uint8_t*)&a)[5]) << (8 * 2)) |
        (uint64_t(((uint8_t*)&a)[6]) << (8 * 1)) |
        (uint64_t(((uint8_t*)&a)[7]) << (8 * 0));

#endif
}

constexpr uint32_t lfs_tobe32(uint32_t a) {
    return lfs_frombe32(a);
}

constexpr uint64_t lfs_tobe64(uint64_t a) {
    return lfs_frombe64(a);
}

/// Small type-level utilities ///
// operations on block pairs

constexpr void lfs_pair_swap(lfs_block_t pair[2]) {

    lfs_block_t t = pair[0];
    pair[0] = pair[1];
    pair[1] = t;
}

constexpr bool lfs_pair_isnull(const lfs_block_t pair[2]) {

    return pair[0] == LFS_BLOCK_NULL || pair[1] == LFS_BLOCK_NULL;
}

constexpr int lfs_pair_cmp(const lfs_block_t paira[2], const lfs_block_t pairb[2]) {

    return !(paira[0] == pairb[0] || paira[1] == pairb[1] ||
        paira[0] == pairb[1] || paira[1] == pairb[0]);
}

constexpr bool lfs_pair_sync(const lfs_block_t paira[2], const lfs_block_t pairb[2]) {

    return (paira[0] == pairb[0] && paira[1] == pairb[1]) ||
        (paira[0] == pairb[1] && paira[1] == pairb[0]);
}

constexpr void lfs_pair_fromle64(lfs_block_t pair[2]) {

    pair[0] = lfs_fromle64(pair[0]);
    pair[1] = lfs_fromle64(pair[1]);
}

constexpr void lfs_pair_tole64(lfs_block_t pair[2]) {

    pair[0] = lfs_tole64(pair[0]);
    pair[1] = lfs_tole64(pair[1]);
}

#define LFS_MKTAG(type, id, size) \
    (((lfs_tag_t)(type) << 20) | ((lfs_tag_t)(id) << 10) | (lfs_tag_t)(size))

#define LFS_MKTAG_IF(cond, type, id, size) \
    ((cond) ? LFS_MKTAG(type, id, size) : LFS_MKTAG(LFS_FROM_NOOP, 0, 0))

#define LFS_MKTAG_IF_ELSE(cond, type1, id1, size1, type2, id2, size2) \
    ((cond) ? LFS_MKTAG(type1, id1, size1) : LFS_MKTAG(type2, id2, size2))

constexpr bool lfs_tag_isvalid(lfs_tag_t tag) {
    return !(tag & 0x80000000);
}

constexpr bool lfs_tag_isdelete(lfs_tag_t tag) {
    return ((int32_t)(tag << 22) >> 22) == -1;
}

constexpr uint16_t lfs_tag_type1(lfs_tag_t tag) {
    return (tag & 0x70000000) >> 20;
}
constexpr uint16_t lfs_tag_type2(lfs_tag_t tag) {
    return (tag & 0x78000000) >> 20;
}
constexpr uint16_t lfs_tag_type3(lfs_tag_t tag) {
    return (tag & 0x7ff00000) >> 20;
}

constexpr uint8_t lfs_tag_chunk(lfs_tag_t tag) {
    return (tag & 0x0ff00000) >> 20;
}

constexpr int8_t lfs_tag_splice(lfs_tag_t tag) {
    return (int8_t)lfs_tag_chunk(tag);
}

constexpr uint16_t lfs_tag_id(lfs_tag_t tag) {
    return (tag & 0x000ffc00) >> 10;
}

constexpr lfs_size_t lfs_tag_size(lfs_tag_t tag) {
    return tag & 0x000003ff;
}

constexpr lfs_size_t lfs_tag_dsize(lfs_tag_t tag) {
    return sizeof(tag) + lfs_tag_size(tag + lfs_tag_isdelete(tag));
}

// operations on global state
constexpr void lfs_gstate_xor(lfs_gstate_t* a, const lfs_gstate_t* b) {

    for (int i = 0; i < 3; i++) {

        ((uint32_t*)a)[i] ^= ((const uint32_t*)b)[i];
    }
}

constexpr bool lfs_gstate_iszero(const lfs_gstate_t* a) {

    for (int i = 0; i < 3; i++) {

        if (((uint32_t*)a)[i] != 0) {

            return false;
        }
    }

    return true;
}

constexpr bool lfs_gstate_hasorphans(const lfs_gstate_t* a) {
    return lfs_tag_size(a->tag);
}

constexpr uint8_t lfs_gstate_getorphans(const lfs_gstate_t* a) {
    return (uint8_t)lfs_tag_size(a->tag);
}

constexpr bool lfs_gstate_hasmove(const lfs_gstate_t* a) {
    return lfs_tag_type1(a->tag);
}

constexpr bool lfs_gstate_hasmovehere(const lfs_gstate_t* a, const lfs_block_t* pair) {
    return lfs_tag_type1(a->tag) && lfs_pair_cmp(a->pair, pair) == 0;
}

constexpr void lfs_gstate_fromle64(lfs_gstate_t* a) {
    a->tag = lfs_fromle32(a->tag);
    a->pair[0] = lfs_fromle64(a->pair[0]);
    a->pair[1] = lfs_fromle64(a->pair[1]);
}

constexpr void lfs_gstate_tole64(lfs_gstate_t* a) {
    a->tag = lfs_tole32(a->tag);
    a->pair[0] = lfs_tole64(a->pair[0]);
    a->pair[1] = lfs_tole64(a->pair[1]);
}

// other endianness operations
constexpr void lfs_ctz_fromle64(lfs_ctz_t* ctz) {
    ctz->head = lfs_fromle64(ctz->head);
    ctz->size = lfs_fromle64(ctz->size);
}

constexpr void lfs_ctz_tole64(lfs_ctz_t* ctz) {
    ctz->head = lfs_tole64(ctz->head);
    ctz->size = lfs_tole64(ctz->size);
}

constexpr void lfs_superblock_fromle64(lfs_superblock_t* superblock) {
    superblock->version = lfs_fromle32(superblock->version);
    superblock->block_size = lfs_fromle64(superblock->block_size);
    superblock->block_count = lfs_fromle64(superblock->block_count);
    superblock->name_max_length = lfs_fromle64(superblock->name_max_length);
    superblock->file_max_size = lfs_fromle64(superblock->file_max_size);
    superblock->attr_max_size = lfs_fromle64(superblock->attr_max_size);
}

constexpr void lfs_superblock_tole64(lfs_superblock_t* superblock) {
    superblock->version = lfs_tole32(superblock->version);
    superblock->block_size = lfs_tole64(superblock->block_size);
    superblock->block_count = lfs_tole64(superblock->block_count);
    superblock->name_max_length = lfs_tole64(superblock->name_max_length);
    superblock->file_max_size = lfs_tole64(superblock->file_max_size);
    superblock->attr_max_size = lfs_tole64(superblock->attr_max_size);
}

constexpr bool lfs_mlist_isopen(lfs_metadata_list_t* head, lfs_metadata_list_t* node) {

    for (lfs_metadata_list_t** p = &head; *p; p = &(*p)->next) {

        if (*p == (lfs_metadata_list_t*)node) {

            return true;
        }
    }

    return false;
}

constexpr void lfs_mlist_remove(lfs_t* lfs, lfs_metadata_list_t* metadata_list) {

    for (lfs_metadata_list_t** p = &lfs->metadata_list; *p; p = &(*p)->next) {

        if (*p == metadata_list) {

            *p = (*p)->next;
            break;
        }
    }
}

constexpr void lfs_mlist_append(lfs_t* lfs, lfs_metadata_list_t* metadata_list) {
    metadata_list->next = lfs->metadata_list;
    lfs->metadata_list = metadata_list;
}

// Software CRC implementation with small lookup table
constexpr uint32_t lfs_crc(uint32_t crc, const void* buffer, size_t size) {

    const uint32_t rtable[16] = {
        0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
        0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
        0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
        0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
    };

    const uint8_t* data = (const uint8_t *)buffer;

    for (size_t i = 0; i < size; i++) {

        crc = (crc >> 4) ^ rtable[(crc ^ (data[i] >> 0)) & 0xf];
        crc = (crc >> 4) ^ rtable[(crc ^ (data[i] >> 4)) & 0xf];
    }

    return crc;
}

constexpr void lfs_cache_drop(lfs_t* lfs, lfs_cache_t* read_cache) {
    // do not zero, cheaper if cache is readonly or only going to be
    // written with identical data (during relocates)
    (void)lfs;
    read_cache->block = LFS_BLOCK_NULL;
}

inline void lfs_cache_zero(lfs_t* lfs, lfs_cache_t* write_cache) {
    // zero to avoid information leak
    memset(write_cache->buffer, 0xff, lfs->cfg->cache_size);
    write_cache->block = LFS_BLOCK_NULL;
}