//
// Created by davis on 2021/3/15.
//

#ifndef OPENBHOS_FS_FAT32_H
#define OPENBHOS_FS_FAT32_H

#include "fs_common.h"

//differet in 32bits system
#define ROOT_PARENT 0xFFFFFFFFFFFFFFFF
#define FAT32_FILE_END 0x0FFFFFFF
#define FAT32_CLUS0 0xF8FFFF0F
#define FAT32_EOC 0x0FFFFFF8
#define FAT32_BAD 0x0FFFFFF7      //bad cluster
#define FAT32_VALID_MAX FAT32_BAD
#define ENTRY_ATTR_DIR 0x10
#define ENTRY_ATTR_ROLL 0x08
#define ENTRY_ATTR_ARCHIVE 0x20
#define ENTRY_ATTR_LONG_NAME 0x0F
#define MAX_FULL_NAME 13

typedef
struct {
    uint32_t first_data_sec;
    uint32_t data_sec_cnt;
    uint32_t data_clus_cnt;
    uint32_t byts_per_clus;

    struct {
        uint16_t byts_per_sec;      // offset:0x0B~0x0C
        uint8_t  sec_per_clus;      //        0x0D~0x0D
        uint16_t rsvd_sec_cnt;      //        0x0E~0x0F
        uint8_t  fat_cnt;           //        0x10~0x10
        uint32_t hidd_sec;          //        0x1C~0x1F     count of hidden sectors to EBR
        uint32_t tot_sec;           //total count of sectors including all regions
        //        0x13~0x14 when size of partition less than 32MB
        //        0x20~0x23 when size of partition bigger than 32MB
        uint32_t fat_sz;            //count of sectors for a FAT region
        //        0x16~0x17 when size of fat32 less than 32MB
        //        0x24~0x27 when size of fat32 bigger than 32MB
        uint32_t root_clus;         //        0x2C~0x2F root dir first clus,
        // it will be 0x2 under normal conditions.
    } bpb;
} fs_t;

typedef
struct entry_s{
    char filename[CONFIG_FS_FAT32_MAX_FILENAME_LEN];
    struct entry_s * parent;
    uint32_t ref_cnt;
    uint8_t attr;
    uint32_t first_clus_no;
    uint32_t file_size;
    bool dirty;
    dnode_t dnode;
    uint32_t offset_in_dir;
    rw_lock_t rw_lock;
}entry_t;

typedef
struct{
    entry_t buffer[CONFIG_FS_ENTRY_CACHE_CNT];
    dlink_t dlink;
    bool dirty;
    rw_lock_t rw_lock;
} entry_cache_t;

typedef
struct {
    char name_head[8];
    char name_suffix[3];
    uint8_t attr;
    uint8_t rev;
    uint8_t create_time_ten_ms;
    uint16_t create_time;
    uint16_t create_day;
    uint16_t last_access_day;
    uint16_t first_clus_high;
    uint16_t last_write_time;
    uint16_t last_write_day;
    uint16_t first_clus_low;
    uint32_t file_size;
}__attribute__((packed)) entry_data_t;

void fat32_module_init();
void fat32_test();
#endif //OPENBHOS_FS_FAT32_H
