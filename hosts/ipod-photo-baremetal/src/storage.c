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
#define ATA_TIMEOUT_US 2000000u

static uint32_t storage_error;
static const char guest_directory[11] = {'P','O','C','K','E','T','J','S',' ',' ',' '};
static const char guest_filename[11] = {'A','P','P',' ',' ',' ',' ',' ','P','K','T'};

static bool wait_bsy_clear(void)
{
    uint32_t start = timer_now_us();
    for (;;) {
        if ((PP_ATA_ALT_STATUS & ATA_STATUS_BSY) == 0u) return true;
        if ((uint32_t)(timer_now_us() - start) >= ATA_TIMEOUT_US) return false;
    }
}

static bool wait_drq(void)
{
    uint32_t start = timer_now_us();
    for (;;) {
        uint8_t status = PP_ATA_ALT_STATUS;
        if ((status & (ATA_STATUS_ERR | ATA_STATUS_DF)) != 0u) return false;
        if ((status & ATA_STATUS_BSY) == 0u && (status & ATA_STATUS_DRQ) != 0u) {
            return true;
        }
        if ((uint32_t)(timer_now_us() - start) >= ATA_TIMEOUT_US) return false;
    }
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
    if (lba >= 0x10000000u || sector == 0) return false;
    if (!wait_bsy_clear()) return false;

    PP_ATA_SELECT = (uint8_t)(ATA_SELECT_LBA | ((lba >> 24) & 0x0fu));
    ata_delay_400ns();
    if (!wait_bsy_clear()) return false;

    PP_ATA_NSECTOR = 1u;
    PP_ATA_SECTOR = (uint8_t)lba;
    PP_ATA_LCYL = (uint8_t)(lba >> 8);
    PP_ATA_HCYL = (uint8_t)(lba >> 16);
    PP_ATA_COMMAND = ATA_CMD_READ_SECTORS;
    if (!wait_drq()) return false;

    for (uint32_t word = 0u; word < PJS_STORAGE_SECTOR_BYTES / 2u; ++word) {
        uint16_t value = PP_ATA_DATA;
        sector[word * 2u] = (uint8_t)value;
        sector[word * 2u + 1u] = (uint8_t)(value >> 8);
    }
    if (!wait_bsy_clear()) return false;
    uint8_t status = PP_ATA_STATUS;
    return (status & (ATA_STATUS_BSY | ATA_STATUS_DF | ATA_STATUS_ERR)) == 0u;
}

int pjs_storage_load_guest(PjsStorageFile *file)
{
    if (file == 0) return PJS_STORAGE_ERR_ARGUMENT;
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
    rc = pjs_fat32_short_file_size(&fat, guest_directory, guest_filename, &expected);
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
    rc = pjs_fat32_read_short_file(&fat, guest_directory, guest_filename,
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
