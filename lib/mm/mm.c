/**
 * \file
 * \brief A library for managing physical memory (i.e., caps)
 */

#include <mm/mm.h>
#include <aos/debug.h>

#define MIN_SPLIT_SIZE BASE_PAGE_SIZE
#define aligned(address, alignment) (address & (alignment -1)) == 0
#define INITIAL_SPLIT_ALIGNMENT 1024*1024 // 1MB
#define MIN_FREE_SLOTS 15 // How many do we really need?

errval_t mm_find_smallest_node(struct mm *mm, size_t size, size_t alignment,
                               struct mmnode **current);
struct mmnode *mm_find_node_by_base(struct mm *mm, size_t base, enum nodetype type);
struct mmnode *mm_create_node(struct mm *mm, struct capref cap, genpaddr_t base,
                              size_t size);
void mm_insert_node(struct mm *mm, struct mmnode *new_node,
                    struct mmnode *start);
errval_t mm_delete_node(struct mm *mm, struct mmnode *node, bool destroy_cap);
void mm_extract_node(struct mm *mm, struct mmnode *node);

size_t largest_contained_power_of_2(size_t size);

errval_t split_cap(struct mm *mm, struct mmnode *source, size_t offset,
                   struct capref *left_slot, struct capref *right_slot);

bool can_split_with_alignment(struct mmnode *node, size_t alignment,
                              size_t *right_off);

errval_t split_mmnode_final(struct mm *mm, struct mmnode *current,
                            size_t right_offset);
errval_t break_off_cap(struct mm *mm, struct mmnode *current,
                       size_t left_size, struct capref *left_cap);

errval_t refill_slots_if_needed(struct mm *mm, bool *refilled);

__attribute__ ((unused))
void mm_print_node(struct mmnode *node) {
    debug_printf("-- Printing node --\n");
    debug_printf("node size: %llu\n", node->size);
    debug_printf("node base: %llu\n", node->base);
    debug_printf("node type is internal: %i\n", node->type == NodeType_Keep);
    debug_printf("\n");
}

 __attribute__ ((unused))
void mm_traverse_list(struct mmnode *head) {
    printf("Head=%p. ", head);
    printf("Node sizes: ");
    struct mmnode *cur = head;
    while (true) {
        if (cur->type == NodeType_Keep) {
            printf("%llu [%llu] (keep), ", cur->size, cur->base);
        } else {
            printf("%llu [%llu] (hand), ", cur->size, cur->base);
        }
        if (cur->next == head) {
            break;
        }
        cur = cur->next;
    }
    printf("\n");
}


/**
 * \brief Initialize the memory manager.
 *
 * \param  mm               The mm struct to initialize.
 * \param  objtype          The cap type this manager should deal with.
 * \param  slab_refill_func Custom function for refilling the slab allocator.
 * \param  slot_alloc_func  Function for allocating capability slots.
 * \param  slot_refill_func Function for refilling (making) new slots.
 * \param  slot_alloc_inst  Pointer to a slot allocator instance (typically
 *                          passed to the alloc and refill functions).
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
    thread_mutex_init(&mm->mm_mutex);

    return SYS_ERR_OK;
}

/**
 * \brief Destroys the memory allocator.
 */
void mm_destroy(struct mm *mm)
{
}

/**
 * \brief Adds a capability to the memory manager.
 *
 * \param  cap  Capability to add
 * \param  base Physical base address of the capability
 * \param  size Size of the capability (in bytes)
 */
errval_t mm_add(struct mm *mm, struct capref cap, genpaddr_t base, size_t size)
{
    struct frame_identity ident;
    frame_identify(cap, &ident);
    assert(ident.base <= base && ident.bytes >= size);

    struct mmnode *new_memnode;
    if (ident.base != base || ident.bytes != size) {
        // as we have given half of the memory region to app core but did not change the cap,
        // we need to create a new cap conforming to the new size and base here
        struct capref new_cap;
        errval_t err = slot_alloc_prealloc(mm->slot_alloc_inst, 1, &new_cap);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "failed allocating new_cap slot in mm_add");
            return err;
        }
        err = cap_retype(new_cap, cap, base - ident.base, mm->objtype, size, 1);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "original cap retype error: %s\n", err_getstring(err));
            return err;
        }
        new_memnode = mm_create_node(mm, new_cap, base, size);
    }
    else {
        new_memnode = mm_create_node(mm, cap, base, size);
    }

    if (new_memnode == NULL) {
        debug_printf("Not possible to create new mmnode\n");
        return MM_ERR_NEW_NODE;
    }
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

    err = refill_slots_if_needed(mm, NULL);
    if (err_is_fail(err)) {
        debug_printf("failed to refill slots in add\n");
        return err;
    }

    assert(mm->head != NULL);

    return SYS_ERR_OK;
}

