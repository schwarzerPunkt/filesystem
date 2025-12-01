#include "fat_dir_search.h"
#include "fat_cluster.h"
#include "fat_table.h"
#include "fat_root.h"
#include "fat_lfn.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

bool fat_compare_short_name(const uint8_t *short_name, const char *filename){
    // parameter validation
    if(!short_name || !filename){
        return false;
    }

    char name_part[9] = {0};
    char ext_part[4] = {0};

    // get name and trim trailing spaces
    int name_len = 8;
    while(name_len > 0 && short_name[name_len-1] == ' '){
        name_len--;
    }

    memcpy(name_part, short_name, name_len);
    name_part[name_len] = '\0';

    // extract extension and trim trailing spaces
    int ext_len = 3;
    while(ext_len > 0 && short_name[8 + ext_len - 1] == ' '){
        ext_len--;
    }

    memcpy(ext_part, &short_name[8], ext_len);
    ext_part[ext_len] = '\0';

    // rebuild complete entry (filename + extension)
    char full_name[13] = {0};
    strcpy(full_name, name_part);
    if(ext_len>0){
        strcat(full_name, ".");
        strcat(full_name, ext_part);
    }

    // filename comparison (case insensitive)
    if(strlen(filename) != strlen(full_name)){
        return false;
    }

    for(size_t i=0; i<strlen(filename); i++){
        if(tolower(filename[i]) != tolower(full_name[i])){
            return false;
        }
    }

    return true;
}

fat_error_t fat_find_entry (fat_volume_t *volume, cluster_t dir_cluster, const char *name,
                            fat_dir_entry_t *entry, uint32_t *entry_index){

    // parameter validation
    if(!volume || !name || !entry){
        return FAT_ERR_INVALID_PARAM;
    }                                

    uint32_t current_cluster = dir_cluster;
    uint32_t entry_idx = 0;
    bool is_root_fat12 = (dir_cluster == 0 && volume->type != FAT_TYPE_FAT32);

    uint32_t entries_per_cluster = volume->bytes_per_cluster / 32;
    uint32_t entries_per_sector = volume->bytes_per_sector / 32;

    uint32_t max_root_entries = is_root_fat12 ? volume->root_entry_count : 0;
    uint32_t root_start_sector = 0;
    if(is_root_fat12){
        root_start_sector = volume->reserved_sector_count +
                            (volume->num_fats * volume->fat_size_sectors);
    }

    uint8_t *read_buffer = malloc(volume->bytes_per_cluster);
    if(!read_buffer){
        return FAT_ERR_NO_MEMORY;
    }

    while(1){
        uint32_t sector;
        uint32_t sectors_to_read;
        uint32_t entries_in_buffer;

        if(is_root_fat12){
            // FAT12 root directory
            if(entry_idx >= max_root_entries){
                free(read_buffer);
                return FAT_ERR_NOT_FOUND;
            }

            // read root sectors
            sector = root_start_sector + (entry_idx / entries_per_sector);
            sectors_to_read = 1;
            entries_in_buffer = entries_per_sector;

            int result = volume->device->read_sectors(volume->device->device_data,
                                                      sector, sectors_to_read, 
                                                      read_buffer);

            if(result != 0){
                free(read_buffer);
                return FAT_ERR_DEVICE_ERROR;
            }                                                      
        } else {
            // subdirectory or FAT32 root
            if(current_cluster == 0 || fat_is_eoc(volume, current_cluster)){
                free(read_buffer);
                return FAT_ERR_NOT_FOUND;
            }

            sector = fat_cluster_to_sector(volume, current_cluster);
            sectors_to_read = volume->sectors_per_cluster;
            entries_in_buffer = entries_per_cluster;

            int result = volume->device->read_sectors(volume->device->device_data,
                                                      sector, sectors_to_read,
                                                      read_buffer);

            if(result != 0){
                free(read_buffer);
                return FAT_ERR_DEVICE_ERROR;
            }
        }

        // process entries
        uint32_t buffer_entry_idx = entry_idx % entries_in_buffer;

        for(uint32_t i = buffer_entry_idx; i< entries_in_buffer; i++){
            if(is_root_fat12 && entry_idx>=max_root_entries){
                break; // FAT12/16 root directory is of fixed size
            }

            fat_dir_entry_t *current_entry = (fat_dir_entry_t *)&read_buffer[i * 32];

            if(current_entry->name[0] == FAT_DIR_ENTRY_FREE){
                free(read_buffer);
                return FAT_ERR_NOT_FOUND;
            }

            if(current_entry->name[0] == FAT_DIR_ENTRY_DELETED){
                entry_idx++;
                continue;
            }

            // skip LFN for now
            if(current_entry->attr == FAT_ATTR_LONG_NAME){
                entry_idx++;
                continue;
            }

            if(current_entry->attr & FAT_ATTR_VOLUME_ID){
                entry_idx++;
                continue;
            }

            // check if short name matches
            if(fat_compare_short_name(current_entry->name, name)){
                memcpy(entry, current_entry, sizeof(fat_dir_entry_t));
                if(entry_index){
                    *entry_index = entry_idx;
                }
                free(read_buffer);
                return FAT_OK;
            }

            // check if long filename matches
            if(entry_idx > 0){
                char long_filename[256];
                uint8_t checksum = fat_calculate_lfn_checksum(current_entry->name);
                uint32_t lfn_entry_idx = entry_idx;

                fat_error_t lfn_err = fat_read_lfn_sequence(volume, 
                                                            dir_cluster, 
                                                            &lfn_entry_idx, 
                                                            long_filename, 
                                                            sizeof(long_filename), 
                                                            checksum);

                if(lfn_err == FAT_OK){
                    // compare searchname with long filename (case insensitive)
                    if(strlen(name) == strlen(long_filename)){
                        bool match = true;
                        for(size_t j=0; j<strlen(name); j++){
                            if(tolower(name[j]) != tolower(long_filename[j])){
                                match = false;
                                break;
                            }
                        }

                        if(match){
                            memcpy(entry, current_entry, sizeof(fat_dir_entry_t));
                            if(entry_index){
                                *entry_index = entry_idx;
                            }
                            free(read_buffer);
                            return FAT_OK;
                        }
                    }
                }                                                            
            }

            entry_idx++;
        }

        // next cluster
        if(!is_root_fat12){
            fat_error_t err = fat_get_next_cluster (volume, current_cluster, 
                                                    &current_cluster);

            if(err != FAT_OK){
                free(read_buffer);
                return err;
            }
        }

        // update entry index
        if(is_root_fat12){
            entry_idx = ((entry_idx / entries_per_sector) + 1) * entries_per_sector;
        } else {
            entry_idx = ((entry_idx / entries_per_cluster) + 1) * entries_per_cluster;
        }
    }
}

