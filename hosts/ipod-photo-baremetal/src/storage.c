#include "storage.h"

#include <stdint.h>

#include "heap.h"
#include "pp5020.h"
#include "timer.h"

#define ATA_STATUS_BSY 0x80u
#define ATA_STATUS_RDY 0x40u
#define ATA_STATUS_DF  0x20u
#define ATA_STATUS_DRQ 0x08u
#define ATA_STATUS_ERR 0x01u
#define ATA_SELECT_LBA 0x40u
#define ATA_CMD_READ_SECTORS 0x20u
#define ATA_CMD_WRITE_SECTORS 0x30u
#define ATA_CMD_FLUSH_CACHE 0xe7u
#define ATA_TIMEOUT_US 2000000u
#define ATA_STATE_TRANSACTION_TIMEOUT_US 5000000u

static uint32_t storage_error;
static uint32_t storage_sector_reads;
static uint32_t storage_first_failed_lba = UINT32_MAX;
static const char guest_directory[11] = {'P','O','C','K','E','T','J','S',' ',' ',' '};
static const char guest_filename[11] = {'A','P','P',' ',' ',' ',' ',' ','P','K','T'};
static const char apps_directory[11] = {'A','P','P','S',' ',' ',' ',' ',' ',' ',' '};
static const char package_extension[3] = {'P','K','T'};
static const char state_filenames[2][11] = {
    {'S','T','A','T','E','0',' ',' ','B','I','N'},
    {'S','T','A','T','E','1',' ',' ','B','I','N'},
};
static uint32_t state_lbas[2];
static bool state_lbas_ready;
static bool ata_transaction_active;
static uint32_t ata_transaction_deadline;

static bool ata_read_failed(uint32_t lba)
{
    if (storage_first_failed_lba == UINT32_MAX) storage_first_failed_lba = lba;
    return false;
}

static bool wait_bsy_clear(void)
{
    uint32_t start = timer_now_us();
    for (;;) {
        uint32_t now = timer_now_us();
        if ((uint32_t)(now - start) >= ATA_TIMEOUT_US ||
            (ata_transaction_active &&
             (int32_t)(now - ata_transaction_deadline) >= 0)) return false;
        if ((PP_ATA_ALT_STATUS & ATA_STATUS_BSY) == 0u) return true;
    }
}

static bool wait_drq(void)
{
    uint32_t start = timer_now_us();
    for (;;) {
        uint32_t now = timer_now_us();
        if ((uint32_t)(now - start) >= ATA_TIMEOUT_US ||
            (ata_transaction_active &&
             (int32_t)(now - ata_transaction_deadline) >= 0)) return false;
        uint8_t status = PP_ATA_ALT_STATUS;
        if ((status & (ATA_STATUS_ERR | ATA_STATUS_DF)) != 0u) return false;
        if ((status & ATA_STATUS_BSY) == 0u && (status & ATA_STATUS_DRQ) != 0u) {
            return true;
        }
    }
}

static bool completion_ready(uint8_t status)
{
    return (status & ATA_STATUS_RDY) != 0u &&
           (status & (ATA_STATUS_BSY | ATA_STATUS_DF |
                      ATA_STATUS_DRQ | ATA_STATUS_ERR)) == 0u;
}

static bool wait_ready(void)
{
    if (!wait_bsy_clear()) return false;
    return completion_ready(PP_ATA_ALT_STATUS);
}

static void ata_delay_400ns(void)
{
    (void)PP_ATA_ALT_STATUS;
    (void)PP_ATA_ALT_STATUS;
    (void)PP_ATA_ALT_STATUS;
    (void)PP_ATA_ALT_STATUS;
}

static void ata_prepare(void)
{
    /* The reversible Rockbox loader has just read this image from disk, so the
     * drive power rail is already up. Own only the PP5020 IDE host registers
     * in this first read-only gate; disk power transitions remain untouched. */
    PP_DEV_EN |= PP_DEV_IDE0;
    PP_IDE0_CFG |= (1u << 5);
    PP_IDE0_CFG &= ~0x10000000u;
    PP_IDE0_PRI_TIMING0 = 0x0000c293u;
    PP_IDE0_PRI_TIMING1 = 0x80002150u;
    PP_ATA_CONTROL = 0x02u; /* nIEN: keep ATA IRQ delivery disabled. */
    ata_delay_400ns();
}

