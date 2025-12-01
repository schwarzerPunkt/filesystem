#ifndef FAT_FILE_H
#define FAT_FILE_H

#include "fat_types.h"
#include "fat_volume.h"
#include "fat_dir.h"

typedef struct {
    fat_volume_t *volume;
    fat_dir_entry_t dir_entry;
    cluster_t current_cluster;
    uint32_t position;
    cluster_t dir_cluster;
    uint32_t dir_entry_offset;
    int flags;
    bool modified;
    uint32_t cluster_offset;
} fat_file_t;

fat_error_t fat_open(fat_volume_t *volume, const char *path, int flags, fat_file_t **file);

fat_error_t fat_close(fat_file_t *file);

fat_error_t fat_init_file_handle(fat_file_t *file, fat_volume_t *volume, 
                                 const fat_dir_entry_t *dir_entry, cluster_t dir_cluster,
                                uint32_t dir_entry_offset, int flags);

bool fat_validate_open_flags(int flags, const fat_dir_entry_t *entry);

void fat_update_file_timestamps(fat_dir_entry_t *entry);

#endif