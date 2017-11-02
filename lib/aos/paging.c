/**
 * \file
 * \brief AOS paging helpers.
 */

/*
 * Copyright (c) 2012, 2013, 2016, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <aos/aos.h>
#include <aos/paging.h>
#include <aos/except.h>
#include <aos/slab.h>
#include "threads_priv.h"

#include <stdio.h>
#include <string.h>

static struct paging_state current;

static void exception_handler(enum exception_type type, int subtype,
                              void *addr, arch_registers_state_t *regs,
                              arch_registers_fpu_state_t *fpuregs);
static errval_t init_l2_pagetab(struct paging_state *st, struct capref *ret,
                                struct capref l1_cap, lvaddr_t index_l1,
                                int flags);

// Helper functions to work with the free list.
static void free_list_dump(struct paging_state *st);
static void free_list_node_insert(struct paging_state *st,
        struct paging_region *node);
static void free_list_node_dec_size(struct paging_state *st,
        struct paging_region *node, size_t decrement);
static void free_list_init_node(struct paging_region *node,
        lvaddr_t base, lvaddr_t cur, size_t size, struct paging_region *next,
        struct paging_region *prev);
static struct paging_region *free_list_create_node(struct paging_state *st,
        lvaddr_t base, lvaddr_t cur, size_t size, struct paging_region *next,
        struct paging_region *prev);
static struct paging_region *free_list_find_node(struct paging_state *st,
        size_t size);
/**
 * \brief Helper function that allocates a slot and
 *        creates a ARM l2 page table capability
 */
__attribute__((unused))
static errval_t arml2_alloc(struct paging_state * st, struct capref *ret)
{
    errval_t err;
    err = st->slot_alloc->alloc(st->slot_alloc, ret);
    if (err_is_fail(err)) {
        debug_printf("slot_alloc failed: %s\n", err_getstring(err));
        return err;
    }
    err = vnode_create(*ret, ObjType_VNode_ARM_l2);
    if (err_is_fail(err)) {
        debug_printf("vnode_create failed: %s\n", err_getstring(err));
        return err;
    }
    return SYS_ERR_OK;
}

errval_t paging_init_state(struct paging_state *st, lvaddr_t start_vaddr,
        struct capref pdir, struct slot_allocator * ca)
{
    debug_printf("paging_init_state got called\n");
    st->l1_capref = pdir;
    st->slot_alloc = ca;
    for (int i = 0; i < ARM_L1_MAX_ENTRIES; ++i) {
        st->l2_pagetabs[i].initialized = false;
    }

    // Set up page fault handler. The exception stack is a static buffer.
#define STACK_SIZE 8*BASE_PAGE_SIZE
    static char exception_stack[STACK_SIZE];
    errval_t err = thread_set_exception_handler(exception_handler, NULL,
            exception_stack, &exception_stack[STACK_SIZE-1], NULL, NULL);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "failed setting a page fault handler");
        return err;
    }
    const lvaddr_t VIRTUAL_SPACE_SIZE = 0xffffffff;

    // Init slab and give it some memory.
    slab_init(&st->slabs, sizeof(struct paging_region), slab_default_refill);

    // Initialize head.
    free_list_init_node(&st->first_region, start_vaddr, start_vaddr,
            (size_t) VIRTUAL_SPACE_SIZE - start_vaddr, NULL, NULL);
    st->free_list_head = &st->first_region;
    debug_printf("finished the slab init\n");
    return SYS_ERR_OK;
}

/**
 * \brief Handles exceptions.
 */

__attribute__((noreturn))
void exception_handler(enum exception_type type, int subtype,
                                     void *addr, arch_registers_state_t *regs,
                                     arch_registers_fpu_state_t *fpuregs) {
    debug_printf("an exception occured\n");
    while (true) {
    }
}
/**
 * \brief This function initializes the paging for this domain
 * It is called once before main.
 */
errval_t paging_init(void)
{
    debug_printf("paging_init\n");
    // TODO (M4): initialize self-paging handler
    // TIP: use thread_set_exception_handler() to setup a page fault handler
    // TIP: Think about the fact that later on, you'll have to make sure that
    // you can handle page faults in any thread of a domain.
    // TIP: it might be a good idea to call paging_init_state() from here to
    // avoid code duplication.
    struct slot_allocator *default_sa = get_default_slot_allocator();
    // The initial offset has to be BASE_PAGE_SIZE aligned!
    lvaddr_t initial_offset = VADDR_OFFSET; // 1 GB
    struct capref l1_cap_dest = {
        .cnode = cnode_page,
        .slot = 0,
    };
    // The order here is important: current paging state has to be set up
    // before calling paging_init_state, because paging_init_state calls
    // paging_map_fixed_attr.
    set_current_paging_state(&current);
    paging_init_state(&current, initial_offset, l1_cap_dest, default_sa);
    return SYS_ERR_OK;
}


