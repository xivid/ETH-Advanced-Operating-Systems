/**
 * \file
 * \brief A library for managing physical memory (i.e., caps)
 */

#include <mm/mm.h>
#include <aos/debug.h>

#define MIN_SPLIT_SIZE BASE_PAGE_SIZE
#define aligned(address, alignment) (address & (alignment -1)) == 0
#define INITIAL_SPLIT_ALIGNMENT 1024*1024 // 1MB

errval_t mm_find_smallest_node(struct mm *mm, size_t size, size_t alignment, struct mmnode **current);
struct mmnode *mm_create_node(struct mm *mm, struct capref cap, genpaddr_t base, size_t size);
void mm_insert_node(struct mm *mm, struct mmnode *new_node, struct mmnode *start);
void mm_delete_node(struct mm *mm, struct mmnode *node);

size_t largest_contained_power_of_2(size_t size);
void mm_print_node(struct mmnode *node);
void mm_traverse_list(struct mmnode *head);

errval_t split_cap(struct mm *mm, struct capref source,
        size_t left_size, size_t right_size, struct capref *left_slot,
        struct capref *right_slot);

bool can_split_with_alignment(struct mmnode *node, size_t alignment,
        size_t *left_size, size_t *right_size);

errval_t split_mmnode(struct mm *mm, struct mmnode *current,
        struct mmnode **left_node, struct mmnode **right_node,
        size_t left_size, size_t right_size);

__attribute__ ((unused))
void mm_print_node(struct mmnode *node) {
    debug_printf("-- Printing node --\n");
    debug_printf("node size: %llu\n", node->size);
    debug_printf("node base: %llu\n", node->base);
    debug_printf("node type is free: %i\n", node->type == NodeType_Free);
    debug_printf("\n");
}

 __attribute__ ((unused))
void mm_traverse_list(struct mmnode *head) {
    printf("Head=%p. ", head);
    printf("Node sizes: ");
    struct mmnode *cur = head;
    while (true) {
        if (cur->type == NodeType_Free) {
            printf("%llu (F), ", cur->size);
        } else {
            printf("%llu (T), ", cur->size);
        }
        if (cur->next == head) {
            break;
        }
        cur = cur->next;
    }
    printf("\n");
}

/**
 * Initialize the memory manager.
 *
 * \param  mm               The mm struct to initialize.
 * \param  objtype          The cap type this manager should deal with.
 * \param  slab_refill_func Custom function for refilling the slab allocator.
 * \param  slot_alloc_func  Function for allocating capability slots.
 * \param  slot_refill_func Function for refilling (making) new slots.
 * \param  slot_alloc_inst  Pointer to a slot allocator instance (typically passed to the alloc and refill functions).
 */
errval_t mm_init(struct mm *mm, enum objtype objtype,
                 slab_refill_func_t slab_refill_func,
                 slot_alloc_t slot_alloc_func,
                 slot_refill_t slot_refill_func,
                 void *slot_alloc_inst)
{
    assert(mm != NULL);
    if (slab_refill_func == NULL)
        slab_refill_func = slab_default_refill;
    slab_init(&(mm->slabs), sizeof(struct mmnode), slab_refill_func);

    mm->slot_alloc = slot_alloc_func;
    mm->slot_refill = slot_refill_func;
    mm->slot_alloc_inst = slot_alloc_inst;
    mm->allocating_ram = false;
    mm->objtype = objtype;
    mm->head = NULL;

    return SYS_ERR_OK;
}

/**
 * Destroys the memory allocator.
 */
void mm_destroy(struct mm *mm)
{
}

/**
 * Adds a capability to the memory manager.
 *
 * \param  cap  Capability to add
 * \param  base Physical base address of the capability
 * \param  size Size of the capability (in bytes)
 */
errval_t mm_add(struct mm *mm, struct capref cap, genpaddr_t base, size_t size)
{
    struct mmnode *new_memnode = mm_create_node(mm, cap, base, size);
    if (mm->head == NULL) {
        new_memnode->prev = new_memnode;
        new_memnode->next = new_memnode;
        mm->head = new_memnode;
    } else {
        mm_insert_node(mm, new_memnode, mm->head->prev);
    }

    errval_t err = mm_do_initial_split(mm);
    if (err_is_fail(err)) {
        debug_printf("failed to do the initial split\n");
        return err;
    }

    return SYS_ERR_OK;
}

/**
 * Does the splitting on the initial big chunk into two parts:
 * the left part is unaligned and used for requests with small alignments
 * the right part is aligned and used for requests with big alignments
 */
