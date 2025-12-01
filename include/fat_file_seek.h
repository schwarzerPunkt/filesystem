#ifndef FAT_FILE_SEEK_H
#define FAT_FILE_SEEK_H

#include "fat_file.h"

bool fat_validate_seek_parameters(fat_file_t *file, int32_t offset, int whence);

fat_error_t fat_calculate_target_position(fat_file_t *file, int32_t offset, 
                                          int whence, uint32_t *target_position);

fat_error_t fat_optimize_cluster_seek(fat_file_t *file, uint32_t target_position);

fat_error_t fat_seek_to_position(fat_file_t *file, uint32_t target_position);

fat_error_t fat_seek(fat_file_t *file, int32_t offset, int whence);

uint32_t fat_tell(fat_file_t *file);

#endif