#include <stdio.h>
#include <assert.h>
#include "utlist.h"
#include "uthash.h"
#include "lock-token.h"
#include "tangramfs-utils.h"
#include "tangramfs-lock-manager.h"
#include "tangramfs-ucx-server.h"
#include "tangramfs-ucx-delegator.h"

void split_lock() {
}


lock_token_t* tangram_lockmgr_delegator_acquire_lock(lock_table_t** lt, tangram_uct_addr_t* client, char* filename, size_t offset, size_t count, int type) {

    lock_table_t* entry = NULL;
    HASH_FIND_STR(*lt, filename, entry);

    if(!entry) {
        entry = malloc(sizeof(lock_table_t));
        lock_token_list_init(&entry->token_list);
        strcpy(entry->filename, filename);
        HASH_ADD_STR(*lt, filename, entry);
    }

    lock_token_t* token;

    // already hold the lock - 2 cases
    token = lock_token_find_cover(&entry->token_list, offset, count);
    if(token) {
        // Case 1:
        // Had the read lock but ask for a write lock
        // Delete my lock token locally and
        // ask the server to upgrade my lock
        // use the same AM_ID_ACQUIRE_LOCK_REQUEST RPC
        if(token->type != type && type == LOCK_TYPE_WR) {
            lock_token_delete(&entry->token_list, token);
        } else {
        // Case 2:
        // Had the write lock alraedy, nothing to do.
            return token;
        }
    }

    // Do not have the lock, ask lock server for it
    void* out;
    size_t in_size;
    void* in = rpc_in_pack(filename, 1, &offset, &count, &type, &in_size);
    tangram_ucx_delegator_sendrecv_server(AM_ID_ACQUIRE_LOCK_REQUEST, in, in_size, &out);
    token = lock_token_add_from_buf(&entry->token_list, out, client);
    free(in);
    free(out);
    return token;
}

void tangram_lockmgr_delegator_revoke_lock(lock_table_t* lt, char* filename, size_t offset, size_t count, int type) {

    lock_table_t* entry = NULL;
    HASH_FIND_STR(lt, filename, entry);

    if(!entry) return;

    lock_token_t* token;
    token = lock_token_find_cover(&entry->token_list, offset, count);

    // the if(token) should be uncessary, we should be certain
    // that we hold the lock that server wants to revoke
    if(token)
        lock_token_delete(&entry->token_list, token);
}

lock_token_t* tangram_lockmgr_server_acquire_lock(lock_table_t** lt, tangram_uct_addr_t* delegator, char* filename, size_t offset, size_t count, int type) {

    lock_table_t* entry = NULL;
    HASH_FIND_STR(*lt, filename, entry);

    if(!entry) {
        entry = malloc(sizeof(lock_table_t));
        lock_token_list_init(&entry->token_list);
        strcpy(entry->filename, filename);
        HASH_ADD_STR(*lt, filename, entry);
    }

    lock_token_t* token = NULL;

    // First see if the requestor already hold the lock
    // but simply ask to upgrade it, i.e., RD->WR
    token = lock_token_find_cover(&entry->token_list, offset, count);
    if( token && (tangram_uct_addr_compare(token->owner, delegator) == 0) ) {
        if(type == LOCK_TYPE_WR)
            lock_token_update_type(token, type);
        return token;
    }

    token = lock_token_find_conflict(&entry->token_list, offset, count);

    // No one has the lock for the range yet
    // We can safely grant the lock
    //
    // Two implementations:
    // 1. Grant the lock range as asked
    // 2. We can try to extend the lock range
    //    e.g., user asks for [0, 100], we can give [0, infinity]
    if(!token) {
        //token = lock_token_add(&entry->token_list, offset, count, type, delegator);
        token = lock_token_add_extend(&entry->token_list, offset, count, type, delegator);
        return token;
    }

    // Someone has already held the lock

    // Case 1. Both are read locks
    if(type == token->type && type == LOCK_TYPE_RD) {

    // Case 2. Different lock type, split the current owner's lock
    // the requestor is responsible for contatcing the onwer
    //
    // TODO we don't consider the case where we have multiple conflicting owners.
    // e.g. P1:[0-10], P2:[10-20], Accquire[0-20]
    } else {
        /*
        size_t data_len;
        void* data = rpc_in_pack(filename, 1, &offset, &count, NULL, &data_len);
        tangram_ucx_server_revoke_delegator_lock(token->owner, data, data_len);
        free(data);
        lock_token_delete(&entry->token_list, token);
        */
    }

    token = lock_token_add(&entry->token_list, offset, count, type, delegator);
    return token;
}

void tangram_lockmgr_server_release_lock(lock_table_t* lt, tangram_uct_addr_t* client, char* filename, size_t offset, size_t count) {

    lock_table_t* entry = NULL;
    HASH_FIND_STR(lt, filename, entry);

    if(!entry) return;

    lock_token_t* token = NULL;
    token = lock_token_find_cover(&entry->token_list, offset, count);

    if(token && tangram_uct_addr_compare(token->owner, client) == 0)
        lock_token_delete(&entry->token_list, token);
}

void tangram_lockmgr_server_release_lock_file(lock_table_t* lt, tangram_uct_addr_t* client, char* filename) {
    lock_table_t* entry = NULL;
    HASH_FIND_STR(lt, filename, entry);

    if(entry) {
        lock_token_delete_client(&entry->token_list, client);
    }
}

void tangram_lockmgr_server_release_lock_client(lock_table_t* lt, tangram_uct_addr_t* client) {
    lock_table_t *entry, *tmp;
    HASH_ITER(hh, lt, entry, tmp) {
        lock_token_delete_client(&entry->token_list, client);
    }
}


void tangram_lockmgr_init(lock_table_t** lt) {
    *lt = NULL;
}

void tangram_lockmgr_finalize(lock_table_t** lt) {
    lock_table_t *entry, *tmp;
    HASH_ITER(hh, *lt, entry, tmp) {
        lock_token_list_destroy(&entry->token_list);
        HASH_DEL(*lt, entry);
        free(entry);
    }
    *lt = NULL;
}

