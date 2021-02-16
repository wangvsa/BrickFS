#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <mercury.h>
#include "mpi.h"
#include "tangramfs-meta.h"

static hg_class_t*     hg_class   = NULL; /* the mercury class */
static hg_context_t*   hg_context = NULL; /* the mercury context */

static const int TOTAL_RPCS = 3;
static int num_rpcs = 0;


hg_return_t hello_world(hg_handle_t h);

int tfs_meta_start_server() {
    hg_return_t ret;

    hg_class = HG_Init("mpi+static", HG_TRUE);
    assert(hg_class != NULL);

    char hostname[128] = {0};
    hg_size_t hostname_size;
    hg_addr_t self_addr;
    HG_Addr_self(hg_class, &self_addr);
    HG_Addr_to_string(hg_class, hostname, &hostname_size, self_addr);
    printf("Server running at address %s\n",hostname);
    HG_Addr_free(hg_class, self_addr);

    hg_context = HG_Context_create(hg_class);
    assert(hg_context != NULL);

    /* Register the RPC by its name ("hello").
     * The two NULL arguments correspond to the functions user to
     * serialize/deserialize the input and output parameters
     * (hello_world doesn't have parameters and doesn't return anything, hence NULL).
     */
    hg_id_t rpc_id = HG_Register_name(hg_class, "hello", NULL, NULL, hello_world);


    /* We call this function to tell Mercury that hello_world will not
     * send any response back to the client.
     */
    HG_Registered_disable_response(hg_class, rpc_id, HG_TRUE);

    do
    {
        unsigned int count;
        do {
            ret = HG_Trigger(hg_context, 0, 1, &count);
        } while((ret == HG_SUCCESS) && count);
        HG_Progress(hg_context, 100);
    } while(num_rpcs < TOTAL_RPCS);
    /* Exit the loop if we have reached the given number of RPCs. */


    ret = HG_Context_destroy(hg_context);
    assert(ret == HG_SUCCESS);

    ret = HG_Finalize(hg_class);
    assert(ret == HG_SUCCESS);

    return 0;
}

hg_return_t hello_world(hg_handle_t h)
{
    hg_return_t ret;

    printf("Hello World!\n");
    num_rpcs += 1;

    /* We are not going to use the handle anymore, so we should destroy it. */
    ret = HG_Destroy(h);
    assert(ret == HG_SUCCESS);
    return HG_SUCCESS;
}