fat_error_t fat_iterate_directory(fat_volume_t *volume, cluster_t dir_cluster,
                                  fat_dir_iterator_callback callback, void *user_data){


    // parameter validation
    if(!volume || !callback){
        return FAT_ERR_INVALID_PARAM;
    }

    uint32_t current_cluster = dir_cluster;
    uint32_t entry_idx = 0;
    bool is_root_fat12 = (dir_cluster == 0 && volume->type != FAT_TYPE_FAT32);

    uint32_t entries_per_cluster = volume->bytes_per_cluster / 32;
    uint32_t entries_per_sector = volume->bytes_per_sector / 32;
    uint32_t max_root_entries = is_root_fat12 ? volume->root_entry_count : 0;
    uint32_t root_start_sector = 0;

    if(is_root_fat12){
        root_start_sector = volume->reserved_sector_count +
                            (volume->num_fats * volume->fat_size_sectors);
    }

    uint8_t *read_buffer = malloc(volume->bytes_per_cluster);
    if(!read_buffer){
        return FAT_ERR_NO_MEMORY;
    }

    while(1){
        uint32_t sector;
        uint32_t sectors_to_read;
        uint32_t entries_in_buffer;

        if(is_root_fat12){
            if(entry_idx>=max_root_entries){
                break; // Fat12/16 root directory is of fixed size 
            }

            sector = root_start_sector + (entry_idx / entries_per_sector);
            sectors_to_read = 1;
            entries_in_buffer = entries_per_sector;

            int result = volume->device->read_sectors(volume->device->device_data, sector,
                                                      sectors_to_read, read_buffer);
            
            if(result != 0){
                free(read_buffer);
                return FAT_ERR_DEVICE_ERROR;
            }
        } else {
            if(current_cluster == 0 || fat_is_eoc(volume, current_cluster)){
                break;
            }

            sector = fat_cluster_to_sector(volume, current_cluster);
            sectors_to_read = volume->sectors_per_cluster;
            entries_in_buffer = entries_per_cluster;

            int result = volume->device->read_sectors(volume->device->device_data,
                                                      sector, sectors_to_read, read_buffer);

            if(result!=0){
                free(read_buffer);
                return FAT_ERR_DEVICE_ERROR;
            }                                                      
        }

        uint32_t buffer_entry_idx = entry_idx % entries_in_buffer;

        for(uint32_t i = buffer_entry_idx; i<entries_in_buffer; i++){
            if(is_root_fat12 && entry_idx >= max_root_entries){
                break;
            }
            fat_dir_entry_t *current_entry = (fat_dir_entry_t *)&read_buffer[i * 32];

            // check for end of directory
            if(current_entry->name[0] == FAT_DIR_ENTRY_FREE){
                free(read_buffer);
                return FAT_OK;
            }

            if(current_entry->name[0] == FAT_DIR_ENTRY_DELETED){
                entry_idx++;
                continue;
            }

            if(current_entry->attr == FAT_ATTR_LONG_NAME){
                entry_idx++;
                continue;
            }

            if(current_entry->attr & FAT_ATTR_VOLUME_ID){
                entry_idx++;
                continue;
            }

            char *long_name = NULL;
            char long_filename[256];

            if(entry_idx > 0){
                uint8_t checksum = fat_calculate_lfn_checksum(current_entry->name);
                uint32_t lfn_entry_idx = entry_idx;

                fat_error_t lfn_err = fat_read_lfn_sequence(volume, dir_cluster, 
                                                            &lfn_entry_idx,
                                                            long_filename,
                                                            sizeof(long_filename),
                                                            checksum);

                if(lfn_err == FAT_OK){
                    long_name = long_filename;
                }
            }

            // call the callback 
            fat_error_t err = callback(current_entry, long_name, entry_idx, user_data);
            if(err != FAT_OK){
                free(read_buffer);
                return err; // callback requested stop
            }
            entry_idx++;
        }

        // move to next cluster
        if(!is_root_fat12){
            fat_error_t err = fat_get_next_cluster(volume, current_cluster, &current_cluster);
            if(err != FAT_OK){
                free(read_buffer);
                return err;
            }
        }

        if(is_root_fat12){
            entry_idx = ((entry_idx / entries_per_sector) + 1) * entries_per_sector;
        } else {
            entry_idx = ((entry_idx / entries_per_cluster) + 1) * entries_per_cluster;
        }
    }

    free(read_buffer);
    return FAT_OK;
}

