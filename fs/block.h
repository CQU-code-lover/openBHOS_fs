//
// Created by davis on 2021/3/18.
//

#ifndef OPENBHOS_FS_BLOCK_H
#define OPENBHOS_FS_BLOCK_H
#include "fs_common.h"

void block_flush(block_t * block);

void block_flush_all();

void block_module_init(int dev_no);

block_t * block_get_read(uint32_t block_no , int dev_no);

block_t * block_get_write(uint32_t block_no , int dev_no);

void block_put_read(block_t * block);

void block_put_write(block_t * block);

void block_put_write_with_flush(block_t * block);

#endif //OPENBHOS_FS_BLOCK_H
