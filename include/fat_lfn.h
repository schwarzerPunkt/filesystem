#ifndef FAT_LFN_H
#define FAT_LFN_H

#include "fat_types.h"
#include "fat_volume.h"
#include "fat_dir.h"
#include <stdlib.h>

fat_error_t fat_parse_lfn(const fat_lfn_entry_t *lfn_entry, uint16_t *name_buffer, 
                          uint8_t *chars_written);

uint8_t fat_calculate_lfn_checksum(const uint8_t *short_name);

fat_error_t fat_read_lfn_sequence(fat_volume_t *volume, uint32_t dir_cluster, 
                                  uint32_t *entry_index, char *filename_buffer, 
                                  size_t buffer_size, uint8_t expected_checksum);

fat_error_t fat_create_lfn_entries (const char *long_name, const uint8_t *short_name,
                                    fat_lfn_entry_t *lfn_entries, uint8_t *num_entries);

#endif