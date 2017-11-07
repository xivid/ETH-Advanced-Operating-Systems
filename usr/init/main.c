/**
 * \file
 * \brief init process for child spawning
 */

/*
 * Copyright (c) 2007, 2008, 2009, 2010, 2016, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#include <stdio.h>
#include <stdlib.h>

#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/waitset.h>
#include <aos/morecore.h>
#include <aos/paging.h>
#include <aos/kernel_cap_invocations.h>
#include <aos/coreboot.h>
#include <multiboot.h>

#include <mm/mm.h>
#include "mem_alloc.h"
#include <spawn/spawn.h>
#include <spawn/multiboot.h>

#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif

struct client {
    struct EndPoint end;
    struct client* prev;
    struct client* next;

    struct lmp_chan lmp;
} *client_list = NULL;
errval_t recv_handler(void* arg);
errval_t whois(struct capref cap, struct client **he_is);
void* answer_number(struct capref* cap, struct lmp_recv_msg* msg);
void* answer_char(struct capref* cap, struct lmp_recv_msg* msg);
void* answer_str(struct capref* cap, struct lmp_recv_msg* msg);
void* answer_process(struct capref* cap, struct lmp_recv_msg* msg);
void* answer_init(struct capref* cap);
void* answer_ram(struct capref* cap, struct lmp_recv_msg* msg);
errval_t send_received(void* arg);
errval_t send_ram(void* args);
errval_t send_process(void* args);
errval_t init_rpc(void);

coreid_t my_core_id;
struct bootinfo *bi;


bool test_alloc_free(int count);
bool test_virtual_memory(int count, int size);
bool test_multi_spawn(int spawns);
bool test_huge_malloc(void);
bool test_dynamic_slots(int count);
errval_t boot_cpu1(void);

// ****************************** RPC *************************************

errval_t init_rpc(void)
{
    errval_t err;
    err = cap_retype(cap_selfep, cap_dispatcher, 0, ObjType_EndPoint, 0, 1);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c: cap retype of dispatcher to selfep failed");
        return EXIT_FAILURE;
    }
    struct lmp_chan* lmp = (struct lmp_chan*) malloc(sizeof(struct lmp_chan));
    err = lmp_chan_accept(lmp, DEFAULT_LMP_BUF_WORDS, NULL_CAP);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c: lmp chan accept failed");
        return EXIT_FAILURE;
    }
    err = lmp_chan_alloc_recv_slot(lmp);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c: lmp chan alloc recv slot failed");
        return EXIT_FAILURE;
    }
    err = cap_copy(cap_initep, lmp->local_cap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c: lmp cap copy of lmp->local_cap to cap_initep failed");
        return EXIT_FAILURE;
    }
    err = lmp_chan_register_recv(lmp, get_default_waitset(), MKCLOSURE((void*) recv_handler, lmp));
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c: lmp chan register recv failed");
        return EXIT_FAILURE;
    }
    return SYS_ERR_OK;
}

// see also book page 98
errval_t recv_handler(void* arg)
{
    struct lmp_chan* lmp = (struct lmp_chan*) arg;
    struct lmp_recv_msg msg = LMP_RECV_MSG_INIT;
    struct capref cap;
    errval_t err = lmp_chan_recv(lmp, &msg, &cap);
    if (err_is_fail(err)) {
        if (lmp_err_is_transient(err)) {
            err = lmp_chan_recv(lmp, &msg, &cap);
        }
        else {
            DEBUG_ERR(err, "usr/main.c recv_handler: lmp chan recv, non transient error");
            return err;
        }
    }

    // register again for receiving
    err = lmp_chan_alloc_recv_slot(lmp);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c recv_handler: lmp chan alloc recv slot failed");
        return err;
    }
    err = lmp_chan_register_recv(lmp, get_default_waitset(), MKCLOSURE((void*) recv_handler, arg));
    if (err_is_fail(err)) {
            DEBUG_ERR(err, "usr/main.c recv_handler: lmp chan register recv failed");
            return err;
    }

    // no message content received?
    if (msg.buf.msglen <= 0)
        return err;
    void* answer;
    void* answer_args;
    switch (msg.words[0]) {
        case AOS_RPC_ID_NUM:
            answer_args = answer_number(&cap, &msg);
            answer = (void*) send_received;
            break;
        case AOS_RPC_ID_INIT:
            answer_args = answer_init(&cap);
            answer = (void*) send_received;
            break;
        case AOS_RPC_ID_RAM:
            answer_args = answer_ram(&cap, &msg);
            answer = (void*) send_ram;
            break;
        case AOS_RPC_ID_CHAR:
            answer_args = answer_char(&cap, &msg);
            answer = (void*) send_received;
            break;
        case AOS_RPC_ID_STR:
            answer_args = answer_str(&cap, &msg);
            answer = (void*) send_received;
            break;
        case AOS_RPC_ID_PROCESS:
            answer_args = answer_process(&cap, &msg);
            answer = (void*) send_process;
            break;
        default:
            return LIB_ERR_NOT_IMPLEMENTED;
    }
    struct lmp_chan* ret_chan = (struct lmp_chan*) answer_args;
    err = lmp_chan_register_send(ret_chan, get_default_waitset(), MKCLOSURE(answer, answer_args));
    if (err_is_fail(err)) {
            DEBUG_ERR(err, "usr/main.c recv_handler: lmp chan register send failed");
            return err;
    }
    return SYS_ERR_OK;
}

errval_t whois(struct capref cap, struct client **he_is) {
    struct client *cur = client_list;
    struct capability return_cap;
    errval_t err = debug_cap_identify(cap, &return_cap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c whois: debug identify cap failed");
        return err;
    }
    while (cur != NULL) {
        if (return_cap.u.endpoint.listener == cur->end.listener
                && return_cap.u.endpoint.epoffset == cur->end.epoffset) {
            *he_is = cur;
            break;
        }
        cur = cur->next;
    }
    return err;
}

void* answer_number(struct capref* cap, struct lmp_recv_msg* msg) {
    debug_printf("server received number: %u\n", (uint32_t) msg->words[1]);

    // create answer
    struct client* he_is = NULL;
    errval_t err = whois(*cap, &he_is);
    if (err_is_fail(err) || he_is == NULL) {
        DEBUG_ERR(err, "usr/main.c answer number: could not identify client");
        return NULL;
    }

    // lmp channel for the response handler
    return (void*) &(he_is->lmp);
}

void* answer_char(struct capref* cap, struct lmp_recv_msg* msg)
{
    errval_t err = sys_print((const char *)&msg->words[1], 1);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c answer char: could not sys_print");
        return NULL;
    }

    // create answer
    struct client* he_is = NULL;
    err = whois(*cap, &he_is);
    if (err_is_fail(err) || he_is == NULL) {
        DEBUG_ERR(err, "usr/main.c answer char: could not identify client");
        return NULL;
    }

    // lmp channel for the response handler
    return (void*) &(he_is->lmp);
}

void* answer_str(struct capref* cap, struct lmp_recv_msg* msg)
{
    int len = strnlen((const char*) &msg->words[1], 32);
    //debug_printf("Got packet len(%i) %lx!\n", len, &msg->words[3]);
    errval_t err = sys_print((const char *)&msg->words[1], len);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c answer char: could not sys_print");
        return NULL;
    }

    // create answer
    struct client* he_is = NULL;
    err = whois(*cap, &he_is);
    if (err_is_fail(err) || he_is == NULL) {
        DEBUG_ERR(err, "usr/main.c answer char: could not identify client");
        return NULL;
    }

    // lmp channel for the response handler
    return (void*) &(he_is->lmp);
}

void* answer_process(struct capref* cap, struct lmp_recv_msg* msg)
{
    errval_t err;
    debug_printf("got process name: %s\n", &msg->words[2]);
    struct spawninfo *si = malloc(sizeof(struct spawninfo));
    err = spawn_load_by_name(&msg->words[2], si);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c could not start a process\n");
        return NULL;
    }
    debug_printf("domain id is %d\n", si->domain_id);
    // create answer
    struct client* he_is = NULL;
    err = whois(*cap, &he_is);
    if (err_is_fail(err) || he_is == NULL) {
        DEBUG_ERR(err, "usr/main.c answer char: could not identify client");
        return NULL;
    }


    size_t size_of_args = sizeof(domainid_t) +
        ROUND_UP(sizeof(struct lmp_chan),4) +
        ROUND_UP(sizeof(errval_t),4);
    void* args_ptr = malloc(size_of_args);
    void* args = args_ptr;


    // add channel to args
    *((struct lmp_chan*) args) = he_is->lmp;
    // cast to void* and move pointer to after the lmp channel
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(struct lmp_chan), 4);
    // add error to args
    *((errval_t*) args) = err;
    // cast to void* and move pointer to after the error
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(errval_t), 4);
    *((domainid_t*) args) = si->domain_id;

    return (void*) args_ptr;
}
// sets up the client struct for new processes
void* answer_init(struct capref* cap) {
    struct client *potential = NULL;
    errval_t err = whois(*cap, &potential);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c answer init: could not identify client");
        return NULL;
    }
    if (potential != NULL) {
        return (void*) &(potential->lmp);
    }
    potential = (struct client*) malloc(sizeof(struct client));

    struct capability return_cap;
    debug_cap_identify(*cap, &return_cap);
    potential->end = return_cap.u.endpoint;
    potential->prev = NULL;
    if (client_list == NULL) {
        potential->next = NULL;
    }
    else {
        potential->next = client_list;
        client_list->prev = potential;
    }
    client_list = potential;

    err = lmp_chan_accept(&potential->lmp, DEFAULT_LMP_BUF_WORDS, *cap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c answer init: could not do lmp chan accept");
        return NULL;
    }
    return (void*) &(potential->lmp);
}

void* answer_ram(struct capref* cap, struct lmp_recv_msg* msg) {
    struct client *sender = NULL;
    errval_t err = whois(*cap, &sender);
    if (err_is_fail(err) || sender == NULL) {
        DEBUG_ERR(err, "usr/main.c answer init: could not identify client");
        return NULL;
    }
    struct capref return_cap;
    size_t alignment = ROUND_UP((size_t) msg->words[2], BASE_PAGE_SIZE);
    alignment = max(alignment, BASE_PAGE_SIZE); // in case alignment is 0
    size_t allocated_size = ROUND_UP(msg->words[1], alignment);
    err = ram_alloc_aligned(&return_cap, (size_t) msg->words[1], (size_t) msg->words[2]);


    size_t size_of_args = sizeof(size_t) + ROUND_UP(sizeof(struct lmp_chan),4) + ROUND_UP(sizeof(errval_t),4) + ROUND_UP(sizeof(struct capref),4);
    void* args_ptr = malloc(size_of_args);
    void* args = args_ptr;


    // add channel to args
    *((struct lmp_chan*) args) = sender->lmp;
    // cast to void* and move pointer to after the lmp channel
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(struct lmp_chan), 4);
    // add error to args
    *((errval_t*) args) = err;
    // cast to void* and move pointer to after the error
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(errval_t), 4);
    // add cap to args
    *((struct capref*) args) = return_cap;
    // cast t void* and move pointer
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(struct capref), 4);
    *((size_t*) args) = allocated_size;

    return (void*) args_ptr;
}

// handler to send a signal that the message was received
errval_t send_received(void* arg) {
    struct lmp_chan* lmp = (struct lmp_chan*) arg;
    errval_t err = lmp_chan_send1(lmp, LMP_FLAG_SYNC, NULL_CAP, 1); // send a 1 to signal that the message arrived
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c send received: could not do lmp chan send1");
        return err;
    }
    return SYS_ERR_OK;
}

errval_t send_ram(void* args) {
    // get channel
    struct lmp_chan* lmp = (struct lmp_chan*) args;
    // cast to void* and move pointer to after the lmp channel
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(struct lmp_chan), 4);
    // get error
    errval_t* err = (errval_t*) args;
    // cast to void* and move pointer to after the error
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(errval_t), 4);
    // get cap
    struct capref* cap = (struct capref*) args;
    // cast t void* and move pointer
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(struct capref), 4);
    size_t* allocated_size = (size_t*) args;

    errval_t err_send = lmp_chan_send3(lmp, LMP_FLAG_SYNC, *cap, (size_t)(err_is_fail(*err)? 0:1), *allocated_size, (uintptr_t) *err);
    if (err_is_fail(err_send)) {
        DEBUG_ERR(err_send, "usr/main.c send ram: could not do lmp chan send3");
        return err_send;
    }

    return SYS_ERR_OK;
}

errval_t boot_cpu1(void) {
    struct capref kcb_ram;
    errval_t err = ram_alloc(&kcb_ram, OBJSIZE_KCB);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c boot cpu1: could not ram alloc kcb_ram");
        return err;
    }
    err = slot_alloc(&kcb_ram);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c boot cpu1: could not slot alloc kcb_ram");
        return err;
    }
    struct capref kcb;
    err = cap_retype(kcb, kcb_ram, 0, ObjType_KernelControlBlock, OBJSIZE_KCB, 1);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c boot cpu1: could not cap retype kcb");
        return err;
    }

    struct capref core_data_f;
    size_t ret;
    err = frame_alloc(&core_data_f, OBJSIZE_KCB, &ret);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c boot cpu1: could not frame alloc core data frame");
        return err;
    }
    err = invoke_kcb_clone(kcb, core_data_f);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c boot cpu1: could not invoke kcb clone");
        return err;
    }

    struct arm_core_data* core_data;
    err = paging_map_frame(get_current_paging_state(), (void**) &core_data, BASE_PAGE_SIZE, core_data_f, NULL, NULL);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c boot cpu1: could not paging map frame core data to core_data_f");
        return err;
    }

    // allocate memory for the new core
    core_data->memory_bytes = 3u*BASE_PAGE_SIZE*ARM_CORE_DATA_PAGES;
    core_data->memory_base_start = (uint32_t) malloc(core_data->memory_bytes);
    core_data->cmdline = (lvaddr_t) core_data->cmdline_buf;

    // fill rest of core_data
    struct mem_region* module = multiboot_find_module(bi, "init");
    if (module == NULL) {
        debug_printf("usr/main.c boot cpu1: init module not found");
        return SPAWN_ERR_FIND_MODULE;
    }
    struct multiboot_modinfo monitor_modinfo = {
        .mod_start = module->mr_base,
        .mod_end = module->mrmod_size + module->mr_base,
        .string = module->mrmod_data,
        .reserved = 0
    };
    core_data->monitor_module = monitor_modinfo;

    // load and relocate cpu driver
    struct mem_region* cpu_module = multiboot_find_module(bi, "cpu_omap44xx");
    if (module == NULL) {
        debug_printf("usr/main.c boot cpu1: cpu driver module not found");
        return SPAWN_ERR_FIND_MODULE;
    }
    struct capref cpu_frame = {
        .cnode = cnode_module,
        .slot = cpu_module->mrmod_slot
    };

    struct frame_identity cpu_frame_id;
    err = frame_identify(cpu_frame, &cpu_frame_id);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c boot cpu1: could not frame identify");
        return err;
    }

    // memory for relocatable segment
    struct capref relocatable_segment;
    err = frame_alloc(&relocatable_segment, cpu_frame_id.bytes, &ret);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c boot cpu1: could not frame alloc relocatable_segment");
        return err;
    }
    void* segment_addr;
    err = paging_map_frame(get_current_paging_state(), &segment_addr, ret, relocatable_segment, NULL, NULL);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c boot cpu1: could not paging map relocatable segment memory");
        return err;
    }

    struct frame_identity segment_id;
    err = frame_identify(relocatable_segment, &segment_id);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c boot cpu1: could not frame identify the relocatable_segment");
        return err;
    }

    // elf text segment
    void* elfdata;
    err = paging_map_frame(get_current_paging_state(), &elfdata, cpu_frame_id.bytes, cpu_frame, NULL, NULL);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c boot cpu1: could not paging map elf frame");
        return err;
    }

    //TODO: is this the correct upper limit of the heap?
    lvaddr_t heap_upper_limit = VADDR_OFFSET;
    err = load_cpu_relocatable_segment(elfdata, segment_addr, segment_id.base + heap_upper_limit, core_data->kernel_load_base, &core_data->got_base);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c boot cpu1: could not load cpu relocatable segment");
        return err;
    }
    err = sys_debug_flush_cache();
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c boot cpu1: could not flush cache");
        return err;
    }
    sys_armv7_cache_invalidate(core_data, core_data + sizeof(core_data));
    sys_armv7_cache_clean_poc(core_data, core_data + sizeof(core_data));
    sys_armv7_cache_clean_pou(core_data, core_data + sizeof(core_data));
    // Do we really want to clear the cache for physical addresses?
    sys_armv7_cache_invalidate((void *)(uintptr_t)segment_id.base, (void *)(uintptr_t)segment_id.base + segment_id.bytes);
    sys_armv7_cache_clean_poc((void *)(uintptr_t)segment_id.base, (void *)(uintptr_t)segment_id.base + segment_id.bytes);
    sys_armv7_cache_clean_pou((void *)(uintptr_t)segment_id.base, (void *)(uintptr_t)segment_id.base + segment_id.bytes);

    err = invoke_monitor_spawn_core(1, CPU_ARM7, (forvaddr_t) core_data->entry_point);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c boot cpu1: could not invoke cpu1");
        return err;
    }
    return SYS_ERR_OK;
}

errval_t send_process(void *args) {
    // get channel
    struct lmp_chan* lmp = (struct lmp_chan*) args;
    // cast to void* and move pointer to after the lmp channel
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(struct lmp_chan), 4);
    // get error
    errval_t* err = (errval_t*) args;
    // cast to void* and move pointer to after the error
    args = (void*) ROUND_UP((uintptr_t) args + sizeof(errval_t), 4);
    domainid_t *domain_id = (domainid_t*) args;

    errval_t err_send = lmp_chan_send3(lmp, LMP_FLAG_SYNC, NULL_CAP, (size_t)(err_is_fail(*err)? 0:1), *domain_id, (uintptr_t) *err);
    if (err_is_fail(err_send)) {
        DEBUG_ERR(err_send, "usr/main.c send ram: could not do lmp chan send3");
        return err_send;
    }

    return SYS_ERR_OK;
}

int main(int argc, char *argv[])
{
    errval_t err;
    client_list = NULL;
    /* Set the core id in the disp_priv struct */
    err = invoke_kernel_get_core_id(cap_kernel, &my_core_id);
    assert(err_is_ok(err));
    disp_set_core_id(my_core_id);

    debug_printf("init: on core %" PRIuCOREID " invoked as:", my_core_id);
    for (int i = 0; i < argc; i++) {
       printf(" %s", argv[i]);
    }
    printf("\n");

    /* First argument contains the bootinfo location, if it's not set */
    bi = (struct bootinfo*)strtol(argv[1], NULL, 10);
    if (!bi) {
        assert(my_core_id > 0);
    }

    err = initialize_ram_alloc();
    if(err_is_fail(err)){
        DEBUG_ERR(err, "initialize_ram_alloc");
        return EXIT_FAILURE;
    }

    err = init_rpc();
    if(err_is_fail(err)){
        DEBUG_ERR(err, "init_rpc");
        return EXIT_FAILURE;
    }

    //test_multi_spawn(1);
    if (my_core_id == 0) {
        err = boot_cpu1();
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "usr/main.c: boot_cpu1 failed");
            return EXIT_FAILURE;
        }
    }


    struct spawninfo *si = malloc(sizeof(struct spawninfo));
    err = spawn_load_by_name("/armv7/sbin/hello", si);
    if (err_is_fail(err)) {
        debug_printf("Failed spawning process memeater\n");
        return false;
    }

    /* test_virtual_memory(10, BASE_PAGE_SIZE); */
    debug_printf("Message handler loop\n");
    // Hang around
    struct waitset *default_ws = get_default_waitset();
    while (true) {
        err = event_dispatch(default_ws);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "in event_dispatch");
            abort();
        }
    }

    return EXIT_SUCCESS;
}

