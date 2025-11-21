// ASSUMPTION: LITTLE ENDIAN ARCHITECTURE - see notes

#include "fat_table.h"
#include <string.h>

// validate cluster number
static inline bool is_valid_cluster(fat_volume_t *volume, cluster_t cluster){
    return (cluster>= FAT_FIRST_VALID_CLUSTER && 
            cluster < (FAT_FIRST_VALID_CLUSTER + volume->total_clusters));
}


fat_error_t fat_read_entry(fat_volume_t *volume, cluster_t cluster, uint32_t *value){

    // parameter validation
    if(!volume || !value){
        return FAT_ERR_INVALID_PARAM;
    }

    if(!is_valid_cluster(volume, cluster)){
        return FAT_ERR_INVALID_CLUSTER;
    }

    // read FAT entry
    switch(volume->type){
        case FAT_TYPE_FAT12: {
            
            uint32_t byte_offset = (cluster * 3) / 2;

            // read 16-bits to ensure the entire entry is read
            uint16_t entry = *(uint16_t*)(&volume->fat_cache[byte_offset]);

            if(cluster & 1) {
                *value = entry >> 4;        // odd cluster: read upper 12 bits
            } else {
                *value = entry & 0x0FFF;    // even cluster: read lower 12 bits
            }

            break;
        }
        case FAT_TYPE_FAT16: {

            uint32_t byte_offset = cluster*2;
            *value = *(uint16_t*)(&volume->fat_cache[byte_offset]);
            
            break;
        }
        case FAT_TYPE_FAT32: {

            uint32_t byte_offset = cluster*4;
            uint32_t raw_value = *(uint32_t*)(&volume->fat_cache[byte_offset]);
            *value = raw_value & 0x0FFFFFFF;

            break;
        }
        default:
            return FAT_ERR_UNSUPPORTED_FAT_TYPE;
    }

    return FAT_OK;
}

fat_error_t fat_write_entry(fat_volume_t *volume, cluster_t cluster, uint32_t value){

    // parameter validation
    if(!volume){
        return FAT_ERR_INVALID_PARAM;
    }

    if(!is_valid_cluster(volume, cluster)){
        return FAT_ERR_INVALID_CLUSTER;
    }

    // write FAT entry
    switch(volume->type){
        case FAT_TYPE_FAT12: {

            uint32_t byte_offset = (cluster*3)/2;

            // read 16-bits to ensure the entire entry is read
            uint16_t entry = *(uint16_t*)(&volume->fat_cache[byte_offset]);
            value &=0x0FFF;

            if(cluster & 1) {
                entry = (entry & 0x0FFF)|(value << 4);  // odd cluster: update upper 12 bits
            } else {
                entry = (entry & 0xF000)|value;         // even cluster: update lower 12 bits
            }

            *(uint16_t*)(&volume->fat_cache[byte_offset]) = entry;

            break;
        }
        case FAT_TYPE_FAT16: {

            uint32_t byte_offset = cluster*2;
            value &= 0xFFFF;
            *(uint16_t*)(&volume->fat_cache[byte_offset]) = (uint16_t)value;

            break;
        }
        case FAT_TYPE_FAT32: {

            uint32_t byte_offset = cluster*4;
            uint32_t current_entry = *(uint32_t*)(&volume->fat_cache[byte_offset]);
            value &= 0x0FFFFFFF;
            uint32_t new_entry = (current_entry & 0x0FFFFFFF)|value;
            *(uint32_t*)(&volume->fat_cache[byte_offset]) = new_entry;

            break;
        }
        default:
            return FAT_ERR_UNSUPPORTED_FAT_TYPE;

    }

    volume->fat_dirty = true;

    return FAT_OK;
}