#ifndef FAT_DIR_H
#define FAT_DIR_H

#include "fat_types.h"
#include "fat_volume.h"
#include <stdint.h>


// directory entry structure
typedef struct __attribute__((packed)){

    // filename: name 8 byte, extension 3 byte
    uint8_t name[11];
    
    /* attributes
     * 0x01: read-only
     * 0x02: hidden
     * 0x04: system
     * 0x08: volume label
     * 0x10: directory
     * 0x20: archive
     * 0x0F long filename entry
     */
    uint8_t attr;

    /* NT reserved
     * bit 3: lowercase extension
     * bit 4: lowercase basename
     */
    uint8_t nt_reserved;

    // fine grained creation time in 10ms (0-199)
    uint8_t create_time_tenth;

    /* creation time
     * bits 0-4: seconds / 2 (0-29)
     * bits 5-10: minutes (0-59)
     * bits 10-15: hours (0-23)
     */
    uint16_t create_time;

    /* creation date
     * bits 0-4: day (1-31)
     * bits 5-8: month (1-12)
     * bits 9-15: year from 1980 (0-127 = 1980 - 2107)
     */
    uint16_t create_date;

    // last access date - see creation date
    uint16_t access_date;

    // first cluster high - upper 16 bits of starting cluster (FAT12/16 always 0)
    uint16_t first_cluster_high;

    // last modification time - see creation time
    uint16_t write_time;

    // last modification date - see creation date
    uint16_t write_date;

    // first cluster low - lower 16 bits of starting cluster
    uint16_t first_cluster_low;

    // file size - directories: 0
    uint32_t file_size;
} fat_dir_entry_t;

// long filename structure
typedef struct __attribute__((packed)){

    /** sequence number
     * first entry: 0x41 bit set
     * second entry: 0x42 bit set
     * ... 
     * last entry: 0x40 bit set
     * deleted entry: 0xE5 bit set
     */
    uint8_t order;
    
    // name part 1 - first 5 unicode characters (UTF-16LE)
    uint8_t name1[10];

    // attributes - always 0x0F for LFN entries
    uint8_t attr;

    // type - always 0 (reserved)
    uint8_t type;

    // checksum - checksum of corresponding short name, validates LFN belongs to shor entry
    uint8_t checksum;

    // name part 2 - next 6 unicode characters
    uint8_t name2[12];

    // first cluster low - always 0 for LFN entries
    uint16_t first_cluster_low;

    // name part 3 - last 2 unicode characters
    uint8_t name3[4];
} fat_lfn_entry_t;

fat_error_t fat_read_dir_entry (fat_volume_t *volume, uint32_t sector, 
                                uint32_t offset, fat_dir_entry_t *entry);
fat_error_t fat_write_dir_entry(fat_volume_t *volume, uint32_t sector,
                                uint32_t offset, const fat_dir_entry_t *entry);
cluster_t fat_get_entry_cluster(fat_volume_t *volume, const fat_dir_entry_t *entry);
void fat_set_entry_cluster(fat_volume_t *volume, fat_dir_entry_t *entry, cluster_t cluster);
#endif