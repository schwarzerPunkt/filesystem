#include "fat_file.h"
#include "fat_path.h"
#include "fat_dir_search.h"
#include "fat_cluster.h"
#include "fat_table.h"
#include "fat_lfn.h"
#include "fat_root.h"
#include <string.h>
#include <stdlib.h>

bool fat_validate_delete_permissions(const fat_dir_entry_t *entry){

    // permission validation
    if(!entry){
        return false;
    }

    if(entry->attr & FAT_ATTR_READ_ONLY){
        return false;
    }

    if(entry->attr & FAT_ATTR_DIRECTORY){
        return false;
    }

    if(entry->attr & FAT_ATTR_VOLUME_ID){
        return false;
    }

    return true;
}

fat_error_t fat_update_free_cluster_count(fat_volume_t *volume, uint32_t clusters_freed){

    // parameter validation
    if(!volume || volume->type != FAT_TYPE_FAT32){
        return FAT_OK;
    }

    /* TODO: implement FS info sector updates 
    1. read FS info sector
    2. update free cluster count
    3. write sector back
    */
    clusters_freed = clusters_freed;

    return FAT_OK;
}

fat_error_t fat_find_lfn_entries(fat_volume_t *volume, cluster_t parent_cluster, 
                                uint32_t entry_index, uint32_t *lfn_start_index, 
                                uint32_t *lfn_count){

    // parameter validation
    if(!volume || !lfn_start_index || !lfn_count){
        return FAT_ERR_INVALID_PARAM;
    }

    *lfn_start_index = entry_index;
    *lfn_count = 0;

    if(entry_index == 0){
        return FAT_OK;
    }

    fat_dir_entry_t main_entry;
    uint32_t sector, offset;

    // calculate sector and offset for main entry
    bool is_root_fat12 = (parent_cluster == 0 && volume->type != FAT_TYPE_FAT32);

    if(is_root_fat12){
        // FAT12/16
        uint32_t entries_per_sector = volume->bytes_per_sector / 32;
        uint32_t root_start = volume->reserved_sector_count +
                              (volume->num_fats * volume->fat_size_sectors);
        sector = root_start + (entry_index / entries_per_sector);
        offset = (entry_index % entries_per_sector) * 32;
    } else {
        // FAT32
        uint32_t entries_per_cluster = volume->bytes_per_cluster / 32;
        uint32_t cluster_index = entry_index / entries_per_cluster;

        cluster_t target_cluster = parent_cluster;
        for(uint32_t i=0; i<cluster_index; i++){
            fat_error_t err = fat_get_next_cluster(volume, target_cluster, 
                                                   &target_cluster);
            if(err != FAT_OK){
                return err;
            }
        }

        sector = fat_cluster_to_sector(volume, target_cluster);
        offset = (entry_index % entries_per_cluster) * 32;
    }

    fat_error_t err = fat_read_dir_entry(volume, sector, offset, &main_entry);
    if(err != FAT_OK){
        return err;
    }

    uint8_t expected_checksum = fat_calculate_lfn_checksum(main_entry.name);

    uint32_t current_index = entry_index;
    uint32_t lfn_entries_found = 0;

    while(current_index > 0){
        current_index--;

        fat_lfn_entry_t lfn_entry;

        if(is_root_fat12){
            uint32_t entries_per_sector = volume->bytes_per_sector / 32;
            uint32_t root_start = volume->reserved_sector_count +
                                  (volume->num_fats * volume->fat_size_sectors);
            sector = root_start + (current_index / entries_per_sector);
            offset = (current_index % entries_per_sector) * 32;
        } else {
            uint32_t entries_per_cluster = volume->bytes_per_cluster / 32;
            uint32_t cluster_index = current_index / entries_per_cluster;

            cluster_t target_cluster = parent_cluster;
            for(uint32_t i=0; i<cluster_index; i++){
                err = fat_get_next_cluster(volume, target_cluster, &target_cluster);
                if(err != FAT_OK){
                    break;
                }
            }

            sector = fat_cluster_to_sector(volume, target_cluster);
            offset = (current_index % entries_per_cluster) * 32;
        }

        err = fat_read_dir_entry(volume, sector, offset, (fat_dir_entry_t*)&lfn_entry);
        if(err != FAT_OK){
            break;
        }

        if(lfn_entry.attr != FAT_ATTR_LONG_NAME){
            break;
        }

        // check checksum to dettermine whether LFN belongs to file
        if(lfn_entry.checksum != expected_checksum){
            break;
        }

        lfn_entries_found++;

        if(lfn_entry.order & 0x40){
            // found first LFN entry
            *lfn_start_index = current_index;
            *lfn_count = lfn_entries_found;
            return FAT_OK;
        }
    }

    // no complete sequence of LFN entries found - possible corruption
    if(lfn_entries_found > 0){
        *lfn_start_index = current_index + 1;
        *lfn_count = lfn_entries_found;
    }

    return FAT_OK;
}

