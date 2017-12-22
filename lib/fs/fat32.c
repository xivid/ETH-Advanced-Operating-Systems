#include <ctype.h> // include for isspace for example
#include <fs/fat32.h>
#include <aos/aos.h>
#include "../../usr/drivers/omap44xx/mmchs/mmchs.h"

#define BLOCK_SIZE 512
static struct fat32_mount *singleton;
static bool singleton_set;
errval_t load_cluster(size_t number, char* cluster);
errval_t get_entry(size_t number, uint32_t* entry);
errval_t find_dirent(char* name, struct fat32_handle* handle);
errval_t load_next_dir(struct fat32_handle *handle, char** name, struct fat32_handle* result);
errval_t load_cluster_from_handle(struct fat32_handle* handle, uint32_t number);
errval_t load_next_cluster_from_handle(struct fat32_handle* handle);
void extract_name(struct fat32_dirent* dirent, char** name);
errval_t handle_copy(struct fat32_handle* orig, struct fat32_handle* copy);
errval_t move_cluster_number(uint32_t old_num, size_t offset, uint32_t* new_num);
errval_t resolve_path(char* path, struct fat32_handle* handle);

inline size_t cluster_size(void) {
    return singleton->BytsPerSec*singleton->SecPerClus;
}
inline bool isEOF(size_t x) {
    return x >= 0x0ffffff8;
}
inline bool usable_attr(uint8_t DIR_Attr) {
    return (DIR_Attr & 0xE);
    // 0xE is the addition of 0x02 (hidden files), 0x04 (system files), 0x08 (volume id)
    // none of these flags should be set for a file/directory which is found
}

errval_t fat32_mount(const char* path, const char* uri) {
    errval_t err;
    if (!singleton_set) {
        mmchs_init();
        singleton = malloc(sizeof(struct fat32_mount));
        if (singleton == NULL)
            return LIB_ERR_MALLOC_FAIL;
        singleton->sector0_offset = 0;
        
        // alloc sector0
        singleton->sector0 = (char *) malloc(BLOCK_SIZE);
        if (singleton->sector0 == NULL) {
            free(singleton);
            singleton = NULL;
            return LIB_ERR_MALLOC_FAIL;
        }
        // read sector0
        err = mmchs_read_block(singleton->sector0_offset, singleton->sector0);
        if (err_is_fail(err)) {
            free(singleton->sector0);
            free(singleton);
            singleton = NULL;
            DEBUG_ERR(err, "fat32_mount mmchs_read_block failed");
            return err;
        }
        if (!(singleton->sector0[0] == 0xe9 || (singleton->sector0[0] == 0xeb && singleton->sector0[2] == 0x90))) {
            free(singleton->sector0);
            free(singleton);
            singleton = NULL;
            return FAT_ERR_BAD_FS;
        }
        // read bytes per sec
        singleton->BytsPerSec = *(uint16_t *)&singleton->sector0[11];
        // read sectors per cluster
        singleton->SecPerClus = singleton->sector0[13];
        assert(singleton->SecPerClus > 0);
        // read reserved sector count
        singleton->RsvdSecCnt = *(uint16_t *)&singleton->sector0[14];
        assert(singleton->RsvdSecCnt > 0);
        // read number of fat data structures on the volumen (usually 2)
        singleton->NumFATs = singleton->sector0[16];
        assert(singleton->NumFATs > 0);
        singleton->FATSz32 = *(uint32_t *)&singleton->sector0[36];
        singleton->FirstDataSector = singleton->RsvdSecCnt + (singleton->NumFATs * singleton->FATSz32); // note: RootDirSectors is always 0 for FAT32
        // allocate root handle
        singleton->root = malloc(sizeof(struct fat32_handle));
        if (singleton->root == NULL) {
            free(singleton->sector0);
            free(singleton);
            singleton = NULL;
            return LIB_ERR_MALLOC_FAIL;
        }
        singleton->root->isdir = true;
        singleton->root->in_memory = true;
        singleton->root->first_clust = *(uint32_t *)&singleton->sector0[44];
        singleton->root->current_clust = singleton->root->first_clust;
        singleton->root->dircluster = malloc(cluster_size());
        if (singleton->root->dircluster == NULL) {
            free(singleton->sector0);
            free(singleton->root);
            free(singleton);
            singleton = NULL;
            return LIB_ERR_MALLOC_FAIL;
        }
        
        err = load_cluster(singleton->root->current_clust, (char *)singleton->root->dircluster);
        if (err_is_fail(err)) {
            free(singleton->sector0);
            free(singleton->root->dircluster);
            free(singleton->root);
            free(singleton);
            singleton = NULL;
            return err;
        }
        
        // allocate fat table
        singleton->fat = calloc(singleton->FATSz32, sizeof(singleton->fat));
        if (singleton->fat == NULL) {
            free(singleton->sector0);
            free(singleton->root->dircluster);
            free(singleton->root);
            free(singleton);
            singleton = NULL;
            return LIB_ERR_MALLOC_FAIL;
        }
        
        singleton->path = strndup(&path[path[0]==FS_PATH_SEP], 1024);
        if (singleton->path == NULL) {
            free(singleton->sector0);
            free(singleton->fat);
            free(singleton->root->dircluster);
            free(singleton->root);
            free(singleton);
            singleton = NULL;
            return LIB_ERR_MALLOC_FAIL;
        }
        
    }
    singleton_set = true;
    return SYS_ERR_OK;
}
// loads cluster "number" to cluster
// cluster has to be a pointer to 
errval_t load_cluster(size_t number, char* cluster) {
    errval_t err;
    size_t FirstSectorofCluster = singleton->sector0_offset + ((number -2) * singleton->SecPerClus) + singleton->FirstDataSector;
    for (uint32_t i = 0; i < singleton->SecPerClus; i++) {
        err = mmchs_read_block(FirstSectorofCluster+i, (void*) cluster);
        cluster += singleton->BytsPerSec;
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "load_cluster mmchs_read_block failed");
            return err;
        }
    }
    return SYS_ERR_OK;
}

