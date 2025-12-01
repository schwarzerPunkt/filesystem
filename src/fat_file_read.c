#include "fat_table.h"
#include "fat_root.h"
#include "fat_file_read.h"
#include <string.h>
#include <stdlib.h>

void fat_calculate_cluster_position(fat_volume_t *volume, uint32_t position,
                                    uint32_t *cluster_index, uint32_t *cluster_offset){
    
    // parameter validation
    if(!volume || !cluster_index || !cluster_offset){
        return;
    }

    *cluster_index = position / volume->bytes_per_cluster;
    *cluster_offset = position % volume->bytes_per_cluster;
}

fat_error_t fat_walk_cluster_chain(fat_volume_t *volume, cluster_t start_cluster, 
                                   uint32_t target_index, cluster_t *result_cluster){
    
    // parameter validation
    if(!volume || !result_cluster){
        return FAT_ERR_INVALID_PARAM;
    }

    cluster_t current_cluster = start_cluster;

    for(uint32_t i=0; i<target_index; i++){
        if(current_cluster < 2 || current_cluster >= volume->total_clusters + 2){
            return FAT_ERR_INVALID_CLUSTER;
        }

        if(fat_is_eoc(volume, current_cluster)){
            return FAT_ERR_EOF;
        }

        fat_error_t err = fat_get_next_cluster(volume, current_cluster, &current_cluster);
        if(err != FAT_OK){
            return err;
        }
    }

    *result_cluster = current_cluster;
    return FAT_OK;
}

fat_error_t fat_seek_to_position(fat_file_t *file, uint32_t target_position){

    // parameter validation
    if(!file){
        return FAT_ERR_INVALID_PARAM;
    }

    if(target_position> file->dir_entry.file_size){
        return FAT_ERR_INVALID_PARAM;
    }

    uint32_t target_cluster_index = 0; 
    uint32_t target_cluster_offset = 0;
    
    fat_calculate_cluster_position (file->volume, target_position, 
                                    &target_cluster_index, &target_cluster_offset);
    
    uint32_t current_cluster_index = 0; 
    uint32_t current_cluster_offset = 0;
    fat_calculate_cluster_position (file->volume, file->position, 
                                    &current_cluster_index, &current_cluster_offset);

    cluster_t new_cluster;

    if(target_cluster_index == current_cluster_index){
        // same cluster - update offset
        new_cluster = file->current_cluster;
    } else if(target_cluster_index > current_cluster_index){
        // forward seek
        uint32_t clusters_to_advance = target_cluster_index - current_cluster_index;
        fat_error_t err = fat_walk_cluster_chain(file->volume, file->current_cluster, 
                                                 clusters_to_advance, &new_cluster);

        if(err != FAT_OK){
            return err;
        }
    } else {
        // backward seek
        cluster_t start_cluster = fat_get_entry_cluster(file->volume, &file->dir_entry);
        fat_error_t err = fat_walk_cluster_chain(file->volume, start_cluster, 
                                                 target_cluster_index, &new_cluster);
        
        if(err != FAT_OK){
            return err;
        }
    }

    file->position = target_position;
    file->current_cluster = new_cluster;
    file->cluster_offset = target_cluster_offset;

    return FAT_OK;
}

fat_error_t fat_read_cluster_data(fat_volume_t *volume, cluster_t cluster, 
                                  uint32_t offset, void *buffer, size_t length){

    // parameter validation
    if(!volume || !buffer || length == 0){
        return FAT_ERR_INVALID_PARAM;
    }

    if(cluster<2 || cluster >= volume->total_clusters + 2){
        return FAT_ERR_INVALID_PARAM;
    }

    if(offset >= volume->bytes_per_cluster){
        return FAT_ERR_INVALID_PARAM;
    }

    // limit length to cluster boundary
    if(offset + length > volume->bytes_per_cluster){
        length = volume->bytes_per_cluster - offset;
    }

    uint32_t first_sector = fat_cluster_to_sector(volume, cluster);

    // calculate sector length for requested data
    uint32_t start_sector = first_sector + (offset / volume->bytes_per_sector);
    uint32_t end_sector = first_sector + ((offset + length - 1) / volume->bytes_per_sector);
    uint32_t sectors_to_read = end_sector - start_sector + 1;

    uint8_t *sector_buffer = malloc(sectors_to_read * volume->bytes_per_sector);
    if(!sector_buffer){
        return FAT_ERR_NO_MEMORY;
    }

    int result = volume->device->read_sectors(volume->device->device_data, start_sector,
                                              sectors_to_read, sector_buffer);

    if(result != 0){
        free(sector_buffer);
        return FAT_ERR_DEVICE_ERROR;
    }

    // calculate offset within read buffer
    uint32_t buffer_offset = offset % volume->bytes_per_sector;

    memcpy(buffer, &sector_buffer[buffer_offset], length);

    free(sector_buffer);
    return FAT_OK;
    
}

int fat_read(fat_file_t *file, void *buffer, size_t size){

    // parameter validation
    if(!file || !buffer || size == 0){
        return -FAT_ERR_INVALID_PARAM;
    }

    if(!(file->flags & (FAT_O_RDONLY | FAT_O_RDWR))){
        return -FAT_ERR_INVALID_PARAM;
    }

    if(file->position >= file->dir_entry.file_size){
        return 0;
    }

    // calculate how much data is available
    uint32_t available = file->dir_entry.file_size - file->position;
    if(size>available){
        size = available;
    }

    if(size == 0){
        return 0;
    }

    // check position
    fat_error_t err = fat_seek_to_position(file,  file->position);
    if(err != FAT_OK){
        return -err;
    }
    
    uint8_t *output_buffer = (uint8_t *)buffer;
    size_t bytes_read = 0;
    size_t remaining = size;
    
    while(remaining > 0){
        // calculate amount to read from current cluster
        uint32_t cluster_remaining = file->volume->bytes_per_cluster - file->cluster_offset;
        size_t chunk_size = (remaining < cluster_remaining) ? remaining : cluster_remaining;

        err = fat_read_cluster_data(file->volume, file->current_cluster, file->cluster_offset, 
                                    &output_buffer[bytes_read], chunk_size);

        if(err != FAT_OK){
            // return bytes read or error if nothing was read
            return bytes_read > 0 ? (uint32_t)bytes_read : -err;
        }

        bytes_read += chunk_size;
        remaining -= chunk_size;
        file->cluster_offset += chunk_size;

        if(file->cluster_offset >= file->volume->bytes_per_cluster && remaining > 0){
            cluster_t next_cluster;
            err = fat_get_next_cluster(file->volume, file->current_cluster, &next_cluster);
            if(err != FAT_OK || fat_is_eoc(file->volume, next_cluster)) {
                // should never happen if file size is correct
                break;
            }

            file->current_cluster = next_cluster;
            file->cluster_offset = 0;
        }
    }

    file->position += bytes_read;

    return (int)bytes_read;

}