static bool ata_read_sector(void *context, uint32_t lba,
                            uint8_t sector[PJS_STORAGE_SECTOR_BYTES])
{
    (void)context;
    if (storage_sector_reads != UINT32_MAX) ++storage_sector_reads;
    if (lba >= 0x10000000u || sector == 0) return ata_read_failed(lba);
    if (!wait_bsy_clear()) return ata_read_failed(lba);

    PP_ATA_SELECT = (uint8_t)(ATA_SELECT_LBA | ((lba >> 24) & 0x0fu));
    ata_delay_400ns();
    if (!wait_ready()) return ata_read_failed(lba);

    PP_ATA_NSECTOR = 1u;
    PP_ATA_SECTOR = (uint8_t)lba;
    PP_ATA_LCYL = (uint8_t)(lba >> 8);
    PP_ATA_HCYL = (uint8_t)(lba >> 16);
    PP_ATA_COMMAND = ATA_CMD_READ_SECTORS;
    ata_delay_400ns();
    if (!wait_drq()) return ata_read_failed(lba);

    for (uint32_t word = 0u; word < PJS_STORAGE_SECTOR_BYTES / 2u; ++word) {
        uint16_t value = PP_ATA_DATA;
        sector[word * 2u] = (uint8_t)value;
        sector[word * 2u + 1u] = (uint8_t)(value >> 8);
    }
    if (!wait_bsy_clear()) return ata_read_failed(lba);
    uint8_t status = PP_ATA_STATUS;
    return completion_ready(status) ? true : ata_read_failed(lba);
}

static bool ata_flush(void)
{
    if (!wait_ready()) return false;
    PP_ATA_COMMAND = ATA_CMD_FLUSH_CACHE;
    ata_delay_400ns();
    if (!wait_bsy_clear()) return false;
    uint8_t status = PP_ATA_STATUS;
    return completion_ready(status);
}

static bool ata_write_sector(uint32_t lba,
                             const uint8_t sector[PJS_STORAGE_SECTOR_BYTES])
{
    if (lba >= 0x10000000u || sector == 0) return ata_read_failed(lba);
    if (!wait_bsy_clear()) return ata_read_failed(lba);
    PP_ATA_SELECT = (uint8_t)(ATA_SELECT_LBA | ((lba >> 24) & 0x0fu));
    ata_delay_400ns();
    if (!wait_ready()) return ata_read_failed(lba);

    PP_ATA_NSECTOR = 1u;
    PP_ATA_SECTOR = (uint8_t)lba;
    PP_ATA_LCYL = (uint8_t)(lba >> 8);
    PP_ATA_HCYL = (uint8_t)(lba >> 16);
    PP_ATA_COMMAND = ATA_CMD_WRITE_SECTORS;
    ata_delay_400ns();
    if (!wait_drq()) return ata_read_failed(lba);
    for (uint32_t word = 0u; word < PJS_STORAGE_SECTOR_BYTES / 2u; ++word) {
        PP_ATA_DATA = (uint16_t)sector[word * 2u] |
                      ((uint16_t)sector[word * 2u + 1u] << 8);
    }
    if (!wait_bsy_clear()) return ata_read_failed(lba);
    uint8_t status = PP_ATA_STATUS;
    if (!completion_ready(status)) return ata_read_failed(lba);
    if (!ata_flush()) return ata_read_failed(lba);
    return true;
}

static bool sectors_equal(const uint8_t *left, const uint8_t *right)
{
    for (uint32_t index = 0u; index < PJS_STORAGE_SECTOR_BYTES; ++index) {
        if (left[index] != right[index]) return false;
    }
    return true;
}

static int resolve_state_lbas(PjsFat32 *fat)
{
    for (uint32_t slot = 0u; slot < 2u; ++slot) {
        int rc = pjs_fat32_short_file_sector(
            fat, guest_directory, state_filenames[slot],
            PJS_STORAGE_SECTOR_BYTES, &state_lbas[slot]);
        if (rc != PJS_STORAGE_OK) return rc;
    }
    if (state_lbas[0] == state_lbas[1]) return PJS_STORAGE_ERR_STATE;
    state_lbas_ready = true;
    return PJS_STORAGE_OK;
}