errval_t move_cluster_number(uint32_t old_num, size_t offset, uint32_t* new_num) {
    errval_t err;
    uint32_t current = old_num;
    uint32_t next;
    for (uint32_t i = 0; i < offset; i++) {
        err = get_entry(current, &next);
        if (err_is_fail(err)) {
            return err;
        }
        if (isEOF(next)) {
            return FS_ERR_NOTFOUND;
        }
        current = next;
    }
    *new_num = current;
    return SYS_ERR_OK;
}

errval_t get_entry(size_t number, uint32_t* entry) {
    errval_t err;
    number = number *4; // for FAT32 this is the offset
    size_t sector_in_array = number / singleton->BytsPerSec;
    size_t offset_in_fat = (number % singleton->BytsPerSec) / 4;
    size_t sector_on_drive = singleton->sector0_offset + singleton->RsvdSecCnt + sector_in_array;
    
    if (singleton->fat[sector_in_array] == NULL) {
        // we need to load it from the sd card
        uint32_t *buffer = malloc(singleton->BytsPerSec);
        if (buffer == NULL)
            return LIB_ERR_MALLOC_FAIL;
        err = mmchs_read_block(sector_on_drive, buffer);
        if (err_is_fail(err)) {
            free(buffer);
            return err;
        }
        singleton->fat[sector_in_array] = buffer;
    }
    // set entry to the right value and only return relevant 7 bytes
    *entry = 0x0fffffff & (singleton->fat[sector_in_array][offset_in_fat]);
    return SYS_ERR_OK;
}

errval_t fat32_unmount(void) {
    if (singleton->root->in_memory) {
        free(singleton->root->dircluster);
    }
    free(singleton->sector0);
    free(singleton->root);
    for (uint32_t i = 0; i < singleton->FATSz32; i++) {
        if (singleton->fat[i] != NULL)
            free(singleton->fat[i]);
    }
    free(singleton->fat);
    free(singleton->path);
    free(singleton);
    return SYS_ERR_OK;
}