fat_error_t fat_find_free_entry(fat_volume_t *volume, cluster_t dir_cluster, 
                                uint32_t num_entries, uint32_t *entry_index){

    // parameter validation
    if(!volume || num_entries == 0 || !entry_index){
        return FAT_ERR_INVALID_PARAM;
    }

    uint32_t current_cluster = dir_cluster;
    uint32_t entry_idx = 0;
    uint32_t consecutive_free = 0;
    uint32_t first_free_idx = 0;
    bool is_root_fat12 = (dir_cluster == 0 && volume->type != FAT_TYPE_FAT32);

    uint32_t entries_per_cluster = volume->bytes_per_cluster / 32;
    uint32_t entries_per_sector = volume->bytes_per_sector / 32;
    uint32_t max_root_entries = is_root_fat12 ? volume->root_entry_count : 0;
    uint32_t root_start_sector = 0;

    if(is_root_fat12){
        root_start_sector = volume->reserved_sector_count +
                            (volume->num_fats * volume->fat_size_sectors);
    }

    uint8_t *read_buffer = malloc(volume->bytes_per_cluster);
    if(!read_buffer){
        return FAT_ERR_NO_MEMORY;
    }

    while(1){
        uint32_t sector;
        uint32_t sectors_to_read;
        uint32_t entries_in_buffer;

        if(is_root_fat12){
            if(entry_idx >= max_root_entries){
                // FAT12/16 root directory has fixed size
                free(read_buffer);
                return FAT_ERR_DISK_FULL;
            }

            sector = root_start_sector + (entry_idx / entries_per_sector);
            sectors_to_read = 1;
            entries_in_buffer = entries_per_sector;

            int result = volume->device->read_sectors(volume->device->device_data,
                                                      sector, sectors_to_read, read_buffer);

            if(result != 0){
                free(read_buffer);
                return FAT_ERR_DEVICE_ERROR;
            }
        } else {
            if(current_cluster == 0 || fat_is_eoc(volume, current_cluster)){
                // need to allocate new cluster for the directory
                // TODO implement directory growth by allocatng new clusters
                free(read_buffer);
                return FAT_ERR_DISK_FULL;
            }

            sector = fat_cluster_to_sector(volume, current_cluster);
            sectors_to_read = volume->sectors_per_cluster;
            entries_in_buffer = entries_per_cluster;

            int result = volume->device->read_sectors(volume->device->device_data,
                                                      sector, sectors_to_read, read_buffer);
            
            if(result != 0){
                free(read_buffer);
                return FAT_ERR_DEVICE_ERROR;
            }
        }

        uint32_t buffer_entry_idx = entry_idx % entries_in_buffer;

        for(uint32_t i = buffer_entry_idx; i<entries_in_buffer; i++){
            if(is_root_fat12 && entry_idx >= max_root_entries){
                break;
            }

            fat_dir_entry_t *current_entry = (fat_dir_entry_t *)&read_buffer[i * 32];
            
            // check if entry is free
            if(current_entry->name[0] == FAT_DIR_ENTRY_FREE || 
               current_entry->name[0] == FAT_DIR_ENTRY_DELETED){
                if(consecutive_free == 0){
                    first_free_idx = entry_idx;
                }
                consecutive_free++;

                // check if enough consecutive free entries exist
                if(consecutive_free >= num_entries){
                    *entry_index = first_free_idx;
                    free(read_buffer);
                    return FAT_OK;
                } else {
                    consecutive_free = 0;
                }
                entry_idx++;
            }

            // move to next cluster
            if(!is_root_fat12){
                fat_error_t err = fat_get_next_cluster(volume, current_cluster, &current_cluster);
                if(err != FAT_OK){
                    free(read_buffer);
                    return err;
                }
            }

            if(is_root_fat12){
                entry_idx = ((entry_idx / entries_per_sector) + 1) * entries_per_sector;
            } else {
                entry_idx = ((entry_idx / entries_per_cluster) + 1) * entries_per_cluster;
            }
        }
    }
}