static bool generation_after(uint32_t left, uint32_t right)
{
    return (int32_t)(left - right) > 0;
}

int pjs_storage_state_load(PjsPersistenceState *state)
{
    if (state == 0) return PJS_STORAGE_ERR_ARGUMENT;
    *state = (PjsPersistenceState){0};
    state_lbas_ready = false;
    ata_prepare();
    PjsFat32 fat = {0};
    int rc = pjs_fat32_mount(&fat, ata_read_sector, 0);
    if (rc == PJS_STORAGE_OK) rc = resolve_state_lbas(&fat);
    if (rc != PJS_STORAGE_OK) {
        state->error = (uint32_t)(-rc);
        return rc;
    }

    bool valid[2] = {false, false};
    uint32_t generations[2] = {0u, 0u};
    uint32_t payloads[2] = {0u, 0u};
    uint8_t record[PJS_STORAGE_SECTOR_BYTES];
    for (uint32_t slot = 0u; slot < 2u; ++slot) {
        if (!ata_read_sector(0, state_lbas[slot], record)) {
            state->error = (uint32_t)(-PJS_STORAGE_ERR_ATA);
            return PJS_STORAGE_ERR_ATA;
        }
        valid[slot] = pjs_state_record_read(
            record, &generations[slot], &payloads[slot]);
    }
    if (!valid[0] && !valid[1]) {
        state->error = (uint32_t)(-PJS_STORAGE_ERR_STATE);
        return PJS_STORAGE_ERR_STATE;
    }
    uint32_t selected = valid[1] &&
        (!valid[0] || generation_after(generations[1], generations[0])) ? 1u : 0u;
    state->available = 1u;
    state->active_slot = selected;
    state->generation = generations[selected];
    state->payload = payloads[selected];
    return PJS_STORAGE_OK;
}

static int storage_state_write_bounded(PjsPersistenceState *state, bool publish,
                                       uint32_t *attempted_slot,
                                       uint32_t *attempted_generation)
{
    if (state == 0 || attempted_slot == 0 || attempted_generation == 0 ||
        state->available == 0u || !state_lbas_ready) {
        return PJS_STORAGE_ERR_ARGUMENT;
    }
    uint32_t slot = state->active_slot ^ 1u;
    uint32_t generation = state->generation + 1u;
    *attempted_slot = slot;
    *attempted_generation = generation;

    uint8_t record[PJS_STORAGE_SECTOR_BYTES];
    uint8_t readback[PJS_STORAGE_SECTOR_BYTES];
    pjs_state_record_build(record, generation, generation, false);
    if (!ata_write_sector(state_lbas[slot], record) ||
        !ata_read_sector(0, state_lbas[slot], readback)) {
        state->error = (uint32_t)(-PJS_STORAGE_ERR_ATA);
        return PJS_STORAGE_ERR_ATA;
    }
    if (!sectors_equal(record, readback)) {
        state->error = (uint32_t)(-PJS_STORAGE_ERR_VERIFY);
        return PJS_STORAGE_ERR_VERIFY;
    }
    if (!publish) return PJS_STORAGE_OK;

    pjs_state_record_build(record, generation, generation, true);
    if (!ata_write_sector(state_lbas[slot], record) ||
        !ata_read_sector(0, state_lbas[slot], readback)) {
        state->error = (uint32_t)(-PJS_STORAGE_ERR_ATA);
        return PJS_STORAGE_ERR_ATA;
    }
    uint32_t verified_generation = 0u;
    uint32_t verified_payload = 0u;
    if (!sectors_equal(record, readback) ||
        !pjs_state_record_read(
            readback, &verified_generation, &verified_payload) ||
        verified_generation != generation || verified_payload != generation) {
        state->error = (uint32_t)(-PJS_STORAGE_ERR_VERIFY);
        return PJS_STORAGE_ERR_VERIFY;
    }
    state->active_slot = slot;
    state->generation = generation;
    state->payload = generation;
    state->error = 0u;
    return PJS_STORAGE_OK;
}

