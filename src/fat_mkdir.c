#include "fat_file.h"
#include "fat_path.h"
#include "fat_dir_search.h"
#include "fat_cluster.h"
#include "fat_table.h"
#include "fat_lfn.h"
#include "fat_file_create.h"
#include "fat_root.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

bool fat_validate_directory_name(const char *name){

    // parameter validation
    if(!name || strlen(name) == 0){
        return false;
    }

    if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0){
        return false;
    }

    if(strchr(name,'/') != NULL || strchr(name, '\\') != NULL){
        return false;
    }

    return fat_validate_filename(name);
}

fat_error_t fat_create_dot_entries(fat_volume_t *volume, uint8_t *cluster_buffer, 
                                   cluster_t dir_cluster, cluster_t parent_cluster){

    // parameter validation
    if(!volume || !cluster_buffer){
        return FAT_ERR_INVALID_PARAM;
    }

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    uint16_t fat_time = 0;
    uint16_t fat_date = 0;

    if(tm_info){
        fat_time = ((tm_info->tm_hour & 0x1F) << 11) |
                    ((tm_info->tm_min & 0x3F) << 5) |
                    ((tm_info->tm_sec / 2) & 0x1F);
        fat_date = (((tm_info->tm_year - 80) & 0x7F) << 9) |
                    (((tm_info->tm_mon + 1) & 0x0F) << 5) |
                    (tm_info->tm_mday & 0x1F);
    }

    // create "."
    fat_dir_entry_t *dot_entry = (fat_dir_entry_t*)&cluster_buffer[0];
    memset(dot_entry, 0, sizeof(fat_dir_entry_t));

    memcpy(dot_entry->name, ".          ", 11);
    dot_entry->attr = FAT_ATTR_DIRECTORY;
    dot_entry->create_time = fat_time;
    dot_entry->create_date = fat_date;
    dot_entry->write_time = fat_time;
    dot_entry->write_date = fat_date;
    dot_entry->access_date = fat_date;
    dot_entry->file_size = 0;

    // set cluster reference for "."
    fat_set_entry_cluster(volume, dot_entry, dir_cluster);

    // create ".."
    fat_dir_entry_t *dotdot_entry = (fat_dir_entry_t*)&cluster_buffer[32];
    memset(dotdot_entry, 0, sizeof(fat_dir_entry_t));

    memcpy(dotdot_entry->name, "..          ", 11);
    dotdot_entry->attr = FAT_ATTR_DIRECTORY;
    dotdot_entry->create_time = fat_time;
    dotdot_entry->create_date = fat_date;
    dotdot_entry->write_time = fat_time;
    dotdot_entry->write_date = fat_date;
    dotdot_entry->access_date = fat_date;
    dotdot_entry->file_size = 0;

    // set cluster reference for ".."
    cluster_t parent_ref = parent_cluster;
    if(parent_cluster == fat_get_root_dir_cluster(volume)){
        // parent is root
        parent_ref = 0;
    }
    fat_set_entry_cluster(volume, dotdot_entry, parent_ref);
    
    return FAT_OK;
}

fat_error_t fat_initialize_directory_cluster(fat_volume_t *volume, 
                                             cluster_t dir_cluster, 
                                             cluster_t parent_cluster){

    // parameter validation
    if(!volume || dir_cluster < 2){
        return FAT_ERR_INVALID_PARAM;
    }

    uint8_t *cluster_buffer = calloc(1, volume->bytes_per_cluster);
    if(!cluster_buffer){
        return FAT_ERR_NO_MEMORY;
    }

    fat_error_t err = fat_create_dot_entries(volume, cluster_buffer, dir_cluster, 
                                             parent_cluster);
    if(err != FAT_OK){
        free(cluster_buffer);
        return err;
    }

    uint32_t first_sector = fat_cluster_to_sector(volume, dir_cluster);
    int result = volume->device->write_sectors(volume->device->device_data, 
                                               first_sector, volume->sectors_per_cluster, 
                                               cluster_buffer);
    free(cluster_buffer);

    if(result != 0){
        return FAT_ERR_DEVICE_ERROR;
    }

    return FAT_OK;
}

