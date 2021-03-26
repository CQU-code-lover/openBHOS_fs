//
// Created by davis on 2021/3/15.
//

#include "fat32.h"
#include "block.h"
#include "string.h"

static fs_t fat32;
static entry_cache_t entry_cache;
static entry_t * root;
static inline uint32_t _fat_sec_no_of_clus(uint32_t clus_no, uint8_t fat_no)
{
    return fat32.bpb.rsvd_sec_cnt + (clus_no << 2) / fat32.bpb.byts_per_sec + fat32.bpb.fat_sz * fat_no;
}

static inline uint32_t _first_sec_in_clus(uint32_t clus_no){
    return ((clus_no - 2) * fat32.bpb.sec_per_clus) + fat32.first_data_sec;
}

static inline uint32_t _fat_offset_in_sec_of_clus(uint32_t clus_no){
    return clus_no%(fat32.bpb.byts_per_sec>>2);
}

static inline void _clus_clear(uint32_t clus_no){
    uint32_t sec= _first_sec_in_clus(clus_no);
    for(int i = 0;i<fat32.bpb.sec_per_clus;i++,sec++){
        block_t * block = block_get_write(sec,CONFIG_FS_FAT32_DEV_NO);
        memset(block->data,0,CONFIG_FS_BLOCK_SIZE);
        block_put_write(block);
    }
}

static uint32_t _fat_read(uint32_t clus_no)
{
    if (clus_no >= FAT32_EOC) {
        return clus_no;
    }
    if (clus_no > fat32.data_clus_cnt + 1) {
        return 0;
    }
    uint32_t fat_sec = _fat_sec_no_of_clus(clus_no, 0);
    block_t * block = block_get_read(fat_sec,0);
    uint32_t next_clus = *((uint32_t *)block->data + _fat_offset_in_sec_of_clus(clus_no));
    block_put_read(block);
    return next_clus;
}

static uint32_t _fat_write(uint32_t clus_no, uint32_t data){
    if (clus_no >= FAT32_EOC) {
        return clus_no;
    }
    if (clus_no > fat32.data_clus_cnt + 1) {
        return 0;
    }
    uint32_t fat_sec = _fat_sec_no_of_clus(clus_no, 0);
    block_t * block = block_get_write(fat_sec,0);
    uint32_t pre_data = *((uint32_t *)block->data + _fat_offset_in_sec_of_clus(clus_no));
    *((uint32_t *)block->data + _fat_offset_in_sec_of_clus(clus_no)) = data;
    block_put_write(block);
    return pre_data;
}

static inline uint32_t _clus_alloc(){
    block_t *block;
    uint32_t sec = fat32.bpb.rsvd_sec_cnt;
    uint32_t const ent_per_sec = fat32.bpb.byts_per_sec / sizeof(uint32_t);
    for (uint32_t i = 0; i < fat32.bpb.fat_sz; i++, sec++) {
        block = block_get_write(sec,0);
        for (uint32_t j = 0; j < ent_per_sec; j++) {
            if (((uint32_t *)(block->data))[j] == 0) {
                ((uint32_t *)(block->data))[j] = FAT32_EOC + 7;
                block_put_write(block);
                uint32_t clus = i * ent_per_sec + j;
                _clus_clear(clus);
                return clus;
            }
        }
        block_put_write(block);
    }
    PANIC("no clusters");
    return 0;
}



static inline void _clus_free(uint32_t clus_no){
    _fat_write(clus_no ,0);
}


/*!
 * @note read or write a clus.
 * @param clus_no
 * @param buffer
 * @param offset
 * @param length
 * @param write
 */
