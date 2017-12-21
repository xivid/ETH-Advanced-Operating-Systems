/**
 * \file
 * \brief Interface declaration for AOS rpc-like messaging
 */

/*
 * Copyright (c) 2013, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef _LIB_BARRELFISH_AOS_MESSAGES_H
#define _LIB_BARRELFISH_AOS_MESSAGES_H

#include <aos/aos.h>
#include <fs/fs.h>

#define AOS_RPC_ATTEMPTS            (10) // how many attempts do we want for sending/receiving before throwing an error
#define LMP_ARGS_SIZE               (10)

// IDs for different transmission types
enum enum_rpc_msgtype {
    AOS_RPC_ID_ACK = 1,
    AOS_RPC_ID_INIT,
    AOS_RPC_ID_RAM,
    AOS_RPC_ID_NUM,
    AOS_RPC_ID_CHAR,
    AOS_RPC_ID_STR,
    AOS_RPC_ID_PROCESS,
    AOS_RPC_ID_GET_PIDS,
    AOS_RPC_ID_GET_PNAME,
    AOS_RPC_ID_GET_CHAR,
    AOS_RPC_ID_RETURN_CHAR,
    AOS_RPC_ID_GET_DEVICE_CAP,
    AOS_RPC_ID_NAMESERVER_SYN,
    AOS_RPC_ID_REGISTER_NAMESERVER_WITH_INIT,
    AOS_RPC_ID_SET_NAMESERVER_EP,
    AOS_RPC_ID_GET_NAMESERVER_EP,
    AOS_RPC_ID_REGISTER_EP_WITH_NAMESERVER,
    AOS_RPC_ID_FAIL,
};

typedef enum {
    NS_ERR_NAME_OK = 0,
    NS_ERR_NAME_ALREADY_TAKEN,
    NS_ERR_NAME_WRONG_NAME,
} ns_err_names_t;

struct aos_rpc {
    struct lmp_chan lmp;
    struct waitset* ws;
    struct thread_mutex rpc_mutex;
    // TODO: add state for your implementation
};

errval_t aos_rpc_ns(struct waitset* ws, struct aos_rpc *rpc, struct capref cap);
/**
 * \brief send a number over the given channel
 */
errval_t aos_rpc_send_number(struct aos_rpc *chan, uintptr_t val);

/**
 * \brief send a string over the given channel
 */
errval_t aos_rpc_send_string(struct aos_rpc *chan, const char *string);

/**
 * \brief request a RAM capability with >= request_bits of size over the given
 * channel.
 */
errval_t aos_rpc_get_ram_cap(struct aos_rpc *chan, size_t bytes, size_t align,
                             struct capref *retcap, size_t *ret_bytes);

/**
 * \brief request the nameserver endpoint capability from the init process
 */
errval_t aos_rpc_get_nameserver_ep(struct aos_rpc *init_chan,
        struct capref *retcap);
/**
 * \brief get one character from the serial port
 */
errval_t aos_rpc_serial_getchar(struct aos_rpc *chan, char *retc);

/**
 * \brief send one character to the serial port
 */
errval_t aos_rpc_serial_putchar(struct aos_rpc *chan, char c);

/**
 * \brief Request process manager to start a new process
 * \arg name the name of the process that needs to be spawned (without a
 *           path prefix)
 * \arg newpid the process id of the newly spawned process
 */
errval_t aos_rpc_process_spawn(struct aos_rpc *chan, char *name,
                               coreid_t core, domainid_t *newpid);

/**
 * \brief Request process manager to start a new process with arguments
 * \arg name the name of the process that needs to be spawned (without a
 *           path prefix)
 * \arg newpid the process id of the newly spawned process
 * \arg arguments arbitrary length command line arguments to be passed in, as a char array
 */
errval_t aos_rpc_process_spawn_with_arguments(struct aos_rpc *chan,
                                              char *name,
                                              coreid_t core,
                                              domainid_t *newpid,
                                              const char *arguments);


/**
 * \brief Get name of process with id pid.
 * \arg pid the process id to lookup
 * \arg name A null-terminated character array with the name of the process
 * that is allocated by the rpc implementation. Freeing is the caller's
 * responsibility.
 */
errval_t aos_rpc_process_get_name(struct aos_rpc *chan, domainid_t pid,
                                  char **name);

/**
 * \brief Get name of process with id pid.
 * \arg pid the process id to lookup
 * \arg name A null-terminated character array with the name of the process
 * that is allocated by the rpc implementation. Freeing is the caller's
 * responsibility.
 * \arg dest PID allocated by the process manager (the 1st byte is coreid, the last 3 bytes are the actual process id)
 */
