#ifndef _URPC_SERVER_H_
#define _URPC_SERVER_H_

#include <aos/aos_rpc.h>
#include <spawn/spawn.h>
#include <spawn/process_manager.h>

#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif

// macros copied from kernel/include/arch/arm/startup_arm.h
#define INIT_BOOTINFO_VBASE     ((lvaddr_t) 0x200000)
#define INIT_ARGS_VBASE         (INIT_BOOTINFO_VBASE + BOOTINFO_SIZE)
#define INIT_DISPATCHER_VBASE   (INIT_ARGS_VBASE + ARGS_SIZE)
#define MON_URPC_VBASE          (INIT_DISPATCHER_VBASE + DISPATCHER_SIZE)

#define INIT_PROCESS_ID (0)

#define N_LINES                 (MON_URPC_SIZE / 2 / 64)
#define LINE_WORDS              (16)
#define URPC_PAYLOAD_LEN        (10)

extern void *urpc_shared_base;
extern coreid_t my_core_id;
extern struct process_manager pm;

void urpc_write(const uint32_t message[URPC_PAYLOAD_LEN], coreid_t core);
void urpc_read_and_process(uint32_t *rx, coreid_t core);
void urpc_read_until_ack(uint32_t *ack_response, coreid_t core);
void urpc_spawn_handler(coreid_t core, void *name);
void urpc_get_pids_handler(coreid_t core, domainid_t pid);
void urpc_get_pname_handler(coreid_t core, domainid_t pid);
void urpc_get_char_handler(coreid_t core);
errval_t urpc_read_and_process_non_block(coreid_t core);

#endif /* _URPC_SERVER_H */