errval_t mm_do_initial_split(struct mm *mm) {
    struct mmnode *head = mm->head;
    size_t left_size, right_size;
    if (can_split_with_alignment(head, INITIAL_SPLIT_ALIGNMENT,
                &left_size, &right_size)) {
        struct mmnode *left, *right;
        errval_t err = split_mmnode(mm, mm->head, &left, &right,
                left_size, right_size);
        // TODO: change this dirty bugfix
        mm->head = right;
        return err;
    }
    debug_printf("Initial split failed\n");
    return MM_ERR_NEW_NODE;
}

/**
 * Allocate aligned physical memory.
 *
 * \param       mm        The memory manager.
 * \param       size      How much memory to allocate.
 * \param       alignment The alignment requirement of the base address for your memory.
 * \param[out]  retcap    Capability for the allocated region.
 */
errval_t mm_alloc_aligned(struct mm *mm, size_t size, size_t alignment, struct capref *retcap)
{
    size_t left_size = 0, right_size = 0;
    struct mmnode *left_node = NULL, *right_node = NULL;
    struct mmnode *current = mm->head;
    errval_t err;
    if (current == NULL) {
        DEBUG_ERR(MM_ERR_MISSING_CAPS, "Cannot allocate memory without caps\n");
        return MM_ERR_MISSING_CAPS;
    }

    err = mm_find_smallest_node(mm, size, alignment, &current);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "Error finding smallest node\n");
        return err;
    }

    if (!mm->allocating_ram) {
        mm->allocating_ram = true;

        // Split capability until it has the right size, don't split below 4k.
        while (current->size >= 2*size && current->size > MIN_SPLIT_SIZE) {
            if (!slot_alloc_enough_slots(mm->slot_alloc_inst, 2)) {
                err = slot_prealloc_refill(mm->slot_alloc_inst);
                if (err_is_fail(err)) {
                    DEBUG_ERR(err, "slot refill failed");
                    return err;
                }
            }

            // After slab or slot refill the current node may be already taken.
            if (current->type == NodeType_Allocated) {
                current = mm->head;
                err = mm_find_smallest_node(mm, size, alignment, &current);
                if (err_is_fail(err)) {
                    DEBUG_ERR(err,
                            "Error finding smallest node after slot refill\n");
                    return err;
                }
                // After slab/slot refill it may happen that a bigger chunk
                // was split into smaller chunks and one of them fits current
                // memory request. In this case we don't have to reiterate
                // again.
                if (current->size < 2*size) {
                    break;
                }
            }

            left_size = largest_contained_power_of_2(current->size);
            right_size = current->size - left_size;
            err = split_mmnode(mm, current, &left_node, &right_node,
                            left_size, right_size);
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "Failed at splitting node");
                return err;
            }

            current = right_node;
        }
        mm->allocating_ram = false;
    }
    current->type = NodeType_Allocated;
    assert(aligned(current->base, alignment));
    assert(current->size >= size);
    *retcap = current->cap;
    return SYS_ERR_OK;
}

/**
 * Allocate physical memory.
 *
 * Note that this implementation does not split memory but instead returns the
 * smallest existing slot that fits.
 *
 * \param       mm        The memory manager.
 * \param       size      How much memory to allocate.
 * \param[out]  retcap    Capability for the allocated region.
 */
errval_t mm_alloc(struct mm *mm, size_t size, struct capref *retcap)
{
    return mm_alloc_aligned(mm, size, BASE_PAGE_SIZE, retcap);
}

/**
 * Free a certain region (for later re-use).
 *
 * \param       mm        The memory manager.
 * \param       cap       The capability to free.
 * \param       base      The physical base address of the region.
 * \param       size      The size of the region.
 */
errval_t mm_free(struct mm *mm, struct capref cap, genpaddr_t base, gensize_t size)
{
    struct mmnode *current = mm->head;

    if (current == NULL) {
        printf("No capability slots to free");
        return MM_ERR_MISSING_CAPS;
    }

    while (current->cap.cnode.croot != cap.cnode.croot ||
           current->cap.cnode.cnode != cap.cnode.cnode ||
           current->cap.cnode.level != cap.cnode.level ||
           current->cap.slot != cap.slot) {
        current = current->next;
        if (current == mm->head) {
            printf("Trying to free a capability that was not added\n");
            return MM_ERR_NOT_FOUND;
        }
    }

    if (current->type != NodeType_Allocated) {
        printf("Double free of a capability");
        return MM_ERR_MM_FREE;
    }

    current->type = NodeType_Free;
    // TODO: merge with neighboring capability if it is free

    return SYS_ERR_OK;
}

/**
 * Find the largest power of two which is contained in size
 *
 * \return The largest power of two which fits into size with some extra space
 */
size_t largest_contained_power_of_2(size_t size) {
    size_t power = 1;
    while (power < size) {
         power = power << 1;
    }
    return power >> 1;
}

/**
 * Find the smallest free node that is large enough starting at current.
 *
 * \param       mm        The memory manager
 * \param       size      How much memory to allocate.
 * \param[out]  current   The smallest free node
 */