errval_t aos_rpc_process_get_name_on_core(struct aos_rpc *chan, domainid_t pid,
                                  char **name, domainid_t dest);

/**
 * \brief Get process ids of all running processes
 * \arg pids An array containing the process ids of all currently active
 * processes. Will be allocated by the rpc implementation. Freeing is the
 * caller's  responsibility.
 * \arg pid_count The number of entries in `pids' if the call was successful
 */
errval_t aos_rpc_process_get_all_pids(struct aos_rpc *chan,
                                      domainid_t **pids, size_t *pid_count);


/**
 * \brief Get process ids of all running processes on a given core
 * \arg pids An array containing the process ids of all currently active
 * processes. Will be allocated by the rpc implementation. Freeing is the
 * caller's  responsibility.
 * \arg pid_count The number of entries in `pids' if the call was successful
 * \arg dest PID allocated by the process manager (the 1st byte is coreid, the last 3 bytes are the actual process id)
 */
errval_t aos_rpc_process_get_all_pids_on_core(struct aos_rpc *chan,
                                           domainid_t **pids, size_t *pid_count,
                                           domainid_t dest);


/**
 * \brief Gets a capability to device registers
 * \param rpc  the rpc channel
 * \param paddr physical address of the device
 * \param bytes number of bytes of the device memory
 * \param frame returned devframe
 */
errval_t aos_rpc_get_device_cap(struct aos_rpc *rpc, lpaddr_t paddr, size_t bytes,
                                struct capref *frame);
/**
 * \brief Initialize given rpc channel.
 * TODO: you may want to change the inteface of your init function, depending
 * on how you design your message passing code.
 */
errval_t aos_rpc_init(struct waitset* ws, struct aos_rpc *rpc);

/**
 * \brief Initialize a channel to nameserver (see the nameserver protocol)
 * \param rpc Will store the channel to nameserver
 * \param cap A capability for nameserver's endpoint obtained from init
 * \param id Will store the unique client id
 */
errval_t aos_rpc_nameserver_syn(struct aos_rpc *rpc, struct capref cap,
        unsigned *id);

/**
 * \brief Register an endpoint with nameserver (see the nameserver protocol)
 * \param rpc        The channel to nameserver
 * \param id         The client id assigned by nameserver
 * \param endpoint   The endpoint capability to register
 * \param name       The service name to register
 * \param ns_err     Will store the error code obtained from the nameserver
 */
errval_t aos_rpc_nameserver_register(struct aos_rpc *rpc, unsigned id,
        struct capref endpoint, char *name, ns_err_names_t *ns_err);
/**
 * \brief Returns the RPC channel to init.
 */
struct aos_rpc *aos_rpc_get_init_channel(void);

/**
 * \brief Returns the channel to the memory server
 */
struct aos_rpc *aos_rpc_get_memory_channel(void);

/**
 * \brief Returns the channel to the process manager
 */
struct aos_rpc *aos_rpc_get_process_channel(void);

/**
 * \brief Returns the channel to the serial console
 */
struct aos_rpc *aos_rpc_get_serial_channel(void);

// rpc calls for fat32

// mount mount fat32 drive is mounted to path, given also the uri of the mount command
errval_t aos_rpc_fat_mount(struct aos_rpc* chan, const char* path, const char* uri);
// unmounts the fat32 drive (use with care)
errval_t aos_rpc_fat_unmount(struct aos_rpc* chan);
// open file at path and return via handle
errval_t aos_rpc_fat_open(struct aos_rpc* chan, char* path, void** handle);
// close the file given with handle
errval_t aos_rpc_fat_close(struct aos_rpc* chan, void* handle);
// read the file given in handle to buffer, read maximum of bytes, returns bytes_read with the actual read number
errval_t aos_rpc_fat_read(struct aos_rpc* chan, void* handle, void* buffer, size_t bytes, size_t* bytes_read);
// move read pointer of open file at handle, move by offset, new offset is given in handle->current_offset
errval_t aos_rpc_fat_seek(struct aos_rpc* chan, void* handle, enum fs_seekpos whence, off_t offset);
// open directory "path", return opened directory via handle
errval_t aos_rpc_fat_opendir(struct aos_rpc* chan, const char *path, void** handle);
// close the directory given by handle
errval_t aos_rpc_fat_closedir(struct aos_rpc* chan, void* handle);
// read next entry in the directory given by handle, return the name
errval_t aos_rpc_fat_dir_read_next(struct aos_rpc* chan, void* handle, char** name);
// returns the stat fs_fileinfo of the open file/dir given by handle
errval_t aos_rpc_fat_stat(struct aos_rpc* chan, void* handle, struct fs_fileinfo* info);

#endif // _LIB_BARRELFISH_AOS_MESSAGES_H
