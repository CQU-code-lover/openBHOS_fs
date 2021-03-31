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

static uint32_t _clus_alloc(){
    block_t *block;
    uint32_t sec = fat32.bpb.rsvd_sec_cnt;
    uint32_t const ent_per_sec = fat32.bpb.byts_per_sec / sizeof(uint32_t);
    for (uint32_t i = 0; i < fat32.bpb.fat_sz; i++, sec++) {
        block = block_get_write(sec,0);
        for (uint32_t j = 0; j < ent_per_sec; j++) {
            if (((uint32_t *)(block->data))[j] == 0) {
                ((uint32_t *)(block->data))[j] = FAT32_FILE_END;
                block_put_write(block);
                uint32_t clus = i * ent_per_sec + j;
                _clus_clear(clus);
                return clus;
            }
        }
        block_put_write(block);
    }
    PANIC("no clusters to alloc!\n");
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
            uint32_t cpy_len = fat32.bpb.byts_per_sec - offset_in_sec;
            if(write){
                block_t * block = block_get_write(probe_sec,0);
                memcpy(block->data+offset_in_sec,buffer + buffer_offset,  cpy_len);
                block_put_write(block);
            }
            else{
                block_t * block = block_get_read(probe_sec,0);
                memcpy(buffer + buffer_offset, block->data+offset_in_sec, cpy_len);
                block_put_read(block);
            }
            buffer_offset+=cpy_len;
            length -= cpy_len;
        }
        else{
            if(write){
                block_t * block = block_get_write(probe_sec,0);
                memcpy( block->data+offset_in_sec,buffer + buffer_offset, length);
                block_put_write(block);
            }
            else{
                block_t * block = block_get_read(probe_sec,0);
                memcpy(buffer + buffer_offset, block->data+offset_in_sec, length);
                block_put_read(block);
            }
        }
    }
}

static bool _multi_clus_rw(uint32_t start_clus_no, void * buffer , uint32_t offset, uint32_t length,bool write){
    // relocate the start_clus_no and start offset
    uint32_t clus_no_offset = offset/fat32.byts_per_clus;
    for(;clus_no_offset>0;clus_no_offset--){
        start_clus_no = _fat_read(start_clus_no);
        if(start_clus_no>=FAT32_VALID_MAX){
            return false;
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
                return false;
            }
        }
        else{
            // is the last clus
            break;
        }
    }
    return true;
}

static inline bool _char_is_upper_or_num(char c){
    return (c>=0x41&&c<=0x5A)||(c>=0x30&&c<=0x39);
}

/*!
 * @note split a name to head and suffix,
 *       the name if the name don`t have
 *       suffix,the name must end with
 *       '.\0'.
 * @param name
 * @param head_buffer
 * @param suffix_buffer
 * @return false when the name is valid and true
 *         when split the name successful.
 */
static bool _full_name_split(const char * name , char * head_buffer ,char * suffix_buffer){
    int head_len = 0;
    int suffix_len = 0;
    int str_len = 0;
    bool have_point = false;
    bool point_get = false;
    int i = 0;
    for(;name[i]!='\0';i++){
        if(name[i]=='.'){
            have_point = true;
        }
    }
    str_len = i;
    if(!have_point){
        return false;
    }
    i--;
    for(;i>=0;i--){
        if(_char_is_upper_or_num(name[i])||(name[i]=='.')){
            if(point_get){
                head_len++;
            }
            else{
                if(name[i]=='.'){
                    point_get=true;
                }
                else{
                    suffix_len++;
                }
            }
        }
        else{
            return false;
        }
    }
    // check if the name length is available.
    if(suffix_len>3||head_len>8){
        return false;
    }
    for(int head_probe = 0;head_probe<8;head_probe++,head_len--){
        if(head_len>0){
            head_buffer[head_probe] = name[head_probe];
        }
        else{
            head_buffer[head_probe] = 0x20;
        }
    }
    for(int suffix_probe = 0;suffix_probe<3;suffix_probe++,suffix_len--){
        if(suffix_len>0){
            suffix_buffer[suffix_probe] = name[str_len-suffix_len];
        }
        else{
            suffix_buffer[suffix_probe] = 0x20;
        }
    }
    return true;
}

static bool _full_name_put_to_data(entry_data_t * entry_data, char * name){
    return _full_name_split(name,entry_data->name_head,entry_data->name_suffix);
}