errval_t mm_find_smallest_node(struct mm *mm, size_t size, size_t alignment, struct mmnode **current) {

    assert(current != NULL && *current != NULL);
    while (!((*current)->type == NodeType_Free && (*current)->size >= size && aligned((*current)->base, alignment))) {
        *current = (*current)->next;
        assert(*current != NULL);
        if (*current == mm->head) {
            printf("No more capabilities left to hand out that are large enough\n");
            return MM_ERR_NEW_NODE;
        }
    }

    return SYS_ERR_OK;
}

/**
 * Alloc a memory node and fill it with data
 *
 * \param       mm        The memory manager
 * \param       cap       The capability to allocate the node for
 * \param       base      The physical base address of the region
 * \param       size      The size of the region
 * \param[out]  ret       The newly initialized memory node
 */
struct mmnode *mm_create_node(struct mm *mm, struct capref cap, genpaddr_t base, size_t size) {
    struct mmnode *ret = (struct mmnode *) slab_alloc(&(mm->slabs));
    if (!ret) {
        debug_printf("error in mm_create_node: failed to allocate new slab\n");
        return NULL;
    }
    ret->type = NodeType_Free;
    ret->cap = cap;
    ret->base = base;
    ret->size = size;
    return ret;
}

/**
 * Insert the given node into the node list
 *
 * The node will be inserted in sorted order provided the start node is larger
 * than the given node or start points to the head of the node list.
 *
 * \param       mm        The memory manager
 * \param       new_node  The node to insert
 * \param       start     The starting point for finding the insertion
 */
void mm_insert_node(struct mm *mm, struct mmnode *new_node, struct mmnode *start) {
    struct mmnode *current = start;
    while (current->size > new_node->size && current != mm->head) {
        current = current -> prev;
    }

    if (current == mm->head && current->size > new_node->size) {
        new_node->next = current;
        new_node->prev = current->prev;

        current->prev->next = new_node;
        current->prev = new_node;

        mm->head = new_node;
    } else {
        new_node->next = current->next;
        new_node->prev = current;
        current->next->prev = new_node;
        current->next = new_node;
    }
}

/**
 * Delete the given node from the node list.
 *
 * \param       mm        The memory manager
 * \param       node      The node to delete
 */
void mm_delete_node(struct mm *mm, struct mmnode *node) {
    assert(node != mm->head || node->prev != mm->head);
    // TODO: what if delete the only element from the list?
    node->next->prev = node->prev;
    node->prev->next = node->next;
    if (mm->head == node) {
        mm->head = node->next;
    }
    slab_free(&(mm->slabs), node);
}

errval_t split_cap(struct mm *mm, struct capref source, size_t left_size,
        size_t right_size, struct capref *left_slot, struct capref *right_slot) {
    // Init left part
    errval_t err = slot_alloc_prealloc(mm->slot_alloc_inst, 1, left_slot);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "failed allocating left slot");
        return err;
    }
    err = cap_retype(*left_slot, source, 0, mm->objtype, left_size, 1);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "left cap retype error: %s\n", err_getstring(err));
        return err;
    }
    // Init right part
    err = slot_alloc_prealloc(mm->slot_alloc_inst, 1, right_slot);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "failed allocating left slot");
        return err;
    }
    err = cap_retype(*right_slot, source, left_size, mm->objtype, right_size, 1);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "right cap retype error: %s\n", err_getstring(err));
        return err;
    }
    return SYS_ERR_OK;

}

bool can_split_with_alignment(struct mmnode *node, size_t alignment,
        size_t *left_size, size_t *right_size) {
    // Done like this to avoid int overflows.
    uint64_t cap_size = node->size;
    uint64_t base = node->base;
    uint64_t split_point = ROUND_UP(base, alignment);
    if (split_point < base + cap_size) {
        *left_size = (size_t) split_point - base;
        *right_size = (size_t) cap_size - *left_size;
        return true;
    }
    return false;
}

errval_t split_mmnode(struct mm *mm, struct mmnode *current,
        struct mmnode **left_node, struct mmnode **right_node,
        size_t left_size, size_t right_size) {
    struct capref left_slot, right_slot;
    split_cap(mm, current->cap, left_size, right_size, &left_slot,
            &right_slot);

    *left_node = mm_create_node(mm, left_slot, current->base, left_size);
    if (!left_node) {
        return MM_ERR_NEW_NODE;
    }
    mm_insert_node(mm, *left_node, current);

    *right_node = mm_create_node(mm, right_slot,
            current->base + left_size, right_size);
    if (!right_node) {
        return MM_ERR_NEW_NODE;
    }
    mm_insert_node(mm, *right_node, current);

    mm_delete_node(mm, current);
    return SYS_ERR_OK;
}
