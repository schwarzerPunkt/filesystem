#include "fat_lfn.h"
#include "fat_dir.h"
#include "fat_cluster.h"
#include <string.h>
#include <stdlib.h>

fat_error_t fat_parse_lfn ( const fat_lfn_entry_t *lfn_entry, uint16_t *name_buffer, 
                            uint8_t *chars_written){

    // parameter validation
    if (!lfn_entry || !name_buffer || !chars_written){
        return FAT_ERR_INVALID_PARAM;
    }

    if (lfn_entry->attr != FAT_ATTR_LONG_NAME){
        return FAT_ERR_INVALID_PARAM;
    }

    uint8_t count = 0;
    uint16_t ch;

    // extract 5 characters from name1
    for (int i = 0; i < 10; i +=2){
        // read little endian 16-bit character
        ch = lfn_entry->name1[i] | (lfn_entry->name1[i+1] << 8);
        // check for null-terminator and padding
        if (ch==0x0000 || ch== 0xFFFF){
            *chars_written = count;
            return FAT_OK;
        }

        name_buffer[count++] = ch;
    }

    // extract 6 characters from name2
    for (int i = 0; i < 12; i+=2){
        ch = lfn_entry->name2[i] | (lfn_entry->name2[i+1] << 8);

        if (ch == 0x0000 || ch == 0xFFFF){
            *chars_written = count;
            return FAT_OK;
        }

        name_buffer[count++] = ch;
    }

    // extract 2 characters from name3
    for (int i = 0; i < 4; i+=2){
        ch = lfn_entry->name3[i] | (lfn_entry->name3[i+1] << 8);

        if (ch==0x0000 || ch==0xFFFF){
            *chars_written = count;
            return FAT_OK;
        }

        name_buffer[count++] = ch;
    }

    *chars_written = count;
    return FAT_OK;
}

uint8_t fat_calculate_lfn_checksum(const uint8_t *short_name){
    
    // parameter validation
    if(!short_name){
        return 0;
    }

    uint8_t checksum = 0;

    for (int i = 0; i < 11; i++){
        checksum = ((checksum & 1) ? 0x80 : 0) + (checksum >> 1) + short_name[i];
    }

    return checksum;
}

fat_error_t fat_read_lfn_sequence(fat_volume_t *volume, uint32_t dir_cluster,
                                  uint32_t *entry_index, char *filename_buffer,
                                  size_t buffer_size, uint8_t expected_checksum){
    
    // parameter validation
    if (!volume || !entry_index || !filename_buffer || buffer_size == 0){
        return FAT_ERR_INVALID_PARAM;
    }

    uint16_t utf16_buffer[256];
    int utf16_length = 0;

    uint32_t current_index = *entry_index;
    uint8_t expected_order = 1;
    bool found_first = false;

    bool is_root_fat1216 = (dir_cluster == 0 && volume->type != FAT_TYPE_FAT32);
    uint32_t entries_per_cluster = volume->bytes_per_cluster / 32;

    // read backwards through directory entries
    while(current_index > 0){
        current_index--;

        uint32_t sector;
        uint32_t entry_offset;

        if(is_root_fat1216){
            // FAT12/16 root directory (fixed region)
            uint32_t entries_per_sector = volume->bytes_per_sector / 32;
            uint32_t root_start = volume->reserved_sector_count +
                                    (volume->num_fats * volume->fat_size_sectors);
            sector = root_start + (current_index / entries_per_sector);
            entry_offset = (current_index  % entries_per_sector);
        } else {
            // FAT32 root directory / subdirectory
            uint32_t cluster_index = current_index / entries_per_cluster;
            entry_offset = (current_index % entries_per_cluster);
        
            // walk cluster chain
            uint32_t target_cluster = dir_cluster;
            for(uint32_t i=0; i<cluster_index; i++){
                fat_error_t err = fat_get_next_cluster(volume, target_cluster, &target_cluster);
                if(err != FAT_OK || fat_is_eoc(volume, target_cluster)){
                    return FAT_ERR_CORRUPTED;
                }
            }

            // convert cluster to sector
            sector = volume->data_begin_sector + 
                        ((target_cluster -2) * volume->sectors_per_cluster);
            sector += entry_offset / volume->bytes_per_sector;
            entry_offset = entry_offset % volume->bytes_per_sector;
        }

        // read directory entry
        fat_lfn_entry_t lfn_entry;        
        fat_error_t err = fat_read_dir_entry(volume, sector, entry_offset, 
                                            (fat_dir_entry_t*)&lfn_entry);

        if(err != FAT_OK){
            return err;
        }
        
        // check if entry is LFN
        if(lfn_entry.attr != FAT_ATTR_LONG_NAME){
            break;
        }

        // validate checksum
        if(lfn_entry.checksum != expected_checksum){
            return FAT_ERR_CORRUPTED;
        }
        
        // check if this is the first LFN entry
        if(lfn_entry.order & 0x40){
            found_first = true;
            expected_order = lfn_entry.order & 0x3F; // remove bit 0x40
        }

        // validate sequence order
        if((lfn_entry.order & 0x3F) != expected_order){
            return FAT_ERR_CORRUPTED;
        }

        uint16_t entry_chars[13];
        uint8_t chars_read;
        err = fat_parse_lfn(&lfn_entry, entry_chars, &chars_read);
        if(err != FAT_OK){
            return err;
        }

        // prepend characters to buffer - shift existing characters to the right
        memmove(&utf16_buffer[chars_read], utf16_buffer, 
                utf16_length * sizeof(uint16_t));
        memcpy(utf16_buffer, entry_chars, chars_read * sizeof(uint16_t));
        utf16_length += chars_read;
        
        expected_order--;

        if(expected_order == 0){
            break;
        }
    }

    if (!found_first){
        return FAT_ERR_CORRUPTED;
    }

    // convert UTF-16LE to UTF-8/ASCII
    size_t out_pos = 0;
    for(int i = 0; i < utf16_length && out_pos < buffer_size - 1; i++){
        if(utf16_buffer[i] < 0x80){
            // ASCI range - direct conversion
            filename_buffer[out_pos++] = (char)utf16_buffer[i];
        } else {
            // TODO proper UTF16 to UTF-8 conversion
            filename_buffer[out_pos++] = '?';
        }
    }

    filename_buffer[out_pos] = '\0';

    *entry_index = current_index;
    return FAT_OK;
}