/**
 * \brief Split large capability into large aligned part and unaligned part
 *
 * The left part is not guaranteed to be aligned to anything and might be used
 * for requests with small alignments.
 * The right part is aligned to INITIAL_SPLIT_ALIGNMENT.
 */
errval_t mm_do_initial_split(struct mm *mm)
{
    size_t right_offset;
    if (!can_split_with_alignment(mm->head, INITIAL_SPLIT_ALIGNMENT,
                                  &right_offset)) {
        debug_printf("Cannot do initial split with alignment\n");
        return MM_ERR_NEW_NODE;
    }

    debug_printf("Doing initial split with size %i and size %i (base=%llx)\n", right_offset, mm->head->size - right_offset, mm->head->base);

    if (right_offset > 0) {
        errval_t err = split_mmnode_final(mm, mm->head, right_offset);
        if (err_is_fail(err)) {
            debug_printf("Failed to split initial mmnode\n");
            return err;
        }
    }
    return SYS_ERR_OK;
}

/**
 * \brief check if can split the node with the given alignment
 *
 * \param       node            The node to check
 * \param       alignment       The alignment
 * \param[out]  right_off       Offset of the right node in the given node
 *
 * \return Whether a split is feasible
 */
bool can_split_with_alignment(struct mmnode *node, size_t alignment,
                              size_t *right_off)
{
    uint64_t base = node->base;
    uint64_t split_point = ROUND_UP(base, alignment);

    *right_off = (size_t) split_point - base;
    return *right_off < node->size;
}

/**
 * Allocate aligned physical memory.
 *
 * \param       mm        The memory manager.
 * \param       size      How much memory to allocate.
 * \param       alignment The alignment requirement of the base address
 * \param[out]  retcap    Capability for the allocated region.
 */
errval_t mm_alloc_aligned(struct mm *mm, size_t size, size_t alignment,
                          struct capref *retcap)
{
    errval_t err;
    thread_mutex_lock_nested(&mm->mm_mutex);
    struct mmnode *current = mm->head;
    if (current == NULL) {
        debug_printf("Cannot allocate memory without caps\n");
        thread_mutex_unlock(&mm->mm_mutex);
        return MM_ERR_MISSING_CAPS;
    }

    err = mm_find_smallest_node(mm, size, alignment, &current);
    if (err_is_fail(err)) {
        debug_printf("Error finding smallest node\n");
        thread_mutex_unlock(&mm->mm_mutex);
        return err;
    }

    // Split capability until it has the right size, don't split below 4k.
    while (current->size >= 2*size && current->size > MIN_SPLIT_SIZE) {
        size_t offset = largest_contained_power_of_2(current->size);
        err = break_off_cap(mm, current, offset, retcap);
        if (err_is_fail(err)) {
            debug_printf("Failed at splitting node\n");
            thread_mutex_unlock(&mm->mm_mutex);
            return err;
        }

        if (current->size >= 2*size && current->size > MIN_SPLIT_SIZE) {
            struct mmnode *new_node = mm_create_node(mm, *retcap,
                                                     current->base - offset,
                                                     offset);
            if (new_node == NULL) {
                debug_printf("Failed to create new mmnode\n");
                thread_mutex_unlock(&mm->mm_mutex);
                return MM_ERR_NEW_NODE;
            }
            mm_insert_node(mm, new_node, current);
            current = new_node;
        } else {
            thread_mutex_unlock(&mm->mm_mutex);
            return SYS_ERR_OK;
        }

        bool refilled = false;
        err = refill_slots_if_needed(mm, &refilled);
        if (err_is_fail(err)) {
            debug_printf("Refilling slots failed\n");
            thread_mutex_unlock(&mm->mm_mutex);
            return err;
        }
        if (refilled) {
            current = mm->head;
            err = mm_find_smallest_node(mm, size, alignment, &current);
            if (err_is_fail(err)) {
                debug_printf("Error finding node after slot refill\n");
                thread_mutex_unlock(&mm->mm_mutex);
                return err;
            }
        }
    }

    if (current->type == NodeType_Keep) {
        err = break_off_cap(mm, current, current->size, retcap);
        if (err_is_fail(err)) {
            debug_printf("Failed to break off cap\n");
            return err;
        }
    } else {
        assert(aligned(current->base, alignment));
        assert(current->size >= size);
        *retcap = current->cap;
        mm_delete_node(mm, current, false);
    }

    thread_mutex_unlock(&mm->mm_mutex);
    return SYS_ERR_OK;
}

/**
 * \brief Make sure we never drop below a certain amount of slots
 */
