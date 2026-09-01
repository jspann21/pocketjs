#ifndef POCKETJS_IPOD_PHOTO_STORAGE_H
#define POCKETJS_IPOD_PHOTO_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PJS_STORAGE_SECTOR_BYTES 512u
#define PJS_STORAGE_MAX_FILE_BYTES (8u * 1024u * 1024u)

#define PJS_STORAGE_OK 0
#define PJS_STORAGE_ERR_ARGUMENT -1
#define PJS_STORAGE_ERR_ATA -2
#define PJS_STORAGE_ERR_PARTITION -3
#define PJS_STORAGE_ERR_BPB -4
#define PJS_STORAGE_ERR_NOT_FOUND -5
#define PJS_STORAGE_ERR_CHAIN -6
#define PJS_STORAGE_ERR_TOO_LARGE -7
#define PJS_STORAGE_ERR_ALLOC -8
#define PJS_STORAGE_ERR_SHORT_READ -9

typedef bool (*PjsSectorReadFn)(void *context, uint32_t lba,
                                uint8_t sector[PJS_STORAGE_SECTOR_BYTES]);

typedef struct {
    PjsSectorReadFn read_sector;
    void *context;
    uint32_t partition_lba;
    uint32_t partition_sectors;
    uint32_t fat_lba;
    uint32_t data_lba;
    uint32_t fat_sectors;
    uint32_t root_cluster;
    uint32_t cluster_count;
    uint8_t sectors_per_cluster;
    uint8_t fat_count;
} PjsFat32;

typedef struct {
    uint8_t *bytes;
    uint32_t length;
} PjsStorageFile;

int pjs_fat32_mount(PjsFat32 *fat, PjsSectorReadFn reader, void *context);
int pjs_fat32_short_file_size(PjsFat32 *fat,
                              const char directory_name[11],
                              const char file_name[11],
                              uint32_t *size_out);
int pjs_fat32_read_short_file(PjsFat32 *fat,
                              const char directory_name[11],
                              const char file_name[11],
                              uint8_t *destination, uint32_t capacity,
                              uint32_t *length_out);

int pjs_storage_load_guest(PjsStorageFile *file);
void pjs_storage_release(PjsStorageFile *file);
uint32_t pjs_storage_last_error(void);
uint32_t pjs_storage_sector_read_count(void);
uint32_t pjs_storage_first_failed_lba(void);

#endif
