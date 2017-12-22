/**
 * \file fopen_fat32.c 
 * \brief  fopen.c version for fat32
 */


#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <aos/aos.h>
#include <aos/aos_rpc.h>

#include "fopen_fat32.h"
//#include <fs/fat32.h>
#include <fs/fs.h>
#include <fs/dirent.h>
#include "fs_internal.h"


static struct aos_rpc fat32_instance;
static bool aos_init;

/*
 * FD table
 */

#define STDIN_FILENO    0       /* standard input file descriptor */
#define STDOUT_FILENO   1       /* standard output file descriptor */
#define STDERR_FILENO   2       /* standard error file descriptor */

static struct fdtab_entry fdtab[MAX_FD] = {
    [STDIN_FILENO] = {
        .type = FDTAB_TYPE_STDIN,
        .handle = NULL,
    },
    [STDOUT_FILENO] = {
        .type = FDTAB_TYPE_STDOUT,
        .handle = NULL,
    },
    [STDERR_FILENO] = {
        .type = FDTAB_TYPE_STDERR,
        .handle = NULL,
    },
};

static int fdtab_alloc(struct fdtab_entry *h)
{
    for (int fd = MIN_FD; fd < MAX_FD; fd++) {
        if (fdtab[fd].type == FDTAB_TYPE_AVAILABLE) {
            fdtab[fd].inherited = 0; // Just precautionary
            memcpy(&fdtab[fd], h, sizeof(struct fdtab_entry));

            return fd;
        }
    }

    // table full
    errno = EMFILE;
    return -1;
}

static struct fdtab_entry *fdtab_get(int fd)
{
    static struct fdtab_entry invalid = {
        .type = FDTAB_TYPE_AVAILABLE,
        .handle = NULL,
        .inherited = 0,
    };

    if (fd < MIN_FD || fd >= MAX_FD) {
        return &invalid;
    } else {
        return &fdtab[fd];
    }
}

static void fdtab_free(int fd)
{
    assert(fd >= MIN_FD && fd < MAX_FD);
    assert(fdtab[fd].type != FDTAB_TYPE_AVAILABLE);
    fdtab[fd].type = FDTAB_TYPE_AVAILABLE;
    fdtab[fd].handle = NULL;
    fdtab[fd].fd = 0;
    fdtab[fd].inherited = 0;
}

//XXX: flags are ignored...
static int fs_libc_open(char *path, int flags)
{
    fs_dirhandle_t vh;
    errval_t err;

    // If O_CREAT was given, we use ramfsfs_create()
    if(flags & O_CREAT) {
        err = LIB_ERR_NOT_IMPLEMENTED;
    } else {
        // Regular open()
        err = aos_rpc_fat_open(&fat32_instance, path, (void**)&vh);
    }

    if (err_is_fail(err)) {
        switch(err_no(err)) {
        case FS_ERR_NOTFOUND:
            errno = ENOENT;
            break;

        default:
            break;
        }

        return -1;
    }

    struct fdtab_entry e = {
        .type = FDTAB_TYPE_FILE,
        .handle = vh,
        .epoll_fd = -1,
    };
    int fd = fdtab_alloc(&e);
    if (fd < 0) {
        err = aos_rpc_fat_close(&fat32_instance, &vh);
        return -1;
    } else {
        return fd;
    }
}

static int fs_libc_read(int fd, void *buf, size_t len)
{
    errval_t err;

    struct fdtab_entry *e = fdtab_get(fd);
    size_t retlen = 0;
    switch(e->type) {
    case FDTAB_TYPE_FILE:
    {
        fs_dirhandle_t fh = e->handle;
        assert(e->handle);
        err = aos_rpc_fat_read(&fat32_instance, fh, buf, len, &retlen);
        if (err_is_fail(err)) {
            return -1;
        }
    }
        break;
    default :
        return -1;
    }

    return retlen;
}

// not implemented
//static int fs_libc_write(int fd, void *buf, size_t len);

static int fs_libc_close(int fd)
{
    errval_t err;
    struct fdtab_entry *e = fdtab_get(fd);
    if (e->type == FDTAB_TYPE_AVAILABLE) {
        return -1;
    }

    fs_dirhandle_t fh = e->handle;
    switch(e->type) {
    case FDTAB_TYPE_FILE:
        err = aos_rpc_fat_close(&fat32_instance, fh);
        if (err_is_fail(err)) {
            return -1;
        }
        break;
    default:
        return -1;
    }

    fdtab_free(fd);
    return 0;
}

