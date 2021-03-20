#include <stdio.h>
#include "fs/virtul_disk.h"
#include "fs/fs.h"
#include "fs/fat32.h"


int main() {
    unsigned char buffer[512];
    block_module_init(0);
    block_t * block = block_get_write(0,0);
    fat32_init();
    block->data[0]='C';
    block_put_write_with_flush(block);
    read_select(buffer,0);
    disk_close();
    return 0;
}