errval_t handle_copy(struct fat32_handle* orig, struct fat32_handle* copy) {
    copy->name = strdup(orig->name);
    copy->isdir = orig->isdir;
    copy->size = orig->size;
    copy->first_clust = orig->first_clust;
    copy->current_clust = orig->current_clust;
    copy->file_pos = orig->file_pos;
    copy->in_memory = orig->in_memory;
    if (orig->in_memory) {
        copy->dircluster = malloc(cluster_size());
        if (copy->dircluster == NULL)
            return LIB_ERR_MALLOC_FAIL;
        memcpy(copy->dircluster, orig->dircluster, cluster_size());
    }
    return SYS_ERR_OK;
}
void handle_close(struct fat32_handle* handle) {
    if (handle->in_memory)
        free(handle->cluster_data);
    if (handle->name != NULL)
        free(handle->name);
    free(handle);
}

// somewhat similar to resolve_path() of ramfs, space for handle has to be allocated
errval_t resolve_path(char* path, struct fat32_handle* handle) {
    errval_t err;
    size_t pos = 0;
    if (path[0]==FS_PATH_SEP)
        pos++;
    size_t path_len = strlen(singleton->path);
    // compare if the first path_len symbols of the path to open match the mount path
    // if not, or the mount path is longer than path_len, return error
    if (strncmp(singleton->path, &path[pos], path_len) != 0 || strlen(&path[pos]) < path_len) {
        return FS_ERR_NOTFOUND;
    }
    pos += path_len + 1; // compare further from after the mount path
    
    err = handle_copy(singleton->root, handle);
    if (err_is_fail(err)) {
        return err;
    }

    // almost one to one from ramfs
    while (path[pos] != '\0') {
        char *nextsep = strchr(&path[pos], FS_PATH_SEP);
        size_t nextlen;
        if (nextsep == NULL) {
            nextlen = strlen(&path[pos]);
        } else {
            nextlen = nextsep - &path[pos];
        }

        char pathbuf[nextlen + 1];
        memcpy(pathbuf, &path[pos], nextlen);
        pathbuf[nextlen] = '\0';

        err = find_dirent(pathbuf, handle);
        if (err_is_fail(err)) {
            return err;
        }

        if (!handle->isdir && nextsep != NULL) {
            return FS_ERR_NOTDIR;
        }

        if (nextsep == NULL) {
            break;
        }

        pos += nextlen + 1;
    }
    return SYS_ERR_OK;
}

// search for name in handle, returns the result in handle, similar to ramfs
errval_t find_dirent(char* name, struct fat32_handle* handle) {
    if (!handle->isdir)
        return FS_ERR_NOTDIR;
    struct fat32_handle* next_dir = malloc(sizeof(struct fat32_handle));
    if (next_dir == NULL)
        return LIB_ERR_MALLOC_FAIL;
    errval_t err = SYS_ERR_OK;
    while (!err_is_fail(err)) {
        char* next_dir_name = NULL;
        err = load_next_dir(handle, &next_dir_name, next_dir);
        if (err_is_fail(err)) {
            free(next_dir);
            break;
        }
        if (strcmp(name, next_dir_name) == 0) {
            err = handle_copy(next_dir, handle);
            if (next_dir_name != NULL)
                free(next_dir_name);
            free(next_dir);
            return err;
        }
        
        if (next_dir_name != NULL) {
            free(next_dir_name);
        }
    }
    free(next_dir);
    if (err == FS_ERR_NOTFOUND) {
        return err;
    }
    else {
        DEBUG_ERR(err, "error during find_dirent");
        return err;
    }
}