/**
 * \brief Initialize per-thread paging state
 */
void paging_init_onthread(struct thread *t)
{
    // TODO (M4): setup exception handler for thread `t'.
}

/**
 * \brief return a pointer to a bit of the paging region `pr`.
 * This function gets used in some of the code that is responsible
 * for allocating Frame (and other) capabilities.
 */
errval_t paging_region_init(struct paging_state *st, struct paging_region *pr, size_t size)
{
    void *base;
    errval_t err = paging_alloc(st, &base, size);
    if (err_is_fail(err)) {
        debug_printf("paging_region_init: paging_alloc failed\n");
        return err_push(err, LIB_ERR_VSPACE_MMU_AWARE_INIT);
    }
    pr->base_addr    = (lvaddr_t)base;
    pr->current_addr = pr->base_addr;
    pr->region_size  = size;
    return SYS_ERR_OK;
}

/**
 * \brief return a pointer to a bit of the paging region `pr`.
 * This function gets used in some of the code that is responsible
 * for allocating Frame (and other) capabilities.
 */
errval_t paging_region_map(struct paging_region *pr, size_t req_size,
                           void **retbuf, size_t *ret_size)
{
    lvaddr_t end_addr = pr->base_addr + pr->region_size;
    ssize_t rem = end_addr - pr->current_addr;
    if (rem > req_size) {
        // ok
        *retbuf = (void*)pr->current_addr;
        *ret_size = req_size;
        pr->current_addr += req_size;
    } else if (rem > 0) {
        *retbuf = (void*)pr->current_addr;
        *ret_size = rem;
        pr->current_addr += rem;
        debug_printf("exhausted paging region, "
                "expect badness on next allocation\n");
    } else {
        return LIB_ERR_VSPACE_MMU_AWARE_NO_SPACE;
    }
    return SYS_ERR_OK;
}

/**
 * \brief free a bit of the paging region `pr`.
 * This function gets used in some of the code that is responsible
 * for allocating Frame (and other) capabilities.
 * NOTE: Implementing this function is optional.
 */
errval_t paging_region_unmap(struct paging_region *pr, lvaddr_t base, size_t bytes)
{
    // TIP: you will need to keep track of possible holes in the region
    return SYS_ERR_OK;
}

/**
 *
 * \brief Find a bit of free virtual address space that is large enough to
 *        accomodate a buffer of size `bytes`.
 */
errval_t paging_alloc(struct paging_state *st, void **buf, size_t bytes)
{
    size_t round_size = ROUND_UP(bytes, BASE_PAGE_SIZE);
    struct paging_region *node = free_list_find_node(st, round_size);
    if (!node) {
        return LIB_ERR_VSPACE_MMU_AWARE_NO_SPACE;
    }
    *buf = (void *) node->base_addr;
    free_list_node_dec_size(st, node, round_size);
    return SYS_ERR_OK;
}

/**
 * \brief A wrapper around paging_map_frame_attr
 */
errval_t paging_map_frame_readwrite(void **buf, size_t bytes, struct capref frame) {
    return paging_map_frame_attr(&current, buf, bytes, frame, VREGION_FLAGS_READ_WRITE, NULL, NULL);
}

/**
 * \brief map a user provided frame, and return the VA of the mapped
 *        frame in `buf`.
 */
errval_t paging_map_frame_attr(struct paging_state *st, void **buf,
                               size_t bytes, struct capref frame,
                               int flags, void *arg1, void *arg2)
{
    errval_t err = paging_alloc(st, buf, bytes);
    if (err_is_fail(err)) {
        return err;
    }
    return paging_map_fixed_attr(st, (lvaddr_t)(*buf), frame, bytes, flags);
}

errval_t
slab_refill_no_pagefault(struct slab_allocator *slabs, struct capref frame, size_t minbytes)
{
    //TODO: implement me
    // Refill the two-level slot allocator without causing a page-fault
    return LIB_ERR_NOT_IMPLEMENTED;
}

