#ifndef FAT_FILE_WRITE_H
#define FAT_FILE_WRITE_H

#include "fat_file.h"
#include "fat_cluster.h"
#include "fat_table.h"
#include <stdlib.h>

uint32_t fat_calculate_clusters_needed(fat_volume_t *volume, uint32_t file_size);

fat_error_t fat_find_last_cluster(fat_volume_t *volume, cluster_t start_cluster, 
                                  cluster_t *last_cluster);

fat_error_t fat_allocate_and_link_cluster(fat_volume_t *volume, cluster_t prev_cluster, 
                                          cluster_t *new_cluster);

fat_error_t fat_extend_file(fat_file_t *file, uint32_t new_size);

fat_error_t fat_write_cluster_data(fat_volume_t *volume, cluster_t cluster, 
                                   uint32_t offset, const void *buffer, size_t length);

int fat_write(fat_file_t *file, const void *buffer, size_t size);

#endif