static void _clus_rw(uint32_t clus_no, void * buffer, uint32_t offset, uint32_t length , bool write){
    ASSERT(offset<fat32.byts_per_clus,"error in offset!\n");
    if(offset+length>fat32.byts_per_clus){
        length = fat32.byts_per_clus-offset;
    }
    uint32_t first_sec =_first_sec_in_clus(clus_no);
    uint32_t max_sec = first_sec+fat32.bpb.sec_per_clus;
    uint32_t sec_shift = offset/fat32.bpb.byts_per_sec;
    uint32_t offset_in_sec = offset%fat32.bpb.byts_per_sec;
    uint32_t buffer_offset = 0;
    for(uint32_t probe_sec = first_sec+sec_shift; probe_sec < max_sec; probe_sec++,offset_in_sec = 0){
        if(offset_in_sec+length>fat32.bpb.byts_per_sec){
            block_t * block = block_get_read(probe_sec,0);
            uint32_t cpy_len = fat32.bpb.byts_per_sec - offset_in_sec;
            if(write){
                memcpy(block->data+offset_in_sec,buffer + buffer_offset,  cpy_len);
            }
            else{
                memcpy(buffer + buffer_offset, block->data+offset_in_sec, cpy_len);
            }
            buffer_offset+=cpy_len;
            block_put_read(block);
            length -= cpy_len;
        }
        else{
            block_t * block = block_get_read(probe_sec,0);
            if(write){
                memcpy( block->data+offset_in_sec,buffer + buffer_offset, length);
            }
            else{
                memcpy(buffer + buffer_offset, block->data+offset_in_sec, length);
            }
            block_put_read(block);
        }
    }
}

static void _multi_clus_rw(uint32_t start_clus_no, void * buffer , uint32_t offset, uint32_t length,bool write){
    // relocate the start_clus_no and start offset
    uint32_t clus_no_offset = offset/fat32.byts_per_clus;
    for(;clus_no_offset>0;clus_no_offset--){
        start_clus_no = _fat_read(start_clus_no);
        if(start_clus_no>=FAT32_VALID_MAX){
            PANIC("Multi clusters rw fault!\n");
        }
    }
    offset %=fat32.byts_per_clus;
    uint32_t buffer_offset = 0;
    for(uint32_t probe_clus = start_clus_no;length>0;){
        _clus_rw(probe_clus,buffer+buffer_offset,offset,length,write);
        if(length>fat32.byts_per_clus - offset){
            // have other clus
            buffer_offset+=(fat32.byts_per_clus-offset);
            length-=(fat32.byts_per_clus-offset);
            offset=0;
            // get next clus
            probe_clus = _fat_read(probe_clus);
            if(probe_clus>=FAT32_VALID_MAX){
                PANIC("Multi clusters rw fault!\n");
            }
        }
        else{
            // is the last clus
            break;
        }
    }
}

bool _name_check(const char * name){
    int head_len = 0;
    int suffix_len = 0;
    int point_len = 0;
    bool point_get = false;
    for(int i =0 ;*(name+i)!='\0';i++){
        if(point_get){
            suffix_len++;
        }
        else{
            if(*(name+i)=='.'){
                point_len++;
                point_get=true;
            }
            else{
                head_len++;
            }
        }
    }
    return point_len==1&&suffix_len<=3&&head_len<=8;
}

void _name_get_head(const char * name,char * head){
    int i =0;
    for(;name[i]!='\0';i++){
        if(name[i]=='.'){
            break;
        }
        else{
            head[i] = name[i];
        }
    }
    head[i] = '\0';
}
void _name_get_suffix(const char * name,char * suffix){
    int i =0;
    for(;name[i]!='.';i++);
    i++;
    for(;name[i]!='\0';i++){
        suffix[i] = name[i];
    }
    suffix[i] = '\0';
}

void _name_get_full_name_from_data(entry_data_t * entryData, char * name_buffer){
    memcpy(name_buffer,entryData->name_head,8);
    int i = 0;
    for(;i<8&&name_buffer[i]!=0x20;i++);
    if(entryData->attr!=ENTRY_ATTR_ARCHIVE){
        name_buffer[i] = '\0';
        return;
    }
    name_buffer[i] = '.';
    i++;
    for(int j = 0;i<MAX_FULL_NAME-1&&entryData->name_suffix[j]!=0x20;i++,j++){
        name_buffer[i] = entryData->name_suffix[j];
    }
    if(i>=MAX_FULL_NAME){
        PANIC("Error of name length!\n");
    }
    name_buffer[i] = '\0';
}

/*!
 * @note load entry to cache.
 * @warning Must Invoking With Holding
 *          entry Write Lock and parent`s
 *          Read or Write Lock.
 * @param entry
 * @param first_clus_no : the entry`s first cluster number.
 */
