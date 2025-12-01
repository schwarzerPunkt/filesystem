#ifndef FAT_DIR_SEARCH_H
#define FAT_DIR_SEARCH_H

#include "fat_types.h"
#include "fat_volume.h"
#include "fat_dir.h"

typedef fat_error_t (*fat_dir_iterator_callback)(
    const fat_dir_entry_t *entry,
    const char *long_name,
    uint32_t entry_index,
    void *user_data
);

fat_error_t fat_find_entry (fat_volume_t *volume, 
                            cluster_t dir_cluster, 
                            const char *name, 
                            fat_dir_entry_t *entry, 
                            uint32_t *entry_index);

fat_error_t fat_iterate_directory(fat_volume_t *volume,
                                  cluster_t dir_cluster,
                                  fat_dir_iterator_callback callback,
                                  void *user_data);

fat_error_t fat_find_free_entry(fat_volume_t *volume,
                                cluster_t dir_cluster,
                                uint32_t num_entries,
                                uint32_t *entry_index);
    
bool fat_compare_short_name(const uint8_t *short_name, const char *filename);

#endif