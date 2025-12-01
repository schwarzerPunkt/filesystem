#include "fat_file.h"
#include "fat_path.h"
#include "fat_dir_search.h"
#include "fat_root.h"
#include "fat_cluster.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

bool fat_validate_open_flags(int flags, const fat_dir_entry_t *entry){

    int access_mode = flags & (FAT_O_RDONLY | FAT_O_WRONLY | FAT_O_RDWR);
    if(access_mode == (FAT_O_RDONLY | FAT_O_WRONLY)){
        return false;
    }

    if (access_mode == 0){
        return false;
    }

    if(entry){
        if(entry->attr & FAT_ATTR_DIRECTORY){
            return false;
        }

        if((entry->attr & FAT_ATTR_READ_ONLY) && (flags & (FAT_O_WRONLY | FAT_O_RDWR))){
            return false;
        }

    }

    if((flags & FAT_O_CREATE) && !(flags & (FAT_O_WRONLY | FAT_O_RDWR))){
        return false;
    }

    return true;
}

void fat_update_file_timestamps(fat_dir_entry_t *entry){
    if(!entry){
        return;
    }

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    uint16_t fat_time = ((tm_info->tm_hour & 0x1F) <<  11) |
                        ((tm_info->tm_min & 0x3F) << 5) |
                        ((tm_info->tm_sec / 2) & 0x1F);
    uint16_t fat_date = (((tm_info->tm_year - 80) & 0x7F) << 9) |
                        (((tm_info->tm_mon + 1) & 0x0F) << 5) |
                        (tm_info->tm_mday & 0x1F);
    entry->write_time = fat_time;
    entry->write_date = fat_date;
}

fat_error_t fat_init_file_handle(fat_file_t *file, fat_volume_t *volume,
                                 const fat_dir_entry_t *dir_entry, cluster_t dir_cluster,
                                 uint32_t dir_entry_offset, int flags){
    
    // parameter validation
    if(!file || !volume || !dir_entry){
        return FAT_ERR_INVALID_PARAM;
    }

    memset(file, 0, sizeof(fat_file_t));

    file->volume = volume;
    memcpy(&file->dir_entry, dir_entry, sizeof(fat_dir_entry_t));
    file->dir_cluster = dir_cluster;
    file->dir_entry_offset = dir_entry_offset;
    file->flags = flags;
    file->modified = false;

    file->position = 0;
    file->cluster_offset = 0;

    file->current_cluster = fat_get_entry_cluster(volume, dir_entry);

    // handle truncation
    if(flags & FAT_O_TRUNC){
        // TODO implement file truncation - free all clusters and set size to 0
        file->dir_entry.file_size = 0;
        file->modified = true;
    }

    return FAT_OK;
}

fat_error_t fat_open(fat_volume_t *volume, const char *path, int flags, fat_file_t **file){

    // parameter validation
    if(!volume || !path || !file){
        return FAT_ERR_INVALID_PARAM;
    }

    *file = NULL;

    // flag validation
    if(!fat_validate_open_flags(flags, NULL)){
        return FAT_ERR_INVALID_PARAM;
    }

    fat_dir_entry_t dir_entry;
    cluster_t parent_cluster;
    uint32_t entry_index;

    fat_error_t err = fat_resolve_path(volume, path, &dir_entry, &parent_cluster, 
                                       &entry_index);
    if(err == FAT_ERR_NOT_FOUND){
        
        // file does not exist
        if(flags & FAT_O_CREATE){
            /* TODO implement file creation 
                - creating directory entry
                - allocating initial cluster
                - writing directory entry to parent directory
            */
            return FAT_ERR_NOT_FOUND;
        } else {
            return FAT_ERR_NOT_FOUND;
        }
    } else if(err != FAT_OK){
        // other erro, e.g. device, corruption, ...
        return err;
    }

    // file exists
    if(!fat_validate_open_flags(flags, &dir_entry)){
        return FAT_ERR_INVALID_PARAM;
    }

    fat_file_t *new_file = malloc(sizeof(fat_file_t));
    if(!new_file){
        return FAT_ERR_NO_MEMORY;
    }

    err = fat_init_file_handle(new_file, volume, &dir_entry, parent_cluster,
                               entry_index, flags);

    if(err != FAT_OK){
        free(new_file);
        return err;
    }

    *file = new_file;
    return FAT_OK;
}

fat_error_t fat_close(fat_file_t *file){
    if(!file){
        return FAT_ERR_INVALID_PARAM;
    }

    fat_error_t result = FAT_OK;

    if(file->modified){
        fat_update_file_timestamps(&file->dir_entry);
        
        uint32_t sector;
        uint32_t offset;

        if(file->dir_cluster == 0 && file->volume->type != FAT_TYPE_FAT32){
            
            // FAT12/16 root
            uint32_t entries_per_sector = file->volume->bytes_per_sector / 32;
            uint32_t root_start = file->volume->reserved_sector_count + 
                                (file->volume->num_fats * file->volume->fat_size_sectors);
            sector = root_start + (file->dir_entry_offset / entries_per_sector);
            offset = (file->dir_entry_offset % entries_per_sector) * 32;
        } else {

            // FAT32 or subdirectory
            uint32_t entries_per_cluster = file->volume->bytes_per_cluster / 32;
            uint32_t cluster_index = file->dir_entry_offset / entries_per_cluster;

            // walk cluster chain
            cluster_t target_cluster = file->dir_cluster;
            for(uint32_t i = 0; i<cluster_index; i++){
                fat_error_t err = fat_get_next_cluster (file->volume, target_cluster, 
                                                        &target_cluster);
                if(err != FAT_OK){
                    result = err;
                    goto cleanup;
                }
            }

            sector = fat_cluster_to_sector(file->volume, target_cluster);
            offset = (file->dir_entry_offset % entries_per_cluster) * 32;
        }

        fat_error_t err = fat_write_dir_entry(file->volume, sector, offset, 
                                              &file->dir_entry);
        
        if(err != FAT_OK){
            result = err;
        }
    }

// free file handle memory regardless of errors
cleanup:
    free(file);
    return result;
}
