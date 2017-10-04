/**
 * \file
 * \brief A library for managing physical memory (i.e., caps)
 */

#include <mm/mm.h>
#include <aos/debug.h>

errval_t mm_create_mmnode(struct mm *mm, struct capref cap, genpaddr_t base, size_t size, struct mmnode **ret);
void mm_insert_node(struct mm *mm, struct mmnode *new_node, struct mmnode *start);

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
    struct mmnode *new_memnode;

    errval_t err = mm_create_mmnode(mm, cap, base, size, &new_memnode);
    if (err_is_fail(err)) {
        return err;
    }

    if (mm->head == NULL) {
        new_memnode->prev = new_memnode;
        new_memnode->next = new_memnode;
        mm->head = new_memnode;
    } else {
        mm_insert_node(mm, new_memnode, mm->head);
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
    printf("Call alloc_aligned with %i and %i \n", size, alignment);
    struct mmnode *current = mm->head;

    if (current == NULL) {
        printf("Cannot allocate memory without caps\n");
        return MM_ERR_MISSING_CAPS;
    }

    size_t alignment_offset = 0;
    while (1) {
        alignment_offset = ROUND_UP(current->cap.base, alignment) - current->cap.base;
        if (current->type == NodeType_Free && current->cap.size >= size + alignment_offset) {
            break;
        }

        current = current->next;
        if (current == mm->head) {
            // TODO: Try to defragment capabilities and start over.
            printf("No more capabilities left to hand out that are large enough\n");
            return MM_ERR_NEW_NODE;
        }
    }

    // split capability until it has the right size
    while (current->cap.size > 2*(size + alignment_offset) &&
           current->cap.size >= 2* BASE_PAGE_SIZE) {

        // TODO: allocate new level 2 cnodes if needed

        struct capref new_cap_slot;
        slot_alloc_prealloc(mm->slot_alloc_inst, 2, &new_cap_slot);
        cap_retype(new_cap_slot, current->cap.cap, 0, mm->objtype, current->cap.size / 2, 2);

        struct mmnode *node_split1;
        errval_t err = mm_create_mmnode(mm, new_cap_slot, current->cap.base, current->cap.size / 2, &node_split1);
        if (err_is_fail(err)) {
            return err;
        }

        struct mmnode *node_split2;
        new_cap_slot.slot++;
        err = mm_create_mmnode(mm, new_cap_slot, current->cap.base + current->cap.size / 2, current->cap.size / 2, &node_split2);
        if (err_is_fail(err)) {
            return err;
        }

        // remove current node from node-list and free slab
        current->next->prev = current->prev;
        current->prev->next = current->next;
        struct mmnode *to_remove = current;
        if (current == mm->head) {
            mm->head = current->next;
            current = current->next;
        } else {
            current = current->prev;
        }
        slab_free(&(mm->slabs), to_remove);

        mm_insert_node(mm, node_split1, current);
        current = node_split1;
        mm_insert_node(mm, node_split2, current);
        current = node_split2;
    }

    current->type = NodeType_Allocated;
    current->cap.size -= alignment_offset;
    current->cap.base += alignment_offset;

    *retcap = current->cap.cap;
    return SYS_ERR_OK;
}

/**
 * Allocate physical memory.
 *
 * \param       mm        The memory manager.
 * \param       size      How much memory to allocate.
 * \param[out]  retcap    Capability for the allocated region.
 */
errval_t mm_alloc(struct mm *mm, size_t size, struct capref *retcap)
{
    return mm_alloc_aligned(mm, size, 1, retcap);
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
    current->cap.size = current->size;
    current->cap.base = current->base;

    return SYS_ERR_OK;
}

/**
 * Allocate a new memory node and fill it with the capability
 *
 * \param       mm        The memory manager
 * \param       cap       The capability to allocate the node for
 * \param       base      The physical base address of the region
 * \param       size      The size of the region
 * \param[out]  ret       The newly allocated and initialized memory node
 */
errval_t mm_create_mmnode(struct mm *mm, struct capref cap, genpaddr_t base, size_t size, struct mmnode **ret) {
    struct mmnode *new_memnode = (struct mmnode *) slab_alloc(&(mm->slabs));
    if (!new_memnode) {
        printf("slab refill not implemented yet\n");
        return LIB_ERR_NOT_IMPLEMENTED;
    }

    new_memnode->type = NodeType_Free;
    new_memnode->cap.cap = cap;
    new_memnode->cap.base = base;
    new_memnode->cap.size = size;
    new_memnode->base = base;
    new_memnode->size = size;

    *ret = new_memnode;
    return SYS_ERR_OK;
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
    while (current->prev->size > new_node->size) {
        current = current -> prev;
        if (current == mm->head) {
            if (current->size > new_node->size)
                mm->head = new_node;
            break;
        }
    }

    new_node->next = current->next;
    new_node->prev = current;
    current->next->prev = new_node;
    current->next = new_node;
}
