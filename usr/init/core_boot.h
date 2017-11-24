//
// Created by zhiyang on 11/22/17.
//

#ifndef _INIT_CORE_BOOT_H_
#define _INIT_CORE_BOOT_H_


/* multi-core declarations */

/**
 * \brief Boots the APP core whose id is core_id.
 */
errval_t boot_core(coreid_t core_id);

/**
 * \breif Called from the BSP core.
 * Allocates memory for the URPC Frame to be shared between cores,
 * maps it into the current dispatcher's space,
 * and writes the resources information into the first bytes of the shared urpc frame.
 * @return error value
 */
errval_t core_boot_dump_resources(void);

/**
 * \breif Called from the APP core.
 * Reads the resources information shared by BSP core from the first bytes of the shared urpc frame,
 * initializes ram allocator,
 * and forges the caps.
 * @return error value
 */
errval_t core_boot_load_resources(coreid_t core_id);


/* Multi-core implementations */

errval_t boot_core(coreid_t core_id) {

    /* allocate memory and obtain a new KCB */
    errval_t err;
    struct capref kcb_ram;
    err = ram_alloc(&kcb_ram, OBJSIZE_KCB);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not ram alloc kcb_ram");
        return err;
    }

    struct capref kcb;
    err = slot_alloc(&kcb);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not slot alloc kcb_ram");
        return err;
    }

    err = cap_retype(kcb, kcb_ram, 0, ObjType_KernelControlBlock, OBJSIZE_KCB, 1);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not cap retype kcb");
        return err;
    }

    /* initialize core_data by cloning the running kernel */
    struct frame_identity kcb_id;
    err = frame_identify(kcb, &kcb_id);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c boot cpu1: could not identify kcb frame");
        return err;
    }

    struct capref core_data_f;
    size_t ret;
    err = frame_alloc(&core_data_f, sizeof(struct arm_core_data), &ret);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not frame alloc core data frame");
        return err;
    }

    struct frame_identity core_data_id;
    err = frame_identify(core_data_f, &core_data_id);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c boot cpu1: could not frame identify core data");
        return err;
    }

    err = invoke_kcb_clone(kcb, core_data_f);

    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not invoke kcb clone");
        return err;
    }

    // map into our address space so that we can access it
    struct arm_core_data* core_data;
    err = paging_map_frame(get_current_paging_state(), (void**) &core_data, ret, core_data_f, NULL, NULL);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not paging map frame core data to core_data_f");
        return err;
    }

    struct capref init_space;
    err = frame_alloc(&init_space, ARM_CORE_DATA_PAGES*BASE_PAGE_SIZE, &ret);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c boot cpu1: could not frame alloc init_space");
        return err;
    }

    struct frame_identity init_frame_id;
    err = frame_identify(init_space, &init_frame_id);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/main.c boot cpu1: could not identify init frame");
        return err;
    }

    // allocate memory for the new core
    core_data->memory_bytes = ret;
    core_data->memory_base_start = init_frame_id.base;
    core_data->cmdline = core_data_id.base + (size_t)((lvaddr_t)&(core_data->cmdline_buf) - (lvaddr_t)core_data);
    core_data->kcb = kcb_id.base;

    // fill rest of core_data
    struct mem_region* module = multiboot_find_module(bi, "init");
    if (module == NULL) {
        debug_printf("usr/init/main.c boot_core: init module not found");
        return SPAWN_ERR_FIND_MODULE;
    }
    struct multiboot_modinfo monitor_modinfo = {
            .mod_start = module->mr_base,
            .mod_end = module->mrmod_size + module->mr_base,
            .string = module->mrmod_data,
            .reserved = 0
    };
    core_data->monitor_module = monitor_modinfo;

    /* the URPC frame is allocated in main() */
    struct frame_identity urpc_frame_id;
    err = frame_identify(cap_urpc, &urpc_frame_id);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not frame identify urpc_frame");
        return err;
    }
    core_data->urpc_frame_base = urpc_frame_id.base;
    core_data->urpc_frame_size = urpc_frame_id.bytes;

    /* load and relocate cpu driver */
    struct mem_region* cpu_module = multiboot_find_module(bi, "cpu_omap44xx");
    if (cpu_module == NULL) {
        debug_printf("usr/init/main.c boot_core: cpu driver module not found");
        return SPAWN_ERR_FIND_MODULE;
    }
    struct capref cpu_frame = {
            .cnode = cnode_module,
            .slot = cpu_module->mrmod_slot
    };

    struct frame_identity cpu_frame_id;
    err = frame_identify(cpu_frame, &cpu_frame_id);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not frame identify");
        return err;
    }

    // memory for relocatable segment
    struct capref relocatable_segment;
    err = frame_alloc(&relocatable_segment, cpu_frame_id.bytes, &ret);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not frame alloc relocatable_segment");
        return err;
    }

    void* segment_addr;
    err = paging_map_frame(get_current_paging_state(), &segment_addr, ret, relocatable_segment, NULL, NULL);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not paging map relocatable segment memory");
        return err;
    }

    struct frame_identity segment_id;
    err = frame_identify(relocatable_segment, &segment_id);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not frame identify the relocatable_segment");
        return err;
    }

    // elf text segment
    void* elfdata;
    err = paging_map_frame(get_current_paging_state(), &elfdata, cpu_frame_id.bytes, cpu_frame, NULL, NULL);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not paging map elf frame");
        return err;
    }

    err = load_cpu_relocatable_segment(elfdata, segment_addr, segment_id.base, core_data->kernel_load_base, &core_data->got_base);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not load cpu relocatable segment");
        return err;
    }

    sys_armv7_cache_clean_pou(core_data, core_data + sizeof(core_data));
    sys_armv7_cache_clean_poc(core_data, core_data + sizeof(core_data));
    sys_armv7_cache_invalidate(core_data, core_data + sizeof(core_data));

    sys_armv7_cache_clean_pou((void *)(uintptr_t)segment_id.base, (void *)(uintptr_t)segment_id.base + segment_id.bytes);
    sys_armv7_cache_clean_poc((void *)(uintptr_t)segment_id.base, (void *)(uintptr_t)segment_id.base + segment_id.bytes);
    sys_armv7_cache_invalidate((void *)(uintptr_t)segment_id.base, (void *)(uintptr_t)segment_id.base + segment_id.bytes);

    err = invoke_monitor_spawn_core(core_id, CPU_ARM7, core_data_id.base);

    if (err_is_fail(err)) {
        DEBUG_ERR(err, "usr/init/main.c boot_core: could not invoke cpu1");
        return err;
    }
    return SYS_ERR_OK;
}

