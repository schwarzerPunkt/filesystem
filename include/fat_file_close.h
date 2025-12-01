#ifndef FAT_FILE_CLOSE_H
#define FAT_FILE_CLOSE_H

#include "fat_file.h"

bool fat_validate_file_handle(fat_file_t *file);

fat_error_t fat_calculate_directory_entry_location(fat_file_t *file, uint32_t *sector, 
                                                   uint32_t *offset);

fat_error_t fat_update_directory_entry(fat_file_t *file, const fat_dir_entry_t *entry);

fat_error_t fat_flush_file_data(fat_file_t *file);

fat_error_t fat_close(fat_file_t *file);
#endif