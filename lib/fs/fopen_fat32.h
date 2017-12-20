#ifndef _FOPEN_FAT32_H_
#define _FOPEN_FAT32_H_

errval_t init_fat32_rpc(void);
errval_t fopen_fat32_mount(const char* path, const char* uri);
errval_t fs_unmount(void);

#endif