#ifndef FAT_FILE_READ_H
#define FAT_FILE_READ_H

#include "fat_volume.h"
#include "fat_cluster.h"
#include "fat_file.h"
#include "fat_types.h"
#include <stdlib.h>

void fat_calculate_cluster_position(fat_volume_t *volume, uint32_t position,
                                    uint32_t *cluster_index, uint32_t *cluster_offset);

fat_error_t fat_walk_cluster_chain(fat_volume_t *volume, cluster_t start_cluster, 
                                   uint32_t target_index, cluster_t *result_cluster);

fat_error_t fat_seek_to_position(fat_file_t *file, uint32_t target_position);

fat_error_t fat_read_cluster_data(fat_volume_t *volume, cluster_t cluster, 
                                  uint32_t offset, void *buffer, size_t length);

int fat_read(fat_file_t *file, void *buffer, size_t size);

#endif