int pjs_storage_state_write(PjsPersistenceState *state, bool publish,
                            uint32_t *attempted_slot,
                            uint32_t *attempted_generation)
{
    ata_prepare();
    ata_transaction_active = true;
    ata_transaction_deadline = timer_now_us() + ATA_STATE_TRANSACTION_TIMEOUT_US;
    int rc = storage_state_write_bounded(
        state, publish, attempted_slot, attempted_generation);
    if (rc == PJS_STORAGE_OK &&
        (int32_t)(timer_now_us() - ata_transaction_deadline) >= 0) {
        state->error = (uint32_t)(-PJS_STORAGE_ERR_ATA);
        rc = PJS_STORAGE_ERR_ATA;
    }
    ata_transaction_active = false;
    return rc;
}

void pjs_storage_reset_diagnostics(void)
{
    storage_error = 0u;
    storage_sector_reads = 0u;
    storage_first_failed_lba = UINT32_MAX;
}

int pjs_storage_load_guest_named(PjsStorageFile *file,
                                 const char file_name[11])
{
    if (file == 0 || file_name == 0) return PJS_STORAGE_ERR_ARGUMENT;
    *file = (PjsStorageFile){0};
    storage_error = 0u;
    ata_prepare();

    PjsFat32 fat = {0};
    int rc = pjs_fat32_mount(&fat, ata_read_sector, 0);
    if (rc != PJS_STORAGE_OK) {
        storage_error = (uint32_t)(-rc);
        return rc;
    }

    uint32_t expected = 0u;
    rc = pjs_fat32_short_file_size(&fat, guest_directory, file_name, &expected);
    if (rc != PJS_STORAGE_OK) {
        storage_error = (uint32_t)(-rc);
        return rc;
    }
    if (expected == 0u || expected > PJS_STORAGE_MAX_FILE_BYTES) {
        storage_error = (uint32_t)(-PJS_STORAGE_ERR_TOO_LARGE);
        return PJS_STORAGE_ERR_TOO_LARGE;
    }
    uint8_t *bytes = pjs_heap_alloc(expected, 16u);
    if (bytes == 0) {
        storage_error = (uint32_t)(-PJS_STORAGE_ERR_ALLOC);
        return PJS_STORAGE_ERR_ALLOC;
    }
    uint32_t length = 0u;
    rc = pjs_fat32_read_short_file(&fat, guest_directory, file_name,
                                   bytes, expected, &length);
    if (rc != PJS_STORAGE_OK) {
        pjs_heap_free(bytes);
        storage_error = (uint32_t)(-rc);
        return rc;
    }
    if (length == 0u) {
        pjs_heap_free(bytes);
        storage_error = (uint32_t)(-PJS_STORAGE_ERR_SHORT_READ);
        return PJS_STORAGE_ERR_SHORT_READ;
    }
    file->bytes = bytes;
    file->length = length;
    return PJS_STORAGE_OK;
}

int pjs_storage_load_guest(PjsStorageFile *file)
{
    pjs_storage_reset_diagnostics();
    return pjs_storage_load_guest_named(file, guest_filename);
}

