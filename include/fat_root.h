#ifndef FAT_ROOT_H
#define FAT_ROOT_H

#include <stdint.h>
#include "fat_types.h"
#include "fat_volume.h"
#include "fat_dir.h"

cluster_t fat_get_root_dir_cluster(fat_volume_t *volume);

fat_error_t fat_read_root_dir_fat12(fat_volume_t *volume, fat_dir_entry_t **entries, 
                                    uint32_t *count);

fat_error_t fat_read_root_dir_fat32(fat_volume_t *volume, fat_dir_entry_t **entries,
                                    uint32_t *count);

uint32_t fat_cluster_to_sector(fat_volume_t *volume, cluster_t cluster);

#endif