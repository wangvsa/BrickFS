#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include "utlist.h"
#include "tangramfs-ucx.h"
#include "tangramfs-ucx-comm.h"

#define NUM_THREADS 8

volatile static bool         g_server_running = true;
static int                   g_mpi_size;
static ucs_async_context_t*  g_server_async;
static tangram_uct_context_t g_server_context;


/* Represents one RPC request */
typedef struct rpc_task {
    uint8_t id;
    void*   respond;
    size_t  respond_len;

    tangram_uct_addr_t client;

    void*   data;

    struct rpc_task *next, *prev;
} rpc_task_t;


/*
 * Each worker maintains a FIFO queue of RPC tasks
 * Server will insert tasks worker's queue in a
 * round-robin manner.
 */
typedef struct rpc_task_worker {
    int tid;
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    rpc_task_t *tasks;
} rpc_task_worker_t;
static rpc_task_worker_t g_workers[NUM_THREADS];
static int who = 0;


void* (*user_am_data_handler)(int8_t, tangram_uct_addr_t* client, void* data, size_t *respond_len);


/**
 * Append a task into one worker's task queue,
 * then notify that worker.
 *
 * uint64_t is the header in am_short();
 * We do not use it for now.
 */
void append_task(uint8_t id, void* buf, size_t buf_len) {
    rpc_task_t *task = malloc(sizeof(rpc_task_t));

    task->id = id;
    task->respond = NULL;
    task->respond_len = 0;
    task->data = NULL;
    unpack_rpc_buffer(buf, buf_len, &task->client, &task->data);

    pthread_mutex_lock(&g_workers[who].lock);
    DL_APPEND(g_workers[who].tasks, task);
    pthread_cond_signal(&g_workers[who].cond);
    pthread_mutex_unlock(&g_workers[who].lock);

    who = (who + 1) % NUM_THREADS;
}

void destroy_task(rpc_task_t* task) {
    if(task->data)
        free(task->data);
    if(task->respond)
        free(task->respond);
    free(task->client.dev);
    free(task->client.iface);
    free(task);
}

static ucs_status_t am_query_listener(void *arg, void *buf, size_t buf_len, unsigned flags) {
    // TODO can directly use the data and return UCS_INPROGRESS
    // then free it later.
    append_task(AM_ID_QUERY_REQUEST, buf, buf_len);
    return UCS_OK;
}
static ucs_status_t am_post_listener(void *arg, void *buf, size_t buf_len, unsigned flags) {
    append_task(AM_ID_POST_REQUEST, buf, buf_len);
    return UCS_OK;
}
static ucs_status_t am_stat_listener(void *arg, void *buf, size_t buf_len, unsigned flags) {
    append_task(AM_ID_STAT_REQUEST, buf, buf_len);
    return UCS_OK;
}

static ucs_status_t am_stop_listener(void *arg, void *buf, size_t buf_len, unsigned flags) {
    g_server_running = false;
    return UCS_OK;
}


// Receive the size of mpi clients and init client addresses
static ucs_status_t am_mpi_size_listener(void *arg, void *data, size_t length, unsigned flags) {
    g_mpi_size = *(uint64_t*) data;
    return UCS_OK;
}

void handle_one_task(rpc_task_t* task) {
    pthread_mutex_lock(&g_server_context.mutex);
    uct_ep_h ep;

    uct_ep_create_connect(g_server_context.iface, &task->client, &ep);
    pthread_mutex_unlock(&g_server_context.mutex);

    task->respond = (*user_am_data_handler)(task->id, &task->client, task->data, &task->respond_len);

    if(task->respond) {
        uint8_t id;
        if(task->id == AM_ID_QUERY_REQUEST)
            id = AM_ID_QUERY_RESPOND;
        if(task->id == AM_ID_POST_REQUEST)
            id = AM_ID_POST_RESPOND;
        if(task->id == AM_ID_STAT_REQUEST)
            id = AM_ID_STAT_RESPOND;
        do_uct_am_short(&g_server_context.mutex, ep, id, &g_server_context.self_addr, task->respond, task->respond_len);
    }

    pthread_mutex_lock(&g_server_context.mutex);
    uct_ep_destroy(ep);
    pthread_mutex_unlock(&g_server_context.mutex);
}

void* rpc_task_worker_func(void* arg) {
    int tid = *((int*)arg);
    rpc_task_worker_t *me = &g_workers[tid];

    while(g_server_running) {

        pthread_mutex_lock(&me->lock);

        // If no task available, go to sleep
        // Server will insert a task and wake us up later.
        if (me->tasks == NULL)
            pthread_cond_wait(&me->cond, &me->lock);

        // Possible get the signal because server stoped
        if (!g_server_running)
            break;

        // FIFO manner
        rpc_task_t *task = me->tasks;
        assert(task != NULL);
        DL_DELETE(me->tasks, task);

        pthread_mutex_unlock(&me->lock);

        handle_one_task(task);
        destroy_task(task);
    }

    // At this point, we should have handled all tasks.
    // i.e., g_workers[tid].tasks should be empty.
    //
    // But it is possible that client stoped the server
    // before all their requests have been finished.
}


void tangram_ucx_server_init(tfs_info_t *tfs_info) {
    ucs_status_t status;
    ucs_async_context_create(UCS_ASYNC_MODE_THREAD_SPINLOCK, &g_server_async);

    tangram_uct_context_init(g_server_async, tfs_info->rpc_dev_name, tfs_info->rpc_tl_name, true, &g_server_context);

    status = uct_iface_set_am_handler(g_server_context.iface, AM_ID_QUERY_REQUEST, am_query_listener, NULL, 0);
    assert(status == UCS_OK);
    status = uct_iface_set_am_handler(g_server_context.iface, AM_ID_POST_REQUEST, am_post_listener, NULL, 0);
    assert(status == UCS_OK);
    status = uct_iface_set_am_handler(g_server_context.iface, AM_ID_STAT_REQUEST, am_stat_listener, NULL, 0);
    assert(status == UCS_OK);
    status = uct_iface_set_am_handler(g_server_context.iface, AM_ID_STOP_REQUEST, am_stop_listener, NULL, 0);
    assert(status == UCS_OK);
    //status = uct_iface_set_am_handler(g_server_context.iface, AM_ID_CLIENT_ADDR, am_client_addr_listener, NULL, 0);
    //assert(status == UCS_OK);
    status = uct_iface_set_am_handler(g_server_context.iface, AM_ID_MPI_SIZE, am_mpi_size_listener, NULL, 0);
    assert(status == UCS_OK);

    for(int i = 0; i < NUM_THREADS; i++) {
        g_workers[i].tid = i;
        g_workers[i].tasks = NULL;
        int err = pthread_mutex_init(&g_workers[i].lock, NULL);
        assert(err == 0);
        pthread_create(&(g_workers[i].thread), NULL, rpc_task_worker_func, &g_workers[i].tid);
    }
}

void tangram_ucx_server_register_rpc(void* (*user_handler)(int8_t, tangram_uct_addr_t*, void*, size_t*)) {
    user_am_data_handler = user_handler;
}


void tangram_ucx_server_start() {

    while(g_server_running) {
        pthread_mutex_lock(&g_server_context.mutex);
        uct_worker_progress(g_server_context.worker);
        pthread_mutex_unlock(&g_server_context.mutex);
    }

    // Server stopped, clean up now
    for(int i = 0; i < NUM_THREADS; i++) {
        pthread_cond_signal(&g_workers[i].cond);
        pthread_join(g_workers[i].thread, NULL);
    }

    tangram_uct_context_destroy(&g_server_context);
    ucs_async_context_destroy(g_server_async);
}
