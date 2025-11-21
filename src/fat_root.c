#include "fat_root.h"
#include "fat_cluster.h"
#include <stdlib.h>
#include <string.h>

cluster_t fat_get_root_dir_cluster(fat_volume_t *volume){

    // parameter validation
    if(!volume){
        return 0;
    }

    if(volume-> type == FAT_TYPE_FAT32){
        // FAT32 - first cluster is root_cluster
        return volume->root_cluster;
    } else {
        // FAT12/16 - fixed root region
        return 0;
    }
}

fat_error_t fat_read_root_dir_fat12(fat_volume_t *volume, fat_dir_entry_t **entries,
                                    uint32_t *count){

    // parameter validation
    if(!volume||!entries||!count){
        return FAT_ERR_INVALID_PARAM;
    }

    if(volume->type == FAT_TYPE_FAT32) {
        return FAT_ERR_UNSUPPORTED_FAT_TYPE;
    }

    // calculate root directory location
    uint32_t root_dir_start_sector = volume->reserved_sector_count + 
                                    (volume->num_fats * volume->fat_size_sectors);
    
    uint32_t root_dir_sectors = volume->root_dir_sectors;
    
    // get number of entries in root directory
    uint32_t total_entries = volume->root_entry_count;
    *count = total_entries;

    // allocate buffer for all entries
    *entries = (fat_dir_entry_t*)malloc(total_entries * sizeof(fat_dir_entry_t));
    if(!*entries){
        return FAT_ERR_NO_MEMORY;
    }

    // read root directory sectors
    uint32_t entries_per_sector = volume->bytes_per_sector / sizeof(fat_dir_entry_t);
    uint32_t entry_index = 0;

    for(uint32_t sector = 0; sector < root_dir_sectors; sector++){

        // allocate sector buffer
        uint8_t *sector_buffer = (uint8_t*)malloc(volume->bytes_per_sector);
        if(!sector_buffer){
            free(*entries);
            *entries = NULL;
            return FAT_ERR_NO_MEMORY;
        }

        // read sector
        int result = volume->device->read_sectors(volume->device->device_data, 
                                                  root_dir_start_sector + sector, 
                                                  1, sector_buffer);
        
        if(result != 0){
            free(sector_buffer);
            free(*entries);
            *entries = NULL;
            return FAT_ERR_DEVICE_ERROR;
        }

        for (uint32_t i = 0; i < entries_per_sector && entry_index < total_entries; i++){
            memcpy(&(*entries)[entry_index], 
                     &sector_buffer[i * sizeof(fat_dir_entry_t)], 
                     sizeof(fat_dir_entry_t));
            entry_index++;
        }

        free(sector_buffer);
    }
    return FAT_OK;
}

fat_error_t fat_read_root_dir_fat32(fat_volume_t *volume, fat_dir_entry_t **entries, 
                                    uint32_t *count){

    // parameter validation
    if(!volume||!entries||!count){
        return FAT_ERR_INVALID_PARAM;
    }                                 
    
    if(volume->type != FAT_TYPE_FAT32){
        return FAT_ERR_UNSUPPORTED_FAT_TYPE;
    }

    // count clusters in root dir chain
    cluster_t current_cluster = volume->root_cluster;
    uint32_t cluster_count = 0;
    uint32_t max_clusters = volume->total_clusters;

    while (cluster_count < max_clusters){
        cluster_count++;

        // get next cluster
        cluster_t next_cluster;
        fat_error_t err = fat_get_next_cluster(volume, current_cluster, &next_cluster);
        if(err != FAT_OK){
            return err;
        }

        // check EOC
        if(fat_is_eoc(volume, next_cluster)){
            break;
        }

        // validate next cluster
        if (next_cluster < FAT_FIRST_VALID_CLUSTER ||
            next_cluster >= FAT_FIRST_VALID_CLUSTER + volume->total_clusters){
            return FAT_ERR_CORRUPTED;
        }

        current_cluster = next_cluster;
    }

    if(cluster_count >= max_clusters){
        return FAT_ERR_CORRUPTED;
    }

    // calculate total entries
    uint32_t entries_per_cluster = volume->bytes_per_cluster / sizeof(fat_dir_entry_t);
    uint32_t total_entries = cluster_count * entries_per_cluster;
    *count = total_entries;

    // allocate buffer for all entries
    *entries = (fat_dir_entry_t*)malloc(total_entries * sizeof(fat_dir_entry_t));
    if(!*entries){
        return FAT_ERR_NO_MEMORY;
    }

    // read all clusters in chain
    current_cluster = volume->root_cluster;
    uint32_t entry_index = 0;

    for(uint32_t i = 0; i < cluster_count; i++){

        // allocate cluster buffer
        uint8_t *cluster_buffer = (uint8_t*)malloc(volume->bytes_per_cluster);
        if(!cluster_buffer){
            free(*entries);
            *entries = NULL;
            return FAT_ERR_NO_MEMORY;
        }

        uint32_t first_sector = fat_cluster_to_sector(volume, current_cluster);

        int result = volume->device->read_sectors(volume->device->device_data,
                                                  first_sector,
                                                  volume->sectors_per_cluster,
                                                  cluster_buffer);
        
        if(result != 0){
            free(cluster_buffer);
            free(*entries);
            *entries = NULL;
            return FAT_ERR_DEVICE_ERROR;
        }

        for(uint32_t j = 0; j < entries_per_cluster && entry_index < total_entries; j++){
            memcpy(&(*entries)[entry_index],
                    &cluster_buffer[j * sizeof(fat_dir_entry_t)],
                    sizeof(fat_dir_entry_t));
            entry_index++;
        }

        free(cluster_buffer);

        // get next cluster
        if(i < cluster_count - 1){
            cluster_t next_cluster;
            fat_error_t err = fat_get_next_cluster(volume, current_cluster, &next_cluster);
            if(err != FAT_OK){
                free(*entries);
                *entries = NULL;
                return err;
            }
            current_cluster = next_cluster;
        }
    }
    return FAT_OK;
}

uint32_t fat_cluster_to_sector(fat_volume_t *volume, cluster_t cluster){

    // parameter validation
    if(!volume){
        return 0;
    }

    if(cluster < FAT_FIRST_VALID_CLUSTER){
        return 0;
    }

    // calculate sector offset
    uint32_t cluster_offset = cluster - FAT_FIRST_VALID_CLUSTER;
    uint32_t sector_offset = cluster_offset * volume->sectors_per_cluster;
    uint32_t first_sector = volume->data_begin_sector + sector_offset;

    return first_sector;
}