__attribute__((unused))
bool test_alloc_free(int allocations) {
    struct capref capabilities[allocations];
    errval_t err;
    size_t size;

    for (int i = 0; i < allocations; i++) {
        size = BASE_PAGE_SIZE << (i % 9);
        err = ram_alloc_aligned(capabilities + i, size, size);
        if (err_is_fail(err)) {
            debug_printf("Failed allocating capability %i\n", i);
            return false;
        }
    }
    debug_printf("allocations test succeeded\n");
    for (int i = 0; i < allocations; i++) {
        size = BASE_PAGE_SIZE << (i % 9);

        err = aos_ram_free(capabilities[i], size);
        if (err_is_fail(err)) {
            debug_printf("Failed freeing capability %i\n", i);
            return false;
        }
    }
    debug_printf("frees test succeeded\n");
    return true;
}

__attribute__((unused))
bool test_virtual_memory(int count, int size) {
    errval_t err;

    for (int i = 0 ; i < count ; ++i) {
        size_t retsize;
        struct capref frame;
        void *buf;
        frame_alloc(&frame, size, &retsize);
        err = paging_map_frame_readwrite(&buf, size, frame);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "Failed mapping %i\n", i);
            return false;
        } else {
            debug_printf("Succeeded mapping %i\n", i);
            debug_printf("-----------------------------\n");
        }
        ((int *)buf)[0] = 42;
    }
    debug_printf("virtual memory test succeeded\n");
    return true;
}

