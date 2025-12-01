#include "fat_file.h"
#include "fat_path.h"
#include "fat_dir_search.h"
#include "fat_lfn.h"
#include "fat_cluster.h"
#include "fat_table.h"
#include "fat_types.h"
#include "fat_root.h"
#include "fat_volume.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <stdio.h>


bool fat_validate_filename(const char *filename){

    // parameter validation
    if(!filename || strlen(filename) == 0){
        return false;
    }

    size_t len = strlen(filename);
    if(len > 255){
        return false;
    }

    const char *invalid_chars = "<>:\"|?*";
    for(size_t i=0; i<len; i++){
        char c = filename[i];

        // check for control characters
        if(c<32){
            return false;
        }

        // check for invalid chars
        if(strchr(invalid_chars, c) != NULL){
            return false;
        }
    }

    // check for reserved names (case-insensitive)
    const char *reserved[] = {
        "CON", "PRN", "AUX", "NUL", 
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };
 
    char base_name[256];
    const char *dot = strchr(filename, '.');
    if(dot){
        size_t base_len = dot - filename;
        if(base_len >= sizeof(base_name)){
            base_len = sizeof(base_name) - 1;
        }
        memcpy(base_name, filename, base_len);
        base_name[base_len] = '\0';
    } else {
        strcpy(base_name, filename);
    }

    // check base name against reserved names
    for(size_t i=0; i<sizeof(reserved) / sizeof(reserved[0]); i++){
        if(strcmp(base_name, reserved[i]) == 0){
            return false;
        }
    }

    return true;
}

uint32_t fat_calculate_entries_needed(const char *filename){

    // parameter validation
    if(!filename){
        return 1;
    }

    size_t len = strlen(filename);

    // check if filename fits 8.3 
    if(len<= 12 && strchr(filename, ' ') == NULL){
        const char *dot = strchr(filename, '.');
        if(!dot || (dot == strchr(filename, '.') && 
                   (dot - filename) <= 8 && 
                   strlen(dot+1) <= 3)) {

            bool is_perfect_83 = true;

            // check if all characters are valid 8.3
            for(size_t i=0; i<len; i++){
                char c = filename[i];
                if(c != '.' && !isalnum(c) && c != '_' && c != '-'){
                    is_perfect_83 = false;
                    break;
                }
            }

            if(is_perfect_83){
                return 1;
            }
        }
    }

    // create LFN if name is not perfect 8.3
    uint32_t lfn_entries = (len + 12) / 13;
    return lfn_entries + 1;
}