errval_t refill_slots_if_needed(struct mm *mm, bool *refilled)
{
    if (!mm->allocating_ram &&
        !slot_alloc_enough_slots(mm->slot_alloc_inst, MIN_FREE_SLOTS)) {

        mm->allocating_ram = true;
        errval_t err = slot_prealloc_refill(mm->slot_alloc_inst);
        mm->allocating_ram = false;
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "slot refill failed");
            return err;
        }
        if (refilled != NULL) {
            *refilled = true;
        }
    }
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
errval_t mm_free(struct mm *mm, struct capref cap, genpaddr_t base,
                 gensize_t size)
{
    thread_mutex_lock_nested(&mm->mm_mutex);
    struct frame_identity ident;
    frame_identify(cap, &ident);
    assert(ident.base == base && ident.bytes == size);
    // TODO: more sanity checks

    errval_t err;
    if (mm->head == NULL) {
        printf("No capability slots to free");
        thread_mutex_unlock(&mm->mm_mutex);
        return MM_ERR_MISSING_CAPS;
    }

    struct mmnode *buddy = mm_find_node_by_base(mm, base + size, NodeType_Keep);

    if (buddy == NULL) {
        struct mmnode *newly_free = mm_create_node(mm, cap, base, size);
        mm_insert_node(mm, newly_free, mm->head->prev);

        err = refill_slots_if_needed(mm, NULL);
        if (err_is_fail(err)) {
            debug_printf("Refilling slots failed in mm_free\n");
            thread_mutex_unlock(&mm->mm_mutex);
            return err;
        }
    } else {
        gensize_t curr_size = size;
        cap_destroy(cap);
        struct mmnode *current;

        buddy->size += curr_size;
        buddy->base -= curr_size;

        while (true) {
            current = buddy;
            curr_size = current->size;

            struct frame_identity fi;
            frame_identify(current->cap, &fi);
            if (current->base != fi.base) {
                buddy = mm_find_node_by_base(mm, current->base - current->size, NodeType_Handout);

                if (buddy == NULL) {
                    mm_extract_node(mm, current);
                    mm_insert_node(mm, current, mm->head->prev);
                    break;
                }
                mm_delete_node(mm, current, true);

                buddy->size += curr_size;
            } else {
                current->type = NodeType_Handout;
                buddy = mm_find_node_by_base(mm, current->base + current->size, NodeType_Keep);

                if (buddy == NULL) {
                    mm_extract_node(mm, current);
                    mm_insert_node(mm, current, mm->head->prev);
                    break;
                }
                mm_delete_node(mm, current, true);

                buddy->size += curr_size;
                buddy->base -= curr_size;
            }
        }
    }

    thread_mutex_unlock(&mm->mm_mutex);
    return SYS_ERR_OK;
}

/**
 * Find the largest power of two which is contained in size
 *
 * \return The largest power of two which fits into size with some extra space
 */