__attribute__((unused))
bool test_multi_spawn(int spawns) {
    errval_t err;

    for (int i = 0; i < spawns; i++) {
        struct spawninfo *si = malloc(sizeof(struct spawninfo));
        err = spawn_load_by_name("/armv7/sbin/hello", si);
        if (err_is_fail(err)) {
            debug_printf("Failed spawning process %i\n", i);
            return false;
        }
    }
    debug_printf("Spawning test succeeded\n");
    return true;
}

__attribute__((unused))
bool test_huge_malloc(void) {
    debug_printf("testing huge malloc\n");
    int *tmp = malloc(100 * 1024 * 1024);
    tmp[10000] = 42;
    tmp[10001] = 43;
    debug_printf("tmp=%p &tmp[10000]=%p\n", tmp, &tmp[10000]);
    debug_printf("testing huge malloc done\n");
    return true;
}

__attribute__((unused))
bool test_dynamic_slots(int count) {
    debug_printf("testing dynamic slots\n");
    for (int i = 0 ; i < count ; ++i) {
        struct capref slot;
        /* errval_t err = frame_alloc(&frame, BASE_PAGE_SIZE, NULL); */
        errval_t err = slot_alloc(&slot);
        if (err_is_fail(err)) {
            debug_printf("err at test: %d. err=%s\n", i, err_getstring(err));
            return false;
        }
    }
    debug_printf("testing dynamic slots done\n");
    return true;
}
