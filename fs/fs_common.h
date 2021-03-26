//
// Created by davis on 2021/3/15.
//

#include "stdio.h"
#include "pthread.h"
#ifndef OPENBHOS_FS_FS_COMMON_H
#define OPENBHOS_FS_FS_COMMON_H

#define CONFIG_FS_BLOCK_SIZE 512
#define CONFIG_FS_BLOCK_CACHE_CNT 1024
#define CONFIG_FS_ENTRY_CACHE_CNT 64
#define CONFIG_FS_FAT32_MAX_FILENAME_LEN 15
#define CONFIG_FS_FAT32_DEV_NO 0
#define NULL (void *)0

typedef int bool;
typedef unsigned int uint32_t;
typedef unsigned short uint16_t;
typedef unsigned char uint8_t;
typedef unsigned char byte;
typedef void * node_data_t;

typedef unsigned long size_t;

typedef int rw_lock_t;


static void assert(bool in , char * text){
    if(!in){
        printf(text);
        while(1);
    }
}

static void panic(char * text){
    printf(text);
    while(1);
}

#define ASSERT assert
#define PANIC panic
#define BLOCK_NO_ERROR 0xFFFFFFFF
#define true 1
#define false 0

typedef
struct dnode{
    struct dnode* prev;
    struct dnode* next;
    node_data_t data;
} dnode_t;


typedef struct{
    dnode_t * head;
    dnode_t * tail;
    size_t size;
}dlink_t;

typedef
struct {
    int dev_no;
    uint32_t block_no;    //eq to selector number.
    bool dirty;     // if the block is not sync with disk, dirty will be set.
    rw_lock_t rw_lock;
    byte data[CONFIG_FS_BLOCK_SIZE];
    dnode_t dnode;
} block_t;

typedef
struct{
    block_t buffer[CONFIG_FS_BLOCK_CACHE_CNT];
    dlink_t dlink;
    bool dirty;
    rw_lock_t rw_lock;
} block_cache_t;

static inline void fs_stub_rw_lock_init(void * lock){

}

static inline void fs_stub_rw_r_lock_acquire(void * lock){

}

static inline void fs_stub_rw_r_lock_release(void * lock){

}

static inline void fs_stub_rw_w_lock_acquire(void * lock){

}

static inline void fs_stub_rw_w_lock_release(void * lock){

}

//declare
void read_select(void * buffer , uint32_t select_no);
void write_select(void * buffer , uint32_t select_no);
void disk_init();

// must holding block write lock
static inline void fs_stub_source_read(block_t * block){
    read_select(block->data,block->block_no);
}

// must holding block read lock
static inline void fs_stub_source_write(block_t * block){
    write_select(block->data,block->block_no);
}

static inline void fs_stub_source_init(){
    disk_init();
}

void dlink_add_tail(dlink_t * dlink, dnode_t * dnode);

void dlink_add_head(dlink_t * dlink, dnode_t * dnode);

dnode_t * dlink_find_dnode_by_data(dlink_t * dlink, node_data_t data);

dnode_t * dlink_remove_dnode_unsafe(dlink_t *dlink,dnode_t * dnode);

dnode_t * dlink_remove_by_data(dlink_t * dlink, node_data_t data);

void dlink_test();

#endif //OPENBHOS_FS_FS_COMMON_H