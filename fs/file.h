//
// Created by davis on 2021/3/29.
//

#ifndef OPENBHOS_FS_FILE_H
#define OPENBHOS_FS_FILE_H

#include "fs_common.h"

#define BHOS_EOF

typedef
enum {
    FMODE_READ,
    FMODE_WRITE,
    FMODE_APPEND,
    FMODE_READ_WRITE,
} fmode_t;

typedef
struct{

}BHOS_FILE;

BHOS_FILE * bhos_fopen( const char * filename, fmode_t fmode);
int bhos_fclose(BHOS_FILE *fp);
int bhos_fread(BHOS_FILE *fp ,void * buffer,size_t length);
int bhos_fwrite(BHOS_FILE *fp ,void * buffer,size_t length);
#endif //OPENBHOS_FS_FILE_H