// returns the next name and next entry (via result handle) starting from handle
errval_t load_next_dir(struct fat32_handle *handle, char** name, struct fat32_handle* result) {
    if (!handle->isdir)
        return FS_ERR_NOTDIR;
    errval_t err;
    if (!handle->in_memory) {
        err = load_cluster_from_handle(handle, handle->current_clust);
        if (err_is_fail(err))
            return err;
    }
    struct fat32_dirent dirent;
    while(true) {
        if (handle->current_offset >= (cluster_size()/sizeof(struct fat32_dirent))) {
            err = load_next_cluster_from_handle(handle);
            if(err_is_fail(err))
                return err;
        }
        // make dirent available via struct
        dirent = handle->dircluster[handle->current_offset];
        
        if (dirent.DIR_Name[0] == 0x00) {
            return FS_ERR_NOTFOUND;
        }
        if (!(dirent.DIR_Name[0] == 0xe5) && usable_attr(dirent.DIR_Attr))
            break;
        handle->current_offset++;
    }
    
    if (result != NULL) {
        // initialize handle correctly
        result->size = dirent.DIR_FileSize;
        result->isdir = (dirent.DIR_Attr & 0x10);
        uint32_t DIR_FstClusHi = dirent.DIR_FstClusHi;
        DIR_FstClusHi = DIR_FstClusHi << 16;
        result->first_clust = DIR_FstClusHi | dirent.DIR_FstClusLO;
        if (result->in_memory) {
            free(result->cluster_data);
            result->in_memory = false;
            result->cluster_data = NULL;
        }
        result->current_offset = 0;
        extract_name(&dirent, &result->name);
        if (name != NULL)
            *name = strdup(result->name);
    }
    else if (name != NULL) {
        // we are not interested in the handle but only the name
        extract_name(&dirent, name);
    }
    handle->current_offset++;
    return SYS_ERR_OK;
}

// extracts the name from the directory entry and returns it in a initially unallocated char** name
void extract_name(struct fat32_dirent* dirent, char** name) {
    *name = malloc(13); // max length of 11 + implicit "." + null terminator
    size_t progress = 0;
    // first part is at most 8 characters
    while ((progress <= 7) && (!isspace(dirent->DIR_Name[progress]))) {
        progress++;
    }
    strncpy(*name, dirent->DIR_Name, progress);
    
    size_t progress2 = 0;
    while ((progress2 <= 2) && (!isspace(dirent->DIR_Extension[progress2]))) {
        progress2++;
    }
    progress2++;
    if (progress2 > 0) {
        (*name)[progress] = '.';
        strncpy(&((*name)[progress+1]), dirent->DIR_Extension, progress2);
    }
    (*name)[progress + 1 + progress2] = '\0';
}

errval_t load_cluster_from_handle(struct fat32_handle* handle, uint32_t number) {
    errval_t err;
    if (handle->in_memory && handle->current_clust == number)
        return SYS_ERR_OK;
    // data not in memory yet, allocate memory
    handle->cluster_data = malloc(cluster_size());
    if (handle->cluster_data == NULL)
        return LIB_ERR_MALLOC_FAIL;
    handle->in_memory = true;
    // load the data
    err = load_cluster(number, (char*) handle->cluster_data);
    if (err_is_fail(err))
        return err;
    handle->current_clust = number;
    handle->current_offset = 0;
    return err; // should be SYS_ERR_OK at this point
}

// loads the next cluster after the one in handle
errval_t load_next_cluster_from_handle(struct fat32_handle* handle) {
    errval_t err;
    uint32_t entry;
    err = get_entry(handle->current_clust, &entry);
    if (err_is_fail(err)) {
        return err;
    }
    if (isEOF(entry))
        return FS_ERR_NOTFOUND;
    return load_cluster_from_handle(handle, entry);
}

// opens a handle to path
errval_t fat32_open(char* path, void** hand) {
    struct fat32_handle* handle = (struct fat32_handle*) *hand;
    errval_t err;
    handle = malloc(sizeof(struct fat32_handle));
    if (handle == NULL)
        return LIB_ERR_MALLOC_FAIL;
    err = resolve_path(path, handle);
    if (err_is_fail(err)) {
        handle_close(handle);
        return err;
    }
    else if (handle->isdir) {
        handle_close(handle);
        return FS_ERR_NOTFILE;
    }
    *hand = (void*) handle;
    return SYS_ERR_OK;
    
}

