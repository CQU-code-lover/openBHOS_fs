//
// Created by davis on 2021/3/15.
//

#include "fat32.h"
#include "block.h"
#include "string.h"

static fs_t fat32;

void fat32_init(){
    fs_t * tmp_fat = &fat32;
    block_t * block = block_get_read(0,0);    // first selector
    assert(strncmp((char const*)(block->data + 0x52), "FAT32", 5)==0,"not FAT32 volume");
    fat32.bpb.byts_per_sec = *(uint16_t *)(block->data + 0x0B);
    fat32.bpb.sec_per_clus = *(block->data + 0x0D);
    fat32.bpb.rsvd_sec_cnt = *(uint16_t *)(block->data + 0x0E);
    fat32.bpb.fat_cnt = *(block->data + 0x10);
    fat32.bpb.hidd_sec = *(uint32_t *)(block->data + 0x1C);
    if(*(uint16_t *)(block->data + 0x13) == 0){
        // bigger than 32MB
        fat32.bpb.tot_sec = *(uint32_t *)(block->data + 0x20);
    }
    else{
        // little than 32MB
        fat32.bpb.tot_sec = *(uint16_t *)(block->data + 0x13);
    }
    if(*(uint16_t *)(block->data + 0x16) == 0){
        fat32.bpb.fat_sz = *(uint32_t *)(block->data + 0x24);
    }
    else{
        fat32.bpb.fat_sz = *(uint16_t *)(block->data + 0x16);
    }
    fat32.bpb.root_clus = *(uint32_t *)(block->data + 0x2C);
    fat32.first_data_sec = fat32.bpb.rsvd_sec_cnt + fat32.bpb.fat_cnt * fat32.bpb.fat_sz;
    fat32.data_sec_cnt = fat32.bpb.tot_sec - fat32.first_data_sec;
    fat32.data_clus_cnt = fat32.data_sec_cnt / fat32.bpb.sec_per_clus;
    fat32.byts_per_clus = fat32.bpb.sec_per_clus * fat32.bpb.byts_per_sec;
    assert(fat32.byts_per_clus == fat32.bpb.byts_per_sec, "Not support:sector size not equaled to clus size!\n");
}

static inline void * _clus_read(uint32_t clus_no , void * read_buffer){

}

static inline void _clus_write(uint32_t clus_no , void * write_buffer, int offset , int length){

}

static inline void _clus_copy_out(uint32_t clus_no , void * buffer , int offset ,int length){

}

static inline void _clus_clear(uint32_t clus_no){

}

static inline void _clus_alloc(){

}