#include "fat_file_write.h"
#include "fat_file_seek.h"
#include "fat_root.h"
#include <string.h>

uint32_t fat_calculate_clusters_needed(fat_volume_t *volume, uint32_t file_size){

    // parameter validation
    if(!volume){
        return 1;
    }

    if(file_size == 0){
        // empty files need 1 cluster
        return 1;
    }

    // round up to cluster boundary
    return (file_size + volume->bytes_per_cluster - 1) / volume->bytes_per_cluster;
}

fat_error_t fat_find_last_cluster(fat_volume_t *volume, cluster_t start_cluster,
                                  cluster_t *last_cluster){

    // parameter validation
    if(!volume || !last_cluster || start_cluster){
        return FAT_ERR_INVALID_PARAM;
    }

    cluster_t current_cluster = start_cluster;

    while(1){
        uint32_t fat_entry;
        fat_error_t err = fat_read_entry(volume, current_cluster, &fat_entry);
        if(err != FAT_OK){
            return err;
        }

        // check if current cluster is EOC (= last cluster)
        if(fat_is_eoc(volume, fat_entry)){
            *last_cluster = current_cluster;
            return FAT_OK;
        }

        current_cluster = fat_entry;

        // validate cluster number
        if(current_cluster < 2 || current_cluster >= volume->total_clusters + 2){
            return  FAT_ERR_CORRUPTED;
        }
    }
}

fat_error_t fat_allocate_and_link_cluster(fat_volume_t *volume, cluster_t prev_cluster, 
                                          cluster_t *new_cluster){

    // parameter validation
    if(!volume || !new_cluster || prev_cluster <2){
        return FAT_ERR_INVALID_PARAM;
    }

    // allocate new cluster
    cluster_t allocated_cluster;
    fat_error_t err = fat_allocate_cluster(volume, &allocated_cluster);
    if(err != FAT_OK){
        return err;
    }

    uint32_t eoc_marker;
    switch(volume->type){
        case FAT_TYPE_FAT12:
            eoc_marker = FAT12_EOC;
            break;
        case FAT_TYPE_FAT16:
            eoc_marker = FAT16_EOC;
            break;
        case FAT_TYPE_FAT32:
            eoc_marker = FAT32_EOC;
            break;
        default:
            return FAT_ERR_UNSUPPORTED_FAT_TYPE;
    }

    err = fat_write_entry(volume, allocated_cluster, eoc_marker);
    if(err != FAT_OK){
        // try to mark allocated_cluster as free
        fat_write_entry(volume, allocated_cluster, FAT_FREE);
        return err;
    }

    // link previous cluster to allocated_cluster
    err = fat_write_entry(volume, prev_cluster, allocated_cluster);
    if(err != FAT_OK){
        // clean up - free allocated_cluster and restore EOC
        fat_write_entry(volume, allocated_cluster, FAT_FREE);
        fat_write_entry(volume, prev_cluster, eoc_marker);
        return err;
    }

    *new_cluster = allocated_cluster;
    return FAT_OK;
}

fat_error_t fat_extend_file(fat_file_t *file, uint32_t new_size){

    // parameter validation
    if(!file || new_size <= file->dir_entry.file_size){
        return FAT_ERR_INVALID_PARAM;
    }

    uint32_t clusters_needed = fat_calculate_clusters_needed(file->volume, new_size);
    uint32_t current_clusters = fat_calculate_clusters_needed(file->volume, 
                                                              file->dir_entry.file_size);
    if(clusters_needed <= current_clusters){
        // no additional clusters required
        return FAT_OK;
    }

    uint32_t clusters_to_add = clusters_needed - current_clusters;

    cluster_t last_cluster;
    cluster_t start_cluster = fat_get_entry_cluster(file->volume, &file->dir_entry);

    if(start_cluster == 0){
        // no clusters allocated yet 
        fat_error_t err = fat_allocate_cluster(file->volume, &start_cluster);
        if (err != FAT_OK){
            return err;
        }

        // write EOC to first cluster
        uint32_t eoc_marker = (file->volume->type == FAT_TYPE_FAT12) ? FAT12_EOC :
                              (file->volume->type == FAT_TYPE_FAT16) ? FAT16_EOC : 
                              FAT32_EOC;
        fat_write_entry(file->volume, start_cluster, eoc_marker);

        // update directory entry
        fat_set_entry_cluster(file->volume, &file->dir_entry, start_cluster);
        file->current_cluster = start_cluster;

        clusters_to_add--;
        last_cluster = start_cluster;
    } else {
        fat_error_t err = fat_find_last_cluster(file->volume, start_cluster, 
                                                &last_cluster);
        if(err != FAT_OK){
            return err;
        }
    }

    // allocate and link additional clusters
    cluster_t current_last = last_cluster;
    for(uint32_t i=0; i<clusters_to_add; i++){
        cluster_t new_cluster;
        fat_error_t err = fat_allocate_and_link_cluster(file->volume, current_last, 
                                                        &new_cluster);
        if(err != FAT_OK){
            return err;
        }
        current_last = new_cluster;
    }

    return FAT_OK;
}

