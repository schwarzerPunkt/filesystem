#include "fat_dir.h"
#include <string.h>
#include <stdlib.h>

fat_error_t fat_read_dir_entry (fat_volume_t *volume, uint32_t sector,
                                uint32_t offset, fat_dir_entry_t *entry){
    
    // parameter validation
    if(!volume || !entry){
        return FAT_ERR_INVALID_PARAM;
    }

    if(offset % 32 != 0){
        return FAT_ERR_INVALID_PARAM;
    }

    if(offset >= volume->bytes_per_sector){
        return FAT_ERR_INVALID_PARAM;
    }

    // read sector
    uint8_t *sector_buffer = (uint8_t*)malloc(volume->bytes_per_sector);
    if(!sector_buffer){
        return FAT_ERR_NO_MEMORY;
    }

    int result = volume->device->read_sectors(volume->device->device_data, 
                                              sector, 1, sector_buffer);
    
    if(result != 0){
        free(sector_buffer);
        return FAT_ERR_DEVICE_ERROR;
    }

    // copy 32 bit entry from sector
    memcpy(entry, &sector_buffer[offset], sizeof(fat_dir_entry_t));

    free(sector_buffer);
    
    return FAT_OK;
}

fat_error_t fat_write_dir_entry(fat_volume_t *volume, uint32_t sector, 
                                uint32_t offset, const fat_dir_entry_t *entry){
    
    // parameter validation
    if(!volume || !entry){
        return FAT_ERR_INVALID_PARAM;
    }

    if(offset % 32 != 0){
        return FAT_ERR_INVALID_PARAM;
    }

    if(offset >= volume->bytes_per_sector){
        return FAT_ERR_INVALID_PARAM;
    }

    // read-modify-write
    uint8_t *sector_buffer = (uint8_t*)malloc(volume->bytes_per_sector);
    if(!sector_buffer){
        return FAT_ERR_NO_MEMORY;
    }

    // read current sector
    int result = volume->device->read_sectors(volume->device->device_data,
                                              sector, 1, sector_buffer);
    if(result != 0){
        free(sector_buffer);
        return FAT_ERR_DEVICE_ERROR;
    }

    // modify the entry
    memcpy(&sector_buffer[offset], entry, sizeof(fat_dir_entry_t));

    // write sector to drive
    result = volume->device->write_sectors(volume->device->device_data,
                                            sector, 1, sector_buffer);
    if(result != 0){
        free(sector_buffer);
        return FAT_ERR_DEVICE_ERROR;
    }

    free(sector_buffer);
    return FAT_OK;
}

cluster_t fat_get_entry_cluster(fat_volume_t *volume, const fat_dir_entry_t *entry){

    // parameter validation
    if(!volume || !entry){
        return 0;
    }

    if(volume->type == FAT_TYPE_FAT32){

        // FAT32
        uint32_t cluster = ((uint32_t)entry->first_cluster_high << 16) |
                            entry->first_cluster_low;
        return cluster;
    } else {
        // FAT12/16
        return entry->first_cluster_low;
    }
}

void fat_set_entry_cluster(fat_volume_t *volume, fat_dir_entry_t *entry, cluster_t cluster){

    // parameter validation
    if(!volume || !entry){
        return;
    }

    entry->first_cluster_low = (uint16_t)(cluster & 0xFFFF);

    if(volume->type == FAT_TYPE_FAT32){
        entry->first_cluster_high = (uint16_t)((cluster >> 16) & 0xFFFF);
    } else {
        entry->first_cluster_high = 0;
    }
}