static void _full_name_get_from_data(entry_data_t * entry_data, char * name){
    memcpy(name, entry_data->name_head, 8);
    int i = 0;
    for(;i<8 && name[i] != 0x20; i++);
    name[i] = '.';
    i++;
    for(int j = 0;j<3 && entry_data->name_suffix[j] != 0x20; i++,j++){
        name[i] = entry_data->name_suffix[j];
    }
    name[i] = '\0';
}

/*!
 * @note load entry to cache.
 * @warning Must Invoking With Holding
 *          entry Write Lock and parent`s
 *          Read or Write Lock.
 * @param entry
 * @param first_clus_no : the entry`s first cluster number.
 */
static bool _entry_load(entry_t * parent, const char * name, entry_t * entry){
    if(parent->attr!=ENTRY_ATTR_DIR){
        PANIC("Parent is not a dir!\n");
    }
    char name_buffer[MAX_FULL_NAME];
    entry_data_t entry_data;
    uint32_t clus_no = parent->first_clus_no;
    uint32_t offset = 0;
    for (;;offset += 32) {
        if (!_multi_clus_rw(clus_no, &entry_data, offset, 32, false)) {
            goto not_find;
        }
        bool all_zero_flag = true;
        for (int i = 0; i < sizeof(entry_data_t); i++) {
            if (((char *) &entry_data)[i] != '\0') {
                all_zero_flag = false;
            }
        }
        if (all_zero_flag) {
            goto not_find;
        }
        _full_name_get_from_data(&entry_data, name_buffer);
        if (strcmp(name_buffer, name) == 0) {
            // check entry is available
            if (entry_data.attr != ENTRY_ATTR_DIR && entry_data.attr != ENTRY_ATTR_ARCHIVE) {
                goto not_find;
            }
            else{
                goto success_get;
            }
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
 * @warning Must Invoking With Holding Entry`s Write
 *          And Parent`s Write Lock.
 * @param entry
 */
static bool _entry_flush(entry_t * entry){
    if(entry->parent==NULL||entry->parent == ROOT_PARENT||(!entry->dirty)){
        return false;
    }
    entry_t * parent = entry->parent;
    entry_data_t data;
    if(!_multi_clus_rw(parent->first_clus_no,&data,entry->offset_in_dir,32,false)){
        return false;
    }

    data.file_size = entry->file_size;
    data.attr = entry->attr;
    data.first_clus_low = entry->first_clus_no<<16>>16;
    data.first_clus_high = entry->first_clus_no>>16;
    if(!_full_name_put_to_data(&data,entry->filename)){
        // filename is invalid
        return false;
    }
    return _multi_clus_rw(parent->first_clus_no,&data,entry->offset_in_dir,32,true);
}

/*!
 * @note write back all of dirty entry in cache to block layer.
 * @warning don`t hold any entry`s lock.
 */
void entry_flush_all(){
    fs_stub_rw_r_lock_acquire(&entry_cache.rw_lock);
    entry_t * probe_entry;
    for(dnode_t * probe = entry_cache.dlink.head;probe!=NULL;probe = probe->next){
        probe_entry = probe->data;
        if(probe_entry->dirty){
            if(probe_entry->parent!=NULL&&probe_entry->parent!=ROOT_PARENT){
                fs_stub_rw_w_lock_acquire(&probe_entry->parent->rw_lock);
                fs_stub_rw_w_lock_acquire(&probe_entry->rw_lock);
                probe_entry->parent->dirty = true;
                _entry_flush(probe_entry);
                fs_stub_rw_w_lock_release(&probe_entry->rw_lock);
                fs_stub_rw_w_lock_release(&probe_entry->parent->rw_lock);
            }
        }
    }
    fs_stub_rw_r_lock_release(&entry_cache.rw_lock);
}

/*!
 * @note get a idle entry with holding it`s write lock.
 *       generally invoking by entry_new.
 * @return idle entry or NULL when idle entry not find.
 */
entry_t * _entry_get_idle_write(){
    entry_t * ret = NULL;
    fs_stub_rw_w_lock_acquire(&entry_cache.rw_lock);
    entry_cache.dirty = true;
    for(dnode_t * probe = entry_cache.dlink.head;probe!=NULL;probe=probe->prev){
        entry_t * entry = probe->data;
        // get entry write lock
        fs_stub_rw_w_lock_acquire(&entry->rw_lock);
        if(entry->ref_cnt == 0){
            // get idle
            if(entry->parent!=NULL){
                fs_stub_rw_w_lock_acquire(&entry->parent->rw_lock);
                entry->parent->dirty = true;
                _entry_flush(entry);
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

static entry_t * _entry_sub_get(entry_t * parent, char * name, bool write){
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
        if((strcmp(entry->filename , name) == 0)&&(entry->parent == parent)){
            // cache hit!
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
            entry_idle->parent->dirty = true;
            _entry_flush(entry_idle);
            entry_idle->parent->ref_cnt--;
            fs_stub_rw_w_lock_release(&entry_idle->parent->rw_lock);
        }
        fs_stub_rw_w_lock_acquire(&parent->rw_lock);
        parent->dirty = true;
        if(!_entry_load(parent, name, entry_idle)){
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

entry_t * entry_get_sub_read(entry_t * parent, char * name){
    return _entry_sub_get(parent, name, false);
};

entry_t *  entry_get_sub_write(entry_t * parent, char * name){
    entry_t * entry = _entry_sub_get(parent, name, true);
    if(entry == NULL){
        return NULL;
    }
    entry->dirty = true;
    return entry;
}

/*!
 * @note get the entry`s read lock and let ref cnt increase.
 * @warning for the target entry,don`t hold any lock before this function invoking.
 * @param entry
 * @return
 */
entry_t * entry_get_read(entry_t * entry){
    fs_stub_rw_w_lock_acquire(&entry->rw_lock);
    entry->ref_cnt++;
    fs_stub_rw_w_lock_release(&entry->rw_lock);
    // if the write lock is release here.the ref cnt is not zero,
    // so the cache of this entry can`t be switch.
    fs_stub_rw_r_lock_acquire(&entry->rw_lock);
    return entry;
}

/*!
 * @note get the entry`s write lock and let ref cnt increase.
 * @warning for the target entry,don`t hold any lock before this function invoking.
 * @param entry
 * @return
 */
void entry_get_write(entry_t * entry){
    fs_stub_rw_w_lock_acquire(&entry->rw_lock);
    entry->ref_cnt++;
    entry->dirty = true;
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

static uint32_t _get_dir_file_size(entry_t * entry){
    ASSERT(entry!=NULL&&entry->attr!=ENTRY_ATTR_ARCHIVE,"entry is not dir!\n");
    char data_buffer[32];
    int off = 0;
    for(;;off+=32){
        bool all_zero_flag = true;
        if(_multi_clus_rw(entry->first_clus_no, data_buffer,off,32,false)){
            for(int i = 0;i<32;i++){
                if(data_buffer[i]!='\0'){
                    all_zero_flag = false;
                }
            }
            if(all_zero_flag){
                break;
            }
        }
        else{
            break;
        }
    }
    return off;
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
    ASSERT(entry!=NULL,"entry is invalid!\n");
    if(entry->first_clus_no == 0){
        // this is a new create file with no cluster allocating.
        // alloc one
        entry->first_clus_no = _clus_alloc();
    }
    uint32_t file_size;
    if(entry->attr==ENTRY_ATTR_ARCHIVE){
        file_size = entry->file_size;
    }
    else{
        file_size = _get_dir_file_size(entry);
    }
    uint32_t clus_cnt = file_size/fat32.byts_per_clus;
    if(file_size%fat32.byts_per_clus!=0){
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
            if(entry->attr == ENTRY_ATTR_ARCHIVE){
                entry->file_size = file_now_size;
            }
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
 * @note create a entry and hold it`s write lock.
 * @warning must hold parent write lock
 * @param parent
 * @param name
 * @param attr
 * @return new entry with write lock or NULL when fail to create entry.
 */
entry_t *entry_create_write(entry_t * parent , char * name , uint8_t attr){
    ASSERT(parent!=NULL&&parent->attr == ENTRY_ATTR_DIR&&strlen(name)<MAX_FULL_NAME ,"Parent Dir is Not Dir!\n");
    ASSERT(attr==ENTRY_ATTR_DIR||attr==ENTRY_ATTR_ARCHIVE,"Unexpected attr when create entry!\n");
    entry_t * tmp;
    if((tmp= entry_get_sub_read(parent, name)) != NULL){
        // this entry is exist
        entry_put_read(tmp);
        return NULL;
    }
    entry_t * idle = _entry_get_idle_write();
    idle->parent = parent;
    strcpy(idle->filename,name);
    entry_data_t new_entry_data;
    // write a empty entry to parent.
    // the parent file size will change
    entry_rw(parent,&new_entry_data,parent->file_size,sizeof(entry_data_t),true);
    idle->offset_in_dir = parent->file_size-32;
    idle->parent->ref_cnt++;
    if(attr==ENTRY_ATTR_ARCHIVE){
        idle->first_clus_no = 0;
        idle->file_size = 0;
    }
    else{
        idle->first_clus_no = _clus_alloc();
        idle->file_size = 32*2;
        // add entry "." and ".."
        entry_data_t buffer[2]={
                {
                        ".",
                        "",
                        ENTRY_ATTR_DIR,
                        0,
                        0,
                        0,
                        0,
                        0,
                        idle->first_clus_no>>16,
                        0,
                        0,
                        idle->first_clus_no<<16>>16,
                        idle->file_size
                },
                {
                        "..",
                        "",
                        ENTRY_ATTR_DIR,
                        0,
                        0,
                        0,
                        0,
                        0,
                        idle->parent->first_clus_no>>16,
                        0,
                        0,
                        idle->parent->first_clus_no<<16>>16,
                        idle->parent->file_size
                }
        };
        entry_rw(idle,buffer,0,sizeof(entry_data_t)*2,true);
    }
    return idle;
}


/*!
 * @warning must hold parent write lock.And check if the entry is idle.
 * @param parent
 * @param name
 * @return
 */
bool entry_rm_sub(entry_t * parent, char * name){
    // get target entry with write lock.
    // if this entry`s sub entry not use and not in cache
    // the entry`s ref cnt will be 1.
    entry_t * entry = entry_get_sub_write(parent, name);
    if(entry==NULL){
        return false;
    }
    if(entry->ref_cnt!=1){
        entry_put_write(entry);
        return false;
    }
    // target entry can remove
    parent->ref_cnt--;
    uint8_t buffer = 0xE5;
    entry_rw(parent,&buffer,entry->offset_in_dir,1,true);
    // set entry`s parent to NULL,so can`t get from cache by parent and name,
    // and can`t flush back to block layer automatically.
    entry->parent = NULL;
    entry_put_write(entry);
    return true;
};

void entry_ls(entry_t * parent,void * buffer){
    ASSERT(parent->attr==ENTRY_ATTR_DIR,"this entry is not a dir!\n");
}

entry_t * _parse_path(const char * path , bool write){
    entry_t * parent = entry_get_read(root);
    if(path[0]!='/'){
        return NULL;
    }
    char buffer[13];
    int last_index = 0;
    int i=0;
    bool end_flag = false;
    entry_t * sub;
    for(;;i++){
        if(path[i]=='/'||path[i]=='\0'){
            if(path[i]=='\0'){
                end_flag = true;
            }
            if(i>=last_index+2){
                bool have_point = false;
                for(int j = last_index+1;j<i;j++){
                    if(path[j]=='.'){
                        have_point = true;
                    }
                }
                if(have_point){
                    if((i-last_index-1)>12){
                        return NULL;
                    }
                    else{
                        strncpy(buffer,path+last_index+1,i-last_index-1);
                        buffer[i-last_index-1] = '\0';
                    }
                }
                else{
                    if((i-last_index-1)>11){
                        return NULL;
                    }
                    else{
                        strncpy(buffer,path+last_index+1,i-last_index-1);
                        buffer[i-last_index-1] = '.';
                        buffer[i-last_index] = '\0';
                    }
                }
                if(end_flag){
                    if(write){
                        sub = entry_get_sub_write(parent,buffer);
                    }
                    else{
                        sub = entry_get_sub_read(parent,buffer);
                    }
                    return sub;
                }
                else{
                    sub = entry_get_sub_read(parent,buffer);
                }
                entry_put_read(parent);
                if(sub==NULL){
                    return NULL;
                }
                else{
                    parent = sub;
                }
            }
            last_index = i;
        }
    }
}

entry_t * parse_path_read(const char * path){
    return _parse_path(path,false);
}

entry_t * parse_path_write(const char * path){
    return _parse_path(path,true);
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
    entry_t * entry1 = parse_path_write("/A/1.TXT");
    char s_buffer[100]={[0 ... 99]='T'};
    char r_buffer[100]={[0 ... 99]='F'};
    entry_rw(entry1,s_buffer,0,100,true);
    entry_rw(entry1,r_buffer,0,100,false);
    entry_put_write(entry1);
    entry_flush_all();
    block_flush_all();
}