bool entry_load(entry_t * parent,const char * name,entry_t * entry){
    if(parent->attr!=ENTRY_ATTR_DIR){
        PANIC("Parent is not a dir!\n");
    }
    char name_buffer[MAX_FULL_NAME];
    entry_data_t entry_data;
    uint32_t clus_no = parent->first_clus_no;
    uint32_t offset = 0;
    while(true){
        for (; offset < parent->file_size; offset+=32) {
            if(offset>parent->file_size+32){
                goto not_find;
            }
            _multi_clus_rw(clus_no, &entry_data, offset,32, false);
            //name check
            _name_get_full_name_from_data(&entry_data,name_buffer);
            if(strcmp(name_buffer,name)==0){
                // check entry is available
                if(entry_data.name_head[0]==0xE5){
                    goto not_find;
                }
                if(entry->attr==ENTRY_ATTR_LONG_NAME||entry->attr == ENTRY_ATTR_ROLL){
                    // long name entry
                    goto not_find;
                }
                goto success_get;
            }
        }
        uint32_t next;
        if((next =_fat_read(clus_no))<FAT32_EOC){
            clus_no = next;
        }
        else{
            // not find
            break;
        }
    }
    not_find:
    return false;
    success_get:
    strncpy(entry->filename,name_buffer,MAX_FULL_NAME);
    entry->dirty = false;
    entry->first_clus_no = (entry_data.first_clus_high<<16)|entry_data.first_clus_low;
    entry->parent = parent;
    entry->ref_cnt = 1;
    entry->file_size = entry_data.file_size;
    entry->attr = entry_data.attr;
    entry->offset_in_dir = offset;
    return true;
}


/*!
 * @note store entry from cache to block layer.
 * @warning Must Invoking With Holding Entry
 *          And Parent`s Read or Write Lock.
 * @param entry
 */
bool entry_flush(entry_t * entry){
    if(entry->parent==NULL||entry->parent == ROOT_PARENT){
        return false;
    }
    entry_t * parent = entry->parent;
    uint32_t offset = entry->offset_in_dir;
    entry_data_t data;
    _clus_rw(parent->first_clus_no+offset/fat32.byts_per_clus,&data,offset%fat32.byts_per_clus,32,false);

    data.file_size = entry->file_size;
    data.attr = entry->attr;
    data.first_clus_low = entry->first_clus_no<<16>>16;
    data.first_clus_high = entry->first_clus_no>>16;
    char name_head[9];
    char name_suffix[4];

    if(data.attr==ENTRY_ATTR_ARCHIVE){
        if(!_name_check(entry->filename)){
            PANIC("Name is invalid!\n");
        }
        _name_get_head(entry->filename,name_head);
        _name_get_suffix(entry->filename,name_suffix);
        strncpy(data.name_head,name_head,8);
        strncpy(data.name_suffix,name_suffix,3);
    }
    else{
        strncpy(data.name_head,name_head,8);
        for(int k=0;k<8;k++){
            if(*(data.name_head+k)=='\0'){
                for(int m = k;m<8;m++){
                    *(data.name_head+k) = 0x20;
                }
                break;
            }
        }
    }
    _clus_rw(parent->first_clus_no+offset/fat32.byts_per_clus,&data,offset%fat32.byts_per_clus,32,true);
    return true;
}

/*!
 * @note get a idle entry with holding it`s write lock.
 *       generally invoking by entry_new.
 * @return idle entry or NULL when idle entry not find.
 */
entry_t * _entry_get_idle_write(){
    entry_t * ret = NULL;
    fs_stub_rw_w_lock_acquire(&entry_cache.rw_lock);
    for(dnode_t * probe = entry_cache.dlink.head;probe!=NULL;probe=probe->prev){
        entry_t * entry = probe->data;
        // get entry write lock
        fs_stub_rw_w_lock_acquire(&entry->rw_lock);
        if(entry->ref_cnt == 0){
            // get idle
            if(entry->parent!=NULL){
                fs_stub_rw_w_lock_acquire(&entry->parent->rw_lock);
                entry_flush(entry);
                entry->parent->ref_cnt--;
                fs_stub_rw_w_lock_release(&entry->parent->rw_lock);
            }
            ret = entry;
            break;
        }
        fs_stub_rw_w_lock_release(&entry->rw_lock);
    }
    fs_stub_rw_w_lock_release(&entry_cache.rw_lock);
    return ret;
}

