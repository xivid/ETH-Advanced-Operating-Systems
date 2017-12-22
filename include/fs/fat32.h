#ifndef _FS_FAT32_H
#define _FS_FAT32_H_

#include <fs/fs.h>

struct fat32_dirent {
    // name without extensions
    char DIR_Name[8];
    
    // 3 char extensions after the implicit point (.)
    char DIR_Extension[3];
    uint8_t DIR_Attr;
    uint8_t DIR_NTRes;
    uint8_t DIR_CrtTimeTenth; // optional date creation time
    uint16_t DIR_FstClusHi; // High word (16 bits) of this entry’s first cluster number
    uint16_t DIR_WrtTime; // last time of writing / creation
    uint16_t DIR_WrtDate; // last date of writing/creation
    uint16_t DIR_FstClusLO; // Low word (16 bits) of this entry’s first cluster number
    uint32_t DIR_FileSize; // file size in bytes, or 0 for directories
};

// handle to a file/dir (dirs are almost equal to files, thus we use the same struct, somewhat analog to ramfs_handle
struct fat32_handle {
    char* name;
    
    // is this a dir
    bool isdir;
    size_t size; // should be 0 for directories
    uint32_t first_clust;
    uint32_t current_clust;
    off_t file_pos; // pointer to position in file (analog to in the ramfs_handle)
    bool in_memory;
    union {
        struct fat32_dirent *dircluster; // pointer to the array of directory entries in the cluster of this directory
        char *cluster_data; // pointer to data of current cluster
    };
    size_t current_offset; // offset of read bytes in cluster_data or for directories the offset to the next direntry to read
    
};

// stores general information about the mounted sd card, analog to ramfs but much more details
// most are things read from the Boot Sector and BPB Structure (see the provided fat documentation)
struct fat32_mount {
    struct fat32_handle *root;
    
    char *path;
    
    // bytes per sector, 512, 1024, 2048 or 4096
    uint16_t BytsPerSec;
    // sectors per cluster, power of 2 smaller or equal 128
    uint8_t SecPerClus;
    // reserved sector count, number of reserved sectors in the reserved region (almost always 32 for fat32)
    uint16_t RsvdSecCnt;
    // number of fat data structures on the volume, normally 2
    uint8_t NumFATs;
    // total sector count
    uint32_t FATSz32;
    // pointer to the boot sector
    char* sector0;
    // offset from boot sector, normally 0 without partitions
    size_t sector0_offset;
    // file allocation table
    uint32_t** fat;
    // data offset, first sector with data, first sector of cluster 2
    size_t FirstDataSector;
};
size_t cluster_size(void);

errval_t fat32_mount(const char *path, const char *uri);
errval_t fat32_unmount(void);

// opens a handle to path
errval_t fat32_open(char* path, void** hand);
// closes given handle
errval_t fat32_close(void* hand);
// reads at most bytes many bytes into buffer from the handle, returns the buffer and bytes_read
errval_t fat32_read(void* hand, void* buffer, size_t bytes, size_t* bytes_read);
errval_t fat32_seek(void* hand, enum fs_seekpos whence, off_t offset);
// opens a directory to path and returns the handle
errval_t fat32_opendir(char *path, void** hand);
// closes a directory at handle
errval_t fat32_closedir(void* hand);
// reads the next directory entry and returns the name
errval_t fat32_dir_read_next(void* hand, char** name);
// returns the fileinfo for the file at handle
errval_t fat32_stat(void* hand, struct fs_fileinfo* info);

void* package_handle(struct fat32_handle* handle, size_t* length);
struct fat32_handle* unpackage_handle(void* buffer, size_t length);
void handle_close(struct fat32_handle* handle);
#endif