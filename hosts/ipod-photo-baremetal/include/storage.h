#ifndef POCKETJS_IPOD_PHOTO_STORAGE_H
#define POCKETJS_IPOD_PHOTO_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PJS_STORAGE_SECTOR_BYTES 512u
#define PJS_STORAGE_MAX_FILE_BYTES (8u * 1024u * 1024u)
#define PJS_STORAGE_MAX_APPS 6u

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
#define PJS_STORAGE_ERR_TOO_MANY -10
#define PJS_STORAGE_ERR_STATE -11
#define PJS_STORAGE_ERR_VERIFY -12

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

typedef struct {
    char file_name[11];
    uint32_t size;
} PjsStorageApp;

typedef struct {
    PjsStorageApp apps[PJS_STORAGE_MAX_APPS];
    uint32_t count;
} PjsStorageCatalog;

typedef struct {
    uint32_t available;
    uint32_t active_slot;
    uint32_t generation;
    uint32_t payload;
    uint32_t error;
} PjsPersistenceState;

#define PJS_LINEAGE_PHASE_ACTIVE 1u
#define PJS_LINEAGE_PHASE_TRIAL 2u
#define PJS_LINEAGE_PHASE_RUNNING 3u
#define PJS_LINEAGE_PHASE_CRASHED 4u

typedef struct {
    uint32_t generation;
    uint32_t phase;
    uint32_t active_source;
    uint32_t active_hash_low;
    uint32_t active_hash_high;
    uint32_t last_good_source;
    uint32_t last_good_hash_low;
    uint32_t last_good_hash_high;
    uint32_t trial_source;
    uint32_t trial_hash_low;
    uint32_t trial_hash_high;
    uint32_t rejected_source;
    uint32_t rejected_hash_low;
    uint32_t rejected_hash_high;
    uint32_t failure_stage;
    uint32_t failure_code;
} PjsLineageRecord;

typedef struct {
    uint32_t available;
    uint32_t active_slot;
    uint32_t error;
    PjsLineageRecord record;
} PjsLineageState;

int pjs_fat32_mount(PjsFat32 *fat, PjsSectorReadFn reader, void *context);
int pjs_fat32_find_short_directory(PjsFat32 *fat, uint32_t parent_cluster,
                                   const char directory_name[11],
                                   uint32_t *cluster_out);
int pjs_fat32_list_short_files(PjsFat32 *fat, uint32_t directory_cluster,
                               const char extension[3],
                               PjsStorageApp *entries, uint32_t capacity,
                               uint32_t *count_out);
int pjs_fat32_short_file_size_at(PjsFat32 *fat, uint32_t directory_cluster,
                                 const char file_name[11],
                                 uint32_t *size_out);
int pjs_fat32_read_short_file_at(PjsFat32 *fat, uint32_t directory_cluster,
                                 const char file_name[11],
                                 uint8_t *destination, uint32_t capacity,
                                 uint32_t *length_out);
int pjs_fat32_short_file_size(PjsFat32 *fat,
                              const char directory_name[11],
                              const char file_name[11],
                              uint32_t *size_out);
int pjs_fat32_read_short_file(PjsFat32 *fat,
                              const char directory_name[11],
                              const char file_name[11],
                              uint8_t *destination, uint32_t capacity,
                              uint32_t *length_out);
int pjs_fat32_short_file_sector(PjsFat32 *fat,
                                const char directory_name[11],
                                const char file_name[11],
                                uint32_t expected_size,
                                uint32_t *lba_out);

void pjs_state_record_build(uint8_t record[PJS_STORAGE_SECTOR_BYTES],
                            uint32_t generation, uint32_t payload,
                            bool committed);
bool pjs_state_record_read(const uint8_t record[PJS_STORAGE_SECTOR_BYTES],
                           uint32_t *generation_out,
                           uint32_t *payload_out);
void pjs_lineage_record_build(uint8_t record[PJS_STORAGE_SECTOR_BYTES],
                              const PjsLineageRecord *lineage,
                              bool committed);
bool pjs_lineage_record_read(const uint8_t record[PJS_STORAGE_SECTOR_BYTES],
                             PjsLineageRecord *lineage_out);

int pjs_storage_load_guest(PjsStorageFile *file);
void pjs_storage_reset_diagnostics(void);
int pjs_storage_load_guest_named(PjsStorageFile *file,
                                 const char file_name[11]);
int pjs_storage_discover_apps(PjsStorageCatalog *catalog);
int pjs_storage_load_app(PjsStorageFile *file, const char file_name[11]);
void pjs_storage_release(PjsStorageFile *file);
uint32_t pjs_storage_last_error(void);
uint32_t pjs_storage_sector_read_count(void);
uint32_t pjs_storage_first_failed_lba(void);
int pjs_storage_state_load(PjsPersistenceState *state);
int pjs_storage_state_write(PjsPersistenceState *state, bool publish,
                            uint32_t *attempted_slot,
                            uint32_t *attempted_generation);
int pjs_storage_lineage_load(PjsLineageState *state);
int pjs_storage_lineage_write(PjsLineageState *state,
                              const PjsLineageRecord *record);

#endif
