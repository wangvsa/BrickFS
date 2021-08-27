#ifndef _TANGRAMFS_METADATA_H_
#define _TANGRAMFS_METADATA_H_
#include <stdbool.h>
#include <sys/stat.h>

void tangram_ms_init();
void tangram_ms_finalize();
void tangram_ms_handle_post(int rank, char* filename, size_t offset, size_t count);
bool tangram_ms_handle_query(char* filename, size_t offset, size_t count, int *rank);
void tangram_ms_handle_stat(char* path, struct stat* buf);

#endif