fat_error_t fat_delete_directory_entries(fat_volume_t *volume, cluster_t parent_cluster, 
                                         uint32_t entry_index, bool has_lfn){

    // parameter validation
    if(!volume){
        return FAT_ERR_INVALID_PARAM;
    }

    fat_error_t result = FAT_OK;

    if(has_lfn){
        uint32_t lfn_start_index, lfn_count;
        fat_error_t err = fat_find_lfn_entries(volume, parent_cluster, entry_index, 
                                               &lfn_start_index, &lfn_count);
        if(err == FAT_OK && lfn_count > 0){
            for(uint32_t i=0; i<lfn_count; i++){
                uint32_t lfn_index = lfn_start_index + i;

                // read entry
                fat_dir_entry_t entry;
                uint32_t sector, offset;
                bool is_root_fat12 = (parent_cluster == 0 && 
                                      volume->type != FAT_TYPE_FAT32);
                if(is_root_fat12){
                    // FAT12/16
                    uint32_t entries_per_sector = volume->bytes_per_sector / 32;
                    uint32_t root_start = volume->reserved_sector_count + 
                                          (volume->num_fats * volume->fat_size_sectors);
                    sector = root_start + (lfn_index / entries_per_sector);
                    offset = (lfn_index % entries_per_sector) * 32;
                } else {
                    // FAT32
                    uint32_t entries_per_cluster = volume->bytes_per_cluster / 32;
                    uint32_t cluster_index = lfn_index / entries_per_cluster;

                    cluster_t target_cluster = parent_cluster;
                    for(uint32_t j=0; j<cluster_index; j++){
                        err = fat_get_next_cluster(volume, target_cluster, &target_cluster);
                        if(err != FAT_OK){
                            return err;
                        }
                    }

                    sector = fat_cluster_to_sector(volume, target_cluster);
                    offset = (lfn_index % entries_per_cluster) * 32;
                }

                err = fat_read_dir_entry(volume, sector, offset, &entry);
                if(err != FAT_OK){
                    if(result == FAT_OK){
                        result = err;
                    }
                    continue;
                }
                // mark entry as deleted
                entry.name[0] = FAT_DIR_ENTRY_DELETED;

                err = fat_write_dir_entry(volume, sector, offset, &entry);
                if(err != FAT_OK && result == FAT_OK){
                    result = err;
                }
            }
        }
    }

    // delete main directory entry
    fat_dir_entry_t main_entry;
    uint32_t sector, offset;
    bool is_root_fat12 = (parent_cluster == 0 && volume->type != FAT_TYPE_FAT32);

    if(is_root_fat12){
        uint32_t entries_per_sector = volume->bytes_per_sector / 32;
        uint32_t root_start = volume->reserved_sector_count + 
                              (volume->num_fats * volume->fat_size_sectors);
        sector = root_start + (entry_index / entries_per_sector);
        offset = (entry_index % entries_per_sector) * 32;
    } else {
        uint32_t entries_per_cluster = volume->bytes_per_cluster / 32;
        uint32_t cluster_index = entry_index / entries_per_cluster;
        cluster_t target_cluster = parent_cluster;
        for(uint32_t i=0; i<cluster_index; i++){
            fat_error_t err = fat_get_next_cluster(volume, target_cluster, 
                                                   &target_cluster);
            if(err != FAT_OK){
                return err;
            }
        }

        sector = fat_cluster_to_sector(volume,target_cluster);
        offset = (entry_index % entries_per_cluster) * 32;
    }

    fat_error_t err = fat_read_dir_entry(volume, sector, offset, &main_entry);
    if(err != FAT_OK){
        return (result != FAT_OK) ? result : err;
    }

    // set main entry to 0xE5
    main_entry.name[0] = FAT_DIR_ENTRY_DELETED;

    err = fat_write_dir_entry(volume, sector, offset, &main_entry);
    if(err != FAT_OK && result == FAT_OK){
        result = err;
    }

    return result;
}

fat_error_t fat_delete_file_clusters(fat_volume_t *volume, cluster_t start_cluster){

    // parameter validation
    if(!volume || start_cluster < 2){
        return FAT_ERR_INVALID_PARAM;
    }

    cluster_t current_cluster = start_cluster;
    uint32_t clusters_freed = 0;

    while(current_cluster >= 2 && current_cluster < volume->total_clusters + 2){

        // read next cluster
        cluster_t next_cluster;
        fat_error_t err = fat_get_next_cluster(volume, current_cluster, &next_cluster);
        if(err != FAT_OK){
            // cannot read next cluster
            next_cluster = 0;
        }

        // free current cluster
        err = fat_write_entry(volume, current_cluster, FAT_FREE);
        if(err != FAT_OK){
            // failed to free current cluster - continue
        } else {
            clusters_freed++;
        }

        if(fat_is_eoc(volume, next_cluster) || next_cluster < 2){
            break;
        }

        current_cluster = next_cluster;

        if(clusters_freed > volume->total_clusters){
            // loop - corrupted chain 
            break;
        }
    }

    if(clusters_freed > 0){
        fat_update_free_cluster_count(volume, clusters_freed);
    }

    return FAT_OK;
}

fat_error_t fat_unlink(fat_volume_t *volume, const char *path){

    // parameter validation
    if(!volume || !path){
        return FAT_ERR_INVALID_PARAM;
    }

    fat_dir_entry_t file_entry;
    cluster_t parent_cluster;
    uint32_t entry_index;

    fat_error_t err = fat_resolve_path(volume, path, &file_entry, &parent_cluster, 
                                       &entry_index);
    if(err != FAT_OK){
        // file not found
        return err;
    }

    if(!fat_validate_delete_permissions(&file_entry)){
        return FAT_ERR_READ_ONLY;
    }

    cluster_t start_cluster = fat_get_entry_cluster(volume, &file_entry);

    if(start_cluster >= 2){
        err = fat_delete_file_clusters(volume, start_cluster);
        if(err != FAT_OK){
            // freeing cluster failed - continue
        }
    }

    bool has_lfn = true;

    err = fat_delete_directory_entries(volume, parent_cluster, entry_index, has_lfn);
    if(err != FAT_OK){
        return err;
    }

    err = fat_flush(volume);
    if(err != FAT_OK){
        return err;
    }

    return FAT_OK;
}