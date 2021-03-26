#include <stdio.h>
#include "fs/virtul_disk.h"
#include "fs/fs.h"
#include "fs/fat32.h"
#include "string.h"

int main() {
    unsigned char buffer[512];
    block_module_init(0);
    fat32_module_init();
    fat32_test();
    disk_close();
    while (1);
    return 0;
}