// closes given handle
errval_t fat32_close(void* hand) {
    struct fat32_handle* handle = (struct fat32_handle*) hand;
    if (handle->isdir) {
        return FS_ERR_NOTFILE;
    }
    handle_close(handle);
    return SYS_ERR_OK;
}
// reads at most bytes many bytes into buffer from the handle, returns the buffer and bytes_read
errval_t fat32_read(void* hand, void* buffer, size_t bytes, size_t* bytes_read) {
    struct fat32_handle* handle = (struct fat32_handle*) hand;
    if (handle->isdir) {
        return FS_ERR_NOTFILE;
    }

    assert(handle->file_pos >= 0);
    
    if (handle->size < (size_t) handle->file_pos) {
        bytes = 0;
        *bytes_read = bytes;
        return SYS_ERR_OK;
    } else if (handle->size < (size_t) handle->file_pos + bytes) {
        bytes = handle->size - (size_t) handle->file_pos;
        assert((size_t)handle->file_pos + bytes == handle->size);
    }
    // get data:
    errval_t err;
    *bytes_read = 0;
    
    // first load data from current handle
    if (handle->current_offset < cluster_size()) {
        err = load_cluster_from_handle(handle, handle->current_clust);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "getting data in fat32_read failed");
            return err;
        }
        size_t leftover = cluster_size() - handle->current_offset;
        if (leftover >= bytes) {
            memcpy(buffer, &handle->cluster_data[handle->current_offset], bytes);
            handle->current_offset+= bytes;
            return SYS_ERR_OK;
        }
        memcpy(buffer, &handle->cluster_data[handle->current_offset], leftover);
        *bytes_read+= leftover;
    }
    size_t to_copy = 0;
    while(*bytes_read < bytes) {
        err = load_next_cluster_from_handle(handle);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "get data in fat32_read, loading next cluster failed");
            return err;
        }
        to_copy = bytes - *bytes_read;
        if (to_copy > cluster_size()) {
            cluster_size();
        }
        memcpy((void*)&((char*)buffer)[*bytes_read], handle->cluster_data, to_copy);
        *bytes_read += to_copy;
    }
    handle->current_offset = to_copy;
    
    handle->file_pos += *bytes_read;
    hand = (void*) handle;
    return SYS_ERR_OK;
}
errval_t fat32_seek(void* hand, enum fs_seekpos whence, off_t offset) {
    struct fat32_handle* handle = (struct fat32_handle*) hand;
    errval_t err;
    
    if (handle->isdir)
        return FS_ERR_NOTFILE;
    
    off_t file_pos;
    
    switch (whence) {
    case FS_SEEK_SET:
        assert(offset >= 0);
        file_pos = offset;
        break;
    case FS_SEEK_CUR:
        assert(offset >= 0);
        file_pos = (handle->file_pos + offset);
        break;
    case FS_SEEK_END:
        file_pos = ((off_t)handle->size + offset);
        break;
    default:
        return FS_ERR_INDEX_BOUNDS;
    }
    if (file_pos < 0 || file_pos > handle->size)
        return FS_ERR_INDEX_BOUNDS;
    
    int32_t difference = file_pos - ((size_t) handle->file_pos);
    bool did_inner = false; // did we have to change the cluster
    size_t jump_cluster_num;
    size_t base_cluster_num;
    if (difference > 0) {
        if (difference > (cluster_size() - handle->current_offset)) {
            did_inner = true;
            difference -= (cluster_size() - handle->current_offset);
            jump_cluster_num = difference/cluster_size(); // rounds off
            base_cluster_num = handle->current_clust;
        }
    }
    else if (difference < 0) {
        // new file position is smaller than the current position
        if (difference < handle->current_offset) {
            did_inner = true;
            jump_cluster_num = file_pos/cluster_size();
            base_cluster_num = handle->first_clust;
        }
    }
    if (did_inner) {
        // if we had to change the cluster
        uint32_t new_cluster_num;
        err = move_cluster_number(base_cluster_num, jump_cluster_num, &new_cluster_num);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "fat32_seek moving cluster number failed");
            return err;
        }
        handle->current_clust = new_cluster_num;
        if (difference < 0) {
            handle->current_offset = file_pos % cluster_size();
        }
        else {
            handle->current_offset = difference & cluster_size();
        }
        if (handle->in_memory) {
            free(handle->cluster_data);
            handle->cluster_data = NULL;
            handle->in_memory = false;
        }
    }
    else {
        handle->current_offset += difference;
    }
    handle->file_pos = (off_t) file_pos;
    hand = (void*) handle;
    return SYS_ERR_OK;
}
// opens a directory to path and returns the handle
errval_t fat32_opendir(char *path, void** hand) {
    struct fat32_handle* handle = (struct fat32_handle*) *hand;
    errval_t err;
    handle = malloc(sizeof(struct fat32_handle));
    if (handle==NULL)
        return LIB_ERR_MALLOC_FAIL;
    err = resolve_path(path, handle);
    if (err_is_fail(err)) {
        handle_close(handle);
        return err;
    }
    else if (!handle->isdir){
        handle_close(handle);
        return FS_ERR_NOTDIR;
    }
    *hand = (void*) handle;
    return SYS_ERR_OK;
}
// closes a directory at handle
errval_t fat32_closedir(void* hand) {
    struct fat32_handle* handle = (struct fat32_handle*) hand;
    if (!handle->isdir) {
        return FS_ERR_NOTDIR;
    }
    handle_close(handle);
    return SYS_ERR_OK;
}
// reads the next directory entry and returns the name
errval_t fat32_dir_read_next(void* hand, char** name) {
    struct fat32_handle* handle = (struct fat32_handle*) hand;
    if (!handle->isdir) {
        return FS_ERR_NOTDIR;
    }
    
    errval_t err = load_next_dir(handle, name, NULL); // we don't give a handle to receive the dir
    return err;
}
// returns the fileinfo for the file at handle
errval_t fat32_stat(void* hand, struct fs_fileinfo* info) {
    struct fat32_handle* handle = (struct fat32_handle*) hand;
    assert(info != NULL);
    info->type = handle->isdir ? FS_DIRECTORY : FS_FILE;
    info->size = handle->size;

    return SYS_ERR_OK;
}