errval_t core_boot_dump_resources(void) {
    errval_t err = SYS_ERR_OK;

    size_t ret;
    // alloc frame for shared memory between cores
    frame_alloc(&cap_urpc, MON_URPC_SIZE, &ret);
    if(err_is_fail(err)){
        DEBUG_ERR(err, "main.c: cap_urpc frame alloc");
        return EXIT_FAILURE;
    }
    // map the urpc frame for bsp core
    err = paging_map_frame(get_current_paging_state(), &urpc_shared_base, MON_URPC_SIZE, cap_urpc, NULL, NULL);
    if(err_is_fail(err)){
        DEBUG_ERR(err, "main.c: cap_urpc paging map frame");
        return EXIT_FAILURE;
    }
    void *shared_buf_urpc = urpc_shared_base;

    /* set the status
    *(uint32_t *) shared_buf_urpc = 0; // TODO: set the right word to 0
    shared_buf_urpc += sizeof(uint32_t); */

    // write info of cap_bootinfo to shared frame
    struct frame_identity id;
    frame_identify(cap_bootinfo, &id);
    *((genpaddr_t *) shared_buf_urpc) = id.base;
    shared_buf_urpc += sizeof(genpaddr_t);
    *((gensize_t*) shared_buf_urpc) = id.bytes;
    shared_buf_urpc += sizeof(gensize_t);

    // write info of mmstrings info to shared frame
    struct capref cap_mmstrings = {
            .cnode = cnode_module,
            .slot = 0
    };
    frame_identify(cap_mmstrings, &id);
    *((genpaddr_t *) shared_buf_urpc) = id.base;
    shared_buf_urpc += sizeof(genpaddr_t);
    *((gensize_t*) shared_buf_urpc) = id.bytes;
    shared_buf_urpc += sizeof(gensize_t);

    debug_printf("written shared information\n");
    return err;
}

