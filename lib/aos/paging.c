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
                                struct capref l1_cap, lvaddr_t index_l1, int flags);

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
    // TODO (M4): Implement page fault handler that installs frames when a page fault
    // occurs and keeps track of the virtual address space.
    st->l1_capref = pdir;
    st->slot_alloc = ca;
    st->next_free_addr = start_vaddr;
    for (int i = 0; i < ARM_L1_MAX_ENTRIES; ++i) {
        st->l2_pagetabs[i].initialized = false;
    }
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
    lvaddr_t initial_offset = VADDR_OFFSET; // 1 GB
    struct capref l1_cap_dest = {
        .cnode = cnode_page,
        .slot = 0,
    };
    paging_init_state(&current, initial_offset, l1_cap_dest, default_sa);
    set_current_paging_state(&current);
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
    *buf = (void *) st->next_free_addr;
    st->next_free_addr += round_size;
    // TODO: detect integer overflows
    // TODO: think about a smarter strategy, we'll need it for freeing memory
    // and defragmentation issues
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
    // Refill the two-level slot allocator without causing a page-fault
    return SYS_ERR_OK;
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
        uint64_t pte_count = MIN(bytes_left / BASE_PAGE_SIZE, ARM_L2_MAX_ENTRIES - l2_offset);
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
 * \brief unmap a user provided frame, and return the VA of the mapped
 *        frame in `buf`.
 * NOTE: Implementing this function is optional.
 */
errval_t paging_unmap(struct paging_state *st, const void *region)
{
    return SYS_ERR_OK;
}
