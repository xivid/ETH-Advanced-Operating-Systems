/**
 * \file
 * \brief Slot allocator for two level CSpace
 */

/*
 * Copyright (c) 2016, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <aos/aos.h>
#include <stdlib.h>
#include "internal.h"

static errval_t rootcn_alloc(void *st, size_t reqsize, struct capref *ret)
{
    return ram_alloc(ret, reqsize);
}

/**
 * \brief slot allocator
 *
 * \param ca   Instance of the allocator
 * \param ret  Pointer to return the allocated slot
 */
errval_t two_level_alloc(struct slot_allocator *ca, struct capref *ret)
{
    errval_t err = SYS_ERR_OK;
    struct multi_slot_allocator *mca = (struct multi_slot_allocator*)ca;

    thread_mutex_lock(&ca->mutex);
    assert(ca->space != 0);
    ca->space--;

    /* Try allocating from the list of single slot allocators */
    struct slot_allocator_list *walk = mca->head;
    //struct slot_allocator_list *prev = NULL;
    while(walk != NULL) {
        err = walk->a.a.alloc(&walk->a.a, ret);
        if (err_no(err) != LIB_ERR_SLOT_ALLOC_NO_SPACE) {
            break;
        }
        //prev = walk;
        walk = walk->next;
    }
    if (err_is_fail(err)) {
        thread_mutex_unlock(&ca->mutex);
        return err_push(err, LIB_ERR_SINGLE_SLOT_ALLOC);
    }

    /* If no more slots left, grow */
    /* It works the following way:
     * For new slots we need some slab memory. If we ran out of slab memory,
     * we need to grow the slab. To grow the slab we need at least one slot.
     * Thus we get an infinite recursion. To avoid it the flag
     * "growing_in_progress" and a non-zero limit for minimal number of slots
     * were added.
     */
    if (!ca->growing_in_progress && ca->space < 10) {
        ca->growing_in_progress = true;
        ca->space += ca->nslots;
        /* Pull in the reserve */
        mca->reserve->next = mca->head;
        mca->head = mca->reserve;

        /* Setup a new reserve */
        // Cnode: in Root CN
        struct capref cap;
        struct cnoderef cnode;
        err = slot_alloc_root(&cap);
        thread_mutex_unlock(&ca->mutex);
        // From here: we may call back into slot_alloc when resizing root
        // cnode and/or creating new L2 Cnode.
        if (err_no(err) == LIB_ERR_SLOT_ALLOC_NO_SPACE) {
            // resize root slot allocator (and rootcn)
            err = root_slot_allocator_refill(rootcn_alloc, NULL);
            if (err_is_fail(err)) {
                return err_push(err, LIB_ERR_ROOTSA_RESIZE);
            }
            err = slot_alloc_root(&cap);
        }
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "slot_alloc_root failed");
            return err_push(err, LIB_ERR_SLOT_ALLOC);
        }
        err = cnode_create_raw(cap, &cnode, ObjType_L2CNode, ca->nslots, NULL);
        if (err_is_fail(err)) {
            return err_push(err, LIB_ERR_CNODE_CREATE);
        }
        thread_mutex_lock(&ca->mutex);

        // Buffers
        void *buf = slab_alloc(&mca->slab);
        if (!buf) { /* Grow slab */
            // Allocate slot out of the list
            mca->a.space--;

            thread_mutex_unlock(&ca->mutex);

            // get slot for frame for refilling slab allocator
            struct capref frame;
            err = mca->a.alloc(&mca->a, &frame);
            if (err_is_fail(err)) {
                return err_push(err, LIB_ERR_SLOT_ALLOC);
            }
            // use slab refill function that never causes a pagefault
            err = slab_refill_no_pagefault(&mca->slab, frame, mca->slab.blocksize);
            if (err_is_fail(err)) {
                return err_push(err, LIB_ERR_SLAB_REFILL);
            }
            thread_mutex_lock(&ca->mutex);

            // Try allocating again
            buf = slab_alloc(&mca->slab);
            if (!buf) {
                thread_mutex_unlock(&ca->mutex);
                return err_push(err, LIB_ERR_SLAB_ALLOC_FAIL);
            }
        }

        mca->reserve = buf;
        buf = (char *)buf + sizeof(struct slot_allocator_list);
        size_t bufsize = mca->slab.blocksize - sizeof(struct slot_allocator_list);

        // Allocator
        err = single_slot_alloc_init_raw(&mca->reserve->a, cap, cnode,
                                         mca->a.nslots, buf, bufsize);
        if (err_is_fail(err)) {
            thread_mutex_unlock(&ca->mutex);
            return err_push(err, LIB_ERR_SINGLE_SLOT_ALLOC_INIT_RAW);
        }
        ca->growing_in_progress = false;
    }

    thread_mutex_unlock(&ca->mutex);
    return SYS_ERR_OK;
}