entry_t * _entry_get(entry_t * parent, char * name, bool write){
    //first: search subdir in entry cache
    fs_stub_rw_r_lock_acquire(&entry_cache.rw_lock);
    entry_t * entry;
    entry_t * entry_idle=NULL;
    for(dnode_t * node_probe = entry_cache.dlink.head;node_probe!=NULL;node_probe=node_probe->next){
        entry= node_probe->data;
        if(write){
            fs_stub_rw_w_lock_acquire(&entry->rw_lock);
        }
        else{
            fs_stub_rw_r_lock_acquire(&entry->rw_lock);
        }
        if(strcmp(entry->filename , name) == 0){
            // cache hit!
            if(entry->parent==NULL&&entry->parent!=parent){
                PANIC("error of parent dir!\n");
            }
            fs_stub_rw_r_lock_release(&entry_cache.rw_lock);
            return entry;
        }
        if(entry->ref_cnt == 0){
            entry_idle = entry;
        }
        if(write){
            fs_stub_rw_w_lock_release(&entry->rw_lock);
        }
        else{
            fs_stub_rw_r_lock_release(&entry->rw_lock);
        }
    }
    // not hit !!!
    // load from block
    if(entry_idle!=NULL){
        fs_stub_rw_w_lock_acquire(&entry_idle->rw_lock);
        if(entry_idle->parent!=NULL){
            // root can`t be idle entry, so don`t consider this case.
            fs_stub_rw_w_lock_acquire(&entry_idle->parent->rw_lock);
            entry_flush(entry_idle);
            entry_idle->parent->ref_cnt--;
            fs_stub_rw_w_lock_release(&entry_idle->parent->rw_lock);
        }
        fs_stub_rw_w_lock_acquire(&parent->rw_lock);
        if(!entry_load(parent,name,entry_idle)){
            fs_stub_rw_w_lock_release(&parent->rw_lock);
            if(write){
                fs_stub_rw_w_lock_release(&entry_idle->rw_lock);
            }
            else{
                fs_stub_rw_r_lock_release(&entry_idle->rw_lock);
            }
            fs_stub_rw_r_lock_release(&entry_cache.rw_lock);
            return NULL;
        }
        parent->ref_cnt++;
        fs_stub_rw_w_lock_release(&parent->rw_lock);
        if(!write){
            fs_stub_rw_w_lock_release(&entry_idle->rw_lock);
            fs_stub_rw_r_lock_acquire(&entry_idle->rw_lock);
        }
        fs_stub_rw_r_lock_release(&entry_cache.rw_lock);
        return entry_idle;
    }
    else{
        PANIC("All entries are busy!\n");
        return NULL;
    }
}

entry_t * entry_get_read(entry_t * parent, char * name){
    return _entry_get(parent,name,false);
};

entry_t *  entry_get_write(entry_t * parent, char * name){
    entry_t * entry = _entry_get(parent,name,true);
    if(entry == NULL){
        return NULL;
    }
    entry->dirty = true;
    return entry;
}

void entry_put_read(entry_t * entry) {
    fs_stub_rw_r_lock_release(&entry->rw_lock);
    fs_stub_rw_w_lock_acquire(&entry->rw_lock);
    entry->ref_cnt--;
    fs_stub_rw_w_lock_release(&entry->rw_lock);
}

void entry_put_write(entry_t * entry){
    entry->ref_cnt--;
    fs_stub_rw_w_lock_release(&entry->rw_lock);
}

/*!
 * @note read or write a file.
 * @warning must hold writing lock when write and read lock when reading.
 *          don`t hold parent entry`s lock.
 * @param entry
 * @param buffer
 * @param offset
 * @param length
 * @param write
 */
void entry_rw(entry_t * entry,void * buffer,uint32_t offset, uint32_t length,bool write){
    if(entry->file_size == 0){
        if(entry->first_clus_no == 0){
            // this is a new create file with no cluster allocating.
            // alloc one
            entry->first_clus_no = _clus_alloc();
        }
    }
    uint32_t clus_cnt = entry->file_size/fat32.byts_per_clus;
    if(entry->file_size%fat32.byts_per_clus!=0){
        clus_cnt++;
    }
    uint32_t allocated_size = clus_cnt * fat32.byts_per_clus;
    uint32_t file_now_size = offset + length;
    if(file_now_size > allocated_size){
        if(write){
            // alloc more cluster
            uint32_t alloc_clus_cnt = (file_now_size - allocated_size)/fat32.byts_per_clus;
            if((file_now_size - allocated_size)%fat32.byts_per_clus!=0){
                alloc_clus_cnt++;
            }
            // find file end
            uint32_t probe_clus = entry->first_clus_no;
            for(;_fat_read(probe_clus)!= FAT32_FILE_END;probe_clus = _fat_read(probe_clus));
            for(;alloc_clus_cnt>0;alloc_clus_cnt--){
                uint32_t next = _clus_alloc();
                _fat_write(probe_clus, next);
                probe_clus = next;
            }
            entry->file_size = file_now_size;
        }
        else{
            // if read out of the file size,panic
            PANIC("Read Out Of File!\n");
        }
    }
    // do read or write
    _multi_clus_rw(entry->first_clus_no,buffer,offset,length,write);
}

