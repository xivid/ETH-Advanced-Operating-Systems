/**
 * \file
 * \brief A library for managing physical memory (i.e., caps)
 */

#include <mm/mm.h>
#include <aos/debug.h>

#define MIN_SPLIT_SIZE BASE_PAGE_SIZE
#define aligned(address, alignment) (address & (alignment -1)) == 0

errval_t mm_find_smallest_node(struct mm *mm, size_t size, size_t alignment, struct mmnode **current);
void mm_fill_node(struct mm *mm, struct capref cap, genpaddr_t base, size_t size, struct mmnode *ret);
void mm_insert_node(struct mm *mm, struct mmnode *new_node, struct mmnode *start);
static size_t largest_contained_power_of_2(size_t size);

static void mm_print_node(struct mmnode *node);
static void mm_traverse_list(struct mmnode *head);

void mm_print_node(struct mmnode *node) {
    printf("-- Printing node --\n");
    printf("node size: %llu\n", node->size);
    printf("node base: %llu\n", node->base);
    printf("node type is free: %i\n", node->type == NodeType_Free);
    printf("\n");
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
    printf("Add capability with size %i\n", size);

    struct mmnode *new_memnode = (struct mmnode *) slab_alloc(&(mm->slabs));
    if (!new_memnode) {
        printf("slab refill not implemented yet\n");
        return LIB_ERR_NOT_IMPLEMENTED;
    }
    mm_fill_node(mm, cap, base, size, new_memnode);
    mm_print_node(new_memnode);

    if (mm->head == NULL) {
        new_memnode->prev = new_memnode;
        new_memnode->next = new_memnode;
        mm->head = new_memnode;
    } else {
        mm_insert_node(mm, new_memnode, mm->head->prev);
    }

    return SYS_ERR_OK;
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
    debug_printf("Call alloc_aligned with %i and %i \n", size, alignment);
    debug_printf("Free slabs: %i\n", slab_freecount(&(mm->slabs)));

    struct mmnode *current = mm->head;
    if (current == NULL) {
        DEBUG_ERR(MM_ERR_MISSING_CAPS, "Cannot allocate memory without caps\n");
        return MM_ERR_MISSING_CAPS;
    }

    errval_t err = mm_find_smallest_node(mm, size, alignment, &current);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "Error finding smallest node\n");
        return err;
    }

    if (!mm->allocating_ram) {
        mm->allocating_ram = true;

        // split capability until it has the right size, don't split below 1k.
        while (current->size >= 2*size && current->size > MIN_SPLIT_SIZE) {
            struct mmnode *left_split = (struct mmnode *) slab_alloc(&(mm->slabs));
            if (!left_split) {
                DEBUG_ERR(LIB_ERR_NOT_IMPLEMENTED,
                        "Failed to allocate a new slab\n");
                return LIB_ERR_NOT_IMPLEMENTED;
            } else if (current->type == NodeType_Allocated) {
                current = mm->head;
                err = mm_find_smallest_node(mm, size, alignment, &current);
                if (err_is_fail(err)) {
                    DEBUG_ERR(err,
                            "Error finding smallest node after refill\n");
                    return err;
                }
            }

            struct capref new_cap_slot;
            err = slot_alloc_prealloc(mm->slot_alloc_inst, 2, &new_cap_slot);
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "failed allocating two slots");
                return err;
            }
            size_t left_split_size = largest_contained_power_of_2(current->size);
            err = cap_retype(new_cap_slot, current->cap.cap, 0, mm->objtype, left_split_size, 1);
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "cap retype error: %s\n", err_getstring(err));
                return err;
            }
            new_cap_slot.slot++;
            err = cap_retype(new_cap_slot, current->cap.cap, left_split_size, mm->objtype, current->cap.size - left_split_size, 1);
            if (err_is_fail(err)) {
                DEBUG_ERR(err,
                        "cap retype resize error: %s\n", err_getstring(err));
                return err;
            }
            new_cap_slot.slot--;
            
            mm_fill_node(mm, new_cap_slot, current->cap.base, left_split_size, left_split);
            mm_insert_node(mm, left_split, current);
            
            current->next->prev = current->prev;
            current->prev->next = current->next;
            if (mm->head == current) {
                mm->head = current->next;
            }

            struct mmnode *right_split = current;
            new_cap_slot.slot++;
            mm_fill_node(mm, new_cap_slot, current->cap.base + left_split_size, current->cap.size - left_split_size, right_split);
            mm_insert_node(mm, right_split, left_split);
            
            current = right_split;
        }
        mm->allocating_ram = false;
    }
    current->type = NodeType_Allocated;

    debug_printf("End call alloc_aligned with %d and %d, return size %llu \n",
            size, alignment, current->size);
    assert(aligned(current->cap.base, alignment));
    printf("---------------------------------\n");

    *retcap = current->cap.cap;
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
    printf("Called mm_alloc!!");
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

    while (current->cap.cap.cnode.croot != cap.cnode.croot ||
           current->cap.cap.cnode.cnode != cap.cnode.cnode ||
           current->cap.cap.cnode.level != cap.cnode.level ||
           current->cap.cap.slot != cap.slot) {
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
static size_t largest_contained_power_of_2(size_t size) {
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
 * \param       alignment The addres the mmnode should be aligned to
 * \param[out]  current   The smallest free node
 */
errval_t mm_find_smallest_node(struct mm *mm, size_t size, size_t alignment, struct mmnode **current) {

    while (1) {
        if ((*current)->type == NodeType_Free && (*current)->cap.size >= size && aligned((*current)->cap.base, alignment)) {
            break;
        }

        *current = (*current)->next;
        if (*current == mm->head) {
            printf("No more capabilities left to hand out that are large enough\n");
            return MM_ERR_NEW_NODE;
        }
    }

    return SYS_ERR_OK;
}

/**
 * Fill a memory node it with the capability
 *
 * \param       mm        The memory manager
 * \param       cap       The capability to allocate the node for
 * \param       base      The physical base address of the region
 * \param       size      The size of the region
 * \param[out]  ret       The newly initialized memory node
 */
void mm_fill_node(struct mm *mm, struct capref cap, genpaddr_t base, size_t size, struct mmnode *ret) {
    ret->type = NodeType_Free;
    ret->cap.cap = cap;
    ret->cap.base = base;
    ret->cap.size = size;
    ret->base = base;
    ret->size = size;
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
