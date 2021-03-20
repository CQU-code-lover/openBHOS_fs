cmake_minimum_required(VERSION 3.17)
project(openBHOS_fs C)

set(CMAKE_C_STANDARD 99)

add_executable(openBHOS_fs main.c fs/fs.h fs/fs_common.h fs/block.c fs/fat32.c fs/fat32.h fs/dlink.c fs/block.h fs/virtul_disk.c fs/virtul_disk.h)