fat_error_t fat_generate_short_name(const char *long_name, uint8_t *short_name, 
                                    fat_volume_t *volume, cluster_t parent_cluster){

    // parameter validation
    if(!long_name || !short_name) {
        return FAT_ERR_INVALID_PARAM;
    }

    memset(short_name, ' ', 11);

    // find last dot
    const char *last_dot = strchr(long_name, '.');
    const char *name_part = long_name;
    const char *ext_part = NULL;
    size_t name_len = strlen(long_name);

    if(last_dot && last_dot != long_name){
        name_len = last_dot - long_name;
        ext_part = last_dot + 1;
    }

    // create base name
    char base_name[9] = {0};
    size_t base_pos = 0;

    // process name
    for(size_t i=0; i<name_len && base_pos<8; i++){
        char c = toupper(name_part[i]);

        if(c == ' ' || c == '.'){
            continue;
        }

        if(c == '+') c = '_';
        if(c == ',') c = '_';
        if(c == ';') c = '_';
        if(c == '=') c = '_';
        if(c == '[') c = '_';
        if(c == ']') c = '_';

        if(isalnum(c) || c == '_' || c == '-' || c == '$' || c == '%' ||
            c == '\'' || c == '@' || c == '~' || c == '`' || c == '!' ||
            c == '('  || c == ')' || c == '{' || c == '}' || c == '^' ||
            c == '#'  || c == '&'){
            short_name[base_pos++] = c;
        }
    }

    // default name
    if(base_pos == 0){
        strcpy(base_name, "NONAME");
        base_pos = 6;
    }

    // process extension
    char ext_name[4] = {4};
    if(ext_part){
        size_t ext_pos = 0;
        size_t ext_len = strlen(ext_part);

        for(size_t i=0; i<ext_len && ext_pos<3; i++){
            char c = toupper(ext_part[i]);

            if(isalnum(c) || c == '_' || c == '-'){
                short_name[8 + ext_pos++] = c;
            }
        }
    }

    // check for name collision
    for(int suffix=1; suffix<=999999; suffix++){
        
        // build test name
        memset(short_name, ' ', 11);

        if(suffix == 1){
            memcpy(short_name, base_name, strlen(base_name));
        } else {
        
            // name collision - add suffix
            char suffix_str[8];
            snprintf(suffix_str, sizeof(suffix_str), "~%d", suffix);

            // calculate how much of the original name to keep
            size_t suffix_len = strlen(suffix_str);
            size_t keep_len = (8 > suffix_len) ? (8 - suffix_len) : 0;
            size_t copy_len = (base_pos < keep_len) ? base_pos : keep_len;

            // copy truncated base name + suffix
            memcpy(short_name, base_name, copy_len);
            memcpy(&short_name[copy_len], suffix_str, suffix_len);
        }

        // copy extension
        if(strlen(ext_name)>0){
            memcpy(&short_name[8], ext_name, strlen(ext_name));
        }

        // test if name already exists
        fat_dir_entry_t existing_entry;
        uint32_t entry_index;

        fat_error_t err = fat_find_entry(volume, parent_cluster, (char *)short_name, 
                                         &existing_entry, &entry_index);
        if(err == FAT_ERR_NOT_FOUND){
            // name is unique
            return FAT_OK;
        } else if(err != FAT_OK){
            // other error
            return err;
        }
        // name collision - try next number for suffix
    }
    // could not find a unique name
    return FAT_ERR_ALREADY_EXISTS;
}

