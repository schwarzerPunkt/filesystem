#ifndef FAT_FILE_DELETE_H
#define FAT_FILE_DELETE_H

bool fat_validate_delete_permissions(const fat_dir_entry_t *entry);

fat_error_t fat_update_free_cluster_count(fat_volume_t *volume, 
                                          uint32_t clusters_freed);

fat_error_t fat_find_lfn_entries(fat_volume_t *volume, cluster_t parent_cluster, 
                                uint32_t entry_index, uint32_t *lfn_start_index, 
                                uint32_t *lfn_count);

fat_error_t fat_delete_directory_entries(fat_volume_t *volume, cluster_t parent_cluster, 
                                         uint32_t entry_index, bool has_lfn);

fat_error_t fat_delete_file_clusters(fat_volume_t *volume, cluster_t start_cluster);

fat_error_t fat_unlink(fat_volume_t *volume, const char *path);
#endif