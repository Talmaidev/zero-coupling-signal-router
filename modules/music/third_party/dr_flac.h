/* dr_flac.h — vendored single-file FLAC decoder.
 *
 * Source: dr_flac 0.13.4, as namespaced (ma_dr_flac_*) inside miniaudio 1.73
 * (https://github.com/mackron/miniaudio). Extracted from the miniaudio header,
 * renamed to the drflac_* prefix; the miniaudio allocation-callback / result
 * types are shimmed below so this header stays a standalone C11 dependency.
 *
 * License: dr_flac and miniaudio are released under the public domain (MIT-0).
 * See dr_flac_LICENSE.txt next to this file.
 *
 * Usage:
 *   - In exactly one implementation TU: #define DR_FLAC_IMPLEMENTATION before
 *     including this header.
 *   - Define DR_FLAC_NO_STDIO (no file-open entry points) and
 *     DR_FLAC_NO_SIMD (portable scalar path; no SSE2/NEON detection) at every
 *     include site.
 *   - All decoder memory comes from caller-provided allocation callbacks; the
 *     module does not call malloc/free through this header. */
#ifndef DR_FLAC_H
#define DR_FLAC_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64)
#define DR_FLAC_64BIT 1
#endif
#ifndef DRFLAC_API
#define DRFLAC_API
#endif
#ifndef DRFLAC_INLINE
#if defined(_MSC_VER)
#define DRFLAC_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define DRFLAC_INLINE __inline__ __attribute__((always_inline))
#else
#define DRFLAC_INLINE
#endif
#endif
#ifndef DRFLAC_ASSERT
#define DRFLAC_ASSERT(expression) assert(expression)
#endif
typedef struct
{
    void* pUserData;
    void* (* onMalloc)(size_t sz, void* pUserData);
    void* (* onRealloc)(void* p, size_t sz, void* pUserData);
    void  (* onFree)(void* p, void* pUserData);
} ma_allocation_callbacks;
typedef int ma_result;
#define MA_SUCCESS       0
#define MA_AT_END        1
#define MA_CRC_MISMATCH  (-1)
#define MA_ERROR         (-2)
#define MA_OUT_OF_MEMORY (-3)
#define MA_BAD_SEEK      (-4)
#define MA_INVALID_DATA  (-5)
/* dr_flac_h begin */
#ifndef drflac_h
#define drflac_h
#ifdef __cplusplus
extern "C" {
#endif
#define DR_FLAC_STRINGIFY(x)      #x
#define DR_FLAC_XSTRINGIFY(x)     DR_FLAC_STRINGIFY(x)
#define DR_FLAC_VERSION_MAJOR     0
#define DR_FLAC_VERSION_MINOR     13
#define DR_FLAC_VERSION_REVISION  4
#define DR_FLAC_VERSION_STRING    DR_FLAC_XSTRINGIFY(DR_FLAC_VERSION_MAJOR) "." DR_FLAC_XSTRINGIFY(DR_FLAC_VERSION_MINOR) "." DR_FLAC_XSTRINGIFY(DR_FLAC_VERSION_REVISION)
#include <stddef.h>
#if defined(_MSC_VER) && _MSC_VER >= 1700
    #define DR_FLAC_DEPRECATED       __declspec(deprecated)
#elif (defined(__GNUC__) && __GNUC__ >= 4)
    #define DR_FLAC_DEPRECATED       __attribute__((deprecated))
#elif defined(__has_feature)
    #if __has_feature(attribute_deprecated)
        #define DR_FLAC_DEPRECATED   __attribute__((deprecated))
    #else
        #define DR_FLAC_DEPRECATED
    #endif
#else
    #define DR_FLAC_DEPRECATED
#endif
DRFLAC_API void drflac_version(uint32_t* pMajor, uint32_t* pMinor, uint32_t* pRevision);
DRFLAC_API const char* drflac_version_string(void);
#ifndef DR_FLAC_BUFFER_SIZE
#define DR_FLAC_BUFFER_SIZE   4096
#endif
#if 1
typedef uint64_t drflac_cache_t;
#else
typedef uint32_t drflac_cache_t;
#endif
#define DR_FLAC_METADATA_BLOCK_TYPE_STREAMINFO       0
#define DR_FLAC_METADATA_BLOCK_TYPE_PADDING          1
#define DR_FLAC_METADATA_BLOCK_TYPE_APPLICATION      2
#define DR_FLAC_METADATA_BLOCK_TYPE_SEEKTABLE        3
#define DR_FLAC_METADATA_BLOCK_TYPE_VORBIS_COMMENT   4
#define DR_FLAC_METADATA_BLOCK_TYPE_CUESHEET         5
#define DR_FLAC_METADATA_BLOCK_TYPE_PICTURE          6
#define DR_FLAC_METADATA_BLOCK_TYPE_INVALID          127
#define DR_FLAC_PICTURE_TYPE_OTHER                   0
#define DR_FLAC_PICTURE_TYPE_FILE_ICON               1
#define DR_FLAC_PICTURE_TYPE_OTHER_FILE_ICON         2
#define DR_FLAC_PICTURE_TYPE_COVER_FRONT             3
#define DR_FLAC_PICTURE_TYPE_COVER_BACK              4
#define DR_FLAC_PICTURE_TYPE_LEAFLET_PAGE            5
#define DR_FLAC_PICTURE_TYPE_MEDIA                   6
#define DR_FLAC_PICTURE_TYPE_LEAD_ARTIST             7
#define DR_FLAC_PICTURE_TYPE_ARTIST                  8
#define DR_FLAC_PICTURE_TYPE_CONDUCTOR               9
#define DR_FLAC_PICTURE_TYPE_BAND                    10
#define DR_FLAC_PICTURE_TYPE_COMPOSER                11
#define DR_FLAC_PICTURE_TYPE_LYRICIST                12
#define DR_FLAC_PICTURE_TYPE_RECORDING_LOCATION      13
#define DR_FLAC_PICTURE_TYPE_DURING_RECORDING        14
#define DR_FLAC_PICTURE_TYPE_DURING_PERFORMANCE      15
#define DR_FLAC_PICTURE_TYPE_SCREEN_CAPTURE          16
#define DR_FLAC_PICTURE_TYPE_BRIGHT_COLORED_FISH     17
#define DR_FLAC_PICTURE_TYPE_ILLUSTRATION            18
#define DR_FLAC_PICTURE_TYPE_BAND_LOGOTYPE           19
#define DR_FLAC_PICTURE_TYPE_PUBLISHER_LOGOTYPE      20
typedef enum
{
    drflac_container_native,
    drflac_container_ogg,
    drflac_container_unknown
} drflac_container;
typedef enum
{
    DR_FLAC_SEEK_SET,
    DR_FLAC_SEEK_CUR,
    DR_FLAC_SEEK_END
} drflac_seek_origin;
typedef struct
{
    uint64_t firstPCMFrame;
    uint64_t flacFrameOffset;
    uint16_t pcmFrameCount;
} drflac_seekpoint;
typedef struct
{
    uint16_t minBlockSizeInPCMFrames;
    uint16_t maxBlockSizeInPCMFrames;
    uint32_t minFrameSizeInPCMFrames;
    uint32_t maxFrameSizeInPCMFrames;
    uint32_t sampleRate;
    uint8_t  channels;
    uint8_t  bitsPerSample;
    uint64_t totalPCMFrameCount;
    uint8_t  md5[16];
} drflac_streaminfo;
typedef struct
{
    uint32_t type;
    uint32_t rawDataSize;
    uint64_t rawDataOffset;
    const void* pRawData;
    union
    {
        drflac_streaminfo streaminfo;
        struct
        {
            int unused;
        } padding;
        struct
        {
            uint32_t id;
            const void* pData;
            uint32_t dataSize;
        } application;
        struct
        {
            uint32_t seekpointCount;
            const drflac_seekpoint* pSeekpoints;
        } seektable;
        struct
        {
            uint32_t vendorLength;
            const char* vendor;
            uint32_t commentCount;
            const void* pComments;
        } vorbis_comment;
        struct
        {
            char catalog[128];
            uint64_t leadInSampleCount;
            int isCD;
            uint8_t trackCount;
            const void* pTrackData;
        } cuesheet;
        struct
        {
            uint32_t type;
            uint32_t mimeLength;
            const char* mime;
            uint32_t descriptionLength;
            const char* description;
            uint32_t width;
            uint32_t height;
            uint32_t colorDepth;
            uint32_t indexColorCount;
            uint32_t pictureDataSize;
            uint64_t pictureDataOffset;
            const uint8_t* pPictureData;
        } picture;
    } data;
} drflac_metadata;
typedef size_t (* drflac_read_proc)(void* pUserData, void* pBufferOut, size_t bytesToRead);
typedef int (* drflac_seek_proc)(void* pUserData, int offset, drflac_seek_origin origin);
typedef int (* drflac_tell_proc)(void* pUserData, int64_t* pCursor);
typedef void (* drflac_meta_proc)(void* pUserData, drflac_metadata* pMetadata);
typedef struct
{
    const uint8_t* data;
    size_t dataSize;
    size_t currentReadPos;
} drflac__memory_stream;
typedef struct
{
    drflac_read_proc onRead;
    drflac_seek_proc onSeek;
    drflac_tell_proc onTell;
    void* pUserData;
    size_t unalignedByteCount;
    drflac_cache_t unalignedCache;
    uint32_t nextL2Line;
    uint32_t consumedBits;
    drflac_cache_t cacheL2[DR_FLAC_BUFFER_SIZE/sizeof(drflac_cache_t)];
    drflac_cache_t cache;
    uint16_t crc16;
    drflac_cache_t crc16Cache;
    uint32_t crc16CacheIgnoredBytes;
} drflac_bs;
typedef union
{
    int32_t* pS32;
    int64_t* pS64;
} drflac_samples;
typedef struct
{
    uint8_t subframeType;
    uint8_t wastedBitsPerSample;
    uint8_t lpcOrder;
    drflac_samples samples;
} drflac_subframe;
typedef struct
{
    uint64_t pcmFrameNumber;
    uint32_t flacFrameNumber;
    uint32_t sampleRate;
    uint16_t blockSizeInPCMFrames;
    uint8_t channelAssignment;
    uint8_t bitsPerSample;
    uint8_t crc8;
} drflac_frame_header;
typedef struct
{
    drflac_frame_header header;
    uint32_t pcmFramesRemaining;
    drflac_subframe subframes[8];
} drflac_frame;
typedef struct
{
    drflac_meta_proc onMeta;
    void* pUserDataMD;
    ma_allocation_callbacks allocationCallbacks;
    uint32_t sampleRate;
    uint8_t channels;
    uint8_t bitsPerSample;
    uint16_t maxBlockSizeInPCMFrames;
    uint64_t totalPCMFrameCount;
    drflac_container container;
    uint32_t seekpointCount;
    drflac_frame currentFLACFrame;
    uint64_t currentPCMFrame;
    uint64_t firstFLACFramePosInBytes;
    drflac__memory_stream memoryStream;
    drflac_samples decodedSamples;
    void* pDecodedSamplesAllocation;
    uint16_t decodedSamplesCapacityInPCMFrames;
    drflac_seekpoint* pSeekpoints;
    void* _oggbs;
    int _noSeekTableSeek    : 1;
    int _noBinarySearchSeek : 1;
    int _noBruteForceSeek   : 1;
    drflac_bs bs;
    uint8_t pExtraData[1];
} drflac;
DRFLAC_API drflac* drflac_open(drflac_read_proc onRead, drflac_seek_proc onSeek, drflac_tell_proc onTell, void* pUserData, const ma_allocation_callbacks* pAllocationCallbacks);
DRFLAC_API drflac* drflac_open_relaxed(drflac_read_proc onRead, drflac_seek_proc onSeek, drflac_tell_proc onTell, drflac_container container, void* pUserData, const ma_allocation_callbacks* pAllocationCallbacks);
DRFLAC_API drflac* drflac_open_with_metadata(drflac_read_proc onRead, drflac_seek_proc onSeek, drflac_tell_proc onTell, drflac_meta_proc onMeta, void* pUserData, const ma_allocation_callbacks* pAllocationCallbacks);
DRFLAC_API drflac* drflac_open_with_metadata_relaxed(drflac_read_proc onRead, drflac_seek_proc onSeek, drflac_tell_proc onTell, drflac_meta_proc onMeta, drflac_container container, void* pUserData, const ma_allocation_callbacks* pAllocationCallbacks);
DRFLAC_API void drflac_close(drflac* pFlac);
DRFLAC_API uint64_t drflac_read_pcm_frames_s32(drflac* pFlac, uint64_t framesToRead, int32_t* pBufferOut);
DRFLAC_API uint64_t drflac_read_pcm_frames_s16(drflac* pFlac, uint64_t framesToRead, int16_t* pBufferOut);
DRFLAC_API uint64_t drflac_read_pcm_frames_f32(drflac* pFlac, uint64_t framesToRead, float* pBufferOut);
DRFLAC_API int drflac_seek_to_pcm_frame(drflac* pFlac, uint64_t pcmFrameIndex);
#ifndef DR_FLAC_NO_STDIO
DRFLAC_API drflac* drflac_open_file(const char* pFileName, const ma_allocation_callbacks* pAllocationCallbacks);
DRFLAC_API drflac* drflac_open_file_w(const wchar_t* pFileName, const ma_allocation_callbacks* pAllocationCallbacks);
DRFLAC_API drflac* drflac_open_file_with_metadata(const char* pFileName, drflac_meta_proc onMeta, void* pUserData, const ma_allocation_callbacks* pAllocationCallbacks);
DRFLAC_API drflac* drflac_open_file_with_metadata_w(const wchar_t* pFileName, drflac_meta_proc onMeta, void* pUserData, const ma_allocation_callbacks* pAllocationCallbacks);
#endif
DRFLAC_API drflac* drflac_open_memory(const void* pData, size_t dataSize, const ma_allocation_callbacks* pAllocationCallbacks);
DRFLAC_API drflac* drflac_open_memory_with_metadata(const void* pData, size_t dataSize, drflac_meta_proc onMeta, void* pUserData, const ma_allocation_callbacks* pAllocationCallbacks);
DRFLAC_API int32_t* drflac_open_and_read_pcm_frames_s32(drflac_read_proc onRead, drflac_seek_proc onSeek, drflac_tell_proc onTell, void* pUserData, unsigned int* channels, unsigned int* sampleRate, uint64_t* totalPCMFrameCount, const ma_allocation_callbacks* pAllocationCallbacks);
DRFLAC_API int16_t* drflac_open_and_read_pcm_frames_s16(drflac_read_proc onRead, drflac_seek_proc onSeek, drflac_tell_proc onTell, void* pUserData, unsigned int* channels, unsigned int* sampleRate, uint64_t* totalPCMFrameCount, const ma_allocation_callbacks* pAllocationCallbacks);
DRFLAC_API float* drflac_open_and_read_pcm_frames_f32(drflac_read_proc onRead, drflac_seek_proc onSeek, drflac_tell_proc onTell, void* pUserData, unsigned int* channels, unsigned int* sampleRate, uint64_t* totalPCMFrameCount, const ma_allocation_callbacks* pAllocationCallbacks);
#ifndef DR_FLAC_NO_STDIO
DRFLAC_API int32_t* drflac_open_file_and_read_pcm_frames_s32(const char* filename, unsigned int* channels, unsigned int* sampleRate, uint64_t* totalPCMFrameCount, const ma_allocation_callbacks* pAllocationCallbacks);
DRFLAC_API int16_t* drflac_open_file_and_read_pcm_frames_s16(const char* filename, unsigned int* channels, unsigned int* sampleRate, uint64_t* totalPCMFrameCount, const ma_allocation_callbacks* pAllocationCallbacks);
DRFLAC_API float* drflac_open_file_and_read_pcm_frames_f32(const char* filename, unsigned int* channels, unsigned int* sampleRate, uint64_t* totalPCMFrameCount, const ma_allocation_callbacks* pAllocationCallbacks);
#endif
DRFLAC_API int32_t* drflac_open_memory_and_read_pcm_frames_s32(const void* data, size_t dataSize, unsigned int* channels, unsigned int* sampleRate, uint64_t* totalPCMFrameCount, const ma_allocation_callbacks* pAllocationCallbacks);
DRFLAC_API int16_t* drflac_open_memory_and_read_pcm_frames_s16(const void* data, size_t dataSize, unsigned int* channels, unsigned int* sampleRate, uint64_t* totalPCMFrameCount, const ma_allocation_callbacks* pAllocationCallbacks);
DRFLAC_API float* drflac_open_memory_and_read_pcm_frames_f32(const void* data, size_t dataSize, unsigned int* channels, unsigned int* sampleRate, uint64_t* totalPCMFrameCount, const ma_allocation_callbacks* pAllocationCallbacks);
DRFLAC_API void drflac_free(void* p, const ma_allocation_callbacks* pAllocationCallbacks);
typedef struct
{
    uint32_t countRemaining;
    const char* pRunningData;
} drflac_vorbis_comment_iterator;
DRFLAC_API void drflac_init_vorbis_comment_iterator(drflac_vorbis_comment_iterator* pIter, uint32_t commentCount, const void* pComments);
DRFLAC_API const char* drflac_next_vorbis_comment(drflac_vorbis_comment_iterator* pIter, uint32_t* pCommentLengthOut);
typedef struct
{
    uint32_t countRemaining;
    const char* pRunningData;
} drflac_cuesheet_track_iterator;
typedef struct
{
    uint64_t offset;
    uint8_t index;
    uint8_t reserved[3];
} drflac_cuesheet_track_index;
typedef struct
{
    uint64_t offset;
    uint8_t trackNumber;
    char ISRC[12];
    uint8_t isAudio;
    uint8_t preEmphasis;
    uint8_t indexCount;
    const drflac_cuesheet_track_index* pIndexPoints;
} drflac_cuesheet_track;
DRFLAC_API void drflac_init_cuesheet_track_iterator(drflac_cuesheet_track_iterator* pIter, uint32_t trackCount, const void* pTrackData);
DRFLAC_API int drflac_next_cuesheet_track(drflac_cuesheet_track_iterator* pIter, drflac_cuesheet_track* pCuesheetTrack);
#ifdef __cplusplus
}
#endif
#endif
/* dr_flac_h end */

/* dr_flac_c begin */
#if defined(DR_FLAC_IMPLEMENTATION)
#ifndef drflac_c
#define drflac_c
#if defined(__clang__) || (defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)))
    #pragma GCC diagnostic push
    #if __GNUC__ >= 7
    #pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
    #endif
#endif
#ifdef __linux__
    #ifndef _BSD_SOURCE
        #define _BSD_SOURCE
    #endif
    #ifndef _DEFAULT_SOURCE
        #define _DEFAULT_SOURCE
    #endif
    #ifndef __USE_BSD
        #define __USE_BSD
    #endif
    #include <endian.h>
#endif
#include <stdlib.h>
#include <string.h>
#if !defined(DR_FLAC_NO_SIMD)
    #if defined(MA_X64) || defined(MA_X86)
        #if defined(_MSC_VER) && !defined(__clang__)
            #if _MSC_VER >= 1400 && !defined(DR_FLAC_NO_SSE2)
                #define DR_FLAC_SUPPORT_SSE2
            #endif
            #if _MSC_VER >= 1600 && !defined(DR_FLAC_NO_SSE41)
                #define DR_FLAC_SUPPORT_SSE41
            #endif
        #elif defined(__clang__) || (defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 3)))
            #if defined(__SSE2__) && !defined(DR_FLAC_NO_SSE2)
                #define DR_FLAC_SUPPORT_SSE2
            #endif
            #if defined(__SSE4_1__) && !defined(DR_FLAC_NO_SSE41)
                #define DR_FLAC_SUPPORT_SSE41
            #endif
        #endif
        #if !defined(__GNUC__) && !defined(__clang__) && defined(__has_include)
            #if !defined(DR_FLAC_SUPPORT_SSE2) && !defined(DR_FLAC_NO_SSE2) && __has_include(<emmintrin.h>)
                #define DR_FLAC_SUPPORT_SSE2
            #endif
            #if !defined(DR_FLAC_SUPPORT_SSE41) && !defined(DR_FLAC_NO_SSE41) && __has_include(<smmintrin.h>)
                #define DR_FLAC_SUPPORT_SSE41
            #endif
        #endif
        #if defined(DR_FLAC_SUPPORT_SSE41)
            #include <smmintrin.h>
        #elif defined(DR_FLAC_SUPPORT_SSE2)
            #include <emmintrin.h>
        #endif
    #endif
    #if defined(MA_ARM)
        #if !defined(DR_FLAC_NO_NEON) && (defined(__ARM_NEON) || defined(__aarch64__) || defined(_M_ARM64))
            #define DR_FLAC_SUPPORT_NEON
            #include <arm_neon.h>
        #endif
    #endif
#endif
#if !defined(DR_FLAC_NO_SIMD) && (defined(MA_X86) || defined(MA_X64))
    #if defined(_MSC_VER) && !defined(__clang__)
        #if _MSC_VER >= 1400
            #include <intrin.h>
            static void drflac__cpuid(int info[4], int fid)
            {
                __cpuid(info, fid);
            }
        #else
            #define DR_FLAC_NO_CPUID
        #endif
    #else
        #if defined(__GNUC__) || defined(__clang__)
            static void drflac__cpuid(int info[4], int fid)
            {
                #if defined(MA_X86) && defined(__PIC__)
                    __asm__ __volatile__ (
                        "xchg{l} {%%}ebx, %k1;"
                        "cpuid;"
                        "xchg{l} {%%}ebx, %k1;"
                        : "=a"(info[0]), "=&r"(info[1]), "=c"(info[2]), "=d"(info[3]) : "a"(fid), "c"(0)
                    );
                #else
                    __asm__ __volatile__ (
                        "cpuid" : "=a"(info[0]), "=b"(info[1]), "=c"(info[2]), "=d"(info[3]) : "a"(fid), "c"(0)
                    );
                #endif
            }
        #else
            #define DR_FLAC_NO_CPUID
        #endif
    #endif
#else
    #define DR_FLAC_NO_CPUID
#endif
static DRFLAC_INLINE int drflac_has_sse2(void)
{
#if defined(DR_FLAC_SUPPORT_SSE2)
    #if (defined(MA_X64) || defined(MA_X86)) && !defined(DR_FLAC_NO_SSE2)
        #if defined(MA_X64)
            return 1;
        #elif (defined(_M_IX86_FP) && _M_IX86_FP == 2) || defined(__SSE2__)
            return 1;
        #else
            #if defined(DR_FLAC_NO_CPUID)
                return 0;
            #else
                int info[4];
                drflac__cpuid(info, 1);
                return (info[3] & (1 << 26)) != 0;
            #endif
        #endif
    #else
        return 0;
    #endif
#else
    return 0;
#endif
}
static DRFLAC_INLINE int drflac_has_sse41(void)
{
#if defined(DR_FLAC_SUPPORT_SSE41)
    #if (defined(MA_X64) || defined(MA_X86)) && !defined(DR_FLAC_NO_SSE41)
        #if defined(__SSE4_1__) || defined(__AVX__)
            return 1;
        #else
            #if defined(DR_FLAC_NO_CPUID)
                return 0;
            #else
                int info[4];
                drflac__cpuid(info, 1);
                return (info[2] & (1 << 19)) != 0;
            #endif
        #endif
    #else
        return 0;
    #endif
#else
    return 0;
#endif
}
#if defined(_MSC_VER) && _MSC_VER >= 1500 && (defined(MA_X86) || defined(MA_X64)) && !defined(__clang__)
    #define DR_FLAC_HAS_LZCNT_INTRINSIC
#elif (defined(__GNUC__) && ((__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7)))
    #define DR_FLAC_HAS_LZCNT_INTRINSIC
#elif defined(__clang__)
    #if defined(__has_builtin)
        #if __has_builtin(__builtin_clzll) || __has_builtin(__builtin_clzl)
            #define DR_FLAC_HAS_LZCNT_INTRINSIC
        #endif
    #endif
#endif
#if defined(_MSC_VER) && _MSC_VER >= 1400 && !defined(__clang__)
    #define DR_FLAC_HAS_BYTESWAP16_INTRINSIC
    #define DR_FLAC_HAS_BYTESWAP32_INTRINSIC
    #define DR_FLAC_HAS_BYTESWAP64_INTRINSIC
#elif defined(__clang__)
    #if defined(__has_builtin)
        #if __has_builtin(__builtin_bswap16)
            #define DR_FLAC_HAS_BYTESWAP16_INTRINSIC
        #endif
        #if __has_builtin(__builtin_bswap32)
            #define DR_FLAC_HAS_BYTESWAP32_INTRINSIC
        #endif
        #if __has_builtin(__builtin_bswap64)
            #define DR_FLAC_HAS_BYTESWAP64_INTRINSIC
        #endif
    #endif
#elif defined(__GNUC__)
    #if ((__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 3))
        #define DR_FLAC_HAS_BYTESWAP32_INTRINSIC
        #define DR_FLAC_HAS_BYTESWAP64_INTRINSIC
    #endif
    #if ((__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8))
        #define DR_FLAC_HAS_BYTESWAP16_INTRINSIC
    #endif
#elif defined(__WATCOMC__) && defined(__386__)
    #define DR_FLAC_HAS_BYTESWAP16_INTRINSIC
    #define DR_FLAC_HAS_BYTESWAP32_INTRINSIC
    #define DR_FLAC_HAS_BYTESWAP64_INTRINSIC
    extern __inline uint16_t _watcom_bswap16(uint16_t);
    extern __inline uint32_t _watcom_bswap32(uint32_t);
    extern __inline uint64_t _watcom_bswap64(uint64_t);
#pragma aux _watcom_bswap16 = \
    "xchg al, ah" \
    parm  [ax]    \
    value [ax]    \
    modify nomemory;
#pragma aux _watcom_bswap32 = \
    "bswap eax" \
    parm  [eax] \
    value [eax] \
    modify nomemory;
#pragma aux _watcom_bswap64 = \
    "bswap eax"     \
    "bswap edx"     \
    "xchg eax,edx"  \
    parm [eax edx]  \
    value [eax edx] \
    modify nomemory;
#endif
#ifndef DR_FLAC_ASSERT
#include <assert.h>
#define DR_FLAC_ASSERT(expression)           assert(expression)
#endif
#ifndef DR_FLAC_MALLOC
#define DR_FLAC_MALLOC(sz)                   malloc((sz))
#endif
#ifndef DR_FLAC_REALLOC
#define DR_FLAC_REALLOC(p, sz)               realloc((p), (sz))
#endif
#ifndef DR_FLAC_FREE
#define DR_FLAC_FREE(p)                      free((p))
#endif
#ifndef DR_FLAC_COPY_MEMORY
#define DR_FLAC_COPY_MEMORY(dst, src, sz)    memcpy((dst), (src), (sz))
#endif
#ifndef DR_FLAC_ZERO_MEMORY
#define DR_FLAC_ZERO_MEMORY(p, sz)           memset((p), 0, (sz))
#endif
#ifndef DR_FLAC_ZERO_OBJECT
#define DR_FLAC_ZERO_OBJECT(p)               DR_FLAC_ZERO_MEMORY((p), sizeof(*(p)))
#endif
#define DR_FLAC_MIN(a, b)                    (((a) < (b)) ? (a) : (b))
#define DR_FLAC_MAX_SIMD_VECTOR_SIZE                     64
#define DR_FLAC_SUBFRAME_CONSTANT                        0
#define DR_FLAC_SUBFRAME_VERBATIM                        1
#define DR_FLAC_SUBFRAME_FIXED                           8
#define DR_FLAC_SUBFRAME_LPC                             32
#define DR_FLAC_SUBFRAME_RESERVED                        255
#define DR_FLAC_RESIDUAL_CODING_METHOD_PARTITIONED_RICE  0
#define DR_FLAC_RESIDUAL_CODING_METHOD_PARTITIONED_RICE2 1
#define DR_FLAC_CHANNEL_ASSIGNMENT_INDEPENDENT_STEREO    1
#define DR_FLAC_CHANNEL_ASSIGNMENT_LEFT_SIDE             8
#define DR_FLAC_CHANNEL_ASSIGNMENT_RIGHT_SIDE            9
#define DR_FLAC_CHANNEL_ASSIGNMENT_MID_SIDE              10
#define drflac_align(x, a)                              ((((x) + (a) - 1) / (a)) * (a))
#define DR_FLAC_SEEKPOINT_SIZE_IN_BYTES                  18
#define DR_FLAC_CUESHEET_TRACK_SIZE_IN_BYTES             36
#define DR_FLAC_CUESHEET_TRACK_INDEX_SIZE_IN_BYTES       12
#define DR_FLAC_CUESHEET_TRACK_PADDED_SIZE_IN_BYTES      drflac_align(DR_FLAC_CUESHEET_TRACK_SIZE_IN_BYTES, sizeof(uint64_t))
DRFLAC_API void drflac_version(uint32_t* pMajor, uint32_t* pMinor, uint32_t* pRevision)
{
    if (pMajor) {
        *pMajor = DR_FLAC_VERSION_MAJOR;
    }
    if (pMinor) {
        *pMinor = DR_FLAC_VERSION_MINOR;
    }
    if (pRevision) {
        *pRevision = DR_FLAC_VERSION_REVISION;
    }
}
DRFLAC_API const char* drflac_version_string(void)
{
    return DR_FLAC_VERSION_STRING;
}
#if defined(__has_feature)
    #if __has_feature(thread_sanitizer)
        #define DR_FLAC_NO_THREAD_SANITIZE __attribute__((no_sanitize("thread")))
    #else
        #define DR_FLAC_NO_THREAD_SANITIZE
    #endif
#else
    #define DR_FLAC_NO_THREAD_SANITIZE
#endif
#if defined(DR_FLAC_HAS_LZCNT_INTRINSIC)
static int drflac__gIsLZCNTSupported = 0;
#endif
#ifndef DR_FLAC_NO_CPUID
static int drflac__gIsSSE2Supported  = 0;
static int drflac__gIsSSE41Supported = 0;
DR_FLAC_NO_THREAD_SANITIZE static void drflac__init_cpu_caps(void)
{
    static int isCPUCapsInitialized = 0;
    if (!isCPUCapsInitialized) {
#if defined(DR_FLAC_HAS_LZCNT_INTRINSIC)
        int info[4] = {0};
        drflac__cpuid(info, 0x80000001);
        drflac__gIsLZCNTSupported = (info[2] & (1 << 5)) != 0;
#endif
        drflac__gIsSSE2Supported = drflac_has_sse2();
        drflac__gIsSSE41Supported = drflac_has_sse41();
        isCPUCapsInitialized = 1;
    }
}
#else
static int drflac__gIsNEONSupported  = 0;
static DRFLAC_INLINE int drflac__has_neon(void)
{
#if defined(DR_FLAC_SUPPORT_NEON)
    #if defined(MA_ARM) && !defined(DR_FLAC_NO_NEON)
        #if (defined(__ARM_NEON) || defined(__aarch64__) || defined(_M_ARM64))
            return 1;
        #else
            return 0;
        #endif
    #else
        return 0;
    #endif
#else
    return 0;
#endif
}
DR_FLAC_NO_THREAD_SANITIZE static void drflac__init_cpu_caps(void)
{
    drflac__gIsNEONSupported = drflac__has_neon();
#if defined(DR_FLAC_HAS_LZCNT_INTRINSIC) && defined(MA_ARM) && (defined(__ARM_ARCH) && __ARM_ARCH >= 5)
    drflac__gIsLZCNTSupported = 1;
#endif
}
#endif
static DRFLAC_INLINE int drflac__is_little_endian(void)
{
#if defined(MA_X86) || defined(MA_X64)
    return 1;
#elif defined(__BYTE_ORDER) && defined(__LITTLE_ENDIAN) && __BYTE_ORDER == __LITTLE_ENDIAN
    return 1;
#else
    int n = 1;
    return (*(char*)&n) == 1;
#endif
}
static DRFLAC_INLINE uint16_t drflac__swap_endian_uint16(uint16_t n)
{
#ifdef DR_FLAC_HAS_BYTESWAP16_INTRINSIC
    #if defined(_MSC_VER) && !defined(__clang__)
        return _byteswap_ushort(n);
    #elif defined(__GNUC__) || defined(__clang__)
        return __builtin_bswap16(n);
    #elif defined(__WATCOMC__) && defined(__386__)
        return _watcom_bswap16(n);
    #else
        #error "This compiler does not support the byte swap intrinsic."
    #endif
#else
    return ((n & 0xFF00) >> 8) |
           ((n & 0x00FF) << 8);
#endif
}
static DRFLAC_INLINE uint32_t drflac__swap_endian_uint32(uint32_t n)
{
#ifdef DR_FLAC_HAS_BYTESWAP32_INTRINSIC
    #if defined(_MSC_VER) && !defined(__clang__)
        return _byteswap_ulong(n);
    #elif defined(__GNUC__) || defined(__clang__)
        #if defined(MA_ARM) && (defined(__ARM_ARCH) && __ARM_ARCH >= 6) && !defined(__ARM_ARCH_6M__) && !defined(DR_FLAC_64BIT)
            uint32_t r;
            __asm__ __volatile__ (
            #if defined(DR_FLAC_64BIT)
                "rev %w[out], %w[in]" : [out]"=r"(r) : [in]"r"(n)
            #else
                "rev %[out], %[in]" : [out]"=r"(r) : [in]"r"(n)
            #endif
            );
            return r;
        #else
            return __builtin_bswap32(n);
        #endif
    #elif defined(__WATCOMC__) && defined(__386__)
        return _watcom_bswap32(n);
    #else
        #error "This compiler does not support the byte swap intrinsic."
    #endif
#else
    return ((n & 0xFF000000) >> 24) |
           ((n & 0x00FF0000) >>  8) |
           ((n & 0x0000FF00) <<  8) |
           ((n & 0x000000FF) << 24);
#endif
}
static DRFLAC_INLINE uint64_t drflac__swap_endian_uint64(uint64_t n)
{
#ifdef DR_FLAC_HAS_BYTESWAP64_INTRINSIC
    #if defined(_MSC_VER) && !defined(__clang__)
        return _byteswap_uint64(n);
    #elif defined(__GNUC__) || defined(__clang__)
        return __builtin_bswap64(n);
    #elif defined(__WATCOMC__) && defined(__386__)
        return _watcom_bswap64(n);
    #else
        #error "This compiler does not support the byte swap intrinsic."
    #endif
#else
    return ((n & ((uint64_t)0xFF000000 << 32)) >> 56) |
           ((n & ((uint64_t)0x00FF0000 << 32)) >> 40) |
           ((n & ((uint64_t)0x0000FF00 << 32)) >> 24) |
           ((n & ((uint64_t)0x000000FF << 32)) >>  8) |
           ((n & ((uint64_t)0xFF000000      )) <<  8) |
           ((n & ((uint64_t)0x00FF0000      )) << 24) |
           ((n & ((uint64_t)0x0000FF00      )) << 40) |
           ((n & ((uint64_t)0x000000FF      )) << 56);
#endif
}
static DRFLAC_INLINE uint16_t drflac__be2host_16(uint16_t n)
{
    if (drflac__is_little_endian()) {
        return drflac__swap_endian_uint16(n);
    }
    return n;
}
static DRFLAC_INLINE uint32_t drflac__be2host_32(uint32_t n)
{
    if (drflac__is_little_endian()) {
        return drflac__swap_endian_uint32(n);
    }
    return n;
}
static DRFLAC_INLINE uint32_t drflac__be2host_32_ptr_unaligned(const void* pData)
{
    const uint8_t* pNum = (uint8_t*)pData;
    return *(pNum) << 24 | *(pNum+1) << 16 | *(pNum+2) << 8 | *(pNum+3);
}
static DRFLAC_INLINE uint64_t drflac__be2host_64(uint64_t n)
{
    if (drflac__is_little_endian()) {
        return drflac__swap_endian_uint64(n);
    }
    return n;
}
static DRFLAC_INLINE uint32_t drflac__le2host_32(uint32_t n)
{
    if (!drflac__is_little_endian()) {
        return drflac__swap_endian_uint32(n);
    }
    return n;
}
static DRFLAC_INLINE uint32_t drflac__le2host_32_ptr_unaligned(const void* pData)
{
    const uint8_t* pNum = (uint8_t*)pData;
    return *pNum | *(pNum+1) << 8 |  *(pNum+2) << 16 | *(pNum+3) << 24;
}
static DRFLAC_INLINE uint32_t drflac__unsynchsafe_32(uint32_t n)
{
    uint32_t result = 0;
    result |= (n & 0x7F000000) >> 3;
    result |= (n & 0x007F0000) >> 2;
    result |= (n & 0x00007F00) >> 1;
    result |= (n & 0x0000007F) >> 0;
    return result;
}
static uint8_t drflac__crc8_table[] = {
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15, 0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65, 0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5, 0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85, 0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2, 0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2, 0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32, 0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42, 0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C, 0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC, 0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C, 0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C, 0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B, 0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B, 0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB, 0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB, 0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3
};
static uint16_t drflac__crc16_table[] = {
    0x0000, 0x8005, 0x800F, 0x000A, 0x801B, 0x001E, 0x0014, 0x8011,
    0x8033, 0x0036, 0x003C, 0x8039, 0x0028, 0x802D, 0x8027, 0x0022,
    0x8063, 0x0066, 0x006C, 0x8069, 0x0078, 0x807D, 0x8077, 0x0072,
    0x0050, 0x8055, 0x805F, 0x005A, 0x804B, 0x004E, 0x0044, 0x8041,
    0x80C3, 0x00C6, 0x00CC, 0x80C9, 0x00D8, 0x80DD, 0x80D7, 0x00D2,
    0x00F0, 0x80F5, 0x80FF, 0x00FA, 0x80EB, 0x00EE, 0x00E4, 0x80E1,
    0x00A0, 0x80A5, 0x80AF, 0x00AA, 0x80BB, 0x00BE, 0x00B4, 0x80B1,
    0x8093, 0x0096, 0x009C, 0x8099, 0x0088, 0x808D, 0x8087, 0x0082,
    0x8183, 0x0186, 0x018C, 0x8189, 0x0198, 0x819D, 0x8197, 0x0192,
    0x01B0, 0x81B5, 0x81BF, 0x01BA, 0x81AB, 0x01AE, 0x01A4, 0x81A1,
    0x01E0, 0x81E5, 0x81EF, 0x01EA, 0x81FB, 0x01FE, 0x01F4, 0x81F1,
    0x81D3, 0x01D6, 0x01DC, 0x81D9, 0x01C8, 0x81CD, 0x81C7, 0x01C2,
    0x0140, 0x8145, 0x814F, 0x014A, 0x815B, 0x015E, 0x0154, 0x8151,
    0x8173, 0x0176, 0x017C, 0x8179, 0x0168, 0x816D, 0x8167, 0x0162,
    0x8123, 0x0126, 0x012C, 0x8129, 0x0138, 0x813D, 0x8137, 0x0132,
    0x0110, 0x8115, 0x811F, 0x011A, 0x810B, 0x010E, 0x0104, 0x8101,
    0x8303, 0x0306, 0x030C, 0x8309, 0x0318, 0x831D, 0x8317, 0x0312,
    0x0330, 0x8335, 0x833F, 0x033A, 0x832B, 0x032E, 0x0324, 0x8321,
    0x0360, 0x8365, 0x836F, 0x036A, 0x837B, 0x037E, 0x0374, 0x8371,
    0x8353, 0x0356, 0x035C, 0x8359, 0x0348, 0x834D, 0x8347, 0x0342,
    0x03C0, 0x83C5, 0x83CF, 0x03CA, 0x83DB, 0x03DE, 0x03D4, 0x83D1,
    0x83F3, 0x03F6, 0x03FC, 0x83F9, 0x03E8, 0x83ED, 0x83E7, 0x03E2,
    0x83A3, 0x03A6, 0x03AC, 0x83A9, 0x03B8, 0x83BD, 0x83B7, 0x03B2,
    0x0390, 0x8395, 0x839F, 0x039A, 0x838B, 0x038E, 0x0384, 0x8381,
    0x0280, 0x8285, 0x828F, 0x028A, 0x829B, 0x029E, 0x0294, 0x8291,
    0x82B3, 0x02B6, 0x02BC, 0x82B9, 0x02A8, 0x82AD, 0x82A7, 0x02A2,
    0x82E3, 0x02E6, 0x02EC, 0x82E9, 0x02F8, 0x82FD, 0x82F7, 0x02F2,
    0x02D0, 0x82D5, 0x82DF, 0x02DA, 0x82CB, 0x02CE, 0x02C4, 0x82C1,
    0x8243, 0x0246, 0x024C, 0x8249, 0x0258, 0x825D, 0x8257, 0x0252,
    0x0270, 0x8275, 0x827F, 0x027A, 0x826B, 0x026E, 0x0264, 0x8261,
    0x0220, 0x8225, 0x822F, 0x022A, 0x823B, 0x023E, 0x0234, 0x8231,
    0x8213, 0x0216, 0x021C, 0x8219, 0x0208, 0x820D, 0x8207, 0x0202
};
static DRFLAC_INLINE uint8_t drflac_crc8_byte(uint8_t crc, uint8_t data)
{
    return drflac__crc8_table[crc ^ data];
}
static DRFLAC_INLINE uint8_t drflac_crc8(uint8_t crc, uint32_t data, uint32_t count)
{
#ifdef DR_FLAC_NO_CRC
    (void)crc;
    (void)data;
    (void)count;
    return 0;
#else
#if 0
    uint8_t p = 0x07;
    for (int i = count-1; i >= 0; --i) {
        uint8_t bit = (data & (1 << i)) >> i;
        if (crc & 0x80) {
            crc = ((crc << 1) | bit) ^ p;
        } else {
            crc = ((crc << 1) | bit);
        }
    }
    return crc;
#else
    uint32_t wholeBytes;
    uint32_t leftoverBits;
    uint64_t leftoverDataMask;
    static uint64_t leftoverDataMaskTable[8] = {
        0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F
    };
    DR_FLAC_ASSERT(count <= 32);
    wholeBytes = count >> 3;
    leftoverBits = count - (wholeBytes*8);
    leftoverDataMask = leftoverDataMaskTable[leftoverBits];
    switch (wholeBytes) {
        case 4: crc = drflac_crc8_byte(crc, (uint8_t)((data & (0xFF000000UL << leftoverBits)) >> (24 + leftoverBits)));
        case 3: crc = drflac_crc8_byte(crc, (uint8_t)((data & (0x00FF0000UL << leftoverBits)) >> (16 + leftoverBits)));
        case 2: crc = drflac_crc8_byte(crc, (uint8_t)((data & (0x0000FF00UL << leftoverBits)) >> ( 8 + leftoverBits)));
        case 1: crc = drflac_crc8_byte(crc, (uint8_t)((data & (0x000000FFUL << leftoverBits)) >> ( 0 + leftoverBits)));
        case 0: if (leftoverBits > 0) crc = (uint8_t)((crc << leftoverBits) ^ drflac__crc8_table[(crc >> (8 - leftoverBits)) ^ (data & leftoverDataMask)]);
    }
    return crc;
#endif
#endif
}
static DRFLAC_INLINE uint16_t drflac_crc16_byte(uint16_t crc, uint8_t data)
{
    return (crc << 8) ^ drflac__crc16_table[(uint8_t)(crc >> 8) ^ data];
}
static DRFLAC_INLINE uint16_t drflac_crc16_cache(uint16_t crc, drflac_cache_t data)
{
#if 1
    crc = drflac_crc16_byte(crc, (uint8_t)((data >> 56) & 0xFF));
    crc = drflac_crc16_byte(crc, (uint8_t)((data >> 48) & 0xFF));
    crc = drflac_crc16_byte(crc, (uint8_t)((data >> 40) & 0xFF));
    crc = drflac_crc16_byte(crc, (uint8_t)((data >> 32) & 0xFF));
#endif
    crc = drflac_crc16_byte(crc, (uint8_t)((data >> 24) & 0xFF));
    crc = drflac_crc16_byte(crc, (uint8_t)((data >> 16) & 0xFF));
    crc = drflac_crc16_byte(crc, (uint8_t)((data >>  8) & 0xFF));
    crc = drflac_crc16_byte(crc, (uint8_t)((data >>  0) & 0xFF));
    return crc;
}
static DRFLAC_INLINE uint16_t drflac_crc16_bytes(uint16_t crc, drflac_cache_t data, uint32_t byteCount)
{
    switch (byteCount)
    {
#if 1
    case 8: crc = drflac_crc16_byte(crc, (uint8_t)((data >> 56) & 0xFF));
    case 7: crc = drflac_crc16_byte(crc, (uint8_t)((data >> 48) & 0xFF));
    case 6: crc = drflac_crc16_byte(crc, (uint8_t)((data >> 40) & 0xFF));
    case 5: crc = drflac_crc16_byte(crc, (uint8_t)((data >> 32) & 0xFF));
#endif
    case 4: crc = drflac_crc16_byte(crc, (uint8_t)((data >> 24) & 0xFF));
    case 3: crc = drflac_crc16_byte(crc, (uint8_t)((data >> 16) & 0xFF));
    case 2: crc = drflac_crc16_byte(crc, (uint8_t)((data >>  8) & 0xFF));
    case 1: crc = drflac_crc16_byte(crc, (uint8_t)((data >>  0) & 0xFF));
    }
    return crc;
}
#if 0
static DRFLAC_INLINE uint16_t drflac_crc16__32bit(uint16_t crc, uint32_t data, uint32_t count)
{
#ifdef DR_FLAC_NO_CRC
    (void)crc;
    (void)data;
    (void)count;
    return 0;
#else
#if 0
    uint16_t p = 0x8005;
    for (int i = count-1; i >= 0; --i) {
        uint16_t bit = (data & (1ULL << i)) >> i;
        if (r & 0x8000) {
            r = ((r << 1) | bit) ^ p;
        } else {
            r = ((r << 1) | bit);
        }
    }
    return crc;
#else
    uint32_t wholeBytes;
    uint32_t leftoverBits;
    uint64_t leftoverDataMask;
    static uint64_t leftoverDataMaskTable[8] = {
        0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F
    };
    DR_FLAC_ASSERT(count <= 64);
    wholeBytes = count >> 3;
    leftoverBits = count & 7;
    leftoverDataMask = leftoverDataMaskTable[leftoverBits];
    switch (wholeBytes) {
        default:
        case 4: crc = drflac_crc16_byte(crc, (uint8_t)((data & (0xFF000000UL << leftoverBits)) >> (24 + leftoverBits)));
        case 3: crc = drflac_crc16_byte(crc, (uint8_t)((data & (0x00FF0000UL << leftoverBits)) >> (16 + leftoverBits)));
        case 2: crc = drflac_crc16_byte(crc, (uint8_t)((data & (0x0000FF00UL << leftoverBits)) >> ( 8 + leftoverBits)));
        case 1: crc = drflac_crc16_byte(crc, (uint8_t)((data & (0x000000FFUL << leftoverBits)) >> ( 0 + leftoverBits)));
        case 0: if (leftoverBits > 0) crc = (crc << leftoverBits) ^ drflac__crc16_table[(crc >> (16 - leftoverBits)) ^ (data & leftoverDataMask)];
    }
    return crc;
#endif
#endif
}
static DRFLAC_INLINE uint16_t drflac_crc16__64bit(uint16_t crc, uint64_t data, uint32_t count)
{
#ifdef DR_FLAC_NO_CRC
    (void)crc;
    (void)data;
    (void)count;
    return 0;
#else
    uint32_t wholeBytes;
    uint32_t leftoverBits;
    uint64_t leftoverDataMask;
    static uint64_t leftoverDataMaskTable[8] = {
        0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F
    };
    DR_FLAC_ASSERT(count <= 64);
    wholeBytes = count >> 3;
    leftoverBits = count & 7;
    leftoverDataMask = leftoverDataMaskTable[leftoverBits];
    switch (wholeBytes) {
        default:
        case 8: crc = drflac_crc16_byte(crc, (uint8_t)((data & (((uint64_t)0xFF000000 << 32) << leftoverBits)) >> (56 + leftoverBits)));
        case 7: crc = drflac_crc16_byte(crc, (uint8_t)((data & (((uint64_t)0x00FF0000 << 32) << leftoverBits)) >> (48 + leftoverBits)));
        case 6: crc = drflac_crc16_byte(crc, (uint8_t)((data & (((uint64_t)0x0000FF00 << 32) << leftoverBits)) >> (40 + leftoverBits)));
        case 5: crc = drflac_crc16_byte(crc, (uint8_t)((data & (((uint64_t)0x000000FF << 32) << leftoverBits)) >> (32 + leftoverBits)));
        case 4: crc = drflac_crc16_byte(crc, (uint8_t)((data & (((uint64_t)0xFF000000      ) << leftoverBits)) >> (24 + leftoverBits)));
        case 3: crc = drflac_crc16_byte(crc, (uint8_t)((data & (((uint64_t)0x00FF0000      ) << leftoverBits)) >> (16 + leftoverBits)));
        case 2: crc = drflac_crc16_byte(crc, (uint8_t)((data & (((uint64_t)0x0000FF00      ) << leftoverBits)) >> ( 8 + leftoverBits)));
        case 1: crc = drflac_crc16_byte(crc, (uint8_t)((data & (((uint64_t)0x000000FF      ) << leftoverBits)) >> ( 0 + leftoverBits)));
        case 0: if (leftoverBits > 0) crc = (crc << leftoverBits) ^ drflac__crc16_table[(crc >> (16 - leftoverBits)) ^ (data & leftoverDataMask)];
    }
    return crc;
#endif
}
static DRFLAC_INLINE uint16_t drflac_crc16(uint16_t crc, drflac_cache_t data, uint32_t count)
{
#if 1
    return drflac_crc16__64bit(crc, data, count);
#else
    return drflac_crc16__32bit(crc, data, count);
#endif
}
#endif
#if 1
#define drflac__be2host__cache_line drflac__be2host_64
#else
#define drflac__be2host__cache_line drflac__be2host_32
#endif
#define DR_FLAC_CACHE_L1_SIZE_BYTES(bs)                      (sizeof((bs)->cache))
#define DR_FLAC_CACHE_L1_SIZE_BITS(bs)                       (sizeof((bs)->cache)*8)
#define DR_FLAC_CACHE_L1_BITS_REMAINING(bs)                  (DR_FLAC_CACHE_L1_SIZE_BITS(bs) - (bs)->consumedBits)
#define DR_FLAC_CACHE_L1_SELECTION_MASK(_bitCount)           (~((~(drflac_cache_t)0) >> (_bitCount)))
#define DR_FLAC_CACHE_L1_SELECTION_SHIFT(bs, _bitCount)      (DR_FLAC_CACHE_L1_SIZE_BITS(bs) - (_bitCount))
#define DR_FLAC_CACHE_L1_SELECT(bs, _bitCount)               (((bs)->cache) & DR_FLAC_CACHE_L1_SELECTION_MASK(_bitCount))
#define DR_FLAC_CACHE_L1_SELECT_AND_SHIFT(bs, _bitCount)     (DR_FLAC_CACHE_L1_SELECT((bs), (_bitCount)) >>  DR_FLAC_CACHE_L1_SELECTION_SHIFT((bs), (_bitCount)))
#define DR_FLAC_CACHE_L1_SELECT_AND_SHIFT_SAFE(bs, _bitCount)(DR_FLAC_CACHE_L1_SELECT((bs), (_bitCount)) >> (DR_FLAC_CACHE_L1_SELECTION_SHIFT((bs), (_bitCount)) & (DR_FLAC_CACHE_L1_SIZE_BITS(bs)-1)))
#define DR_FLAC_CACHE_L2_SIZE_BYTES(bs)                      (sizeof((bs)->cacheL2))
#define DR_FLAC_CACHE_L2_LINE_COUNT(bs)                      (DR_FLAC_CACHE_L2_SIZE_BYTES(bs) / sizeof((bs)->cacheL2[0]))
#define DR_FLAC_CACHE_L2_LINES_REMAINING(bs)                 (DR_FLAC_CACHE_L2_LINE_COUNT(bs) - (bs)->nextL2Line)
#ifndef DR_FLAC_NO_CRC
static DRFLAC_INLINE void drflac__reset_crc16(drflac_bs* bs)
{
    bs->crc16 = 0;
    bs->crc16CacheIgnoredBytes = bs->consumedBits >> 3;
}
static DRFLAC_INLINE void drflac__update_crc16(drflac_bs* bs)
{
    if (bs->crc16CacheIgnoredBytes == 0) {
        bs->crc16 = drflac_crc16_cache(bs->crc16, bs->crc16Cache);
    } else {
        bs->crc16 = drflac_crc16_bytes(bs->crc16, bs->crc16Cache, DR_FLAC_CACHE_L1_SIZE_BYTES(bs) - bs->crc16CacheIgnoredBytes);
        bs->crc16CacheIgnoredBytes = 0;
    }
}
static DRFLAC_INLINE uint16_t drflac__flush_crc16(drflac_bs* bs)
{
    DR_FLAC_ASSERT((DR_FLAC_CACHE_L1_BITS_REMAINING(bs) & 7) == 0);
    if (DR_FLAC_CACHE_L1_BITS_REMAINING(bs) == 0) {
        drflac__update_crc16(bs);
    } else {
        bs->crc16 = drflac_crc16_bytes(bs->crc16, bs->crc16Cache >> DR_FLAC_CACHE_L1_BITS_REMAINING(bs), (bs->consumedBits >> 3) - bs->crc16CacheIgnoredBytes);
        bs->crc16CacheIgnoredBytes = bs->consumedBits >> 3;
    }
    return bs->crc16;
}
#endif
static DRFLAC_INLINE int drflac__reload_l1_cache_from_l2(drflac_bs* bs)
{
    size_t bytesRead;
    size_t alignedL1LineCount;
    if (bs->nextL2Line < DR_FLAC_CACHE_L2_LINE_COUNT(bs)) {
        bs->cache = bs->cacheL2[bs->nextL2Line++];
        return 1;
    }
    if (bs->unalignedByteCount > 0) {
        return 0;
    }
    bytesRead = bs->onRead(bs->pUserData, bs->cacheL2, DR_FLAC_CACHE_L2_SIZE_BYTES(bs));
    bs->nextL2Line = 0;
    if (bytesRead == DR_FLAC_CACHE_L2_SIZE_BYTES(bs)) {
        bs->cache = bs->cacheL2[bs->nextL2Line++];
        return 1;
    }
    alignedL1LineCount = bytesRead / DR_FLAC_CACHE_L1_SIZE_BYTES(bs);
    bs->unalignedByteCount = bytesRead - (alignedL1LineCount * DR_FLAC_CACHE_L1_SIZE_BYTES(bs));
    if (bs->unalignedByteCount > 0) {
        bs->unalignedCache = bs->cacheL2[alignedL1LineCount];
    }
    if (alignedL1LineCount > 0) {
        size_t offset = DR_FLAC_CACHE_L2_LINE_COUNT(bs) - alignedL1LineCount;
        size_t i;
        for (i = alignedL1LineCount; i > 0; --i) {
            bs->cacheL2[i-1 + offset] = bs->cacheL2[i-1];
        }
        bs->nextL2Line = (uint32_t)offset;
        bs->cache = bs->cacheL2[bs->nextL2Line++];
        return 1;
    } else {
        bs->nextL2Line = DR_FLAC_CACHE_L2_LINE_COUNT(bs);
        return 0;
    }
}
static int drflac__reload_cache(drflac_bs* bs)
{
    size_t bytesRead;
#ifndef DR_FLAC_NO_CRC
    drflac__update_crc16(bs);
#endif
    if (drflac__reload_l1_cache_from_l2(bs)) {
        bs->cache = drflac__be2host__cache_line(bs->cache);
        bs->consumedBits = 0;
#ifndef DR_FLAC_NO_CRC
        bs->crc16Cache = bs->cache;
#endif
        return 1;
    }
    bytesRead = bs->unalignedByteCount;
    if (bytesRead == 0) {
        bs->consumedBits = DR_FLAC_CACHE_L1_SIZE_BITS(bs);
        return 0;
    }
    DR_FLAC_ASSERT(bytesRead < DR_FLAC_CACHE_L1_SIZE_BYTES(bs));
    bs->consumedBits = (uint32_t)(DR_FLAC_CACHE_L1_SIZE_BYTES(bs) - bytesRead) * 8;
    bs->cache = drflac__be2host__cache_line(bs->unalignedCache);
    bs->cache &= DR_FLAC_CACHE_L1_SELECTION_MASK(DR_FLAC_CACHE_L1_BITS_REMAINING(bs));
    bs->unalignedByteCount = 0;
#ifndef DR_FLAC_NO_CRC
    bs->crc16Cache = bs->cache >> bs->consumedBits;
    bs->crc16CacheIgnoredBytes = bs->consumedBits >> 3;
#endif
    return 1;
}
static void drflac__reset_cache(drflac_bs* bs)
{
    bs->nextL2Line   = DR_FLAC_CACHE_L2_LINE_COUNT(bs);
    bs->consumedBits = DR_FLAC_CACHE_L1_SIZE_BITS(bs);
    bs->cache = 0;
    bs->unalignedByteCount = 0;
    bs->unalignedCache = 0;
#ifndef DR_FLAC_NO_CRC
    bs->crc16Cache = 0;
    bs->crc16CacheIgnoredBytes = 0;
#endif
}
static DRFLAC_INLINE int drflac__read_uint32(drflac_bs* bs, unsigned int bitCount, uint32_t* pResultOut)
{
    DR_FLAC_ASSERT(bs != NULL);
    DR_FLAC_ASSERT(pResultOut != NULL);
    DR_FLAC_ASSERT(bitCount > 0);
    DR_FLAC_ASSERT(bitCount <= 32);
    if (bs->consumedBits == DR_FLAC_CACHE_L1_SIZE_BITS(bs)) {
        if (!drflac__reload_cache(bs)) {
            return 0;
        }
    }
    if (bitCount <= DR_FLAC_CACHE_L1_BITS_REMAINING(bs)) {
#if 1
        *pResultOut = (uint32_t)DR_FLAC_CACHE_L1_SELECT_AND_SHIFT(bs, bitCount);
        bs->consumedBits += bitCount;
        bs->cache <<= bitCount;
#else
        if (bitCount < DR_FLAC_CACHE_L1_SIZE_BITS(bs)) {
            *pResultOut = (uint32_t)DR_FLAC_CACHE_L1_SELECT_AND_SHIFT(bs, bitCount);
            bs->consumedBits += bitCount;
            bs->cache <<= bitCount;
        } else {
            *pResultOut = (uint32_t)bs->cache;
            bs->consumedBits = DR_FLAC_CACHE_L1_SIZE_BITS(bs);
            bs->cache = 0;
        }
#endif
        return 1;
    } else {
        uint32_t bitCountHi = DR_FLAC_CACHE_L1_BITS_REMAINING(bs);
        uint32_t bitCountLo = bitCount - bitCountHi;
        uint32_t resultHi;
        DR_FLAC_ASSERT(bitCountHi > 0);
        DR_FLAC_ASSERT(bitCountHi < 32);
        resultHi = (uint32_t)DR_FLAC_CACHE_L1_SELECT_AND_SHIFT(bs, bitCountHi);
        if (!drflac__reload_cache(bs)) {
            return 0;
        }
        if (bitCountLo > DR_FLAC_CACHE_L1_BITS_REMAINING(bs)) {
            return 0;
        }
        *pResultOut = (resultHi << bitCountLo) | (uint32_t)DR_FLAC_CACHE_L1_SELECT_AND_SHIFT(bs, bitCountLo);
        bs->consumedBits += bitCountLo;
        bs->cache <<= bitCountLo;
        return 1;
    }
}
static int drflac__read_int32(drflac_bs* bs, unsigned int bitCount, int32_t* pResult)
{
    uint32_t result;
    DR_FLAC_ASSERT(bs != NULL);
    DR_FLAC_ASSERT(pResult != NULL);
    DR_FLAC_ASSERT(bitCount > 0);
    DR_FLAC_ASSERT(bitCount <= 32);
    if (!drflac__read_uint32(bs, bitCount, &result)) {
        return 0;
    }
    if (bitCount < 32) {
        uint32_t signbit;
        signbit = ((result >> (bitCount-1)) & 0x01);
        result |= (~signbit + 1) << bitCount;
    }
    *pResult = (int32_t)result;
    return 1;
}
static int drflac__read_uint64(drflac_bs* bs, unsigned int bitCount, uint64_t* pResultOut)
{
    uint32_t resultHi = 0;
    uint32_t resultLo;
    DR_FLAC_ASSERT(bs != NULL);
    DR_FLAC_ASSERT(pResultOut != NULL);
    DR_FLAC_ASSERT(bitCount > 0);
    DR_FLAC_ASSERT(bitCount <= 64);
    if (bitCount > 32) {
        if (!drflac__read_uint32(bs, bitCount - 32, &resultHi)) {
            return 0;
        }
        bitCount = 32;
    }
    if (!drflac__read_uint32(bs, bitCount, &resultLo)) {
        return 0;
    }
    *pResultOut = (((uint64_t)resultHi) << 32) | ((uint64_t)resultLo);
    return 1;
}
static DRFLAC_INLINE int drflac__read_int64(drflac_bs* bs, unsigned int bitCount, int64_t* pResultOut)
{
    uint64_t result;
    DR_FLAC_ASSERT(bs != NULL);
    DR_FLAC_ASSERT(pResultOut != NULL);
    DR_FLAC_ASSERT(bitCount > 0);
    DR_FLAC_ASSERT(bitCount <= 64);
    if (!drflac__read_uint64(bs, bitCount, &result)) {
        return 0;
    }
    if (bitCount < 64) {
        uint64_t signbit;
        signbit = ((result >> (bitCount-1)) & 0x01);
        result |= (~signbit + 1) << bitCount;
    }
    *pResultOut = (int64_t)result;
    return 1;
}
static int drflac__read_uint16(drflac_bs* bs, unsigned int bitCount, uint16_t* pResult)
{
    uint32_t result;
    DR_FLAC_ASSERT(bs != NULL);
    DR_FLAC_ASSERT(pResult != NULL);
    DR_FLAC_ASSERT(bitCount > 0);
    DR_FLAC_ASSERT(bitCount <= 16);
    if (!drflac__read_uint32(bs, bitCount, &result)) {
        return 0;
    }
    *pResult = (uint16_t)result;
    return 1;
}
#if 0
static int drflac__read_int16(drflac_bs* bs, unsigned int bitCount, int16_t* pResult)
{
    int32_t result;
    DR_FLAC_ASSERT(bs != NULL);
    DR_FLAC_ASSERT(pResult != NULL);
    DR_FLAC_ASSERT(bitCount > 0);
    DR_FLAC_ASSERT(bitCount <= 16);
    if (!drflac__read_int32(bs, bitCount, &result)) {
        return 0;
    }
    *pResult = (int16_t)result;
    return 1;
}
#endif
static int drflac__read_uint8(drflac_bs* bs, unsigned int bitCount, uint8_t* pResult)
{
    uint32_t result;
    DR_FLAC_ASSERT(bs != NULL);
    DR_FLAC_ASSERT(pResult != NULL);
    DR_FLAC_ASSERT(bitCount > 0);
    DR_FLAC_ASSERT(bitCount <= 8);
    if (!drflac__read_uint32(bs, bitCount, &result)) {
        return 0;
    }
    *pResult = (uint8_t)result;
    return 1;
}
static int drflac__read_int8(drflac_bs* bs, unsigned int bitCount, int8_t* pResult)
{
    int32_t result;
    DR_FLAC_ASSERT(bs != NULL);
    DR_FLAC_ASSERT(pResult != NULL);
    DR_FLAC_ASSERT(bitCount > 0);
    DR_FLAC_ASSERT(bitCount <= 8);
    if (!drflac__read_int32(bs, bitCount, &result)) {
        return 0;
    }
    *pResult = (int8_t)result;
    return 1;
}
static int drflac__seek_bits(drflac_bs* bs, size_t bitsToSeek)
{
    if (bitsToSeek <= DR_FLAC_CACHE_L1_BITS_REMAINING(bs)) {
        bs->consumedBits += (uint32_t)bitsToSeek;
        bs->cache <<= bitsToSeek;
        return 1;
    } else {
        bitsToSeek       -= DR_FLAC_CACHE_L1_BITS_REMAINING(bs);
        bs->consumedBits += DR_FLAC_CACHE_L1_BITS_REMAINING(bs);
        bs->cache         = 0;
#if 1
        while (bitsToSeek >= DR_FLAC_CACHE_L1_SIZE_BITS(bs)) {
            uint64_t bin;
            if (!drflac__read_uint64(bs, DR_FLAC_CACHE_L1_SIZE_BITS(bs), &bin)) {
                return 0;
            }
            bitsToSeek -= DR_FLAC_CACHE_L1_SIZE_BITS(bs);
        }
#else
        while (bitsToSeek >= DR_FLAC_CACHE_L1_SIZE_BITS(bs)) {
            uint32_t bin;
            if (!drflac__read_uint32(bs, DR_FLAC_CACHE_L1_SIZE_BITS(bs), &bin)) {
                return 0;
            }
            bitsToSeek -= DR_FLAC_CACHE_L1_SIZE_BITS(bs);
        }
#endif
        while (bitsToSeek >= 8) {
            uint8_t bin;
            if (!drflac__read_uint8(bs, 8, &bin)) {
                return 0;
            }
            bitsToSeek -= 8;
        }
        if (bitsToSeek > 0) {
            uint8_t bin;
            if (!drflac__read_uint8(bs, (uint32_t)bitsToSeek, &bin)) {
                return 0;
            }
            bitsToSeek = 0;
        }
        DR_FLAC_ASSERT(bitsToSeek == 0);
        return 1;
    }
}
static int drflac__find_and_seek_to_next_sync_code(drflac_bs* bs)
{
    DR_FLAC_ASSERT(bs != NULL);
    if (!drflac__seek_bits(bs, DR_FLAC_CACHE_L1_BITS_REMAINING(bs) & 7)) {
        return 0;
    }
    for (;;) {
        uint8_t hi;
#ifndef DR_FLAC_NO_CRC
        drflac__reset_crc16(bs);
#endif
        if (!drflac__read_uint8(bs, 8, &hi)) {
            return 0;
        }
        while (hi == 0xFF) {
            uint8_t lo;
            uint8_t trailingBits;
            if (!drflac__read_uint8(bs, 6, &lo)) {
                return 0;
            }
            if (lo == 0x3E) {
                return 1;
            }
            if (!drflac__read_uint8(bs, 2, &trailingBits)) {
                return 0;
            }
            hi = (uint8_t)((lo << 2) | trailingBits);
            if (hi == 0xFF) {
                #ifndef DR_FLAC_NO_CRC
                {
                    drflac__reset_crc16(bs);
                    bs->crc16 = drflac_crc16_byte(0, hi);
                }
                #endif
            }
        }
    }
}
#if defined(DR_FLAC_HAS_LZCNT_INTRINSIC)
#define DR_FLAC_IMPLEMENT_CLZ_LZCNT
#endif
#if  defined(_MSC_VER) && _MSC_VER >= 1400 && (defined(MA_X64) || defined(MA_X86)) && !defined(__clang__)
#define DR_FLAC_IMPLEMENT_CLZ_MSVC
#endif
#if  defined(__WATCOMC__) && defined(__386__)
#define DR_FLAC_IMPLEMENT_CLZ_WATCOM
#endif
#ifdef __MRC__
#include <intrinsics.h>
#define DR_FLAC_IMPLEMENT_CLZ_MRC
#endif
static DRFLAC_INLINE uint32_t drflac__clz_software(drflac_cache_t x)
{
    uint32_t n;
    static uint32_t clz_table_4[] = {
        0,
        4,
        3, 3,
        2, 2, 2, 2,
        1, 1, 1, 1, 1, 1, 1, 1
    };
    if (x == 0) {
        return sizeof(x)*8;
    }
    n = clz_table_4[x >> (sizeof(x)*8 - 4)];
    if (n == 0) {
#if 1
        if ((x & ((uint64_t)0xFFFFFFFF << 32)) == 0) { n  = 32; x <<= 32; }
        if ((x & ((uint64_t)0xFFFF0000 << 32)) == 0) { n += 16; x <<= 16; }
        if ((x & ((uint64_t)0xFF000000 << 32)) == 0) { n += 8;  x <<= 8;  }
        if ((x & ((uint64_t)0xF0000000 << 32)) == 0) { n += 4;  x <<= 4;  }
#else
        if ((x & 0xFFFF0000) == 0) { n  = 16; x <<= 16; }
        if ((x & 0xFF000000) == 0) { n += 8;  x <<= 8;  }
        if ((x & 0xF0000000) == 0) { n += 4;  x <<= 4;  }
#endif
        n += clz_table_4[x >> (sizeof(x)*8 - 4)];
    }
    return n - 1;
}
#ifdef DR_FLAC_IMPLEMENT_CLZ_LZCNT
static DRFLAC_INLINE int drflac__is_lzcnt_supported(void)
{
#if defined(DR_FLAC_HAS_LZCNT_INTRINSIC) && defined(MA_ARM) && (defined(__ARM_ARCH) && __ARM_ARCH >= 5)
    return 1;
#elif defined(__MRC__)
    return 1;
#else
    #ifdef DR_FLAC_HAS_LZCNT_INTRINSIC
        return drflac__gIsLZCNTSupported;
    #else
        return 0;
    #endif
#endif
}
static DRFLAC_INLINE uint32_t drflac__clz_lzcnt(drflac_cache_t x)
{
#if defined(_MSC_VER)
    #if 1
        return (uint32_t)__lzcnt64(x);
    #else
        return (uint32_t)__lzcnt(x);
    #endif
#else
    #if defined(__GNUC__) || defined(__clang__)
        #if defined(MA_X64)
            {
                uint64_t r;
                __asm__ __volatile__ (
                    "rep; bsr{q %1, %0| %0, %1}" : "=r"(r) : "r"(x) : "cc"
                );
                return (uint32_t)r;
            }
        #elif defined(MA_X86)
            {
                uint32_t r;
                __asm__ __volatile__ (
                    "rep; bsr{l %1, %0| %0, %1}" : "=r"(r) : "r"(x) : "cc"
                );
                return r;
            }
        #elif defined(MA_ARM) && (defined(__ARM_ARCH) && __ARM_ARCH >= 5) && !defined(__ARM_ARCH_6M__) && !(defined(__thumb__) && !defined(__thumb2__)) && !defined(DR_FLAC_64BIT)
            {
                unsigned int r;
                __asm__ __volatile__ (
                #if defined(DR_FLAC_64BIT)
                    "clz %w[out], %w[in]" : [out]"=r"(r) : [in]"r"(x)
                #else
                    "clz %[out], %[in]" : [out]"=r"(r) : [in]"r"(x)
                #endif
                );
                return r;
            }
        #else
            if (x == 0) {
                return sizeof(x)*8;
            }
            #if 1
                return (uint32_t)__builtin_clzll((uint64_t)x);
            #else
                return (uint32_t)__builtin_clzl((uint32_t)x);
            #endif
        #endif
    #else
        #error "This compiler does not support the lzcnt intrinsic."
    #endif
#endif
}
#endif
#ifdef DR_FLAC_IMPLEMENT_CLZ_MSVC
#include <intrin.h>
static DRFLAC_INLINE uint32_t drflac__clz_msvc(drflac_cache_t x)
{
    uint32_t n;
    if (x == 0) {
        return sizeof(x)*8;
    }
#if 1
    _BitScanReverse64((unsigned long*)&n, x);
#else
    _BitScanReverse((unsigned long*)&n, x);
#endif
    return sizeof(x)*8 - n - 1;
}
#endif
#ifdef DR_FLAC_IMPLEMENT_CLZ_WATCOM
static __inline uint32_t drflac__clz_watcom (uint32_t);
#ifdef DR_FLAC_IMPLEMENT_CLZ_WATCOM_LZCNT
#pragma aux drflac__clz_watcom_lzcnt = \
    "db 0F3h, 0Fh, 0BDh, 0C0h"  \
    parm [eax] \
    value [eax] \
    modify nomemory;
#else
#pragma aux drflac__clz_watcom = \
    "bsr eax, eax" \
    "xor eax, 31" \
    parm [eax] nomemory \
    value [eax] \
    modify exact [eax] nomemory;
#endif
#endif
static DRFLAC_INLINE uint32_t drflac__clz(drflac_cache_t x)
{
#ifdef DR_FLAC_IMPLEMENT_CLZ_LZCNT
    if (drflac__is_lzcnt_supported()) {
        return drflac__clz_lzcnt(x);
    } else
#endif
    {
#ifdef DR_FLAC_IMPLEMENT_CLZ_MSVC
        return drflac__clz_msvc(x);
#elif defined(DR_FLAC_IMPLEMENT_CLZ_WATCOM_LZCNT)
        return drflac__clz_watcom_lzcnt(x);
#elif defined(DR_FLAC_IMPLEMENT_CLZ_WATCOM)
        return (x == 0) ? sizeof(x)*8 : drflac__clz_watcom(x);
#elif defined(__MRC__)
        return __cntlzw(x);
#else
        return drflac__clz_software(x);
#endif
    }
}
static DRFLAC_INLINE int drflac__seek_past_next_set_bit(drflac_bs* bs, unsigned int* pOffsetOut)
{
    uint32_t zeroCounter = 0;
    uint32_t setBitOffsetPlus1;
    while (bs->cache == 0) {
        zeroCounter += (uint32_t)DR_FLAC_CACHE_L1_BITS_REMAINING(bs);
        if (!drflac__reload_cache(bs)) {
            return 0;
        }
    }
    if (bs->cache == 1) {
        *pOffsetOut = zeroCounter + (uint32_t)DR_FLAC_CACHE_L1_BITS_REMAINING(bs) - 1;
        if (!drflac__reload_cache(bs)) {
            return 0;
        }
        return 1;
    }
    setBitOffsetPlus1 = drflac__clz(bs->cache);
    setBitOffsetPlus1 += 1;
    if (setBitOffsetPlus1 > DR_FLAC_CACHE_L1_BITS_REMAINING(bs)) {
        return 0;
    }
    bs->consumedBits += setBitOffsetPlus1;
    bs->cache <<= setBitOffsetPlus1;
    *pOffsetOut = zeroCounter + setBitOffsetPlus1 - 1;
    return 1;
}
static int drflac__seek_to_byte(drflac_bs* bs, uint64_t offsetFromStart)
{
    DR_FLAC_ASSERT(bs != NULL);
    if (offsetFromStart > 0x7FFFFFFF) {
        uint64_t bytesRemaining = offsetFromStart;
        if (!bs->onSeek(bs->pUserData, 0x7FFFFFFF, DR_FLAC_SEEK_SET)) {
            return 0;
        }
        bytesRemaining -= 0x7FFFFFFF;
        while (bytesRemaining > 0x7FFFFFFF) {
            if (!bs->onSeek(bs->pUserData, 0x7FFFFFFF, DR_FLAC_SEEK_CUR)) {
                return 0;
            }
            bytesRemaining -= 0x7FFFFFFF;
        }
        if (bytesRemaining > 0) {
            if (!bs->onSeek(bs->pUserData, (int)bytesRemaining, DR_FLAC_SEEK_CUR)) {
                return 0;
            }
        }
    } else {
        if (!bs->onSeek(bs->pUserData, (int)offsetFromStart, DR_FLAC_SEEK_SET)) {
            return 0;
        }
    }
    drflac__reset_cache(bs);
    return 1;
}
static ma_result drflac__read_utf8_coded_number(drflac_bs* bs, uint64_t* pNumberOut, uint8_t* pCRCOut)
{
    uint8_t crc;
    uint64_t result;
    uint8_t utf8[7] = {0};
    int byteCount;
    int i;
    DR_FLAC_ASSERT(bs != NULL);
    DR_FLAC_ASSERT(pNumberOut != NULL);
    DR_FLAC_ASSERT(pCRCOut != NULL);
    crc = *pCRCOut;
    if (!drflac__read_uint8(bs, 8, utf8)) {
        *pNumberOut = 0;
        return MA_AT_END;
    }
    crc = drflac_crc8(crc, utf8[0], 8);
    if ((utf8[0] & 0x80) == 0) {
        *pNumberOut = utf8[0];
        *pCRCOut = crc;
        return MA_SUCCESS;
    }
    if ((utf8[0] & 0xE0) == 0xC0) {
        byteCount = 2;
    } else if ((utf8[0] & 0xF0) == 0xE0) {
        byteCount = 3;
    } else if ((utf8[0] & 0xF8) == 0xF0) {
        byteCount = 4;
    } else if ((utf8[0] & 0xFC) == 0xF8) {
        byteCount = 5;
    } else if ((utf8[0] & 0xFE) == 0xFC) {
        byteCount = 6;
    } else if ((utf8[0] & 0xFF) == 0xFE) {
        byteCount = 7;
    } else {
        *pNumberOut = 0;
        return MA_CRC_MISMATCH;
    }
    DR_FLAC_ASSERT(byteCount > 1);
    result = (uint64_t)(utf8[0] & (0xFF >> (byteCount + 1)));
    for (i = 1; i < byteCount; ++i) {
        if (!drflac__read_uint8(bs, 8, utf8 + i)) {
            *pNumberOut = 0;
            return MA_AT_END;
        }
        crc = drflac_crc8(crc, utf8[i], 8);
        result = (result << 6) | (utf8[i] & 0x3F);
    }
    *pNumberOut = result;
    *pCRCOut = crc;
    return MA_SUCCESS;
}
static DRFLAC_INLINE uint32_t drflac__ilog2_u32(uint32_t x)
{
#if 1
    uint32_t result = 0;
    while (x > 0) {
        result += 1;
        x >>= 1;
    }
    return result;
#endif
}
static DRFLAC_INLINE int drflac__use_64_bit_prediction(uint32_t bitsPerSample, uint32_t order, uint32_t precision)
{
    return bitsPerSample + precision + drflac__ilog2_u32(order) > 32;
}
#if defined(__clang__)
__attribute__((no_sanitize("signed-integer-overflow")))
#endif
static DRFLAC_INLINE int32_t drflac__calculate_prediction_32_s32(uint32_t order, int32_t shift, const int32_t* coefficients, int32_t* pDecodedSamples)
{
    int32_t prediction = 0;
    DR_FLAC_ASSERT(order <= 32);
    switch (order)
    {
    case 32: prediction += coefficients[31] * pDecodedSamples[-32];
    case 31: prediction += coefficients[30] * pDecodedSamples[-31];
    case 30: prediction += coefficients[29] * pDecodedSamples[-30];
    case 29: prediction += coefficients[28] * pDecodedSamples[-29];
    case 28: prediction += coefficients[27] * pDecodedSamples[-28];
    case 27: prediction += coefficients[26] * pDecodedSamples[-27];
    case 26: prediction += coefficients[25] * pDecodedSamples[-26];
    case 25: prediction += coefficients[24] * pDecodedSamples[-25];
    case 24: prediction += coefficients[23] * pDecodedSamples[-24];
    case 23: prediction += coefficients[22] * pDecodedSamples[-23];
    case 22: prediction += coefficients[21] * pDecodedSamples[-22];
    case 21: prediction += coefficients[20] * pDecodedSamples[-21];
    case 20: prediction += coefficients[19] * pDecodedSamples[-20];
    case 19: prediction += coefficients[18] * pDecodedSamples[-19];
    case 18: prediction += coefficients[17] * pDecodedSamples[-18];
    case 17: prediction += coefficients[16] * pDecodedSamples[-17];
    case 16: prediction += coefficients[15] * pDecodedSamples[-16];
    case 15: prediction += coefficients[14] * pDecodedSamples[-15];
    case 14: prediction += coefficients[13] * pDecodedSamples[-14];
    case 13: prediction += coefficients[12] * pDecodedSamples[-13];
    case 12: prediction += coefficients[11] * pDecodedSamples[-12];
    case 11: prediction += coefficients[10] * pDecodedSamples[-11];
    case 10: prediction += coefficients[ 9] * pDecodedSamples[-10];
    case  9: prediction += coefficients[ 8] * pDecodedSamples[- 9];
    case  8: prediction += coefficients[ 7] * pDecodedSamples[- 8];
    case  7: prediction += coefficients[ 6] * pDecodedSamples[- 7];
    case  6: prediction += coefficients[ 5] * pDecodedSamples[- 6];
    case  5: prediction += coefficients[ 4] * pDecodedSamples[- 5];
    case  4: prediction += coefficients[ 3] * pDecodedSamples[- 4];
    case  3: prediction += coefficients[ 2] * pDecodedSamples[- 3];
    case  2: prediction += coefficients[ 1] * pDecodedSamples[- 2];
    case  1: prediction += coefficients[ 0] * pDecodedSamples[- 1];
    }
    return (int32_t)(prediction >> shift);
}
static DRFLAC_INLINE int32_t drflac__calculate_prediction_64_s32(uint32_t order, int32_t shift, const int32_t* coefficients, int32_t* pDecodedSamples)
{
    int64_t prediction;
    DR_FLAC_ASSERT(order <= 32);
#if 1
    if (order == 8)
    {
        prediction  = coefficients[0] * (int64_t)pDecodedSamples[-1];
        prediction += coefficients[1] * (int64_t)pDecodedSamples[-2];
        prediction += coefficients[2] * (int64_t)pDecodedSamples[-3];
        prediction += coefficients[3] * (int64_t)pDecodedSamples[-4];
        prediction += coefficients[4] * (int64_t)pDecodedSamples[-5];
        prediction += coefficients[5] * (int64_t)pDecodedSamples[-6];
        prediction += coefficients[6] * (int64_t)pDecodedSamples[-7];
        prediction += coefficients[7] * (int64_t)pDecodedSamples[-8];
    }
    else if (order == 7)
    {
        prediction  = coefficients[0] * (int64_t)pDecodedSamples[-1];
        prediction += coefficients[1] * (int64_t)pDecodedSamples[-2];
        prediction += coefficients[2] * (int64_t)pDecodedSamples[-3];
        prediction += coefficients[3] * (int64_t)pDecodedSamples[-4];
        prediction += coefficients[4] * (int64_t)pDecodedSamples[-5];
        prediction += coefficients[5] * (int64_t)pDecodedSamples[-6];
        prediction += coefficients[6] * (int64_t)pDecodedSamples[-7];
    }
    else if (order == 3)
    {
        prediction  = coefficients[0] * (int64_t)pDecodedSamples[-1];
        prediction += coefficients[1] * (int64_t)pDecodedSamples[-2];
        prediction += coefficients[2] * (int64_t)pDecodedSamples[-3];
    }
    else if (order == 6)
    {
        prediction  = coefficients[0] * (int64_t)pDecodedSamples[-1];
        prediction += coefficients[1] * (int64_t)pDecodedSamples[-2];
        prediction += coefficients[2] * (int64_t)pDecodedSamples[-3];
        prediction += coefficients[3] * (int64_t)pDecodedSamples[-4];
        prediction += coefficients[4] * (int64_t)pDecodedSamples[-5];
        prediction += coefficients[5] * (int64_t)pDecodedSamples[-6];
    }
    else if (order == 5)
    {
        prediction  = coefficients[0] * (int64_t)pDecodedSamples[-1];
        prediction += coefficients[1] * (int64_t)pDecodedSamples[-2];
        prediction += coefficients[2] * (int64_t)pDecodedSamples[-3];
        prediction += coefficients[3] * (int64_t)pDecodedSamples[-4];
        prediction += coefficients[4] * (int64_t)pDecodedSamples[-5];
    }
    else if (order == 4)
    {
        prediction  = coefficients[0] * (int64_t)pDecodedSamples[-1];
        prediction += coefficients[1] * (int64_t)pDecodedSamples[-2];
        prediction += coefficients[2] * (int64_t)pDecodedSamples[-3];
        prediction += coefficients[3] * (int64_t)pDecodedSamples[-4];
    }
    else if (order == 12)
    {
        prediction  = coefficients[0]  * (int64_t)pDecodedSamples[-1];
        prediction += coefficients[1]  * (int64_t)pDecodedSamples[-2];
        prediction += coefficients[2]  * (int64_t)pDecodedSamples[-3];
        prediction += coefficients[3]  * (int64_t)pDecodedSamples[-4];
        prediction += coefficients[4]  * (int64_t)pDecodedSamples[-5];
        prediction += coefficients[5]  * (int64_t)pDecodedSamples[-6];
        prediction += coefficients[6]  * (int64_t)pDecodedSamples[-7];
        prediction += coefficients[7]  * (int64_t)pDecodedSamples[-8];
        prediction += coefficients[8]  * (int64_t)pDecodedSamples[-9];
        prediction += coefficients[9]  * (int64_t)pDecodedSamples[-10];
        prediction += coefficients[10] * (int64_t)pDecodedSamples[-11];
        prediction += coefficients[11] * (int64_t)pDecodedSamples[-12];
    }
    else if (order == 2)
    {
        prediction  = coefficients[0] * (int64_t)pDecodedSamples[-1];
        prediction += coefficients[1] * (int64_t)pDecodedSamples[-2];
    }
    else if (order == 1)
    {
        prediction = coefficients[0] * (int64_t)pDecodedSamples[-1];
    }
    else if (order == 10)
    {
        prediction  = coefficients[0]  * (int64_t)pDecodedSamples[-1];
        prediction += coefficients[1]  * (int64_t)pDecodedSamples[-2];
        prediction += coefficients[2]  * (int64_t)pDecodedSamples[-3];
        prediction += coefficients[3]  * (int64_t)pDecodedSamples[-4];
        prediction += coefficients[4]  * (int64_t)pDecodedSamples[-5];
        prediction += coefficients[5]  * (int64_t)pDecodedSamples[-6];
        prediction += coefficients[6]  * (int64_t)pDecodedSamples[-7];
        prediction += coefficients[7]  * (int64_t)pDecodedSamples[-8];
        prediction += coefficients[8]  * (int64_t)pDecodedSamples[-9];
        prediction += coefficients[9]  * (int64_t)pDecodedSamples[-10];
    }
    else if (order == 9)
    {
        prediction  = coefficients[0]  * (int64_t)pDecodedSamples[-1];
        prediction += coefficients[1]  * (int64_t)pDecodedSamples[-2];
        prediction += coefficients[2]  * (int64_t)pDecodedSamples[-3];
        prediction += coefficients[3]  * (int64_t)pDecodedSamples[-4];
        prediction += coefficients[4]  * (int64_t)pDecodedSamples[-5];
        prediction += coefficients[5]  * (int64_t)pDecodedSamples[-6];
        prediction += coefficients[6]  * (int64_t)pDecodedSamples[-7];
        prediction += coefficients[7]  * (int64_t)pDecodedSamples[-8];
        prediction += coefficients[8]  * (int64_t)pDecodedSamples[-9];
    }
    else if (order == 11)
    {
        prediction  = coefficients[0]  * (int64_t)pDecodedSamples[-1];
        prediction += coefficients[1]  * (int64_t)pDecodedSamples[-2];
        prediction += coefficients[2]  * (int64_t)pDecodedSamples[-3];
        prediction += coefficients[3]  * (int64_t)pDecodedSamples[-4];
        prediction += coefficients[4]  * (int64_t)pDecodedSamples[-5];
        prediction += coefficients[5]  * (int64_t)pDecodedSamples[-6];
        prediction += coefficients[6]  * (int64_t)pDecodedSamples[-7];
        prediction += coefficients[7]  * (int64_t)pDecodedSamples[-8];
        prediction += coefficients[8]  * (int64_t)pDecodedSamples[-9];
        prediction += coefficients[9]  * (int64_t)pDecodedSamples[-10];
        prediction += coefficients[10] * (int64_t)pDecodedSamples[-11];
    }
    else
    {
        int j;
        prediction = 0;
        for (j = 0; j < (int)order; ++j) {
            prediction += coefficients[j] * (int64_t)pDecodedSamples[-j-1];
        }
    }
#endif
#if 1
    prediction = 0;
    switch (order)
    {
    case 32: prediction += coefficients[31] * (int64_t)pDecodedSamples[-32];
    case 31: prediction += coefficients[30] * (int64_t)pDecodedSamples[-31];
    case 30: prediction += coefficients[29] * (int64_t)pDecodedSamples[-30];
    case 29: prediction += coefficients[28] * (int64_t)pDecodedSamples[-29];
    case 28: prediction += coefficients[27] * (int64_t)pDecodedSamples[-28];
    case 27: prediction += coefficients[26] * (int64_t)pDecodedSamples[-27];
    case 26: prediction += coefficients[25] * (int64_t)pDecodedSamples[-26];
    case 25: prediction += coefficients[24] * (int64_t)pDecodedSamples[-25];
    case 24: prediction += coefficients[23] * (int64_t)pDecodedSamples[-24];
    case 23: prediction += coefficients[22] * (int64_t)pDecodedSamples[-23];
    case 22: prediction += coefficients[21] * (int64_t)pDecodedSamples[-22];
    case 21: prediction += coefficients[20] * (int64_t)pDecodedSamples[-21];
    case 20: prediction += coefficients[19] * (int64_t)pDecodedSamples[-20];
    case 19: prediction += coefficients[18] * (int64_t)pDecodedSamples[-19];
    case 18: prediction += coefficients[17] * (int64_t)pDecodedSamples[-18];
    case 17: prediction += coefficients[16] * (int64_t)pDecodedSamples[-17];
    case 16: prediction += coefficients[15] * (int64_t)pDecodedSamples[-16];
    case 15: prediction += coefficients[14] * (int64_t)pDecodedSamples[-15];
    case 14: prediction += coefficients[13] * (int64_t)pDecodedSamples[-14];
    case 13: prediction += coefficients[12] * (int64_t)pDecodedSamples[-13];
    case 12: prediction += coefficients[11] * (int64_t)pDecodedSamples[-12];
    case 11: prediction += coefficients[10] * (int64_t)pDecodedSamples[-11];
    case 10: prediction += coefficients[ 9] * (int64_t)pDecodedSamples[-10];
    case  9: prediction += coefficients[ 8] * (int64_t)pDecodedSamples[- 9];
    case  8: prediction += coefficients[ 7] * (int64_t)pDecodedSamples[- 8];
    case  7: prediction += coefficients[ 6] * (int64_t)pDecodedSamples[- 7];
    case  6: prediction += coefficients[ 5] * (int64_t)pDecodedSamples[- 6];
    case  5: prediction += coefficients[ 4] * (int64_t)pDecodedSamples[- 5];
    case  4: prediction += coefficients[ 3] * (int64_t)pDecodedSamples[- 4];
    case  3: prediction += coefficients[ 2] * (int64_t)pDecodedSamples[- 3];
    case  2: prediction += coefficients[ 1] * (int64_t)pDecodedSamples[- 2];
    case  1: prediction += coefficients[ 0] * (int64_t)pDecodedSamples[- 1];
    }
#endif
    return (int32_t)(prediction >> shift);
}
#if 0
static int drflac__decode_samples_with_residual__rice__reference(drflac_bs* bs, uint32_t bitsPerSample, uint32_t count, uint8_t riceParam, uint32_t lpcOrder, int32_t lpcShift, uint32_t lpcPrecision, const int32_t* coefficients, int32_t* pSamplesOut)
{
    uint32_t i;
    DR_FLAC_ASSERT(bs != NULL);
    DR_FLAC_ASSERT(pSamplesOut != NULL);
    for (i = 0; i < count; ++i) {
        uint32_t zeroCounter = 0;
        for (;;) {
            uint8_t bit;
            if (!drflac__read_uint8(bs, 1, &bit)) {
                return 0;
            }
            if (bit == 0) {
                zeroCounter += 1;
            } else {
                break;
            }
        }
        uint32_t decodedRice;
        if (riceParam > 0) {
            if (!drflac__read_uint32(bs, riceParam, &decodedRice)) {
                return 0;
            }
        } else {
            decodedRice = 0;
        }
        decodedRice |= (zeroCounter << riceParam);
        if ((decodedRice & 0x01)) {
            decodedRice = ~(decodedRice >> 1);
        } else {
            decodedRice =  (decodedRice >> 1);
        }
        if (drflac__use_64_bit_prediction(bitsPerSample, lpcOrder, lpcPrecision)) {
            pSamplesOut[i] = decodedRice + drflac__calculate_prediction_64_s32(lpcOrder, lpcShift, coefficients, pSamplesOut + i);
        } else {
            pSamplesOut[i] = decodedRice + drflac__calculate_prediction_32_s32(lpcOrder, lpcShift, coefficients, pSamplesOut + i);
        }
    }
    return 1;
}
#endif
#if 0
static int drflac__read_rice_parts__reference(drflac_bs* bs, uint8_t riceParam, uint32_t* pZeroCounterOut, uint32_t* pRiceParamPartOut)
{
    uint32_t zeroCounter = 0;
    uint32_t decodedRice;
    for (;;) {
        uint8_t bit;
        if (!drflac__read_uint8(bs, 1, &bit)) {
            return 0;
        }
        if (bit == 0) {
            zeroCounter += 1;
        } else {
            break;
        }
    }
    if (riceParam > 0) {
        if (!drflac__read_uint32(bs, riceParam, &decodedRice)) {
            return 0;
        }
    } else {
        decodedRice = 0;
    }
    *pZeroCounterOut = zeroCounter;
    *pRiceParamPartOut = decodedRice;
    return 1;
}
#endif
#if 0
static DRFLAC_INLINE int drflac__read_rice_parts(drflac_bs* bs, uint8_t riceParam, uint32_t* pZeroCounterOut, uint32_t* pRiceParamPartOut)
{
    drflac_cache_t riceParamMask;
    uint32_t zeroCounter;
    uint32_t setBitOffsetPlus1;
    uint32_t riceParamPart;
    uint32_t riceLength;
    DR_FLAC_ASSERT(riceParam > 0);
    riceParamMask = DR_FLAC_CACHE_L1_SELECTION_MASK(riceParam);
    zeroCounter = 0;
    while (bs->cache == 0) {
        zeroCounter += (uint32_t)DR_FLAC_CACHE_L1_BITS_REMAINING(bs);
        if (!drflac__reload_cache(bs)) {
            return 0;
        }
    }
    setBitOffsetPlus1 = drflac__clz(bs->cache);
    zeroCounter += setBitOffsetPlus1;
    setBitOffsetPlus1 += 1;
    riceLength = setBitOffsetPlus1 + riceParam;
    if (riceLength < DR_FLAC_CACHE_L1_BITS_REMAINING(bs)) {
        riceParamPart = (uint32_t)((bs->cache & (riceParamMask >> setBitOffsetPlus1)) >> DR_FLAC_CACHE_L1_SELECTION_SHIFT(bs, riceLength));
        bs->consumedBits += riceLength;
        bs->cache <<= riceLength;
    } else {
        uint32_t bitCountLo;
        drflac_cache_t resultHi;
        bs->consumedBits += riceLength;
        bs->cache <<= setBitOffsetPlus1 & (DR_FLAC_CACHE_L1_SIZE_BITS(bs)-1);
        bitCountLo = bs->consumedBits - DR_FLAC_CACHE_L1_SIZE_BITS(bs);
        resultHi = DR_FLAC_CACHE_L1_SELECT_AND_SHIFT(bs, riceParam);
        if (bs->nextL2Line < DR_FLAC_CACHE_L2_LINE_COUNT(bs)) {
#ifndef DR_FLAC_NO_CRC
            drflac__update_crc16(bs);
#endif
            bs->cache = drflac__be2host__cache_line(bs->cacheL2[bs->nextL2Line++]);
            bs->consumedBits = 0;
#ifndef DR_FLAC_NO_CRC
            bs->crc16Cache = bs->cache;
#endif
        } else {
            if (!drflac__reload_cache(bs)) {
                return 0;
            }
            if (bitCountLo > DR_FLAC_CACHE_L1_BITS_REMAINING(bs)) {
                return 0;
            }
        }
        riceParamPart = (uint32_t)(resultHi | DR_FLAC_CACHE_L1_SELECT_AND_SHIFT_SAFE(bs, bitCountLo));
        bs->consumedBits += bitCountLo;
        bs->cache <<= bitCountLo;
    }
    pZeroCounterOut[0] = zeroCounter;
    pRiceParamPartOut[0] = riceParamPart;
    return 1;
}
#endif
static DRFLAC_INLINE int drflac__read_rice_parts_x1(drflac_bs* bs, uint8_t riceParam, uint32_t* pZeroCounterOut, uint32_t* pRiceParamPartOut)
{
    uint32_t  riceParamPlus1 = riceParam + 1;
    uint32_t  riceParamPlus1Shift = DR_FLAC_CACHE_L1_SELECTION_SHIFT(bs, riceParamPlus1);
    uint32_t  riceParamPlus1MaxConsumedBits = DR_FLAC_CACHE_L1_SIZE_BITS(bs) - riceParamPlus1;
    drflac_cache_t bs_cache = bs->cache;
    uint32_t  bs_consumedBits = bs->consumedBits;
    uint32_t  lzcount = drflac__clz(bs_cache);
    if (lzcount < sizeof(bs_cache)*8) {
        pZeroCounterOut[0] = lzcount;
    extract_rice_param_part:
        bs_cache       <<= lzcount;
        bs_consumedBits += lzcount;
        if (bs_consumedBits <= riceParamPlus1MaxConsumedBits) {
            pRiceParamPartOut[0] = (uint32_t)(bs_cache >> riceParamPlus1Shift);
            bs_cache       <<= riceParamPlus1;
            bs_consumedBits += riceParamPlus1;
        } else {
            uint32_t riceParamPartHi;
            uint32_t riceParamPartLo;
            uint32_t riceParamPartLoBitCount;
            riceParamPartHi = (uint32_t)(bs_cache >> riceParamPlus1Shift);
            riceParamPartLoBitCount = bs_consumedBits - riceParamPlus1MaxConsumedBits;
            DR_FLAC_ASSERT(riceParamPartLoBitCount > 0 && riceParamPartLoBitCount < 32);
            if (bs->nextL2Line < DR_FLAC_CACHE_L2_LINE_COUNT(bs)) {
            #ifndef DR_FLAC_NO_CRC
                drflac__update_crc16(bs);
            #endif
                bs_cache = drflac__be2host__cache_line(bs->cacheL2[bs->nextL2Line++]);
                bs_consumedBits = riceParamPartLoBitCount;
            #ifndef DR_FLAC_NO_CRC
                bs->crc16Cache = bs_cache;
            #endif
            } else {
                if (!drflac__reload_cache(bs)) {
                    return 0;
                }
                if (riceParamPartLoBitCount > DR_FLAC_CACHE_L1_BITS_REMAINING(bs)) {
                    return 0;
                }
                bs_cache = bs->cache;
                bs_consumedBits = bs->consumedBits + riceParamPartLoBitCount;
            }
            riceParamPartLo = (uint32_t)(bs_cache >> (DR_FLAC_CACHE_L1_SELECTION_SHIFT(bs, riceParamPartLoBitCount)));
            pRiceParamPartOut[0] = riceParamPartHi | riceParamPartLo;
            bs_cache <<= riceParamPartLoBitCount;
        }
    } else {
        uint32_t zeroCounter = (uint32_t)(DR_FLAC_CACHE_L1_SIZE_BITS(bs) - bs_consumedBits);
        for (;;) {
            if (bs->nextL2Line < DR_FLAC_CACHE_L2_LINE_COUNT(bs)) {
            #ifndef DR_FLAC_NO_CRC
                drflac__update_crc16(bs);
            #endif
                bs_cache = drflac__be2host__cache_line(bs->cacheL2[bs->nextL2Line++]);
                bs_consumedBits = 0;
            #ifndef DR_FLAC_NO_CRC
                bs->crc16Cache = bs_cache;
            #endif
            } else {
                if (!drflac__reload_cache(bs)) {
                    return 0;
                }
                bs_cache = bs->cache;
                bs_consumedBits = bs->consumedBits;
            }
            lzcount = drflac__clz(bs_cache);
            zeroCounter += lzcount;
            if (lzcount < sizeof(bs_cache)*8) {
                break;
            }
        }
        pZeroCounterOut[0] = zeroCounter;
        goto extract_rice_param_part;
    }
    bs->cache = bs_cache;
    bs->consumedBits = bs_consumedBits;
    return 1;
}
static DRFLAC_INLINE int drflac__seek_rice_parts(drflac_bs* bs, uint8_t riceParam)
{
    uint32_t  riceParamPlus1 = riceParam + 1;
    uint32_t  riceParamPlus1MaxConsumedBits = DR_FLAC_CACHE_L1_SIZE_BITS(bs) - riceParamPlus1;
    drflac_cache_t bs_cache = bs->cache;
    uint32_t  bs_consumedBits = bs->consumedBits;
    uint32_t  lzcount = drflac__clz(bs_cache);
    if (lzcount < sizeof(bs_cache)*8) {
    extract_rice_param_part:
        bs_cache       <<= lzcount;
        bs_consumedBits += lzcount;
        if (bs_consumedBits <= riceParamPlus1MaxConsumedBits) {
            bs_cache       <<= riceParamPlus1;
            bs_consumedBits += riceParamPlus1;
        } else {
            uint32_t riceParamPartLoBitCount = bs_consumedBits - riceParamPlus1MaxConsumedBits;
            DR_FLAC_ASSERT(riceParamPartLoBitCount > 0 && riceParamPartLoBitCount < 32);
            if (bs->nextL2Line < DR_FLAC_CACHE_L2_LINE_COUNT(bs)) {
            #ifndef DR_FLAC_NO_CRC
                drflac__update_crc16(bs);
            #endif
                bs_cache = drflac__be2host__cache_line(bs->cacheL2[bs->nextL2Line++]);
                bs_consumedBits = riceParamPartLoBitCount;
            #ifndef DR_FLAC_NO_CRC
                bs->crc16Cache = bs_cache;
            #endif
            } else {
                if (!drflac__reload_cache(bs)) {
                    return 0;
                }
                if (riceParamPartLoBitCount > DR_FLAC_CACHE_L1_BITS_REMAINING(bs)) {
                    return 0;
                }
                bs_cache = bs->cache;
                bs_consumedBits = bs->consumedBits + riceParamPartLoBitCount;
            }
            bs_cache <<= riceParamPartLoBitCount;
        }
    } else {
        for (;;) {
            if (bs->nextL2Line < DR_FLAC_CACHE_L2_LINE_COUNT(bs)) {
            #ifndef DR_FLAC_NO_CRC
                drflac__update_crc16(bs);
            #endif
                bs_cache = drflac__be2host__cache_line(bs->cacheL2[bs->nextL2Line++]);
                bs_consumedBits = 0;
            #ifndef DR_FLAC_NO_CRC
                bs->crc16Cache = bs_cache;
            #endif
            } else {
                if (!drflac__reload_cache(bs)) {
                    return 0;
                }
                bs_cache = bs->cache;
                bs_consumedBits = bs->consumedBits;
            }
            lzcount = drflac__clz(bs_cache);
            if (lzcount < sizeof(bs_cache)*8) {
                break;
            }
        }
        goto extract_rice_param_part;
    }
    bs->cache = bs_cache;
    bs->consumedBits = bs_consumedBits;
    return 1;
}
static int drflac__decode_samples_with_residual__rice__scalar_zeroorder(drflac_bs* bs, uint32_t bitsPerSample, uint32_t count, uint8_t riceParam, uint32_t order, int32_t shift, const int32_t* coefficients, int32_t* pSamplesOut)
{
    uint32_t t[2] = {0x00000000, 0xFFFFFFFF};
    uint32_t zeroCountPart0;
    uint32_t riceParamPart0;
    uint32_t riceParamMask;
    uint32_t i;
    DR_FLAC_ASSERT(bs != NULL);
    DR_FLAC_ASSERT(pSamplesOut != NULL);
    (void)bitsPerSample;
    (void)order;
    (void)shift;
    (void)coefficients;
    riceParamMask  = (uint32_t)~((~0UL) << riceParam);
    i = 0;
    while (i < count) {
        if (!drflac__read_rice_parts_x1(bs, riceParam, &zeroCountPart0, &riceParamPart0)) {
            return 0;
        }
        riceParamPart0 &= riceParamMask;
        riceParamPart0 |= (zeroCountPart0 << riceParam);
        riceParamPart0  = (riceParamPart0 >> 1) ^ t[riceParamPart0 & 0x01];
        pSamplesOut[i] = riceParamPart0;
        i += 1;
    }
    return 1;
}
static int drflac__decode_samples_with_residual__rice__scalar(drflac_bs* bs, uint32_t bitsPerSample, uint32_t count, uint8_t riceParam, uint32_t lpcOrder, int32_t lpcShift, uint32_t lpcPrecision, const int32_t* coefficients, int32_t* pSamplesOut)
{
    uint32_t t[2] = {0x00000000, 0xFFFFFFFF};
    uint32_t zeroCountPart0 = 0;
    uint32_t zeroCountPart1 = 0;
    uint32_t zeroCountPart2 = 0;
    uint32_t zeroCountPart3 = 0;
    uint32_t riceParamPart0 = 0;
    uint32_t riceParamPart1 = 0;
    uint32_t riceParamPart2 = 0;
    uint32_t riceParamPart3 = 0;
    uint32_t riceParamMask;
    const int32_t* pSamplesOutEnd;
    uint32_t i;
    DR_FLAC_ASSERT(bs != NULL);
    DR_FLAC_ASSERT(pSamplesOut != NULL);
    if (lpcOrder == 0) {
        return drflac__decode_samples_with_residual__rice__scalar_zeroorder(bs, bitsPerSample, count, riceParam, lpcOrder, lpcShift, coefficients, pSamplesOut);
    }
    riceParamMask  = (uint32_t)~((~0UL) << riceParam);
    pSamplesOutEnd = pSamplesOut + (count & ~3);
    if (drflac__use_64_bit_prediction(bitsPerSample, lpcOrder, lpcPrecision)) {
        while (pSamplesOut < pSamplesOutEnd) {
            if (!drflac__read_rice_parts_x1(bs, riceParam, &zeroCountPart0, &riceParamPart0) ||
                !drflac__read_rice_parts_x1(bs, riceParam, &zeroCountPart1, &riceParamPart1) ||
                !drflac__read_rice_parts_x1(bs, riceParam, &zeroCountPart2, &riceParamPart2) ||
                !drflac__read_rice_parts_x1(bs, riceParam, &zeroCountPart3, &riceParamPart3)) {
                return 0;
            }
            riceParamPart0 &= riceParamMask;
            riceParamPart1 &= riceParamMask;
            riceParamPart2 &= riceParamMask;
            riceParamPart3 &= riceParamMask;
            riceParamPart0 |= (zeroCountPart0 << riceParam);
            riceParamPart1 |= (zeroCountPart1 << riceParam);
            riceParamPart2 |= (zeroCountPart2 << riceParam);
            riceParamPart3 |= (zeroCountPart3 << riceParam);
            riceParamPart0  = (riceParamPart0 >> 1) ^ t[riceParamPart0 & 0x01];
            riceParamPart1  = (riceParamPart1 >> 1) ^ t[riceParamPart1 & 0x01];
            riceParamPart2  = (riceParamPart2 >> 1) ^ t[riceParamPart2 & 0x01];
            riceParamPart3  = (riceParamPart3 >> 1) ^ t[riceParamPart3 & 0x01];
            pSamplesOut[0] = riceParamPart0 + drflac__calculate_prediction_64_s32(lpcOrder, lpcShift, coefficients, pSamplesOut + 0);
            pSamplesOut[1] = riceParamPart1 + drflac__calculate_prediction_64_s32(lpcOrder, lpcShift, coefficients, pSamplesOut + 1);
            pSamplesOut[2] = riceParamPart2 + drflac__calculate_prediction_64_s32(lpcOrder, lpcShift, coefficients, pSamplesOut + 2);
            pSamplesOut[3] = riceParamPart3 + drflac__calculate_prediction_64_s32(lpcOrder, lpcShift, coefficients, pSamplesOut + 3);
            pSamplesOut += 4;
        }
    } else {
        while (pSamplesOut < pSamplesOutEnd) {
            if (!drflac__read_rice_parts_x1(bs, riceParam, &zeroCountPart0, &riceParamPart0) ||
                !drflac__read_rice_parts_x1(bs, riceParam, &zeroCountPart1, &riceParamPart1) ||
                !drflac__read_rice_parts_x1(bs, riceParam, &zeroCountPart2, &riceParamPart2) ||
                !drflac__read_rice_parts_x1(bs, riceParam, &zeroCountPart3, &riceParamPart3)) {
                return 0;
            }
            riceParamPart0 &= riceParamMask;
            riceParamPart1 &= riceParamMask;
            riceParamPart2 &= riceParamMask;
            riceParamPart3 &= riceParamMask;
            riceParamPart0 |= (zeroCountPart0 << riceParam);
            riceParamPart1 |= (zeroCountPart1 << riceParam);
            riceParamPart2 |= (zeroCountPart2 << riceParam);
            riceParamPart3 |= (zeroCountPart3 << riceParam);
            riceParamPart0  = (riceParamPart0 >> 1) ^ t[riceParamPart0 & 0x01];
            riceParamPart1  = (riceParamPart1 >> 1) ^ t[riceParamPart1 & 0x01];
            riceParamPart2  = (riceParamPart2 >> 1) ^ t[riceParamPart2 & 0x01];
            riceParamPart3  = (riceParamPart3 >> 1) ^ t[riceParamPart3 & 0x01];
            pSamplesOut[0] = riceParamPart0 + drflac__calculate_prediction_32_s32(lpcOrder, lpcShift, coefficients, pSamplesOut + 0);
            pSamplesOut[1] = riceParamPart1 + drflac__calculate_prediction_32_s32(lpcOrder, lpcShift, coefficients, pSamplesOut + 1);
            pSamplesOut[2] = riceParamPart2 + drflac__calculate_prediction_32_s32(lpcOrder, lpcShift, coefficients, pSamplesOut + 2);
            pSamplesOut[3] = riceParamPart3 + drflac__calculate_prediction_32_s32(lpcOrder, lpcShift, coefficients, pSamplesOut + 3);
            pSamplesOut += 4;
        }
    }
    i = (count & ~3);
    while (i < count) {
        if (!drflac__read_rice_parts_x1(bs, riceParam, &zeroCountPart0, &riceParamPart0)) {
            return 0;
        }
        riceParamPart0 &= riceParamMask;
        riceParamPart0 |= (zeroCountPart0 << riceParam);
        riceParamPart0  = (riceParamPart0 >> 1) ^ t[riceParamPart0 & 0x01];
        if (drflac__use_64_bit_prediction(bitsPerSample, lpcOrder, lpcPrecision)) {
            pSamplesOut[0] = riceParamPart0 + drflac__calculate_prediction_64_s32(lpcOrder, lpcShift, coefficients, pSamplesOut + 0);
        } else {
            pSamplesOut[0] = riceParamPart0 + drflac__calculate_prediction_32_s32(lpcOrder, lpcShift, coefficients, pSamplesOut + 0);
        }
        i += 1;
        pSamplesOut += 1;
    }
    return 1;
}
#if defined(DR_FLAC_SUPPORT_SSE2)
static DRFLAC_INLINE __m128i drflac__mm_packs_interleaved_epi32(__m128i a, __m128i b)
{
    __m128i r;
    r = _mm_packs_epi32(a, b);
    r = _mm_shuffle_epi32(r, _MM_SHUFFLE(3, 1, 2, 0));
    r = _mm_shufflehi_epi16(r, _MM_SHUFFLE(3, 1, 2, 0));
    r = _mm_shufflelo_epi16(r, _MM_SHUFFLE(3, 1, 2, 0));
    return r;
}
#endif
#if defined(DR_FLAC_SUPPORT_SSE41)
static DRFLAC_INLINE __m128i drflac__mm_not_si128(__m128i a)
{
    return _mm_xor_si128(a, _mm_cmpeq_epi32(_mm_setzero_si128(), _mm_setzero_si128()));
}
static DRFLAC_INLINE __m128i drflac__mm_hadd_epi32(__m128i x)
{
    __m128i x64 = _mm_add_epi32(x, _mm_shuffle_epi32(x, _MM_SHUFFLE(1, 0, 3, 2)));
    __m128i x32 = _mm_shufflelo_epi16(x64, _MM_SHUFFLE(1, 0, 3, 2));
    return _mm_add_epi32(x64, x32);
}
static DRFLAC_INLINE __m128i drflac__mm_hadd_epi64(__m128i x)
{
    return _mm_add_epi64(x, _mm_shuffle_epi32(x, _MM_SHUFFLE(1, 0, 3, 2)));
}
static DRFLAC_INLINE __m128i drflac__mm_srai_epi64(__m128i x, int count)
{
    __m128i lo = _mm_srli_epi64(x, count);
    __m128i hi = _mm_srai_epi32(x, count);
    hi = _mm_and_si128(hi, _mm_set_epi32(0xFFFFFFFF, 0, 0xFFFFFFFF, 0));
    return _mm_or_si128(lo, hi);
}
static int drflac__decode_samples_with_residual__rice__sse41_32(drflac_bs* bs, uint32_t count, uint8_t riceParam, uint32_t order, int32_t shift, const int32_t* coefficients, int32_t* pSamplesOut)
{
    int i;
    uint32_t riceParamMask;
    int32_t* pDecodedSamples    = pSamplesOut;
    int32_t* pDecodedSamplesEnd = pSamplesOut + (count & ~3);
    uint32_t zeroCountParts0 = 0;
    uint32_t zeroCountParts1 = 0;
    uint32_t zeroCountParts2 = 0;
    uint32_t zeroCountParts3 = 0;
    uint32_t riceParamParts0 = 0;
    uint32_t riceParamParts1 = 0;
    uint32_t riceParamParts2 = 0;
    uint32_t riceParamParts3 = 0;
    __m128i coefficients128_0;
    __m128i coefficients128_4;
    __m128i coefficients128_8;
    __m128i samples128_0;
    __m128i samples128_4;
    __m128i samples128_8;
    __m128i riceParamMask128;
    const uint32_t t[2] = {0x00000000, 0xFFFFFFFF};
    riceParamMask    = (uint32_t)~((~0UL) << riceParam);
    riceParamMask128 = _mm_set1_epi32(riceParamMask);
    coefficients128_0 = _mm_setzero_si128();
    coefficients128_4 = _mm_setzero_si128();
    coefficients128_8 = _mm_setzero_si128();
    samples128_0 = _mm_setzero_si128();
    samples128_4 = _mm_setzero_si128();
    samples128_8 = _mm_setzero_si128();
#if 1
    {
        int runningOrder = order;
        if (runningOrder >= 4) {
            coefficients128_0 = _mm_loadu_si128((const __m128i*)(coefficients + 0));
            samples128_0      = _mm_loadu_si128((const __m128i*)(pSamplesOut  - 4));
            runningOrder -= 4;
        } else {
            switch (runningOrder) {
                case 3: coefficients128_0 = _mm_set_epi32(0, coefficients[2], coefficients[1], coefficients[0]); samples128_0 = _mm_set_epi32(pSamplesOut[-1], pSamplesOut[-2], pSamplesOut[-3], 0); break;
                case 2: coefficients128_0 = _mm_set_epi32(0, 0,               coefficients[1], coefficients[0]); samples128_0 = _mm_set_epi32(pSamplesOut[-1], pSamplesOut[-2], 0,               0); break;
                case 1: coefficients128_0 = _mm_set_epi32(0, 0,               0,               coefficients[0]); samples128_0 = _mm_set_epi32(pSamplesOut[-1], 0,               0,               0); break;
            }
            runningOrder = 0;
        }
        if (runningOrder >= 4) {
            coefficients128_4 = _mm_loadu_si128((const __m128i*)(coefficients + 4));
            samples128_4      = _mm_loadu_si128((const __m128i*)(pSamplesOut  - 8));
            runningOrder -= 4;
        } else {
            switch (runningOrder) {
                case 3: coefficients128_4 = _mm_set_epi32(0, coefficients[6], coefficients[5], coefficients[4]); samples128_4 = _mm_set_epi32(pSamplesOut[-5], pSamplesOut[-6], pSamplesOut[-7], 0); break;
                case 2: coefficients128_4 = _mm_set_epi32(0, 0,               coefficients[5], coefficients[4]); samples128_4 = _mm_set_epi32(pSamplesOut[-5], pSamplesOut[-6], 0,               0); break;
                case 1: coefficients128_4 = _mm_set_epi32(0, 0,               0,               coefficients[4]); samples128_4 = _mm_set_epi32(pSamplesOut[-5], 0,               0,               0); break;
            }
            runningOrder = 0;
        }
        if (runningOrder == 4) {
            coefficients128_8 = _mm_loadu_si128((const __m128i*)(coefficients + 8));
            samples128_8      = _mm_loadu_si128((const __m128i*)(pSamplesOut  - 12));
            runningOrder -= 4;
        } else {
            switch (runningOrder) {
                case 3: coefficients128_8 = _mm_set_epi32(0, coefficients[10], coefficients[9], coefficients[8]); samples128_8 = _mm_set_epi32(pSamplesOut[-9], pSamplesOut[-10], pSamplesOut[-11], 0); break;
                case 2: coefficients128_8 = _mm_set_epi32(0, 0,                coefficients[9], coefficients[8]); samples128_8 = _mm_set_epi32(pSamplesOut[-9], pSamplesOut[-10], 0,                0); break;
                case 1: coefficients128_8 = _mm_set_epi32(0, 0,                0,               coefficients[8]); samples128_8 = _mm_set_epi32(pSamplesOut[-9], 0,                0,                0); break;
            }
            runningOrder = 0;
        }
        coefficients128_0 = _mm_shuffle_epi32(coefficients128_0, _MM_SHUFFLE(0, 1, 2, 3));
        coefficients128_4 = _mm_shuffle_epi32(coefficients128_4, _MM_SHUFFLE(0, 1, 2, 3));
        coefficients128_8 = _mm_shuffle_epi32(coefficients128_8, _MM_SHUFFLE(0, 1, 2, 3));
    }
#else
    switch (order)
    {
    case 12: ((int32_t*)&coefficients128_8)[0] = coefficients[11]; ((int32_t*)&samples128_8)[0] = pDecodedSamples[-12];
    case 11: ((int32_t*)&coefficients128_8)[1] = coefficients[10]; ((int32_t*)&samples128_8)[1] = pDecodedSamples[-11];
    case 10: ((int32_t*)&coefficients128_8)[2] = coefficients[ 9]; ((int32_t*)&samples128_8)[2] = pDecodedSamples[-10];
    case 9:  ((int32_t*)&coefficients128_8)[3] = coefficients[ 8]; ((int32_t*)&samples128_8)[3] = pDecodedSamples[- 9];
    case 8:  ((int32_t*)&coefficients128_4)[0] = coefficients[ 7]; ((int32_t*)&samples128_4)[0] = pDecodedSamples[- 8];
    case 7:  ((int32_t*)&coefficients128_4)[1] = coefficients[ 6]; ((int32_t*)&samples128_4)[1] = pDecodedSamples[- 7];
    case 6:  ((int32_t*)&coefficients128_4)[2] = coefficients[ 5]; ((int32_t*)&samples128_4)[2] = pDecodedSamples[- 6];
    case 5:  ((int32_t*)&coefficients128_4)[3] = coefficients[ 4]; ((int32_t*)&samples128_4)[3] = pDecodedSamples[- 5];
    case 4:  ((int32_t*)&coefficients128_0)[0] = coefficients[ 3]; ((int32_t*)&samples128_0)[0] = pDecodedSamples[- 4];
    case 3:  ((int32_t*)&coefficients128_0)[1] = coefficients[ 2]; ((int32_t*)&samples128_0)[1] = pDecodedSamples[- 3];
    case 2:  ((int32_t*)&coefficients128_0)[2] = coefficients[ 1]; ((int32_t*)&samples128_0)[2] = pDecodedSamples[- 2];
    case 1:  ((int32_t*)&coefficients128_0)[3] = coefficients[ 0]; ((int32_t*)&samples128_0)[3] = pDecodedSamples[- 1];
    }
#endif
    while (pDecodedSamples < pDecodedSamplesEnd) {
        __m128i prediction128;
        __m128i zeroCountPart128;
        __m128i riceParamPart128;
        if (!drflac__read_rice_parts_x1(bs, riceParam, &zeroCountParts0, &riceParamParts0) ||
            !drflac__read_rice_parts_x1(bs, riceParam, &zeroCountParts1, &riceParamParts1) ||
            !drflac__read_rice_parts_x1(bs, riceParam, &zeroCountParts2, &riceParamParts2) ||
            !drflac__read_rice_parts_x1(bs, riceParam, &zeroCountParts3, &riceParamParts3)) {
            return 0;
        }
        zeroCountPart128 = _mm_set_epi32(zeroCountParts3, zeroCountParts2, zeroCountParts1, zeroCountParts0);
        riceParamPart128 = _mm_set_epi32(riceParamParts3, riceParamParts2, riceParamParts1, riceParamParts0);
        riceParamPart128 = _mm_and_si128(riceParamPart128, riceParamMask128);
        riceParamPart128 = _mm_or_si128(riceParamPart128, _mm_slli_epi32(zeroCountPart128, riceParam));
        riceParamPart128 = _mm_xor_si128(_mm_srli_epi32(riceParamPart128, 1), _mm_add_epi32(drflac__mm_not_si128(_mm_and_si128(riceParamPart128, _mm_set1_epi32(0x01))), _mm_set1_epi32(0x01)));
        if (order <= 4) {
            for (i = 0; i < 4; i += 1) {
                prediction128 = _mm_mullo_epi32(coefficients128_0, samples128_0);
                prediction128 = drflac__mm_hadd_epi32(prediction128);
                prediction128 = _mm_srai_epi32(prediction128, shift);
                prediction128 = _mm_add_epi32(riceParamPart128, prediction128);
                samples128_0 = _mm_alignr_epi8(prediction128, samples128_0, 4);
                riceParamPart128 = _mm_alignr_epi8(_mm_setzero_si128(), riceParamPart128, 4);
            }
        } else if (order <= 8) {
            for (i = 0; i < 4; i += 1) {
                prediction128 =                              _mm_mullo_epi32(coefficients128_4, samples128_4);
                prediction128 = _mm_add_epi32(prediction128, _mm_mullo_epi32(coefficients128_0, samples128_0));
                prediction128 = drflac__mm_hadd_epi32(prediction128);
                prediction128 = _mm_srai_epi32(prediction128, shift);
                prediction128 = _mm_add_epi32(riceParamPart128, prediction128);
                samples128_4 = _mm_alignr_epi8(samples128_0,  samples128_4, 4);
                samples128_0 = _mm_alignr_epi8(prediction128, samples128_0, 4);
                riceParamPart128 = _mm_alignr_epi8(_mm_setzero_si128(), riceParamPart128, 4);
            }
        } else {
            for (i = 0; i < 4; i += 1) {
                prediction128 =                              _mm_mullo_epi32(coefficients128_8, samples128_8);
                prediction128 = _mm_add_epi32(prediction128, _mm_mullo_epi32(coefficients128_4, samples128_4));
                prediction128 = _mm_add_epi32(prediction128, _mm_mullo_epi32(coefficients128_0, samples128_0));
                prediction128 = drflac__mm_hadd_epi32(prediction128);
                prediction128 = _mm_srai_epi32(prediction128, shift);
                prediction128 = _mm_add_epi32(riceParamPart128, prediction128);
                samples128_8 = _mm_alignr_epi8(samples128_4,  samples128_8, 4);
                samples128_4 = _mm_alignr_epi8(samples128_0,  samples128_4, 4);
                samples128_0 = _mm_alignr_epi8(prediction128, samples128_0, 4);
                riceParamPart128 = _mm_alignr_epi8(_mm_setzero_si128(), riceParamPart128, 4);
            }
        }
        _mm_storeu_si128((__m128i*)pDecodedSamples, samples128_0);
        pDecodedSamples += 4;
    }
    i = (count & ~3);
    while (i < (int)count) {
        if (!drflac__read_rice_parts_x1(bs, riceParam, &zeroCountParts0, &riceParamParts0)) {
            return 0;
        }
        riceParamParts0 &= riceParamMask;
        riceParamParts0 |= (zeroCountParts0 << riceParam);
        riceParamParts0  = (riceParamParts0 >> 1) ^ t[riceParamParts0 & 0x01];
        pDecodedSamples[0] = riceParamParts0 + drflac__calculate_prediction_32_s32(order, shift, coefficients, pDecodedSamples);
        i += 1;
        pDecodedSamples += 1;
    }
    return 1;
}
static int drflac__decode_samples_with_residual__rice__sse41_64(drflac_bs* bs, uint32_t count, uint8_t riceParam, uint32_t order, int32_t shift, const int32_t* coefficients, int32_t* pSamplesOut)
{
    int i;
    uint32_t riceParamMask;
    int32_t* pDecodedSamples    = pSamplesOut;
    int32_t* pDecodedSamplesEnd = pSamplesOut + (count & ~3);
    uint32_t zeroCountParts0 = 0;
    uint32_t zeroCountParts1 = 0;
    uint32_t zeroCountParts2 = 0;
    uint32_t zeroCountParts3 = 0;
    uint32_t riceParamParts0 = 0;
    uint32_t riceParamParts1 = 0;
    uint32_t riceParamParts2 = 0;
    uint32_t riceParamParts3 = 0;
    __m128i coefficients128_0;
    __m128i coefficients128_4;
    __m128i coefficients128_8;
    __m128i samples128_0;
    __m128i samples128_4;
    __m128i samples128_8;
    __m128i prediction128;
    __m128i riceParamMask128;
    const uint32_t t[2] = {0x00000000, 0xFFFFFFFF};
    DR_FLAC_ASSERT(order <= 12);
    riceParamMask    = (uint32_t)~((~0UL) << riceParam);
    riceParamMask128 = _mm_set1_epi32(riceParamMask);
    prediction128 = _mm_setzero_si128();
    coefficients128_0  = _mm_setzero_si128();
    coefficients128_4  = _mm_setzero_si128();
    coefficients128_8  = _mm_setzero_si128();
    samples128_0  = _mm_setzero_si128();
    samples128_4  = _mm_setzero_si128();
    samples128_8  = _mm_setzero_si128();
#if 1
    {
        int runningOrder = order;
        if (runningOrder >= 4) {
            coefficients128_0 = _mm_loadu_si128((const __m128i*)(coefficients + 0));
            samples128_0      = _mm_loadu_si128((const __m128i*)(pSamplesOut  - 4));
            runningOrder -= 4;
        } else {
            switch (runningOrder) {
                case 3: coefficients128_0 = _mm_set_epi32(0, coefficients[2], coefficients[1], coefficients[0]); samples128_0 = _mm_set_epi32(pSamplesOut[-1], pSamplesOut[-2], pSamplesOut[-3], 0); break;
                case 2: coefficients128_0 = _mm_set_epi32(0, 0,               coefficients[1], coefficients[0]); samples128_0 = _mm_set_epi32(pSamplesOut[-1], pSamplesOut[-2], 0,               0); break;
                case 1: coefficients128_0 = _mm_set_epi32(0, 0,               0,               coefficients[0]); samples128_0 = _mm_set_epi32(pSamplesOut[-1], 0,               0,               0); break;
            }
            runningOrder = 0;
        }
        if (runningOrder >= 4) {
            coefficients128_4 = _mm_loadu_si128((const __m128i*)(coefficients + 4));
            samples128_4      = _mm_loadu_si128((const __m128i*)(pSamplesOut  - 8));
            runningOrder -= 4;
        } else {
            switch (runningOrder) {
                case 3: coefficients128_4 = _mm_set_epi32(0, coefficients[6], coefficients[5], coefficients[4]); samples128_4 = _mm_set_epi32(pSamplesOut[-5], pSamplesOut[-6], pSamplesOut[-7], 0); break;
                case 2: coefficients128_4 = _mm_set_epi32(0, 0,               coefficients[5], coefficients[4]); samples128_4 = _mm_set_epi32(pSamplesOut[-5], pSamplesOut[-6], 0,               0); break;
                case 1: coefficients128_4 = _mm_set_epi32(0, 0,               0,               coefficients[4]); samples128_4 = _mm_set_epi32(pSamplesOut[-5], 0,               0,               0); break;
            }
            runningOrder = 0;
        }
        if (runningOrder == 4) {
            coefficients128_8 = _mm_loadu_si128((const __m128i*)(coefficients + 8));
            samples128_8      = _mm_loadu_si128((const __m128i*)(pSamplesOut  - 12));
            runningOrder -= 4;
        } else {
            switch (runningOrder) {
                case 3: coefficients128_8 = _mm_set_epi32(0, coefficients[10], coefficients[9], coefficients[8]); samples128_8 = _mm_set_epi32(pSamplesOut[-9], pSamplesOut[-10], pSamplesOut[-11], 0); break;
                case 2: coefficients128_8 = _mm_set_epi32(0, 0,                coefficients[9], coefficients[8]); samples128_8 = _mm_set_epi32(pSamplesOut[-9], pSamplesOut[-10], 0,                0); break;
                case 1: coefficients128_8 = _mm_set_epi32(0, 0,                0,               coefficients[8]); samples128_8 = _mm_set_epi32(pSamplesOut[-9], 0,                0,                0); break;
            }
            runningOrder = 0;
        }
        coefficients128_0 = _mm_shuffle_epi32(coefficients128_0, _MM_SHUFFLE(0, 1, 2, 3));
        coefficients128_4 = _mm_shuffle_epi32(coefficients128_4, _MM_SHUFFLE(0, 1, 2, 3));
        coefficients128_8 = _mm_shuffle_epi32(coefficients128_8, _MM_SHUFFLE(0, 1, 2, 3));
    }
#else
    switch (order)
    {
    case 12: ((int32_t*)&coefficients128_8)[0] = coefficients[11]; ((int32_t*)&samples128_8)[0] = pDecodedSamples[-12];
    case 11: ((int32_t*)&coefficients128_8)[1] = coefficients[10]; ((int32_t*)&samples128_8)[1] = pDecodedSamples[-11];
    case 10: ((int32_t*)&coefficients128_8)[2] = coefficients[ 9]; ((int32_t*)&samples128_8)[2] = pDecodedSamples[-10];
    case 9:  ((int32_t*)&coefficients128_8)[3] = coefficients[ 8]; ((int32_t*)&samples128_8)[3] = pDecodedSamples[- 9];
    case 8:  ((int32_t*)&coefficients128_4)[0] = coefficients[ 7]; ((int32_t*)&samples128_4)[0] = pDecodedSamples[- 8];
    case 7:  ((int32_t*)&coefficients128_4)[1] = coefficients[ 6]; ((int32_t*)&samples128_4)[1] = pDecodedSamples[- 7];
    case 6:  ((int32_t*)&coefficients128_4)[2] = coefficients[ 5]; ((int32_t*)&samples128_4)[2] = pDecodedSamples[- 6];
    case 5:  ((int32_t*)&coefficients128_4)[3] = coefficients[ 4]; ((int32_t*)&samples128_4)[3] = pDecodedSamples[- 5];
    case 4:  ((int32_t*)&coefficients128_0)[0] = coefficients[ 3]; ((int32_t*)&samples128_0)[0] = pDecodedSamples[- 4];
    case 3:  ((int32_t*)&coefficients128_0)[1] = coefficients[ 2]; ((int32_t*)&samples128_0)[1] = pDecodedSamples[- 3];
    case 2:  ((int32_t*)&coefficients128_0)[2] = coefficients[ 1]; ((int32_t*)&samples128_0)[2] = pDecodedSamples[- 2];
    case 1:  ((int32_t*)&coefficients128_0)[3] = coefficients[ 0]; ((int32_t*)&samples128_0)[3] = pDecodedSamples[- 1];
    }
#endif
    while (pDecodedSamples < pDecodedSamplesEnd) {
        __m128i zeroCountPart128;
        __m128i riceParamPart128;
        if (!drflac__read_rice_parts_x1(bs, riceParam, &zeroCountParts0, &riceParamParts0) ||
            !drflac__read_rice_parts_x1(bs, riceParam, &zeroCountParts1, &riceParamParts1) ||
            !drflac__read_rice_parts_x1(bs, riceParam, &zeroCountParts2, &riceParamParts2) ||
            !drflac__read_rice_parts_x1(bs, riceParam, &zeroCountParts3, &riceParamParts3)) {
            return 0;
        }
        zeroCountPart128 = _mm_set_epi32(zeroCountParts3, zeroCountParts2, zeroCountParts1, zeroCountParts0);
        riceParamPart128 = _mm_set_epi32(riceParamParts3, riceParamParts2, riceParamParts1, riceParamParts0);
        riceParamPart128 = _mm_and_si128(riceParamPart128, riceParamMask128);
        riceParamPart128 = _mm_or_si128(riceParamPart128, _mm_slli_epi32(zeroCountPart128, riceParam));
        riceParamPart128 = _mm_xor_si128(_mm_srli_epi32(riceParamPart128, 1), _mm_add_epi32(drflac__mm_not_si128(_mm_and_si128(riceParamPart128, _mm_set1_epi32(1))), _mm_set1_epi32(1)));
        for (i = 0; i < 4; i += 1) {
            prediction128 = _mm_xor_si128(prediction128, prediction128);
            switch (order)
            {
            case 12:
            case 11: prediction128 = _mm_add_epi64(prediction128, _mm_mul_epi32(_mm_shuffle_epi32(coefficients128_8, _MM_SHUFFLE(1, 1, 0, 0)), _mm_shuffle_epi32(samples128_8, _MM_SHUFFLE(1, 1, 0, 0))));
            case 10:
            case  9: prediction128 = _mm_add_epi64(prediction128, _mm_mul_epi32(_mm_shuffle_epi32(coefficients128_8, _MM_SHUFFLE(3, 3, 2, 2)), _mm_shuffle_epi32(samples128_8, _MM_SHUFFLE(3, 3, 2, 2))));
            case  8:
            case  7: prediction128 = _mm_add_epi64(prediction128, _mm_mul_epi32(_mm_shuffle_epi32(coefficients128_4, _MM_SHUFFLE(1, 1, 0, 0)), _mm_shuffle_epi32(samples128_4, _MM_SHUFFLE(1, 1, 0, 0))));
            case  6:
            case  5: prediction128 = _mm_add_epi64(prediction128, _mm_mul_epi32(_mm_shuffle_epi32(coefficients128_4, _MM_SHUFFLE(3, 3, 2, 2)), _mm_shuffle_epi32(samples128_4, _MM_SHUFFLE(3, 3, 2, 2))));
            case  4:
            case  3: prediction128 = _mm_add_epi64(prediction128, _mm_mul_epi32(_mm_shuffle_epi32(coefficients128_0, _MM_SHUFFLE(1, 1, 0, 0)), _mm_shuffle_epi32(samples128_0, _MM_SHUFFLE(1, 1, 0, 0))));
            case  2:
            case  1: prediction128 = _mm_add_epi64(prediction128, _mm_mul_epi32(_mm_shuffle_epi32(coefficients128_0, _MM_SHUFFLE(3, 3, 2, 2)), _mm_shuffle_epi32(samples128_0, _MM_SHUFFLE(3, 3, 2, 2))));
            }
            prediction128 = drflac__mm_hadd_epi64(prediction128);
            prediction128 = drflac__mm_srai_epi64(prediction128, shift);
            prediction128 = _mm_add_epi32(riceParamPart128, prediction128);
            samples128_8 = _mm_alignr_epi8(samples128_4,  samples128_8, 4);
            samples128_4 = _mm_alignr_epi8(samples128_0,  samples128_4, 4);
            samples128_0 = _mm_alignr_epi8(prediction128, samples128_0, 4);
            riceParamPart128 = _mm_alignr_epi8(_mm_setzero_si128(), riceParamPart128, 4);
        }
        _mm_storeu_si128((__m128i*)pDecodedSamples, samples128_0);
        pDecodedSamples += 4;
    }
    i = (count & ~3);
    while (i < (int)count) {
        if (!drflac__read_rice_parts_x1(bs, riceParam, &zeroCountParts0, &riceParamParts0)) {
            return 0;
        }
        riceParamParts0 &= riceParamMask;
        riceParamParts0 |= (zeroCountParts0 << riceParam);
        riceParamParts0  = (riceParamParts0 >> 1) ^ t[riceParamParts0 & 0x01];
        pDecodedSamples[0] = riceParamParts0 + drflac__calculate_prediction_64_s32(order, shift, coefficients, pDecodedSamples);
        i += 1;
        pDecodedSamples += 1;
    }
    return 1;
}
static int drflac__decode_samples_with_residual__rice__sse41(drflac_bs* bs, uint32_t bitsPerSample, uint32_t count, uint8_t riceParam, uint32_t lpcOrder, int32_t lpcShift, uint32_t lpcPrecision, const int32_t* coefficients, int32_t* pSamplesOut)
{
    DR_FLAC_ASSERT(bs != NULL);
    DR_FLAC_ASSERT(pSamplesOut != NULL);
    if (lpcOrder > 0 && lpcOrder <= 12) {
        if (drflac__use_64_bit_prediction(bitsPerSample, lpcOrder, lpcPrecision)) {
            return drflac__decode_samples_with_residual__rice__sse41_64(bs, count, riceParam, lpcOrder, lpcShift, coefficients, pSamplesOut);
        } else {
            return drflac__decode_samples_with_residual__rice__sse41_32(bs, count, riceParam, lpcOrder, lpcShift, coefficients, pSamplesOut);
        }
    } else {
        return drflac__decode_samples_with_residual__rice__scalar(bs, bitsPerSample, count, riceParam, lpcOrder, lpcShift, lpcPrecision, coefficients, pSamplesOut);
    }
}
#endif
#if defined(DR_FLAC_SUPPORT_NEON)
static DRFLAC_INLINE void drflac__vst2q_s32(int32_t* p, int32x4x2_t x)
{
    vst1q_s32(p+0, x.val[0]);
    vst1q_s32(p+4, x.val[1]);
}
static DRFLAC_INLINE void drflac__vst2q_u32(uint32_t* p, uint32x4x2_t x)
{
    vst1q_u32(p+0, x.val[0]);
    vst1q_u32(p+4, x.val[1]);
}
static DRFLAC_INLINE void drflac__vst2q_f32(float* p, float32x4x2_t x)
{
    vst1q_f32(p+0, x.val[0]);
    vst1q_f32(p+4, x.val[1]);
}
static DRFLAC_INLINE void drflac__vst2q_s16(int16_t* p, int16x4x2_t x)
{
    vst1q_s16(p, vcombine_s16(x.val[0], x.val[1]));
}
static DRFLAC_INLINE void drflac__vst2q_u16(uint16_t* p, uint16x4x2_t x)
{
    vst1q_u16(p, vcombine_u16(x.val[0], x.val[1]));
}
static DRFLAC_INLINE int32x4_t drflac__vdupq_n_s32x4(int32_t x3, int32_t x2, int32_t x1, int32_t x0)
{
    int32_t x[4];
    x[3] = x3;
    x[2] = x2;
    x[1] = x1;
    x[0] = x0;
    return vld1q_s32(x);
}
static DRFLAC_INLINE int32x4_t drflac__valignrq_s32_1(int32x4_t a, int32x4_t b)
{
    return vextq_s32(b, a, 1);
}
static DRFLAC_INLINE uint32x4_t drflac__valignrq_u32_1(uint32x4_t a, uint32x4_t b)
{
    return vextq_u32(b, a, 1);
}
static DRFLAC_INLINE int32x2_t drflac__vhaddq_s32(int32x4_t x)
{
    int32x2_t r = vadd_s32(vget_high_s32(x), vget_low_s32(x));
    return vpadd_s32(r, r);
}
static DRFLAC_INLINE int64x1_t drflac__vhaddq_s64(int64x2_t x)
{
    return vadd_s64(vget_high_s64(x), vget_low_s64(x));
}
static DRFLAC_INLINE int32x4_t drflac__vrevq_s32(int32x4_t x)
{
    return vrev64q_s32(vcombine_s32(vget_high_s32(x), vget_low_s32(x)));
}
static DRFLAC_INLINE int32x4_t drflac__vnotq_s32(int32x4_t x)
{
    return veorq_s32(x, vdupq_n_s32(0xFFFFFFFF));
}
static DRFLAC_INLINE uint32x4_t drflac__vnotq_u32(uint32x4_t x)
{
    return veorq_u32(x, vdupq_n_u32(0xFFFFFFFF));
}
static int drflac__decode_samples_with_residual__rice__neon_32(drflac_bs* bs, uint32_t count, uint8_t riceParam, uint32_t order, int32_t shift, const int32_t* coefficients, int32_t* pSamplesOut)
{
    int i;
    uint32_t riceParamMask;
    int32_t* pDecodedSamples    = pSamplesOut;
    int32_t* pDecodedSamplesEnd = pSamplesOut + (count & ~3);
    uint32_t zeroCountParts[4];
    uint32_t riceParamParts[4];
    int32x4_t coefficients128_0;
    int32x4_t coefficients128_4;
    int32x4_t coefficients128_8;
    int32x4_t samples128_0;
    int32x4_t samples128_4;
    int32x4_t samples128_8;
    uint32x4_t riceParamMask128;
    int32x4_t riceParam128;
    int32x2_t shift64;
    uint32x4_t one128;
    const uint32_t t[2] = {0x00000000, 0xFFFFFFFF};
    riceParamMask    = (uint32_t)~((~0UL) << riceParam);
    riceParamMask128 = vdupq_n_u32(riceParamMask);
    riceParam128 = vdupq_n_s32(riceParam);
    shift64 = vdup_n_s32(-shift);
    one128 = vdupq_n_u32(1);
    {
        int runningOrder = order;
        int32_t tempC[4] = {0, 0, 0, 0};
        int32_t tempS[4] = {0, 0, 0, 0};
        if (runningOrder >= 4) {
            coefficients128_0 = vld1q_s32(coefficients + 0);
            samples128_0      = vld1q_s32(pSamplesOut  - 4);
            runningOrder -= 4;
        } else {
            switch (runningOrder) {
                case 3: tempC[2] = coefficients[2]; tempS[1] = pSamplesOut[-3];
                case 2: tempC[1] = coefficients[1]; tempS[2] = pSamplesOut[-2];
                case 1: tempC[0] = coefficients[0]; tempS[3] = pSamplesOut[-1];
            }
            coefficients128_0 = vld1q_s32(tempC);
            samples128_0      = vld1q_s32(tempS);
            runningOrder = 0;
        }
        if (runningOrder >= 4) {
            coefficients128_4 = vld1q_s32(coefficients + 4);
            samples128_4      = vld1q_s32(pSamplesOut  - 8);
            runningOrder -= 4;
        } else {
            switch (runningOrder) {
                case 3: tempC[2] = coefficients[6]; tempS[1] = pSamplesOut[-7];
                case 2: tempC[1] = coefficients[5]; tempS[2] = pSamplesOut[-6];
                case 1: tempC[0] = coefficients[4]; tempS[3] = pSamplesOut[-5];
            }
            coefficients128_4 = vld1q_s32(tempC);
            samples128_4      = vld1q_s32(tempS);
            runningOrder = 0;
        }
        if (runningOrder == 4) {
            coefficients128_8 = vld1q_s32(coefficients + 8);
            samples128_8      = vld1q_s32(pSamplesOut  - 12);
            runningOrder -= 4;
        } else {
            switch (runningOrder) {
                case 3: tempC[2] = coefficients[10]; tempS[1] = pSamplesOut[-11];
                case 2: tempC[1] = coefficients[ 9]; tempS[2] = pSamplesOut[-10];
                case 1: tempC[0] = coefficients[ 8]; tempS[3] = pSamplesOut[- 9];
            }
            coefficients128_8 = vld1q_s32(tempC);
            samples128_8      = vld1q_s32(tempS);
            runningOrder = 0;
        }
        coefficients128_0 = drflac__vrevq_s32(coefficients128_0);
        coefficients128_4 = drflac__vrevq_s32(coefficients128_4);
        coefficients128_8 = drflac__vrevq_s32(coefficients128_8);
    }
    while (pDecodedSamples < pDecodedSamplesEnd) {
        int32x4_t prediction128;
        int32x2_t prediction64;
        uint32x4_t zeroCountPart128;
        uint32x4_t riceParamPart128;
        if (!drflac__read_rice_parts_x1(bs, riceParam, &zeroCountParts[0], &riceParamParts[0]) ||
            !drflac__read_rice_parts_x1(bs, riceParam, &zeroCountParts[1], &riceParamParts[1]) ||
            !drflac__read_rice_parts_x1(bs, riceParam, &zeroCountParts[2], &riceParamParts[2]) ||
            !drflac__read_rice_parts_x1(bs, riceParam, &zeroCountParts[3], &riceParamParts[3])) {
            return 0;
        }
        zeroCountPart128 = vld1q_u32(zeroCountParts);
        riceParamPart128 = vld1q_u32(riceParamParts);
        riceParamPart128 = vandq_u32(riceParamPart128, riceParamMask128);
        riceParamPart128 = vorrq_u32(riceParamPart128, vshlq_u32(zeroCountPart128, riceParam128));
        riceParamPart128 = veorq_u32(vshrq_n_u32(riceParamPart128, 1), vaddq_u32(drflac__vnotq_u32(vandq_u32(riceParamPart128, one128)), one128));
        if (order <= 4) {
            for (i = 0; i < 4; i += 1) {
                prediction128 = vmulq_s32(coefficients128_0, samples128_0);
                prediction64 = drflac__vhaddq_s32(prediction128);
                prediction64 = vshl_s32(prediction64, shift64);
                prediction64 = vadd_s32(prediction64, vget_low_s32(vreinterpretq_s32_u32(riceParamPart128)));
                samples128_0 = drflac__valignrq_s32_1(vcombine_s32(prediction64, vdup_n_s32(0)), samples128_0);
                riceParamPart128 = drflac__valignrq_u32_1(vdupq_n_u32(0), riceParamPart128);
            }
        } else if (order <= 8) {
            for (i = 0; i < 4; i += 1) {
                prediction128 =                vmulq_s32(coefficients128_4, samples128_4);
                prediction128 = vmlaq_s32(prediction128, coefficients128_0, samples128_0);
                prediction64 = drflac__vhaddq_s32(prediction128);
                prediction64 = vshl_s32(prediction64, shift64);
                prediction64 = vadd_s32(prediction64, vget_low_s32(vreinterpretq_s32_u32(riceParamPart128)));
                samples128_4 = drflac__valignrq_s32_1(samples128_0, samples128_4);
                samples128_0 = drflac__valignrq_s32_1(vcombine_s32(prediction64, vdup_n_s32(0)), samples128_0);
                riceParamPart128 = drflac__valignrq_u32_1(vdupq_n_u32(0), riceParamPart128);
            }
        } else {
            for (i = 0; i < 4; i += 1) {
                prediction128 =                vmulq_s32(coefficients128_8, samples128_8);
                prediction128 = vmlaq_s32(prediction128, coefficients128_4, samples128_4);
                prediction128 = vmlaq_s32(prediction128, coefficients128_0, samples128_0);
                prediction64 = drflac__vhaddq_s32(prediction128);
                prediction64 = vshl_s32(prediction64, shift64);
                prediction64 = vadd_s32(prediction64, vget_low_s32(vreinterpretq_s32_u32(riceParamPart128)));
                samples128_8 = drflac__valignrq_s32_1(samples128_4, samples128_8);
                samples128_4 = drflac__valignrq_s32_1(samples128_0, samples128_4);
                samples128_0 = drflac__valignrq_s32_1(vcombine_s32(prediction64, vdup_n_s32(0)), samples128_0);
                riceParamPart128 = drflac__valignrq_u32_1(vdupq_n_u32(0), riceParamPart128);
            }
        }
        vst1q_s32(pDecodedSamples, samples128_0);
        pDecodedSamples += 4;
    }
    i = (count & ~3);
    while (i < (int)count) {
        if (!drflac__read_rice_parts_x1(bs, riceParam, &zeroCountParts[0], &riceParamParts[0])) {
            return 0;
        }
        riceParamParts[0] &= riceParamMask;
        riceParamParts[0] |= (zeroCountParts[0] << riceParam);
        riceParamParts[0]  = (riceParamParts[0] >> 1) ^ t[riceParamParts[0] & 0x01];
        pDecodedSamples[0] = riceParamParts[0] + drflac__calculate_prediction_32_s32(order, shift, coefficients, pDecodedSamples);
        i += 1;
        pDecodedSamples += 1;
    }
    return 1;
}
static int drflac__decode_samples_with_residual__rice__neon_64(drflac_bs* bs, uint32_t count, uint8_t riceParam, uint32_t order, int32_t shift, const int32_t* coefficients, int32_t* pSamplesOut)
{
    int i;
    uint32_t riceParamMask;
    int32_t* pDecodedSamples    = pSamplesOut;
    int32_t* pDecodedSamplesEnd = pSamplesOut + (count & ~3);
    uint32_t zeroCountParts[4];
    uint32_t riceParamParts[4];
    int32x4_t coefficients128_0;
    int32x4_t coefficients128_4;
    int32x4_t coefficients128_8;
    int32x4_t samples128_0;
    int32x4_t samples128_4;
    int32x4_t samples128_8;
    uint32x4_t riceParamMask128;
    int32x4_t riceParam128;
    int64x1_t shift64;
    uint32x4_t one128;
    int64x2_t prediction128;
    uint32x4_t zeroCountPart128;
    uint32x4_t riceParamPart128;
    const uint32_t t[2] = {0x00000000, 0xFFFFFFFF};
    riceParamMask    = (uint32_t)~((~0UL) << riceParam);
    riceParamMask128 = vdupq_n_u32(riceParamMask);
    riceParam128 = vdupq_n_s32(riceParam);
    shift64 = vdup_n_s64(-shift);
    one128 = vdupq_n_u32(1);
    {
        int runningOrder = order;
        int32_t tempC[4] = {0, 0, 0, 0};
        int32_t tempS[4] = {0, 0, 0, 0};
        if (runningOrder >= 4) {
            coefficients128_0 = vld1q_s32(coefficients + 0);
            samples128_0      = vld1q_s32(pSamplesOut  - 4);
            runningOrder -= 4;
        } else {
            switch (runningOrder) {
                case 3: tempC[2] = coefficients[2]; tempS[1] = pSamplesOut[-3];
                case 2: tempC[1] = coefficients[1]; tempS[2] = pSamplesOut[-2];
                case 1: tempC[0] = coefficients[0]; tempS[3] = pSamplesOut[-1];
            }
            coefficients128_0 = vld1q_s32(tempC);
            samples128_0      = vld1q_s32(tempS);
            runningOrder = 0;
        }
        if (runningOrder >= 4) {
            coefficients128_4 = vld1q_s32(coefficients + 4);
            samples128_4      = vld1q_s32(pSamplesOut  - 8);
            runningOrder -= 4;
        } else {
            switch (runningOrder) {
                case 3: tempC[2] = coefficients[6]; tempS[1] = pSamplesOut[-7];
                case 2: tempC[1] = coefficients[5]; tempS[2] = pSamplesOut[-6];
                case 1: tempC[0] = coefficients[4]; tempS[3] = pSamplesOut[-5];
            }
            coefficients128_4 = vld1q_s32(tempC);
            samples128_4      = vld1q_s32(tempS);
            runningOrder = 0;
        }
        if (runningOrder == 4) {
            coefficients128_8 = vld1q_s32(coefficients + 8);
            samples128_8      = vld1q_s32(pSamplesOut  - 12);
            runningOrder -= 4;
        } else {
            switch (runningOrder) {
                case 3: tempC[2] = coefficients[10]; tempS[1] = pSamplesOut[-11];
                case 2: tempC[1] = coefficients[ 9]; tempS[2] = pSamplesOut[-10];
                case 1: tempC[0] = coefficients[ 8]; tempS[3] = pSamplesOut[- 9];
            }
            coefficients128_8 = vld1q_s32(tempC);
            samples128_8      = vld1q_s32(tempS);
            runningOrder = 0;
        }
        coefficients128_0 = drflac__vrevq_s32(coefficients128_0);
        coefficients128_4 = drflac__vrevq_s32(coefficients128_4);
        coefficients128_8 = drflac__vrevq_s32(coefficients128_8);
    }
    while (pDecodedSamples < pDecodedSamplesEnd) {
        if (!drflac__read_rice_parts_x1(bs, riceParam, &zeroCountParts[0], &riceParamParts[0]) ||
            !drflac__read_rice_parts_x1(bs, riceParam, &zeroCountParts[1], &riceParamParts[1]) ||
            !drflac__read_rice_parts_x1(bs, riceParam, &zeroCountParts[2], &riceParamParts[2]) ||
            !drflac__read_rice_parts_x1(bs, riceParam, &zeroCountParts[3], &riceParamParts[3])) {
            return 0;
        }
        zeroCountPart128 = vld1q_u32(zeroCountParts);
        riceParamPart128 = vld1q_u32(riceParamParts);
        riceParamPart128 = vandq_u32(riceParamPart128, riceParamMask128);
        riceParamPart128 = vorrq_u32(riceParamPart128, vshlq_u32(zeroCountPart128, riceParam128));
        riceParamPart128 = veorq_u32(vshrq_n_u32(riceParamPart128, 1), vaddq_u32(drflac__vnotq_u32(vandq_u32(riceParamPart128, one128)), one128));
        for (i = 0; i < 4; i += 1) {
            int64x1_t prediction64;
            prediction128 = vdupq_n_s64(0);
            switch (order)
            {
            case 12:
            case 11: prediction128 = vaddq_s64(prediction128, vmull_s32(vget_low_s32(coefficients128_8), vget_low_s32(samples128_8)));
            case 10:
            case  9: prediction128 = vaddq_s64(prediction128, vmull_s32(vget_high_s32(coefficients128_8), vget_high_s32(samples128_8)));
            case  8:
            case  7: prediction128 = vaddq_s64(prediction128, vmull_s32(vget_low_s32(coefficients128_4), vget_low_s32(samples128_4)));
            case  6:
            case  5: prediction128 = vaddq_s64(prediction128, vmull_s32(vget_high_s32(coefficients128_4), vget_high_s32(samples128_4)));
            case  4:
            case  3: prediction128 = vaddq_s64(prediction128, vmull_s32(vget_low_s32(coefficients128_0), vget_low_s32(samples128_0)));
            case  2:
            case  1: prediction128 = vaddq_s64(prediction128, vmull_s32(vget_high_s32(coefficients128_0), vget_high_s32(samples128_0)));
            }
            prediction64 = drflac__vhaddq_s64(prediction128);
            prediction64 = vshl_s64(prediction64, shift64);
            prediction64 = vadd_s64(prediction64, vdup_n_s64(vgetq_lane_u32(riceParamPart128, 0)));
            samples128_8 = drflac__valignrq_s32_1(samples128_4, samples128_8);
            samples128_4 = drflac__valignrq_s32_1(samples128_0, samples128_4);
            samples128_0 = drflac__valignrq_s32_1(vcombine_s32(vreinterpret_s32_s64(prediction64), vdup_n_s32(0)), samples128_0);
            riceParamPart128 = drflac__valignrq_u32_1(vdupq_n_u32(0), riceParamPart128);
        }
        vst1q_s32(pDecodedSamples, samples128_0);
        pDecodedSamples += 4;
    }
    i = (count & ~3);
    while (i < (int)count) {
        if (!drflac__read_rice_parts_x1(bs, riceParam, &zeroCountParts[0], &riceParamParts[0])) {
            return 0;
        }
        riceParamParts[0] &= riceParamMask;
        riceParamParts[0] |= (zeroCountParts[0] << riceParam);
        riceParamParts[0]  = (riceParamParts[0] >> 1) ^ t[riceParamParts[0] & 0x01];
        pDecodedSamples[0] = riceParamParts[0] + drflac__calculate_prediction_64_s32(order, shift, coefficients, pDecodedSamples);
        i += 1;
        pDecodedSamples += 1;
    }
    return 1;
}
static int drflac__decode_samples_with_residual__rice__neon(drflac_bs* bs, uint32_t bitsPerSample, uint32_t count, uint8_t riceParam, uint32_t lpcOrder, int32_t lpcShift, uint32_t lpcPrecision, const int32_t* coefficients, int32_t* pSamplesOut)
{
    DR_FLAC_ASSERT(bs != NULL);
    DR_FLAC_ASSERT(pSamplesOut != NULL);
    if (lpcOrder > 0 && lpcOrder <= 12) {
        if (drflac__use_64_bit_prediction(bitsPerSample, lpcOrder, lpcPrecision)) {
            return drflac__decode_samples_with_residual__rice__neon_64(bs, count, riceParam, lpcOrder, lpcShift, coefficients, pSamplesOut);
        } else {
            return drflac__decode_samples_with_residual__rice__neon_32(bs, count, riceParam, lpcOrder, lpcShift, coefficients, pSamplesOut);
        }
    } else {
        return drflac__decode_samples_with_residual__rice__scalar(bs, bitsPerSample, count, riceParam, lpcOrder, lpcShift, lpcPrecision, coefficients, pSamplesOut);
    }
}
#endif
static int drflac__decode_samples_with_residual__rice(drflac_bs* bs, uint32_t bitsPerSample, uint32_t count, uint8_t riceParam, uint32_t lpcOrder, int32_t lpcShift, uint32_t lpcPrecision, const int32_t* coefficients, int32_t* pSamplesOut)
{
#if defined(DR_FLAC_SUPPORT_SSE41)
    if (drflac__gIsSSE41Supported) {
        return drflac__decode_samples_with_residual__rice__sse41(bs, bitsPerSample, count, riceParam, lpcOrder, lpcShift, lpcPrecision, coefficients, pSamplesOut);
    } else
#elif defined(DR_FLAC_SUPPORT_NEON)
    if (drflac__gIsNEONSupported) {
        return drflac__decode_samples_with_residual__rice__neon(bs, bitsPerSample, count, riceParam, lpcOrder, lpcShift, lpcPrecision, coefficients, pSamplesOut);
    } else
#endif
    {
    #if 0
        return drflac__decode_samples_with_residual__rice__reference(bs, bitsPerSample, count, riceParam, lpcOrder, lpcShift, lpcPrecision, coefficients, pSamplesOut);
    #else
        return drflac__decode_samples_with_residual__rice__scalar(bs, bitsPerSample, count, riceParam, lpcOrder, lpcShift, lpcPrecision, coefficients, pSamplesOut);
    #endif
    }
}
static int drflac__read_and_seek_residual__rice(drflac_bs* bs, uint32_t count, uint8_t riceParam)
{
    uint32_t i;
    DR_FLAC_ASSERT(bs != NULL);
    for (i = 0; i < count; ++i) {
        if (!drflac__seek_rice_parts(bs, riceParam)) {
            return 0;
        }
    }
    return 1;
}
#if defined(__clang__)
__attribute__((no_sanitize("signed-integer-overflow")))
#endif
static int drflac__decode_samples_with_residual__unencoded(drflac_bs* bs, uint32_t bitsPerSample, uint32_t count, uint8_t unencodedBitsPerSample, uint32_t lpcOrder, int32_t lpcShift, uint32_t lpcPrecision, const int32_t* coefficients, int32_t* pSamplesOut)
{
    uint32_t i;
    DR_FLAC_ASSERT(bs != NULL);
    DR_FLAC_ASSERT(unencodedBitsPerSample <= 31);
    DR_FLAC_ASSERT(pSamplesOut != NULL);
    for (i = 0; i < count; ++i) {
        if (unencodedBitsPerSample > 0) {
            if (!drflac__read_int32(bs, unencodedBitsPerSample, pSamplesOut + i)) {
                return 0;
            }
        } else {
            pSamplesOut[i] = 0;
        }
        if (drflac__use_64_bit_prediction(bitsPerSample, lpcOrder, lpcPrecision)) {
            pSamplesOut[i] += drflac__calculate_prediction_64_s32(lpcOrder, lpcShift, coefficients, pSamplesOut + i);
        } else {
            pSamplesOut[i] += drflac__calculate_prediction_32_s32(lpcOrder, lpcShift, coefficients, pSamplesOut + i);
        }
    }
    return 1;
}
static DRFLAC_INLINE int64_t drflac__calculate_prediction_64_s64(uint32_t order, int32_t shift, const int32_t* coefficients, const int64_t* pDecodedSamples)
{
    int64_t prediction = 0;
    uint32_t i;
    for (i = 0; i < order; ++i) {
        prediction += coefficients[i] * pDecodedSamples[-(int32_t)i - 1];
    }
    return prediction >> shift;
}
static int drflac__decode_samples_with_residual__rice__s64(drflac_bs* bs, uint32_t bitsPerSample, uint32_t count, uint8_t riceParam, uint32_t lpcOrder, int32_t lpcShift, const int32_t* coefficients, int64_t* pSamplesOut)
{
    uint32_t riceParamMask;
    int64_t minSample;
    int64_t maxSample;
    uint32_t i;
    DR_FLAC_ASSERT(bitsPerSample > 0 && bitsPerSample <= 33);
    riceParamMask = (1U << riceParam) - 1;
    minSample = -((int64_t)1 << (bitsPerSample - 1));
    maxSample = -(minSample + 1);
    for (i = 0; i < count; ++i) {
        uint32_t zeroCountPart;
        uint32_t riceParamPart;
        int32_t residual;
        int64_t sample;
        if (!drflac__read_rice_parts_x1(bs, riceParam, &zeroCountPart, &riceParamPart)) {
            return 0;
        }
        if (zeroCountPart > (0xFFFFFFFFU >> riceParam)) {
            return 0;
        }
        riceParamPart = (zeroCountPart << riceParam) | (riceParamPart & riceParamMask);
        if (riceParamPart == 0xFFFFFFFFU) {
            return 0;
        }
        residual = (int32_t)(riceParamPart >> 1);
        if ((riceParamPart & 1) != 0) {
            residual = -residual - 1;
        }
        sample = drflac__calculate_prediction_64_s64(lpcOrder, lpcShift, coefficients, pSamplesOut + i) + residual;
        if (sample < minSample || sample > maxSample) {
            return 0;
        }
        pSamplesOut[i] = sample;
    }
    return 1;
}
static int drflac__decode_samples_with_residual__unencoded_s64(drflac_bs* bs, uint32_t bitsPerSample, uint32_t count, uint8_t unencodedBitsPerSample, uint32_t lpcOrder, int32_t lpcShift, const int32_t* coefficients, int64_t* pSamplesOut)
{
    int64_t minSample;
    int64_t maxSample;
    uint32_t i;
    DR_FLAC_ASSERT(bitsPerSample > 0 && bitsPerSample <= 33);
    DR_FLAC_ASSERT(unencodedBitsPerSample <= 31);
    minSample = -((int64_t)1 << (bitsPerSample - 1));
    maxSample = -(minSample + 1);
    for (i = 0; i < count; ++i) {
        int32_t residual = 0;
        int64_t sample;
        if (unencodedBitsPerSample > 0) {
            if (!drflac__read_int32(bs, unencodedBitsPerSample, &residual)) {
                return 0;
            }
        }
        sample = drflac__calculate_prediction_64_s64(lpcOrder, lpcShift, coefficients, pSamplesOut + i) + residual;
        if (sample < minSample || sample > maxSample) {
            return 0;
        }
        pSamplesOut[i] = sample;
    }
    return 1;
}
static int drflac__decode_samples_with_residual(drflac_bs* bs, uint32_t bitsPerSample, uint32_t blockSize, uint32_t lpcOrder, int32_t lpcShift, uint32_t lpcPrecision, const int32_t* coefficients, int32_t* pDecodedSamplesS32, int64_t* pDecodedSamplesS64)
{
    uint8_t residualMethod;
    uint8_t partitionOrder;
    uint32_t samplesInPartition;
    uint32_t partitionsRemaining;
    DR_FLAC_ASSERT(bs != NULL);
    DR_FLAC_ASSERT(blockSize != 0);
    DR_FLAC_ASSERT(pDecodedSamplesS32 != NULL || pDecodedSamplesS64 != NULL);
    DR_FLAC_ASSERT(pDecodedSamplesS32 == NULL || pDecodedSamplesS64 == NULL);
    if (!drflac__read_uint8(bs, 2, &residualMethod)) {
        return 0;
    }
    if (residualMethod != DR_FLAC_RESIDUAL_CODING_METHOD_PARTITIONED_RICE && residualMethod != DR_FLAC_RESIDUAL_CODING_METHOD_PARTITIONED_RICE2) {
        return 0;
    }
    if (!drflac__read_uint8(bs, 4, &partitionOrder)) {
        return 0;
    }
    if ((blockSize & ((1U << partitionOrder) - 1)) != 0) {
        return 0;
    }
    if ((blockSize / (1 << partitionOrder)) < lpcOrder) {
        return 0;
    }
    if (pDecodedSamplesS32 != NULL) {
        pDecodedSamplesS32 += lpcOrder;
    } else {
        pDecodedSamplesS64 += lpcOrder;
    }
    samplesInPartition = (blockSize / (1 << partitionOrder)) - lpcOrder;
    partitionsRemaining = (1 << partitionOrder);
    for (;;) {
        uint8_t riceParam = 0;
        if (residualMethod == DR_FLAC_RESIDUAL_CODING_METHOD_PARTITIONED_RICE) {
            if (!drflac__read_uint8(bs, 4, &riceParam)) {
                return 0;
            }
            if (riceParam == 15) {
                riceParam = 0xFF;
            }
        } else if (residualMethod == DR_FLAC_RESIDUAL_CODING_METHOD_PARTITIONED_RICE2) {
            if (!drflac__read_uint8(bs, 5, &riceParam)) {
                return 0;
            }
            if (riceParam == 31) {
                riceParam = 0xFF;
            }
        }
        if (riceParam != 0xFF) {
            if (pDecodedSamplesS32 != NULL) {
                if (!drflac__decode_samples_with_residual__rice(bs, bitsPerSample, samplesInPartition, riceParam, lpcOrder, lpcShift, lpcPrecision, coefficients, pDecodedSamplesS32)) {
                    return 0;
                }
            } else {
                if (!drflac__decode_samples_with_residual__rice__s64(bs, bitsPerSample, samplesInPartition, riceParam, lpcOrder, lpcShift, coefficients, pDecodedSamplesS64)) {
                    return 0;
                }
            }
        } else {
            uint8_t unencodedBitsPerSample = 0;
            if (!drflac__read_uint8(bs, 5, &unencodedBitsPerSample)) {
                return 0;
            }
            if (pDecodedSamplesS32 != NULL) {
                if (!drflac__decode_samples_with_residual__unencoded(bs, bitsPerSample, samplesInPartition, unencodedBitsPerSample, lpcOrder, lpcShift, lpcPrecision, coefficients, pDecodedSamplesS32)) {
                    return 0;
                }
            } else {
                if (!drflac__decode_samples_with_residual__unencoded_s64(bs, bitsPerSample, samplesInPartition, unencodedBitsPerSample, lpcOrder, lpcShift, coefficients, pDecodedSamplesS64)) {
                    return 0;
                }
            }
        }
        if (pDecodedSamplesS32 != NULL) {
            pDecodedSamplesS32 += samplesInPartition;
        } else {
            pDecodedSamplesS64 += samplesInPartition;
        }
        if (partitionsRemaining == 1) {
            break;
        }
        partitionsRemaining -= 1;
        if (partitionOrder != 0) {
            samplesInPartition = blockSize / (1 << partitionOrder);
        }
    }
    return 1;
}
static int drflac__read_and_seek_residual(drflac_bs* bs, uint32_t blockSize, uint32_t order)
{
    uint8_t residualMethod;
    uint8_t partitionOrder;
    uint32_t samplesInPartition;
    uint32_t partitionsRemaining;
    DR_FLAC_ASSERT(bs != NULL);
    DR_FLAC_ASSERT(blockSize != 0);
    if (!drflac__read_uint8(bs, 2, &residualMethod)) {
        return 0;
    }
    if (residualMethod != DR_FLAC_RESIDUAL_CODING_METHOD_PARTITIONED_RICE && residualMethod != DR_FLAC_RESIDUAL_CODING_METHOD_PARTITIONED_RICE2) {
        return 0;
    }
    if (!drflac__read_uint8(bs, 4, &partitionOrder)) {
        return 0;
    }
    if ((blockSize & ((1U << partitionOrder) - 1)) != 0) {
        return 0;
    }
    if ((blockSize / (1 << partitionOrder)) < order) {
        return 0;
    }
    samplesInPartition = (blockSize / (1 << partitionOrder)) - order;
    partitionsRemaining = (1 << partitionOrder);
    for (;;)
    {
        uint8_t riceParam = 0;
        if (residualMethod == DR_FLAC_RESIDUAL_CODING_METHOD_PARTITIONED_RICE) {
            if (!drflac__read_uint8(bs, 4, &riceParam)) {
                return 0;
            }
            if (riceParam == 15) {
                riceParam = 0xFF;
            }
        } else if (residualMethod == DR_FLAC_RESIDUAL_CODING_METHOD_PARTITIONED_RICE2) {
            if (!drflac__read_uint8(bs, 5, &riceParam)) {
                return 0;
            }
            if (riceParam == 31) {
                riceParam = 0xFF;
            }
        }
        if (riceParam != 0xFF) {
            if (!drflac__read_and_seek_residual__rice(bs, samplesInPartition, riceParam)) {
                return 0;
            }
        } else {
            uint8_t unencodedBitsPerSample = 0;
            if (!drflac__read_uint8(bs, 5, &unencodedBitsPerSample)) {
                return 0;
            }
            if (!drflac__seek_bits(bs, unencodedBitsPerSample * samplesInPartition)) {
                return 0;
            }
        }
        if (partitionsRemaining == 1) {
            break;
        }
        partitionsRemaining -= 1;
        samplesInPartition = blockSize / (1 << partitionOrder);
    }
    return 1;
}
static int drflac__decode_samples__constant(drflac_bs* bs, uint32_t blockSize, uint32_t subframeBitsPerSample, int32_t* pDecodedSamplesS32, int64_t* pDecodedSamplesS64)
{
    uint32_t i;
    DR_FLAC_ASSERT(pDecodedSamplesS32 != NULL || pDecodedSamplesS64 != NULL);
    DR_FLAC_ASSERT(pDecodedSamplesS32 == NULL || pDecodedSamplesS64 == NULL);
    if (pDecodedSamplesS32 != NULL) {
        int32_t sample;
        if (!drflac__read_int32(bs, subframeBitsPerSample, &sample)) {
            return 0;
        }
        for (i = 0; i < blockSize; ++i) {
            pDecodedSamplesS32[i] = sample;
        }
    } else {
        int64_t sample;
        if (!drflac__read_int64(bs, subframeBitsPerSample, &sample)) {
            return 0;
        }
        for (i = 0; i < blockSize; ++i) {
            pDecodedSamplesS64[i] = sample;
        }
    }
    return 1;
}
static int drflac__decode_samples__verbatim(drflac_bs* bs, uint32_t blockSize, uint32_t subframeBitsPerSample, int32_t* pDecodedSamplesS32, int64_t* pDecodedSamplesS64)
{
    uint32_t i;
    DR_FLAC_ASSERT(pDecodedSamplesS32 != NULL || pDecodedSamplesS64 != NULL);
    DR_FLAC_ASSERT(pDecodedSamplesS32 == NULL || pDecodedSamplesS64 == NULL);
    if (pDecodedSamplesS32 != NULL) {
        for (i = 0; i < blockSize; ++i) {
            if (!drflac__read_int32(bs, subframeBitsPerSample, pDecodedSamplesS32 + i)) {
                return 0;
            }
        }
    } else {
        for (i = 0; i < blockSize; ++i) {
            if (!drflac__read_int64(bs, subframeBitsPerSample, pDecodedSamplesS64 + i)) {
                return 0;
            }
        }
    }
    return 1;
}
static int drflac__decode_samples__fixed(drflac_bs* bs, uint32_t blockSize, uint32_t subframeBitsPerSample, uint8_t lpcOrder, int32_t* pDecodedSamplesS32, int64_t* pDecodedSamplesS64)
{
    uint32_t i;
    static int32_t lpcCoefficientsTable[5][4] = {
        {0,  0, 0,  0},
        {1,  0, 0,  0},
        {2, -1, 0,  0},
        {3, -3, 1,  0},
        {4, -6, 4, -1}
    };
    DR_FLAC_ASSERT(pDecodedSamplesS32 != NULL || pDecodedSamplesS64 != NULL);
    DR_FLAC_ASSERT(pDecodedSamplesS32 == NULL || pDecodedSamplesS64 == NULL);
    if (pDecodedSamplesS32 != NULL) {
        for (i = 0; i < lpcOrder; ++i) {
            if (!drflac__read_int32(bs, subframeBitsPerSample, pDecodedSamplesS32 + i)) {
                return 0;
            }
        }
    } else {
        for (i = 0; i < lpcOrder; ++i) {
            if (!drflac__read_int64(bs, subframeBitsPerSample, pDecodedSamplesS64 + i)) {
                return 0;
            }
        }
    }
    if (!drflac__decode_samples_with_residual(bs, subframeBitsPerSample, blockSize, lpcOrder, 0, 4, lpcCoefficientsTable[lpcOrder], pDecodedSamplesS32, pDecodedSamplesS64)) {
        return 0;
    }
    return 1;
}
static int drflac__decode_samples__lpc(drflac_bs* bs, uint32_t blockSize, uint32_t bitsPerSample, uint8_t lpcOrder, int32_t* pDecodedSamplesS32, int64_t* pDecodedSamplesS64)
{
    uint8_t i;
    uint8_t lpcPrecision;
    int8_t lpcShift;
    int32_t coefficients[32];
    DR_FLAC_ASSERT(pDecodedSamplesS32 != NULL || pDecodedSamplesS64 != NULL);
    DR_FLAC_ASSERT(pDecodedSamplesS32 == NULL || pDecodedSamplesS64 == NULL);
    if (pDecodedSamplesS32 != NULL) {
        for (i = 0; i < lpcOrder; ++i) {
            if (!drflac__read_int32(bs, bitsPerSample, pDecodedSamplesS32 + i)) {
                return 0;
            }
        }
    } else {
        for (i = 0; i < lpcOrder; ++i) {
            if (!drflac__read_int64(bs, bitsPerSample, pDecodedSamplesS64 + i)) {
                return 0;
            }
        }
    }
    if (!drflac__read_uint8(bs, 4, &lpcPrecision)) {
        return 0;
    }
    if (lpcPrecision == 15) {
        return 0;
    }
    lpcPrecision += 1;
    if (!drflac__read_int8(bs, 5, &lpcShift)) {
        return 0;
    }
    if (lpcShift < 0) {
        return 0;
    }
    DR_FLAC_ZERO_MEMORY(coefficients, sizeof(coefficients));
    for (i = 0; i < lpcOrder; ++i) {
        if (!drflac__read_int32(bs, lpcPrecision, coefficients + i)) {
            return 0;
        }
    }
    if (!drflac__decode_samples_with_residual(bs, bitsPerSample, blockSize, lpcOrder, lpcShift, lpcPrecision, coefficients, pDecodedSamplesS32, pDecodedSamplesS64)) {
        return 0;
    }
    return 1;
}
static int drflac__read_next_flac_frame_header(drflac_bs* bs, uint8_t streaminfoBitsPerSample, drflac_frame_header* header)
{
    const uint32_t sampleRateTable[12]  = {0, 88200, 176400, 192000, 8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000};
    const uint8_t bitsPerSampleTable[8] = {0, 8, 12, (uint8_t)-1, 16, 20, 24, 32};
    DR_FLAC_ASSERT(bs != NULL);
    DR_FLAC_ASSERT(header != NULL);
    for (;;) {
        uint8_t crc8 = 0xCE;
        uint8_t reserved = 0;
        uint8_t blockingStrategy = 0;
        uint8_t blockSize = 0;
        uint8_t sampleRate = 0;
        uint8_t channelAssignment = 0;
        uint8_t bitsPerSample = 0;
        int isVariableBlockSize;
        if (!drflac__find_and_seek_to_next_sync_code(bs)) {
            return 0;
        }
        if (!drflac__read_uint8(bs, 1, &reserved)) {
            return 0;
        }
        if (reserved == 1) {
            continue;
        }
        crc8 = drflac_crc8(crc8, reserved, 1);
        if (!drflac__read_uint8(bs, 1, &blockingStrategy)) {
            return 0;
        }
        crc8 = drflac_crc8(crc8, blockingStrategy, 1);
        if (!drflac__read_uint8(bs, 4, &blockSize)) {
            return 0;
        }
        if (blockSize == 0) {
            continue;
        }
        crc8 = drflac_crc8(crc8, blockSize, 4);
        if (!drflac__read_uint8(bs, 4, &sampleRate)) {
            return 0;
        }
        crc8 = drflac_crc8(crc8, sampleRate, 4);
        if (!drflac__read_uint8(bs, 4, &channelAssignment)) {
            return 0;
        }
        if (channelAssignment > 10) {
            continue;
        }
        crc8 = drflac_crc8(crc8, channelAssignment, 4);
        if (!drflac__read_uint8(bs, 3, &bitsPerSample)) {
            return 0;
        }
        if (bitsPerSample == 3) {
            continue;
        }
        crc8 = drflac_crc8(crc8, bitsPerSample, 3);
        if (!drflac__read_uint8(bs, 1, &reserved)) {
            return 0;
        }
        if (reserved == 1) {
            continue;
        }
        crc8 = drflac_crc8(crc8, reserved, 1);
        isVariableBlockSize = blockingStrategy == 1;
        if (isVariableBlockSize) {
            uint64_t pcmFrameNumber;
            ma_result result = drflac__read_utf8_coded_number(bs, &pcmFrameNumber, &crc8);
            if (result != MA_SUCCESS) {
                if (result == MA_AT_END) {
                    return 0;
                } else {
                    continue;
                }
            }
            header->flacFrameNumber  = 0;
            header->pcmFrameNumber = pcmFrameNumber;
        } else {
            uint64_t flacFrameNumber = 0;
            ma_result result = drflac__read_utf8_coded_number(bs, &flacFrameNumber, &crc8);
            if (result != MA_SUCCESS) {
                if (result == MA_AT_END) {
                    return 0;
                } else {
                    continue;
                }
            }
            header->flacFrameNumber  = (uint32_t)flacFrameNumber;
            header->pcmFrameNumber = 0;
        }
        DR_FLAC_ASSERT(blockSize > 0);
        if (blockSize == 1) {
            header->blockSizeInPCMFrames = 192;
        } else if (blockSize <= 5) {
            DR_FLAC_ASSERT(blockSize >= 2);
            header->blockSizeInPCMFrames = 576 * (1 << (blockSize - 2));
        } else if (blockSize == 6) {
            if (!drflac__read_uint16(bs, 8, &header->blockSizeInPCMFrames)) {
                return 0;
            }
            crc8 = drflac_crc8(crc8, header->blockSizeInPCMFrames, 8);
            header->blockSizeInPCMFrames += 1;
        } else if (blockSize == 7) {
            if (!drflac__read_uint16(bs, 16, &header->blockSizeInPCMFrames)) {
                return 0;
            }
            crc8 = drflac_crc8(crc8, header->blockSizeInPCMFrames, 16);
            if (header->blockSizeInPCMFrames == 0xFFFF) {
                return 0;
            }
            header->blockSizeInPCMFrames += 1;
        } else {
            DR_FLAC_ASSERT(blockSize >= 8);
            header->blockSizeInPCMFrames = 256 * (1 << (blockSize - 8));
        }
        if (sampleRate <= 11) {
            header->sampleRate = sampleRateTable[sampleRate];
        } else if (sampleRate == 12) {
            if (!drflac__read_uint32(bs, 8, &header->sampleRate)) {
                return 0;
            }
            crc8 = drflac_crc8(crc8, header->sampleRate, 8);
            header->sampleRate *= 1000;
        } else if (sampleRate == 13) {
            if (!drflac__read_uint32(bs, 16, &header->sampleRate)) {
                return 0;
            }
            crc8 = drflac_crc8(crc8, header->sampleRate, 16);
        } else if (sampleRate == 14) {
            if (!drflac__read_uint32(bs, 16, &header->sampleRate)) {
                return 0;
            }
            crc8 = drflac_crc8(crc8, header->sampleRate, 16);
            header->sampleRate *= 10;
        } else {
            continue;
        }
        header->channelAssignment = channelAssignment;
        header->bitsPerSample = bitsPerSampleTable[bitsPerSample];
        if (header->bitsPerSample == 0) {
            header->bitsPerSample = streaminfoBitsPerSample;
        }
        if (streaminfoBitsPerSample != 0) {
            if (streaminfoBitsPerSample < 32) {
                if (header->bitsPerSample == 32) {
                    return 0;
                }
            } else {
                if (header->bitsPerSample != 32) {
                    return 0;
                }
            }
        }
        if (!drflac__read_uint8(bs, 8, &header->crc8)) {
            return 0;
        }
#ifndef DR_FLAC_NO_CRC
        if (header->crc8 != crc8) {
            continue;
        }
#endif
        return 1;
    }
}
static int drflac__read_subframe_header(drflac_bs* bs, drflac_subframe* pSubframe)
{
    uint8_t header;
    int type;
    if (!drflac__read_uint8(bs, 8, &header)) {
        return 0;
    }
    if ((header & 0x80) != 0) {
        return 0;
    }
    pSubframe->lpcOrder = 0;
    type = (header & 0x7E) >> 1;
    if (type == 0) {
        pSubframe->subframeType = DR_FLAC_SUBFRAME_CONSTANT;
    } else if (type == 1) {
        pSubframe->subframeType = DR_FLAC_SUBFRAME_VERBATIM;
    } else {
        if ((type & 0x20) != 0) {
            pSubframe->subframeType = DR_FLAC_SUBFRAME_LPC;
            pSubframe->lpcOrder = (uint8_t)(type & 0x1F) + 1;
        } else if ((type & 0x08) != 0) {
            pSubframe->subframeType = DR_FLAC_SUBFRAME_FIXED;
            pSubframe->lpcOrder = (uint8_t)(type & 0x07);
            if (pSubframe->lpcOrder > 4) {
                pSubframe->subframeType = DR_FLAC_SUBFRAME_RESERVED;
                pSubframe->lpcOrder = 0;
            }
        } else {
            pSubframe->subframeType = DR_FLAC_SUBFRAME_RESERVED;
        }
    }
    if (pSubframe->subframeType == DR_FLAC_SUBFRAME_RESERVED) {
        return 0;
    }
    pSubframe->wastedBitsPerSample = 0;
    if ((header & 0x01) == 1) {
        unsigned int wastedBitsPerSample;
        if (!drflac__seek_past_next_set_bit(bs, &wastedBitsPerSample)) {
            return 0;
        }
        pSubframe->wastedBitsPerSample = (uint8_t)wastedBitsPerSample + 1;
    }
    return 1;
}
static int drflac__decode_subframe(drflac_bs* bs, drflac_frame* frame, int subframeIndex, int32_t* pDecodedSamplesOutS32, int64_t* pDecodedSamplesOutS64)
{
    drflac_subframe* pSubframe;
    uint32_t subframeBitsPerSample;
    int decodeResult;
    DR_FLAC_ASSERT(bs != NULL);
    DR_FLAC_ASSERT(frame != NULL);
    DR_FLAC_ASSERT(pDecodedSamplesOutS32 != NULL || pDecodedSamplesOutS64 != NULL);
    DR_FLAC_ASSERT(pDecodedSamplesOutS32 == NULL || pDecodedSamplesOutS64 == NULL);
    pSubframe = frame->subframes + subframeIndex;
    if (!drflac__read_subframe_header(bs, pSubframe)) {
        return 0;
    }
    subframeBitsPerSample = frame->header.bitsPerSample;
    if ((frame->header.channelAssignment == DR_FLAC_CHANNEL_ASSIGNMENT_LEFT_SIDE || frame->header.channelAssignment == DR_FLAC_CHANNEL_ASSIGNMENT_MID_SIDE) && subframeIndex == 1) {
        subframeBitsPerSample += 1;
    } else if (frame->header.channelAssignment == DR_FLAC_CHANNEL_ASSIGNMENT_RIGHT_SIDE && subframeIndex == 0) {
        subframeBitsPerSample += 1;
    }
    if (pSubframe->wastedBitsPerSample >= subframeBitsPerSample) {
        return 0;
    }
    subframeBitsPerSample -= pSubframe->wastedBitsPerSample;
    if (pDecodedSamplesOutS32 != NULL) {
        if (subframeBitsPerSample > 32) {
            return 0;
        }
    } else {
        if (subframeBitsPerSample > 33) {
            return 0;
        }
    }
    if (frame->header.blockSizeInPCMFrames < pSubframe->lpcOrder) {
        return 0;
    }
    switch (pSubframe->subframeType)
    {
        case DR_FLAC_SUBFRAME_CONSTANT:
        {
            decodeResult = drflac__decode_samples__constant(bs, frame->header.blockSizeInPCMFrames, subframeBitsPerSample, pDecodedSamplesOutS32, pDecodedSamplesOutS64);
        } break;
        case DR_FLAC_SUBFRAME_VERBATIM:
        {
            decodeResult = drflac__decode_samples__verbatim(bs, frame->header.blockSizeInPCMFrames, subframeBitsPerSample, pDecodedSamplesOutS32, pDecodedSamplesOutS64);
        } break;
        case DR_FLAC_SUBFRAME_FIXED:
        {
            decodeResult = drflac__decode_samples__fixed(bs, frame->header.blockSizeInPCMFrames, subframeBitsPerSample, pSubframe->lpcOrder, pDecodedSamplesOutS32, pDecodedSamplesOutS64);
        } break;
        case DR_FLAC_SUBFRAME_LPC:
        {
            decodeResult = drflac__decode_samples__lpc(bs, frame->header.blockSizeInPCMFrames, subframeBitsPerSample, pSubframe->lpcOrder, pDecodedSamplesOutS32, pDecodedSamplesOutS64);
        } break;
        default: decodeResult = 0;
    }
    if (!decodeResult) {
        return 0;
    }
    if (pDecodedSamplesOutS32 != NULL) {
        pSubframe->samples.pS32 = pDecodedSamplesOutS32;
    } else {
        pSubframe->samples.pS64 = pDecodedSamplesOutS64;
    }
    return 1;
}
static int drflac__seek_subframe(drflac_bs* bs, drflac_frame* frame, int subframeIndex)
{
    drflac_subframe* pSubframe;
    uint32_t subframeBitsPerSample;
    DR_FLAC_ASSERT(bs != NULL);
    DR_FLAC_ASSERT(frame != NULL);
    pSubframe = frame->subframes + subframeIndex;
    if (!drflac__read_subframe_header(bs, pSubframe)) {
        return 0;
    }
    subframeBitsPerSample = frame->header.bitsPerSample;
    if ((frame->header.channelAssignment == DR_FLAC_CHANNEL_ASSIGNMENT_LEFT_SIDE || frame->header.channelAssignment == DR_FLAC_CHANNEL_ASSIGNMENT_MID_SIDE) && subframeIndex == 1) {
        subframeBitsPerSample += 1;
    } else if (frame->header.channelAssignment == DR_FLAC_CHANNEL_ASSIGNMENT_RIGHT_SIDE && subframeIndex == 0) {
        subframeBitsPerSample += 1;
    }
    if (pSubframe->wastedBitsPerSample >= subframeBitsPerSample) {
        return 0;
    }
    subframeBitsPerSample -= pSubframe->wastedBitsPerSample;
    DR_FLAC_ZERO_MEMORY(&pSubframe->samples, sizeof(pSubframe->samples));
    switch (pSubframe->subframeType)
    {
        case DR_FLAC_SUBFRAME_CONSTANT:
        {
            if (!drflac__seek_bits(bs, subframeBitsPerSample)) {
                return 0;
            }
        } break;
        case DR_FLAC_SUBFRAME_VERBATIM:
        {
            unsigned int bitsToSeek = frame->header.blockSizeInPCMFrames * subframeBitsPerSample;
            if (!drflac__seek_bits(bs, bitsToSeek)) {
                return 0;
            }
        } break;
        case DR_FLAC_SUBFRAME_FIXED:
        {
            unsigned int bitsToSeek = pSubframe->lpcOrder * subframeBitsPerSample;
            if (!drflac__seek_bits(bs, bitsToSeek)) {
                return 0;
            }
            if (!drflac__read_and_seek_residual(bs, frame->header.blockSizeInPCMFrames, pSubframe->lpcOrder)) {
                return 0;
            }
        } break;
        case DR_FLAC_SUBFRAME_LPC:
        {
            uint8_t lpcPrecision;
            unsigned int bitsToSeek = pSubframe->lpcOrder * subframeBitsPerSample;
            if (!drflac__seek_bits(bs, bitsToSeek)) {
                return 0;
            }
            if (!drflac__read_uint8(bs, 4, &lpcPrecision)) {
                return 0;
            }
            if (lpcPrecision == 15) {
                return 0;
            }
            lpcPrecision += 1;
            bitsToSeek = (pSubframe->lpcOrder * lpcPrecision) + 5;
            if (!drflac__seek_bits(bs, bitsToSeek)) {
                return 0;
            }
            if (!drflac__read_and_seek_residual(bs, frame->header.blockSizeInPCMFrames, pSubframe->lpcOrder)) {
                return 0;
            }
        } break;
        default: return 0;
    }
    return 1;
}
static DRFLAC_INLINE uint8_t drflac__get_channel_count_from_channel_assignment(int8_t channelAssignment)
{
    uint8_t lookup[] = {1, 2, 3, 4, 5, 6, 7, 8, 2, 2, 2};
    DR_FLAC_ASSERT(channelAssignment <= 10);
    return lookup[channelAssignment];
}
static int drflac__resize_decoded_samples(drflac* pFlac, uint16_t blockSizeInPCMFrames);
static ma_result drflac__decode_flac_frame(drflac* pFlac)
{
    int channelCount;
    int i;
    uint8_t paddingSizeInBits;
    uint16_t desiredCRC16;
#ifndef DR_FLAC_NO_CRC
    uint16_t actualCRC16;
#endif
    pFlac->currentFLACFrame.pcmFramesRemaining = 0;
    DR_FLAC_ZERO_MEMORY(pFlac->currentFLACFrame.subframes, sizeof(pFlac->currentFLACFrame.subframes));
    channelCount = drflac__get_channel_count_from_channel_assignment(pFlac->currentFLACFrame.header.channelAssignment);
    if (channelCount != (int)pFlac->channels) {
        return MA_ERROR;
    }
    if (pFlac->currentFLACFrame.header.blockSizeInPCMFrames > pFlac->decodedSamplesCapacityInPCMFrames) {
        if (!drflac__resize_decoded_samples(pFlac, pFlac->currentFLACFrame.header.blockSizeInPCMFrames)) {
            return MA_OUT_OF_MEMORY;
        }
    }
    if (pFlac->bitsPerSample != 32) {
        for (i = 0; i < channelCount; ++i) {
            if (!drflac__decode_subframe(&pFlac->bs, &pFlac->currentFLACFrame, i, pFlac->decodedSamples.pS32 + (pFlac->currentFLACFrame.header.blockSizeInPCMFrames * i), NULL)) {
                return MA_ERROR;
            }
        }
    } else {
        for (i = 0; i < channelCount; ++i) {
            if (!drflac__decode_subframe(&pFlac->bs, &pFlac->currentFLACFrame, i, NULL, pFlac->decodedSamples.pS64 + (pFlac->currentFLACFrame.header.blockSizeInPCMFrames * i))) {
                return MA_ERROR;
            }
        }
    }
    paddingSizeInBits = (uint8_t)(DR_FLAC_CACHE_L1_BITS_REMAINING(&pFlac->bs) & 7);
    if (paddingSizeInBits > 0) {
        uint8_t padding = 0;
        if (!drflac__read_uint8(&pFlac->bs, paddingSizeInBits, &padding)) {
            return MA_AT_END;
        }
    }
#ifndef DR_FLAC_NO_CRC
    actualCRC16 = drflac__flush_crc16(&pFlac->bs);
#endif
    if (!drflac__read_uint16(&pFlac->bs, 16, &desiredCRC16)) {
        return MA_AT_END;
    }
#ifndef DR_FLAC_NO_CRC
    if (actualCRC16 != desiredCRC16) {
        return MA_CRC_MISMATCH;
    }
#endif
    pFlac->currentFLACFrame.pcmFramesRemaining = pFlac->currentFLACFrame.header.blockSizeInPCMFrames;
    return MA_SUCCESS;
}
static ma_result drflac__seek_flac_frame(drflac* pFlac)
{
    ma_result result;
    int channelCount;
    int i;
    uint16_t desiredCRC16;
#ifndef DR_FLAC_NO_CRC
    uint16_t actualCRC16;
#endif
    pFlac->currentFLACFrame.pcmFramesRemaining = 0;
    channelCount = drflac__get_channel_count_from_channel_assignment(pFlac->currentFLACFrame.header.channelAssignment);
    for (i = 0; i < channelCount; ++i) {
        if (!drflac__seek_subframe(&pFlac->bs, &pFlac->currentFLACFrame, i)) {
            result = MA_ERROR;
            goto error;
        }
    }
    if (!drflac__seek_bits(&pFlac->bs, DR_FLAC_CACHE_L1_BITS_REMAINING(&pFlac->bs) & 7)) {
        result = MA_ERROR;
        goto error;
    }
#ifndef DR_FLAC_NO_CRC
    actualCRC16 = drflac__flush_crc16(&pFlac->bs);
#endif
    if (!drflac__read_uint16(&pFlac->bs, 16, &desiredCRC16)) {
        result = MA_AT_END;
        goto error;
    }
#ifndef DR_FLAC_NO_CRC
    if (actualCRC16 != desiredCRC16) {
        result = MA_CRC_MISMATCH;
        goto error;
    }
#endif
    return MA_SUCCESS;
error:
    DR_FLAC_ZERO_MEMORY(pFlac->currentFLACFrame.subframes, sizeof(pFlac->currentFLACFrame.subframes));
    return result;
}
static int drflac__read_and_decode_next_flac_frame(drflac* pFlac)
{
    DR_FLAC_ASSERT(pFlac != NULL);
    for (;;) {
        ma_result result;
        if (!drflac__read_next_flac_frame_header(&pFlac->bs, pFlac->bitsPerSample, &pFlac->currentFLACFrame.header)) {
            DR_FLAC_ZERO_MEMORY(pFlac->currentFLACFrame.subframes, sizeof(pFlac->currentFLACFrame.subframes));
            return 0;
        }
        result = drflac__decode_flac_frame(pFlac);
        if (result != MA_SUCCESS) {
            DR_FLAC_ZERO_MEMORY(pFlac->currentFLACFrame.subframes, sizeof(pFlac->currentFLACFrame.subframes));
            if (result == MA_CRC_MISMATCH) {
                continue;
            } else {
                return 0;
            }
        }
        return 1;
    }
}
static void drflac__get_pcm_frame_range_of_current_flac_frame(drflac* pFlac, uint64_t* pFirstPCMFrame, uint64_t* pLastPCMFrame)
{
    uint64_t firstPCMFrame;
    uint64_t lastPCMFrame;
    DR_FLAC_ASSERT(pFlac != NULL);
    firstPCMFrame = pFlac->currentFLACFrame.header.pcmFrameNumber;
    if (firstPCMFrame == 0) {
        firstPCMFrame = ((uint64_t)pFlac->currentFLACFrame.header.flacFrameNumber) * pFlac->maxBlockSizeInPCMFrames;
    }
    lastPCMFrame = firstPCMFrame + pFlac->currentFLACFrame.header.blockSizeInPCMFrames;
    if (lastPCMFrame > 0) {
        lastPCMFrame -= 1;
    }
    if (pFirstPCMFrame) {
        *pFirstPCMFrame = firstPCMFrame;
    }
    if (pLastPCMFrame) {
        *pLastPCMFrame = lastPCMFrame;
    }
}
static int drflac__seek_to_first_frame(drflac* pFlac)
{
    int result;
    DR_FLAC_ASSERT(pFlac != NULL);
    result = drflac__seek_to_byte(&pFlac->bs, pFlac->firstFLACFramePosInBytes);
    DR_FLAC_ZERO_MEMORY(&pFlac->currentFLACFrame, sizeof(pFlac->currentFLACFrame));
    pFlac->currentPCMFrame = 0;
    return result;
}
static DRFLAC_INLINE ma_result drflac__seek_to_next_flac_frame(drflac* pFlac)
{
    DR_FLAC_ASSERT(pFlac != NULL);
    return drflac__seek_flac_frame(pFlac);
}
static uint64_t drflac__seek_forward_by_pcm_frames(drflac* pFlac, uint64_t pcmFramesToSeek)
{
    uint64_t pcmFramesRead = 0;
    while (pcmFramesToSeek > 0) {
        if (pFlac->currentFLACFrame.pcmFramesRemaining == 0) {
            if (!drflac__read_and_decode_next_flac_frame(pFlac)) {
                break;
            }
        } else {
            if (pFlac->currentFLACFrame.pcmFramesRemaining > pcmFramesToSeek) {
                pcmFramesRead   += pcmFramesToSeek;
                pFlac->currentFLACFrame.pcmFramesRemaining -= (uint32_t)pcmFramesToSeek;
                pcmFramesToSeek  = 0;
            } else {
                pcmFramesRead   += pFlac->currentFLACFrame.pcmFramesRemaining;
                pcmFramesToSeek -= pFlac->currentFLACFrame.pcmFramesRemaining;
                pFlac->currentFLACFrame.pcmFramesRemaining = 0;
            }
        }
    }
    pFlac->currentPCMFrame += pcmFramesRead;
    return pcmFramesRead;
}
static int drflac__seek_to_pcm_frame__brute_force(drflac* pFlac, uint64_t pcmFrameIndex)
{
    int isMidFrame = 0;
    uint64_t runningPCMFrameCount;
    DR_FLAC_ASSERT(pFlac != NULL);
    if (pcmFrameIndex >= pFlac->currentPCMFrame) {
        runningPCMFrameCount = pFlac->currentPCMFrame;
        if (pFlac->currentPCMFrame == 0 && pFlac->currentFLACFrame.pcmFramesRemaining == 0) {
            if (!drflac__read_next_flac_frame_header(&pFlac->bs, pFlac->bitsPerSample, &pFlac->currentFLACFrame.header)) {
                return 0;
            }
        } else {
            isMidFrame = 1;
        }
    } else {
        runningPCMFrameCount = 0;
        if (!drflac__seek_to_first_frame(pFlac)) {
            return 0;
        }
        if (!drflac__read_next_flac_frame_header(&pFlac->bs, pFlac->bitsPerSample, &pFlac->currentFLACFrame.header)) {
            return 0;
        }
    }
    for (;;) {
        uint64_t pcmFrameCountInThisFLACFrame;
        uint64_t firstPCMFrameInFLACFrame = 0;
        uint64_t lastPCMFrameInFLACFrame = 0;
        drflac__get_pcm_frame_range_of_current_flac_frame(pFlac, &firstPCMFrameInFLACFrame, &lastPCMFrameInFLACFrame);
        pcmFrameCountInThisFLACFrame = (lastPCMFrameInFLACFrame - firstPCMFrameInFLACFrame) + 1;
        if (pcmFrameIndex < (runningPCMFrameCount + pcmFrameCountInThisFLACFrame)) {
            uint64_t pcmFramesToDecode = pcmFrameIndex - runningPCMFrameCount;
            if (!isMidFrame) {
                ma_result result = drflac__decode_flac_frame(pFlac);
                if (result == MA_SUCCESS) {
                    return drflac__seek_forward_by_pcm_frames(pFlac, pcmFramesToDecode) == pcmFramesToDecode;
                } else {
                    if (result == MA_CRC_MISMATCH) {
                        goto next_iteration;
                    } else {
                        return 0;
                    }
                }
            } else {
                return drflac__seek_forward_by_pcm_frames(pFlac, pcmFramesToDecode) == pcmFramesToDecode;
            }
        } else {
            if (!isMidFrame) {
                ma_result result = drflac__seek_to_next_flac_frame(pFlac);
                if (result == MA_SUCCESS) {
                    runningPCMFrameCount += pcmFrameCountInThisFLACFrame;
                } else {
                    if (result == MA_CRC_MISMATCH) {
                        goto next_iteration;
                    } else {
                        return 0;
                    }
                }
            } else {
                runningPCMFrameCount += pFlac->currentFLACFrame.pcmFramesRemaining;
                pFlac->currentFLACFrame.pcmFramesRemaining = 0;
                isMidFrame = 0;
            }
            if (runningPCMFrameCount == pcmFrameIndex) {
                return 1;
            }
        }
    next_iteration:
        if (!drflac__read_next_flac_frame_header(&pFlac->bs, pFlac->bitsPerSample, &pFlac->currentFLACFrame.header)) {
            return 0;
        }
    }
}
#if !defined(DR_FLAC_NO_CRC)
#define DR_FLAC_BINARY_SEARCH_APPROX_COMPRESSION_RATIO 0.6f
static int drflac__read_and_decode_next_flac_frame_for_seek(drflac* pFlac)
{
    DR_FLAC_ASSERT(pFlac != NULL);
    for (;;) {
        ma_result result;
        if (!drflac__read_next_flac_frame_header(&pFlac->bs, pFlac->bitsPerSample, &pFlac->currentFLACFrame.header)) {
            return 0;
        }
        result = drflac__decode_flac_frame(pFlac);
        if (result == MA_SUCCESS) {
            return 1;
        }
        if (result == MA_AT_END) {
            return 0;
        }
    }
}
static int drflac__seek_to_approximate_flac_frame_to_byte(drflac* pFlac, uint64_t targetByte, uint64_t rangeLo, uint64_t rangeHi, uint64_t* pLastSuccessfulSeekOffset)
{
    DR_FLAC_ASSERT(pFlac != NULL);
    DR_FLAC_ASSERT(pLastSuccessfulSeekOffset != NULL);
    DR_FLAC_ASSERT(targetByte >= rangeLo);
    DR_FLAC_ASSERT(targetByte <= rangeHi);
    *pLastSuccessfulSeekOffset = pFlac->firstFLACFramePosInBytes;
    for (;;) {
        if (!drflac__seek_to_byte(&pFlac->bs, targetByte)) {
            if (targetByte == 0) {
                drflac__seek_to_first_frame(pFlac);
                return 0;
            }
            rangeHi = targetByte;
            if (rangeHi <= rangeLo) {
                return 0;
            }
            targetByte = rangeLo + ((rangeHi - rangeLo)/2);
        } else {
            DR_FLAC_ZERO_MEMORY(&pFlac->currentFLACFrame, sizeof(pFlac->currentFLACFrame));
            #if 1
            {
                if (!drflac__read_and_decode_next_flac_frame_for_seek(pFlac)) {
                    rangeHi = targetByte;
                    if (rangeHi <= rangeLo) {
                        return 0;
                    }
                    targetByte = rangeLo + ((rangeHi - rangeLo)/2);
                } else {
                    break;
                }
            }
            #else
            {
                if (!drflac__read_next_flac_frame_header(&pFlac->bs, pFlac->bitsPerSample, &pFlac->currentFLACFrame.header)) {
                    targetByte = rangeLo + ((rangeHi - rangeLo)/2);
                    rangeHi = targetByte;
                } else {
                    break;
                }
            }
            #endif
        }
    }
    drflac__get_pcm_frame_range_of_current_flac_frame(pFlac, &pFlac->currentPCMFrame, NULL);
    DR_FLAC_ASSERT(targetByte <= rangeHi);
    *pLastSuccessfulSeekOffset = targetByte;
    return 1;
}
static int drflac__decode_flac_frame_and_seek_forward_by_pcm_frames(drflac* pFlac, uint64_t offset)
{
#if 0
    if (drflac__decode_flac_frame(pFlac) != MA_SUCCESS) {
        if (drflac__read_and_decode_next_flac_frame(pFlac) == 0) {
            return 0;
        }
    }
#endif
    return drflac__seek_forward_by_pcm_frames(pFlac, offset) == offset;
}
static int drflac__seek_to_pcm_frame__binary_search_internal(drflac* pFlac, uint64_t pcmFrameIndex, uint64_t byteRangeLo, uint64_t byteRangeHi)
{
    uint64_t targetByte;
    uint64_t pcmRangeLo = pFlac->totalPCMFrameCount;
    uint64_t pcmRangeHi = 0;
    uint64_t lastSuccessfulSeekOffset = (uint64_t)-1;
    uint64_t closestSeekOffsetBeforeTargetPCMFrame = byteRangeLo;
    uint32_t seekForwardThreshold = (pFlac->maxBlockSizeInPCMFrames != 0) ? pFlac->maxBlockSizeInPCMFrames*2 : 4096;
    if (byteRangeLo > byteRangeHi || pFlac->currentPCMFrame > pcmFrameIndex) {
        return 0;
    }
    targetByte = (uint64_t)(((int64_t)((pcmFrameIndex - pFlac->currentPCMFrame) * pFlac->channels * pFlac->bitsPerSample)/8.0f) * DR_FLAC_BINARY_SEARCH_APPROX_COMPRESSION_RATIO);
    if (targetByte > byteRangeHi - byteRangeLo) {
        targetByte = byteRangeHi;
    } else {
        targetByte += byteRangeLo;
    }
    for (;;) {
        uint64_t previousByteRangeLo;
        uint64_t previousByteRangeHi;
        previousByteRangeLo = byteRangeLo;
        previousByteRangeHi = byteRangeHi;
        if ((byteRangeHi - byteRangeLo) <= 1) {
            if (!drflac__seek_to_approximate_flac_frame_to_byte(pFlac, closestSeekOffsetBeforeTargetPCMFrame, closestSeekOffsetBeforeTargetPCMFrame, byteRangeHi, &lastSuccessfulSeekOffset)) {
                break;
            }
            if (pFlac->currentPCMFrame <= pcmFrameIndex && drflac__decode_flac_frame_and_seek_forward_by_pcm_frames(pFlac, pcmFrameIndex - pFlac->currentPCMFrame)) {
                return 1;
            }
            break;
        }
        if (drflac__seek_to_approximate_flac_frame_to_byte(pFlac, targetByte, byteRangeLo, byteRangeHi, &lastSuccessfulSeekOffset)) {
            uint64_t newPCMRangeLo;
            uint64_t newPCMRangeHi;
            drflac__get_pcm_frame_range_of_current_flac_frame(pFlac, &newPCMRangeLo, &newPCMRangeHi);
            if (pcmRangeLo == newPCMRangeLo) {
                if (!drflac__seek_to_approximate_flac_frame_to_byte(pFlac, closestSeekOffsetBeforeTargetPCMFrame, closestSeekOffsetBeforeTargetPCMFrame, byteRangeHi, &lastSuccessfulSeekOffset)) {
                    break;
                }
                if (drflac__decode_flac_frame_and_seek_forward_by_pcm_frames(pFlac, pcmFrameIndex - pFlac->currentPCMFrame)) {
                    return 1;
                } else {
                    break;
                }
            }
            pcmRangeLo = newPCMRangeLo;
            pcmRangeHi = newPCMRangeHi;
            if (pcmRangeLo <= pcmFrameIndex && pcmRangeHi >= pcmFrameIndex) {
                if (drflac__decode_flac_frame_and_seek_forward_by_pcm_frames(pFlac, pcmFrameIndex - pFlac->currentPCMFrame) ) {
                    return 1;
                } else {
                    break;
                }
            } else {
                if (pcmRangeLo > pcmFrameIndex) {
                    byteRangeHi = lastSuccessfulSeekOffset;
                    if (byteRangeLo > byteRangeHi) {
                        byteRangeLo = byteRangeHi;
                    }
                    targetByte = byteRangeLo + ((byteRangeHi - byteRangeLo) / 2);
                    if (targetByte < byteRangeLo) {
                        targetByte = byteRangeLo;
                    }
                } else  {
                    if ((pcmFrameIndex - pcmRangeLo) < seekForwardThreshold) {
                        if (drflac__decode_flac_frame_and_seek_forward_by_pcm_frames(pFlac, pcmFrameIndex - pFlac->currentPCMFrame)) {
                            return 1;
                        } else {
                            break;
                        }
                    } else {
                        const double approxCompressionRatio = (int64_t)(lastSuccessfulSeekOffset - pFlac->firstFLACFramePosInBytes) / ((int64_t)(pcmRangeLo * pFlac->channels * pFlac->bitsPerSample)/8.0);
                        byteRangeLo = lastSuccessfulSeekOffset;
                        if (byteRangeHi < byteRangeLo) {
                            byteRangeHi = byteRangeLo;
                        }
                        targetByte = lastSuccessfulSeekOffset + (uint64_t)(((int64_t)((pcmFrameIndex-pcmRangeLo) * pFlac->channels * pFlac->bitsPerSample)/8.0) * approxCompressionRatio);
                        if (targetByte > byteRangeHi) {
                            targetByte = byteRangeHi;
                        }
                        if (closestSeekOffsetBeforeTargetPCMFrame < lastSuccessfulSeekOffset) {
                            closestSeekOffsetBeforeTargetPCMFrame = lastSuccessfulSeekOffset;
                        }
                    }
                }
            }
            if (byteRangeLo == previousByteRangeLo && byteRangeHi == previousByteRangeHi) {
                if (!drflac__seek_to_approximate_flac_frame_to_byte(pFlac, closestSeekOffsetBeforeTargetPCMFrame, closestSeekOffsetBeforeTargetPCMFrame, byteRangeHi, &lastSuccessfulSeekOffset)) {
                    break;
                }
                if (pFlac->currentPCMFrame <= pcmFrameIndex && drflac__decode_flac_frame_and_seek_forward_by_pcm_frames(pFlac, pcmFrameIndex - pFlac->currentPCMFrame)) {
                    return 1;
                }
                break;
            }
            if ((byteRangeHi - byteRangeLo) > 1 && (targetByte <= byteRangeLo || targetByte >= byteRangeHi)) {
                targetByte = byteRangeLo + ((byteRangeHi - byteRangeLo) / 2);
            }
        } else {
            break;
        }
    }
    drflac__seek_to_first_frame(pFlac);
    return 0;
}
static int drflac__seek_to_pcm_frame__binary_search(drflac* pFlac, uint64_t pcmFrameIndex)
{
    uint64_t byteRangeLo;
    uint64_t byteRangeHi;
    uint32_t seekForwardThreshold = (pFlac->maxBlockSizeInPCMFrames != 0) ? pFlac->maxBlockSizeInPCMFrames*2 : 4096;
    if (drflac__seek_to_first_frame(pFlac) == 0) {
        return 0;
    }
    if (pcmFrameIndex < seekForwardThreshold) {
        return drflac__seek_forward_by_pcm_frames(pFlac, pcmFrameIndex) == pcmFrameIndex;
    }
    byteRangeLo = pFlac->firstFLACFramePosInBytes;
    byteRangeHi = pFlac->firstFLACFramePosInBytes + (uint64_t)((int64_t)(pFlac->totalPCMFrameCount * pFlac->channels * pFlac->bitsPerSample)/8.0f);
    return drflac__seek_to_pcm_frame__binary_search_internal(pFlac, pcmFrameIndex, byteRangeLo, byteRangeHi);
}
#endif
static int drflac__seek_to_pcm_frame__seek_table(drflac* pFlac, uint64_t pcmFrameIndex)
{
    uint32_t iClosestSeekpoint = 0;
    int isMidFrame = 0;
    uint64_t runningPCMFrameCount;
    uint32_t iSeekpoint;
    DR_FLAC_ASSERT(pFlac != NULL);
    if (pFlac->pSeekpoints == NULL || pFlac->seekpointCount == 0) {
        return 0;
    }
    if (pFlac->pSeekpoints[0].firstPCMFrame > pcmFrameIndex) {
        return 0;
    }
    for (iSeekpoint = 0; iSeekpoint < pFlac->seekpointCount; ++iSeekpoint) {
        if (pFlac->pSeekpoints[iSeekpoint].firstPCMFrame >= pcmFrameIndex) {
            break;
        }
        iClosestSeekpoint = iSeekpoint;
    }
    if (pFlac->pSeekpoints[iClosestSeekpoint].pcmFrameCount == 0 || pFlac->pSeekpoints[iClosestSeekpoint].pcmFrameCount > pFlac->maxBlockSizeInPCMFrames) {
        return 0;
    }
    if (pFlac->pSeekpoints[iClosestSeekpoint].firstPCMFrame > pFlac->totalPCMFrameCount && pFlac->totalPCMFrameCount > 0) {
        return 0;
    }
    if (pFlac->pSeekpoints[iClosestSeekpoint].flacFrameOffset > (uint64_t)-1 - pFlac->firstFLACFramePosInBytes) {
        return 0;
    }
#if !defined(DR_FLAC_NO_CRC)
    if (pFlac->totalPCMFrameCount > 0) {
        uint64_t byteRangeLo;
        uint64_t byteRangeHi;
        byteRangeHi = pFlac->firstFLACFramePosInBytes + (uint64_t)((int64_t)(pFlac->totalPCMFrameCount * pFlac->channels * pFlac->bitsPerSample)/8.0f);
        byteRangeLo = pFlac->firstFLACFramePosInBytes + pFlac->pSeekpoints[iClosestSeekpoint].flacFrameOffset;
        if (iClosestSeekpoint < pFlac->seekpointCount-1) {
            uint32_t iNextSeekpoint = iClosestSeekpoint + 1;
            if (pFlac->pSeekpoints[iNextSeekpoint].firstPCMFrame != (((uint64_t)0xFFFFFFFF << 32) | 0xFFFFFFFF)) {
                if (pFlac->pSeekpoints[iClosestSeekpoint].flacFrameOffset >= pFlac->pSeekpoints[iNextSeekpoint].flacFrameOffset || pFlac->pSeekpoints[iNextSeekpoint].pcmFrameCount == 0) {
                    return 0;
                }
                if (pFlac->pSeekpoints[iNextSeekpoint].flacFrameOffset > (uint64_t)-1 - pFlac->firstFLACFramePosInBytes) {
                    return 0;
                }
                byteRangeHi = pFlac->firstFLACFramePosInBytes + pFlac->pSeekpoints[iNextSeekpoint].flacFrameOffset - 1;
            }
        }
        if (byteRangeLo > byteRangeHi) {
            return 0;
        }
        if (drflac__seek_to_byte(&pFlac->bs, pFlac->firstFLACFramePosInBytes + pFlac->pSeekpoints[iClosestSeekpoint].flacFrameOffset)) {
            DR_FLAC_ZERO_MEMORY(&pFlac->currentFLACFrame, sizeof(pFlac->currentFLACFrame));
            if (drflac__read_next_flac_frame_header(&pFlac->bs, pFlac->bitsPerSample, &pFlac->currentFLACFrame.header)) {
                drflac__get_pcm_frame_range_of_current_flac_frame(pFlac, &pFlac->currentPCMFrame, NULL);
                if (pFlac->currentPCMFrame > pcmFrameIndex) {
                    return 0;
                }
                if (drflac__seek_to_pcm_frame__binary_search_internal(pFlac, pcmFrameIndex, byteRangeLo, byteRangeHi)) {
                    return 1;
                }
            }
        }
    }
#endif
    if (pcmFrameIndex >= pFlac->currentPCMFrame && pFlac->pSeekpoints[iClosestSeekpoint].firstPCMFrame <= pFlac->currentPCMFrame) {
        runningPCMFrameCount = pFlac->currentPCMFrame;
        if (pFlac->currentPCMFrame == 0 && pFlac->currentFLACFrame.pcmFramesRemaining == 0) {
            if (!drflac__read_next_flac_frame_header(&pFlac->bs, pFlac->bitsPerSample, &pFlac->currentFLACFrame.header)) {
                return 0;
            }
        } else {
            isMidFrame = 1;
        }
    } else {
        runningPCMFrameCount = pFlac->pSeekpoints[iClosestSeekpoint].firstPCMFrame;
        if (!drflac__seek_to_byte(&pFlac->bs, pFlac->firstFLACFramePosInBytes + pFlac->pSeekpoints[iClosestSeekpoint].flacFrameOffset)) {
            return 0;
        }
        if (!drflac__read_next_flac_frame_header(&pFlac->bs, pFlac->bitsPerSample, &pFlac->currentFLACFrame.header)) {
            return 0;
        }
    }
    for (;;) {
        uint64_t pcmFrameCountInThisFLACFrame;
        uint64_t firstPCMFrameInFLACFrame = 0;
        uint64_t lastPCMFrameInFLACFrame = 0;
        drflac__get_pcm_frame_range_of_current_flac_frame(pFlac, &firstPCMFrameInFLACFrame, &lastPCMFrameInFLACFrame);
        pcmFrameCountInThisFLACFrame = (lastPCMFrameInFLACFrame - firstPCMFrameInFLACFrame) + 1;
        if (pcmFrameIndex < (runningPCMFrameCount + pcmFrameCountInThisFLACFrame)) {
            uint64_t pcmFramesToDecode = pcmFrameIndex - runningPCMFrameCount;
            if (!isMidFrame) {
                ma_result result = drflac__decode_flac_frame(pFlac);
                if (result == MA_SUCCESS) {
                    return drflac__seek_forward_by_pcm_frames(pFlac, pcmFramesToDecode) == pcmFramesToDecode;
                } else {
                    if (result == MA_CRC_MISMATCH) {
                        goto next_iteration;
                    } else {
                        return 0;
                    }
                }
            } else {
                return drflac__seek_forward_by_pcm_frames(pFlac, pcmFramesToDecode) == pcmFramesToDecode;
            }
        } else {
            if (!isMidFrame) {
                ma_result result = drflac__seek_to_next_flac_frame(pFlac);
                if (result == MA_SUCCESS) {
                    runningPCMFrameCount += pcmFrameCountInThisFLACFrame;
                } else {
                    if (result == MA_CRC_MISMATCH) {
                        goto next_iteration;
                    } else {
                        return 0;
                    }
                }
            } else {
                runningPCMFrameCount += pFlac->currentFLACFrame.pcmFramesRemaining;
                pFlac->currentFLACFrame.pcmFramesRemaining = 0;
                isMidFrame = 0;
            }
            if (runningPCMFrameCount == pcmFrameIndex) {
                return 1;
            }
        }
    next_iteration:
        if (!drflac__read_next_flac_frame_header(&pFlac->bs, pFlac->bitsPerSample, &pFlac->currentFLACFrame.header)) {
            return 0;
        }
    }
}
#ifndef DR_FLAC_NO_OGG
typedef struct
{
    uint8_t capturePattern[4];
    uint8_t structureVersion;
    uint8_t headerType;
    uint64_t granulePosition;
    uint32_t serialNumber;
    uint32_t sequenceNumber;
    uint32_t checksum;
    uint8_t segmentCount;
    uint8_t segmentTable[255];
} drflac_ogg_page_header;
#endif
typedef struct
{
    drflac_read_proc onRead;
    drflac_seek_proc onSeek;
    drflac_tell_proc onTell;
    drflac_meta_proc onMeta;
    drflac_container container;
    void* pUserData;
    void* pUserDataMD;
    uint32_t sampleRate;
    uint8_t  channels;
    uint8_t  bitsPerSample;
    uint64_t totalPCMFrameCount;
    uint16_t maxBlockSizeInPCMFrames;
    uint64_t runningFilePos;
    uint64_t firstFrameSearchPos;
    int hasStreamInfoBlock;
    int hasMetadataBlocks;
    drflac_bs bs;
    drflac_frame_header firstFrameHeader;
#ifndef DR_FLAC_NO_OGG
    uint32_t oggSerial;
    uint64_t oggFirstBytePos;
    drflac_ogg_page_header oggBosHeader;
#endif
} drflac_init_info;
static DRFLAC_INLINE void drflac__decode_block_header(uint32_t blockHeader, uint8_t* isLastBlock, uint8_t* blockType, uint32_t* blockSize)
{
    blockHeader = drflac__be2host_32(blockHeader);
    *isLastBlock = (uint8_t)((blockHeader & 0x80000000UL) >> 31);
    *blockType   = (uint8_t)((blockHeader & 0x7F000000UL) >> 24);
    *blockSize   =                (blockHeader & 0x00FFFFFFUL);
}
static DRFLAC_INLINE int drflac__read_and_decode_block_header(drflac_read_proc onRead, void* pUserData, uint8_t* isLastBlock, uint8_t* blockType, uint32_t* blockSize)
{
    uint32_t blockHeader;
    *blockSize = 0;
    if (onRead(pUserData, &blockHeader, 4) != 4) {
        return 0;
    }
    drflac__decode_block_header(blockHeader, isLastBlock, blockType, blockSize);
    return 1;
}
static int drflac__read_streaminfo(drflac_read_proc onRead, void* pUserData, drflac_streaminfo* pStreamInfo)
{
    uint32_t blockSizes;
    uint64_t frameSizes = 0;
    uint64_t importantProps;
    uint8_t md5[16];
    if (onRead(pUserData, &blockSizes, 4) != 4) {
        return 0;
    }
    if (onRead(pUserData, &frameSizes, 6) != 6) {
        return 0;
    }
    if (onRead(pUserData, &importantProps, 8) != 8) {
        return 0;
    }
    if (onRead(pUserData, md5, sizeof(md5)) != sizeof(md5)) {
        return 0;
    }
    blockSizes     = drflac__be2host_32(blockSizes);
    frameSizes     = drflac__be2host_64(frameSizes);
    importantProps = drflac__be2host_64(importantProps);
    pStreamInfo->minBlockSizeInPCMFrames = (uint16_t)((blockSizes & 0xFFFF0000) >> 16);
    pStreamInfo->maxBlockSizeInPCMFrames = (uint16_t) (blockSizes & 0x0000FFFF);
    pStreamInfo->minFrameSizeInPCMFrames = (uint32_t)((frameSizes     &  (((uint64_t)0x00FFFFFF << 16) << 24)) >> 40);
    pStreamInfo->maxFrameSizeInPCMFrames = (uint32_t)((frameSizes     &  (((uint64_t)0x00FFFFFF << 16) <<  0)) >> 16);
    pStreamInfo->sampleRate              = (uint32_t)((importantProps &  (((uint64_t)0x000FFFFF << 16) << 28)) >> 44);
    pStreamInfo->channels                = (uint8_t )((importantProps &  (((uint64_t)0x0000000E << 16) << 24)) >> 41) + 1;
    pStreamInfo->bitsPerSample           = (uint8_t )((importantProps &  (((uint64_t)0x0000001F << 16) << 20)) >> 36) + 1;
    pStreamInfo->totalPCMFrameCount      =                ((importantProps & ((((uint64_t)0x0000000F << 16) << 16) | 0xFFFFFFFF)));
    DR_FLAC_COPY_MEMORY(pStreamInfo->md5, md5, sizeof(md5));
    return 1;
}
static void* drflac__malloc_default(size_t sz, void* pUserData)
{
    (void)pUserData;
    return DR_FLAC_MALLOC(sz);
}
static void* drflac__realloc_default(void* p, size_t sz, void* pUserData)
{
    (void)pUserData;
    return DR_FLAC_REALLOC(p, sz);
}
static void drflac__free_default(void* p, void* pUserData)
{
    (void)pUserData;
    DR_FLAC_FREE(p);
}
static void* drflac__malloc_from_callbacks(size_t sz, const ma_allocation_callbacks* pAllocationCallbacks)
{
    if (pAllocationCallbacks == NULL) {
        return NULL;
    }
    if (pAllocationCallbacks->onMalloc != NULL) {
        return pAllocationCallbacks->onMalloc(sz, pAllocationCallbacks->pUserData);
    }
    if (pAllocationCallbacks->onRealloc != NULL) {
        return pAllocationCallbacks->onRealloc(NULL, sz, pAllocationCallbacks->pUserData);
    }
    return NULL;
}
static void* drflac__realloc_from_callbacks(void* p, size_t szNew, size_t szOld, const ma_allocation_callbacks* pAllocationCallbacks)
{
    if (pAllocationCallbacks == NULL) {
        return NULL;
    }
    if (pAllocationCallbacks->onRealloc != NULL) {
        return pAllocationCallbacks->onRealloc(p, szNew, pAllocationCallbacks->pUserData);
    }
    if (pAllocationCallbacks->onMalloc != NULL && pAllocationCallbacks->onFree != NULL) {
        void* p2;
        p2 = pAllocationCallbacks->onMalloc(szNew, pAllocationCallbacks->pUserData);
        if (p2 == NULL) {
            return NULL;
        }
        if (p != NULL) {
            DR_FLAC_COPY_MEMORY(p2, p, DR_FLAC_MIN(szNew, szOld));
            pAllocationCallbacks->onFree(p, pAllocationCallbacks->pUserData);
        }
        return p2;
    }
    return NULL;
}
static void drflac__free_from_callbacks(void* p, const ma_allocation_callbacks* pAllocationCallbacks)
{
    if (p == NULL || pAllocationCallbacks == NULL) {
        return;
    }
    if (pAllocationCallbacks->onFree != NULL) {
        pAllocationCallbacks->onFree(p, pAllocationCallbacks->pUserData);
    }
}
static int drflac__resize_decoded_samples(drflac* pFlac, uint16_t blockSizeInPCMFrames)
{
    size_t sampleSize;
    size_t allocationSize;
    void* pAllocation;
    void* pDecodedSamples;
    sampleSize = sizeof(int32_t);
    if (pFlac->bitsPerSample == 32) {
        sampleSize = sizeof(int64_t);
    }
    allocationSize = drflac_align(blockSizeInPCMFrames * sampleSize, DR_FLAC_MAX_SIMD_VECTOR_SIZE) * pFlac->channels + DR_FLAC_MAX_SIMD_VECTOR_SIZE;
    pAllocation = drflac__malloc_from_callbacks(allocationSize, &pFlac->allocationCallbacks);
    if (pAllocation == NULL) {
        return 0;
    }
    drflac__free_from_callbacks(pFlac->pDecodedSamplesAllocation, &pFlac->allocationCallbacks);
    pFlac->pDecodedSamplesAllocation = pAllocation;
    pFlac->decodedSamplesCapacityInPCMFrames = blockSizeInPCMFrames;
    pDecodedSamples = (void*)drflac_align((size_t)pAllocation, DR_FLAC_MAX_SIMD_VECTOR_SIZE);
    if (pFlac->bitsPerSample != 32) {
        pFlac->decodedSamples.pS32 = (int32_t*)pDecodedSamples;
    } else {
        pFlac->decodedSamples.pS64 = (int64_t*)pDecodedSamples;
    }
    return 1;
}
static ma_result drflac__read_and_decode_metadata(drflac_read_proc onRead, drflac_seek_proc onSeek, drflac_tell_proc onTell, drflac_meta_proc onMeta, void* pUserData, void* pUserDataMD, uint64_t* pFirstFramePos, uint64_t* pSeektablePos, uint32_t* pSeekpointCount, ma_allocation_callbacks* pAllocationCallbacks)
{
    uint64_t runningFilePos   = 42;
    uint64_t seektablePos     = 0;
    uint32_t seektableSize    = 0;
    int64_t  fileSize         = 0;
    int hasKnownFileSize = 0;
    if (onTell != NULL && onSeek != NULL) {
        if (onSeek(pUserData, 0, DR_FLAC_SEEK_END)) {
            if (onTell(pUserData, &fileSize)) {
                hasKnownFileSize = 1;
            }
            if (!onSeek(pUserData, (int)runningFilePos, DR_FLAC_SEEK_SET)) {
                return MA_BAD_SEEK;
            }
        }
    }
    for (;;) {
        drflac_metadata metadata;
        uint8_t isLastBlock = 0;
        uint8_t blockType = 0;
        uint32_t blockSize;
        if (drflac__read_and_decode_block_header(onRead, pUserData, &isLastBlock, &blockType, &blockSize) == 0) {
            return MA_ERROR;
        }
        runningFilePos += 4;
        if (hasKnownFileSize && (runningFilePos > (uint64_t)fileSize || blockSize > (uint64_t)fileSize - runningFilePos)) {
            return MA_INVALID_DATA;
        }
        metadata.type = blockType;
        metadata.rawDataSize = 0;
        metadata.rawDataOffset = runningFilePos;
        metadata.pRawData = NULL;
        switch (blockType)
        {
            case DR_FLAC_METADATA_BLOCK_TYPE_APPLICATION:
            {
                if (blockSize < 4) {
                    return MA_INVALID_DATA;
                }
                if (onMeta) {
                    void* pRawData = drflac__malloc_from_callbacks(blockSize, pAllocationCallbacks);
                    if (pRawData == NULL) {
                        return MA_ERROR;
                    }
                    if (onRead(pUserData, pRawData, blockSize) != blockSize) {
                        drflac__free_from_callbacks(pRawData, pAllocationCallbacks);
                        return MA_ERROR;
                    }
                    metadata.pRawData = pRawData;
                    metadata.rawDataSize = blockSize;
                    metadata.data.application.id       = drflac__be2host_32(*(uint32_t*)pRawData);
                    metadata.data.application.pData    = (const void*)((uint8_t*)pRawData + sizeof(uint32_t));
                    metadata.data.application.dataSize = blockSize - sizeof(uint32_t);
                    onMeta(pUserDataMD, &metadata);
                    drflac__free_from_callbacks(pRawData, pAllocationCallbacks);
                }
            } break;
            case DR_FLAC_METADATA_BLOCK_TYPE_SEEKTABLE:
            {
                if ((blockSize % DR_FLAC_SEEKPOINT_SIZE_IN_BYTES) != 0) {
                    return MA_INVALID_DATA;
                }
                seektablePos  = runningFilePos;
                seektableSize = blockSize;
                if (onMeta) {
                    uint32_t seekpointCount;
                    uint32_t iSeekpoint;
                    void* pRawData;
                    size_t rawDataSize;
                    seekpointCount = blockSize/DR_FLAC_SEEKPOINT_SIZE_IN_BYTES;
                    rawDataSize = seekpointCount * sizeof(drflac_seekpoint);
                    pRawData = drflac__malloc_from_callbacks(rawDataSize, pAllocationCallbacks);
                    if (pRawData == NULL) {
                        return MA_ERROR;
                    }
                    for (iSeekpoint = 0; iSeekpoint < seekpointCount; ++iSeekpoint) {
                        drflac_seekpoint* pSeekpoint = (drflac_seekpoint*)pRawData + iSeekpoint;
                        if (onRead(pUserData, pSeekpoint, DR_FLAC_SEEKPOINT_SIZE_IN_BYTES) != DR_FLAC_SEEKPOINT_SIZE_IN_BYTES) {
                            drflac__free_from_callbacks(pRawData, pAllocationCallbacks);
                            return MA_ERROR;
                        }
                        pSeekpoint->firstPCMFrame   = drflac__be2host_64(pSeekpoint->firstPCMFrame);
                        pSeekpoint->flacFrameOffset = drflac__be2host_64(pSeekpoint->flacFrameOffset);
                        pSeekpoint->pcmFrameCount   = drflac__be2host_16(pSeekpoint->pcmFrameCount);
                    }
                    metadata.pRawData = pRawData;
                    metadata.rawDataSize = (uint32_t)rawDataSize;
                    metadata.data.seektable.seekpointCount = seekpointCount;
                    metadata.data.seektable.pSeekpoints = (const drflac_seekpoint*)pRawData;
                    onMeta(pUserDataMD, &metadata);
                    drflac__free_from_callbacks(pRawData, pAllocationCallbacks);
                }
            } break;
            case DR_FLAC_METADATA_BLOCK_TYPE_VORBIS_COMMENT:
            {
                if (blockSize < 8) {
                    if (onMeta != NULL && blockSize > 0) {
                        uint8_t unused[8];
                        if (onRead(pUserData, unused, blockSize) != blockSize) {
                            return MA_ERROR;
                        }
                    }
                    break;
                }
                if (onMeta) {
                    void* pRawData;
                    const char* pRunningData;
                    const char* pRunningDataEnd;
                    uint32_t i;
                    pRawData = drflac__malloc_from_callbacks(blockSize, pAllocationCallbacks);
                    if (pRawData == NULL) {
                        return MA_ERROR;
                    }
                    if (onRead(pUserData, pRawData, blockSize) != blockSize) {
                        drflac__free_from_callbacks(pRawData, pAllocationCallbacks);
                        return MA_ERROR;
                    }
                    metadata.pRawData = pRawData;
                    metadata.rawDataSize = blockSize;
                    pRunningData    = (const char*)pRawData;
                    pRunningDataEnd = (const char*)pRawData + blockSize;
                    metadata.data.vorbis_comment.vendorLength = drflac__le2host_32_ptr_unaligned(pRunningData); pRunningData += 4;
                    if ((pRunningDataEnd - pRunningData) - 4 < (int64_t)metadata.data.vorbis_comment.vendorLength) {
                        goto vorbis_comment_done;
                    }
                    metadata.data.vorbis_comment.vendor       = pRunningData;                                   pRunningData += metadata.data.vorbis_comment.vendorLength;
                    metadata.data.vorbis_comment.commentCount = drflac__le2host_32_ptr_unaligned(pRunningData); pRunningData += 4;
                    if ((pRunningDataEnd - pRunningData) / sizeof(uint32_t) < metadata.data.vorbis_comment.commentCount) {
                        goto vorbis_comment_done;
                    }
                    metadata.data.vorbis_comment.pComments    = pRunningData;
                    for (i = 0; i < metadata.data.vorbis_comment.commentCount; ++i) {
                        uint32_t commentLength;
                        if (pRunningDataEnd - pRunningData < 4) {
                            goto vorbis_comment_done;
                        }
                        commentLength = drflac__le2host_32_ptr_unaligned(pRunningData); pRunningData += 4;
                        if (pRunningDataEnd - pRunningData < (int64_t)commentLength) {
                            goto vorbis_comment_done;
                        }
                        pRunningData += commentLength;
                    }
                    onMeta(pUserDataMD, &metadata);
                vorbis_comment_done:
                    drflac__free_from_callbacks(pRawData, pAllocationCallbacks);
                }
            } break;
            case DR_FLAC_METADATA_BLOCK_TYPE_CUESHEET:
            {
                if (blockSize < 396) {
                    return MA_INVALID_DATA;
                }
                if (onMeta) {
                    void* pRawData;
                    const char* pRunningData;
                    const char* pRunningDataEnd;
                    size_t bufferSize;
                    uint8_t iTrack;
                    uint8_t iIndex;
                    void* pTrackData;
                    pRawData = drflac__malloc_from_callbacks(blockSize, pAllocationCallbacks);
                    if (pRawData == NULL) {
                        return MA_ERROR;
                    }
                    if (onRead(pUserData, pRawData, blockSize) != blockSize) {
                        drflac__free_from_callbacks(pRawData, pAllocationCallbacks);
                        return MA_ERROR;
                    }
                    metadata.pRawData = pRawData;
                    metadata.rawDataSize = blockSize;
                    pRunningData    = (const char*)pRawData;
                    pRunningDataEnd = (const char*)pRawData + blockSize;
                    DR_FLAC_COPY_MEMORY(metadata.data.cuesheet.catalog, pRunningData, 128);                              pRunningData += 128;
                    metadata.data.cuesheet.leadInSampleCount = drflac__be2host_64(*(const uint64_t*)pRunningData); pRunningData += 8;
                    metadata.data.cuesheet.isCD              = (pRunningData[0] & 0x80) != 0;                           pRunningData += 259;
                    metadata.data.cuesheet.trackCount        = pRunningData[0];                                         pRunningData += 1;
                    metadata.data.cuesheet.pTrackData        = NULL;
                    {
                        const char* pRunningDataSaved = pRunningData;
                        bufferSize = metadata.data.cuesheet.trackCount * DR_FLAC_CUESHEET_TRACK_PADDED_SIZE_IN_BYTES;
                        for (iTrack = 0; iTrack < metadata.data.cuesheet.trackCount; ++iTrack) {
                            uint8_t indexCount;
                            uint32_t indexPointSize;
                            if (pRunningDataEnd - pRunningData < DR_FLAC_CUESHEET_TRACK_SIZE_IN_BYTES) {
                                drflac__free_from_callbacks(pRawData, pAllocationCallbacks);
                                return MA_INVALID_DATA;
                            }
                            pRunningData += 35;
                            indexCount = pRunningData[0];
                            pRunningData += 1;
                            bufferSize += indexCount * sizeof(drflac_cuesheet_track_index);
                            indexPointSize = indexCount * DR_FLAC_CUESHEET_TRACK_INDEX_SIZE_IN_BYTES;
                            if (pRunningDataEnd - pRunningData < (int64_t)indexPointSize) {
                                drflac__free_from_callbacks(pRawData, pAllocationCallbacks);
                                return MA_INVALID_DATA;
                            }
                            pRunningData += indexPointSize;
                        }
                        pRunningData = pRunningDataSaved;
                    }
                    {
                        char* pRunningTrackData;
                        pTrackData = drflac__malloc_from_callbacks(bufferSize, pAllocationCallbacks);
                        if (pTrackData == NULL) {
                            drflac__free_from_callbacks(pRawData, pAllocationCallbacks);
                            return MA_ERROR;
                        }
                        pRunningTrackData = (char*)pTrackData;
                        for (iTrack = 0; iTrack < metadata.data.cuesheet.trackCount; ++iTrack) {
                            uint8_t indexCount;
                            DR_FLAC_COPY_MEMORY(pRunningTrackData, pRunningData, DR_FLAC_CUESHEET_TRACK_SIZE_IN_BYTES);
                            pRunningData      += DR_FLAC_CUESHEET_TRACK_SIZE_IN_BYTES-1;
                            pRunningTrackData += DR_FLAC_CUESHEET_TRACK_SIZE_IN_BYTES-1;
                            indexCount = pRunningData[0];
                            pRunningData      += 1;
                            pRunningTrackData += 1 + (DR_FLAC_CUESHEET_TRACK_PADDED_SIZE_IN_BYTES - DR_FLAC_CUESHEET_TRACK_SIZE_IN_BYTES);
                            for (iIndex = 0; iIndex < indexCount; ++iIndex) {
                                drflac_cuesheet_track_index* pTrackIndex = (drflac_cuesheet_track_index*)pRunningTrackData;
                                DR_FLAC_COPY_MEMORY(pRunningTrackData, pRunningData, DR_FLAC_CUESHEET_TRACK_INDEX_SIZE_IN_BYTES);
                                pRunningData      += DR_FLAC_CUESHEET_TRACK_INDEX_SIZE_IN_BYTES;
                                pRunningTrackData += sizeof(drflac_cuesheet_track_index);
                                pTrackIndex->offset = drflac__be2host_64(pTrackIndex->offset);
                            }
                        }
                        metadata.data.cuesheet.pTrackData = pTrackData;
                    }
                    onMeta(pUserDataMD, &metadata);
                    drflac__free_from_callbacks(pRawData, pAllocationCallbacks);
                    pRawData = NULL;
                    drflac__free_from_callbacks(pTrackData, pAllocationCallbacks);
                    pTrackData = NULL;
                }
            } break;
            case DR_FLAC_METADATA_BLOCK_TYPE_PICTURE:
            {
                if (blockSize < 32) {
                    return MA_INVALID_DATA;
                }
                if (onMeta) {
                    ma_result result = MA_SUCCESS;
                    uint32_t blockSizeRemaining = blockSize;
                    char* pMime = NULL;
                    char* pDescription = NULL;
                    void* pPictureData = NULL;
                    if (blockSizeRemaining < 4) {
                        result = MA_INVALID_DATA;
                        goto done_flac;
                    }
                    if (onRead(pUserData, &metadata.data.picture.type, 4) != 4) {
                        result = MA_ERROR;
                        goto done_flac;
                    }
                    blockSizeRemaining -= 4;
                    metadata.data.picture.type = drflac__be2host_32(metadata.data.picture.type);
                    if (blockSizeRemaining < 4) {
                        result = MA_INVALID_DATA;
                        goto done_flac;
                    }
                    if (onRead(pUserData, &metadata.data.picture.mimeLength, 4) != 4) {
                        result = MA_ERROR;
                        goto done_flac;
                    }
                    blockSizeRemaining -= 4;
                    metadata.data.picture.mimeLength = drflac__be2host_32(metadata.data.picture.mimeLength);
                    if (blockSizeRemaining < metadata.data.picture.mimeLength) {
                        result = MA_INVALID_DATA;
                        goto done_flac;
                    }
                    pMime = (char*)drflac__malloc_from_callbacks(metadata.data.picture.mimeLength + 1, pAllocationCallbacks);
                    if (pMime == NULL) {
                        result = MA_ERROR;
                        goto done_flac;
                    }
                    if (onRead(pUserData, pMime, metadata.data.picture.mimeLength) != metadata.data.picture.mimeLength) {
                        result = MA_ERROR;
                        goto done_flac;
                    }
                    blockSizeRemaining -= metadata.data.picture.mimeLength;
                    pMime[metadata.data.picture.mimeLength] = '\0';
                    metadata.data.picture.mime = (const char*)pMime;
                    if (blockSizeRemaining < 4) {
                        result = MA_INVALID_DATA;
                        goto done_flac;
                    }
                    if (onRead(pUserData, &metadata.data.picture.descriptionLength, 4) != 4) {
                        result = MA_ERROR;
                        goto done_flac;
                    }
                    blockSizeRemaining -= 4;
                    metadata.data.picture.descriptionLength = drflac__be2host_32(metadata.data.picture.descriptionLength);
                    if (blockSizeRemaining < metadata.data.picture.descriptionLength) {
                        result = MA_INVALID_DATA;
                        goto done_flac;
                    }
                    pDescription = (char*)drflac__malloc_from_callbacks(metadata.data.picture.descriptionLength + 1, pAllocationCallbacks);
                    if (pDescription == NULL) {
                        result = MA_ERROR;
                        goto done_flac;
                    }
                    if (onRead(pUserData, pDescription, metadata.data.picture.descriptionLength) != metadata.data.picture.descriptionLength) {
                        result = MA_ERROR;
                        goto done_flac;
                    }
                    blockSizeRemaining -= metadata.data.picture.descriptionLength;
                    pDescription[metadata.data.picture.descriptionLength] = '\0';
                    metadata.data.picture.description = (const char*)pDescription;
                    if (blockSizeRemaining < 4) {
                        result = MA_INVALID_DATA;
                        goto done_flac;
                    }
                    if (onRead(pUserData, &metadata.data.picture.width, 4) != 4) {
                        result = MA_ERROR;
                        goto done_flac;
                    }
                    blockSizeRemaining -= 4;
                    metadata.data.picture.width = drflac__be2host_32(metadata.data.picture.width);
                    if (blockSizeRemaining < 4) {
                        result = MA_INVALID_DATA;
                        goto done_flac;
                    }
                    if (onRead(pUserData, &metadata.data.picture.height, 4) != 4) {
                        result = MA_ERROR;
                        goto done_flac;
                    }
                    blockSizeRemaining -= 4;
                    metadata.data.picture.height = drflac__be2host_32(metadata.data.picture.height);
                    if (blockSizeRemaining < 4) {
                        result = MA_INVALID_DATA;
                        goto done_flac;
                    }
                    if (onRead(pUserData, &metadata.data.picture.colorDepth, 4) != 4) {
                        result = MA_ERROR;
                        goto done_flac;
                    }
                    blockSizeRemaining -= 4;
                    metadata.data.picture.colorDepth = drflac__be2host_32(metadata.data.picture.colorDepth);
                    if (blockSizeRemaining < 4) {
                        result = MA_INVALID_DATA;
                        goto done_flac;
                    }
                    if (onRead(pUserData, &metadata.data.picture.indexColorCount, 4) != 4) {
                        result = MA_ERROR;
                        goto done_flac;
                    }
                    blockSizeRemaining -= 4;
                    metadata.data.picture.indexColorCount = drflac__be2host_32(metadata.data.picture.indexColorCount);
                    if (blockSizeRemaining < 4) {
                        result = MA_INVALID_DATA;
                        goto done_flac;
                    }
                    if (onRead(pUserData, &metadata.data.picture.pictureDataSize, 4) != 4) {
                        result = MA_ERROR;
                        goto done_flac;
                    }
                    blockSizeRemaining -= 4;
                    metadata.data.picture.pictureDataSize = drflac__be2host_32(metadata.data.picture.pictureDataSize);
                    if (blockSizeRemaining < metadata.data.picture.pictureDataSize) {
                        result = MA_INVALID_DATA;
                        goto done_flac;
                    }
                    metadata.data.picture.pictureDataOffset = runningFilePos + (blockSize - blockSizeRemaining);
                #ifndef DR_FLAC_NO_PICTURE_METADATA_MALLOC
                    pPictureData = drflac__malloc_from_callbacks(metadata.data.picture.pictureDataSize, pAllocationCallbacks);
                    if (pPictureData != NULL) {
                        if (onRead(pUserData, pPictureData, metadata.data.picture.pictureDataSize) != metadata.data.picture.pictureDataSize) {
                            result = MA_ERROR;
                            goto done_flac;
                        }
                    } else
                #endif
                    {
                        if (!onSeek(pUserData, metadata.data.picture.pictureDataSize, DR_FLAC_SEEK_CUR)) {
                            result = MA_ERROR;
                            goto done_flac;
                        }
                    }
                    blockSizeRemaining -= metadata.data.picture.pictureDataSize;
                    (void)blockSizeRemaining;
                    metadata.data.picture.pPictureData = (const uint8_t*)pPictureData;
                    if (metadata.data.picture.pictureDataOffset != 0 || metadata.data.picture.pPictureData != NULL) {
                        onMeta(pUserDataMD, &metadata);
                    } else {
                    }
                done_flac:
                    drflac__free_from_callbacks(pMime,        pAllocationCallbacks);
                    drflac__free_from_callbacks(pDescription, pAllocationCallbacks);
                    drflac__free_from_callbacks(pPictureData, pAllocationCallbacks);
                    if (result != MA_SUCCESS) {
                        return result;
                    }
                }
            } break;
            case DR_FLAC_METADATA_BLOCK_TYPE_PADDING:
            {
                if (onMeta) {
                    metadata.data.padding.unused = 0;
                    if (!onSeek(pUserData, blockSize, DR_FLAC_SEEK_CUR)) {
                        return MA_BAD_SEEK;
                    } else {
                        onMeta(pUserDataMD, &metadata);
                    }
                }
            } break;
            case DR_FLAC_METADATA_BLOCK_TYPE_INVALID:
            {
                return MA_INVALID_DATA;
            } break;
            default:
            {
                if (onMeta) {
                    void* pRawData = drflac__malloc_from_callbacks(blockSize, pAllocationCallbacks);
                    if (pRawData != NULL) {
                        if (onRead(pUserData, pRawData, blockSize) != blockSize) {
                            drflac__free_from_callbacks(pRawData, pAllocationCallbacks);
                            return MA_ERROR;
                        }
                    } else {
                        if (!onSeek(pUserData, blockSize, DR_FLAC_SEEK_CUR)) {
                            return MA_ERROR;
                        }
                    }
                    metadata.pRawData = pRawData;
                    metadata.rawDataSize = blockSize;
                    onMeta(pUserDataMD, &metadata);
                    drflac__free_from_callbacks(pRawData, pAllocationCallbacks);
                }
            } break;
        }
        if (onMeta == NULL && blockSize > 0) {
            if (!onSeek(pUserData, blockSize, DR_FLAC_SEEK_CUR)) {
                return MA_BAD_SEEK;
            }
        }
        runningFilePos += blockSize;
        if (isLastBlock) {
            break;
        }
    }
    *pSeektablePos   = seektablePos;
    *pSeekpointCount = seektableSize / DR_FLAC_SEEKPOINT_SIZE_IN_BYTES;
    *pFirstFramePos  = runningFilePos;
    return MA_SUCCESS;
}
static int drflac__init_private__native_without_streaminfo(drflac_init_info* pInit, const uint8_t* pInitialData, uint64_t firstFrameSearchPos)
{
    uint32_t initialData;
    pInit->container = drflac_container_native;
    pInit->hasStreamInfoBlock = 0;
    pInit->hasMetadataBlocks = 0;
    pInit->firstFrameSearchPos = firstFrameSearchPos;
    DR_FLAC_COPY_MEMORY(&initialData, pInitialData, sizeof(initialData));
    initialData = drflac__be2host_32(initialData);
    pInit->bs.consumedBits = DR_FLAC_CACHE_L1_SIZE_BITS(&pInit->bs) - 32;
    pInit->bs.cache        = (drflac_cache_t)initialData << pInit->bs.consumedBits;
    #ifndef DR_FLAC_NO_CRC
    {
        pInit->bs.crc16Cache = initialData;
        pInit->bs.crc16CacheIgnoredBytes = pInit->bs.consumedBits / 8;
    }
    #endif
    if (!drflac__read_next_flac_frame_header(&pInit->bs, 0, &pInit->firstFrameHeader)) {
        return 0;
    }
    if (pInit->firstFrameHeader.bitsPerSample == 0 || pInit->firstFrameHeader.sampleRate == 0) {
        return 0;
    }
    pInit->sampleRate              = pInit->firstFrameHeader.sampleRate;
    pInit->channels                = drflac__get_channel_count_from_channel_assignment(pInit->firstFrameHeader.channelAssignment);
    pInit->bitsPerSample           = pInit->firstFrameHeader.bitsPerSample;
    pInit->maxBlockSizeInPCMFrames = 65535;
    return 1;
}
static int drflac__init_private__native(drflac_init_info* pInit, drflac_read_proc onRead, drflac_seek_proc onSeek, drflac_meta_proc onMeta, void* pUserData, void* pUserDataMD, int relaxed)
{
    uint8_t isLastBlock;
    uint8_t blockType;
    uint32_t blockSize;
    uint32_t blockHeader;
    (void)onSeek;
    pInit->container = drflac_container_native;
    if (onRead(pUserData, &blockHeader, 4) != 4) {
        return 0;
    }
    drflac__decode_block_header(blockHeader, &isLastBlock, &blockType, &blockSize);
    if (blockType != DR_FLAC_METADATA_BLOCK_TYPE_STREAMINFO || blockSize != 34) {
        if (!relaxed) {
            return 0;
        } else {
            return drflac__init_private__native_without_streaminfo(pInit, (const uint8_t*)&blockHeader, pInit->runningFilePos);
        }
    } else {
        drflac_streaminfo streaminfo;
        if (!drflac__read_streaminfo(onRead, pUserData, &streaminfo)) {
            return 0;
        }
        pInit->hasStreamInfoBlock      = 1;
        pInit->sampleRate              = streaminfo.sampleRate;
        pInit->channels                = streaminfo.channels;
        pInit->bitsPerSample           = streaminfo.bitsPerSample;
        pInit->totalPCMFrameCount      = streaminfo.totalPCMFrameCount;
        pInit->maxBlockSizeInPCMFrames = streaminfo.maxBlockSizeInPCMFrames;
        pInit->hasMetadataBlocks       = !isLastBlock;
        if (onMeta) {
            drflac_metadata metadata;
            metadata.type = DR_FLAC_METADATA_BLOCK_TYPE_STREAMINFO;
            metadata.pRawData = NULL;
            metadata.rawDataSize = 0;
            metadata.data.streaminfo = streaminfo;
            onMeta(pUserDataMD, &metadata);
        }
        return 1;
    }
}
#ifndef DR_FLAC_NO_OGG
#define DR_FLAC_OGG_MAX_PAGE_SIZE            65307
#define DR_FLAC_OGG_CAPTURE_PATTERN_CRC32    1605413199
typedef enum
{
    drflac_ogg_recover_on_crc_mismatch,
    drflac_ogg_fail_on_crc_mismatch
} drflac_ogg_crc_mismatch_recovery;
#ifndef DR_FLAC_NO_CRC
static uint32_t drflac__crc32_table[] = {
    0x00000000L, 0x04C11DB7L, 0x09823B6EL, 0x0D4326D9L,
    0x130476DCL, 0x17C56B6BL, 0x1A864DB2L, 0x1E475005L,
    0x2608EDB8L, 0x22C9F00FL, 0x2F8AD6D6L, 0x2B4BCB61L,
    0x350C9B64L, 0x31CD86D3L, 0x3C8EA00AL, 0x384FBDBDL,
    0x4C11DB70L, 0x48D0C6C7L, 0x4593E01EL, 0x4152FDA9L,
    0x5F15ADACL, 0x5BD4B01BL, 0x569796C2L, 0x52568B75L,
    0x6A1936C8L, 0x6ED82B7FL, 0x639B0DA6L, 0x675A1011L,
    0x791D4014L, 0x7DDC5DA3L, 0x709F7B7AL, 0x745E66CDL,
    0x9823B6E0L, 0x9CE2AB57L, 0x91A18D8EL, 0x95609039L,
    0x8B27C03CL, 0x8FE6DD8BL, 0x82A5FB52L, 0x8664E6E5L,
    0xBE2B5B58L, 0xBAEA46EFL, 0xB7A96036L, 0xB3687D81L,
    0xAD2F2D84L, 0xA9EE3033L, 0xA4AD16EAL, 0xA06C0B5DL,
    0xD4326D90L, 0xD0F37027L, 0xDDB056FEL, 0xD9714B49L,
    0xC7361B4CL, 0xC3F706FBL, 0xCEB42022L, 0xCA753D95L,
    0xF23A8028L, 0xF6FB9D9FL, 0xFBB8BB46L, 0xFF79A6F1L,
    0xE13EF6F4L, 0xE5FFEB43L, 0xE8BCCD9AL, 0xEC7DD02DL,
    0x34867077L, 0x30476DC0L, 0x3D044B19L, 0x39C556AEL,
    0x278206ABL, 0x23431B1CL, 0x2E003DC5L, 0x2AC12072L,
    0x128E9DCFL, 0x164F8078L, 0x1B0CA6A1L, 0x1FCDBB16L,
    0x018AEB13L, 0x054BF6A4L, 0x0808D07DL, 0x0CC9CDCAL,
    0x7897AB07L, 0x7C56B6B0L, 0x71159069L, 0x75D48DDEL,
    0x6B93DDDBL, 0x6F52C06CL, 0x6211E6B5L, 0x66D0FB02L,
    0x5E9F46BFL, 0x5A5E5B08L, 0x571D7DD1L, 0x53DC6066L,
    0x4D9B3063L, 0x495A2DD4L, 0x44190B0DL, 0x40D816BAL,
    0xACA5C697L, 0xA864DB20L, 0xA527FDF9L, 0xA1E6E04EL,
    0xBFA1B04BL, 0xBB60ADFCL, 0xB6238B25L, 0xB2E29692L,
    0x8AAD2B2FL, 0x8E6C3698L, 0x832F1041L, 0x87EE0DF6L,
    0x99A95DF3L, 0x9D684044L, 0x902B669DL, 0x94EA7B2AL,
    0xE0B41DE7L, 0xE4750050L, 0xE9362689L, 0xEDF73B3EL,
    0xF3B06B3BL, 0xF771768CL, 0xFA325055L, 0xFEF34DE2L,
    0xC6BCF05FL, 0xC27DEDE8L, 0xCF3ECB31L, 0xCBFFD686L,
    0xD5B88683L, 0xD1799B34L, 0xDC3ABDEDL, 0xD8FBA05AL,
    0x690CE0EEL, 0x6DCDFD59L, 0x608EDB80L, 0x644FC637L,
    0x7A089632L, 0x7EC98B85L, 0x738AAD5CL, 0x774BB0EBL,
    0x4F040D56L, 0x4BC510E1L, 0x46863638L, 0x42472B8FL,
    0x5C007B8AL, 0x58C1663DL, 0x558240E4L, 0x51435D53L,
    0x251D3B9EL, 0x21DC2629L, 0x2C9F00F0L, 0x285E1D47L,
    0x36194D42L, 0x32D850F5L, 0x3F9B762CL, 0x3B5A6B9BL,
    0x0315D626L, 0x07D4CB91L, 0x0A97ED48L, 0x0E56F0FFL,
    0x1011A0FAL, 0x14D0BD4DL, 0x19939B94L, 0x1D528623L,
    0xF12F560EL, 0xF5EE4BB9L, 0xF8AD6D60L, 0xFC6C70D7L,
    0xE22B20D2L, 0xE6EA3D65L, 0xEBA91BBCL, 0xEF68060BL,
    0xD727BBB6L, 0xD3E6A601L, 0xDEA580D8L, 0xDA649D6FL,
    0xC423CD6AL, 0xC0E2D0DDL, 0xCDA1F604L, 0xC960EBB3L,
    0xBD3E8D7EL, 0xB9FF90C9L, 0xB4BCB610L, 0xB07DABA7L,
    0xAE3AFBA2L, 0xAAFBE615L, 0xA7B8C0CCL, 0xA379DD7BL,
    0x9B3660C6L, 0x9FF77D71L, 0x92B45BA8L, 0x9675461FL,
    0x8832161AL, 0x8CF30BADL, 0x81B02D74L, 0x857130C3L,
    0x5D8A9099L, 0x594B8D2EL, 0x5408ABF7L, 0x50C9B640L,
    0x4E8EE645L, 0x4A4FFBF2L, 0x470CDD2BL, 0x43CDC09CL,
    0x7B827D21L, 0x7F436096L, 0x7200464FL, 0x76C15BF8L,
    0x68860BFDL, 0x6C47164AL, 0x61043093L, 0x65C52D24L,
    0x119B4BE9L, 0x155A565EL, 0x18197087L, 0x1CD86D30L,
    0x029F3D35L, 0x065E2082L, 0x0B1D065BL, 0x0FDC1BECL,
    0x3793A651L, 0x3352BBE6L, 0x3E119D3FL, 0x3AD08088L,
    0x2497D08DL, 0x2056CD3AL, 0x2D15EBE3L, 0x29D4F654L,
    0xC5A92679L, 0xC1683BCEL, 0xCC2B1D17L, 0xC8EA00A0L,
    0xD6AD50A5L, 0xD26C4D12L, 0xDF2F6BCBL, 0xDBEE767CL,
    0xE3A1CBC1L, 0xE760D676L, 0xEA23F0AFL, 0xEEE2ED18L,
    0xF0A5BD1DL, 0xF464A0AAL, 0xF9278673L, 0xFDE69BC4L,
    0x89B8FD09L, 0x8D79E0BEL, 0x803AC667L, 0x84FBDBD0L,
    0x9ABC8BD5L, 0x9E7D9662L, 0x933EB0BBL, 0x97FFAD0CL,
    0xAFB010B1L, 0xAB710D06L, 0xA6322BDFL, 0xA2F33668L,
    0xBCB4666DL, 0xB8757BDAL, 0xB5365D03L, 0xB1F740B4L
};
#endif
static DRFLAC_INLINE uint32_t drflac_crc32_byte(uint32_t crc32, uint8_t data)
{
#ifndef DR_FLAC_NO_CRC
    return (crc32 << 8) ^ drflac__crc32_table[(uint8_t)((crc32 >> 24) & 0xFF) ^ data];
#else
    (void)data;
    return crc32;
#endif
}
#if 0
static DRFLAC_INLINE uint32_t drflac_crc32_uint32(uint32_t crc32, uint32_t data)
{
    crc32 = drflac_crc32_byte(crc32, (uint8_t)((data >> 24) & 0xFF));
    crc32 = drflac_crc32_byte(crc32, (uint8_t)((data >> 16) & 0xFF));
    crc32 = drflac_crc32_byte(crc32, (uint8_t)((data >>  8) & 0xFF));
    crc32 = drflac_crc32_byte(crc32, (uint8_t)((data >>  0) & 0xFF));
    return crc32;
}
static DRFLAC_INLINE uint32_t drflac_crc32_uint64(uint32_t crc32, uint64_t data)
{
    crc32 = drflac_crc32_uint32(crc32, (uint32_t)((data >> 32) & 0xFFFFFFFF));
    crc32 = drflac_crc32_uint32(crc32, (uint32_t)((data >>  0) & 0xFFFFFFFF));
    return crc32;
}
#endif
static DRFLAC_INLINE uint32_t drflac_crc32_buffer(uint32_t crc32, uint8_t* pData, uint32_t dataSize)
{
    uint32_t i;
    for (i = 0; i < dataSize; ++i) {
        crc32 = drflac_crc32_byte(crc32, pData[i]);
    }
    return crc32;
}
static DRFLAC_INLINE int drflac_ogg__is_capture_pattern(uint8_t pattern[4])
{
    return pattern[0] == 'O' && pattern[1] == 'g' && pattern[2] == 'g' && pattern[3] == 'S';
}
static DRFLAC_INLINE uint32_t drflac_ogg__get_page_header_size(drflac_ogg_page_header* pHeader)
{
    return 27 + pHeader->segmentCount;
}
static DRFLAC_INLINE uint32_t drflac_ogg__get_page_body_size(drflac_ogg_page_header* pHeader)
{
    uint32_t pageBodySize = 0;
    int i;
    for (i = 0; i < pHeader->segmentCount; ++i) {
        pageBodySize += pHeader->segmentTable[i];
    }
    return pageBodySize;
}
static ma_result drflac_ogg__read_page_header_after_capture_pattern(drflac_read_proc onRead, void* pUserData, drflac_ogg_page_header* pHeader, uint32_t* pBytesRead, uint32_t* pCRC32)
{
    uint8_t data[23];
    uint32_t i;
    DR_FLAC_ASSERT(*pCRC32 == DR_FLAC_OGG_CAPTURE_PATTERN_CRC32);
    if (onRead(pUserData, data, 23) != 23) {
        return MA_AT_END;
    }
    *pBytesRead += 23;
    pHeader->capturePattern[0] = 'O';
    pHeader->capturePattern[1] = 'g';
    pHeader->capturePattern[2] = 'g';
    pHeader->capturePattern[3] = 'S';
    pHeader->structureVersion = data[0];
    pHeader->headerType       = data[1];
    DR_FLAC_COPY_MEMORY(&pHeader->granulePosition, &data[ 2], 8);
    DR_FLAC_COPY_MEMORY(&pHeader->serialNumber,    &data[10], 4);
    DR_FLAC_COPY_MEMORY(&pHeader->sequenceNumber,  &data[14], 4);
    DR_FLAC_COPY_MEMORY(&pHeader->checksum,        &data[18], 4);
    pHeader->segmentCount     = data[22];
    data[18] = 0;
    data[19] = 0;
    data[20] = 0;
    data[21] = 0;
    for (i = 0; i < 23; ++i) {
        *pCRC32 = drflac_crc32_byte(*pCRC32, data[i]);
    }
    if (onRead(pUserData, pHeader->segmentTable, pHeader->segmentCount) != pHeader->segmentCount) {
        return MA_AT_END;
    }
    *pBytesRead += pHeader->segmentCount;
    for (i = 0; i < pHeader->segmentCount; ++i) {
        *pCRC32 = drflac_crc32_byte(*pCRC32, pHeader->segmentTable[i]);
    }
    return MA_SUCCESS;
}
static ma_result drflac_ogg__read_page_header(drflac_read_proc onRead, void* pUserData, drflac_ogg_page_header* pHeader, uint32_t* pBytesRead, uint32_t* pCRC32)
{
    uint8_t id[4];
    *pBytesRead = 0;
    if (onRead(pUserData, id, 4) != 4) {
        return MA_AT_END;
    }
    *pBytesRead += 4;
    for (;;) {
        if (drflac_ogg__is_capture_pattern(id)) {
            ma_result result;
            *pCRC32 = DR_FLAC_OGG_CAPTURE_PATTERN_CRC32;
            result = drflac_ogg__read_page_header_after_capture_pattern(onRead, pUserData, pHeader, pBytesRead, pCRC32);
            if (result == MA_SUCCESS) {
                return MA_SUCCESS;
            } else {
                if (result == MA_CRC_MISMATCH) {
                    continue;
                } else {
                    return result;
                }
            }
        } else {
            id[0] = id[1];
            id[1] = id[2];
            id[2] = id[3];
            if (onRead(pUserData, &id[3], 1) != 1) {
                return MA_AT_END;
            }
            *pBytesRead += 1;
        }
    }
}
typedef struct
{
    drflac_read_proc onRead;
    drflac_seek_proc onSeek;
    drflac_tell_proc onTell;
    void* pUserData;
    uint64_t currentBytePos;
    uint64_t firstBytePos;
    uint32_t serialNumber;
    drflac_ogg_page_header bosPageHeader;
    drflac_ogg_page_header currentPageHeader;
    uint32_t bytesRemainingInPage;
    uint32_t pageDataSize;
    uint8_t pageData[DR_FLAC_OGG_MAX_PAGE_SIZE];
} drflac_oggbs;
static size_t drflac_oggbs__read_physical(drflac_oggbs* oggbs, void* bufferOut, size_t bytesToRead)
{
    size_t bytesActuallyRead = oggbs->onRead(oggbs->pUserData, bufferOut, bytesToRead);
    oggbs->currentBytePos += bytesActuallyRead;
    return bytesActuallyRead;
}
static int drflac_oggbs__seek_physical(drflac_oggbs* oggbs, uint64_t offset, drflac_seek_origin origin)
{
    if (origin == DR_FLAC_SEEK_SET) {
        if (offset <= 0x7FFFFFFF) {
            if (!oggbs->onSeek(oggbs->pUserData, (int)offset, DR_FLAC_SEEK_SET)) {
                return 0;
            }
            oggbs->currentBytePos = offset;
            return 1;
        } else {
            if (!oggbs->onSeek(oggbs->pUserData, 0x7FFFFFFF, DR_FLAC_SEEK_SET)) {
                return 0;
            }
            oggbs->currentBytePos = offset;
            return drflac_oggbs__seek_physical(oggbs, offset - 0x7FFFFFFF, DR_FLAC_SEEK_CUR);
        }
    } else {
        while (offset > 0x7FFFFFFF) {
            if (!oggbs->onSeek(oggbs->pUserData, 0x7FFFFFFF, DR_FLAC_SEEK_CUR)) {
                return 0;
            }
            oggbs->currentBytePos += 0x7FFFFFFF;
            offset -= 0x7FFFFFFF;
        }
        if (!oggbs->onSeek(oggbs->pUserData, (int)offset, DR_FLAC_SEEK_CUR)) {
            return 0;
        }
        oggbs->currentBytePos += offset;
        return 1;
    }
}
static int drflac_oggbs__goto_next_page(drflac_oggbs* oggbs, drflac_ogg_crc_mismatch_recovery recoveryMethod)
{
    drflac_ogg_page_header header;
    for (;;) {
        uint32_t crc32 = 0;
        uint32_t bytesRead;
        uint32_t pageBodySize;
#ifndef DR_FLAC_NO_CRC
        uint32_t actualCRC32;
#endif
        if (drflac_ogg__read_page_header(oggbs->onRead, oggbs->pUserData, &header, &bytesRead, &crc32) != MA_SUCCESS) {
            return 0;
        }
        oggbs->currentBytePos += bytesRead;
        pageBodySize = drflac_ogg__get_page_body_size(&header);
        if (pageBodySize > DR_FLAC_OGG_MAX_PAGE_SIZE) {
            continue;
        }
        if (header.serialNumber != oggbs->serialNumber) {
            if (pageBodySize > 0 && !drflac_oggbs__seek_physical(oggbs, pageBodySize, DR_FLAC_SEEK_CUR)) {
                return 0;
            }
            continue;
        }
        if (drflac_oggbs__read_physical(oggbs, oggbs->pageData, pageBodySize) != pageBodySize) {
            return 0;
        }
        oggbs->pageDataSize = pageBodySize;
#ifndef DR_FLAC_NO_CRC
        actualCRC32 = drflac_crc32_buffer(crc32, oggbs->pageData, oggbs->pageDataSize);
        if (actualCRC32 != header.checksum) {
            if (recoveryMethod == drflac_ogg_recover_on_crc_mismatch) {
                continue;
            } else {
                drflac_oggbs__goto_next_page(oggbs, drflac_ogg_recover_on_crc_mismatch);
                return 0;
            }
        }
#else
        (void)recoveryMethod;
#endif
        oggbs->currentPageHeader = header;
        oggbs->bytesRemainingInPage = pageBodySize;
        return 1;
    }
}
#if 0
static uint8_t drflac_oggbs__get_current_segment_index(drflac_oggbs* oggbs, uint8_t* pBytesRemainingInSeg)
{
    uint32_t bytesConsumedInPage = drflac_ogg__get_page_body_size(&oggbs->currentPageHeader) - oggbs->bytesRemainingInPage;
    uint8_t iSeg = 0;
    uint32_t iByte = 0;
    while (iByte < bytesConsumedInPage) {
        uint8_t segmentSize = oggbs->currentPageHeader.segmentTable[iSeg];
        if (iByte + segmentSize > bytesConsumedInPage) {
            break;
        } else {
            iSeg += 1;
            iByte += segmentSize;
        }
    }
    *pBytesRemainingInSeg = oggbs->currentPageHeader.segmentTable[iSeg] - (uint8_t)(bytesConsumedInPage - iByte);
    return iSeg;
}
static int drflac_oggbs__seek_to_next_packet(drflac_oggbs* oggbs)
{
    for (;;) {
        int atEndOfPage = 0;
        uint8_t bytesRemainingInSeg;
        uint8_t iFirstSeg = drflac_oggbs__get_current_segment_index(oggbs, &bytesRemainingInSeg);
        uint32_t bytesToEndOfPacketOrPage = bytesRemainingInSeg;
        for (uint8_t iSeg = iFirstSeg; iSeg < oggbs->currentPageHeader.segmentCount; ++iSeg) {
            uint8_t segmentSize = oggbs->currentPageHeader.segmentTable[iSeg];
            if (segmentSize < 255) {
                if (iSeg == oggbs->currentPageHeader.segmentCount-1) {
                    atEndOfPage = 1;
                }
                break;
            }
            bytesToEndOfPacketOrPage += segmentSize;
        }
        drflac_oggbs__seek_physical(oggbs, bytesToEndOfPacketOrPage, DR_FLAC_SEEK_CUR);
        oggbs->bytesRemainingInPage -= bytesToEndOfPacketOrPage;
        if (atEndOfPage) {
            if (!drflac_oggbs__goto_next_page(oggbs)) {
                return 0;
            }
            if ((oggbs->currentPageHeader.headerType & 0x01) == 0) {
                return 1;
            }
        } else {
            return 1;
        }
    }
}
static int drflac_oggbs__seek_to_next_frame(drflac_oggbs* oggbs)
{
    return drflac_oggbs__seek_to_next_packet(oggbs);
}
#endif
static size_t drflac__on_read_ogg(void* pUserData, void* bufferOut, size_t bytesToRead)
{
    drflac_oggbs* oggbs = (drflac_oggbs*)pUserData;
    uint8_t* pRunningBufferOut = (uint8_t*)bufferOut;
    size_t bytesRead = 0;
    DR_FLAC_ASSERT(oggbs != NULL);
    DR_FLAC_ASSERT(pRunningBufferOut != NULL);
    while (bytesRead < bytesToRead) {
        size_t bytesRemainingToRead = bytesToRead - bytesRead;
        if (oggbs->bytesRemainingInPage >= bytesRemainingToRead) {
            DR_FLAC_COPY_MEMORY(pRunningBufferOut, oggbs->pageData + (oggbs->pageDataSize - oggbs->bytesRemainingInPage), bytesRemainingToRead);
            bytesRead += bytesRemainingToRead;
            oggbs->bytesRemainingInPage -= (uint32_t)bytesRemainingToRead;
            break;
        }
        if (oggbs->bytesRemainingInPage > 0) {
            DR_FLAC_COPY_MEMORY(pRunningBufferOut, oggbs->pageData + (oggbs->pageDataSize - oggbs->bytesRemainingInPage), oggbs->bytesRemainingInPage);
            bytesRead += oggbs->bytesRemainingInPage;
            pRunningBufferOut += oggbs->bytesRemainingInPage;
            oggbs->bytesRemainingInPage = 0;
        }
        DR_FLAC_ASSERT(bytesRemainingToRead > 0);
        if (!drflac_oggbs__goto_next_page(oggbs, drflac_ogg_recover_on_crc_mismatch)) {
            break;
        }
    }
    return bytesRead;
}
static int drflac__on_seek_ogg(void* pUserData, int offset, drflac_seek_origin origin)
{
    drflac_oggbs* oggbs = (drflac_oggbs*)pUserData;
    int bytesSeeked = 0;
    DR_FLAC_ASSERT(oggbs != NULL);
    DR_FLAC_ASSERT(offset >= 0);
    if (origin == DR_FLAC_SEEK_SET) {
        if (!drflac_oggbs__seek_physical(oggbs, (int)oggbs->firstBytePos, DR_FLAC_SEEK_SET)) {
            return 0;
        }
        if (!drflac_oggbs__goto_next_page(oggbs, drflac_ogg_fail_on_crc_mismatch)) {
            return 0;
        }
        return drflac__on_seek_ogg(pUserData, offset, DR_FLAC_SEEK_CUR);
    } else if (origin == DR_FLAC_SEEK_CUR) {
        while (bytesSeeked < offset) {
            int bytesRemainingToSeek = offset - bytesSeeked;
            DR_FLAC_ASSERT(bytesRemainingToSeek >= 0);
            if (oggbs->bytesRemainingInPage >= (size_t)bytesRemainingToSeek) {
                bytesSeeked += bytesRemainingToSeek;
                (void)bytesSeeked;
                oggbs->bytesRemainingInPage -= bytesRemainingToSeek;
                break;
            }
            if (oggbs->bytesRemainingInPage > 0) {
                bytesSeeked += (int)oggbs->bytesRemainingInPage;
                oggbs->bytesRemainingInPage = 0;
            }
            DR_FLAC_ASSERT(bytesRemainingToSeek > 0);
            if (!drflac_oggbs__goto_next_page(oggbs, drflac_ogg_fail_on_crc_mismatch)) {
                return 0;
            }
        }
    } else if (origin == DR_FLAC_SEEK_END) {
        return 0;
    }
    return 1;
}
static int drflac__on_tell_ogg(void* pUserData, int64_t* pCursor)
{
    (void)pUserData;
    (void)pCursor;
    return 0;
}
static int drflac_ogg__seek_to_pcm_frame(drflac* pFlac, uint64_t pcmFrameIndex)
{
    drflac_oggbs* oggbs = (drflac_oggbs*)pFlac->_oggbs;
    uint64_t originalBytePos;
    uint64_t runningGranulePosition;
    uint64_t runningFrameBytePos = 0;
    uint64_t runningPCMFrameCount = 0;
    int hasFramePage = 0;
    DR_FLAC_ASSERT(oggbs != NULL);
    originalBytePos = oggbs->currentBytePos;
    if (!drflac__seek_to_byte(&pFlac->bs, pFlac->firstFLACFramePosInBytes)) {
        return 0;
    }
    oggbs->bytesRemainingInPage = 0;
    runningGranulePosition = 0;
    for (;;) {
        if (!drflac_oggbs__goto_next_page(oggbs, drflac_ogg_recover_on_crc_mismatch)) {
            drflac_oggbs__seek_physical(oggbs, originalBytePos, DR_FLAC_SEEK_SET);
            return 0;
        }
        if ((oggbs->currentPageHeader.headerType & 0x01) == 0) {
            if (oggbs->currentPageHeader.segmentTable[0] >= 2) {
                uint8_t firstBytesInPage[2];
                firstBytesInPage[0] = oggbs->pageData[0];
                firstBytesInPage[1] = oggbs->pageData[1];
                if ((firstBytesInPage[0] == 0xFF) && (firstBytesInPage[1] & 0xFC) == 0xF8) {
                    runningFrameBytePos = oggbs->currentBytePos - drflac_ogg__get_page_header_size(&oggbs->currentPageHeader) - oggbs->pageDataSize;
                    runningPCMFrameCount = runningGranulePosition;
                    hasFramePage = 1;
                }
            }
        }
        if (oggbs->currentPageHeader.granulePosition != (uint64_t)-1) {
            if (hasFramePage && oggbs->currentPageHeader.granulePosition >= pcmFrameIndex) {
                break;
            }
            runningGranulePosition = oggbs->currentPageHeader.granulePosition;
        }
    }
    if (!drflac_oggbs__seek_physical(oggbs, runningFrameBytePos, DR_FLAC_SEEK_SET)) {
        return 0;
    }
    if (!drflac_oggbs__goto_next_page(oggbs, drflac_ogg_recover_on_crc_mismatch)) {
        return 0;
    }
    for (;;) {
        uint64_t firstPCMFrameInFLACFrame = 0;
        uint64_t lastPCMFrameInFLACFrame = 0;
        uint64_t pcmFrameCountInThisFrame;
        if (!drflac__read_next_flac_frame_header(&pFlac->bs, pFlac->bitsPerSample, &pFlac->currentFLACFrame.header)) {
            return 0;
        }
        drflac__get_pcm_frame_range_of_current_flac_frame(pFlac, &firstPCMFrameInFLACFrame, &lastPCMFrameInFLACFrame);
        pcmFrameCountInThisFrame = (lastPCMFrameInFLACFrame - firstPCMFrameInFLACFrame) + 1;
        if ((runningPCMFrameCount + pcmFrameCountInThisFrame) == pcmFrameIndex) {
            ma_result result = drflac__decode_flac_frame(pFlac);
            if (result == MA_SUCCESS) {
                pFlac->currentPCMFrame = pcmFrameIndex;
                pFlac->currentFLACFrame.pcmFramesRemaining = 0;
                return 1;
            } else {
                return 0;
            }
        }
        if (pcmFrameIndex < (runningPCMFrameCount + pcmFrameCountInThisFrame)) {
            ma_result result = drflac__decode_flac_frame(pFlac);
            if (result == MA_SUCCESS) {
                uint64_t pcmFramesToDecode = (size_t)(pcmFrameIndex - runningPCMFrameCount);
                if (pcmFramesToDecode == 0) {
                    return 1;
                }
                pFlac->currentPCMFrame = runningPCMFrameCount;
                return drflac__seek_forward_by_pcm_frames(pFlac, pcmFramesToDecode) == pcmFramesToDecode;
            } else {
                if (result == MA_CRC_MISMATCH) {
                    continue;
                } else {
                    return 0;
                }
            }
        } else {
            ma_result result = drflac__seek_to_next_flac_frame(pFlac);
            if (result == MA_SUCCESS) {
                runningPCMFrameCount += pcmFrameCountInThisFrame;
            } else {
                if (result == MA_CRC_MISMATCH) {
                    continue;
                } else {
                    return 0;
                }
            }
        }
    }
}
static int drflac__init_private__ogg(drflac_init_info* pInit, drflac_read_proc onRead, drflac_seek_proc onSeek, drflac_meta_proc onMeta, void* pUserData, void* pUserDataMD, int relaxed)
{
    drflac_ogg_page_header header;
    uint32_t crc32 = DR_FLAC_OGG_CAPTURE_PATTERN_CRC32;
    uint32_t bytesRead = 0;
    (void)relaxed;
    pInit->container = drflac_container_ogg;
    pInit->oggFirstBytePos = 0;
    if (drflac_ogg__read_page_header_after_capture_pattern(onRead, pUserData, &header, &bytesRead, &crc32) != MA_SUCCESS) {
        return 0;
    }
    pInit->runningFilePos += bytesRead;
    for (;;) {
        int pageBodySize;
        if ((header.headerType & 0x02) == 0) {
            return 0;
        }
        pageBodySize = drflac_ogg__get_page_body_size(&header);
        if (pageBodySize == 51) {
            uint32_t bytesRemainingInPage = pageBodySize;
            uint8_t packetType;
            if (onRead(pUserData, &packetType, 1) != 1) {
                return 0;
            }
            bytesRemainingInPage -= 1;
            if (packetType == 0x7F) {
                uint8_t sig[4];
                if (onRead(pUserData, sig, 4) != 4) {
                    return 0;
                }
                bytesRemainingInPage -= 4;
                if (sig[0] == 'F' && sig[1] == 'L' && sig[2] == 'A' && sig[3] == 'C') {
                    uint8_t mappingVersion[2];
                    if (onRead(pUserData, mappingVersion, 2) != 2) {
                        return 0;
                    }
                    if (mappingVersion[0] != 1) {
                        return 0;
                    }
                    if (!onSeek(pUserData, 2, DR_FLAC_SEEK_CUR)) {
                        return 0;
                    }
                    if (onRead(pUserData, sig, 4) != 4) {
                        return 0;
                    }
                    if (sig[0] == 'f' && sig[1] == 'L' && sig[2] == 'a' && sig[3] == 'C') {
                        drflac_streaminfo streaminfo;
                        uint8_t isLastBlock;
                        uint8_t blockType;
                        uint32_t blockSize;
                        if (!drflac__read_and_decode_block_header(onRead, pUserData, &isLastBlock, &blockType, &blockSize)) {
                            return 0;
                        }
                        if (blockType != DR_FLAC_METADATA_BLOCK_TYPE_STREAMINFO || blockSize != 34) {
                            return 0;
                        }
                        if (drflac__read_streaminfo(onRead, pUserData, &streaminfo)) {
                            pInit->hasStreamInfoBlock      = 1;
                            pInit->sampleRate              = streaminfo.sampleRate;
                            pInit->channels                = streaminfo.channels;
                            pInit->bitsPerSample           = streaminfo.bitsPerSample;
                            pInit->totalPCMFrameCount      = streaminfo.totalPCMFrameCount;
                            pInit->maxBlockSizeInPCMFrames = streaminfo.maxBlockSizeInPCMFrames;
                            pInit->hasMetadataBlocks       = !isLastBlock;
                            if (onMeta) {
                                drflac_metadata metadata;
                                metadata.type = DR_FLAC_METADATA_BLOCK_TYPE_STREAMINFO;
                                metadata.pRawData = NULL;
                                metadata.rawDataSize = 0;
                                metadata.data.streaminfo = streaminfo;
                                onMeta(pUserDataMD, &metadata);
                            }
                            pInit->runningFilePos  += pageBodySize;
                            pInit->oggFirstBytePos  = pInit->runningFilePos - 79;
                            pInit->oggSerial        = header.serialNumber;
                            pInit->oggBosHeader     = header;
                            break;
                        } else {
                            return 0;
                        }
                    } else {
                        return 0;
                    }
                } else {
                    if (!onSeek(pUserData, bytesRemainingInPage, DR_FLAC_SEEK_CUR)) {
                        return 0;
                    }
                }
            } else {
                if (!onSeek(pUserData, bytesRemainingInPage, DR_FLAC_SEEK_CUR)) {
                    return 0;
                }
            }
        } else {
            if (!onSeek(pUserData, pageBodySize, DR_FLAC_SEEK_CUR)) {
                return 0;
            }
        }
        pInit->runningFilePos += pageBodySize;
        if (drflac_ogg__read_page_header(onRead, pUserData, &header, &bytesRead, &crc32) != MA_SUCCESS) {
            return 0;
        }
        pInit->runningFilePos += bytesRead;
    }
    pInit->hasMetadataBlocks = 1;
    return 1;
}
#endif
static int drflac__init_private(drflac_init_info* pInit, drflac_read_proc onRead, drflac_seek_proc onSeek, drflac_tell_proc onTell, drflac_meta_proc onMeta, drflac_container container, void* pUserData, void* pUserDataMD)
{
    int relaxed;
    uint8_t id[4];
    if (pInit == NULL || onRead == NULL || onSeek == NULL) {
        return 0;
    }
    DR_FLAC_ZERO_MEMORY(pInit, sizeof(*pInit));
    pInit->onRead       = onRead;
    pInit->onSeek       = onSeek;
    pInit->onTell       = onTell;
    pInit->onMeta       = onMeta;
    pInit->container    = container;
    pInit->pUserData    = pUserData;
    pInit->pUserDataMD  = pUserDataMD;
    pInit->bs.onRead    = onRead;
    pInit->bs.onSeek    = onSeek;
    pInit->bs.onTell    = onTell;
    pInit->bs.pUserData = pUserData;
    drflac__reset_cache(&pInit->bs);
    relaxed = container != drflac_container_unknown;
    for (;;) {
        if (onRead(pUserData, id, 4) != 4) {
            return 0;
        }
        pInit->runningFilePos += 4;
        if (id[0] == 'I' && id[1] == 'D' && id[2] == '3') {
            uint8_t header[6];
            uint8_t flags;
            uint32_t headerSize;
            if (onRead(pUserData, header, 6) != 6) {
                return 0;
            }
            pInit->runningFilePos += 6;
            flags = header[1];
            DR_FLAC_COPY_MEMORY(&headerSize, header+2, 4);
            headerSize = drflac__unsynchsafe_32(drflac__be2host_32(headerSize));
            if (flags & 0x10) {
                headerSize += 10;
            }
            if (!onSeek(pUserData, headerSize, DR_FLAC_SEEK_CUR)) {
                return 0;
            }
            pInit->runningFilePos += headerSize;
        } else {
            break;
        }
    }
    if (id[0] == 'f' && id[1] == 'L' && id[2] == 'a' && id[3] == 'C') {
        return drflac__init_private__native(pInit, onRead, onSeek, onMeta, pUserData, pUserDataMD, relaxed);
    }
#ifndef DR_FLAC_NO_OGG
    if (id[0] == 'O' && id[1] == 'g' && id[2] == 'g' && id[3] == 'S') {
        return drflac__init_private__ogg(pInit, onRead, onSeek, onMeta, pUserData, pUserDataMD, relaxed);
    }
#endif
    if (relaxed) {
        if (container == drflac_container_native) {
            return drflac__init_private__native_without_streaminfo(pInit, id, pInit->runningFilePos - 4);
        }
#ifndef DR_FLAC_NO_OGG
        if (container == drflac_container_ogg) {
            return drflac__init_private__ogg(pInit, onRead, onSeek, onMeta, pUserData, pUserDataMD, relaxed);
        }
#endif
    }
    return 0;
}
static void drflac__init_from_info(drflac* pFlac, const drflac_init_info* pInit)
{
    DR_FLAC_ASSERT(pFlac != NULL);
    DR_FLAC_ASSERT(pInit != NULL);
    DR_FLAC_ZERO_MEMORY(pFlac, sizeof(*pFlac));
    pFlac->bs                      = pInit->bs;
    pFlac->onMeta                  = pInit->onMeta;
    pFlac->pUserDataMD             = pInit->pUserDataMD;
    pFlac->maxBlockSizeInPCMFrames = pInit->maxBlockSizeInPCMFrames;
    pFlac->decodedSamplesCapacityInPCMFrames = pInit->maxBlockSizeInPCMFrames;
    pFlac->sampleRate              = pInit->sampleRate;
    pFlac->channels                = (uint8_t)pInit->channels;
    pFlac->bitsPerSample           = (uint8_t)pInit->bitsPerSample;
    pFlac->totalPCMFrameCount      = pInit->totalPCMFrameCount;
    pFlac->container               = pInit->container;
}
static drflac* drflac_open_with_metadata_private(drflac_read_proc onRead, drflac_seek_proc onSeek, drflac_tell_proc onTell, drflac_meta_proc onMeta, drflac_container container, void* pUserData, void* pUserDataMD, const ma_allocation_callbacks* pAllocationCallbacks)
{
    drflac_init_info init;
    uint32_t allocationSize;
    uint32_t decodedSampleSize;
    uint32_t wholeSIMDVectorCountPerChannel;
    uint32_t decodedSamplesAllocationSize;
    uint8_t* pDecodedSamples;
#ifndef DR_FLAC_NO_OGG
    drflac_oggbs* pOggbs = NULL;
#endif
    uint64_t firstFramePos;
    uint64_t seektablePos;
    uint32_t seekpointCount;
    int recoverMetadata = 0;
    ma_allocation_callbacks allocationCallbacks;
    drflac* pFlac;
    drflac__init_cpu_caps();
    if (!drflac__init_private(&init, onRead, onSeek, onTell, onMeta, container, pUserData, pUserDataMD)) {
        return NULL;
    }
    if (pAllocationCallbacks != NULL) {
        allocationCallbacks = *pAllocationCallbacks;
        if (allocationCallbacks.onFree == NULL || (allocationCallbacks.onMalloc == NULL && allocationCallbacks.onRealloc == NULL)) {
            return NULL;
        }
    } else {
        allocationCallbacks.pUserData = NULL;
        allocationCallbacks.onMalloc  = drflac__malloc_default;
        allocationCallbacks.onRealloc = drflac__realloc_default;
        allocationCallbacks.onFree    = drflac__free_default;
    }
    allocationSize = sizeof(drflac);
    decodedSampleSize = sizeof(int32_t);
    if (init.bitsPerSample == 32) {
        decodedSampleSize = sizeof(int64_t);
    }
    if ((init.maxBlockSizeInPCMFrames % (DR_FLAC_MAX_SIMD_VECTOR_SIZE / decodedSampleSize)) == 0) {
        wholeSIMDVectorCountPerChannel = (init.maxBlockSizeInPCMFrames / (DR_FLAC_MAX_SIMD_VECTOR_SIZE / decodedSampleSize));
    } else {
        wholeSIMDVectorCountPerChannel = (init.maxBlockSizeInPCMFrames / (DR_FLAC_MAX_SIMD_VECTOR_SIZE / decodedSampleSize)) + 1;
    }
    decodedSamplesAllocationSize = wholeSIMDVectorCountPerChannel * DR_FLAC_MAX_SIMD_VECTOR_SIZE * init.channels;
    allocationSize += decodedSamplesAllocationSize;
    allocationSize += DR_FLAC_MAX_SIMD_VECTOR_SIZE;
#ifndef DR_FLAC_NO_OGG
    if (init.container == drflac_container_ogg) {
        allocationSize += sizeof(drflac_oggbs);
        pOggbs = (drflac_oggbs*)drflac__malloc_from_callbacks(sizeof(*pOggbs), &allocationCallbacks);
        if (pOggbs == NULL) {
            return NULL;
        }
        DR_FLAC_ZERO_MEMORY(pOggbs, sizeof(*pOggbs));
        pOggbs->onRead = onRead;
        pOggbs->onSeek = onSeek;
        pOggbs->onTell = onTell;
        pOggbs->pUserData = pUserData;
        pOggbs->currentBytePos = init.oggFirstBytePos;
        pOggbs->firstBytePos = init.oggFirstBytePos;
        pOggbs->serialNumber = init.oggSerial;
        pOggbs->bosPageHeader = init.oggBosHeader;
        pOggbs->bytesRemainingInPage = 0;
    }
#endif
    firstFramePos  = 42;
    if (!init.hasStreamInfoBlock) {
        firstFramePos = init.firstFrameSearchPos;
    }
    seektablePos   = 0;
    seekpointCount = 0;
    if (init.hasMetadataBlocks) {
        ma_result metadataResult;
        drflac_read_proc onReadOverride = onRead;
        drflac_seek_proc onSeekOverride = onSeek;
        drflac_tell_proc onTellOverride = onTell;
        void* pUserDataOverride = pUserData;
#ifndef DR_FLAC_NO_OGG
        if (init.container == drflac_container_ogg) {
            onReadOverride = drflac__on_read_ogg;
            onSeekOverride = drflac__on_seek_ogg;
            onTellOverride = drflac__on_tell_ogg;
            pUserDataOverride = (void*)pOggbs;
        }
#endif
        metadataResult = drflac__read_and_decode_metadata(onReadOverride, onSeekOverride, onTellOverride, onMeta, pUserDataOverride, pUserDataMD, &firstFramePos, &seektablePos, &seekpointCount, &allocationCallbacks);
        if (metadataResult != MA_SUCCESS) {
            if (metadataResult == MA_INVALID_DATA && init.container == drflac_container_native && onSeek != NULL) {
                firstFramePos   = 42;
                seektablePos    = 0;
                seekpointCount  = 0;
                recoverMetadata = 1;
            } else {
        #ifndef DR_FLAC_NO_OGG
                drflac__free_from_callbacks(pOggbs, &allocationCallbacks);
        #endif
                return NULL;
            }
        }
        if ((0xFFFFFFFF - (seekpointCount * sizeof(drflac_seekpoint))) < allocationSize) {
        #ifndef DR_FLAC_NO_OGG
            drflac__free_from_callbacks(pOggbs, &allocationCallbacks);
        #endif
            return NULL;
        }
        allocationSize += seekpointCount * sizeof(drflac_seekpoint);
    }
    pFlac = (drflac*)drflac__malloc_from_callbacks((size_t)allocationSize, &allocationCallbacks);
    if (pFlac == NULL) {
    #ifndef DR_FLAC_NO_OGG
        drflac__free_from_callbacks(pOggbs, &allocationCallbacks);
    #endif
        return NULL;
    }
    drflac__init_from_info(pFlac, &init);
    pFlac->allocationCallbacks = allocationCallbacks;
    pDecodedSamples = (uint8_t*)drflac_align((size_t)pFlac->pExtraData, DR_FLAC_MAX_SIMD_VECTOR_SIZE);
    if (init.bitsPerSample != 32) {
        pFlac->decodedSamples.pS32 = (int32_t*)pDecodedSamples;
    } else {
        pFlac->decodedSamples.pS64 = (int64_t*)pDecodedSamples;
    }
#ifndef DR_FLAC_NO_OGG
    if (init.container == drflac_container_ogg) {
        drflac_oggbs* pInternalOggbs = (drflac_oggbs*)(pDecodedSamples + decodedSamplesAllocationSize + (seekpointCount * sizeof(drflac_seekpoint)));
        DR_FLAC_COPY_MEMORY(pInternalOggbs, pOggbs, sizeof(*pOggbs));
        drflac__free_from_callbacks(pOggbs, &allocationCallbacks);
        pOggbs = NULL;
        pFlac->bs.onRead = drflac__on_read_ogg;
        pFlac->bs.onSeek = drflac__on_seek_ogg;
        pFlac->bs.onTell = drflac__on_tell_ogg;
        pFlac->bs.pUserData = (void*)pInternalOggbs;
        pFlac->_oggbs = (void*)pInternalOggbs;
    }
#endif
    pFlac->firstFLACFramePosInBytes = firstFramePos;
#ifndef DR_FLAC_NO_OGG
    if (init.container == drflac_container_ogg)
    {
        pFlac->pSeekpoints = NULL;
        pFlac->seekpointCount = 0;
    }
    else
#endif
    {
        if (seektablePos != 0) {
            pFlac->seekpointCount = seekpointCount;
            pFlac->pSeekpoints = (drflac_seekpoint*)(pDecodedSamples + decodedSamplesAllocationSize);
            DR_FLAC_ASSERT(pFlac->bs.onSeek != NULL);
            DR_FLAC_ASSERT(pFlac->bs.onRead != NULL);
            if (pFlac->bs.onSeek(pFlac->bs.pUserData, (int)seektablePos, DR_FLAC_SEEK_SET)) {
                uint32_t iSeekpoint;
                for (iSeekpoint = 0; iSeekpoint < seekpointCount; iSeekpoint += 1) {
                    if (pFlac->bs.onRead(pFlac->bs.pUserData, pFlac->pSeekpoints + iSeekpoint, DR_FLAC_SEEKPOINT_SIZE_IN_BYTES) == DR_FLAC_SEEKPOINT_SIZE_IN_BYTES) {
                        pFlac->pSeekpoints[iSeekpoint].firstPCMFrame   = drflac__be2host_64(pFlac->pSeekpoints[iSeekpoint].firstPCMFrame);
                        pFlac->pSeekpoints[iSeekpoint].flacFrameOffset = drflac__be2host_64(pFlac->pSeekpoints[iSeekpoint].flacFrameOffset);
                        pFlac->pSeekpoints[iSeekpoint].pcmFrameCount   = drflac__be2host_16(pFlac->pSeekpoints[iSeekpoint].pcmFrameCount);
                    } else {
                        pFlac->pSeekpoints = NULL;
                        pFlac->seekpointCount = 0;
                        break;
                    }
                }
                if (!pFlac->bs.onSeek(pFlac->bs.pUserData, (int)pFlac->firstFLACFramePosInBytes, DR_FLAC_SEEK_SET)) {
                    drflac__free_from_callbacks(pFlac->pDecodedSamplesAllocation, &allocationCallbacks);
                    drflac__free_from_callbacks(pFlac, &allocationCallbacks);
                    return NULL;
                }
            } else {
                pFlac->pSeekpoints = NULL;
                pFlac->seekpointCount = 0;
            }
        }
    }
    if (recoverMetadata) {
        if (!drflac__seek_to_byte(&pFlac->bs, firstFramePos) || !drflac__read_and_decode_next_flac_frame(pFlac)) {
            drflac__free_from_callbacks(pFlac->pDecodedSamplesAllocation, &allocationCallbacks);
            drflac__free_from_callbacks(pFlac, &allocationCallbacks);
            return NULL;
        }
    } else if (!init.hasStreamInfoBlock) {
        pFlac->currentFLACFrame.header = init.firstFrameHeader;
        for (;;) {
            ma_result result = drflac__decode_flac_frame(pFlac);
            if (result == MA_SUCCESS) {
                break;
            } else {
                if (result == MA_CRC_MISMATCH) {
                    if (!drflac__read_next_flac_frame_header(&pFlac->bs, pFlac->bitsPerSample, &pFlac->currentFLACFrame.header)) {
                        drflac__free_from_callbacks(pFlac->pDecodedSamplesAllocation, &allocationCallbacks);
                        drflac__free_from_callbacks(pFlac, &allocationCallbacks);
                        return NULL;
                    }
                    continue;
                } else {
                    drflac__free_from_callbacks(pFlac->pDecodedSamplesAllocation, &allocationCallbacks);
                    drflac__free_from_callbacks(pFlac, &allocationCallbacks);
                    return NULL;
                }
            }
        }
    }
    return pFlac;
}
#ifndef DR_FLAC_NO_STDIO
#include <stdio.h>
#ifndef DR_FLAC_NO_WCHAR
#include <wchar.h>
#endif
static size_t drflac__on_read_stdio(void* pUserData, void* bufferOut, size_t bytesToRead)
{
    return fread(bufferOut, 1, bytesToRead, (FILE*)pUserData);
}
static int drflac__on_seek_stdio(void* pUserData, int offset, drflac_seek_origin origin)
{
    int whence = SEEK_SET;
    if (origin == DR_FLAC_SEEK_CUR) {
        whence = SEEK_CUR;
    } else if (origin == DR_FLAC_SEEK_END) {
        whence = SEEK_END;
    }
    return fseek((FILE*)pUserData, offset, whence) == 0;
}
static int drflac__on_tell_stdio(void* pUserData, int64_t* pCursor)
{
    FILE* pFileStdio = (FILE*)pUserData;
    int64_t result;
    DR_FLAC_ASSERT(pFileStdio != NULL);
    DR_FLAC_ASSERT(pCursor    != NULL);
#if defined(_WIN32) && !defined(NXDK)
    #if defined(_MSC_VER) && _MSC_VER > 1200
        result = _ftelli64(pFileStdio);
    #else
        result = ftell(pFileStdio);
    #endif
#else
    result = ftell(pFileStdio);
#endif
    *pCursor = result;
    return 1;
}
DRFLAC_API drflac* drflac_open_file(const char* pFileName, const ma_allocation_callbacks* pAllocationCallbacks)
{
    drflac* pFlac;
    FILE* pFile;
    if (ma_fopen(&pFile, pFileName, "rb") != MA_SUCCESS) {
        return NULL;
    }
    pFlac = drflac_open(drflac__on_read_stdio, drflac__on_seek_stdio, drflac__on_tell_stdio, (void*)pFile, pAllocationCallbacks);
    if (pFlac == NULL) {
        fclose(pFile);
        return NULL;
    }
    return pFlac;
}
#ifndef DR_FLAC_NO_WCHAR
DRFLAC_API drflac* drflac_open_file_w(const wchar_t* pFileName, const ma_allocation_callbacks* pAllocationCallbacks)
{
    drflac* pFlac;
    FILE* pFile;
    if (ma_wfopen(&pFile, pFileName, L"rb", pAllocationCallbacks) != MA_SUCCESS) {
        return NULL;
    }
    pFlac = drflac_open(drflac__on_read_stdio, drflac__on_seek_stdio, drflac__on_tell_stdio, (void*)pFile, pAllocationCallbacks);
    if (pFlac == NULL) {
        fclose(pFile);
        return NULL;
    }
    return pFlac;
}
#endif
DRFLAC_API drflac* drflac_open_file_with_metadata(const char* pFileName, drflac_meta_proc onMeta, void* pUserData, const ma_allocation_callbacks* pAllocationCallbacks)
{
    drflac* pFlac;
    FILE* pFile;
    if (ma_fopen(&pFile, pFileName, "rb") != MA_SUCCESS) {
        return NULL;
    }
    pFlac = drflac_open_with_metadata_private(drflac__on_read_stdio, drflac__on_seek_stdio, drflac__on_tell_stdio, onMeta, drflac_container_unknown, (void*)pFile, pUserData, pAllocationCallbacks);
    if (pFlac == NULL) {
        fclose(pFile);
        return pFlac;
    }
    return pFlac;
}
#ifndef DR_FLAC_NO_WCHAR
DRFLAC_API drflac* drflac_open_file_with_metadata_w(const wchar_t* pFileName, drflac_meta_proc onMeta, void* pUserData, const ma_allocation_callbacks* pAllocationCallbacks)
{
    drflac* pFlac;
    FILE* pFile;
    if (ma_wfopen(&pFile, pFileName, L"rb", pAllocationCallbacks) != MA_SUCCESS) {
        return NULL;
    }
    pFlac = drflac_open_with_metadata_private(drflac__on_read_stdio, drflac__on_seek_stdio, drflac__on_tell_stdio, onMeta, drflac_container_unknown, (void*)pFile, pUserData, pAllocationCallbacks);
    if (pFlac == NULL) {
        fclose(pFile);
        return pFlac;
    }
    return pFlac;
}
#endif
#endif
static size_t drflac__on_read_memory(void* pUserData, void* bufferOut, size_t bytesToRead)
{
    drflac__memory_stream* memoryStream = (drflac__memory_stream*)pUserData;
    size_t bytesRemaining;
    DR_FLAC_ASSERT(memoryStream != NULL);
    DR_FLAC_ASSERT(memoryStream->dataSize >= memoryStream->currentReadPos);
    bytesRemaining = memoryStream->dataSize - memoryStream->currentReadPos;
    if (bytesToRead > bytesRemaining) {
        bytesToRead = bytesRemaining;
    }
    if (bytesToRead > 0) {
        DR_FLAC_COPY_MEMORY(bufferOut, memoryStream->data + memoryStream->currentReadPos, bytesToRead);
        memoryStream->currentReadPos += bytesToRead;
    }
    return bytesToRead;
}
static int drflac__on_seek_memory(void* pUserData, int offset, drflac_seek_origin origin)
{
    drflac__memory_stream* memoryStream = (drflac__memory_stream*)pUserData;
    int64_t newCursor;
    DR_FLAC_ASSERT(memoryStream != NULL);
    if (origin == DR_FLAC_SEEK_SET) {
        newCursor = 0;
    } else if (origin == DR_FLAC_SEEK_CUR) {
        newCursor = (int64_t)memoryStream->currentReadPos;
    } else if (origin == DR_FLAC_SEEK_END) {
        newCursor = (int64_t)memoryStream->dataSize;
    } else {
        DR_FLAC_ASSERT(!"Invalid seek origin");
        return 0;
    }
    newCursor += offset;
    if (newCursor < 0) {
        return 0;
    }
    if ((size_t)newCursor > memoryStream->dataSize) {
        return 0;
    }
    memoryStream->currentReadPos = (size_t)newCursor;
    return 1;
}
static int drflac__on_tell_memory(void* pUserData, int64_t* pCursor)
{
    drflac__memory_stream* memoryStream = (drflac__memory_stream*)pUserData;
    DR_FLAC_ASSERT(memoryStream != NULL);
    DR_FLAC_ASSERT(pCursor != NULL);
    *pCursor = (int64_t)memoryStream->currentReadPos;
    return 1;
}
DRFLAC_API drflac* drflac_open_memory(const void* pData, size_t dataSize, const ma_allocation_callbacks* pAllocationCallbacks)
{
    drflac__memory_stream memoryStream;
    drflac* pFlac;
    memoryStream.data = (const uint8_t*)pData;
    memoryStream.dataSize = dataSize;
    memoryStream.currentReadPos = 0;
    pFlac = drflac_open(drflac__on_read_memory, drflac__on_seek_memory, drflac__on_tell_memory, &memoryStream, pAllocationCallbacks);
    if (pFlac == NULL) {
        return NULL;
    }
    pFlac->memoryStream = memoryStream;
#ifndef DR_FLAC_NO_OGG
    if (pFlac->container == drflac_container_ogg)
    {
        drflac_oggbs* oggbs = (drflac_oggbs*)pFlac->_oggbs;
        oggbs->pUserData = &pFlac->memoryStream;
    }
    else
#endif
    {
        pFlac->bs.pUserData = &pFlac->memoryStream;
    }
    return pFlac;
}
DRFLAC_API drflac* drflac_open_memory_with_metadata(const void* pData, size_t dataSize, drflac_meta_proc onMeta, void* pUserData, const ma_allocation_callbacks* pAllocationCallbacks)
{
    drflac__memory_stream memoryStream;
    drflac* pFlac;
    memoryStream.data = (const uint8_t*)pData;
    memoryStream.dataSize = dataSize;
    memoryStream.currentReadPos = 0;
    pFlac = drflac_open_with_metadata_private(drflac__on_read_memory, drflac__on_seek_memory, drflac__on_tell_memory, onMeta, drflac_container_unknown, &memoryStream, pUserData, pAllocationCallbacks);
    if (pFlac == NULL) {
        return NULL;
    }
    pFlac->memoryStream = memoryStream;
#ifndef DR_FLAC_NO_OGG
    if (pFlac->container == drflac_container_ogg)
    {
        drflac_oggbs* oggbs = (drflac_oggbs*)pFlac->_oggbs;
        oggbs->pUserData = &pFlac->memoryStream;
    }
    else
#endif
    {
        pFlac->bs.pUserData = &pFlac->memoryStream;
    }
    return pFlac;
}
DRFLAC_API drflac* drflac_open(drflac_read_proc onRead, drflac_seek_proc onSeek, drflac_tell_proc onTell, void* pUserData, const ma_allocation_callbacks* pAllocationCallbacks)
{
    return drflac_open_with_metadata_private(onRead, onSeek, onTell, NULL, drflac_container_unknown, pUserData, pUserData, pAllocationCallbacks);
}
DRFLAC_API drflac* drflac_open_relaxed(drflac_read_proc onRead, drflac_seek_proc onSeek, drflac_tell_proc onTell, drflac_container container, void* pUserData, const ma_allocation_callbacks* pAllocationCallbacks)
{
    return drflac_open_with_metadata_private(onRead, onSeek, onTell, NULL, container, pUserData, pUserData, pAllocationCallbacks);
}
DRFLAC_API drflac* drflac_open_with_metadata(drflac_read_proc onRead, drflac_seek_proc onSeek, drflac_tell_proc onTell, drflac_meta_proc onMeta, void* pUserData, const ma_allocation_callbacks* pAllocationCallbacks)
{
    return drflac_open_with_metadata_private(onRead, onSeek, onTell, onMeta, drflac_container_unknown, pUserData, pUserData, pAllocationCallbacks);
}
DRFLAC_API drflac* drflac_open_with_metadata_relaxed(drflac_read_proc onRead, drflac_seek_proc onSeek, drflac_tell_proc onTell, drflac_meta_proc onMeta, drflac_container container, void* pUserData, const ma_allocation_callbacks* pAllocationCallbacks)
{
    return drflac_open_with_metadata_private(onRead, onSeek, onTell, onMeta, container, pUserData, pUserData, pAllocationCallbacks);
}
DRFLAC_API void drflac_close(drflac* pFlac)
{
    if (pFlac == NULL) {
        return;
    }
#ifndef DR_FLAC_NO_STDIO
    if (pFlac->bs.onRead == drflac__on_read_stdio) {
        fclose((FILE*)pFlac->bs.pUserData);
    }
#ifndef DR_FLAC_NO_OGG
    if (pFlac->container == drflac_container_ogg) {
        drflac_oggbs* oggbs = (drflac_oggbs*)pFlac->_oggbs;
        DR_FLAC_ASSERT(pFlac->bs.onRead == drflac__on_read_ogg);
        if (oggbs->onRead == drflac__on_read_stdio) {
            fclose((FILE*)oggbs->pUserData);
        }
    }
#endif
#endif
    drflac__free_from_callbacks(pFlac->pDecodedSamplesAllocation, &pFlac->allocationCallbacks);
    drflac__free_from_callbacks(pFlac, &pFlac->allocationCallbacks);
}
#if 0
static DRFLAC_INLINE void drflac_read_pcm_frames_s32__decode_left_side__reference(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int32_t* pOutputSamples)
{
    uint64_t i;
    for (i = 0; i < frameCount; ++i) {
        uint32_t left  = (uint32_t)pInputSamples0[i] << (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample);
        uint32_t side  = (uint32_t)pInputSamples1[i] << (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample);
        uint32_t right = left - side;
        pOutputSamples[i*2+0] = (int32_t)left;
        pOutputSamples[i*2+1] = (int32_t)right;
    }
}
#endif
static DRFLAC_INLINE void drflac_read_pcm_frames_s32__decode_left_side__scalar(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int32_t* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
    uint32_t shift1 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
    for (i = 0; i < frameCount4; ++i) {
        uint32_t left0 = pInputSamples0U32[i*4+0] << shift0;
        uint32_t left1 = pInputSamples0U32[i*4+1] << shift0;
        uint32_t left2 = pInputSamples0U32[i*4+2] << shift0;
        uint32_t left3 = pInputSamples0U32[i*4+3] << shift0;
        uint32_t side0 = pInputSamples1U32[i*4+0] << shift1;
        uint32_t side1 = pInputSamples1U32[i*4+1] << shift1;
        uint32_t side2 = pInputSamples1U32[i*4+2] << shift1;
        uint32_t side3 = pInputSamples1U32[i*4+3] << shift1;
        uint32_t right0 = left0 - side0;
        uint32_t right1 = left1 - side1;
        uint32_t right2 = left2 - side2;
        uint32_t right3 = left3 - side3;
        pOutputSamples[i*8+0] = (int32_t)left0;
        pOutputSamples[i*8+1] = (int32_t)right0;
        pOutputSamples[i*8+2] = (int32_t)left1;
        pOutputSamples[i*8+3] = (int32_t)right1;
        pOutputSamples[i*8+4] = (int32_t)left2;
        pOutputSamples[i*8+5] = (int32_t)right2;
        pOutputSamples[i*8+6] = (int32_t)left3;
        pOutputSamples[i*8+7] = (int32_t)right3;
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        uint32_t left  = pInputSamples0U32[i] << shift0;
        uint32_t side  = pInputSamples1U32[i] << shift1;
        uint32_t right = left - side;
        pOutputSamples[i*2+0] = (int32_t)left;
        pOutputSamples[i*2+1] = (int32_t)right;
    }
}
#if defined(DR_FLAC_SUPPORT_SSE2)
static DRFLAC_INLINE void drflac_read_pcm_frames_s32__decode_left_side__sse2(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int32_t* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
    uint32_t shift1 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
    DR_FLAC_ASSERT(pFlac->currentFLACFrame.header.bitsPerSample <= 24);
    for (i = 0; i < frameCount4; ++i) {
        __m128i left  = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples0 + i), shift0);
        __m128i side  = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples1 + i), shift1);
        __m128i right = _mm_sub_epi32(left, side);
        _mm_storeu_si128((__m128i*)(pOutputSamples + i*8 + 0), _mm_unpacklo_epi32(left, right));
        _mm_storeu_si128((__m128i*)(pOutputSamples + i*8 + 4), _mm_unpackhi_epi32(left, right));
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        uint32_t left  = pInputSamples0U32[i] << shift0;
        uint32_t side  = pInputSamples1U32[i] << shift1;
        uint32_t right = left - side;
        pOutputSamples[i*2+0] = (int32_t)left;
        pOutputSamples[i*2+1] = (int32_t)right;
    }
}
#endif
#if defined(DR_FLAC_SUPPORT_NEON)
static DRFLAC_INLINE void drflac_read_pcm_frames_s32__decode_left_side__neon(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int32_t* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
    uint32_t shift1 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
    int32x4_t shift0_4;
    int32x4_t shift1_4;
    DR_FLAC_ASSERT(pFlac->currentFLACFrame.header.bitsPerSample <= 24);
    shift0_4 = vdupq_n_s32(shift0);
    shift1_4 = vdupq_n_s32(shift1);
    for (i = 0; i < frameCount4; ++i) {
        uint32x4_t left;
        uint32x4_t side;
        uint32x4_t right;
        left  = vshlq_u32(vld1q_u32(pInputSamples0U32 + i*4), shift0_4);
        side  = vshlq_u32(vld1q_u32(pInputSamples1U32 + i*4), shift1_4);
        right = vsubq_u32(left, side);
        drflac__vst2q_u32((uint32_t*)pOutputSamples + i*8, vzipq_u32(left, right));
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        uint32_t left  = pInputSamples0U32[i] << shift0;
        uint32_t side  = pInputSamples1U32[i] << shift1;
        uint32_t right = left - side;
        pOutputSamples[i*2+0] = (int32_t)left;
        pOutputSamples[i*2+1] = (int32_t)right;
    }
}
#endif
static DRFLAC_INLINE void drflac_read_pcm_frames_s32__decode_duplicate_mono(uint64_t frameCount, uint32_t shift, const int32_t* pInputSamples, int32_t* pOutputSamples)
{
    uint64_t i;
    const uint32_t* pInputSamplesU32 = (const uint32_t*)pInputSamples;
    DR_FLAC_ASSERT(shift < 32);
    for (i = 0; i < frameCount; ++i) {
        int32_t sample;
        sample = (int32_t)(pInputSamplesU32[i] << shift);
        pOutputSamples[i*2+0] = sample;
        pOutputSamples[i*2+1] = sample;
    }
}
static DRFLAC_INLINE void drflac_read_pcm_frames_s32__decode_left_side(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int32_t* pOutputSamples)
{
    if (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample == 32) {
        drflac_read_pcm_frames_s32__decode_duplicate_mono(frameCount, unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample, pInputSamples0, pOutputSamples);
        return;
    }
#if defined(DR_FLAC_SUPPORT_SSE2)
    if (drflac__gIsSSE2Supported && pFlac->currentFLACFrame.header.bitsPerSample <= 24) {
        drflac_read_pcm_frames_s32__decode_left_side__sse2(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
    } else
#elif defined(DR_FLAC_SUPPORT_NEON)
    if (drflac__gIsNEONSupported && pFlac->currentFLACFrame.header.bitsPerSample <= 24) {
        drflac_read_pcm_frames_s32__decode_left_side__neon(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
    } else
#endif
    {
#if 0
        drflac_read_pcm_frames_s32__decode_left_side__reference(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
#else
        drflac_read_pcm_frames_s32__decode_left_side__scalar(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
#endif
    }
}
#if 0
static DRFLAC_INLINE void drflac_read_pcm_frames_s32__decode_right_side__reference(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int32_t* pOutputSamples)
{
    uint64_t i;
    for (i = 0; i < frameCount; ++i) {
        uint32_t side  = (uint32_t)pInputSamples0[i] << (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample);
        uint32_t right = (uint32_t)pInputSamples1[i] << (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample);
        uint32_t left  = right + side;
        pOutputSamples[i*2+0] = (int32_t)left;
        pOutputSamples[i*2+1] = (int32_t)right;
    }
}
#endif
static DRFLAC_INLINE void drflac_read_pcm_frames_s32__decode_right_side__scalar(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int32_t* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
    uint32_t shift1 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
    for (i = 0; i < frameCount4; ++i) {
        uint32_t side0  = pInputSamples0U32[i*4+0] << shift0;
        uint32_t side1  = pInputSamples0U32[i*4+1] << shift0;
        uint32_t side2  = pInputSamples0U32[i*4+2] << shift0;
        uint32_t side3  = pInputSamples0U32[i*4+3] << shift0;
        uint32_t right0 = pInputSamples1U32[i*4+0] << shift1;
        uint32_t right1 = pInputSamples1U32[i*4+1] << shift1;
        uint32_t right2 = pInputSamples1U32[i*4+2] << shift1;
        uint32_t right3 = pInputSamples1U32[i*4+3] << shift1;
        uint32_t left0 = right0 + side0;
        uint32_t left1 = right1 + side1;
        uint32_t left2 = right2 + side2;
        uint32_t left3 = right3 + side3;
        pOutputSamples[i*8+0] = (int32_t)left0;
        pOutputSamples[i*8+1] = (int32_t)right0;
        pOutputSamples[i*8+2] = (int32_t)left1;
        pOutputSamples[i*8+3] = (int32_t)right1;
        pOutputSamples[i*8+4] = (int32_t)left2;
        pOutputSamples[i*8+5] = (int32_t)right2;
        pOutputSamples[i*8+6] = (int32_t)left3;
        pOutputSamples[i*8+7] = (int32_t)right3;
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        uint32_t side  = pInputSamples0U32[i] << shift0;
        uint32_t right = pInputSamples1U32[i] << shift1;
        uint32_t left  = right + side;
        pOutputSamples[i*2+0] = (int32_t)left;
        pOutputSamples[i*2+1] = (int32_t)right;
    }
}
#if defined(DR_FLAC_SUPPORT_SSE2)
static DRFLAC_INLINE void drflac_read_pcm_frames_s32__decode_right_side__sse2(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int32_t* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
    uint32_t shift1 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
    DR_FLAC_ASSERT(pFlac->currentFLACFrame.header.bitsPerSample <= 24);
    for (i = 0; i < frameCount4; ++i) {
        __m128i side  = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples0 + i), shift0);
        __m128i right = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples1 + i), shift1);
        __m128i left  = _mm_add_epi32(right, side);
        _mm_storeu_si128((__m128i*)(pOutputSamples + i*8 + 0), _mm_unpacklo_epi32(left, right));
        _mm_storeu_si128((__m128i*)(pOutputSamples + i*8 + 4), _mm_unpackhi_epi32(left, right));
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        uint32_t side  = pInputSamples0U32[i] << shift0;
        uint32_t right = pInputSamples1U32[i] << shift1;
        uint32_t left  = right + side;
        pOutputSamples[i*2+0] = (int32_t)left;
        pOutputSamples[i*2+1] = (int32_t)right;
    }
}
#endif
#if defined(DR_FLAC_SUPPORT_NEON)
static DRFLAC_INLINE void drflac_read_pcm_frames_s32__decode_right_side__neon(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int32_t* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
    uint32_t shift1 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
    int32x4_t shift0_4;
    int32x4_t shift1_4;
    DR_FLAC_ASSERT(pFlac->currentFLACFrame.header.bitsPerSample <= 24);
    shift0_4 = vdupq_n_s32(shift0);
    shift1_4 = vdupq_n_s32(shift1);
    for (i = 0; i < frameCount4; ++i) {
        uint32x4_t side;
        uint32x4_t right;
        uint32x4_t left;
        side  = vshlq_u32(vld1q_u32(pInputSamples0U32 + i*4), shift0_4);
        right = vshlq_u32(vld1q_u32(pInputSamples1U32 + i*4), shift1_4);
        left  = vaddq_u32(right, side);
        drflac__vst2q_u32((uint32_t*)pOutputSamples + i*8, vzipq_u32(left, right));
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        uint32_t side  = pInputSamples0U32[i] << shift0;
        uint32_t right = pInputSamples1U32[i] << shift1;
        uint32_t left  = right + side;
        pOutputSamples[i*2+0] = (int32_t)left;
        pOutputSamples[i*2+1] = (int32_t)right;
    }
}
#endif
static DRFLAC_INLINE void drflac_read_pcm_frames_s32__decode_right_side(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int32_t* pOutputSamples)
{
    if (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample == 32) {
        drflac_read_pcm_frames_s32__decode_duplicate_mono(frameCount, unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample, pInputSamples1, pOutputSamples);
        return;
    }
#if defined(DR_FLAC_SUPPORT_SSE2)
    if (drflac__gIsSSE2Supported && pFlac->currentFLACFrame.header.bitsPerSample <= 24) {
        drflac_read_pcm_frames_s32__decode_right_side__sse2(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
    } else
#elif defined(DR_FLAC_SUPPORT_NEON)
    if (drflac__gIsNEONSupported && pFlac->currentFLACFrame.header.bitsPerSample <= 24) {
        drflac_read_pcm_frames_s32__decode_right_side__neon(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
    } else
#endif
    {
#if 0
        drflac_read_pcm_frames_s32__decode_right_side__reference(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
#else
        drflac_read_pcm_frames_s32__decode_right_side__scalar(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
#endif
    }
}
#if 0
static DRFLAC_INLINE void drflac_read_pcm_frames_s32__decode_mid_side__reference(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int32_t* pOutputSamples)
{
    for (uint64_t i = 0; i < frameCount; ++i) {
        uint32_t mid  = pInputSamples0U32[i] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
        uint32_t side = pInputSamples1U32[i] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
        mid = (mid << 1) | (side & 0x01);
        pOutputSamples[i*2+0] = (int32_t)((uint32_t)((int32_t)(mid + side) >> 1) << unusedBitsPerSample);
        pOutputSamples[i*2+1] = (int32_t)((uint32_t)((int32_t)(mid - side) >> 1) << unusedBitsPerSample);
    }
}
#endif
static DRFLAC_INLINE void drflac_read_pcm_frames_s32__decode_mid_side__scalar(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int32_t* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    int32_t shift = unusedBitsPerSample;
    if (shift > 0) {
        for (i = 0; i < frameCount4; ++i) {
            uint32_t temp0L;
            uint32_t temp1L;
            uint32_t temp2L;
            uint32_t temp3L;
            uint32_t temp0R;
            uint32_t temp1R;
            uint32_t temp2R;
            uint32_t temp3R;
            uint32_t mid0  = pInputSamples0U32[i*4+0] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t mid1  = pInputSamples0U32[i*4+1] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t mid2  = pInputSamples0U32[i*4+2] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t mid3  = pInputSamples0U32[i*4+3] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t side0 = pInputSamples1U32[i*4+0] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            uint32_t side1 = pInputSamples1U32[i*4+1] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            uint32_t side2 = pInputSamples1U32[i*4+2] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            uint32_t side3 = pInputSamples1U32[i*4+3] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            mid0 = (mid0 << 1) | (side0 & 0x01);
            mid1 = (mid1 << 1) | (side1 & 0x01);
            mid2 = (mid2 << 1) | (side2 & 0x01);
            mid3 = (mid3 << 1) | (side3 & 0x01);
            temp0L = (mid0 + side0) << (shift - 1);
            temp1L = (mid1 + side1) << (shift - 1);
            temp2L = (mid2 + side2) << (shift - 1);
            temp3L = (mid3 + side3) << (shift - 1);
            temp0R = (mid0 - side0) << (shift - 1);
            temp1R = (mid1 - side1) << (shift - 1);
            temp2R = (mid2 - side2) << (shift - 1);
            temp3R = (mid3 - side3) << (shift - 1);
            pOutputSamples[i*8+0] = (int32_t)temp0L;
            pOutputSamples[i*8+1] = (int32_t)temp0R;
            pOutputSamples[i*8+2] = (int32_t)temp1L;
            pOutputSamples[i*8+3] = (int32_t)temp1R;
            pOutputSamples[i*8+4] = (int32_t)temp2L;
            pOutputSamples[i*8+5] = (int32_t)temp2R;
            pOutputSamples[i*8+6] = (int32_t)temp3L;
            pOutputSamples[i*8+7] = (int32_t)temp3R;
        }
    } else {
        for (i = 0; i < frameCount4; ++i) {
            uint32_t temp0L;
            uint32_t temp1L;
            uint32_t temp2L;
            uint32_t temp3L;
            uint32_t temp0R;
            uint32_t temp1R;
            uint32_t temp2R;
            uint32_t temp3R;
            uint64_t mid0  = (uint64_t)(int64_t)pInputSamples0[i*4+0] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint64_t mid1  = (uint64_t)(int64_t)pInputSamples0[i*4+1] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint64_t mid2  = (uint64_t)(int64_t)pInputSamples0[i*4+2] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint64_t mid3  = (uint64_t)(int64_t)pInputSamples0[i*4+3] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint64_t side0 = (uint64_t)(int64_t)pInputSamples1[i*4+0] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            uint64_t side1 = (uint64_t)(int64_t)pInputSamples1[i*4+1] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            uint64_t side2 = (uint64_t)(int64_t)pInputSamples1[i*4+2] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            uint64_t side3 = (uint64_t)(int64_t)pInputSamples1[i*4+3] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            mid0 = (mid0 << 1) | (side0 & 0x01);
            mid1 = (mid1 << 1) | (side1 & 0x01);
            mid2 = (mid2 << 1) | (side2 & 0x01);
            mid3 = (mid3 << 1) | (side3 & 0x01);
            temp0L = (uint32_t)((int64_t)(mid0 + side0) >> 1);
            temp1L = (uint32_t)((int64_t)(mid1 + side1) >> 1);
            temp2L = (uint32_t)((int64_t)(mid2 + side2) >> 1);
            temp3L = (uint32_t)((int64_t)(mid3 + side3) >> 1);
            temp0R = (uint32_t)((int64_t)(mid0 - side0) >> 1);
            temp1R = (uint32_t)((int64_t)(mid1 - side1) >> 1);
            temp2R = (uint32_t)((int64_t)(mid2 - side2) >> 1);
            temp3R = (uint32_t)((int64_t)(mid3 - side3) >> 1);
            pOutputSamples[i*8+0] = (int32_t)temp0L;
            pOutputSamples[i*8+1] = (int32_t)temp0R;
            pOutputSamples[i*8+2] = (int32_t)temp1L;
            pOutputSamples[i*8+3] = (int32_t)temp1R;
            pOutputSamples[i*8+4] = (int32_t)temp2L;
            pOutputSamples[i*8+5] = (int32_t)temp2R;
            pOutputSamples[i*8+6] = (int32_t)temp3L;
            pOutputSamples[i*8+7] = (int32_t)temp3R;
        }
    }
    if (shift > 0) {
        for (i = (frameCount4 << 2); i < frameCount; ++i) {
            uint32_t mid  = pInputSamples0U32[i] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t side = pInputSamples1U32[i] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            mid = (mid << 1) | (side & 0x01);
            pOutputSamples[i*2+0] = (int32_t)((uint32_t)((int32_t)(mid + side) >> 1) << unusedBitsPerSample);
            pOutputSamples[i*2+1] = (int32_t)((uint32_t)((int32_t)(mid - side) >> 1) << unusedBitsPerSample);
        }
    } else {
        for (i = (frameCount4 << 2); i < frameCount; ++i) {
            uint64_t mid;
            uint64_t side;
            mid  = (uint64_t)(int64_t)pInputSamples0[i] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            side = (uint64_t)(int64_t)pInputSamples1[i] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            mid = (mid << 1) | (side & 0x01);
            pOutputSamples[i*2+0] = (int32_t)((int64_t)(mid + side) >> 1);
            pOutputSamples[i*2+1] = (int32_t)((int64_t)(mid - side) >> 1);
        }
    }
}
#if defined(DR_FLAC_SUPPORT_SSE2)
static DRFLAC_INLINE void drflac_read_pcm_frames_s32__decode_mid_side__sse2(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int32_t* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    int32_t shift = unusedBitsPerSample;
    DR_FLAC_ASSERT(pFlac->currentFLACFrame.header.bitsPerSample <= 24);
    if (shift == 0) {
        for (i = 0; i < frameCount4; ++i) {
            __m128i mid;
            __m128i side;
            __m128i left;
            __m128i right;
            mid   = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples0 + i), pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample);
            side  = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples1 + i), pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample);
            mid   = _mm_or_si128(_mm_slli_epi32(mid, 1), _mm_and_si128(side, _mm_set1_epi32(0x01)));
            left  = _mm_srai_epi32(_mm_add_epi32(mid, side), 1);
            right = _mm_srai_epi32(_mm_sub_epi32(mid, side), 1);
            _mm_storeu_si128((__m128i*)(pOutputSamples + i*8 + 0), _mm_unpacklo_epi32(left, right));
            _mm_storeu_si128((__m128i*)(pOutputSamples + i*8 + 4), _mm_unpackhi_epi32(left, right));
        }
        for (i = (frameCount4 << 2); i < frameCount; ++i) {
            uint32_t mid  = pInputSamples0U32[i] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t side = pInputSamples1U32[i] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            mid = (mid << 1) | (side & 0x01);
            pOutputSamples[i*2+0] = (int32_t)(mid + side) >> 1;
            pOutputSamples[i*2+1] = (int32_t)(mid - side) >> 1;
        }
    } else {
        shift -= 1;
        for (i = 0; i < frameCount4; ++i) {
            __m128i mid;
            __m128i side;
            __m128i left;
            __m128i right;
            mid   = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples0 + i), pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample);
            side  = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples1 + i), pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample);
            mid   = _mm_or_si128(_mm_slli_epi32(mid, 1), _mm_and_si128(side, _mm_set1_epi32(0x01)));
            left  = _mm_slli_epi32(_mm_add_epi32(mid, side), shift);
            right = _mm_slli_epi32(_mm_sub_epi32(mid, side), shift);
            _mm_storeu_si128((__m128i*)(pOutputSamples + i*8 + 0), _mm_unpacklo_epi32(left, right));
            _mm_storeu_si128((__m128i*)(pOutputSamples + i*8 + 4), _mm_unpackhi_epi32(left, right));
        }
        for (i = (frameCount4 << 2); i < frameCount; ++i) {
            uint32_t mid  = pInputSamples0U32[i] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t side = pInputSamples1U32[i] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            mid = (mid << 1) | (side & 0x01);
            pOutputSamples[i*2+0] = (int32_t)((mid + side) << shift);
            pOutputSamples[i*2+1] = (int32_t)((mid - side) << shift);
        }
    }
}
#endif
#if defined(DR_FLAC_SUPPORT_NEON)
static DRFLAC_INLINE void drflac_read_pcm_frames_s32__decode_mid_side__neon(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int32_t* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    int32_t shift = unusedBitsPerSample;
    int32x4_t  wbpsShift0_4;
    int32x4_t  wbpsShift1_4;
    uint32x4_t one4;
    DR_FLAC_ASSERT(pFlac->currentFLACFrame.header.bitsPerSample <= 24);
    wbpsShift0_4 = vdupq_n_s32(pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample);
    wbpsShift1_4 = vdupq_n_s32(pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample);
    one4         = vdupq_n_u32(1);
    if (shift == 0) {
        for (i = 0; i < frameCount4; ++i) {
            uint32x4_t mid;
            uint32x4_t side;
            int32x4_t left;
            int32x4_t right;
            mid   = vshlq_u32(vld1q_u32(pInputSamples0U32 + i*4), wbpsShift0_4);
            side  = vshlq_u32(vld1q_u32(pInputSamples1U32 + i*4), wbpsShift1_4);
            mid   = vorrq_u32(vshlq_n_u32(mid, 1), vandq_u32(side, one4));
            left  = vshrq_n_s32(vreinterpretq_s32_u32(vaddq_u32(mid, side)), 1);
            right = vshrq_n_s32(vreinterpretq_s32_u32(vsubq_u32(mid, side)), 1);
            drflac__vst2q_s32(pOutputSamples + i*8, vzipq_s32(left, right));
        }
        for (i = (frameCount4 << 2); i < frameCount; ++i) {
            uint32_t mid  = pInputSamples0U32[i] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t side = pInputSamples1U32[i] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            mid = (mid << 1) | (side & 0x01);
            pOutputSamples[i*2+0] = (int32_t)(mid + side) >> 1;
            pOutputSamples[i*2+1] = (int32_t)(mid - side) >> 1;
        }
    } else {
        int32x4_t shift4;
        shift -= 1;
        shift4 = vdupq_n_s32(shift);
        for (i = 0; i < frameCount4; ++i) {
            uint32x4_t mid;
            uint32x4_t side;
            int32x4_t left;
            int32x4_t right;
            mid   = vshlq_u32(vld1q_u32(pInputSamples0U32 + i*4), wbpsShift0_4);
            side  = vshlq_u32(vld1q_u32(pInputSamples1U32 + i*4), wbpsShift1_4);
            mid   = vorrq_u32(vshlq_n_u32(mid, 1), vandq_u32(side, one4));
            left  = vreinterpretq_s32_u32(vshlq_u32(vaddq_u32(mid, side), shift4));
            right = vreinterpretq_s32_u32(vshlq_u32(vsubq_u32(mid, side), shift4));
            drflac__vst2q_s32(pOutputSamples + i*8, vzipq_s32(left, right));
        }
        for (i = (frameCount4 << 2); i < frameCount; ++i) {
            uint32_t mid  = pInputSamples0U32[i] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t side = pInputSamples1U32[i] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            mid = (mid << 1) | (side & 0x01);
            pOutputSamples[i*2+0] = (int32_t)((mid + side) << shift);
            pOutputSamples[i*2+1] = (int32_t)((mid - side) << shift);
        }
    }
}
#endif
static DRFLAC_INLINE void drflac_read_pcm_frames_s32__decode_mid_side(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int32_t* pOutputSamples)
{
#if defined(DR_FLAC_SUPPORT_SSE2)
    if (drflac__gIsSSE2Supported && pFlac->currentFLACFrame.header.bitsPerSample <= 24) {
        drflac_read_pcm_frames_s32__decode_mid_side__sse2(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
    } else
#elif defined(DR_FLAC_SUPPORT_NEON)
    if (drflac__gIsNEONSupported && pFlac->currentFLACFrame.header.bitsPerSample <= 24) {
        drflac_read_pcm_frames_s32__decode_mid_side__neon(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
    } else
#endif
    {
#if 0
        drflac_read_pcm_frames_s32__decode_mid_side__reference(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
#else
        drflac_read_pcm_frames_s32__decode_mid_side__scalar(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
#endif
    }
}
#if 0
static DRFLAC_INLINE void drflac_read_pcm_frames_s32__decode_independent_stereo__reference(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int32_t* pOutputSamples)
{
    for (uint64_t i = 0; i < frameCount; ++i) {
        pOutputSamples[i*2+0] = (int32_t)((uint32_t)pInputSamples0[i] << (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample));
        pOutputSamples[i*2+1] = (int32_t)((uint32_t)pInputSamples1[i] << (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample));
    }
}
#endif
static DRFLAC_INLINE void drflac_read_pcm_frames_s32__decode_independent_stereo__scalar(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int32_t* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
    uint32_t shift1 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
    for (i = 0; i < frameCount4; ++i) {
        uint32_t tempL0 = pInputSamples0U32[i*4+0] << shift0;
        uint32_t tempL1 = pInputSamples0U32[i*4+1] << shift0;
        uint32_t tempL2 = pInputSamples0U32[i*4+2] << shift0;
        uint32_t tempL3 = pInputSamples0U32[i*4+3] << shift0;
        uint32_t tempR0 = pInputSamples1U32[i*4+0] << shift1;
        uint32_t tempR1 = pInputSamples1U32[i*4+1] << shift1;
        uint32_t tempR2 = pInputSamples1U32[i*4+2] << shift1;
        uint32_t tempR3 = pInputSamples1U32[i*4+3] << shift1;
        pOutputSamples[i*8+0] = (int32_t)tempL0;
        pOutputSamples[i*8+1] = (int32_t)tempR0;
        pOutputSamples[i*8+2] = (int32_t)tempL1;
        pOutputSamples[i*8+3] = (int32_t)tempR1;
        pOutputSamples[i*8+4] = (int32_t)tempL2;
        pOutputSamples[i*8+5] = (int32_t)tempR2;
        pOutputSamples[i*8+6] = (int32_t)tempL3;
        pOutputSamples[i*8+7] = (int32_t)tempR3;
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        pOutputSamples[i*2+0] = (int32_t)(pInputSamples0U32[i] << shift0);
        pOutputSamples[i*2+1] = (int32_t)(pInputSamples1U32[i] << shift1);
    }
}
#if defined(DR_FLAC_SUPPORT_SSE2)
static DRFLAC_INLINE void drflac_read_pcm_frames_s32__decode_independent_stereo__sse2(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int32_t* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
    uint32_t shift1 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
    for (i = 0; i < frameCount4; ++i) {
        __m128i left  = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples0 + i), shift0);
        __m128i right = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples1 + i), shift1);
        _mm_storeu_si128((__m128i*)(pOutputSamples + i*8 + 0), _mm_unpacklo_epi32(left, right));
        _mm_storeu_si128((__m128i*)(pOutputSamples + i*8 + 4), _mm_unpackhi_epi32(left, right));
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        pOutputSamples[i*2+0] = (int32_t)(pInputSamples0U32[i] << shift0);
        pOutputSamples[i*2+1] = (int32_t)(pInputSamples1U32[i] << shift1);
    }
}
#endif
#if defined(DR_FLAC_SUPPORT_NEON)
static DRFLAC_INLINE void drflac_read_pcm_frames_s32__decode_independent_stereo__neon(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int32_t* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
    uint32_t shift1 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
    int32x4_t shift4_0 = vdupq_n_s32(shift0);
    int32x4_t shift4_1 = vdupq_n_s32(shift1);
    for (i = 0; i < frameCount4; ++i) {
        int32x4_t left;
        int32x4_t right;
        left  = vreinterpretq_s32_u32(vshlq_u32(vld1q_u32(pInputSamples0U32 + i*4), shift4_0));
        right = vreinterpretq_s32_u32(vshlq_u32(vld1q_u32(pInputSamples1U32 + i*4), shift4_1));
        drflac__vst2q_s32(pOutputSamples + i*8, vzipq_s32(left, right));
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        pOutputSamples[i*2+0] = (int32_t)(pInputSamples0U32[i] << shift0);
        pOutputSamples[i*2+1] = (int32_t)(pInputSamples1U32[i] << shift1);
    }
}
#endif
static DRFLAC_INLINE void drflac_read_pcm_frames_s32__decode_independent_stereo(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int32_t* pOutputSamples)
{
#if defined(DR_FLAC_SUPPORT_SSE2)
    if (drflac__gIsSSE2Supported && pFlac->currentFLACFrame.header.bitsPerSample <= 24) {
        drflac_read_pcm_frames_s32__decode_independent_stereo__sse2(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
    } else
#elif defined(DR_FLAC_SUPPORT_NEON)
    if (drflac__gIsNEONSupported && pFlac->currentFLACFrame.header.bitsPerSample <= 24) {
        drflac_read_pcm_frames_s32__decode_independent_stereo__neon(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
    } else
#endif
    {
#if 0
        drflac_read_pcm_frames_s32__decode_independent_stereo__reference(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
#else
        drflac_read_pcm_frames_s32__decode_independent_stereo__scalar(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
#endif
    }
}
static DRFLAC_INLINE void drflac__decode_stereo_sample_s64(const drflac_frame* pFrame, uint64_t iPCMFrame, int32_t* pLeft, int32_t* pRight)
{
    uint64_t sample0;
    uint64_t sample1;
    sample0 = (uint64_t)pFrame->subframes[0].samples.pS64[iPCMFrame] << pFrame->subframes[0].wastedBitsPerSample;
    sample1 = (uint64_t)pFrame->subframes[1].samples.pS64[iPCMFrame] << pFrame->subframes[1].wastedBitsPerSample;
    switch (pFrame->header.channelAssignment)
    {
        case DR_FLAC_CHANNEL_ASSIGNMENT_LEFT_SIDE:
        {
            *pLeft  = (int32_t)sample0;
            *pRight = (int32_t)(sample0 - sample1);
        } break;
        case DR_FLAC_CHANNEL_ASSIGNMENT_RIGHT_SIDE:
        {
            *pLeft  = (int32_t)(sample0 + sample1);
            *pRight = (int32_t)sample1;
        } break;
        case DR_FLAC_CHANNEL_ASSIGNMENT_MID_SIDE:
        {
            sample0 = (sample0 << 1) | (sample1 & 0x01);
            *pLeft  = (int32_t)((int64_t)(sample0 + sample1) >> 1);
            *pRight = (int32_t)((int64_t)(sample0 - sample1) >> 1);
        } break;
        case DR_FLAC_CHANNEL_ASSIGNMENT_INDEPENDENT_STEREO:
        {
            *pLeft  = (int32_t)sample0;
            *pRight = (int32_t)sample1;
        } break;
        default:
        {
            DR_FLAC_ASSERT(!"Invalid stereo channel assignment");
            *pLeft  = 0;
            *pRight = 0;
        } break;
    }
}
static void drflac_read_pcm_frames_s32__decode_s64(drflac* pFlac, uint64_t frameCount, uint64_t iFirstPCMFrame, int32_t* pOutputSamples)
{
    uint64_t i;
    DR_FLAC_ASSERT(pFlac->bitsPerSample == 32);
    if (pFlac->channels == 2) {
        for (i = 0; i < frameCount; ++i) {
            int32_t left;
            int32_t right;
            drflac__decode_stereo_sample_s64(&pFlac->currentFLACFrame, iFirstPCMFrame + i, &left, &right);
            pOutputSamples[i*2+0] = left;
            pOutputSamples[i*2+1] = right;
        }
    } else {
        for (i = 0; i < frameCount; ++i) {
            unsigned int j;
            for (j = 0; j < pFlac->channels; ++j) {
                int32_t sample;
                sample = (int32_t)((uint64_t)pFlac->currentFLACFrame.subframes[j].samples.pS64[iFirstPCMFrame + i] << pFlac->currentFLACFrame.subframes[j].wastedBitsPerSample);
                pOutputSamples[i*pFlac->channels+j] = sample;
            }
        }
    }
}
static void drflac_read_pcm_frames_s16__decode_s64(drflac* pFlac, uint64_t frameCount, uint64_t iFirstPCMFrame, int16_t* pOutputSamples)
{
    uint64_t i;
    DR_FLAC_ASSERT(pFlac->bitsPerSample == 32);
    if (pFlac->channels == 2) {
        for (i = 0; i < frameCount; ++i) {
            int32_t left;
            int32_t right;
            drflac__decode_stereo_sample_s64(&pFlac->currentFLACFrame, iFirstPCMFrame + i, &left, &right);
            pOutputSamples[i*2+0] = (int16_t)(left  >> 16);
            pOutputSamples[i*2+1] = (int16_t)(right >> 16);
        }
    } else {
        for (i = 0; i < frameCount; ++i) {
            unsigned int j;
            for (j = 0; j < pFlac->channels; ++j) {
                int32_t sample;
                sample = (int32_t)((uint64_t)pFlac->currentFLACFrame.subframes[j].samples.pS64[iFirstPCMFrame + i] << pFlac->currentFLACFrame.subframes[j].wastedBitsPerSample);
                pOutputSamples[i*pFlac->channels+j] = (int16_t)(sample >> 16);
            }
        }
    }
}
static void drflac_read_pcm_frames_f32__decode_s64(drflac* pFlac, uint64_t frameCount, uint64_t iFirstPCMFrame, float* pOutputSamples)
{
    uint64_t i;
    DR_FLAC_ASSERT(pFlac->bitsPerSample == 32);
    if (pFlac->channels == 2) {
        for (i = 0; i < frameCount; ++i) {
            int32_t left;
            int32_t right;
            drflac__decode_stereo_sample_s64(&pFlac->currentFLACFrame, iFirstPCMFrame + i, &left, &right);
            pOutputSamples[i*2+0] = left  * (1.0f / 2147483648.0f);
            pOutputSamples[i*2+1] = right * (1.0f / 2147483648.0f);
        }
    } else {
        for (i = 0; i < frameCount; ++i) {
            unsigned int j;
            for (j = 0; j < pFlac->channels; ++j) {
                int32_t sample;
                sample = (int32_t)((uint64_t)pFlac->currentFLACFrame.subframes[j].samples.pS64[iFirstPCMFrame + i] << pFlac->currentFLACFrame.subframes[j].wastedBitsPerSample);
                pOutputSamples[i*pFlac->channels+j] = sample * (1.0f / 2147483648.0f);
            }
        }
    }
}
static int drflac__is_current_flac_frame_valid(drflac* pFlac)
{
    uint32_t iChannel;
    uint32_t channelCount;
    if (pFlac->currentFLACFrame.header.blockSizeInPCMFrames > pFlac->decodedSamplesCapacityInPCMFrames || pFlac->currentFLACFrame.pcmFramesRemaining > pFlac->currentFLACFrame.header.blockSizeInPCMFrames) {
        return 0;
    }
    channelCount = drflac__get_channel_count_from_channel_assignment(pFlac->currentFLACFrame.header.channelAssignment);
    if (channelCount != pFlac->channels) {
        return 0;
    }
    if (pFlac->bitsPerSample == 32) {
        for (iChannel = 0; iChannel < channelCount; iChannel += 1) {
            if (pFlac->currentFLACFrame.subframes[iChannel].samples.pS64 == NULL) {
                return 0;
            }
        }
    } else {
        for (iChannel = 0; iChannel < channelCount; iChannel += 1) {
            if (pFlac->currentFLACFrame.subframes[iChannel].samples.pS32 == NULL) {
                return 0;
            }
        }
    }
    return 1;
}
static DRFLAC_INLINE void drflac_read_pcm_frames_s32__decode(drflac* pFlac, uint64_t frameCount, uint64_t iFirstPCMFrame, int32_t* pOutputSamples)
{
    unsigned int channelCount;
    uint32_t unusedBitsPerSample;
    DR_FLAC_ASSERT(pFlac->currentFLACFrame.header.bitsPerSample < 32);
    channelCount = drflac__get_channel_count_from_channel_assignment(pFlac->currentFLACFrame.header.channelAssignment);
    unusedBitsPerSample = 32 - pFlac->currentFLACFrame.header.bitsPerSample;
    if (channelCount == 2) {
        const int32_t* pDecodedSamples0 = pFlac->currentFLACFrame.subframes[0].samples.pS32 + iFirstPCMFrame;
        const int32_t* pDecodedSamples1 = pFlac->currentFLACFrame.subframes[1].samples.pS32 + iFirstPCMFrame;
        switch (pFlac->currentFLACFrame.header.channelAssignment)
        {
            case DR_FLAC_CHANNEL_ASSIGNMENT_LEFT_SIDE:
            {
                drflac_read_pcm_frames_s32__decode_left_side(pFlac, frameCount, unusedBitsPerSample, pDecodedSamples0, pDecodedSamples1, pOutputSamples);
            } break;
            case DR_FLAC_CHANNEL_ASSIGNMENT_RIGHT_SIDE:
            {
                drflac_read_pcm_frames_s32__decode_right_side(pFlac, frameCount, unusedBitsPerSample, pDecodedSamples0, pDecodedSamples1, pOutputSamples);
            } break;
            case DR_FLAC_CHANNEL_ASSIGNMENT_MID_SIDE:
            {
                drflac_read_pcm_frames_s32__decode_mid_side(pFlac, frameCount, unusedBitsPerSample, pDecodedSamples0, pDecodedSamples1, pOutputSamples);
            } break;
            case DR_FLAC_CHANNEL_ASSIGNMENT_INDEPENDENT_STEREO:
            {
                drflac_read_pcm_frames_s32__decode_independent_stereo(pFlac, frameCount, unusedBitsPerSample, pDecodedSamples0, pDecodedSamples1, pOutputSamples);
            } break;
            default: DR_FLAC_ASSERT(!"Invalid stereo channel assignment"); break;
        }
    } else {
        uint64_t i;
        for (i = 0; i < frameCount; ++i) {
            unsigned int j;
            for (j = 0; j < channelCount; ++j) {
                pOutputSamples[(i*channelCount)+j] = (int32_t)((uint32_t)(pFlac->currentFLACFrame.subframes[j].samples.pS32[iFirstPCMFrame + i]) << (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[j].wastedBitsPerSample));
            }
        }
    }
}
DRFLAC_API uint64_t drflac_read_pcm_frames_s32(drflac* pFlac, uint64_t framesToRead, int32_t* pBufferOut)
{
    uint64_t framesRead;
    if (pFlac == NULL || framesToRead == 0) {
        return 0;
    }
    if (pBufferOut == NULL) {
        return drflac__seek_forward_by_pcm_frames(pFlac, framesToRead);
    }
    DR_FLAC_ASSERT(pFlac->bitsPerSample <= 32);
    framesRead = 0;
    while (framesToRead > 0) {
        if (pFlac->currentFLACFrame.pcmFramesRemaining == 0) {
            if (!drflac__read_and_decode_next_flac_frame(pFlac)) {
                break;
            }
        } else {
            unsigned int channelCount = drflac__get_channel_count_from_channel_assignment(pFlac->currentFLACFrame.header.channelAssignment);
            uint64_t iFirstPCMFrame = pFlac->currentFLACFrame.header.blockSizeInPCMFrames - pFlac->currentFLACFrame.pcmFramesRemaining;
            uint64_t frameCountThisIteration = framesToRead;
            if (frameCountThisIteration > pFlac->currentFLACFrame.pcmFramesRemaining) {
                frameCountThisIteration = pFlac->currentFLACFrame.pcmFramesRemaining;
            }
            if (pFlac->bitsPerSample != 32) {
                drflac_read_pcm_frames_s32__decode(pFlac, frameCountThisIteration, iFirstPCMFrame, pBufferOut);
            } else {
                drflac_read_pcm_frames_s32__decode_s64(pFlac, frameCountThisIteration, iFirstPCMFrame, pBufferOut);
            }
            framesRead             += frameCountThisIteration;
            pBufferOut             += frameCountThisIteration * channelCount;
            framesToRead           -= frameCountThisIteration;
            pFlac->currentPCMFrame += frameCountThisIteration;
            pFlac->currentFLACFrame.pcmFramesRemaining -= (uint32_t)frameCountThisIteration;
        }
    }
    return framesRead;
}
#if 0
static DRFLAC_INLINE void drflac_read_pcm_frames_s16__decode_left_side__reference(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int16_t* pOutputSamples)
{
    uint64_t i;
    for (i = 0; i < frameCount; ++i) {
        uint32_t left  = (uint32_t)pInputSamples0[i] << (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample);
        uint32_t side  = (uint32_t)pInputSamples1[i] << (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample);
        uint32_t right = left - side;
        left  >>= 16;
        right >>= 16;
        pOutputSamples[i*2+0] = (int16_t)left;
        pOutputSamples[i*2+1] = (int16_t)right;
    }
}
#endif
static DRFLAC_INLINE void drflac_read_pcm_frames_s16__decode_left_side__scalar(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int16_t* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
    uint32_t shift1 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
    for (i = 0; i < frameCount4; ++i) {
        uint32_t left0 = pInputSamples0U32[i*4+0] << shift0;
        uint32_t left1 = pInputSamples0U32[i*4+1] << shift0;
        uint32_t left2 = pInputSamples0U32[i*4+2] << shift0;
        uint32_t left3 = pInputSamples0U32[i*4+3] << shift0;
        uint32_t side0 = pInputSamples1U32[i*4+0] << shift1;
        uint32_t side1 = pInputSamples1U32[i*4+1] << shift1;
        uint32_t side2 = pInputSamples1U32[i*4+2] << shift1;
        uint32_t side3 = pInputSamples1U32[i*4+3] << shift1;
        uint32_t right0 = left0 - side0;
        uint32_t right1 = left1 - side1;
        uint32_t right2 = left2 - side2;
        uint32_t right3 = left3 - side3;
        left0  >>= 16;
        left1  >>= 16;
        left2  >>= 16;
        left3  >>= 16;
        right0 >>= 16;
        right1 >>= 16;
        right2 >>= 16;
        right3 >>= 16;
        pOutputSamples[i*8+0] = (int16_t)left0;
        pOutputSamples[i*8+1] = (int16_t)right0;
        pOutputSamples[i*8+2] = (int16_t)left1;
        pOutputSamples[i*8+3] = (int16_t)right1;
        pOutputSamples[i*8+4] = (int16_t)left2;
        pOutputSamples[i*8+5] = (int16_t)right2;
        pOutputSamples[i*8+6] = (int16_t)left3;
        pOutputSamples[i*8+7] = (int16_t)right3;
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        uint32_t left  = pInputSamples0U32[i] << shift0;
        uint32_t side  = pInputSamples1U32[i] << shift1;
        uint32_t right = left - side;
        left  >>= 16;
        right >>= 16;
        pOutputSamples[i*2+0] = (int16_t)left;
        pOutputSamples[i*2+1] = (int16_t)right;
    }
}
#if defined(DR_FLAC_SUPPORT_SSE2)
static DRFLAC_INLINE void drflac_read_pcm_frames_s16__decode_left_side__sse2(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int16_t* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
    uint32_t shift1 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
    DR_FLAC_ASSERT(pFlac->currentFLACFrame.header.bitsPerSample <= 24);
    for (i = 0; i < frameCount4; ++i) {
        __m128i left  = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples0 + i), shift0);
        __m128i side  = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples1 + i), shift1);
        __m128i right = _mm_sub_epi32(left, side);
        left  = _mm_srai_epi32(left,  16);
        right = _mm_srai_epi32(right, 16);
        _mm_storeu_si128((__m128i*)(pOutputSamples + i*8), drflac__mm_packs_interleaved_epi32(left, right));
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        uint32_t left  = pInputSamples0U32[i] << shift0;
        uint32_t side  = pInputSamples1U32[i] << shift1;
        uint32_t right = left - side;
        left  >>= 16;
        right >>= 16;
        pOutputSamples[i*2+0] = (int16_t)left;
        pOutputSamples[i*2+1] = (int16_t)right;
    }
}
#endif
#if defined(DR_FLAC_SUPPORT_NEON)
static DRFLAC_INLINE void drflac_read_pcm_frames_s16__decode_left_side__neon(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int16_t* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
    uint32_t shift1 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
    int32x4_t shift0_4;
    int32x4_t shift1_4;
    DR_FLAC_ASSERT(pFlac->currentFLACFrame.header.bitsPerSample <= 24);
    shift0_4 = vdupq_n_s32(shift0);
    shift1_4 = vdupq_n_s32(shift1);
    for (i = 0; i < frameCount4; ++i) {
        uint32x4_t left;
        uint32x4_t side;
        uint32x4_t right;
        left  = vshlq_u32(vld1q_u32(pInputSamples0U32 + i*4), shift0_4);
        side  = vshlq_u32(vld1q_u32(pInputSamples1U32 + i*4), shift1_4);
        right = vsubq_u32(left, side);
        left  = vshrq_n_u32(left,  16);
        right = vshrq_n_u32(right, 16);
        drflac__vst2q_u16((uint16_t*)pOutputSamples + i*8, vzip_u16(vmovn_u32(left), vmovn_u32(right)));
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        uint32_t left  = pInputSamples0U32[i] << shift0;
        uint32_t side  = pInputSamples1U32[i] << shift1;
        uint32_t right = left - side;
        left  >>= 16;
        right >>= 16;
        pOutputSamples[i*2+0] = (int16_t)left;
        pOutputSamples[i*2+1] = (int16_t)right;
    }
}
#endif
static DRFLAC_INLINE void drflac_read_pcm_frames_s16__decode_duplicate_mono(uint64_t frameCount, uint32_t shift, const int32_t* pInputSamples, int16_t* pOutputSamples)
{
    uint64_t i;
    const uint32_t* pInputSamplesU32 = (const uint32_t*)pInputSamples;
    DR_FLAC_ASSERT(shift < 32);
    for (i = 0; i < frameCount; ++i) {
        int16_t sample;
        sample = (int16_t)((pInputSamplesU32[i] << shift) >> 16);
        pOutputSamples[i*2+0] = sample;
        pOutputSamples[i*2+1] = sample;
    }
}
static DRFLAC_INLINE void drflac_read_pcm_frames_s16__decode_left_side(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int16_t* pOutputSamples)
{
    if (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample == 32) {
        drflac_read_pcm_frames_s16__decode_duplicate_mono(frameCount, unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample, pInputSamples0, pOutputSamples);
        return;
    }
#if defined(DR_FLAC_SUPPORT_SSE2)
    if (drflac__gIsSSE2Supported && pFlac->currentFLACFrame.header.bitsPerSample <= 24) {
        drflac_read_pcm_frames_s16__decode_left_side__sse2(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
    } else
#elif defined(DR_FLAC_SUPPORT_NEON)
    if (drflac__gIsNEONSupported && pFlac->currentFLACFrame.header.bitsPerSample <= 24) {
        drflac_read_pcm_frames_s16__decode_left_side__neon(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
    } else
#endif
    {
#if 0
        drflac_read_pcm_frames_s16__decode_left_side__reference(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
#else
        drflac_read_pcm_frames_s16__decode_left_side__scalar(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
#endif
    }
}
#if 0
static DRFLAC_INLINE void drflac_read_pcm_frames_s16__decode_right_side__reference(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int16_t* pOutputSamples)
{
    uint64_t i;
    for (i = 0; i < frameCount; ++i) {
        uint32_t side  = (uint32_t)pInputSamples0[i] << (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample);
        uint32_t right = (uint32_t)pInputSamples1[i] << (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample);
        uint32_t left  = right + side;
        left  >>= 16;
        right >>= 16;
        pOutputSamples[i*2+0] = (int16_t)left;
        pOutputSamples[i*2+1] = (int16_t)right;
    }
}
#endif
static DRFLAC_INLINE void drflac_read_pcm_frames_s16__decode_right_side__scalar(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int16_t* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
    uint32_t shift1 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
    for (i = 0; i < frameCount4; ++i) {
        uint32_t side0  = pInputSamples0U32[i*4+0] << shift0;
        uint32_t side1  = pInputSamples0U32[i*4+1] << shift0;
        uint32_t side2  = pInputSamples0U32[i*4+2] << shift0;
        uint32_t side3  = pInputSamples0U32[i*4+3] << shift0;
        uint32_t right0 = pInputSamples1U32[i*4+0] << shift1;
        uint32_t right1 = pInputSamples1U32[i*4+1] << shift1;
        uint32_t right2 = pInputSamples1U32[i*4+2] << shift1;
        uint32_t right3 = pInputSamples1U32[i*4+3] << shift1;
        uint32_t left0 = right0 + side0;
        uint32_t left1 = right1 + side1;
        uint32_t left2 = right2 + side2;
        uint32_t left3 = right3 + side3;
        left0  >>= 16;
        left1  >>= 16;
        left2  >>= 16;
        left3  >>= 16;
        right0 >>= 16;
        right1 >>= 16;
        right2 >>= 16;
        right3 >>= 16;
        pOutputSamples[i*8+0] = (int16_t)left0;
        pOutputSamples[i*8+1] = (int16_t)right0;
        pOutputSamples[i*8+2] = (int16_t)left1;
        pOutputSamples[i*8+3] = (int16_t)right1;
        pOutputSamples[i*8+4] = (int16_t)left2;
        pOutputSamples[i*8+5] = (int16_t)right2;
        pOutputSamples[i*8+6] = (int16_t)left3;
        pOutputSamples[i*8+7] = (int16_t)right3;
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        uint32_t side  = pInputSamples0U32[i] << shift0;
        uint32_t right = pInputSamples1U32[i] << shift1;
        uint32_t left  = right + side;
        left  >>= 16;
        right >>= 16;
        pOutputSamples[i*2+0] = (int16_t)left;
        pOutputSamples[i*2+1] = (int16_t)right;
    }
}
#if defined(DR_FLAC_SUPPORT_SSE2)
static DRFLAC_INLINE void drflac_read_pcm_frames_s16__decode_right_side__sse2(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int16_t* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
    uint32_t shift1 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
    DR_FLAC_ASSERT(pFlac->currentFLACFrame.header.bitsPerSample <= 24);
    for (i = 0; i < frameCount4; ++i) {
        __m128i side  = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples0 + i), shift0);
        __m128i right = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples1 + i), shift1);
        __m128i left  = _mm_add_epi32(right, side);
        left  = _mm_srai_epi32(left,  16);
        right = _mm_srai_epi32(right, 16);
        _mm_storeu_si128((__m128i*)(pOutputSamples + i*8), drflac__mm_packs_interleaved_epi32(left, right));
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        uint32_t side  = pInputSamples0U32[i] << shift0;
        uint32_t right = pInputSamples1U32[i] << shift1;
        uint32_t left  = right + side;
        left  >>= 16;
        right >>= 16;
        pOutputSamples[i*2+0] = (int16_t)left;
        pOutputSamples[i*2+1] = (int16_t)right;
    }
}
#endif
#if defined(DR_FLAC_SUPPORT_NEON)
static DRFLAC_INLINE void drflac_read_pcm_frames_s16__decode_right_side__neon(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int16_t* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
    uint32_t shift1 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
    int32x4_t shift0_4;
    int32x4_t shift1_4;
    DR_FLAC_ASSERT(pFlac->currentFLACFrame.header.bitsPerSample <= 24);
    shift0_4 = vdupq_n_s32(shift0);
    shift1_4 = vdupq_n_s32(shift1);
    for (i = 0; i < frameCount4; ++i) {
        uint32x4_t side;
        uint32x4_t right;
        uint32x4_t left;
        side  = vshlq_u32(vld1q_u32(pInputSamples0U32 + i*4), shift0_4);
        right = vshlq_u32(vld1q_u32(pInputSamples1U32 + i*4), shift1_4);
        left  = vaddq_u32(right, side);
        left  = vshrq_n_u32(left,  16);
        right = vshrq_n_u32(right, 16);
        drflac__vst2q_u16((uint16_t*)pOutputSamples + i*8, vzip_u16(vmovn_u32(left), vmovn_u32(right)));
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        uint32_t side  = pInputSamples0U32[i] << shift0;
        uint32_t right = pInputSamples1U32[i] << shift1;
        uint32_t left  = right + side;
        left  >>= 16;
        right >>= 16;
        pOutputSamples[i*2+0] = (int16_t)left;
        pOutputSamples[i*2+1] = (int16_t)right;
    }
}
#endif
static DRFLAC_INLINE void drflac_read_pcm_frames_s16__decode_right_side(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int16_t* pOutputSamples)
{
    if (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample == 32) {
        drflac_read_pcm_frames_s16__decode_duplicate_mono(frameCount, unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample, pInputSamples1, pOutputSamples);
        return;
    }
#if defined(DR_FLAC_SUPPORT_SSE2)
    if (drflac__gIsSSE2Supported && pFlac->currentFLACFrame.header.bitsPerSample <= 24) {
        drflac_read_pcm_frames_s16__decode_right_side__sse2(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
    } else
#elif defined(DR_FLAC_SUPPORT_NEON)
    if (drflac__gIsNEONSupported && pFlac->currentFLACFrame.header.bitsPerSample <= 24) {
        drflac_read_pcm_frames_s16__decode_right_side__neon(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
    } else
#endif
    {
#if 0
        drflac_read_pcm_frames_s16__decode_right_side__reference(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
#else
        drflac_read_pcm_frames_s16__decode_right_side__scalar(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
#endif
    }
}
#if 0
static DRFLAC_INLINE void drflac_read_pcm_frames_s16__decode_mid_side__reference(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int16_t* pOutputSamples)
{
    for (uint64_t i = 0; i < frameCount; ++i) {
        uint32_t mid  = (uint32_t)pInputSamples0[i] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
        uint32_t side = (uint32_t)pInputSamples1[i] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
        mid = (mid << 1) | (side & 0x01);
        pOutputSamples[i*2+0] = (int16_t)(((uint32_t)((int32_t)(mid + side) >> 1) << unusedBitsPerSample) >> 16);
        pOutputSamples[i*2+1] = (int16_t)(((uint32_t)((int32_t)(mid - side) >> 1) << unusedBitsPerSample) >> 16);
    }
}
#endif
static DRFLAC_INLINE void drflac_read_pcm_frames_s16__decode_mid_side__scalar(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int16_t* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift = unusedBitsPerSample;
    if (shift > 0) {
        for (i = 0; i < frameCount4; ++i) {
            uint32_t temp0L;
            uint32_t temp1L;
            uint32_t temp2L;
            uint32_t temp3L;
            uint32_t temp0R;
            uint32_t temp1R;
            uint32_t temp2R;
            uint32_t temp3R;
            uint32_t mid0  = pInputSamples0U32[i*4+0] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t mid1  = pInputSamples0U32[i*4+1] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t mid2  = pInputSamples0U32[i*4+2] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t mid3  = pInputSamples0U32[i*4+3] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t side0 = pInputSamples1U32[i*4+0] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            uint32_t side1 = pInputSamples1U32[i*4+1] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            uint32_t side2 = pInputSamples1U32[i*4+2] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            uint32_t side3 = pInputSamples1U32[i*4+3] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            mid0 = (mid0 << 1) | (side0 & 0x01);
            mid1 = (mid1 << 1) | (side1 & 0x01);
            mid2 = (mid2 << 1) | (side2 & 0x01);
            mid3 = (mid3 << 1) | (side3 & 0x01);
            temp0L = (mid0 + side0) << (shift - 1);
            temp1L = (mid1 + side1) << (shift - 1);
            temp2L = (mid2 + side2) << (shift - 1);
            temp3L = (mid3 + side3) << (shift - 1);
            temp0R = (mid0 - side0) << (shift - 1);
            temp1R = (mid1 - side1) << (shift - 1);
            temp2R = (mid2 - side2) << (shift - 1);
            temp3R = (mid3 - side3) << (shift - 1);
            temp0L >>= 16;
            temp1L >>= 16;
            temp2L >>= 16;
            temp3L >>= 16;
            temp0R >>= 16;
            temp1R >>= 16;
            temp2R >>= 16;
            temp3R >>= 16;
            pOutputSamples[i*8+0] = (int16_t)temp0L;
            pOutputSamples[i*8+1] = (int16_t)temp0R;
            pOutputSamples[i*8+2] = (int16_t)temp1L;
            pOutputSamples[i*8+3] = (int16_t)temp1R;
            pOutputSamples[i*8+4] = (int16_t)temp2L;
            pOutputSamples[i*8+5] = (int16_t)temp2R;
            pOutputSamples[i*8+6] = (int16_t)temp3L;
            pOutputSamples[i*8+7] = (int16_t)temp3R;
        }
    } else {
        for (i = 0; i < frameCount4; ++i) {
            uint32_t temp0L;
            uint32_t temp1L;
            uint32_t temp2L;
            uint32_t temp3L;
            uint32_t temp0R;
            uint32_t temp1R;
            uint32_t temp2R;
            uint32_t temp3R;
            uint64_t mid0  = (uint64_t)(int64_t)pInputSamples0[i*4+0] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint64_t mid1  = (uint64_t)(int64_t)pInputSamples0[i*4+1] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint64_t mid2  = (uint64_t)(int64_t)pInputSamples0[i*4+2] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint64_t mid3  = (uint64_t)(int64_t)pInputSamples0[i*4+3] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint64_t side0 = (uint64_t)(int64_t)pInputSamples1[i*4+0] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            uint64_t side1 = (uint64_t)(int64_t)pInputSamples1[i*4+1] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            uint64_t side2 = (uint64_t)(int64_t)pInputSamples1[i*4+2] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            uint64_t side3 = (uint64_t)(int64_t)pInputSamples1[i*4+3] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            mid0 = (mid0 << 1) | (side0 & 0x01);
            mid1 = (mid1 << 1) | (side1 & 0x01);
            mid2 = (mid2 << 1) | (side2 & 0x01);
            mid3 = (mid3 << 1) | (side3 & 0x01);
            temp0L = (uint32_t)((int64_t)(mid0 + side0) >> 1);
            temp1L = (uint32_t)((int64_t)(mid1 + side1) >> 1);
            temp2L = (uint32_t)((int64_t)(mid2 + side2) >> 1);
            temp3L = (uint32_t)((int64_t)(mid3 + side3) >> 1);
            temp0R = (uint32_t)((int64_t)(mid0 - side0) >> 1);
            temp1R = (uint32_t)((int64_t)(mid1 - side1) >> 1);
            temp2R = (uint32_t)((int64_t)(mid2 - side2) >> 1);
            temp3R = (uint32_t)((int64_t)(mid3 - side3) >> 1);
            temp0L >>= 16;
            temp1L >>= 16;
            temp2L >>= 16;
            temp3L >>= 16;
            temp0R >>= 16;
            temp1R >>= 16;
            temp2R >>= 16;
            temp3R >>= 16;
            pOutputSamples[i*8+0] = (int16_t)temp0L;
            pOutputSamples[i*8+1] = (int16_t)temp0R;
            pOutputSamples[i*8+2] = (int16_t)temp1L;
            pOutputSamples[i*8+3] = (int16_t)temp1R;
            pOutputSamples[i*8+4] = (int16_t)temp2L;
            pOutputSamples[i*8+5] = (int16_t)temp2R;
            pOutputSamples[i*8+6] = (int16_t)temp3L;
            pOutputSamples[i*8+7] = (int16_t)temp3R;
        }
    }
    if (shift > 0) {
        for (i = (frameCount4 << 2); i < frameCount; ++i) {
            uint32_t mid  = pInputSamples0U32[i] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t side = pInputSamples1U32[i] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            mid = (mid << 1) | (side & 0x01);
            pOutputSamples[i*2+0] = (int16_t)(((uint32_t)((int32_t)(mid + side) >> 1) << unusedBitsPerSample) >> 16);
            pOutputSamples[i*2+1] = (int16_t)(((uint32_t)((int32_t)(mid - side) >> 1) << unusedBitsPerSample) >> 16);
        }
    } else {
        for (i = (frameCount4 << 2); i < frameCount; ++i) {
            uint64_t mid;
            uint64_t side;
            mid  = (uint64_t)(int64_t)pInputSamples0[i] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            side = (uint64_t)(int64_t)pInputSamples1[i] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            mid = (mid << 1) | (side & 0x01);
            pOutputSamples[i*2+0] = (int16_t)((int64_t)(mid + side) >> 17);
            pOutputSamples[i*2+1] = (int16_t)((int64_t)(mid - side) >> 17);
        }
    }
}
#if defined(DR_FLAC_SUPPORT_SSE2)
static DRFLAC_INLINE void drflac_read_pcm_frames_s16__decode_mid_side__sse2(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int16_t* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift = unusedBitsPerSample;
    DR_FLAC_ASSERT(pFlac->currentFLACFrame.header.bitsPerSample <= 24);
    if (shift == 0) {
        for (i = 0; i < frameCount4; ++i) {
            __m128i mid;
            __m128i side;
            __m128i left;
            __m128i right;
            mid   = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples0 + i), pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample);
            side  = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples1 + i), pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample);
            mid   = _mm_or_si128(_mm_slli_epi32(mid, 1), _mm_and_si128(side, _mm_set1_epi32(0x01)));
            left  = _mm_srai_epi32(_mm_add_epi32(mid, side), 1);
            right = _mm_srai_epi32(_mm_sub_epi32(mid, side), 1);
            left  = _mm_srai_epi32(left,  16);
            right = _mm_srai_epi32(right, 16);
            _mm_storeu_si128((__m128i*)(pOutputSamples + i*8), drflac__mm_packs_interleaved_epi32(left, right));
        }
        for (i = (frameCount4 << 2); i < frameCount; ++i) {
            uint32_t mid  = pInputSamples0U32[i] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t side = pInputSamples1U32[i] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            mid = (mid << 1) | (side & 0x01);
            pOutputSamples[i*2+0] = (int16_t)(((int32_t)(mid + side) >> 1) >> 16);
            pOutputSamples[i*2+1] = (int16_t)(((int32_t)(mid - side) >> 1) >> 16);
        }
    } else {
        shift -= 1;
        for (i = 0; i < frameCount4; ++i) {
            __m128i mid;
            __m128i side;
            __m128i left;
            __m128i right;
            mid   = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples0 + i), pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample);
            side  = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples1 + i), pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample);
            mid   = _mm_or_si128(_mm_slli_epi32(mid, 1), _mm_and_si128(side, _mm_set1_epi32(0x01)));
            left  = _mm_slli_epi32(_mm_add_epi32(mid, side), shift);
            right = _mm_slli_epi32(_mm_sub_epi32(mid, side), shift);
            left  = _mm_srai_epi32(left,  16);
            right = _mm_srai_epi32(right, 16);
            _mm_storeu_si128((__m128i*)(pOutputSamples + i*8), drflac__mm_packs_interleaved_epi32(left, right));
        }
        for (i = (frameCount4 << 2); i < frameCount; ++i) {
            uint32_t mid  = pInputSamples0U32[i] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t side = pInputSamples1U32[i] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            mid = (mid << 1) | (side & 0x01);
            pOutputSamples[i*2+0] = (int16_t)(((mid + side) << shift) >> 16);
            pOutputSamples[i*2+1] = (int16_t)(((mid - side) << shift) >> 16);
        }
    }
}
#endif
#if defined(DR_FLAC_SUPPORT_NEON)
static DRFLAC_INLINE void drflac_read_pcm_frames_s16__decode_mid_side__neon(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int16_t* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift = unusedBitsPerSample;
    int32x4_t wbpsShift0_4;
    int32x4_t wbpsShift1_4;
    DR_FLAC_ASSERT(pFlac->currentFLACFrame.header.bitsPerSample <= 24);
    wbpsShift0_4 = vdupq_n_s32(pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample);
    wbpsShift1_4 = vdupq_n_s32(pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample);
    if (shift == 0) {
        for (i = 0; i < frameCount4; ++i) {
            uint32x4_t mid;
            uint32x4_t side;
            int32x4_t left;
            int32x4_t right;
            mid   = vshlq_u32(vld1q_u32(pInputSamples0U32 + i*4), wbpsShift0_4);
            side  = vshlq_u32(vld1q_u32(pInputSamples1U32 + i*4), wbpsShift1_4);
            mid   = vorrq_u32(vshlq_n_u32(mid, 1), vandq_u32(side, vdupq_n_u32(1)));
            left  = vshrq_n_s32(vreinterpretq_s32_u32(vaddq_u32(mid, side)), 1);
            right = vshrq_n_s32(vreinterpretq_s32_u32(vsubq_u32(mid, side)), 1);
            left  = vshrq_n_s32(left,  16);
            right = vshrq_n_s32(right, 16);
            drflac__vst2q_s16(pOutputSamples + i*8, vzip_s16(vmovn_s32(left), vmovn_s32(right)));
        }
        for (i = (frameCount4 << 2); i < frameCount; ++i) {
            uint32_t mid  = pInputSamples0U32[i] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t side = pInputSamples1U32[i] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            mid = (mid << 1) | (side & 0x01);
            pOutputSamples[i*2+0] = (int16_t)(((int32_t)(mid + side) >> 1) >> 16);
            pOutputSamples[i*2+1] = (int16_t)(((int32_t)(mid - side) >> 1) >> 16);
        }
    } else {
        int32x4_t shift4;
        shift -= 1;
        shift4 = vdupq_n_s32(shift);
        for (i = 0; i < frameCount4; ++i) {
            uint32x4_t mid;
            uint32x4_t side;
            int32x4_t left;
            int32x4_t right;
            mid   = vshlq_u32(vld1q_u32(pInputSamples0U32 + i*4), wbpsShift0_4);
            side  = vshlq_u32(vld1q_u32(pInputSamples1U32 + i*4), wbpsShift1_4);
            mid   = vorrq_u32(vshlq_n_u32(mid, 1), vandq_u32(side, vdupq_n_u32(1)));
            left  = vreinterpretq_s32_u32(vshlq_u32(vaddq_u32(mid, side), shift4));
            right = vreinterpretq_s32_u32(vshlq_u32(vsubq_u32(mid, side), shift4));
            left  = vshrq_n_s32(left,  16);
            right = vshrq_n_s32(right, 16);
            drflac__vst2q_s16(pOutputSamples + i*8, vzip_s16(vmovn_s32(left), vmovn_s32(right)));
        }
        for (i = (frameCount4 << 2); i < frameCount; ++i) {
            uint32_t mid  = pInputSamples0U32[i] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t side = pInputSamples1U32[i] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            mid = (mid << 1) | (side & 0x01);
            pOutputSamples[i*2+0] = (int16_t)(((mid + side) << shift) >> 16);
            pOutputSamples[i*2+1] = (int16_t)(((mid - side) << shift) >> 16);
        }
    }
}
#endif
static DRFLAC_INLINE void drflac_read_pcm_frames_s16__decode_mid_side(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int16_t* pOutputSamples)
{
#if defined(DR_FLAC_SUPPORT_SSE2)
    if (drflac__gIsSSE2Supported && pFlac->currentFLACFrame.header.bitsPerSample <= 24) {
        drflac_read_pcm_frames_s16__decode_mid_side__sse2(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
    } else
#elif defined(DR_FLAC_SUPPORT_NEON)
    if (drflac__gIsNEONSupported && pFlac->currentFLACFrame.header.bitsPerSample <= 24) {
        drflac_read_pcm_frames_s16__decode_mid_side__neon(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
    } else
#endif
    {
#if 0
        drflac_read_pcm_frames_s16__decode_mid_side__reference(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
#else
        drflac_read_pcm_frames_s16__decode_mid_side__scalar(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
#endif
    }
}
#if 0
static DRFLAC_INLINE void drflac_read_pcm_frames_s16__decode_independent_stereo__reference(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int16_t* pOutputSamples)
{
    for (uint64_t i = 0; i < frameCount; ++i) {
        pOutputSamples[i*2+0] = (int16_t)((int32_t)((uint32_t)pInputSamples0[i] << (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample)) >> 16);
        pOutputSamples[i*2+1] = (int16_t)((int32_t)((uint32_t)pInputSamples1[i] << (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample)) >> 16);
    }
}
#endif
static DRFLAC_INLINE void drflac_read_pcm_frames_s16__decode_independent_stereo__scalar(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int16_t* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
    uint32_t shift1 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
    for (i = 0; i < frameCount4; ++i) {
        uint32_t tempL0 = pInputSamples0U32[i*4+0] << shift0;
        uint32_t tempL1 = pInputSamples0U32[i*4+1] << shift0;
        uint32_t tempL2 = pInputSamples0U32[i*4+2] << shift0;
        uint32_t tempL3 = pInputSamples0U32[i*4+3] << shift0;
        uint32_t tempR0 = pInputSamples1U32[i*4+0] << shift1;
        uint32_t tempR1 = pInputSamples1U32[i*4+1] << shift1;
        uint32_t tempR2 = pInputSamples1U32[i*4+2] << shift1;
        uint32_t tempR3 = pInputSamples1U32[i*4+3] << shift1;
        tempL0 >>= 16;
        tempL1 >>= 16;
        tempL2 >>= 16;
        tempL3 >>= 16;
        tempR0 >>= 16;
        tempR1 >>= 16;
        tempR2 >>= 16;
        tempR3 >>= 16;
        pOutputSamples[i*8+0] = (int16_t)tempL0;
        pOutputSamples[i*8+1] = (int16_t)tempR0;
        pOutputSamples[i*8+2] = (int16_t)tempL1;
        pOutputSamples[i*8+3] = (int16_t)tempR1;
        pOutputSamples[i*8+4] = (int16_t)tempL2;
        pOutputSamples[i*8+5] = (int16_t)tempR2;
        pOutputSamples[i*8+6] = (int16_t)tempL3;
        pOutputSamples[i*8+7] = (int16_t)tempR3;
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        pOutputSamples[i*2+0] = (int16_t)((pInputSamples0U32[i] << shift0) >> 16);
        pOutputSamples[i*2+1] = (int16_t)((pInputSamples1U32[i] << shift1) >> 16);
    }
}
#if defined(DR_FLAC_SUPPORT_SSE2)
static DRFLAC_INLINE void drflac_read_pcm_frames_s16__decode_independent_stereo__sse2(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int16_t* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
    uint32_t shift1 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
    for (i = 0; i < frameCount4; ++i) {
        __m128i left  = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples0 + i), shift0);
        __m128i right = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples1 + i), shift1);
        left  = _mm_srai_epi32(left,  16);
        right = _mm_srai_epi32(right, 16);
        _mm_storeu_si128((__m128i*)(pOutputSamples + i*8), drflac__mm_packs_interleaved_epi32(left, right));
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        pOutputSamples[i*2+0] = (int16_t)((pInputSamples0U32[i] << shift0) >> 16);
        pOutputSamples[i*2+1] = (int16_t)((pInputSamples1U32[i] << shift1) >> 16);
    }
}
#endif
#if defined(DR_FLAC_SUPPORT_NEON)
static DRFLAC_INLINE void drflac_read_pcm_frames_s16__decode_independent_stereo__neon(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int16_t* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
    uint32_t shift1 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
    int32x4_t shift0_4 = vdupq_n_s32(shift0);
    int32x4_t shift1_4 = vdupq_n_s32(shift1);
    for (i = 0; i < frameCount4; ++i) {
        int32x4_t left;
        int32x4_t right;
        left  = vreinterpretq_s32_u32(vshlq_u32(vld1q_u32(pInputSamples0U32 + i*4), shift0_4));
        right = vreinterpretq_s32_u32(vshlq_u32(vld1q_u32(pInputSamples1U32 + i*4), shift1_4));
        left  = vshrq_n_s32(left,  16);
        right = vshrq_n_s32(right, 16);
        drflac__vst2q_s16(pOutputSamples + i*8, vzip_s16(vmovn_s32(left), vmovn_s32(right)));
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        pOutputSamples[i*2+0] = (int16_t)((pInputSamples0U32[i] << shift0) >> 16);
        pOutputSamples[i*2+1] = (int16_t)((pInputSamples1U32[i] << shift1) >> 16);
    }
}
#endif
static DRFLAC_INLINE void drflac_read_pcm_frames_s16__decode_independent_stereo(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, int16_t* pOutputSamples)
{
#if defined(DR_FLAC_SUPPORT_SSE2)
    if (drflac__gIsSSE2Supported && pFlac->currentFLACFrame.header.bitsPerSample <= 24) {
        drflac_read_pcm_frames_s16__decode_independent_stereo__sse2(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
    } else
#elif defined(DR_FLAC_SUPPORT_NEON)
    if (drflac__gIsNEONSupported && pFlac->currentFLACFrame.header.bitsPerSample <= 24) {
        drflac_read_pcm_frames_s16__decode_independent_stereo__neon(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
    } else
#endif
    {
#if 0
        drflac_read_pcm_frames_s16__decode_independent_stereo__reference(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
#else
        drflac_read_pcm_frames_s16__decode_independent_stereo__scalar(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
#endif
    }
}
static DRFLAC_INLINE void drflac_read_pcm_frames_s16__decode(drflac* pFlac, uint64_t frameCount, uint64_t iFirstPCMFrame, int16_t* pOutputSamples)
{
    unsigned int channelCount;
    uint32_t unusedBitsPerSample;
    DR_FLAC_ASSERT(pFlac->currentFLACFrame.header.bitsPerSample < 32);
    channelCount = drflac__get_channel_count_from_channel_assignment(pFlac->currentFLACFrame.header.channelAssignment);
    unusedBitsPerSample = 32 - pFlac->currentFLACFrame.header.bitsPerSample;
    if (channelCount == 2) {
        const int32_t* pDecodedSamples0 = pFlac->currentFLACFrame.subframes[0].samples.pS32 + iFirstPCMFrame;
        const int32_t* pDecodedSamples1 = pFlac->currentFLACFrame.subframes[1].samples.pS32 + iFirstPCMFrame;
        switch (pFlac->currentFLACFrame.header.channelAssignment)
        {
            case DR_FLAC_CHANNEL_ASSIGNMENT_LEFT_SIDE:
            {
                drflac_read_pcm_frames_s16__decode_left_side(pFlac, frameCount, unusedBitsPerSample, pDecodedSamples0, pDecodedSamples1, pOutputSamples);
            } break;
            case DR_FLAC_CHANNEL_ASSIGNMENT_RIGHT_SIDE:
            {
                drflac_read_pcm_frames_s16__decode_right_side(pFlac, frameCount, unusedBitsPerSample, pDecodedSamples0, pDecodedSamples1, pOutputSamples);
            } break;
            case DR_FLAC_CHANNEL_ASSIGNMENT_MID_SIDE:
            {
                drflac_read_pcm_frames_s16__decode_mid_side(pFlac, frameCount, unusedBitsPerSample, pDecodedSamples0, pDecodedSamples1, pOutputSamples);
            } break;
            case DR_FLAC_CHANNEL_ASSIGNMENT_INDEPENDENT_STEREO:
            {
                drflac_read_pcm_frames_s16__decode_independent_stereo(pFlac, frameCount, unusedBitsPerSample, pDecodedSamples0, pDecodedSamples1, pOutputSamples);
            } break;
            default: DR_FLAC_ASSERT(!"Invalid stereo channel assignment"); break;
        }
    } else {
        uint64_t i;
        for (i = 0; i < frameCount; ++i) {
            unsigned int j;
            for (j = 0; j < channelCount; ++j) {
                int32_t sampleS32;
                sampleS32 = (int32_t)((uint32_t)(pFlac->currentFLACFrame.subframes[j].samples.pS32[iFirstPCMFrame + i]) << (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[j].wastedBitsPerSample));
                pOutputSamples[(i*channelCount)+j] = (int16_t)(sampleS32 >> 16);
            }
        }
    }
}
DRFLAC_API uint64_t drflac_read_pcm_frames_s16(drflac* pFlac, uint64_t framesToRead, int16_t* pBufferOut)
{
    uint64_t framesRead;
    if (pFlac == NULL || framesToRead == 0) {
        return 0;
    }
    if (pBufferOut == NULL) {
        return drflac__seek_forward_by_pcm_frames(pFlac, framesToRead);
    }
    DR_FLAC_ASSERT(pFlac->bitsPerSample <= 32);
    framesRead = 0;
    while (framesToRead > 0) {
        if (pFlac->currentFLACFrame.pcmFramesRemaining == 0) {
            if (!drflac__read_and_decode_next_flac_frame(pFlac)) {
                break;
            }
        } else {
            unsigned int channelCount = drflac__get_channel_count_from_channel_assignment(pFlac->currentFLACFrame.header.channelAssignment);
            uint64_t iFirstPCMFrame = pFlac->currentFLACFrame.header.blockSizeInPCMFrames - pFlac->currentFLACFrame.pcmFramesRemaining;
            uint64_t frameCountThisIteration = framesToRead;
            if (frameCountThisIteration > pFlac->currentFLACFrame.pcmFramesRemaining) {
                frameCountThisIteration = pFlac->currentFLACFrame.pcmFramesRemaining;
            }
            if (pFlac->bitsPerSample != 32) {
                drflac_read_pcm_frames_s16__decode(pFlac, frameCountThisIteration, iFirstPCMFrame, pBufferOut);
            } else {
                drflac_read_pcm_frames_s16__decode_s64(pFlac, frameCountThisIteration, iFirstPCMFrame, pBufferOut);
            }
            framesRead             += frameCountThisIteration;
            pBufferOut             += frameCountThisIteration * channelCount;
            framesToRead           -= frameCountThisIteration;
            pFlac->currentPCMFrame += frameCountThisIteration;
            pFlac->currentFLACFrame.pcmFramesRemaining -= (uint32_t)frameCountThisIteration;
        }
    }
    return framesRead;
}
#if 0
static DRFLAC_INLINE void drflac_read_pcm_frames_f32__decode_left_side__reference(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, float* pOutputSamples)
{
    uint64_t i;
    for (i = 0; i < frameCount; ++i) {
        uint32_t left  = (uint32_t)pInputSamples0[i] << (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample);
        uint32_t side  = (uint32_t)pInputSamples1[i] << (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample);
        uint32_t right = left - side;
        pOutputSamples[i*2+0] = (float)((int32_t)left  / 2147483648.0);
        pOutputSamples[i*2+1] = (float)((int32_t)right / 2147483648.0);
    }
}
#endif
static DRFLAC_INLINE void drflac_read_pcm_frames_f32__decode_left_side__scalar(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, float* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
    uint32_t shift1 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
    float factor = 1 / 2147483648.0;
    for (i = 0; i < frameCount4; ++i) {
        uint32_t left0 = pInputSamples0U32[i*4+0] << shift0;
        uint32_t left1 = pInputSamples0U32[i*4+1] << shift0;
        uint32_t left2 = pInputSamples0U32[i*4+2] << shift0;
        uint32_t left3 = pInputSamples0U32[i*4+3] << shift0;
        uint32_t side0 = pInputSamples1U32[i*4+0] << shift1;
        uint32_t side1 = pInputSamples1U32[i*4+1] << shift1;
        uint32_t side2 = pInputSamples1U32[i*4+2] << shift1;
        uint32_t side3 = pInputSamples1U32[i*4+3] << shift1;
        uint32_t right0 = left0 - side0;
        uint32_t right1 = left1 - side1;
        uint32_t right2 = left2 - side2;
        uint32_t right3 = left3 - side3;
        pOutputSamples[i*8+0] = (int32_t)left0  * factor;
        pOutputSamples[i*8+1] = (int32_t)right0 * factor;
        pOutputSamples[i*8+2] = (int32_t)left1  * factor;
        pOutputSamples[i*8+3] = (int32_t)right1 * factor;
        pOutputSamples[i*8+4] = (int32_t)left2  * factor;
        pOutputSamples[i*8+5] = (int32_t)right2 * factor;
        pOutputSamples[i*8+6] = (int32_t)left3  * factor;
        pOutputSamples[i*8+7] = (int32_t)right3 * factor;
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        uint32_t left  = pInputSamples0U32[i] << shift0;
        uint32_t side  = pInputSamples1U32[i] << shift1;
        uint32_t right = left - side;
        pOutputSamples[i*2+0] = (int32_t)left  * factor;
        pOutputSamples[i*2+1] = (int32_t)right * factor;
    }
}
#if defined(DR_FLAC_SUPPORT_SSE2)
static DRFLAC_INLINE void drflac_read_pcm_frames_f32__decode_left_side__sse2(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, float* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample) - 8;
    uint32_t shift1 = (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample) - 8;
    __m128 factor;
    DR_FLAC_ASSERT(pFlac->currentFLACFrame.header.bitsPerSample <= 24);
    factor = _mm_set1_ps(1.0f / 8388608.0f);
    for (i = 0; i < frameCount4; ++i) {
        __m128i left  = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples0 + i), shift0);
        __m128i side  = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples1 + i), shift1);
        __m128i right = _mm_sub_epi32(left, side);
        __m128 leftf  = _mm_mul_ps(_mm_cvtepi32_ps(left),  factor);
        __m128 rightf = _mm_mul_ps(_mm_cvtepi32_ps(right), factor);
        _mm_storeu_ps(pOutputSamples + i*8 + 0, _mm_unpacklo_ps(leftf, rightf));
        _mm_storeu_ps(pOutputSamples + i*8 + 4, _mm_unpackhi_ps(leftf, rightf));
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        uint32_t left  = pInputSamples0U32[i] << shift0;
        uint32_t side  = pInputSamples1U32[i] << shift1;
        uint32_t right = left - side;
        pOutputSamples[i*2+0] = (int32_t)left  / 8388608.0f;
        pOutputSamples[i*2+1] = (int32_t)right / 8388608.0f;
    }
}
#endif
#if defined(DR_FLAC_SUPPORT_NEON)
static DRFLAC_INLINE void drflac_read_pcm_frames_f32__decode_left_side__neon(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, float* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample) - 8;
    uint32_t shift1 = (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample) - 8;
    float32x4_t factor4;
    int32x4_t shift0_4;
    int32x4_t shift1_4;
    DR_FLAC_ASSERT(pFlac->currentFLACFrame.header.bitsPerSample <= 24);
    factor4  = vdupq_n_f32(1.0f / 8388608.0f);
    shift0_4 = vdupq_n_s32(shift0);
    shift1_4 = vdupq_n_s32(shift1);
    for (i = 0; i < frameCount4; ++i) {
        uint32x4_t left;
        uint32x4_t side;
        uint32x4_t right;
        float32x4_t leftf;
        float32x4_t rightf;
        left   = vshlq_u32(vld1q_u32(pInputSamples0U32 + i*4), shift0_4);
        side   = vshlq_u32(vld1q_u32(pInputSamples1U32 + i*4), shift1_4);
        right  = vsubq_u32(left, side);
        leftf  = vmulq_f32(vcvtq_f32_s32(vreinterpretq_s32_u32(left)),  factor4);
        rightf = vmulq_f32(vcvtq_f32_s32(vreinterpretq_s32_u32(right)), factor4);
        drflac__vst2q_f32(pOutputSamples + i*8, vzipq_f32(leftf, rightf));
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        uint32_t left  = pInputSamples0U32[i] << shift0;
        uint32_t side  = pInputSamples1U32[i] << shift1;
        uint32_t right = left - side;
        pOutputSamples[i*2+0] = (int32_t)left  / 8388608.0f;
        pOutputSamples[i*2+1] = (int32_t)right / 8388608.0f;
    }
}
#endif
static DRFLAC_INLINE void drflac_read_pcm_frames_f32__decode_duplicate_mono(uint64_t frameCount, uint32_t shift, const int32_t* pInputSamples, float* pOutputSamples)
{
    uint64_t i;
    const uint32_t* pInputSamplesU32 = (const uint32_t*)pInputSamples;
    float factor;
    DR_FLAC_ASSERT(shift < 32);
    factor = 1 / 2147483648.0f;
    for (i = 0; i < frameCount; ++i) {
        float sample;
        sample = (int32_t)(pInputSamplesU32[i] << shift) * factor;
        pOutputSamples[i*2+0] = sample;
        pOutputSamples[i*2+1] = sample;
    }
}
static DRFLAC_INLINE void drflac_read_pcm_frames_f32__decode_left_side(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, float* pOutputSamples)
{
    if (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample == 32) {
        drflac_read_pcm_frames_f32__decode_duplicate_mono(frameCount, unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample, pInputSamples0, pOutputSamples);
        return;
    }
#if defined(DR_FLAC_SUPPORT_SSE2)
    if (drflac__gIsSSE2Supported && pFlac->currentFLACFrame.header.bitsPerSample <= 24) {
        drflac_read_pcm_frames_f32__decode_left_side__sse2(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
    } else
#elif defined(DR_FLAC_SUPPORT_NEON)
    if (drflac__gIsNEONSupported && pFlac->currentFLACFrame.header.bitsPerSample <= 24) {
        drflac_read_pcm_frames_f32__decode_left_side__neon(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
    } else
#endif
    {
#if 0
        drflac_read_pcm_frames_f32__decode_left_side__reference(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
#else
        drflac_read_pcm_frames_f32__decode_left_side__scalar(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
#endif
    }
}
#if 0
static DRFLAC_INLINE void drflac_read_pcm_frames_f32__decode_right_side__reference(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, float* pOutputSamples)
{
    uint64_t i;
    for (i = 0; i < frameCount; ++i) {
        uint32_t side  = (uint32_t)pInputSamples0[i] << (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample);
        uint32_t right = (uint32_t)pInputSamples1[i] << (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample);
        uint32_t left  = right + side;
        pOutputSamples[i*2+0] = (float)((int32_t)left  / 2147483648.0);
        pOutputSamples[i*2+1] = (float)((int32_t)right / 2147483648.0);
    }
}
#endif
static DRFLAC_INLINE void drflac_read_pcm_frames_f32__decode_right_side__scalar(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, float* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
    uint32_t shift1 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
    float factor = 1 / 2147483648.0;
    for (i = 0; i < frameCount4; ++i) {
        uint32_t side0  = pInputSamples0U32[i*4+0] << shift0;
        uint32_t side1  = pInputSamples0U32[i*4+1] << shift0;
        uint32_t side2  = pInputSamples0U32[i*4+2] << shift0;
        uint32_t side3  = pInputSamples0U32[i*4+3] << shift0;
        uint32_t right0 = pInputSamples1U32[i*4+0] << shift1;
        uint32_t right1 = pInputSamples1U32[i*4+1] << shift1;
        uint32_t right2 = pInputSamples1U32[i*4+2] << shift1;
        uint32_t right3 = pInputSamples1U32[i*4+3] << shift1;
        uint32_t left0 = right0 + side0;
        uint32_t left1 = right1 + side1;
        uint32_t left2 = right2 + side2;
        uint32_t left3 = right3 + side3;
        pOutputSamples[i*8+0] = (int32_t)left0  * factor;
        pOutputSamples[i*8+1] = (int32_t)right0 * factor;
        pOutputSamples[i*8+2] = (int32_t)left1  * factor;
        pOutputSamples[i*8+3] = (int32_t)right1 * factor;
        pOutputSamples[i*8+4] = (int32_t)left2  * factor;
        pOutputSamples[i*8+5] = (int32_t)right2 * factor;
        pOutputSamples[i*8+6] = (int32_t)left3  * factor;
        pOutputSamples[i*8+7] = (int32_t)right3 * factor;
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        uint32_t side  = pInputSamples0U32[i] << shift0;
        uint32_t right = pInputSamples1U32[i] << shift1;
        uint32_t left  = right + side;
        pOutputSamples[i*2+0] = (int32_t)left  * factor;
        pOutputSamples[i*2+1] = (int32_t)right * factor;
    }
}
#if defined(DR_FLAC_SUPPORT_SSE2)
static DRFLAC_INLINE void drflac_read_pcm_frames_f32__decode_right_side__sse2(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, float* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample) - 8;
    uint32_t shift1 = (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample) - 8;
    __m128 factor;
    DR_FLAC_ASSERT(pFlac->currentFLACFrame.header.bitsPerSample <= 24);
    factor = _mm_set1_ps(1.0f / 8388608.0f);
    for (i = 0; i < frameCount4; ++i) {
        __m128i side  = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples0 + i), shift0);
        __m128i right = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples1 + i), shift1);
        __m128i left  = _mm_add_epi32(right, side);
        __m128 leftf  = _mm_mul_ps(_mm_cvtepi32_ps(left),  factor);
        __m128 rightf = _mm_mul_ps(_mm_cvtepi32_ps(right), factor);
        _mm_storeu_ps(pOutputSamples + i*8 + 0, _mm_unpacklo_ps(leftf, rightf));
        _mm_storeu_ps(pOutputSamples + i*8 + 4, _mm_unpackhi_ps(leftf, rightf));
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        uint32_t side  = pInputSamples0U32[i] << shift0;
        uint32_t right = pInputSamples1U32[i] << shift1;
        uint32_t left  = right + side;
        pOutputSamples[i*2+0] = (int32_t)left  / 8388608.0f;
        pOutputSamples[i*2+1] = (int32_t)right / 8388608.0f;
    }
}
#endif
#if defined(DR_FLAC_SUPPORT_NEON)
static DRFLAC_INLINE void drflac_read_pcm_frames_f32__decode_right_side__neon(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, float* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample) - 8;
    uint32_t shift1 = (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample) - 8;
    float32x4_t factor4;
    int32x4_t shift0_4;
    int32x4_t shift1_4;
    DR_FLAC_ASSERT(pFlac->currentFLACFrame.header.bitsPerSample <= 24);
    factor4  = vdupq_n_f32(1.0f / 8388608.0f);
    shift0_4 = vdupq_n_s32(shift0);
    shift1_4 = vdupq_n_s32(shift1);
    for (i = 0; i < frameCount4; ++i) {
        uint32x4_t side;
        uint32x4_t right;
        uint32x4_t left;
        float32x4_t leftf;
        float32x4_t rightf;
        side   = vshlq_u32(vld1q_u32(pInputSamples0U32 + i*4), shift0_4);
        right  = vshlq_u32(vld1q_u32(pInputSamples1U32 + i*4), shift1_4);
        left   = vaddq_u32(right, side);
        leftf  = vmulq_f32(vcvtq_f32_s32(vreinterpretq_s32_u32(left)),  factor4);
        rightf = vmulq_f32(vcvtq_f32_s32(vreinterpretq_s32_u32(right)), factor4);
        drflac__vst2q_f32(pOutputSamples + i*8, vzipq_f32(leftf, rightf));
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        uint32_t side  = pInputSamples0U32[i] << shift0;
        uint32_t right = pInputSamples1U32[i] << shift1;
        uint32_t left  = right + side;
        pOutputSamples[i*2+0] = (int32_t)left  / 8388608.0f;
        pOutputSamples[i*2+1] = (int32_t)right / 8388608.0f;
    }
}
#endif
static DRFLAC_INLINE void drflac_read_pcm_frames_f32__decode_right_side(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, float* pOutputSamples)
{
    if (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample == 32) {
        drflac_read_pcm_frames_f32__decode_duplicate_mono(frameCount, unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample, pInputSamples1, pOutputSamples);
        return;
    }
#if defined(DR_FLAC_SUPPORT_SSE2)
    if (drflac__gIsSSE2Supported && pFlac->currentFLACFrame.header.bitsPerSample <= 24) {
        drflac_read_pcm_frames_f32__decode_right_side__sse2(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
    } else
#elif defined(DR_FLAC_SUPPORT_NEON)
    if (drflac__gIsNEONSupported && pFlac->currentFLACFrame.header.bitsPerSample <= 24) {
        drflac_read_pcm_frames_f32__decode_right_side__neon(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
    } else
#endif
    {
#if 0
        drflac_read_pcm_frames_f32__decode_right_side__reference(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
#else
        drflac_read_pcm_frames_f32__decode_right_side__scalar(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
#endif
    }
}
#if 0
static DRFLAC_INLINE void drflac_read_pcm_frames_f32__decode_mid_side__reference(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, float* pOutputSamples)
{
    for (uint64_t i = 0; i < frameCount; ++i) {
        uint32_t mid  = (uint32_t)pInputSamples0[i] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
        uint32_t side = (uint32_t)pInputSamples1[i] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
        mid = (mid << 1) | (side & 0x01);
        pOutputSamples[i*2+0] = (float)((((int32_t)(mid + side) >> 1) << (unusedBitsPerSample)) / 2147483648.0);
        pOutputSamples[i*2+1] = (float)((((int32_t)(mid - side) >> 1) << (unusedBitsPerSample)) / 2147483648.0);
    }
}
#endif
static DRFLAC_INLINE void drflac_read_pcm_frames_f32__decode_mid_side__scalar(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, float* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift = unusedBitsPerSample;
    float factor = 1 / 2147483648.0;
    if (shift > 0) {
        for (i = 0; i < frameCount4; ++i) {
            uint32_t temp0L;
            uint32_t temp1L;
            uint32_t temp2L;
            uint32_t temp3L;
            uint32_t temp0R;
            uint32_t temp1R;
            uint32_t temp2R;
            uint32_t temp3R;
            uint32_t mid0  = pInputSamples0U32[i*4+0] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t mid1  = pInputSamples0U32[i*4+1] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t mid2  = pInputSamples0U32[i*4+2] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t mid3  = pInputSamples0U32[i*4+3] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t side0 = pInputSamples1U32[i*4+0] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            uint32_t side1 = pInputSamples1U32[i*4+1] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            uint32_t side2 = pInputSamples1U32[i*4+2] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            uint32_t side3 = pInputSamples1U32[i*4+3] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            mid0 = (mid0 << 1) | (side0 & 0x01);
            mid1 = (mid1 << 1) | (side1 & 0x01);
            mid2 = (mid2 << 1) | (side2 & 0x01);
            mid3 = (mid3 << 1) | (side3 & 0x01);
            temp0L = (mid0 + side0) << (shift - 1);
            temp1L = (mid1 + side1) << (shift - 1);
            temp2L = (mid2 + side2) << (shift - 1);
            temp3L = (mid3 + side3) << (shift - 1);
            temp0R = (mid0 - side0) << (shift - 1);
            temp1R = (mid1 - side1) << (shift - 1);
            temp2R = (mid2 - side2) << (shift - 1);
            temp3R = (mid3 - side3) << (shift - 1);
            pOutputSamples[i*8+0] = (int32_t)temp0L * factor;
            pOutputSamples[i*8+1] = (int32_t)temp0R * factor;
            pOutputSamples[i*8+2] = (int32_t)temp1L * factor;
            pOutputSamples[i*8+3] = (int32_t)temp1R * factor;
            pOutputSamples[i*8+4] = (int32_t)temp2L * factor;
            pOutputSamples[i*8+5] = (int32_t)temp2R * factor;
            pOutputSamples[i*8+6] = (int32_t)temp3L * factor;
            pOutputSamples[i*8+7] = (int32_t)temp3R * factor;
        }
    } else {
        for (i = 0; i < frameCount4; ++i) {
            uint32_t temp0L;
            uint32_t temp1L;
            uint32_t temp2L;
            uint32_t temp3L;
            uint32_t temp0R;
            uint32_t temp1R;
            uint32_t temp2R;
            uint32_t temp3R;
            uint64_t mid0  = (uint64_t)(int64_t)pInputSamples0[i*4+0] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint64_t mid1  = (uint64_t)(int64_t)pInputSamples0[i*4+1] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint64_t mid2  = (uint64_t)(int64_t)pInputSamples0[i*4+2] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint64_t mid3  = (uint64_t)(int64_t)pInputSamples0[i*4+3] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint64_t side0 = (uint64_t)(int64_t)pInputSamples1[i*4+0] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            uint64_t side1 = (uint64_t)(int64_t)pInputSamples1[i*4+1] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            uint64_t side2 = (uint64_t)(int64_t)pInputSamples1[i*4+2] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            uint64_t side3 = (uint64_t)(int64_t)pInputSamples1[i*4+3] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            mid0 = (mid0 << 1) | (side0 & 0x01);
            mid1 = (mid1 << 1) | (side1 & 0x01);
            mid2 = (mid2 << 1) | (side2 & 0x01);
            mid3 = (mid3 << 1) | (side3 & 0x01);
            temp0L = (uint32_t)((int64_t)(mid0 + side0) >> 1);
            temp1L = (uint32_t)((int64_t)(mid1 + side1) >> 1);
            temp2L = (uint32_t)((int64_t)(mid2 + side2) >> 1);
            temp3L = (uint32_t)((int64_t)(mid3 + side3) >> 1);
            temp0R = (uint32_t)((int64_t)(mid0 - side0) >> 1);
            temp1R = (uint32_t)((int64_t)(mid1 - side1) >> 1);
            temp2R = (uint32_t)((int64_t)(mid2 - side2) >> 1);
            temp3R = (uint32_t)((int64_t)(mid3 - side3) >> 1);
            pOutputSamples[i*8+0] = (int32_t)temp0L * factor;
            pOutputSamples[i*8+1] = (int32_t)temp0R * factor;
            pOutputSamples[i*8+2] = (int32_t)temp1L * factor;
            pOutputSamples[i*8+3] = (int32_t)temp1R * factor;
            pOutputSamples[i*8+4] = (int32_t)temp2L * factor;
            pOutputSamples[i*8+5] = (int32_t)temp2R * factor;
            pOutputSamples[i*8+6] = (int32_t)temp3L * factor;
            pOutputSamples[i*8+7] = (int32_t)temp3R * factor;
        }
    }
    if (shift > 0) {
        for (i = (frameCount4 << 2); i < frameCount; ++i) {
            uint32_t mid  = pInputSamples0U32[i] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t side = pInputSamples1U32[i] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            mid = (mid << 1) | (side & 0x01);
            pOutputSamples[i*2+0] = (int32_t)((uint32_t)((int32_t)(mid + side) >> 1) << unusedBitsPerSample) * factor;
            pOutputSamples[i*2+1] = (int32_t)((uint32_t)((int32_t)(mid - side) >> 1) << unusedBitsPerSample) * factor;
        }
    } else {
        for (i = (frameCount4 << 2); i < frameCount; ++i) {
            uint64_t mid;
            uint64_t side;
            mid  = (uint64_t)(int64_t)pInputSamples0[i] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            side = (uint64_t)(int64_t)pInputSamples1[i] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            mid = (mid << 1) | (side & 0x01);
            pOutputSamples[i*2+0] = (int32_t)((int64_t)(mid + side) >> 1) * factor;
            pOutputSamples[i*2+1] = (int32_t)((int64_t)(mid - side) >> 1) * factor;
        }
    }
}
#if defined(DR_FLAC_SUPPORT_SSE2)
static DRFLAC_INLINE void drflac_read_pcm_frames_f32__decode_mid_side__sse2(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, float* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift = unusedBitsPerSample - 8;
    float factor;
    __m128 factor128;
    DR_FLAC_ASSERT(pFlac->currentFLACFrame.header.bitsPerSample <= 24);
    factor = 1.0f / 8388608.0f;
    factor128 = _mm_set1_ps(factor);
    if (shift == 0) {
        for (i = 0; i < frameCount4; ++i) {
            __m128i mid;
            __m128i side;
            __m128i tempL;
            __m128i tempR;
            __m128  leftf;
            __m128  rightf;
            mid    = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples0 + i), pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample);
            side   = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples1 + i), pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample);
            mid    = _mm_or_si128(_mm_slli_epi32(mid, 1), _mm_and_si128(side, _mm_set1_epi32(0x01)));
            tempL  = _mm_srai_epi32(_mm_add_epi32(mid, side), 1);
            tempR  = _mm_srai_epi32(_mm_sub_epi32(mid, side), 1);
            leftf  = _mm_mul_ps(_mm_cvtepi32_ps(tempL), factor128);
            rightf = _mm_mul_ps(_mm_cvtepi32_ps(tempR), factor128);
            _mm_storeu_ps(pOutputSamples + i*8 + 0, _mm_unpacklo_ps(leftf, rightf));
            _mm_storeu_ps(pOutputSamples + i*8 + 4, _mm_unpackhi_ps(leftf, rightf));
        }
        for (i = (frameCount4 << 2); i < frameCount; ++i) {
            uint32_t mid  = pInputSamples0U32[i] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t side = pInputSamples1U32[i] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            mid = (mid << 1) | (side & 0x01);
            pOutputSamples[i*2+0] = ((int32_t)(mid + side) >> 1) * factor;
            pOutputSamples[i*2+1] = ((int32_t)(mid - side) >> 1) * factor;
        }
    } else {
        shift -= 1;
        for (i = 0; i < frameCount4; ++i) {
            __m128i mid;
            __m128i side;
            __m128i tempL;
            __m128i tempR;
            __m128 leftf;
            __m128 rightf;
            mid    = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples0 + i), pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample);
            side   = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples1 + i), pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample);
            mid    = _mm_or_si128(_mm_slli_epi32(mid, 1), _mm_and_si128(side, _mm_set1_epi32(0x01)));
            tempL  = _mm_slli_epi32(_mm_add_epi32(mid, side), shift);
            tempR  = _mm_slli_epi32(_mm_sub_epi32(mid, side), shift);
            leftf  = _mm_mul_ps(_mm_cvtepi32_ps(tempL), factor128);
            rightf = _mm_mul_ps(_mm_cvtepi32_ps(tempR), factor128);
            _mm_storeu_ps(pOutputSamples + i*8 + 0, _mm_unpacklo_ps(leftf, rightf));
            _mm_storeu_ps(pOutputSamples + i*8 + 4, _mm_unpackhi_ps(leftf, rightf));
        }
        for (i = (frameCount4 << 2); i < frameCount; ++i) {
            uint32_t mid  = pInputSamples0U32[i] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t side = pInputSamples1U32[i] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            mid = (mid << 1) | (side & 0x01);
            pOutputSamples[i*2+0] = (int32_t)((mid + side) << shift) * factor;
            pOutputSamples[i*2+1] = (int32_t)((mid - side) << shift) * factor;
        }
    }
}
#endif
#if defined(DR_FLAC_SUPPORT_NEON)
static DRFLAC_INLINE void drflac_read_pcm_frames_f32__decode_mid_side__neon(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, float* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift = unusedBitsPerSample - 8;
    float factor;
    float32x4_t factor4;
    int32x4_t shift4;
    int32x4_t wbps0_4;
    int32x4_t wbps1_4;
    DR_FLAC_ASSERT(pFlac->currentFLACFrame.header.bitsPerSample <= 24);
    factor  = 1.0f / 8388608.0f;
    factor4 = vdupq_n_f32(factor);
    wbps0_4 = vdupq_n_s32(pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample);
    wbps1_4 = vdupq_n_s32(pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample);
    if (shift == 0) {
        for (i = 0; i < frameCount4; ++i) {
            int32x4_t lefti;
            int32x4_t righti;
            float32x4_t leftf;
            float32x4_t rightf;
            uint32x4_t mid  = vshlq_u32(vld1q_u32(pInputSamples0U32 + i*4), wbps0_4);
            uint32x4_t side = vshlq_u32(vld1q_u32(pInputSamples1U32 + i*4), wbps1_4);
            mid    = vorrq_u32(vshlq_n_u32(mid, 1), vandq_u32(side, vdupq_n_u32(1)));
            lefti  = vshrq_n_s32(vreinterpretq_s32_u32(vaddq_u32(mid, side)), 1);
            righti = vshrq_n_s32(vreinterpretq_s32_u32(vsubq_u32(mid, side)), 1);
            leftf  = vmulq_f32(vcvtq_f32_s32(lefti),  factor4);
            rightf = vmulq_f32(vcvtq_f32_s32(righti), factor4);
            drflac__vst2q_f32(pOutputSamples + i*8, vzipq_f32(leftf, rightf));
        }
        for (i = (frameCount4 << 2); i < frameCount; ++i) {
            uint32_t mid  = pInputSamples0U32[i] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t side = pInputSamples1U32[i] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            mid = (mid << 1) | (side & 0x01);
            pOutputSamples[i*2+0] = ((int32_t)(mid + side) >> 1) * factor;
            pOutputSamples[i*2+1] = ((int32_t)(mid - side) >> 1) * factor;
        }
    } else {
        shift -= 1;
        shift4 = vdupq_n_s32(shift);
        for (i = 0; i < frameCount4; ++i) {
            uint32x4_t mid;
            uint32x4_t side;
            int32x4_t lefti;
            int32x4_t righti;
            float32x4_t leftf;
            float32x4_t rightf;
            mid    = vshlq_u32(vld1q_u32(pInputSamples0U32 + i*4), wbps0_4);
            side   = vshlq_u32(vld1q_u32(pInputSamples1U32 + i*4), wbps1_4);
            mid    = vorrq_u32(vshlq_n_u32(mid, 1), vandq_u32(side, vdupq_n_u32(1)));
            lefti  = vreinterpretq_s32_u32(vshlq_u32(vaddq_u32(mid, side), shift4));
            righti = vreinterpretq_s32_u32(vshlq_u32(vsubq_u32(mid, side), shift4));
            leftf  = vmulq_f32(vcvtq_f32_s32(lefti),  factor4);
            rightf = vmulq_f32(vcvtq_f32_s32(righti), factor4);
            drflac__vst2q_f32(pOutputSamples + i*8, vzipq_f32(leftf, rightf));
        }
        for (i = (frameCount4 << 2); i < frameCount; ++i) {
            uint32_t mid  = pInputSamples0U32[i] << pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
            uint32_t side = pInputSamples1U32[i] << pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
            mid = (mid << 1) | (side & 0x01);
            pOutputSamples[i*2+0] = (int32_t)((mid + side) << shift) * factor;
            pOutputSamples[i*2+1] = (int32_t)((mid - side) << shift) * factor;
        }
    }
}
#endif
static DRFLAC_INLINE void drflac_read_pcm_frames_f32__decode_mid_side(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, float* pOutputSamples)
{
#if defined(DR_FLAC_SUPPORT_SSE2)
    if (drflac__gIsSSE2Supported && pFlac->currentFLACFrame.header.bitsPerSample <= 24) {
        drflac_read_pcm_frames_f32__decode_mid_side__sse2(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
    } else
#elif defined(DR_FLAC_SUPPORT_NEON)
    if (drflac__gIsNEONSupported && pFlac->currentFLACFrame.header.bitsPerSample <= 24) {
        drflac_read_pcm_frames_f32__decode_mid_side__neon(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
    } else
#endif
    {
#if 0
        drflac_read_pcm_frames_f32__decode_mid_side__reference(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
#else
        drflac_read_pcm_frames_f32__decode_mid_side__scalar(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
#endif
    }
}
#if 0
static DRFLAC_INLINE void drflac_read_pcm_frames_f32__decode_independent_stereo__reference(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, float* pOutputSamples)
{
    for (uint64_t i = 0; i < frameCount; ++i) {
        pOutputSamples[i*2+0] = (float)((int32_t)((uint32_t)pInputSamples0[i] << (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample)) / 2147483648.0);
        pOutputSamples[i*2+1] = (float)((int32_t)((uint32_t)pInputSamples1[i] << (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample)) / 2147483648.0);
    }
}
#endif
static DRFLAC_INLINE void drflac_read_pcm_frames_f32__decode_independent_stereo__scalar(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, float* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample;
    uint32_t shift1 = unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample;
    float factor = 1 / 2147483648.0;
    for (i = 0; i < frameCount4; ++i) {
        uint32_t tempL0 = pInputSamples0U32[i*4+0] << shift0;
        uint32_t tempL1 = pInputSamples0U32[i*4+1] << shift0;
        uint32_t tempL2 = pInputSamples0U32[i*4+2] << shift0;
        uint32_t tempL3 = pInputSamples0U32[i*4+3] << shift0;
        uint32_t tempR0 = pInputSamples1U32[i*4+0] << shift1;
        uint32_t tempR1 = pInputSamples1U32[i*4+1] << shift1;
        uint32_t tempR2 = pInputSamples1U32[i*4+2] << shift1;
        uint32_t tempR3 = pInputSamples1U32[i*4+3] << shift1;
        pOutputSamples[i*8+0] = (int32_t)tempL0 * factor;
        pOutputSamples[i*8+1] = (int32_t)tempR0 * factor;
        pOutputSamples[i*8+2] = (int32_t)tempL1 * factor;
        pOutputSamples[i*8+3] = (int32_t)tempR1 * factor;
        pOutputSamples[i*8+4] = (int32_t)tempL2 * factor;
        pOutputSamples[i*8+5] = (int32_t)tempR2 * factor;
        pOutputSamples[i*8+6] = (int32_t)tempL3 * factor;
        pOutputSamples[i*8+7] = (int32_t)tempR3 * factor;
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        pOutputSamples[i*2+0] = (int32_t)(pInputSamples0U32[i] << shift0) * factor;
        pOutputSamples[i*2+1] = (int32_t)(pInputSamples1U32[i] << shift1) * factor;
    }
}
#if defined(DR_FLAC_SUPPORT_SSE2)
static DRFLAC_INLINE void drflac_read_pcm_frames_f32__decode_independent_stereo__sse2(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, float* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample) - 8;
    uint32_t shift1 = (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample) - 8;
    float factor = 1.0f / 8388608.0f;
    __m128 factor128 = _mm_set1_ps(factor);
    for (i = 0; i < frameCount4; ++i) {
        __m128i lefti;
        __m128i righti;
        __m128 leftf;
        __m128 rightf;
        lefti  = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples0 + i), shift0);
        righti = _mm_slli_epi32(_mm_loadu_si128((const __m128i*)pInputSamples1 + i), shift1);
        leftf  = _mm_mul_ps(_mm_cvtepi32_ps(lefti),  factor128);
        rightf = _mm_mul_ps(_mm_cvtepi32_ps(righti), factor128);
        _mm_storeu_ps(pOutputSamples + i*8 + 0, _mm_unpacklo_ps(leftf, rightf));
        _mm_storeu_ps(pOutputSamples + i*8 + 4, _mm_unpackhi_ps(leftf, rightf));
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        pOutputSamples[i*2+0] = (int32_t)(pInputSamples0U32[i] << shift0) * factor;
        pOutputSamples[i*2+1] = (int32_t)(pInputSamples1U32[i] << shift1) * factor;
    }
}
#endif
#if defined(DR_FLAC_SUPPORT_NEON)
static DRFLAC_INLINE void drflac_read_pcm_frames_f32__decode_independent_stereo__neon(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, float* pOutputSamples)
{
    uint64_t i;
    uint64_t frameCount4 = frameCount >> 2;
    const uint32_t* pInputSamples0U32 = (const uint32_t*)pInputSamples0;
    const uint32_t* pInputSamples1U32 = (const uint32_t*)pInputSamples1;
    uint32_t shift0 = (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[0].wastedBitsPerSample) - 8;
    uint32_t shift1 = (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[1].wastedBitsPerSample) - 8;
    float factor = 1.0f / 8388608.0f;
    float32x4_t factor4 = vdupq_n_f32(factor);
    int32x4_t shift0_4  = vdupq_n_s32(shift0);
    int32x4_t shift1_4  = vdupq_n_s32(shift1);
    for (i = 0; i < frameCount4; ++i) {
        int32x4_t lefti;
        int32x4_t righti;
        float32x4_t leftf;
        float32x4_t rightf;
        lefti  = vreinterpretq_s32_u32(vshlq_u32(vld1q_u32(pInputSamples0U32 + i*4), shift0_4));
        righti = vreinterpretq_s32_u32(vshlq_u32(vld1q_u32(pInputSamples1U32 + i*4), shift1_4));
        leftf  = vmulq_f32(vcvtq_f32_s32(lefti),  factor4);
        rightf = vmulq_f32(vcvtq_f32_s32(righti), factor4);
        drflac__vst2q_f32(pOutputSamples + i*8, vzipq_f32(leftf, rightf));
    }
    for (i = (frameCount4 << 2); i < frameCount; ++i) {
        pOutputSamples[i*2+0] = (int32_t)(pInputSamples0U32[i] << shift0) * factor;
        pOutputSamples[i*2+1] = (int32_t)(pInputSamples1U32[i] << shift1) * factor;
    }
}
#endif
static DRFLAC_INLINE void drflac_read_pcm_frames_f32__decode_independent_stereo(drflac* pFlac, uint64_t frameCount, uint32_t unusedBitsPerSample, const int32_t* pInputSamples0, const int32_t* pInputSamples1, float* pOutputSamples)
{
#if defined(DR_FLAC_SUPPORT_SSE2)
    if (drflac__gIsSSE2Supported && pFlac->currentFLACFrame.header.bitsPerSample <= 24) {
        drflac_read_pcm_frames_f32__decode_independent_stereo__sse2(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
    } else
#elif defined(DR_FLAC_SUPPORT_NEON)
    if (drflac__gIsNEONSupported && pFlac->currentFLACFrame.header.bitsPerSample <= 24) {
        drflac_read_pcm_frames_f32__decode_independent_stereo__neon(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
    } else
#endif
    {
#if 0
        drflac_read_pcm_frames_f32__decode_independent_stereo__reference(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
#else
        drflac_read_pcm_frames_f32__decode_independent_stereo__scalar(pFlac, frameCount, unusedBitsPerSample, pInputSamples0, pInputSamples1, pOutputSamples);
#endif
    }
}
static DRFLAC_INLINE void drflac_read_pcm_frames_f32__decode(drflac* pFlac, uint64_t frameCount, uint64_t iFirstPCMFrame, float* pOutputSamples)
{
    unsigned int channelCount;
    uint32_t unusedBitsPerSample;
    DR_FLAC_ASSERT(pFlac->currentFLACFrame.header.bitsPerSample < 32);
    channelCount = drflac__get_channel_count_from_channel_assignment(pFlac->currentFLACFrame.header.channelAssignment);
    unusedBitsPerSample = 32 - pFlac->currentFLACFrame.header.bitsPerSample;
    if (channelCount == 2) {
        const int32_t* pDecodedSamples0 = pFlac->currentFLACFrame.subframes[0].samples.pS32 + iFirstPCMFrame;
        const int32_t* pDecodedSamples1 = pFlac->currentFLACFrame.subframes[1].samples.pS32 + iFirstPCMFrame;
        switch (pFlac->currentFLACFrame.header.channelAssignment)
        {
            case DR_FLAC_CHANNEL_ASSIGNMENT_LEFT_SIDE:
            {
                drflac_read_pcm_frames_f32__decode_left_side(pFlac, frameCount, unusedBitsPerSample, pDecodedSamples0, pDecodedSamples1, pOutputSamples);
            } break;
            case DR_FLAC_CHANNEL_ASSIGNMENT_RIGHT_SIDE:
            {
                drflac_read_pcm_frames_f32__decode_right_side(pFlac, frameCount, unusedBitsPerSample, pDecodedSamples0, pDecodedSamples1, pOutputSamples);
            } break;
            case DR_FLAC_CHANNEL_ASSIGNMENT_MID_SIDE:
            {
                drflac_read_pcm_frames_f32__decode_mid_side(pFlac, frameCount, unusedBitsPerSample, pDecodedSamples0, pDecodedSamples1, pOutputSamples);
            } break;
            case DR_FLAC_CHANNEL_ASSIGNMENT_INDEPENDENT_STEREO:
            {
                drflac_read_pcm_frames_f32__decode_independent_stereo(pFlac, frameCount, unusedBitsPerSample, pDecodedSamples0, pDecodedSamples1, pOutputSamples);
            } break;
            default: DR_FLAC_ASSERT(!"Invalid stereo channel assignment"); break;
        }
    } else {
        uint64_t i;
        for (i = 0; i < frameCount; ++i) {
            unsigned int j;
            for (j = 0; j < channelCount; ++j) {
                int32_t sampleS32;
                sampleS32 = (int32_t)((uint32_t)(pFlac->currentFLACFrame.subframes[j].samples.pS32[iFirstPCMFrame + i]) << (unusedBitsPerSample + pFlac->currentFLACFrame.subframes[j].wastedBitsPerSample));
                pOutputSamples[(i*channelCount)+j] = (float)(sampleS32 / 2147483648.0);
            }
        }
    }
}
DRFLAC_API uint64_t drflac_read_pcm_frames_f32(drflac* pFlac, uint64_t framesToRead, float* pBufferOut)
{
    uint64_t framesRead;
    if (pFlac == NULL || framesToRead == 0) {
        return 0;
    }
    if (pBufferOut == NULL) {
        return drflac__seek_forward_by_pcm_frames(pFlac, framesToRead);
    }
    DR_FLAC_ASSERT(pFlac->bitsPerSample <= 32);
    framesRead = 0;
    while (framesToRead > 0) {
        if (pFlac->currentFLACFrame.pcmFramesRemaining == 0) {
            if (!drflac__read_and_decode_next_flac_frame(pFlac)) {
                break;
            }
        } else {
            unsigned int channelCount = drflac__get_channel_count_from_channel_assignment(pFlac->currentFLACFrame.header.channelAssignment);
            uint64_t iFirstPCMFrame = pFlac->currentFLACFrame.header.blockSizeInPCMFrames - pFlac->currentFLACFrame.pcmFramesRemaining;
            uint64_t frameCountThisIteration = framesToRead;
            if (frameCountThisIteration > pFlac->currentFLACFrame.pcmFramesRemaining) {
                frameCountThisIteration = pFlac->currentFLACFrame.pcmFramesRemaining;
            }
            if (pFlac->bitsPerSample != 32) {
                drflac_read_pcm_frames_f32__decode(pFlac, frameCountThisIteration, iFirstPCMFrame, pBufferOut);
            } else {
                drflac_read_pcm_frames_f32__decode_s64(pFlac, frameCountThisIteration, iFirstPCMFrame, pBufferOut);
            }
            framesRead             += frameCountThisIteration;
            pBufferOut             += frameCountThisIteration * channelCount;
            framesToRead           -= frameCountThisIteration;
            pFlac->currentPCMFrame += frameCountThisIteration;
            pFlac->currentFLACFrame.pcmFramesRemaining -= (unsigned int)frameCountThisIteration;
        }
    }
    return framesRead;
}
DRFLAC_API int drflac_seek_to_pcm_frame(drflac* pFlac, uint64_t pcmFrameIndex)
{
    if (pFlac == NULL) {
        return 0;
    }
    if (pFlac->currentPCMFrame == pcmFrameIndex) {
        return 1;
    }
    if (pcmFrameIndex == 0) {
        pFlac->currentPCMFrame = 0;
        return drflac__seek_to_first_frame(pFlac);
    } else {
        int wasSuccessful = 0;
        uint64_t originalPCMFrame = pFlac->currentPCMFrame;
        if (pFlac->totalPCMFrameCount != 0 && pcmFrameIndex > pFlac->totalPCMFrameCount) {
            pcmFrameIndex = pFlac->totalPCMFrameCount;
        }
        if (drflac__is_current_flac_frame_valid(pFlac)) {
            if (pcmFrameIndex > pFlac->currentPCMFrame) {
                uint64_t offset = pcmFrameIndex - pFlac->currentPCMFrame;
                if (pFlac->currentFLACFrame.pcmFramesRemaining > offset) {
                    pFlac->currentFLACFrame.pcmFramesRemaining -= (uint32_t)offset;
                    pFlac->currentPCMFrame = pcmFrameIndex;
                    return 1;
                }
            } else {
                uint64_t offsetAbs = pFlac->currentPCMFrame - pcmFrameIndex;
                uint32_t currentFLACFramePCMFrameCount = pFlac->currentFLACFrame.header.blockSizeInPCMFrames;
                uint32_t currentFLACFramePCMFramesConsumed = currentFLACFramePCMFrameCount - pFlac->currentFLACFrame.pcmFramesRemaining;
                if (currentFLACFramePCMFramesConsumed > offsetAbs) {
                    pFlac->currentFLACFrame.pcmFramesRemaining += (uint32_t)offsetAbs;
                    pFlac->currentPCMFrame = pcmFrameIndex;
                    return 1;
                }
            }
        }
#ifndef DR_FLAC_NO_OGG
        if (pFlac->container == drflac_container_ogg)
        {
            wasSuccessful = drflac_ogg__seek_to_pcm_frame(pFlac, pcmFrameIndex);
        }
        else
#endif
        {
            if (!pFlac->_noSeekTableSeek) {
                wasSuccessful = drflac__seek_to_pcm_frame__seek_table(pFlac, pcmFrameIndex);
            }
#if !defined(DR_FLAC_NO_CRC)
            if (!wasSuccessful && !pFlac->_noBinarySearchSeek && pFlac->totalPCMFrameCount > 0) {
                wasSuccessful = drflac__seek_to_pcm_frame__binary_search(pFlac, pcmFrameIndex);
            }
#endif
            if (!wasSuccessful && !pFlac->_noBruteForceSeek) {
                wasSuccessful = drflac__seek_to_pcm_frame__brute_force(pFlac, pcmFrameIndex);
            }
        }
        if (wasSuccessful) {
            pFlac->currentPCMFrame = pcmFrameIndex;
        } else {
            drflac__seek_to_first_frame(pFlac);
            if (originalPCMFrame != 0 && drflac_seek_to_pcm_frame(pFlac, originalPCMFrame) == 0) {
                drflac__seek_to_first_frame(pFlac);
            }
        }
        return wasSuccessful;
    }
}
#define DR_FLAC_DEFINE_FULL_READ_AND_CLOSE(extension, type) \
static type* drflac__full_read_and_close_ ## extension (drflac* pFlac, unsigned int* channelsOut, unsigned int* sampleRateOut, uint64_t* totalPCMFrameCountOut)\
{                                                                                                                                                                   \
    type* pSampleData = NULL;                                                                                                                                       \
    uint64_t totalPCMFrameCount;                                                                                                                               \
    type buffer[4096];                                                                                                                                              \
    uint64_t pcmFramesRead;                                                                                                                                    \
    size_t sampleDataBufferSize = sizeof(buffer);                                                                                                                   \
                                                                                                                                                                    \
    DR_FLAC_ASSERT(pFlac != NULL);                                                                                                                                   \
                                                                                                                                                                    \
    totalPCMFrameCount = 0;                                                                                                                                         \
                                                                                                                                                                    \
    pSampleData = (type*)drflac__malloc_from_callbacks(sampleDataBufferSize, &pFlac->allocationCallbacks);                                                          \
    if (pSampleData == NULL) {                                                                                                                                      \
        goto on_error;                                                                                                                                              \
    }                                                                                                                                                               \
                                                                                                                                                                    \
    while ((pcmFramesRead = (uint64_t)drflac_read_pcm_frames_##extension(pFlac, sizeof(buffer)/sizeof(buffer[0])/pFlac->channels, buffer)) > 0) {              \
        if (((totalPCMFrameCount + pcmFramesRead) * pFlac->channels * sizeof(type)) > sampleDataBufferSize) {                                                       \
            type* pNewSampleData;                                                                                                                                   \
            size_t newSampleDataBufferSize;                                                                                                                         \
                                                                                                                                                                    \
            newSampleDataBufferSize = sampleDataBufferSize * 2;                                                                                                     \
            pNewSampleData = (type*)drflac__realloc_from_callbacks(pSampleData, newSampleDataBufferSize, sampleDataBufferSize, &pFlac->allocationCallbacks);        \
            if (pNewSampleData == NULL) {                                                                                                                           \
                drflac__free_from_callbacks(pSampleData, &pFlac->allocationCallbacks);                                                                              \
                goto on_error;                                                                                                                                      \
            }                                                                                                                                                       \
                                                                                                                                                                    \
            sampleDataBufferSize = newSampleDataBufferSize;                                                                                                         \
            pSampleData = pNewSampleData;                                                                                                                           \
        }                                                                                                                                                           \
                                                                                                                                                                    \
        DR_FLAC_COPY_MEMORY(pSampleData + (totalPCMFrameCount*pFlac->channels), buffer, (size_t)(pcmFramesRead*pFlac->channels*sizeof(type)));                       \
        totalPCMFrameCount += pcmFramesRead;                                                                                                                        \
    }                                                                                                                                                               \
                                                                                                                                                                    \
                                                                                                                         \
    DR_FLAC_ZERO_MEMORY(pSampleData + (totalPCMFrameCount*pFlac->channels), (size_t)(sampleDataBufferSize - totalPCMFrameCount*pFlac->channels*sizeof(type)));       \
                                                                                                                                                                    \
    if (sampleRateOut) *sampleRateOut = pFlac->sampleRate;                                                                                                          \
    if (channelsOut) *channelsOut = pFlac->channels;                                                                                                                \
    if (totalPCMFrameCountOut) *totalPCMFrameCountOut = totalPCMFrameCount;                                                                                         \
                                                                                                                                                                    \
    drflac_close(pFlac);                                                                                                                                            \
    return pSampleData;                                                                                                                                             \
                                                                                                                                                                    \
on_error:                                                                                                                                                           \
    drflac_close(pFlac);                                                                                                                                            \
    return NULL;                                                                                                                                                    \
}
DR_FLAC_DEFINE_FULL_READ_AND_CLOSE(s32, int32_t)
DR_FLAC_DEFINE_FULL_READ_AND_CLOSE(s16, int16_t)
DR_FLAC_DEFINE_FULL_READ_AND_CLOSE(f32, float)
DRFLAC_API int32_t* drflac_open_and_read_pcm_frames_s32(drflac_read_proc onRead, drflac_seek_proc onSeek, drflac_tell_proc onTell, void* pUserData, unsigned int* channelsOut, unsigned int* sampleRateOut, uint64_t* totalPCMFrameCountOut, const ma_allocation_callbacks* pAllocationCallbacks)
{
    drflac* pFlac;
    if (channelsOut) {
        *channelsOut = 0;
    }
    if (sampleRateOut) {
        *sampleRateOut = 0;
    }
    if (totalPCMFrameCountOut) {
        *totalPCMFrameCountOut = 0;
    }
    pFlac = drflac_open(onRead, onSeek, onTell, pUserData, pAllocationCallbacks);
    if (pFlac == NULL) {
        return NULL;
    }
    return drflac__full_read_and_close_s32(pFlac, channelsOut, sampleRateOut, totalPCMFrameCountOut);
}
DRFLAC_API int16_t* drflac_open_and_read_pcm_frames_s16(drflac_read_proc onRead, drflac_seek_proc onSeek, drflac_tell_proc onTell, void* pUserData, unsigned int* channelsOut, unsigned int* sampleRateOut, uint64_t* totalPCMFrameCountOut, const ma_allocation_callbacks* pAllocationCallbacks)
{
    drflac* pFlac;
    if (channelsOut) {
        *channelsOut = 0;
    }
    if (sampleRateOut) {
        *sampleRateOut = 0;
    }
    if (totalPCMFrameCountOut) {
        *totalPCMFrameCountOut = 0;
    }
    pFlac = drflac_open(onRead, onSeek, onTell, pUserData, pAllocationCallbacks);
    if (pFlac == NULL) {
        return NULL;
    }
    return drflac__full_read_and_close_s16(pFlac, channelsOut, sampleRateOut, totalPCMFrameCountOut);
}
DRFLAC_API float* drflac_open_and_read_pcm_frames_f32(drflac_read_proc onRead, drflac_seek_proc onSeek, drflac_tell_proc onTell, void* pUserData, unsigned int* channelsOut, unsigned int* sampleRateOut, uint64_t* totalPCMFrameCountOut, const ma_allocation_callbacks* pAllocationCallbacks)
{
    drflac* pFlac;
    if (channelsOut) {
        *channelsOut = 0;
    }
    if (sampleRateOut) {
        *sampleRateOut = 0;
    }
    if (totalPCMFrameCountOut) {
        *totalPCMFrameCountOut = 0;
    }
    pFlac = drflac_open(onRead, onSeek, onTell, pUserData, pAllocationCallbacks);
    if (pFlac == NULL) {
        return NULL;
    }
    return drflac__full_read_and_close_f32(pFlac, channelsOut, sampleRateOut, totalPCMFrameCountOut);
}
#ifndef DR_FLAC_NO_STDIO
DRFLAC_API int32_t* drflac_open_file_and_read_pcm_frames_s32(const char* filename, unsigned int* channels, unsigned int* sampleRate, uint64_t* totalPCMFrameCount, const ma_allocation_callbacks* pAllocationCallbacks)
{
    drflac* pFlac;
    if (sampleRate) {
        *sampleRate = 0;
    }
    if (channels) {
        *channels = 0;
    }
    if (totalPCMFrameCount) {
        *totalPCMFrameCount = 0;
    }
    pFlac = drflac_open_file(filename, pAllocationCallbacks);
    if (pFlac == NULL) {
        return NULL;
    }
    return drflac__full_read_and_close_s32(pFlac, channels, sampleRate, totalPCMFrameCount);
}
DRFLAC_API int16_t* drflac_open_file_and_read_pcm_frames_s16(const char* filename, unsigned int* channels, unsigned int* sampleRate, uint64_t* totalPCMFrameCount, const ma_allocation_callbacks* pAllocationCallbacks)
{
    drflac* pFlac;
    if (sampleRate) {
        *sampleRate = 0;
    }
    if (channels) {
        *channels = 0;
    }
    if (totalPCMFrameCount) {
        *totalPCMFrameCount = 0;
    }
    pFlac = drflac_open_file(filename, pAllocationCallbacks);
    if (pFlac == NULL) {
        return NULL;
    }
    return drflac__full_read_and_close_s16(pFlac, channels, sampleRate, totalPCMFrameCount);
}
DRFLAC_API float* drflac_open_file_and_read_pcm_frames_f32(const char* filename, unsigned int* channels, unsigned int* sampleRate, uint64_t* totalPCMFrameCount, const ma_allocation_callbacks* pAllocationCallbacks)
{
    drflac* pFlac;
    if (sampleRate) {
        *sampleRate = 0;
    }
    if (channels) {
        *channels = 0;
    }
    if (totalPCMFrameCount) {
        *totalPCMFrameCount = 0;
    }
    pFlac = drflac_open_file(filename, pAllocationCallbacks);
    if (pFlac == NULL) {
        return NULL;
    }
    return drflac__full_read_and_close_f32(pFlac, channels, sampleRate, totalPCMFrameCount);
}
#endif
DRFLAC_API int32_t* drflac_open_memory_and_read_pcm_frames_s32(const void* data, size_t dataSize, unsigned int* channels, unsigned int* sampleRate, uint64_t* totalPCMFrameCount, const ma_allocation_callbacks* pAllocationCallbacks)
{
    drflac* pFlac;
    if (sampleRate) {
        *sampleRate = 0;
    }
    if (channels) {
        *channels = 0;
    }
    if (totalPCMFrameCount) {
        *totalPCMFrameCount = 0;
    }
    pFlac = drflac_open_memory(data, dataSize, pAllocationCallbacks);
    if (pFlac == NULL) {
        return NULL;
    }
    return drflac__full_read_and_close_s32(pFlac, channels, sampleRate, totalPCMFrameCount);
}
DRFLAC_API int16_t* drflac_open_memory_and_read_pcm_frames_s16(const void* data, size_t dataSize, unsigned int* channels, unsigned int* sampleRate, uint64_t* totalPCMFrameCount, const ma_allocation_callbacks* pAllocationCallbacks)
{
    drflac* pFlac;
    if (sampleRate) {
        *sampleRate = 0;
    }
    if (channels) {
        *channels = 0;
    }
    if (totalPCMFrameCount) {
        *totalPCMFrameCount = 0;
    }
    pFlac = drflac_open_memory(data, dataSize, pAllocationCallbacks);
    if (pFlac == NULL) {
        return NULL;
    }
    return drflac__full_read_and_close_s16(pFlac, channels, sampleRate, totalPCMFrameCount);
}
DRFLAC_API float* drflac_open_memory_and_read_pcm_frames_f32(const void* data, size_t dataSize, unsigned int* channels, unsigned int* sampleRate, uint64_t* totalPCMFrameCount, const ma_allocation_callbacks* pAllocationCallbacks)
{
    drflac* pFlac;
    if (sampleRate) {
        *sampleRate = 0;
    }
    if (channels) {
        *channels = 0;
    }
    if (totalPCMFrameCount) {
        *totalPCMFrameCount = 0;
    }
    pFlac = drflac_open_memory(data, dataSize, pAllocationCallbacks);
    if (pFlac == NULL) {
        return NULL;
    }
    return drflac__full_read_and_close_f32(pFlac, channels, sampleRate, totalPCMFrameCount);
}
DRFLAC_API void drflac_free(void* p, const ma_allocation_callbacks* pAllocationCallbacks)
{
    if (pAllocationCallbacks != NULL) {
        drflac__free_from_callbacks(p, pAllocationCallbacks);
    } else {
        drflac__free_default(p, NULL);
    }
}
DRFLAC_API void drflac_init_vorbis_comment_iterator(drflac_vorbis_comment_iterator* pIter, uint32_t commentCount, const void* pComments)
{
    if (pIter == NULL) {
        return;
    }
    pIter->countRemaining = commentCount;
    pIter->pRunningData   = (const char*)pComments;
}
DRFLAC_API const char* drflac_next_vorbis_comment(drflac_vorbis_comment_iterator* pIter, uint32_t* pCommentLengthOut)
{
    int32_t length;
    const char* pComment;
    if (pCommentLengthOut) {
        *pCommentLengthOut = 0;
    }
    if (pIter == NULL || pIter->countRemaining == 0 || pIter->pRunningData == NULL) {
        return NULL;
    }
    length = drflac__le2host_32_ptr_unaligned(pIter->pRunningData);
    pIter->pRunningData += 4;
    pComment = pIter->pRunningData;
    pIter->pRunningData += length;
    pIter->countRemaining -= 1;
    if (pCommentLengthOut) {
        *pCommentLengthOut = length;
    }
    return pComment;
}
DRFLAC_API void drflac_init_cuesheet_track_iterator(drflac_cuesheet_track_iterator* pIter, uint32_t trackCount, const void* pTrackData)
{
    if (pIter == NULL) {
        return;
    }
    pIter->countRemaining = trackCount;
    pIter->pRunningData   = (const char*)pTrackData;
}
DRFLAC_API int drflac_next_cuesheet_track(drflac_cuesheet_track_iterator* pIter, drflac_cuesheet_track* pCuesheetTrack)
{
    drflac_cuesheet_track cuesheetTrack;
    const char* pRunningData;
    uint64_t offsetHi;
    uint64_t offsetLo;
    if (pIter == NULL || pIter->countRemaining == 0 || pIter->pRunningData == NULL) {
        return 0;
    }
    pRunningData = pIter->pRunningData;
    offsetHi                   = drflac__be2host_32(*(const uint32_t*)pRunningData); pRunningData += 4;
    offsetLo                   = drflac__be2host_32(*(const uint32_t*)pRunningData); pRunningData += 4;
    cuesheetTrack.offset       = offsetLo | (offsetHi << 32);
    cuesheetTrack.trackNumber  = pRunningData[0];                                         pRunningData += 1;
    DR_FLAC_COPY_MEMORY(cuesheetTrack.ISRC, pRunningData, sizeof(cuesheetTrack.ISRC));     pRunningData += 12;
    cuesheetTrack.isAudio      = (pRunningData[0] & 0x80) != 0;
    cuesheetTrack.preEmphasis  = (pRunningData[0] & 0x40) != 0;                           pRunningData += 14;
    cuesheetTrack.indexCount   = pRunningData[0];                                         pRunningData += 1 + (DR_FLAC_CUESHEET_TRACK_PADDED_SIZE_IN_BYTES - DR_FLAC_CUESHEET_TRACK_SIZE_IN_BYTES);
    cuesheetTrack.pIndexPoints = (const drflac_cuesheet_track_index*)pRunningData;        pRunningData += cuesheetTrack.indexCount * sizeof(drflac_cuesheet_track_index);
    pIter->pRunningData = pRunningData;
    pIter->countRemaining -= 1;
    if (pCuesheetTrack) {
        *pCuesheetTrack = cuesheetTrack;
    }
    return 1;
}
#if defined(__clang__) || (defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)))
    #pragma GCC diagnostic pop
#endif
#endif
/* dr_flac_c end */
#endif /* DR_FLAC_IMPLEMENTATION */

#endif /* DR_FLAC_H */
