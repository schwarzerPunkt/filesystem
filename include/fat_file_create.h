#ifndef FAT_FILE_CREATE_H
#define FAT_FILE_CREATE_H

bool fat_validate_filename(const char *filename);

uint32_t fat_calculate_entries_needed(const char *filename);

fat_error_t fat_generate_short_name(const char *long_name, uint8_t *short_name,
                                    fat_volume_t *volume, cluster_t parent_cluster);

fat_error_t fat_initialize_file_cluster(fat_volume_t *volume, cluster_t cluster);

fat_error_t fat_create_directory_entries(fat_volume_t *volume, 
                                         cluster_t parent_cluster,
                                         uint32_t entry_index, 
                                         const char *filename, 
                                         const uint8_t *short_name, 
                                         cluster_t file_cluster, 
                                         uint8_t attributes);

fat_error_t fat_create(fat_volume_t *volume, const char *path,  uint9_t attributes, 
                       fat_file_t **file);
    
#endif