errval_t core_boot_load_resources(coreid_t core_id) {
    errval_t err = SYS_ERR_OK;

    urpc_shared_base = (void *)MON_URPC_VBASE;

    void *shared_buf_urpc = urpc_shared_base;

    // read in app core
    genpaddr_t bootinfo_base = *(genpaddr_t *)shared_buf_urpc;
    shared_buf_urpc += sizeof(genpaddr_t);
    gensize_t bootinfo_bytes = *(gensize_t *)shared_buf_urpc;
    shared_buf_urpc += sizeof(gensize_t);
    genpaddr_t mmstrings_base = *(genpaddr_t *)shared_buf_urpc;
    shared_buf_urpc += sizeof(genpaddr_t);
    gensize_t mmstrings_bytes = *(gensize_t *)shared_buf_urpc;

    // forge the bootinfo cap
    err = devframe_forge(cap_bootinfo, bootinfo_base, bootinfo_bytes, my_core_id);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "main.c: devframe_forge bootinfo(0x%llx:0x%llx) failed", bootinfo_base, bootinfo_bytes);
        return EXIT_FAILURE;
    }
    err = paging_map_frame(get_current_paging_state(), (void **) &bi, bootinfo_bytes, cap_bootinfo, NULL, NULL);
    if(err_is_fail(err)){
        DEBUG_ERR(err, "main.c: mapping bootinfo failed");
        return EXIT_FAILURE;
    }

    // forge the cap for available regions
    struct capref mem_cap = {
            .cnode = cnode_super,
            .slot = 0,
    };
    for (int i = 0; i < bi->regions_length; i++) {
        if (bi->regions[i].mr_type == RegionType_Empty) {
            err = ram_forge(mem_cap, bi->regions[i].mr_base, bi->regions[i].mr_bytes, my_core_id);
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "main.c: ram_forge failed");
                return EXIT_FAILURE;
            }
            ++mem_cap.slot;
        }
    }

    // initialize ram for app core
    err = initialize_ram_alloc();
    if(err_is_fail(err)){
        DEBUG_ERR(err, "initialize_ram_alloc of core %d", (int) my_core_id);
        return EXIT_FAILURE;
    }

    // create l2 cnode: cnode_module
    struct capref cap_cnode_root = {
            .cnode = cnode_task,
            .slot = TASKCN_SLOT_ROOTCN
    };
    err = cnode_create_foreign_l2(cap_cnode_root, ROOTCN_SLOT_MODULECN, &cnode_module);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "main.c: create Module CNode failed");
        return EXIT_FAILURE;
    }

    // forge frame caps for mmstrings_cap
    struct capref module_cap = {
            .cnode = cnode_module,
            .slot = 0
    };
    err = frame_forge(module_cap, mmstrings_base, mmstrings_bytes, my_core_id);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "main.c: frame_forge mmstrings(0x%llx:0x%llx) failed", mmstrings_base, mmstrings_bytes);
        return EXIT_FAILURE;
    }

    // forge the caps for modules
    for (int i = 0; i < bi->regions_length; i++) {
        if (bi->regions[i].mr_type == RegionType_Module) {
            module_cap.slot = (cslot_t) bi->regions[i].mrmod_slot;
            err = devframe_forge(module_cap, bi->regions[i].mr_base, bi->regions[i].mrmod_size, my_core_id);
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "main.c: devframe_forge failed");
                return EXIT_FAILURE;
            }
        }
    }

    return err;
}


#endif //_INIT_CORE_BOOT_H_
