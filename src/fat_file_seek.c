#include "fat_file_seek.h"
#include "fat_file_read.h"
#include <limits.h>

bool fat_validate_seek_parameters(fat_file_t *file, int32_t offset, int whence){

    // parameter validation
    if(!file || !file->volume){
        return false;
    }

    if(whence != FAT_SEEK_SET && whence != FAT_SEEK_CUR && whence != FAT_SEEK_END){
        return false;
    }

    if(file->position > file->dir_entry.file_size){
        return false;
    }

    // TODO add proper offset validation
    offset = offset;

    return true;
}

fat_error_t fat_calculate_target_position(fat_file_t *file, int32_t offset, 
                                          int whence, uint32_t *target_position){

    // parameter validation
    if(!file || !target_position){
        return FAT_ERR_INVALID_PARAM;
    }

    int64_t new_position;
    switch(whence){
        case FAT_SEEK_SET:
            if(offset < 0){
                return FAT_ERR_INVALID_PARAM;
            }
            new_position = offset;
            break;
        case FAT_SEEK_CUR:
            new_position = (int64_t)file->position + offset;
            break;
        case FAT_SEEK_END:
            new_position = (int64_t)file->dir_entry.file_size + offset;
            break;
        default:
            return FAT_ERR_INVALID_PARAM;
    }

    // check underflow
    if(new_position < 0){
        return FAT_ERR_INVALID_PARAM;
    }

    // check overflow
    if(new_position > UINT32_MAX){
        return FAT_ERR_INVALID_PARAM;
    }

    *target_position = (uint32_t)new_position;
    return FAT_OK;
}

fat_error_t fat_optimize_cluster_seek(fat_file_t *file, uint32_t target_position){

    //parameter validation
    if(!file){
        return FAT_ERR_INVALID_PARAM;
    }

    uint32_t target_cluster_index = target_position / file->volume->bytes_per_cluster;
    uint32_t target_cluster_offset = target_position % file->volume->bytes_per_cluster;

    uint32_t current_cluster_index = file->position / file->volume->bytes_per_cluster;

    cluster_t new_cluster;
    fat_error_t err;

    if(target_cluster_index == current_cluster_index){
        // same cluster
        new_cluster = file->current_cluster;
    } else if (target_cluster_index > current_cluster_index){
        // forward seek
        uint32_t clusters_to_advance = target_cluster_index - current_cluster_index;
        if(current_cluster_index < clusters_to_advance / 2){
            // very large forward seek - restart from beginning
            cluster_t start_cluster = fat_get_entry_cluster(file->volume, 
                                                             &file->dir_entry);
            err = fat_walk_cluster_chain(file->volume, start_cluster, 
                                         target_cluster_index, &new_cluster);
        } else {
            err = fat_walk_cluster_chain(file->volume, file->current_cluster, 
                                         clusters_to_advance, &new_cluster);
        }

        if(err != FAT_OK){
            return err;
        }
    } else {
        // backward seek
        cluster_t start_cluster = fat_get_entry_cluster(file->volume, 
                                                        &file->dir_entry);
        err = fat_walk_cluster_chain(file->volume, start_cluster, 
                                     target_cluster_index, &new_cluster);
        if(err != FAT_OK){
            return err;
        }
    }

    file->current_cluster = new_cluster;
    file->cluster_offset = target_cluster_offset;

    return FAT_OK;
}

fat_error_t fat_seek_to_position(fat_file_t *file, uint32_t target_position){

    // parameter validation
    if(!file){
        return FAT_ERR_INVALID_PARAM;
    }

    if(target_position > file->dir_entry.file_size){
        return FAT_ERR_INVALID_PARAM;
    }

    uint32_t target_cluster_index = 0; 
    uint32_t target_cluster_offset = 0;
    fat_calculate_cluster_position(file->volume, target_position, 
                                   &target_cluster_index, &target_cluster_offset);
    uint32_t current_cluster_index = 0;
    uint32_t current_cluster_offset = 0;
    fat_calculate_cluster_position(file->volume, file->position, 
                                   &current_cluster_index, &current_cluster_offset);
    cluster_t new_cluster;
    if(target_cluster_index == current_cluster_index){
        // same cluster
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

    // update
    file->position = target_position;
    file->current_cluster = new_cluster;
    file->cluster_offset = target_cluster_offset;

    return FAT_OK;
}

fat_error_t fat_seek(fat_file_t *file, int32_t offset, int whence){

    // parameter validation
    if(!fat_validate_seek_parameters(file, offset, whence)){
        return FAT_ERR_INVALID_PARAM;
    }

    uint32_t target_position;
    fat_error_t err = fat_calculate_target_position(file, offset, whence, 
                                                    &target_position);
    if(err != FAT_OK){
        return err;
    }

    if(target_position == file->position){
        // seek to current position
        return FAT_OK;
    }

    if(file->dir_entry.file_size == 0){
        // seek in empty file
        if(target_position == 0){
            file->position = 0;
            file->cluster_offset = 0;
            return FAT_OK;
        } else {
            // seek beyond file_size
            file->position = target_position;
            file->cluster_offset = 0;
            return FAT_OK;
        }
    }

    // non-empty files
    err = fat_optimize_cluster_seek(file, target_position);
    if(err != FAT_OK){
        return err;
    }

    file->position = target_position;
    return FAT_OK;
}

uint32_t fat_tell(fat_file_t *file){
    if(!file){
        return 0;
    }

    return file->position;
}