fat_error_t fat_write_cluster_data(fat_volume_t *volume, cluster_t cluster, 
                                   uint32_t offset, const void *buffer, size_t length){

    // parameter validation
    if(!volume || !buffer || length==0){
        return FAT_ERR_INVALID_PARAM;
    }

    if(cluster < 2 || cluster >= volume->total_clusters + 2){
        return FAT_ERR_INVALID_CLUSTER;
    }

    if(offset >= volume->bytes_per_cluster){
        return FAT_ERR_INVALID_PARAM;
    }

    // limit length of cluster boundary
    if(offset + length > volume->bytes_per_cluster){
        length = volume->bytes_per_cluster - offset;
    }

    uint32_t first_sector = fat_cluster_to_sector(volume, cluster);

    uint32_t start_sector = first_sector + (offset / volume->bytes_per_sector);
    uint32_t end_sector = first_sector + ((offset + length - 1) / volume->bytes_per_sector);
    uint32_t sectors_to_write = end_sector - start_sector + 1;

    // check if we are writing complete sectors
    uint32_t sector_start_offset = offset * volume->bytes_per_sector;
    bool complete_sectors = (sector_start_offset == 0) && 
                            (length == sectors_to_write * volume->bytes_per_sector);
    if(complete_sectors){
        // direct write
        int result = volume->device->write_sectors(volume->device->device_data, 
                                                   start_sector, sectors_to_write, buffer);
        return (result == 0) ? FAT_OK : FAT_ERR_DEVICE_ERROR;
    } else {
        // partial sector write
        uint8_t *sector_buffer = malloc(sectors_to_write * volume->bytes_per_sector);
        if(!sector_buffer){
            return FAT_ERR_NO_MEMORY;
        }

        // read current sector data
        int result = volume->device->read_sectors(volume->device->device_data, 
                                                  start_sector, sectors_to_write, 
                                                  sector_buffer);
        if(result != 0){
            free(sector_buffer);
            return FAT_ERR_DEVICE_ERROR;
        }

        // modify relevant bytes 
        memcpy(&sector_buffer[sector_start_offset], buffer, length);

        // write modified buffer to drive
        result = volume->device->write_sectors(volume->device->device_data,
                                               start_sector, sectors_to_write,
                                               sector_buffer);
        free(sector_buffer);
        return(result == 0) ? FAT_OK : FAT_ERR_DEVICE_ERROR;
    }
}

int fat_write(fat_file_t *file, const void *buffer, size_t size){

    // parameter validation
    if(!file || !buffer || size == 0){
        return -FAT_ERR_INVALID_PARAM;
    }

    if(!(file->flags & (FAT_O_WRONLY | FAT_O_RDWR))){
        return -FAT_ERR_INVALID_PARAM;
    }

    // check if we must extended the file
    uint32_t write_end_position = file->position + size;
    if(write_end_position > file->dir_entry.file_size){
        fat_error_t err = fat_extend_file(file, write_end_position);
        if(err != FAT_OK){
            // try to write what we can
            if(file->position >= file->dir_entry.file_size){
                return -err;
            }
            // limit write to current file size
            size = file->dir_entry.file_size - file->position;
            write_end_position = file->dir_entry.file_size;
        }
    }

    // check position
    fat_error_t err = fat_seek_to_position(file, file->position);
    if(err != FAT_OK){
        return -err;
    }

    const uint8_t *input_buffer = (const uint8_t *)buffer;
    size_t bytes_written = 0;
    size_t remaining = size;

    // write data
    while(remaining > 0){
        // calculate how much space is left in current cluster
        uint32_t cluster_remaining = file->volume->bytes_per_cluster - file->cluster_offset;
        size_t chunk_size = (remaining < cluster_remaining) ? remaining : cluster_remaining;

        err = fat_write_cluster_data(file->volume, file->current_cluster, 
                                     file->cluster_offset, &input_buffer[bytes_written], 
                                     chunk_size);
        if(err != FAT_OK){
            // return bytes written / error
            return bytes_written > 0 ? (uint32_t)bytes_written : -err;
        }

        //update counters
        bytes_written += chunk_size;
        remaining -= chunk_size;
        file->cluster_offset += chunk_size;

        if(file->cluster_offset >= file->volume->bytes_per_cluster && remaining > 0){
            // move to next cluster
            cluster_t next_cluster;
            err = fat_get_next_cluster(file->volume, file->current_cluster, &next_cluster);
            if(err != FAT_OK || fat_is_eoc(file->volume, next_cluster)){
                // should never happen if extension worked properly
                break;
            }
            file->current_cluster = next_cluster;
            file->cluster_offset = 0;
        }
    }

    // updates + mark update
    file->position += bytes_written;

    if(file->position > file->dir_entry.file_size){
        file->dir_entry.file_size = file->position;
    }

    file->modified = true;
    return (int)bytes_written;
}