/**
 * \brief Free an allocated slot
 *
 * \param ca  Instance of the allocator
 * \param cap The slot to free
 *
 * Walks the list of single slot allocators trying to free the slot.
 */
errval_t two_level_free(struct slot_allocator *ca, struct capref cap)
{
    errval_t err;
    thread_mutex_lock(&ca->mutex);
    struct multi_slot_allocator *mca = (struct multi_slot_allocator*)ca;
    struct slot_allocator_list *walk = mca->head;

    while(walk != NULL) {
        err = walk->a.a.free(&walk->a.a, cap);
        if (err_is_ok(err)) {
            mca->a.space++;
        }
        if (err_no(err) != LIB_ERR_SLOT_ALLOC_WRONG_CNODE) {
            thread_mutex_unlock(&ca->mutex);
            return err;
        }
        walk = walk->next;
    }

    thread_mutex_unlock(&ca->mutex);
    return LIB_ERR_SLOT_ALLOC_WRONG_CNODE;
}

/**
 * \brief Initializer that does not allocate any space
 *
 * #slot_alloc_init duplicates some of the code below,
 * modify it if making changes here.
 *
 * XXX: top_buf head_buf and reserve_buf each point to a separate buffer of
 * size bufsize bytes which can be used for backing storage. bufsize evidently
 * needs to be >= sizeof(struct cnode_meta) * nslots / 2. Don't ask me why! -AB
 */
errval_t two_level_slot_alloc_init_raw(struct multi_slot_allocator *ret,
                                       struct capref initial_cap,
                                       struct cnoderef initial_cnode,
                                       struct capref reserve_cap,
                                       struct cnoderef reserve_cnode,
                                       void *head_buf, void *reserve_buf, size_t bufsize)
{
    errval_t err;

    /* Generic part */
    ret->a.alloc = two_level_alloc;
    ret->a.free  = two_level_free;
    ret->a.space = L2_CNODE_SLOTS;
    ret->a.nslots = L2_CNODE_SLOTS;
    ret->a.growing_in_progress = false;
    thread_mutex_init(&ret->a.mutex);

    // Top unused in two-level allocator

    ret->head->next = NULL;
    ret->reserve->next = NULL;

    /* Head */
    err = single_slot_alloc_init_raw(&ret->head->a, initial_cap,
                                     initial_cnode, L2_CNODE_SLOTS, head_buf,
                                     bufsize);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_SINGLE_SLOT_ALLOC_INIT);
    }

    /* Reserve */
    err = single_slot_alloc_init_raw(&ret->reserve->a, reserve_cap,
                                     reserve_cnode, L2_CNODE_SLOTS,
                                     reserve_buf, bufsize);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_SINGLE_SLOT_ALLOC_INIT);
    }

    /* Slab */
    size_t allocation_unit = sizeof(struct slot_allocator_list) +
                             SINGLE_SLOT_ALLOC_BUFLEN(L2_CNODE_SLOTS);
    slab_init(&ret->slab, allocation_unit, NULL);

    return SYS_ERR_OK;
}

/**
 * \brief Initializer that uses memory
 */
errval_t two_level_slot_alloc_init(struct multi_slot_allocator *ret)
{
    errval_t err;
    size_t bufsize = SINGLE_SLOT_ALLOC_BUFLEN(L2_CNODE_SLOTS); // XXX?

    void *top_buf = malloc(bufsize);
    if (!top_buf) {
        return LIB_ERR_MALLOC_FAIL;
    }

    ret->head = malloc(sizeof(struct slot_allocator_list));
    if (!ret->head) {
        return LIB_ERR_MALLOC_FAIL;
    }
    ret->head->next = NULL;
    void *head_buf = malloc(bufsize);
    if (!head_buf) {
        return LIB_ERR_MALLOC_FAIL;
    }

    ret->reserve = malloc(sizeof(struct slot_allocator_list));
    if (!ret->reserve) {
        return LIB_ERR_MALLOC_FAIL;
    }
    void *reserve_buf = malloc(bufsize);
    if (!reserve_buf) {
        return LIB_ERR_MALLOC_FAIL;
    }

    size_t allocation_unit = sizeof(struct slot_allocator_list) + bufsize;
    err = paging_region_init(get_current_paging_state(), &ret->region,
            allocation_unit * L2_CNODE_SLOTS * 2);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_VSPACE_MMU_AWARE_INIT);
    }

    struct capref initial_cap, reserve_cap;
    struct cnoderef initial_cnode, reserve_cnode;
    err = cnode_create_l2(&initial_cap, &initial_cnode);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CNODE_CREATE);
    }
    err = cnode_create_l2(&reserve_cap, &reserve_cnode);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CNODE_CREATE);
    }
    err = two_level_slot_alloc_init_raw(ret, initial_cap, initial_cnode,
                                        reserve_cap, reserve_cnode, head_buf,
                                        reserve_buf, bufsize);
    return SYS_ERR_OK;
}
