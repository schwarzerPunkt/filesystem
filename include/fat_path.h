#ifndef FAT_PATH_H
#define FAT_PATH_H

#include "fat_types.h"
#include "fat_volume.h"
#include "fat_dir.h"

fat_error_t fat_split_path(const char *path, char ***components, uint32_t *num_components);

void fat_free_path_components(char **components, uint32_t num_components);

fat_error_t fat_resolve_path(fat_volume_t *volume, const char *path, fat_dir_entry_t *entry, 
                             cluster_t *parent_cluster, uint32_t *entry_index);

fat_error_t fat_find_in_directory(fat_volume_t *volume, cluster_t dir_cluster, 
                                  const char *component, fat_dir_entry_t *entry, 
                                  uint32_t *entry_index);

bool fat_validate_component(const char *component);

#endif