size_t largest_contained_power_of_2(size_t size)
{
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
errval_t mm_find_smallest_node(struct mm *mm, size_t size, size_t alignment,
                               struct mmnode **current)
{
    assert(current != NULL && *current != NULL);

    while (!((*current)->size >= size &&
             aligned((*current)->base, alignment))) {
        *current = (*current)->next;
        assert(*current != NULL);
        if (*current == mm->head) {
            printf("No suitable capabilities left to hand out\n");
            return MM_ERR_NEW_NODE;
        }
    }

    return SYS_ERR_OK;
}

/**
 * \brief Finds a node that starts at the given base
 *
 * Only returns keep nodes
 *
 * \return the node with the given base or NULL if not found
 */
struct mmnode *mm_find_node_by_base(struct mm *mm, size_t base, enum nodetype type)
{
    assert(mm->head != NULL);

    struct mmnode *current = mm->head;

    while (current->base != base || current->type != type) {
        current = current->next;
        assert(current != NULL);
        if (current == mm->head) {
            return NULL;
        }
    }
    return current;
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
struct mmnode *mm_create_node(struct mm *mm, struct capref cap,
                              genpaddr_t base, size_t size)
{
    struct mmnode *ret = (struct mmnode *) slab_alloc(&(mm->slabs));
    if (!ret) {
        debug_printf("error in mm_create_node: failed to allocate new slab\n");
        return NULL;
    }
    ret->type = NodeType_Handout;
    ret->cap = cap;
    ret->base = base;
    ret->size = size;
    return ret;
}

/**
 * Insert the given node into the node list
 *
 * The node will be inserted in sorted order provided the start node is larger
 * than the given node or start points to the head->prev of the node list.
 *
 * \param       mm        The memory manager
 * \param       new_node  The node to insert
 * \param       start     The starting point for finding the insertion
 */
void mm_insert_node(struct mm *mm, struct mmnode *new_node,
                    struct mmnode *start)
{
    if (mm->head == NULL) {
        mm->head = new_node;
        new_node->next = new_node;
        new_node->prev = new_node;
        return;
    }
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
 * \brief Remove the given node from the node list and reclaim its slab space
 *
 * \param       mm              The memory manager
 * \param       node            The node to delete
 * \param       destroy_cap     Whether to destroy the underlying cap
 */
errval_t mm_delete_node(struct mm *mm, struct mmnode *node, bool destroy_cap)
{
    mm_extract_node(mm, node);

    slab_free(&(mm->slabs), node);

    if (destroy_cap) {
        return cap_destroy(node->cap);
    } else {
        return SYS_ERR_OK;
    }
}

void mm_extract_node(struct mm *mm, struct mmnode *node)
{
    if (node == mm->head && node->prev == mm->head) {
        mm->head = NULL;
        return;
    }

    node->next->prev = node->prev;
    node->prev->next = node->next;
    if (mm->head == node) {
        mm->head = node->next;
    }
}

/**
 * Splits the given node onto two non-overlapping mmnodes and throws away the
 * original cap. These two caps can't be merged anymore!
 *
 * Should only be called once per mm.
 */
errval_t split_mmnode_final(struct mm *mm, struct mmnode *current,
                            size_t right_offset)
{
    errval_t err;
    struct capref left_cap, right_cap;
    split_cap(mm, current, right_offset, &left_cap, &right_cap);

    struct mmnode *left_node = mm_create_node(mm, left_cap, current->base,
                                              right_offset);
    if (!left_node) {
        return MM_ERR_NEW_NODE;
    }
    mm_insert_node(mm, left_node, current);

    struct mmnode *right_node = mm_create_node(mm, right_cap,
                                               current->base + right_offset,
                                               current->size - right_offset);
    if (!right_node) {
        return MM_ERR_NEW_NODE;
    }
    mm_insert_node(mm, right_node, current);

    err = mm_delete_node(mm, current, true);
    if (err_is_fail(err)) {
        debug_printf("Error destroying the original capability\n");
        return err;
    }
    return SYS_ERR_OK;
}

/**
 * Get two nonoverlapping child capabilities from the given source cap at
 * the given offset.
 *
 * \param       mm              The memory manager
 * \param       source          The parent capability
 * \param       offset          The offset of the right cap in the parent
 * \param[out]  left_cap        The left child capability
 * \param[out]  rigth_cap       The right child capability
 */
errval_t split_cap(struct mm *mm, struct mmnode *source, size_t offset,
                   struct capref *left_cap, struct capref *right_cap)
{
    // Init left part
    errval_t err = slot_alloc_prealloc(mm->slot_alloc_inst, 1, left_cap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "failed allocating left slot");
        return err;
    }
    err = cap_retype(*left_cap, source->cap, 0, mm->objtype, offset, 1);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "left cap retype error: %s\n", err_getstring(err));
        return err;
    }
    // Init right part
    err = slot_alloc_prealloc(mm->slot_alloc_inst, 1, right_cap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "failed allocating left slot");
        return err;
    }
    size_t r_size = source->size - offset;
    err = cap_retype(*right_cap, source->cap, offset, mm->objtype, r_size, 1);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "right cap retype error: %s\n", err_getstring(err));
        return err;
    }
    return SYS_ERR_OK;
}

/**
 * \brief Break off a capability of size from the beginning of the node.
 *
 * \param       mm              The memory manager
 * \param       current         The parent capability
 * \param       left_size       The size of the capability to break off
 * \param[out]  left_cap        The capability that is broken off
 */
errval_t break_off_cap(struct mm *mm, struct mmnode *current,
                       size_t left_size, struct capref *left_cap)
{
    errval_t err;

    err = slot_alloc_prealloc(mm->slot_alloc_inst, 1, left_cap);
    if (err_is_fail(err)) {
        debug_printf("Failed to allocate slot for break off\n");
        return err;
    }

    struct frame_identity parent_ident;
    err = frame_identify(current->cap, &parent_ident);
    size_t parent_offset = current->base - parent_ident.base;
    if (err_is_fail(err)) {
        debug_printf("Failed to identify the case\n");
        parent_offset = 0;
    }

    err = cap_retype(*left_cap, current->cap, parent_offset, mm->objtype, left_size, 1);
    if (err_is_fail(err)) {
        debug_printf("Failed to retype the cap for break off\n");
        debug_printf("%s\n", err_getstring(err));
        return err;
    }

    current->type = NodeType_Keep;
    current->base += left_size;
    current->size -= left_size;

    mm_extract_node(mm, current);
    mm_insert_node(mm, current, current->prev);

    return SYS_ERR_OK;
}
