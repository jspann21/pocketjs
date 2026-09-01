#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "storage.h"

#define SECTORS 600u
#define FILE_BYTES (PJS_STORAGE_SECTOR_BYTES * 2u + 173u)
static uint8_t disk[SECTORS][PJS_STORAGE_SECTOR_BYTES];
static uint8_t expected[FILE_BYTES];

static void put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void put32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static bool read_sector(void *context, uint32_t lba, uint8_t sector[PJS_STORAGE_SECTOR_BYTES]) {
    (void)context;
    if (lba >= SECTORS) return false;
    memcpy(sector, disk[lba], PJS_STORAGE_SECTOR_BYTES);
    return true;
}
static void dirent(uint8_t *e, const char name[11], uint8_t attr, uint32_t cluster, uint32_t size) {
    memcpy(e,name,11); e[11]=attr; put16(e+20,(uint16_t)(cluster>>16)); put16(e+26,(uint16_t)cluster); put32(e+28,size);
}
static void make_disk(void) {
    memset(disk,0,sizeof(disk));
    uint8_t *mbr=disk[0]; mbr[510]=0x55; mbr[511]=0xaa;
    uint8_t *p=mbr+446; p[4]=0x0c; put32(p+8,1); put32(p+12,120);
    uint8_t *b=disk[1]; b[0]=0xeb; b[1]=0x58; b[2]=0x90; put16(b+11,512); b[13]=1; put16(b+14,1); b[16]=1; put16(b+17,0); put16(b+19,0); put16(b+22,0); put32(b+32,120); put32(b+36,547); put32(b+44,2); b[510]=0x55; b[511]=0xaa;
    /* This synthetic image advertises FAT32 cluster count using a large total,
       but the reader only needs the low LBAs touched by the test. */
    put32(b+32,70000);
    put32(p+12,70000);
    uint8_t *fat=disk[2];
    put32(fat+0,0x0ffffff8);
    put32(fat+4,0xffffffff);
    put32(fat+8,0x0fffffff);  /* cluster 2: root directory */
    put32(fat+12,0x0fffffff); /* cluster 3: POCKETJS directory */
    put32(fat+16,9);          /* cluster 4 -> cluster 9 */
    put32(fat+24,0x0fffffff); /* cluster 6: end of file */
    put32(fat+36,6);          /* cluster 9 -> cluster 6 */
    const char dname[11]={'P','O','C','K','E','T','J','S',' ',' ',' '};
    const char fname[11]={'A','P','P',' ',' ',' ',' ',' ','P','K','T'};
    dirent(disk[549],dname,0x10,3,0);
    dirent(disk[550],fname,0x20,4,FILE_BYTES);
    for (uint32_t index=0; index<FILE_BYTES; ++index) {
        expected[index]=(uint8_t)((index*37u+11u)&0xffu);
    }
    memcpy(disk[551],expected,PJS_STORAGE_SECTOR_BYTES); /* cluster 4 */
    memcpy(disk[556],expected+PJS_STORAGE_SECTOR_BYTES,
           PJS_STORAGE_SECTOR_BYTES);                    /* cluster 9 */
    memcpy(disk[553],expected+PJS_STORAGE_SECTOR_BYTES*2u,
           FILE_BYTES-PJS_STORAGE_SECTOR_BYTES*2u);      /* cluster 6 */
}
int main(void) {
    make_disk();
    PjsFat32 fat;
    assert(pjs_fat32_mount(&fat,read_sector,0)==PJS_STORAGE_OK);
    uint8_t out[FILE_BYTES]={0}; uint32_t length=0;
    const char dname[11]={'P','O','C','K','E','T','J','S',' ',' ',' '};
    const char fname[11]={'A','P','P',' ',' ',' ',' ',' ','P','K','T'};
    uint32_t size=0;
    assert(pjs_fat32_short_file_size(&fat,dname,fname,&size)==PJS_STORAGE_OK &&
           size==FILE_BYTES);
    assert(pjs_fat32_read_short_file(&fat,dname,fname,out,sizeof(out),&length)==PJS_STORAGE_OK);
    assert(length==FILE_BYTES && memcmp(out,expected,FILE_BYTES)==0);
    puts("fat32 fragmented multi-cluster loader: OK");
    return 0;
}