// utility funciton that fills all fat32_handle data into a void buffer and returns the length
void* package_handle(struct fat32_handle* handle, size_t* length) {
    // we subtract the char* name pointer and the char* cluster_data pointer, add cluster_size for the cluster_data 
    *length = sizeof(struct fat32_handle)-sizeof(char*)*2;
    if (handle->in_memory)
        *length += cluster_size();
    char* buffer = malloc(*length);
    
    size_t offset = 0;
    strncpy(&buffer[offset], handle->name, 13);
    offset+=13;
    // copies isdir, size, first_clust, current_clust, file_pos, in_memory
    size_t next_chunk = 2*sizeof(bool)+sizeof(size_t)+sizeof(uint32_t)*2+sizeof(off_t);
    memcpy((void*)&buffer[offset], (void*)&handle->isdir, next_chunk);
    offset+=next_chunk;
    memcpy((void*)&buffer[offset], (void*)&handle->current_offset, sizeof(size_t));
    offset+=sizeof(size_t);
    if (handle->in_memory) {
        assert(handle->cluster_data != NULL);
        memcpy((void*)&buffer[offset], (void*)handle->cluster_data, cluster_size());
        offset+=cluster_size();
    }
    assert(offset==*length);
    handle_close(handle); // free the handle
    return (void*) buffer;
}
// utility function that returns a fat32_handle given a handle packaged as void buffer
struct fat32_handle* unpackage_handle(void* buffer, size_t length) {
    char* buf = (char*) buffer;
    struct fat32_handle* handle = malloc(sizeof(struct fat32_handle));
    handle->name = malloc(13);
    strncpy(handle->name, &buf[0], 13);
    size_t offset = 13;
    size_t next_chunk = 2*sizeof(bool)+sizeof(size_t)+sizeof(uint32_t)*2+sizeof(off_t);
    memcpy((void*)&handle->isdir, (void*)&buf[offset], next_chunk);
    offset+=next_chunk;
    memcpy((void*)&handle->current_offset, (void*)&buf[offset], sizeof(size_t));
    offset+=sizeof(size_t);
    if (handle->in_memory) {
        handle->cluster_data = malloc(cluster_size());
        memcpy((void*)handle->cluster_data, (void*)&buf[offset], cluster_size());
        offset+=cluster_size();
    }
    assert(offset=length);
    return handle;
}