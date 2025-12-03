#include "fat_file.h"
#include "fat_path.h"
#include "fat_dir_search.h"
#include "fat_cluster.h"
#include "fat_table.h"
#include "fat_lfn.h"
#include "fat_root.h"
#include "fat_file_delete.h"
#include <string.h>
#include <stdlib.h>

bool fat_is_root_directory(const char *path){
    
    // parameter validation
    if(!path || strlen(path) == 0){
        return true;
    }

    // trim leading slashes
    while(*path == '/'){
        path++;
    }

     return (strlen(path) == 0);
}

bool fat_validate_directory_deletion(fat_volume_t *volume, const 
                                     fat_dir_entry_t *dir_entry, const char *path){

    // parameter validation
    if(!volume || !dir_entry){
        return false;
    }

    if(!(dir_entry->attr & FAT_ATTR_DIRECTORY)) {
        return false;
    }

    if(dir_entry->attr & FAT_ATTR_READ_ONLY){
        return false;
    }

    if(fat_is_root_directory(path)){
        return false;
    }

    if(dir_entry->attr & FAT_ATTR_VOLUME_ID){
        return false;
    }

    return true;
}

fat_error_t fat_count_directory_entries(fat_volume_t *volume, 
                                        cluster_t dir_cluster, uint32_t *entry_count){

    // parameter validation
    if(!volume  || !entry_count){
        return FAT_ERR_INVALID_PARAM;
    }

    *entry_count = 0;

    if(dir_cluster < 2){
        return FAT_ERR_INVALID_CLUSTER;
    }

    cluster_t current_cluster = dir_cluster;
    uint32_t entries_per_cluster = volume->bytes_per_cluster / 32;

    uint8_t *cluster_buffer = malloc(volume->bytes_per_cluster);
    if(!cluster_buffer){
        return FAT_ERR_NO_MEMORY;
    }

    while(current_cluster >= 2 && !fat_is_eoc(volume, current_cluster)){

        uint32_t first_sector = fat_cluster_to_sector(volume, current_cluster);
        int result = volume->device->read_sectors(volume->device->device_data, 
                                                  first_sector, 
                                                  volume->sectors_per_cluster, 
                                                  cluster_buffer);
        if(result != 0){
            free(cluster_buffer);
            return FAT_ERR_DEVICE_ERROR;
        }

        for(uint32_t i=0; i<entries_per_cluster; i++){
            fat_dir_entry_t *entry = (fat_dir_entry_t*)&cluster_buffer[i * 32];
            if(entry->name[0] == FAT_DIR_ENTRY_FREE){
                free(cluster_buffer);
                return FAT_OK;
            }

            if(entry->name[0] != FAT_DIR_ENTRY_DELETED){
                (*entry_count)++;
            }
        }

        fat_error_t err = fat_get_next_cluster(volume, current_cluster, 
                                               &current_cluster);
        if(err != FAT_OK){
            free(cluster_buffer);
            return err;
        }
    }

    free(cluster_buffer);
    return FAT_OK;
}

fat_error_t fat_verify_directory_empty(fat_volume_t *volume, cluster_t dir_cluster){

    // parameter validation
    if(!volume || dir_cluster < 2){
        return FAT_ERR_INVALID_PARAM;
    }

    uint32_t entry_count;
    fat_error_t err = fat_count_directory_entries(volume, dir_cluster, &entry_count);
    if(err != FAT_OK){
        return err;
    }

    if(entry_count != 2){
        return FAT_ERR_DIRECTORY_NOT_EMPTY;
    }

    // TODO verify the found entries are "." and ".."

    return FAT_OK;
}

fat_error_t fat_delete_directory_clusters(fat_volume_t *volume, cluster_t start_cluster){

    // parameter validation
    if(!volume || start_cluster < 2){
        return FAT_ERR_INVALID_PARAM;
    }

    cluster_t current_cluster = start_cluster;
    uint32_t clusters_freed = 0;

    while(current_cluster >= 2 && current_cluster < volume->total_clusters + 2){

        cluster_t next_cluster;
        fat_error_t err = fat_get_next_cluster(volume, current_cluster, &next_cluster);
        if(err != FAT_OK){
            // cannot read next cluster - stop
            next_cluster = 0;
        }

        err = fat_write_entry(volume, current_cluster, FAT_FREE);
        if(err != FAT_OK){
            // continue to free clusters
        } else {
            clusters_freed++;
        }

        if(fat_is_eoc(volume, next_cluster) || next_cluster < 2){
            break;
        }

        current_cluster = next_cluster;

        if(clusters_freed > volume->total_clusters){
            // Loop
            break;
        }
    }

    // TODO implement fat_update_free_cluster_count (only FAT32)
    if(clusters_freed > 0){
        fat_update_free_cluster_count(volume, clusters_freed);
    }

    return FAT_OK;
}

fat_error_t fat_rmdir(fat_volume_t *volume, const char *path){

    // parameter validation
    if(!volume || !path){
        return FAT_ERR_INVALID_PARAM;
    }

    if(fat_is_root_directory(path)){
        return FAT_ERR_INVALID_PARAM;
    }

    fat_dir_entry_t dir_entry;
    cluster_t parent_cluster;
    uint32_t entry_index;

    fat_error_t err = fat_resolve_path(volume, path, &dir_entry, &parent_cluster, 
                                       &entry_index);
    if(err != FAT_OK){
        return err;
    }

    if(!fat_validate_directory_deletion(volume, &dir_entry, path)){
        return FAT_ERR_READ_ONLY;
    }

    cluster_t dir_cluster = fat_get_entry_cluster(volume, &dir_entry);

    err = fat_verify_directory_empty(volume, dir_cluster);
    if(err != FAT_OK){
        return err;
    }

    if(dir_cluster >= 2){
        err = fat_delete_directory_clusters(volume, dir_cluster);
        if(err != FAT_OK){
            // failed to free cluster - continue
        }
    }

    bool has_lfn = true;

    err = fat_delete_directory_entries(volume, parent_cluster, entry_index, has_lfn);
    if(err != FAT_OK){
        return err;
    }

    err = fat_flush(volume);
    if(err != FAT_OK){
        // directory deleted but change might not be persistent
        return err;
    }

    return FAT_OK;
}