fat_error_t fat_initialize_file_cluster(fat_volume_t *volume, cluster_t cluster){

    // parameter validation
    if(!volume || cluster<2){
        return FAT_ERR_INVALID_PARAM;
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

    fat_error_t err = fat_write_entry(volume, cluster, eoc_marker);
    if(err != FAT_OK){
        return err;
    }

    // zero cluster data
    uint8_t *zero_buffer = calloc(1, volume->bytes_per_cluster);
    if(zero_buffer){
        uint32_t first_sector = fat_cluster_to_sector(volume, cluster);
        int result = volume->device->write_sectors (volume->device->device_data,
                                                    first_sector,
                                                    volume->sectors_per_cluster,
                                                    zero_buffer);
        free(zero_buffer);

        if(result != 0){
            // zeroing failed, cluster still allocated -> not critical
        }
    }
    return FAT_OK;

}

fat_error_t fat_create_directory_entries(fat_volume_t *volume, cluster_t parent_cluster,
                                         uint32_t entry_index, const char *filename,
                                         const uint8_t *short_name, cluster_t file_cluster,
                                         uint8_t attributes){

    // paramater validation
    if(!volume || !filename || !short_name){
        return FAT_ERR_INVALID_PARAM;
    }

    uint32_t entries_needed = fat_calculate_entries_needed(filename);
    uint32_t current_index = entry_index;

    // create LFN if necessary
    if(entries_needed > 1){
        uint32_t lfn_entries = entries_needed - 1;
        fat_lfn_entry_t *lfn_array = malloc(lfn_entries * sizeof(fat_lfn_entry_t));
        if(!lfn_array){
            return FAT_ERR_NO_MEMORY;
        }

        uint8_t num_lfn_entries;
        fat_error_t err = fat_create_lfn_entries(filename, short_name, lfn_array, 
                                                 &num_lfn_entries);
        if(err != FAT_OK){
            free(lfn_array);
            return err;
        }

        // write LFN
        for(uint32_t i=0; i<num_lfn_entries; i++){
            uint32_t sector, offset;

            if(parent_cluster==0 && volume->type != FAT_TYPE_FAT32){
                //FAET12/16 root
                uint32_t entries_per_sector = volume->bytes_per_sector / 32;
                uint32_t root_start = volume->reserved_sector_count + 
                                      (volume->num_fats * volume->fat_size_sectors);
                sector = root_start + (current_index / entries_per_sector);
                offset = (current_index & entries_per_sector) * 32;
            } else {
                // FAT32 or subdirectory
                uint32_t entries_per_cluster = volume->bytes_per_cluster / 32;
                uint32_t cluster_index = current_index / entries_per_cluster;

                // walk cluster chain
                cluster_t target_cluster = parent_cluster;
                for(uint32_t j=0; j<cluster_index; j++){
                    err = fat_get_next_cluster(volume, target_cluster, &target_cluster);
                    if(err != FAT_OK){
                        free(lfn_array);
                        return err;
                    }
                }
                sector = fat_cluster_to_sector(volume, target_cluster);
                offset = (current_index % entries_per_cluster) * 32;
            }
            // write LFN 
            err=fat_write_dir_entry(volume, sector, offset, 
                                    (const fat_dir_entry_t *)&lfn_array[i]);
            if(err != FAT_OK){
                free(lfn_array);
                return err;
            }

            current_index++;
        }
        free(lfn_array);
    }

    fat_dir_entry_t dir_entry;
    memset(&dir_entry, 0, sizeof(dir_entry));

    memcpy(dir_entry.name, short_name, 11);

    dir_entry.attr = attributes;

    // set timestamps
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    if(tm_info){
        uint16_t fat_time = ((tm_info->tm_hour & 0x1F) << 11)|
                            ((tm_info->tm_min & 0x3F) << 5) |
                            ((tm_info->tm_sec / 2) & 0x1F);
        uint16_t fat_date = (((tm_info->tm_year - 80) & 0x7F) << 9)|
                            (((tm_info->tm_mon + 1) &0x0F) << 5)|
                            (tm_info->tm_mday & 0x1F);

        dir_entry.create_time = fat_time;
        dir_entry.create_date = fat_date;
        dir_entry.write_time = fat_time;
        dir_entry.write_date = fat_date;
        dir_entry.access_date = fat_date;
    }

    // set cluster and size
    fat_set_entry_cluster(volume, &dir_entry, file_cluster);
    dir_entry.file_size = 0;

    uint32_t sector, offset;
    if(parent_cluster == 0 && volume->type != FAT_TYPE_FAT32){
        // FAT12/16 root
        uint32_t entries_per_sector = volume->bytes_per_sector / 32;
        uint32_t root_start = volume->reserved_sector_count +
                             (volume->num_fats * volume->fat_size_sectors); 
        sector = root_start + (current_index / entries_per_sector);
        offset = (current_index % entries_per_sector) * 32;
    } else {
        // FAT32 or subdirectory
        uint32_t entries_per_cluster = volume->bytes_per_cluster / 32;
        uint32_t cluster_index = current_index / entries_per_cluster;

        cluster_t target_cluster = parent_cluster;
        for(uint32_t i=0; i<cluster_index; i++){
            fat_error_t err = fat_get_next_cluster(volume, target_cluster, 
                                                   &target_cluster);
            if(err != FAT_OK){
                return err;
            }
        }

        sector = fat_cluster_to_sector(volume, target_cluster);
        offset = (current_index % entries_per_cluster) * 32;
    }
    return fat_write_dir_entry(volume, sector, offset, &dir_entry);
}

fat_error_t fat_create(fat_volume_t *volume, const char *path, uint8_t attributes, 
                       fat_file_t **file){

    // parameter validation
    if(!volume || !path || !file){
        return FAT_ERR_INVALID_PARAM;
    }

    *file = NULL;
    
    // get parent directory
    char *path_copy = malloc(strlen(path) + 1);
    if(!path_copy){
        return FAT_ERR_NO_MEMORY;
    }
    strcpy(path_copy, path);

    // separate filename and parent directory
    char *last_slash = strrchr(path_copy, '/');
    char *filename;
    char *parent_path;

    if(last_slash){
        *last_slash = '\0';
        parent_path = path_copy;
        filename = last_slash + 1;

        if(strlen(parent_path) == 0){
            // parent directory is root
            parent_path = "/";
        }
    } else {
        // current directory is root
        parent_path = "/";
        filename = path_copy;
    }

    if(!fat_validate_filename(filename)){
        free(path_copy);
        return FAT_ERR_INVALID_PARAM;
    }

    // check if file already exists
    fat_dir_entry_t existing_entry;
    uint32_t existing_index;
    fat_error_t err = fat_resolve_path(volume, path, &existing_entry, NULL, 
                                       &existing_index);
    if(err == FAT_OK){
        free(path_copy);
        return FAT_ERR_ALREADY_EXISTS;
    } else if(err != FAT_ERR_NOT_FOUND) {
        // other error
        free(path_copy);
        return err;
    }

    fat_dir_entry_t parent_entry;
    cluster_t parent_cluster;
    err = fat_resolve_path(volume, parent_path, &parent_entry, &parent_cluster, 
                           NULL);
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

    uint32_t entries_needed = fat_calculate_entries_needed(filename);

    // find free directory entry slots
    uint32_t entry_index;
    err = fat_find_free_entry(volume, parent_dir_cluster, entries_needed, 
                              &entry_index);
    if(err != FAT_OK){
        free(path_copy);
        return err;
    }

    // allocate first cluster
    cluster_t file_cluster;
    err = fat_allocate_cluster(volume, &file_cluster);
    if(err != FAT_OK){
        // drive full
        free(path_copy);
        return err;
    }

    // initialise allocated cluster
    err = fat_initialize_file_cluster(volume, file_cluster);
    if(err != FAT_OK){
        fat_write_entry(volume, file_cluster, FAT_FREE);
        free(path_copy);
        return err;
    }

    // generate 8.3 name
    uint8_t short_name[11];
    err = fat_generate_short_name(filename, short_name, volume, 
                                  parent_dir_cluster);
    if(err != FAT_OK){
        fat_write_entry(volume, file_cluster, FAT_FREE);
        free(path_copy);
        return err;
    }

    err = fat_create_directory_entries(volume, parent_dir_cluster, entry_index, 
                                       filename, short_name, file_cluster, attributes);
    if(err != FAT_OK){
        fat_write_entry(volume, file_cluster, FAT_FREE);
        free(path_copy);
        return err;
    }

    fat_file_t *new_file = malloc(sizeof(fat_file_t));
    if(!new_file){
        free(path_copy);
        return FAT_ERR_NO_MEMORY;
    }

    // init file handle
    memset(new_file, 0, sizeof(fat_file_t));
    new_file->volume = volume;
    new_file->current_cluster = file_cluster;
    new_file->position = 0;
    new_file->cluster_offset = 0;
    new_file->dir_cluster = parent_dir_cluster;
    new_file->dir_entry_offset = entry_index + entries_needed - 1;
    new_file->flags = FAT_O_RDWR;
    new_file->modified = false;

    memcpy(new_file->dir_entry.name, short_name, 11);
    new_file->dir_entry.attr = attributes;
    new_file->dir_entry.file_size = 0;
    fat_set_entry_cluster(volume, &new_file->dir_entry, file_cluster);

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    if(tm_info){
        uint16_t fat_time = ((tm_info->tm_hour & 0x1F) << 11) |
                            ((tm_info->tm_min & 0x3F) << 5) |
                            ((tm_info->tm_sec / 2) & 0x1F);
        uint16_t fat_date = (((tm_info->tm_year - 80) & 0x7F) << 9) |
                            (((tm_info->tm_mon + 1) & 0x0F) << 5) |
                            (tm_info->tm_mday & 0x1F);
        new_file->dir_entry.create_time = fat_time;
        new_file->dir_entry.create_date = fat_date;
        new_file->dir_entry.write_time = fat_time;
        new_file->dir_entry.write_date = fat_date;
        new_file->dir_entry.access_date = fat_date;
    }

    free(path_copy);
    *file = new_file;
    return FAT_OK;
}