fat_error_t fat_create_directory_entry(fat_volume_t *volume, cluster_t parent_cluster, 
                                       const char *dir_name, cluster_t dir_cluster){

    // parameter validation
    if(!volume || !dir_name || dir_cluster < 2){
        return FAT_ERR_INVALID_PARAM;
    }

    uint32_t entries_needed = fat_calculate_entries_needed(dir_name);

    uint32_t entry_index;
    fat_error_t err = fat_find_free_entry(volume, parent_cluster, entries_needed, 
                                          &entry_index);
    if(err != FAT_OK){
        return err;
    }

    uint8_t short_name[11];
    err = fat_generate_short_name(dir_name, short_name, volume, parent_cluster);
    if(err != FAT_OK){
        return err;
    }

    return fat_create_directory_entries(volume, parent_cluster, entry_index, 
                                        dir_name, short_name, dir_cluster, 
                                        FAT_ATTR_DIRECTORY);
}

fat_error_t fat_check_directory_space(fat_volume_t *volume, cluster_t parent_cluster, 
                                      uint32_t entries_needed){

    // parameter validation
    if(!volume || entries_needed == 0){
        return FAT_ERR_INVALID_PARAM;
    }

    uint32_t entry_index;
    return fat_find_free_entry(volume, parent_cluster, entries_needed, &entry_index);
}

fat_error_t fat_mkdir(fat_volume_t *volume, const char *path){

    // parameter validation
    if(!volume || !path){
        return FAT_ERR_INVALID_PARAM;
    }

    char *path_copy = malloc(strlen(path) + 1);
    if(!path_copy){
        return FAT_ERR_NO_MEMORY;
    }
    strcpy(path_copy, path);

    // find last slash to separate name and parent directory 
    char *last_slash = strrchr(path_copy, '/');
    char *dir_name;
    char *parent_path;
    
    if(last_slash){
        *last_slash = '\0';
        parent_path = path_copy;
        dir_name = last_slash + 1;

        if(strlen(parent_path) == 0){
            // parent directory is root
            parent_path = "/";
        }
    } else {
        // current directory is root
        parent_path = "/";
        dir_name = path_copy;
    }

    if(!fat_validate_directory_name(dir_name)){
        free(path_copy);
        return FAT_ERR_INVALID_PARAM;
    }

    fat_dir_entry_t existing_entry;
    fat_error_t err = fat_resolve_path(volume, path, &existing_entry, NULL, NULL);
    if(err == FAT_OK){
        free(path_copy);
        return FAT_ERR_ALREADY_EXISTS;
    } else if(err != FAT_ERR_NOT_FOUND){
        free(path_copy);
        return err;
    }

    // resolve parent directory
    fat_dir_entry_t parent_entry;
    cluster_t parent_cluster;
    err = fat_resolve_path(volume, parent_path, &parent_entry, &parent_cluster, NULL);
    if(err != FAT_OK){
        // parent directory does not exist
        free(path_copy);
        return err;
    }

    if(!(parent_entry.attr & FAT_ATTR_DIRECTORY)){
        free(path_copy);
        return FAT_ERR_NOT_A_DIRECTORY;
    }

    cluster_t parent_dir_cluster = fat_get_entry_cluster(volume, &parent_entry);

    uint32_t entries_needed = fat_calculate_entries_needed(dir_name);
    err = fat_check_directory_space(volume, parent_dir_cluster, entries_needed);
    if(err != FAT_OK){
        // parent directory full
        free(path_copy);
        return err;
    }

    cluster_t dir_cluster;
    err = fat_allocate_cluster(volume, &dir_cluster);
    if(err != FAT_OK){
        // drive full
        free(path_copy);
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
            fat_write_entry(volume, dir_cluster, FAT_FREE);
            free(path_copy);
            return FAT_ERR_UNSUPPORTED_FAT_TYPE;
    }

    err = fat_write_entry(volume, dir_cluster, eoc_marker);
    if(err != FAT_OK){
        fat_write_entry(volume, dir_cluster, FAT_FREE);
        free(path_copy);
        return err;
    }

    err = fat_initialize_directory_cluster(volume, dir_cluster, parent_dir_cluster);
    if(err != FAT_OK){
        fat_write_entry(volume, dir_cluster, FAT_FREE);
        free(path_copy);
        return err;
    }

    err = fat_create_directory_entry(volume, parent_dir_cluster, dir_name, dir_cluster);
    if(err != FAT_OK){
        fat_write_entry(volume, dir_cluster, FAT_FREE);
        free(path_copy);
        return err;
    }

    // flush changes
    err = fat_flush(volume);
    if(err != FAT_OK){
        // directory created, but changes might not be persistent
        free(path_copy);
        return err;
    }

    free(path_copy);
    return FAT_OK;
}