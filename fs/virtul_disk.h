//
// Created by davis on 2021/3/18.
//

#ifndef OPENBHOS_FS_VIRTUL_DISK_H
#define OPENBHOS_FS_VIRTUL_DISK_H
#include "fs_common.h"

void disk_init();
void disk_close();
void read_select(void * buffer , uint32_t select_no);
void write_select(void * buffer , uint32_t select_no);

#endif //OPENBHOS_FS_VIRTUL_DISK_H