errval_t init_l2_pagetab(struct paging_state *st, struct capref *ret,
                         struct capref l1_cap, lvaddr_t index_l1, int flags) {
    errval_t err = arml2_alloc(st, ret);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "arml2_alloc failed\n");
        return err;
    }
    struct capref mapping;
    err = st->slot_alloc->alloc(st->slot_alloc, &mapping);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "slot allocation failed");
        return err;
    }
    err = vnode_map(l1_cap, *ret, index_l1, flags, 0, 1, mapping);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "vnode_map failed");
        return err;
    }
    st->l2_pagetabs[index_l1].initialized = true;
    st->l2_pagetabs[index_l1].cap = *ret;
    return SYS_ERR_OK;
}

/**
 * \brief map a user provided frame at user provided VA.
 */
errval_t paging_map_fixed_attr(struct paging_state *st, lvaddr_t vaddr,
        struct capref frame, size_t bytes, int flags)
{
    struct capref l1_cap_dest = st->l1_capref;
    errval_t err;
    lvaddr_t cur_vaddr = vaddr;
    size_t bytes_left = ROUND_UP(bytes, BASE_PAGE_SIZE);
    while (bytes_left > 0) {
        struct capref l2_cap;
        lvaddr_t index_l1 = ARM_L1_OFFSET(cur_vaddr);
        if (!st->l2_pagetabs[index_l1].initialized) {
            err = init_l2_pagetab(st, &l2_cap, l1_cap_dest, index_l1, flags);
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "l2 pagetab initialisation failed");
                return err;
            }
        } else {
            l2_cap = st->l2_pagetabs[index_l1].cap;
        }

        struct capref mapping;
        err = st->slot_alloc->alloc(st->slot_alloc, &mapping);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "slot allocation failed");
            return err;
        }

        lvaddr_t l2_offset = ARM_L2_OFFSET(cur_vaddr);
        uint64_t source_offset = (uint64_t) (cur_vaddr - vaddr);
        uint64_t pte_count = MIN(bytes_left / BASE_PAGE_SIZE,
                ARM_L2_MAX_ENTRIES - l2_offset);
        err = vnode_map(l2_cap, frame, l2_offset, flags,
                        source_offset, pte_count, mapping);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "vnode_map failed");
            return err;
        }
        bytes_left -= pte_count * BASE_PAGE_SIZE;
        cur_vaddr += pte_count * BASE_PAGE_SIZE;
    }

    return SYS_ERR_OK;
}

/**
 * \brief unmap region starting at address `region`.
 * NOTE: Implementing this function is optional.
 */
errval_t paging_unmap(struct paging_state *st, const void *region)
{
    return SYS_ERR_OK;
}

__attribute__((unused))
static void free_list_dump(struct paging_state *st)
{
    struct paging_region *cur = st->free_list_head;
    debug_printf("paging free list:\n");
    while (cur) {
        debug_printf("b=0x%x s=0x%x\n", cur->current_addr, cur->region_size);
        cur = cur->next;
    }
    debug_printf("------------------------------------------\n");
}

__attribute__((unused))
static struct paging_region *free_list_create_node(struct paging_state *st,
        lvaddr_t base, lvaddr_t cur, size_t size, struct paging_region *next,
        struct paging_region *prev)
{
    struct paging_region *node = slab_alloc(&st->slabs);
    free_list_init_node(node, base, cur, size, next, prev);
    return node;
}

static void free_list_init_node(struct paging_region *node,
        lvaddr_t base, lvaddr_t cur, size_t size, struct paging_region *next,
        struct paging_region *prev)
{
    node->base_addr = base;
    node->current_addr = cur;
    node->region_size = size;
    node->prev = next;
    node->next = prev;
}
__attribute__((unused))
static void free_list_node_insert(struct paging_state *st,
        struct paging_region *node)
{
}

static struct paging_region *free_list_find_node(struct paging_state *st,
        size_t size)
{
    struct paging_region *cur = st->free_list_head;
    while (cur) {
        if (cur->region_size >= size) {
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}

static void free_list_node_dec_size(struct paging_state *st,
        struct paging_region *node, size_t decrement)
{
    if (node->region_size > decrement) {
        // We don't need to delete the node from the free list.
        node->region_size -= decrement;
        node->base_addr += decrement;
        node->current_addr += decrement;
    } else {
        // Delete the node.
        assert(st->free_list_head != node);
        struct paging_region *next = node->next;
        struct paging_region *prev = node->prev;
        if (next) {
            next->prev = prev;
        }
        if (prev) {
            prev->next = next;
        }
    }

}
