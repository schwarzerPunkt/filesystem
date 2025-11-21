#ifndef FAT_CLUSTER_H
#define FAT_CLUSTER_H

#include <stdint.h>
#include "fat_types.h"
#include "fat_volume.h"

fat_error_t fat_get_next_cluster(fat_volume_t *volume, cluster_t cluster, cluster_t *next_cluster);
bool fat_is_eoc(fat_volume_t *volume, uint32_t value);
bool fat_is_bad(fat_volume_t *volume, uint32_t value);
fat_error_t fat_allocate_cluster(fat_volume_t *volume, cluster_t *cluster);
fat_error_t fat_free_chain(fat_volume_t *volume, cluster_t start_cluster);
fat_error_t fat_validate_chain(fat_volume_t *volume, cluster_t start_cluster);

#endif