/*!
 * @warning must hold parent write lock
 * @param parent
 * @param name
 * @param attr
 * @return new entry with write lock or NULL when fail to create entry.
 */
entry_t *entry_create_write(entry_t * parent , char * name , uint8_t attr){
    ASSERT(parent!=NULL&&parent->attr == ENTRY_ATTR_DIR&&strlen(name)<MAX_FULL_NAME ,"Parent Dir is Not Dir!\n");
    if(entry_get_read(parent,name)!=NULL){
        // this entry is exist
        return NULL;
    }
    entry_t * idle = _entry_get_idle_write();
    idle->parent = parent;
    idle->first_clus_no = 0;
    idle->file_size = 0;
    strcpy(idle->filename,name);
    entry_data_t new_entry_data;
    // write a empty entry to parent.
    // the parent file size will change
    entry_rw(parent,&new_entry_data,parent->file_size,sizeof(entry_data_t),true);
    idle->offset_in_dir = parent->file_size-32;
    return idle;
}

bool entry_rm(entry_t * parent,char * name){
    
}

void entry_ls(entry_t * parent,void * buffer){
    ASSERT(parent->attr==ENTRY_ATTR_DIR,"this entry is not a dir!\n");

}

void fat32_module_init(){
    bzero(&entry_cache, sizeof(entry_cache_t));
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

    //entry cache init
    entry_cache.dirty = false;
    fs_stub_rw_lock_init(&entry_cache.rw_lock);
    for(int i =0;i<CONFIG_FS_ENTRY_CACHE_CNT;i++){
        entry_t * entry = &entry_cache.buffer[i];
        entry->dnode.data = entry;
        entry->ref_cnt = 0;
        entry->dirty = false;
        entry->attr = 0;
        entry->file_size = 0;
        entry->parent = NULL;
        fs_stub_rw_lock_init(&entry->rw_lock);
        dlink_add_tail(&entry_cache.dlink,&entry_cache.buffer[i].dnode);
    }
    entry_cache.dirty = false;

    // load root dir to entry cache
    root = entry_cache.dlink.head->data;
    root->ref_cnt = 1;
    fs_stub_rw_lock_init(&root->rw_lock);
    root->parent = NULL;
    root->dirty = false;
    strcpy(root->filename,"root");
    root->parent = ROOT_PARENT;
    root->first_clus_no = 2;
    //load root`s file size
    char probe_buffer[32];
    uint32_t clus = 2;
    uint32_t offset = 0;
    while(true){
        _multi_clus_rw(clus, probe_buffer, offset, 32, false);
        bool zero_flag = true;
        for(int i=0;i<32;i++){
            if(*(probe_buffer+i) != 0){
                zero_flag = false;
            }
        }
        if(zero_flag){
            break;
        }
        offset+=32;
    }
    root->file_size = offset;
    root->attr = ENTRY_ATTR_DIR;
}

void fat32_test_helper_uint2str(char * buffer , uint32_t number){
    if(number>=1000){
        return;
    }
    uint32_t a = number%10;
    uint32_t b = (number%100-a)/10;
    uint32_t c = (number%1000-number%100)/100;
    uint32_t offset = 0;
    if(c!=0){
        *(buffer+offset) = c+48;
        offset++;
    }
    if(b!=0){
        *(buffer+offset) = b+48;
        offset++;
    }
    if(a!=0){
        *(buffer+offset) = a+48;
        offset++;
    }
    if(offset==0){
        buffer[0] = '0';
        buffer[1] = '\0';
    }
    else{
        *(buffer+offset) = '\0';
    }
}

void fat32_test(){
    size_t sec =  _first_sec_in_clus(2);
    entry_cache_t * ec = &entry_cache;
    entry_t * mroot = root;
    block_t * block = block_get_read(32,0);
    entry_t * entry = entry_get_read(root,"1.TXT");
    uint32_t clus_no = _clus_alloc();
    _clus_free(clus_no);
    uint32_t se = fat32.bpb.rsvd_sec_cnt;
}