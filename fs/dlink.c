//
// Created by davis on 2021/3/15.
//


#include "fs_common.h"


//
// Created by davis on 2021/2/28.
//

void dlink_init(dlink_t * dlink){
    dlink->size = 0;
    dlink->head = NULL;
    dlink->tail = NULL;
}


void dlink_add_tail(dlink_t * dlink, dnode_t * dnode){
    //must clear 'next'
    dnode->next = NULL;
    if(dlink->size == 0){
        dlink->tail = dnode;
        dlink->head = dnode;
        dnode->prev = NULL;
    }
    else{
        dlink->tail->next = dnode;
        dnode->prev = dlink->tail;
        dlink->tail = dnode;
    }
    dlink->size++;
}

void dlink_add_head(dlink_t * dlink, dnode_t * dnode){
    dnode->prev = NULL;
    if(dlink->size == 0){
        dlink->tail = dnode;
        dlink->head = dnode;
        dnode->prev = NULL;
    }
    else{
        dnode->next = dlink->head;
        dlink->head->prev = dnode;
        dlink->head = dnode;
    }
    dlink->size++;
}

dnode_t * dlink_find_dnode_by_data(dlink_t * dlink, node_data_t data){
    dnode_t * ret;
    if(dlink->size == 0){
        ret = NULL;
    }
    else{
        for(dnode_t  * probe = dlink->head; probe != NULL; probe=probe->next){
            if (probe->data == data){
                ret = probe;
                goto out;
            }
        }
        ret = NULL;
    }
    out:
    return ret;
}


/*!
 * @param dnode   can`t be NULL otherwise will raise assert.
 * @return
 */
dnode_t * dnode_remove_self(dnode_t * dnode){
    ASSERT(dnode!=NULL,"panic!\n");
    if(dnode->prev != NULL){
        dnode->prev->next = dnode->next;
    }
    if(dnode->next != NULL){
        dnode->next->prev = dnode->prev;
    }
    return dnode;
}

dnode_t * dlink_remove_by_data(dlink_t * dlink, node_data_t data){
    dnode_t * ret;
    if(dlink->size == 0){
        ret = NULL;
    }
    else{
        for(dnode_t * probe = dlink->head;probe!=NULL;probe=probe->next){
            if(probe->data == data){
                // get target dnode
                dnode_remove_self(probe);
            }
        }
    }
    out:
    return ret;
}


void dlink_test(){

}


