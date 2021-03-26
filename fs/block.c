//
// Created by davis on 2021/3/15.
//

#include "fs_common.h"
#include "block.h"
#include "string.h"

static block_cache_t block_cache;

static inline void _block_init(block_t * block , int dev_no){
    block->block_no = BLOCK_NO_ERROR;
    fs_stub_rw_lock_init(&block->rw_lock);
    block->dirty = false;
    block->dev_no = dev_no;
    block->dnode.data = block;
}

/*!
 * @note LRU.
 * @param block
 */
static inline void _block_move_to_head(dlink_t *dlink,block_t * block){
    dlink_add_head(&block_cache.dlink, dlink_remove_dnode_unsafe(dlink,&block->dnode));
}

/*!
 * @note: flush no check.
 *        must invoked with holding
 *        block`s read lock
 */
static inline void _block_flush_no_check(block_t * block){
    if(block->block_no != BLOCK_NO_ERROR){
        fs_stub_source_write(block);
        block->dirty = false;
    }
}


static inline block_t * _block_get(uint32_t block_no , int dev_no , bool write){
    // search in cache
    fs_stub_rw_w_lock_acquire(&block_cache.rw_lock);
    dnode_t * probe = block_cache.dlink.head;
    for(;probe!=NULL;probe=probe->next){
        block_t * block_probe = (block_t *)probe->data;
        fs_stub_rw_w_lock_acquire(&block_probe->rw_lock);
        if(block_probe->block_no == block_no&&block_probe->dev_no == dev_no){
            // cache hit!
            // move to head
            if(block_cache.dlink.head!=&block_probe->dnode){
                _block_move_to_head(&block_cache.dlink,block_probe);
            }
            if(!write){
                // block cache must lock when change w_lock to r_lock.
                // otherwise,the target block will recycle probably.
                fs_stub_rw_w_lock_release(&block_probe->rw_lock);
                fs_stub_rw_r_lock_acquire(&block_probe->rw_lock);
            }
            fs_stub_rw_w_lock_release(&block_cache.rw_lock);
            return block_probe;
        }
    }
    // no hit
    // load in device
    // LRU
    block_t * block_tail= block_cache.dlink.tail->data;
    fs_stub_rw_w_lock_acquire(&block_tail->rw_lock);
    if(block_tail->block_no!=BLOCK_NO_ERROR){
        // write back this
        block_flush(block_tail);
    }
    if(&block_tail->dnode!=block_cache.dlink.head){
        _block_move_to_head(&block_cache.dlink,block_tail);
    }
    block_tail->dev_no = dev_no;
    block_tail->block_no = block_no;
    block_tail->dirty = false;
    fs_stub_source_read(block_tail);
    if(!write){
        fs_stub_rw_w_lock_release(&block_tail->rw_lock);
        fs_stub_rw_r_lock_acquire(&block_tail->rw_lock);
    }
    fs_stub_rw_w_lock_release(&block_cache.rw_lock);
    return block_tail;
}

/*!
 * @note: copy block`s cache data to disk.
 *        which will check block`s dirty.
 *        must invoked with holding
 *        block`s read lock
 */
void block_flush(block_t * block){
    if(!block->dirty){
        goto out;
    }
    else{
        _block_flush_no_check(block);
    }
    out:
    return;
}

void block_flush_all(){
    fs_stub_rw_r_lock_acquire(&block_cache.rw_lock);
    for(dnode_t * probe = block_cache.dlink.head;probe!=NULL;probe = probe->next){
        block_t * block_probe = probe->data;
        fs_stub_rw_r_lock_acquire(&block_probe->rw_lock);
        block_flush(block_probe);
        fs_stub_rw_r_lock_release(&block_probe->rw_lock);
    }

    fs_stub_rw_r_lock_release(&block_cache.rw_lock);
}

void block_module_init(int dev_no){
    fs_stub_source_init();
    block_cache.dirty = false;
    // clear cache
    bzero(&block_cache, sizeof(block_cache_t));
    fs_stub_rw_lock_init(&block_cache.rw_lock);
    for(int i =0;i<CONFIG_FS_BLOCK_CACHE_CNT;i++){
        _block_init(&block_cache.buffer[i], dev_no);
        dlink_add_tail(&block_cache.dlink,&block_cache.buffer[i].dnode);
    }
}


block_t * block_get_read(uint32_t block_no , int dev_no){
    return _block_get(block_no,dev_no,false);
}

block_t * block_get_write(uint32_t block_no , int dev_no){
    block_t * ret =  _block_get(block_no,dev_no,true);
    ret->dirty = true;
    return ret;
}

void block_put_read(block_t * block){
    fs_stub_rw_r_lock_release(&block->rw_lock);
}

void block_put_write(block_t * block){
    fs_stub_rw_w_lock_release(&block->rw_lock);
}

void block_put_write_with_flush(block_t * block){
    _block_flush_no_check(block);
    fs_stub_rw_w_lock_release(&block->rw_lock);
}
