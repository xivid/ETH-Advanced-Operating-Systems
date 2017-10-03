/**
 * \file
 * \brief A library for managing physical memory (i.e., caps)
 */

#include <mm/mm.h>
#include <aos/debug.h>


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

    slab_init(&(mm->slabs), sizeof(struct mmnode), slab_refill_func);

    mm->slot_alloc = slot_alloc_func;
    mm->slot_refill = slot_refill_func;
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
    struct mmnode *new_memnode;

    new_memnode = (struct mmnode *) slab_alloc(&(mm->slabs));
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

    if (mm->head == NULL) {
        new_memnode->prev = new_memnode;
        new_memnode->next = new_memnode;
    } else {
        struct mmnode *current = mm->head;
        while (current->prev->size > new_memnode->size) {
            current = current -> prev;
            if (current == mm->head) {
                if (current->size > new_memnode->size) {
                    mm->head = new_memnode;
                }
                break;
            }
        }

        new_memnode->next = current->next;
        new_memnode->prev = current;
        current->next->prev = new_memnode;
        current->next = new_memnode;
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
    struct mmnode *current = mm->head;

    if (current == NULL) {
        printf("Cannot allocate memory without caps\n");
        return MM_ERR_MISSING_CAPS;
    }

    size_t alignment_offset = 0;
    while (1) {
        alignment_offset = (alignment - current->cap.base % alignment) % alignment;
        if (current->type == NodeType_Free && current->cap.size >= size + alignment_offset) {
            break;
        }

        current = current->next;
        if (current == mm->head) {
            printf("No more capabilities left to hand out that are large enough\n");
            return MM_ERR_NEW_NODE;
        }
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
    // TODO: Maybe this needs a separate implementation
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
