//
// Created by davis on 2021/3/18.
//

#include "virtul_disk.h"
#include "stdio.h"
#include "fs_common.h"
#define SELECTOR_SIZE 512
FILE * disk = NULL;
uint32_t max_selector_no;
inline uint32_t disk_get_max_selector_no(){
    return  max_selector_no;
}

void disk_init(){
    disk = fopen("../fs/fs.img","rb+");
    assert(disk!=NULL,"disk can`t access!\n");
    fseek(disk,0L,SEEK_END);
    max_selector_no = ftell(disk)/SELECTOR_SIZE;
    fseek(disk,0L,SEEK_SET);
}

void disk_close(){
    fclose(disk);
}

void read_select(void * buffer , uint32_t select_no){
    assert(select_no<max_selector_no,"selector number bigger than max!\n");
    fseek(disk,SELECTOR_SIZE * select_no,SEEK_SET);
    fread(buffer,SELECTOR_SIZE,1,disk);
}

void write_select(void * buffer , uint32_t select_no){
    assert(select_no<max_selector_no,"selector number bigger than max!\n");
    fseek(disk,SELECTOR_SIZE * select_no,SEEK_SET);
    fwrite(buffer,SELECTOR_SIZE,1,disk);
}