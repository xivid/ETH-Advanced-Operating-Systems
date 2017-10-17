#include <aos/aos.h>
#include <spawn/spawn.h>

#include <elf/elf.h>
#include <aos/dispatcher_arch.h>
#include <barrelfish_kpi/paging_arm_v7.h>
#include <barrelfish_kpi/domain_params.h>
#include <spawn/multiboot.h>

extern struct bootinfo *bi;

errval_t init_child_cspace(struct spawninfo* si) {

    // create l1Cnode
    struct cnoderef l1_cnode;
    errval_t err = cnode_create_l1(&si->l1_cnode_cap, &l1_cnode);
    if (err_is_fail(err)) {
        debug_printf("Error during child cspace initialization, creating l1_cnode: %s", err_getstring(err));
        return err;
    }

    // create the L2Cnodes
    for (size_t i = 0; i < ROOTCN_SLOTS_USER; ++i) {
        err = cnode_create_foreign_l2(si->l1_cnode_cap, i, &si->l2_cnodes[i]);
        if (err_is_fail(err)) {
            debug_printf("Error during paging_map_frame: %s", err_getstring(err));
            return err;
        }
    }

    // setup taskn slot rootcn on l1Cnode
    struct capref taskn_slot_rootcn = {
        .cnode = si->l2_cnodes[ROOTCN_SLOT_TASKCN],
        .slot = TASKCN_SLOT_ROOTCN
    };
    err = cap_copy(taskn_slot_rootcn, si->l1_cnode_cap);
    if (err_is_fail(err)) {
        debug_printf("Error during copy of L1Cnode cap to taskcn: %s", err_getstring(err));
        return err;
    }

    // allocate BASE_PAGE_CN slots in ram
    struct capref cap = {
        .cnode = si->l2_cnodes[ROOTCN_SLOT_BASE_PAGE_CN]
    };
    for (cap.slot = 0; cap.slot < L2_CNODE_SLOTS; ++cap.slot) {
        struct capref ram_for_base_page;
        err = ram_alloc(&ram_for_base_page, BASE_PAGE_SIZE);
        if (err_is_fail(err)) {
            debug_printf("Error during BASE_PAGE_CN ram_alloc: %s", err_getstring(err));
            return err;
        }
        err = cap_copy(cap, ram_for_base_page);
        if (err_is_fail(err)) {
            debug_printf("Error during cap copy in BASE_PAGE_CN slot filling: %s", err_getstring(err));
            return err;
            cap_destroy(ram_for_base_page);
        }
    }
    return SYS_ERR_OK;
}

// TODO(M2): Implement this function such that it starts a new process
// TODO(M4): Build and pass a messaging channel to your child process
errval_t spawn_load_by_name(void * binary_name, struct spawninfo * si) {
    printf("spawn start_child: starting: %s\n", binary_name);
    errval_t err;
    // - Get the binary from multiboot image
    struct mem_region *module = multiboot_find_module(bi, binary_name);
    if (!module) {
        debug_printf("The module %s was not found", binary_name);
        return SPAWN_ERR_FIND_MODULE;
    }

    // child capref
    struct capref child_frame = {
        .cnode = cnode_module,
        .slot = module->mrmod_slot,
    };

    // Initialize spawninfo
    memset(si, 0, sizeof(*si));
    si->binary_name = binary_name;


    // - Map multiboot module in your address space
    struct frame_identity id_child_frame;
    frame_identify(child_frame, &id_child_frame);

    lvaddr_t map_elf;
    err = paging_map_frame(get_current_paging_state(), (void**)&map_elf, id_child_frame.bytes, child_frame, NULL, NULL);
    if (err_is_fail(err)) {
        debug_printf("Error during paging_map_frame: %s\n", err_getstring(err));
        return err;
    }

    printf("ELF header: %0x %c %c %c\n", ((char*)map_elf)[0], ((char*)map_elf)[1], ((char*)map_elf)[2], ((char*)map_elf)[3]);

    struct Elf32_Ehdr *elf_head = (void*)map_elf;
    if (!IS_ELF(*elf_head)) {
        debug_printf("Module is not a correct ELF binary\n");
        return ELF_ERR_HEADER;
    }

    err = init_child_cspace(si);
    if (err_is_fail(err)) {
        debug_printf("Error during init_child_cspace: %s \n", err_getstring(err));
        return err;
    }
    printf("reached end of stuff which is implemented so far\n");

    // TODO: create vspace

    /* err = elf_load(EM_ARM, , get_current_paging_state(), , , ); */

    // TODO: Implement me
    // - Setup childs cspace
    // - Setup childs vspace
    // - Load the ELF binary
    // - Setup dispatcher
    // - Setup environment
    // - Make dispatcher runnable

    return SYS_ERR_OK;
}
