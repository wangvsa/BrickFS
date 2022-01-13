#define _GNU_SOURCE

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <mpi.h>
#include "tangramfs-rpc.h"

static double rma_time;


/*
 * Perform RPC.
 * The underlying implementaiton is in src/ucx/tangram-ucx-client.c
 */
void tangram_issue_rpc(uint8_t id, char* filename, tangram_uct_addr_t* dest,
                            size_t *offsets, size_t *counts, int num_intervals, void** respond_ptr) {

    size_t data_size;
    void* user_data = rpc_in_pack(filename, num_intervals, offsets, counts, &data_size);

    switch(id) {
        case AM_ID_POST_REQUEST:
            tangram_ucx_sendrecv_server(id, user_data, data_size, respond_ptr);
            break;
        case AM_ID_QUERY_REQUEST:
            tangram_ucx_sendrecv_server(id, user_data, data_size, respond_ptr);
            break;
        default:
            break;
    }

    free(user_data);
}

/*
 * Perform RPC.
 * The underlying implementaiton is in src/ucx/tangram-ucx-client.c
 */
void tangram_issue_rma(uint8_t id, char* filename, tangram_uct_addr_t* dest,
                            size_t *offsets, size_t *counts, int num_intervals, void* recv_buf) {

    size_t data_size;
    void* user_data = rpc_in_pack(filename, num_intervals, offsets, counts, &data_size);

    size_t total_recv_size = 0;
    for(int i = 0; i < num_intervals; i++)
        total_recv_size += counts[i];

    tangram_ucx_rma_request(dest, user_data, data_size, recv_buf, total_recv_size);
}

void tangram_issue_metadata_rpc(uint8_t id, const char* path, void** respond_ptr) {
    void* data = (void*) path;
    switch(id) {
        case AM_ID_STAT_REQUEST:
            tangram_ucx_sendrecv_server(id, data, 1+strlen(path), respond_ptr);
            break;
        default:
            break;
    }
}

void tangram_rpc_service_start(tfs_info_t *tfs_info){
    tangram_ucx_rpc_service_start(tfs_info);
}

void tangram_rpc_service_stop() {
    tangram_ucx_rpc_service_stop();
}

tangram_uct_addr_t* tangram_rpc_get_client_addr() {
    return tangram_ucx_get_client_addr();
}

void tangram_rma_service_start(tfs_info_t *tfs_info, void* (*serve_rma_data)(void*, size_t*)) {
    tangram_ucx_rma_service_start(tfs_info, serve_rma_data);
}

void tangram_rma_service_stop() {
    tangram_ucx_rma_service_stop();
    //tangram_debug("Total rma time: %.3f\n", rma_time);
}