static off_t fs_libc_lseek(int fd, off_t offset, int whence)
{
    struct fdtab_entry *e = fdtab_get(fd);
    fs_dirhandle_t fh = e->handle;
    switch(e->type) {
    case FDTAB_TYPE_FILE:
    {
        enum fs_seekpos fs_whence;
        errval_t err;

        switch(whence) {
        case SEEK_SET:
            fs_whence = FS_SEEK_SET;
            break;

        case SEEK_CUR:
            fs_whence = FS_SEEK_CUR;
            break;

        case SEEK_END:
            fs_whence = FS_SEEK_END;
            break;

        default:
            return -1;
        }
        off_t* result = malloc(sizeof(result));
        err = aos_rpc_fat_seek(&fat32_instance, fh, fs_whence, offset, result);
        if(err_is_fail(err)) {
            DEBUG_ERR(err, "vfs_seek");
            return -1;
        }
        return *result;
    }
    break;

    default:
        return -1;
    }
}

/*not implemented...
 * static errval_t fs_mkdir(const char *path){ return ramfs_mkdir(mount, path);}
static errval_t fs_rmdir(const char *path){ return ramfs_rmdir(mount, path); }
static errval_t fs_rm(const char *path){ return ramfs_remove(mount, path); }
*/
static errval_t fs_opendir(const char *path, fs_dirhandle_t *h){ return aos_rpc_fat_opendir(&fat32_instance, path, h); }
static errval_t fs_readdir(fs_dirhandle_t h, char **name) { return aos_rpc_fat_dir_read_next(&fat32_instance, h, name); }
static errval_t fs_closedir(fs_dirhandle_t h) { return aos_rpc_fat_closedir(&fat32_instance, h); }
static errval_t fs_fstat(fs_dirhandle_t h, struct fs_fileinfo *b) {
    // fs_dirhandle is a file!
    FILE *f = h;
    struct fdtab_entry *e = fdtab_get(f->_file);
    fs_dirhandle_t file_handle = e->handle;
    return aos_rpc_fat_stat(&fat32_instance, file_handle, b);
    
}

errval_t fs_unmount(void) {
    return aos_rpc_fat_unmount(&fat32_instance);
}

typedef int   fsopen_fn_t(char *, int);
typedef int   fsread_fn_t(int, void *buf, size_t);
typedef int   fswrite_fn_t(int, void *, size_t);
typedef int   fsclose_fn_t(int);
typedef off_t fslseek_fn_t(int, off_t, int);
void
newlib_register_fsops__(fsopen_fn_t *open_fn,
                        fsread_fn_t *read_fn,
                        fswrite_fn_t *write_fn,
                        fsclose_fn_t *close_fn,
                        fslseek_fn_t *lseek_fn);

static void libc_init(void)
{
    // replaced fs_libc_write by NULL
    newlib_register_fsops__(fs_libc_open, fs_libc_read, NULL,
                            fs_libc_close, fs_libc_lseek);

    /* register directory operations */
    // removed unsupported functions fs_mkdir, fs_rmdir, fs_rm
    fs_register_dirops(NULL, NULL, NULL, fs_opendir,
                       fs_readdir, fs_closedir, fs_fstat);

    //mount = fs_state;
}

errval_t init_fat32_rpc(void) {
    if (aos_init) {
        return SYS_ERR_OK;
    }
    struct aos_rpc* rpc = malloc(sizeof(struct aos_rpc*));
    struct waitset* ws = get_default_waitset();
    errval_t err = aos_rpc_init(ws, rpc);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "init_fat32_rpc failed");
        return err;
    }
    domainid_t new_pid;
    aos_rpc_process_spawn(rpc, "/armv7/sbin/fileserver", disp_get_core_id(), &new_pid);
    struct capref cap; // TODO: where do I get this from?
    err = aos_rpc_fs_init(&fat32_instance, cap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "init_fat32_rpc failed");
        return err;
    }
    
    aos_init = true;
    return SYS_ERR_OK;
}

errval_t fopen_fat32_mount(const char* path, const char* uri) {
    errval_t err;
    if (!aos_init)
        err = FS_ERR_INVALID_FH;
        DEBUG_ERR(err, "filesystem_init possibly not executed yet, as fat32 rpc instance not running yet");
        return err;
    err = aos_rpc_fat_mount(&fat32_instance, path, uri);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "fopen_fat32_mount aos_rpc_fat_mount failed");
        return err;
    }
    // init libc pointers
    libc_init();
}