#include "fat_cluster.h"
#include "fat_table.h"

fat_error_t fat_get_next_cluster(fat_volume_t *volume, cluster_t cluster, cluster_t *next_cluster){

    // parameter validation
    if(!volume || !next_cluster){
        return FAT_ERR_INVALID_PARAM;
    }

    // read FAT entry
    uint32_t value;
    fat_error_t err = fat_read_entry(volume, cluster, &value);
    if(err != FAT_OK){
        return err;
    }

    // return next cluster - caller must check for EOC, bad etc.
    *next_cluster = value;
    
    return FAT_OK;
}

bool fat_is_eoc(fat_volume_t *volume, uint32_t value){

    switch(volume->type){
        case FAT_TYPE_FAT12: 
            return value >= FAT12_EOC;
        case FAT_TYPE_FAT16:
            return value >= FAT16_EOC;
        case FAT_TYPE_FAT32:
            return value >= FAT32_EOC;
        default:
            return false;
    }
}

bool fat_is_bad(fat_volume_t *volume, uint32_t value){

    switch(volume->type){
        case FAT_TYPE_FAT12:
            return value == FAT12_BAD;
        case FAT_TYPE_FAT16:
            return value == FAT16_BAD;
        case FAT_TYPE_FAT32:
            return value == FAT32_BAD;
        default:
            return false;
    }
}

fat_error_t fat_allocate_cluster(fat_volume_t *volume, cluster_t *cluster){

    // parameter validation
    if(!volume || !cluster){
        return FAT_ERR_INVALID_PARAM;
    }

    // search for free cluster
    cluster_t FAT_LAST_VALID_CLUSTER = FAT_FIRST_VALID_CLUSTER + volume->total_clusters;
    
    for(cluster_t current_cluster = FAT_FIRST_VALID_CLUSTER; current_cluster < FAT_LAST_VALID_CLUSTER; current_cluster++){
        
        uint32_t value;
        fat_error_t err = fat_read_entry(volume, current_cluster, &value);
        if(err != FAT_OK){
            continue;
        }

        if(value != FAT_FREE){

            // allocate cluster: mark as EOC
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

            // write EOC marker to FAT
            err = fat_write_entry(volume, current_cluster, eoc_marker);
            if(err != FAT_OK){
                return err;
            }

            // return allocated cluster
            *cluster = current_cluster;
            return FAT_OK;
        }
    }

    // no free cluster found
    return FAT_ERR_DISK_FULL;
}

fat_error_t fat_free_chain(fat_volume_t *volume, cluster_t start_cluster){

    // parameter validation
    if(!volume){
        return FAT_ERR_INVALID_PARAM;
    }

    if (start_cluster < FAT_FIRST_VALID_CLUSTER || 
        start_cluster >= FAT_FIRST_VALID_CLUSTER + volume->total_clusters){
        return FAT_ERR_INVALID_CLUSTER;
    }
    
    // follow cluster chain and free each cluster
    cluster_t current_cluster = start_cluster;
    uint32_t max_iterations = volume->total_clusters;
    uint32_t iterations = 0;

    while(iterations < max_iterations){
        iterations++;

        // read next cluster
        uint32_t next_cluster;
        fat_error_t err = fat_read_entry(volume, current_cluster, &next_cluster);
        if(err != FAT_OK){
            return err;
        }

        // free current cluster
        err = fat_write_entry(volume, current_cluster, FAT_FREE);
        if(err != FAT_OK){
            return err;
        }

        // check for EOC
        if(fat_is_eoc(volume, next_cluster)){
            return FAT_OK;
        }

        // check for bad cluster
        if(fat_is_bad(volume, next_cluster)){
            return FAT_OK;
        }

        // validate next cluster in [first_valid_cluster, last_valid_cluster]
        if (next_cluster < FAT_FIRST_VALID_CLUSTER ||
            next_cluster >= FAT_FIRST_VALID_CLUSTER + volume->total_clusters){
                return FAT_ERR_CORRUPTED;
        }

        current_cluster = next_cluster;
    }
    // too many iterations, possibly a cycle
    return FAT_ERR_CORRUPTED;
}

fat_error_t fat_validate_chain(fat_volume_t *volume, cluster_t start_cluster){

    // parameter validation
    if(!volume){
        return FAT_ERR_INVALID_PARAM;
    }

    if (start_cluster < FAT_FIRST_VALID_CLUSTER ||
        start_cluster >= FAT_FIRST_VALID_CLUSTER + volume->total_clusters){
            return FAT_ERR_INVALID_CLUSTER;
    }

    cluster_t tortoise = start_cluster;
    cluster_t hare = start_cluster;

    // traverse cluster chain to check if a cycle exists
    while(true){

        // move slow pointer        
        uint32_t tortoise_next;
        fat_error_t err = fat_read_entry(volume, tortoise, &tortoise_next);
        if(err != FAT_OK){
            return err;
        }

        if(fat_is_eoc(volume, tortoise_next)){
            return FAT_OK;
        }

        if (tortoise_next < FAT_FIRST_VALID_CLUSTER ||
            tortoise_next >= FAT_FIRST_VALID_CLUSTER + volume->total_clusters){
                return FAT_ERR_CORRUPTED;
        }

        tortoise = tortoise_next;

        // move fast pointer
        uint32_t hare_next;
        err = fat_read_entry(volume, hare, &hare_next);
        if(err != FAT_OK){
            return err;
        }

        if(fat_is_eoc(volume, hare_next)){
            return FAT_OK;
        }

        if (hare_next < FAT_FIRST_VALID_CLUSTER ||
            hare_next >= FAT_FIRST_VALID_CLUSTER + volume->total_clusters){
            return FAT_ERR_CORRUPTED;
        }

        hare = hare_next;
        
        // second step
        err = fat_read_entry(volume, hare, &hare_next);
        if(err != FAT_OK){
            return err;
        }

        if(fat_is_eoc(volume, hare_next)){
            return FAT_OK;
        }

        if (hare_next < FAT_FIRST_VALID_CLUSTER ||
            hare_next >= FAT_FIRST_VALID_CLUSTER + volume->total_clusters){
            return FAT_OK;
        }

        hare = hare_next;

        // check if the hare and the tortoise meet
        if(tortoise == hare){
            return FAT_ERR_CORRUPTED;
        }
    }

    return FAT_OK;
}