fat_error_t fat_create_lfn_entries (const char *long_name, const uint8_t *short_name, 
                                    fat_lfn_entry_t *lfn_entries, uint8_t *num_entries){

    // parameter validation
    if(!long_name || !short_name || !lfn_entries || !num_entries){
        return FAT_ERR_INVALID_PARAM;
    }

    // convert UTF-8/ASCII to UTF-16LE
    uint16_t utf16_name[256];
    size_t name_len = strlen(long_name);

    if(name_len > 255){
        return FAT_ERR_INVALID_PARAM;
    }

    // simple ASCII to UTF-16LE conversion
    for(size_t i = 0; i < name_len; i++){
        utf16_name[i] = (uint16_t)(unsigned char)long_name[i];
    }

    uint8_t checksum = fat_calculate_lfn_checksum(short_name);

    // calculate number of LFN entries needed
    uint8_t entries_needed = (name_len + 12) / 13; // round up
    *num_entries = entries_needed;

    // create entries in reverse order
    for(uint8_t entry_num = 0; entry_num < entries_needed ; entry_num++){
        fat_lfn_entry_t *entry = &lfn_entries[entry_num];
        memset(entry, 0, sizeof(fat_lfn_entry_t));

        // set order field
        uint8_t order = entries_needed - entry_num;
        if(entry_num == 0){
            order |= 0x40;
        }
        entry->order = order;

        // set attributes, type, checksum, first cluster low
        entry->attr = FAT_ATTR_LONG_NAME;
        entry->type = 0;
        entry->checksum = checksum;
        entry->first_cluster_low = 0;

        // calculate character range
        size_t start_char = entry_num * 13;
        bool terminated = false;

        // fill name1
        for(int i=0; i<10; i+=2){
            uint16_t ch;
            size_t char_idx = start_char + (i/2);
            if(!terminated && char_idx < name_len){
                ch = utf16_name[char_idx++];
            } else if (!terminated){
                ch = 0x0000;
                terminated = true;
            } else {
                ch = 0xFFFF;
            }
            entry->name1[i] = ch & 0xFF;
            entry->name1[i+1] = (ch >> 8) & 0xFF;
        }

        // fill name2
        for(int i=0; i<12; i+=2){
            uint16_t ch;
            size_t char_idx = start_char + 5 + (i/2);
            if(!terminated && char_idx < name_len){
                ch = utf16_name[char_idx++];
            } else if(!terminated){
                ch = 0x0000;
                terminated = true;
            } else {
                ch = 0xFFFF;
            }
            entry->name2[i] = ch & 0xFF;
            entry->name2[i+1] = (ch >> 8) & 0xFF;
        }

        // fill name3
        for(int i=0; i<4; i+=2){
            uint16_t ch;
            size_t char_idx = start_char + 11 + (i/2);
            if(!terminated && char_idx < name_len){
                ch = utf16_name[char_idx++];
            } else if(!terminated){
                ch = 0x0000;
                terminated = true;
            } else {
                ch = 0xFFFF;
            }
            entry->name3[i] = ch & 0xFF;
            entry->name3[i+1] = (ch >> 8) & 0xFF;
        }
    }

    return FAT_OK;
}