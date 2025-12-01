#include "fat_path.h"
#include "fat_dir_search.h"
#include "fat_root.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

bool fat_validate_component(const char *component){
    
    // parameter validation
    if(!component || strlen(component) == 0){
        return false;
    }


    // 255 characters is max length for LFN
    if(strlen(component) > 255){
        return false;
    }

    // special entries
    if(strcmp(component, ".") == 0 || strcmp(component, "..") == 0){
        return true;
    }

    // check for invalid characters
    const char *invalid_chars = "<>:\"|?*";
    for(size_t i = 0; i<strlen(component); i++){
        char c = component[i];

        // control characters (0-31)
        if(c<32){
            return false;
        }

        // check agains invalid characters
        if(strchr(invalid_chars, c) != NULL) {
            return false;
        }
    }

    return true;
}

fat_error_t fat_split_path(const char *path, char ***components, uint32_t *num_components){
    
    // parameter validation
    if(!path || !components || !num_components){
        return FAT_ERR_INVALID_PARAM;
    }

    *components = NULL;
    *num_components = 0;

    // for empty paths or root path - no components
    if(strlen(path) == 0 || strcmp(path, "/") == 0){
        return FAT_OK;
    }

    char *path_copy = malloc(strlen(path)+1);
    if(!path_copy){
        return FAT_ERR_NO_MEMORY;
    }
    strcpy(path_copy, path);

    // count components (for memory allocation)
    uint32_t count = 0;
    char *temp_copy = malloc(strlen(path) + 1);
    if(!temp_copy){
        free(path_copy);
        return FAT_ERR_NO_MEMORY;
    }
    strcpy(temp_copy, path);

    char *token = strtok(temp_copy, "/");
    while(token != NULL){
        if(strlen(token) > 0){
            count++;
        }
        token = strtok(NULL, "/");
    }
    free(temp_copy);

    if(count == 0){
        free(path_copy);
        return FAT_OK;  // no components
    }

    char **comp_array = malloc(count * sizeof(char*));
    if(!comp_array){
        free(path_copy);
        return FAT_ERR_NO_MEMORY;
    }

    uint32_t index = 0;
    token = strtok(path_copy, "/");
    while(token != NULL && index < count){
        if(strlen(token) > 0){
            
            // validate component
            if(!fat_validate_component(token)){
                for(uint32_t i = 0; i<index; i++){
                    free(comp_array[i]);
                }
                free(comp_array);
                free(path_copy);
                return FAT_ERR_INVALID_PARAM;
            }

            // copy component
            comp_array[index] = malloc(strlen(token) + 1);
            if(!comp_array[index]){
                for(uint32_t i = 0; i<index; i++){
                    free(comp_array[i]);
                }
                free(comp_array);
                free(path_copy);
                return FAT_ERR_NO_MEMORY;
            }
            strcpy(comp_array[index], token);
            index++;
        }
        token = strtok(NULL, "/");
    }
    free(path_copy);
    *components = comp_array;
    *num_components = count;
    return FAT_OK;
}

void fat_free_path_components(char **components, uint32_t num_components){
    
    // parameter validation
    if(!components){
        return;
    }

    for(uint32_t i = 0; i<num_components; i++){
        if(components[i]){
            free(components[i]);
        }
    }
    free(components);
}

fat_error_t fat_find_in_directory(fat_volume_t *volume, cluster_t dir_cluster, 
                                  const char *component, fat_dir_entry_t *entry, 
                                  uint32_t *entry_index){

    // parameter validation
    if(!volume || !component || !entry){
        return FAT_ERR_INVALID_PARAM;
    }

    // "." - current directory
    if(strcmp(component, ".") == 0){
        // create entry for the current directory
        memset(entry, 0, sizeof(fat_dir_entry_t));
        memcpy(entry->name, ".          ", 11);
        entry->attr = FAT_ATTR_DIRECTORY;
        fat_set_entry_cluster(volume, entry, dir_cluster);

        if(entry_index){
            *entry_index = 0;   // "." is the first entry
        }
        return FAT_OK;
    }

    // ".." - parent directory
    if(strcmp(component, "..") == 0){
        // root directory
        cluster_t parent_cluster;
        if(dir_cluster == fat_get_root_dir_cluster(volume)){
            parent_cluster = fat_get_root_dir_cluster(volume);
        } else {
            // TODO full implementation - must track parent directories 
            // or search for ".." in current directory
            return fat_find_entry(volume, dir_cluster, "..", entry, entry_index);
        }

        // create entry for parent directory
        memset(entry, 0, sizeof(fat_dir_entry_t));
        memcpy(entry->name, "..         ", 11);
        entry->attr = FAT_ATTR_DIRECTORY;
        fat_set_entry_cluster(volume, entry, parent_cluster);

        if(entry_index){
            *entry_index = 1;   // ".." is the second entry
        }
        return FAT_OK;
    }

    // regular component
    return fat_find_entry(volume, dir_cluster, component, entry, entry_index);
}

fat_error_t fat_resolve_path(fat_volume_t *volume, const char *path, fat_dir_entry_t *entry, 
                             cluster_t *parent_cluster, uint32_t *entry_index){

    // parameter validation
    if(!volume || !path || !entry){
        return FAT_ERR_INVALID_PARAM;
    }

    char **components;
    uint32_t num_components;
    fat_error_t err = fat_split_path(path, &components, &num_components);
    if(err != FAT_OK){
        return err;
    }

    // root directory
    if(num_components == 0) {
        memset(entry, 0, sizeof(fat_dir_entry_t));
        memcpy(entry->name, "ROOT       ", 11);
        entry->attr = FAT_ATTR_DIRECTORY;
        fat_set_entry_cluster(volume, entry, fat_get_root_dir_cluster(volume));

        if(parent_cluster){
            *parent_cluster = fat_get_root_dir_cluster(volume);
        }

        if(entry_index){
            *entry_index = 0;
        }
        
        fat_free_path_components(components, num_components);
        return FAT_OK;
    }

    // start navigation from root directory
    cluster_t current_cluster = fat_get_root_dir_cluster(volume);
    cluster_t prev_cluster = current_cluster;
    fat_dir_entry_t current_entry;
    uint32_t current_index = 0;

    // navigate through each component
    for(uint32_t i = 0; i<num_components; i++){
        const char *component = components[i];

        // find component in current directory
        err = fat_find_in_directory(volume, current_cluster, component, 
                                    &current_entry, &current_index);
        
        if(err != FAT_OK){
            fat_free_path_components(components, num_components);
            return err;
        }

        // if this is not the last component - it must be a directory
        if(i < num_components - 1){
            if(!(current_entry.attr & FAT_ATTR_DIRECTORY)){
                fat_free_path_components(components, num_components);
                return FAT_ERR_NOT_A_DIRECTORY;
            }

            prev_cluster = current_cluster;
            current_cluster = fat_get_entry_cluster(volume, &current_entry);
        } else {
            // final component - entry found
            prev_cluster = current_cluster;
        }
    }

    memcpy(entry, &current_entry, sizeof(fat_dir_entry_t));
    if(parent_cluster){
        *parent_cluster = prev_cluster;
    }

    if(entry_index){
        *entry_index = current_index;
    }

    fat_free_path_components(components, num_components);
    return FAT_OK;
}