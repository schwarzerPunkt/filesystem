#include "fat_file_close.h"
#include "fat_dir.h"
#include "fat_cluster.h"
#include "fat_root.h"
#include <stdlib.h>
#include <time.h>

bool fat_validate_file_handle(fat_file_t *file){
    // parameter validation
    if(!file){
        return false;
    }

    if(!file->volume){
        return false;
    }

    // check if position is more than a cluster beyond file_size
    if(file->position > file->dir_entry.file_size + file->volume->bytes_per_cluster){
        return false;
    }

    // check if file_size is reasonable 
    if(file->dir_entry.file_size > 0xFFFFFFFF){
        return false;
    }

    return true;
}

fat_error_t fat_calculate_directory_entry_location(fat_file_t *file, uint32_t *sector, 
                                                   uint32_t *offset){
 
    // parameter validation
    if(!file || !sector || !offset){
        return FAT_ERR_INVALID_PARAM;
    }

    bool is_root_fat12 = (file->dir_cluster == 0 &&
                          file->volume->type != FAT_TYPE_FAT32);
    if(is_root_fat12){
        // FAT12/16 root directory
        uint32_t entries_per_sector = file->volume->bytes_per_sector / 32;
        uint32_t root_start_sector = file->volume->reserved_sector_count +
                                    (file->volume->num_fats * file->volume->fat_size_sectors);
        *sector = root_start_sector + (file->dir_entry_offset / entries_per_sector);
        *offset = (file->dir_entry_offset % entries_per_sector) * 32;
    } else {
        // FAT32 or subdirectory
        uint32_t entries_per_cluster = file->volume->bytes_per_cluster / 32;
        uint32_t cluster_index = file->dir_entry_offset / entries_per_cluster;
        uint32_t entry_in_cluster = file->dir_entry_offset % entries_per_cluster;

        // walk cluster chain
        cluster_t target_cluster = file->dir_cluster;
        for(uint32_t i=0 ; i<cluster_index; i++){
            fat_error_t err = fat_get_next_cluster(file->volume, target_cluster, &target_cluster);
            if(err != FAT_OK || fat_is_eoc(file->volume, target_cluster)){
                return FAT_ERR_CORRUPTED;
            }
        }

        // convert cluster to sector
        uint32_t cluster_first_sector = fat_cluster_to_sector(file->volume, target_cluster);
        uint32_t entries_per_sector = file->volume->bytes_per_sector / 32;

        *sector = cluster_first_sector + (entry_in_cluster / entries_per_sector);
        *offset = (entry_in_cluster % entries_per_sector) * 32;
    }
    return FAT_OK;
}

fat_error_t fat_update_directory_entry(fat_file_t *file, const fat_dir_entry_t *entry){

    // parameter validation
    if(!file || !entry){
        return FAT_ERR_INVALID_PARAM;
    }

    uint32_t sector, offset;
    fat_error_t err = fat_calculate_directory_entry_location(file, &sector, &offset);
    if(err != FAT_OK){
        return err;
    }

    return fat_write_dir_entry(file->volume, sector, offset, entry);
}

fat_error_t fat_flush_file_data(fat_file_t *file){

    // parameter validation
    if(!file || !file->volume){
        return FAT_ERR_INVALID_PARAM;
    }

    // flush volume level caches
    fat_error_t err = fat_flush(file->volume);
    if(err != FAT_OK){
        return err;
    }

    // addition flush file-specific buffers
    // addition sync device write caches here

    return FAT_OK;
}

fat_error_t fat_close(fat_file_t *file){

    // parameter validation
    if(!file){
        return FAT_ERR_INVALID_PARAM;
    }

    fat_error_t result = FAT_OK;

    if(!fat_validate_file_handle(file)){
        free(file);
        return FAT_ERR_INVALID_PARAM;
    }

    if(file->modified){
        fat_update_file_timestamps(&file->dir_entry);
        
        fat_error_t err = fat_update_directory_entry(file, &file->dir_entry);
        if(err != FAT_OK && result == FAT_OK){
            result = err;
        }

        // flush any cached data
        err = fat_flush_file_data(file);
        if(err != FAT_OK && result == FAT_OK) {
            result = err;
        }
    }
    
    // cleanup
    free(file);
    return result;
}