static bool app_name_valid(const char file_name[11])
{
    bool saw_character = false;
    bool saw_space = false;
    for (uint32_t index = 0u; index < 8u; ++index) {
        char character = file_name[index];
        if (character == ' ') {
            saw_space = true;
            continue;
        }
        if (saw_space) return false;
        if (!((character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') ||
              character == '_' || character == '-')) return false;
        saw_character = true;
    }
    return saw_character && file_name[8] == 'P' &&
           file_name[9] == 'K' && file_name[10] == 'T';
}

static int compare_app_names(const PjsStorageApp *left,
                             const PjsStorageApp *right)
{
    for (uint32_t index = 0u; index < 11u; ++index) {
        uint8_t a = (uint8_t)left->file_name[index];
        uint8_t b = (uint8_t)right->file_name[index];
        if (a < b) return -1;
        if (a > b) return 1;
    }
    return 0;
}

int pjs_storage_discover_apps(PjsStorageCatalog *catalog)
{
    if (catalog == 0) return PJS_STORAGE_ERR_ARGUMENT;
    *catalog = (PjsStorageCatalog){0};
    storage_error = 0u;
    ata_prepare();

    PjsFat32 fat = {0};
    int rc = pjs_fat32_mount(&fat, ata_read_sector, 0);
    if (rc != PJS_STORAGE_OK) goto failed;
    uint32_t pocketjs_cluster = 0u;
    rc = pjs_fat32_find_short_directory(
        &fat, fat.root_cluster, guest_directory, &pocketjs_cluster);
    if (rc != PJS_STORAGE_OK) goto failed;
    uint32_t apps_cluster = 0u;
    rc = pjs_fat32_find_short_directory(
        &fat, pocketjs_cluster, apps_directory, &apps_cluster);
    if (rc != PJS_STORAGE_OK) goto failed;
    rc = pjs_fat32_list_short_files(
        &fat, apps_cluster, package_extension, catalog->apps,
        PJS_STORAGE_MAX_APPS, &catalog->count);
    if (rc != PJS_STORAGE_OK) goto failed;

    uint32_t write = 0u;
    for (uint32_t read = 0u; read < catalog->count; ++read) {
        if (!app_name_valid(catalog->apps[read].file_name) ||
            catalog->apps[read].size > PJS_STORAGE_MAX_FILE_BYTES) continue;
        if (write != read) catalog->apps[write] = catalog->apps[read];
        ++write;
    }
    catalog->count = write;
    for (uint32_t end = catalog->count; end > 1u; --end) {
        for (uint32_t index = 1u; index < end; ++index) {
            if (compare_app_names(&catalog->apps[index - 1u],
                                  &catalog->apps[index]) <= 0) continue;
            PjsStorageApp temporary = catalog->apps[index - 1u];
            catalog->apps[index - 1u] = catalog->apps[index];
            catalog->apps[index] = temporary;
        }
    }
    return PJS_STORAGE_OK;

failed:
    storage_error = (uint32_t)(-rc);
    return rc;
}

int pjs_storage_load_app(PjsStorageFile *file, const char file_name[11])
{
    if (file == 0 || file_name == 0 || !app_name_valid(file_name)) {
        return PJS_STORAGE_ERR_ARGUMENT;
    }
    *file = (PjsStorageFile){0};
    storage_error = 0u;
    ata_prepare();

    PjsFat32 fat = {0};
    int rc = pjs_fat32_mount(&fat, ata_read_sector, 0);
    if (rc != PJS_STORAGE_OK) goto failed;
    uint32_t pocketjs_cluster = 0u;
    rc = pjs_fat32_find_short_directory(
        &fat, fat.root_cluster, guest_directory, &pocketjs_cluster);
    if (rc != PJS_STORAGE_OK) goto failed;
    uint32_t apps_cluster = 0u;
    rc = pjs_fat32_find_short_directory(
        &fat, pocketjs_cluster, apps_directory, &apps_cluster);
    if (rc != PJS_STORAGE_OK) goto failed;
    uint32_t expected = 0u;
    rc = pjs_fat32_short_file_size_at(
        &fat, apps_cluster, file_name, &expected);
    if (rc != PJS_STORAGE_OK) goto failed;
    if (expected == 0u || expected > PJS_STORAGE_MAX_FILE_BYTES) {
        rc = PJS_STORAGE_ERR_TOO_LARGE;
        goto failed;
    }
    uint8_t *bytes = pjs_heap_alloc(expected, 16u);
    if (bytes == 0) {
        rc = PJS_STORAGE_ERR_ALLOC;
        goto failed;
    }
    uint32_t length = 0u;
    rc = pjs_fat32_read_short_file_at(
        &fat, apps_cluster, file_name, bytes, expected, &length);
    if (rc != PJS_STORAGE_OK || length == 0u) {
        pjs_heap_free(bytes);
        if (rc == PJS_STORAGE_OK) rc = PJS_STORAGE_ERR_SHORT_READ;
        goto failed;
    }
    file->bytes = bytes;
    file->length = length;
    return PJS_STORAGE_OK;

failed:
    storage_error = (uint32_t)(-rc);
    return rc;
}

void pjs_storage_release(PjsStorageFile *file)
{
    if (file == 0) return;
    if (file->bytes != 0) pjs_heap_free(file->bytes);
    *file = (PjsStorageFile){0};
}

uint32_t pjs_storage_last_error(void)
{
    return storage_error;
}

uint32_t pjs_storage_sector_read_count(void)
{
    return storage_sector_reads;
}

uint32_t pjs_storage_first_failed_lba(void)
{
    return storage_first_failed_lba;
}
