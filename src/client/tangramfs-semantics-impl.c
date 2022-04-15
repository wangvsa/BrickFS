#include <stdio.h>
#include "tangramfs.h"

size_t tangram_write_impl(tfs_file_t* tf, const void* buf, size_t count) {
    int semantics = tangram_get_semantics();

    if(semantics == TANGRAM_STRONG_SEMANTICS) {
        tfs_acquire_lock(tf, tf->offset, count, LOCK_TYPE_WR);
    }

    size_t res = tfs_write(tf, buf, count);

    return res;
}

size_t tangram_read_impl(tfs_file_t *tf, void* buf, size_t count) {
    int semantics = tangram_get_semantics();

    if(semantics == TANGRAM_STRONG_SEMANTICS) {
        tfs_acquire_lock(tf, tf->offset, count, LOCK_TYPE_RD);
    }

    // All three semantics use tfs_read()
    return tfs_read(tf, buf, count);
}

int tangram_commit_impl(tfs_file_t* tf) {
    int semantics = tangram_get_semantics();
    if(semantics == TANGRAM_COMMIT_SEMANTICS) {
        tfs_post_file(tf);
    }

    // TODO return value of fsync?
    return 0;
}


int tangram_close_impl(tfs_file_t *tf) {
    // For all three semantics
    int semantics = tangram_get_semantics();
    if (semantics != TANGRAM_STRONG_SEMANTICS)
        tfs_post_file(tf);
    return tfs_close(tf);
}
