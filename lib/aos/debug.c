/**
 * \file
 * \brief Debugging functions
 */

/*
 * Copyright (c) 2008-2011, ETH Zurich.
 * Copyright (c) 2015, 2016 Hewlett Packard Enterprise Development LP.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <stdio.h>
#include <aos/aos.h>
#include <aos/caddr.h>
#include <aos/debug.h>
#include <aos/sys_debug.h>
#include <aos/dispatch.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <barrelfish_kpi/dispatcher_shared.h>

#define DISP_MEMORY_SIZE            1024 // size of memory dump in bytes

/**
 * \brief Print a message and abort.
 *
 * Something irrecoverably bad happened. Print a panic message, then abort.
 */
void user_panic_fn(const char *file, const char *func, int line,
                   const char *msg, ...)
{
    va_list ap;
    char msg_str[128];
    //int msg_str_cc;
    va_start(ap, msg);
    //msg_str_cc =
        vsnprintf(msg_str, sizeof(msg_str), msg, ap);
    va_end(ap);

    char str[256];
    //int strcc =
        snprintf(str, sizeof(str), "%.*s.%u in %s() %s:%d\n%s\n",
                     DISP_NAME_LEN, disp_name(), disp_get_core_id(),
                     func, file, line, msg_str);
    sys_print(str, sizeof(str));

    abort();
}

/*
 * Have this invocation here to make debug_cap_identify work for domains that
 * have no monitor connection but hold Kernel cap (e.g. init)
 */
static inline errval_t
invoke_kernel_identify_cap(capaddr_t cap, int level, struct capability *out)
{
    return cap_invoke4(cap_kernel, KernelCmd_Identify_cap, cap, level,
                       (uintptr_t)out).error;
}

errval_t debug_cap_identify(struct capref cap, struct capability *ret)
{
    errval_t err;

    if (get_cap_addr(cap) == 0) {
        return SYS_ERR_CAP_NOT_FOUND;
    }

    uint8_t level = get_cap_level(cap);
    capaddr_t caddr = get_cap_addr(cap);
    err = invoke_kernel_identify_cap(caddr, level, ret);
    if (err_is_ok(err)) {
        // we have kernel cap, return result;
        return SYS_ERR_OK;
    }

    return LIB_ERR_NOT_IMPLEMENTED;
}

/**
 * \brief Enable fine-grained tracing of cap operations on address range
 * [start_addr, start_addr+size)
 * \arg types enable tracing for given set of ORed ObjType_s
 * \arg start_addr start of region to trace for
 * \arg size size of region to trace for
 */
errval_t debug_cap_trace_ctrl(uintptr_t types, genpaddr_t start_addr, gensize_t size)
{
    if (types) {
        printf("enabling pmem tracing: 0x%"PRIxGENPADDR"--0x%"PRIxGENPADDR
               " for types 0x%"PRIxPTR"\n",
               start_addr, start_addr+size, types);
    }
    return sys_debug_cap_trace_ctrl(types, start_addr, size);
}

/**
 * \brief Dump own hw page tables
 */
errval_t debug_dump_hw_ptables(void)
{
    return invoke_dispatcher_dump_ptables(cap_dispatcher);
}

void debug_printf(const char *fmt, ...)
{
    struct thread *me = thread_self();
    va_list argptr;
    char id[32] = "-";
    char str[256];
    size_t len;

    if (me)
        snprintf(id, sizeof(id), "%"PRIuPTR, thread_get_id(me));
    len = snprintf(str, sizeof(str), "\033[34m%.*s.\033[31m%u.%s\033[0m: ",
                   DISP_NAME_LEN, disp_name(), disp_get_core_id(), id);
    if (len < sizeof(str)) {
        va_start(argptr, fmt);
        vsnprintf(str + len, sizeof(str) - len, fmt, argptr);
        va_end(argptr);
    }
    sys_print(str, sizeof(str));
}

/**
 * \brief Function to do the actual printing based on the type of capability
 */
STATIC_ASSERT(46 == ObjType_Num, "Knowledge of all cap types");
int debug_print_cap(char *buf, size_t len, struct capability *cap)
{
    switch (cap->type) {
    case ObjType_PhysAddr:
        return snprintf(buf, len,
                        "physical address range cap (0x%" PRIxGENPADDR ":0x%" PRIxGENSIZE ")",
                        cap->u.physaddr.base, cap->u.physaddr.bytes);

    case ObjType_RAM:
        return snprintf(buf, len, "RAM cap (0x%" PRIxGENPADDR ":0x%" PRIxGENSIZE ")",
                        cap->u.ram.base, cap->u.ram.bytes);

    case ObjType_L1CNode: {
        int ret = snprintf(buf, len, "L1 CNode cap "
                           "(allocated bytes %#"PRIxGENSIZE
                           ", rights mask %#"PRIxCAPRIGHTS")",
                           cap->u.l1cnode.allocated_bytes, cap->u.l1cnode.rightsmask);
        return ret;
    }

    case ObjType_L2CNode: {
        int ret = snprintf(buf, len, "L2 CNode cap "
                           "(rights mask %#"PRIxCAPRIGHTS")",
                           cap->u.l1cnode.rightsmask);
        return ret;
    }


    case ObjType_Dispatcher:
        return snprintf(buf, len, "Dispatcher cap %p", cap->u.dispatcher.dcb);

    case ObjType_Frame:
        return snprintf(buf, len, "Frame cap (0x%" PRIxGENPADDR ":0x%" PRIxGENSIZE ")",
                        cap->u.frame.base, cap->u.frame.bytes);

    case ObjType_DevFrame:
        return snprintf(buf, len, "Device Frame cap (0x%" PRIxGENPADDR ":0x%" PRIxGENSIZE ")",
                        cap->u.frame.base, cap->u.devframe.bytes);

    case ObjType_VNode_ARM_l1:
        return snprintf(buf, len, "ARM L1 table at 0x%" PRIxGENPADDR,
                        cap->u.vnode_arm_l1.base);

    case ObjType_VNode_ARM_l2:
        return snprintf(buf, len, "ARM L2 table at 0x%" PRIxGENPADDR,
                        cap->u.vnode_arm_l2.base);

    case ObjType_VNode_AARCH64_l0:
        return snprintf(buf, len, "AARCH64 L0 table at 0x%" PRIxGENPADDR,
                        cap->u.vnode_aarch64_l0.base);

    case ObjType_VNode_AARCH64_l1:
        return snprintf(buf, len, "AARCH64 L1 table at 0x%" PRIxGENPADDR,
                        cap->u.vnode_aarch64_l1.base);

    case ObjType_VNode_AARCH64_l2:
        return snprintf(buf, len, "AARCH64 L2 table at 0x%" PRIxGENPADDR,
                        cap->u.vnode_aarch64_l2.base);

    case ObjType_VNode_AARCH64_l3:
        return snprintf(buf, len, "AARCH64 L3 table at 0x%" PRIxGENPADDR,
                        cap->u.vnode_aarch64_l3.base);

    case ObjType_VNode_x86_32_ptable:
        return snprintf(buf, len, "x86_32 Page table at 0x%" PRIxGENPADDR,
                        cap->u.vnode_x86_32_ptable.base);

    case ObjType_VNode_x86_32_pdir:
        return snprintf(buf, len, "x86_32 Page directory at 0x%" PRIxGENPADDR,
                        cap->u.vnode_x86_32_pdir.base);

    case ObjType_VNode_x86_32_pdpt:
        return snprintf(buf, len, "x86_32 PDPT at 0x%" PRIxGENPADDR,
                        cap->u.vnode_x86_32_pdpt.base);

    case ObjType_VNode_x86_64_ptable:
        return snprintf(buf, len, "x86_64 Page table at 0x%" PRIxGENPADDR,
                        cap->u.vnode_x86_64_ptable.base);

    case ObjType_VNode_x86_64_pdir:
        return snprintf(buf, len, "x86_64 Page directory at 0x%" PRIxGENPADDR,
                        cap->u.vnode_x86_64_pdir.base);

    case ObjType_VNode_x86_64_pdpt:
        return snprintf(buf, len, "x86_64 PDPT at 0x%" PRIxGENPADDR,
                        cap->u.vnode_x86_64_pdpt.base);

    case ObjType_VNode_x86_64_pml4:
        return snprintf(buf, len, "x86_64 PML4 at 0x%" PRIxGENPADDR,
                        cap->u.vnode_x86_64_pml4.base);

    case ObjType_Frame_Mapping:
        return snprintf(buf, len, "Frame Mapping (Frame cap @%p, "
                                  "pte @0x%"PRIxLVADDR", pte_count=%hu)",
                                  cap->u.frame_mapping.cap,
                                  cap->u.frame_mapping.pte,
                                  cap->u.frame_mapping.pte_count);

    case ObjType_DevFrame_Mapping:
        return snprintf(buf, len, "DevFrame Mapping (DevFrame cap @%p, "
                                  "pte @0x%"PRIxLVADDR", pte_count=%hu)",
                                  cap->u.devframe_mapping.cap,
                                  cap->u.devframe_mapping.pte,
                                  cap->u.devframe_mapping.pte_count);

    case ObjType_VNode_x86_64_pml4_Mapping:
        return snprintf(buf, len, "x86_64 PML4 Mapping (x86_64 PML4 cap @%p, "
                                  "pte @0x%"PRIxLVADDR", pte_count=%hu)",
                                  cap->u.vnode_x86_64_pml4_mapping.cap,
                                  cap->u.vnode_x86_64_pml4_mapping.pte,
                                  cap->u.vnode_x86_64_pml4_mapping.pte_count);

    case ObjType_VNode_x86_64_pdpt_Mapping:
        return snprintf(buf, len, "x86_64 PDPT Mapping (x86_64 PDPT cap @%p, "
                                  "pte @0x%"PRIxLVADDR", pte_count=%hu)",
                                  cap->u.vnode_x86_64_pdpt_mapping.cap,
                                  cap->u.vnode_x86_64_pdpt_mapping.pte,
                                  cap->u.vnode_x86_64_pdpt_mapping.pte_count);

    case ObjType_VNode_x86_64_pdir_Mapping:
        return snprintf(buf, len, "x86_64 PDIR Mapping (x86_64 PDIR cap @%p, "
                                  "pte @0x%"PRIxLVADDR", pte_count=%hu)",
                                  cap->u.vnode_x86_64_pdir_mapping.cap,
                                  cap->u.vnode_x86_64_pdir_mapping.pte,
                                  cap->u.vnode_x86_64_pdir_mapping.pte_count);

    case ObjType_VNode_x86_64_ptable_Mapping:
        return snprintf(buf, len, "x86_64 PTABLE Mapping (x86_64 PTABLE cap @%p, "
                                  "pte @0x%"PRIxLVADDR", pte_count=%hu)",
                                  cap->u.vnode_x86_64_ptable_mapping.cap,
                                  cap->u.vnode_x86_64_ptable_mapping.pte,
                                  cap->u.vnode_x86_64_ptable_mapping.pte_count);

    case ObjType_VNode_x86_32_pdpt_Mapping:
        return snprintf(buf, len, "x86_32 PDPT Mapping (x86_32 PDPT cap @%p, "
                                  "pte @0x%"PRIxLVADDR", pte_count=%hu)",
                                  cap->u.vnode_x86_32_pdpt_mapping.cap,
                                  cap->u.vnode_x86_32_pdpt_mapping.pte,
                                  cap->u.vnode_x86_32_pdpt_mapping.pte_count);

    case ObjType_VNode_x86_32_pdir_Mapping:
        return snprintf(buf, len, "x86_32 PDIR Mapping (x86_32 PDIR cap @%p, "
                                  "pte @0x%"PRIxLVADDR", pte_count=%hu)",
                                  cap->u.vnode_x86_32_pdir_mapping.cap,
                                  cap->u.vnode_x86_32_pdir_mapping.pte,
                                  cap->u.vnode_x86_32_pdir_mapping.pte_count);

    case ObjType_VNode_x86_32_ptable_Mapping:
        return snprintf(buf, len, "x86_32 PTABLE Mapping (x86_32 PTABLE cap @%p, "
                                  "pte @0x%"PRIxLVADDR", pte_count=%hu)",
                                  cap->u.vnode_x86_32_ptable_mapping.cap,
                                  cap->u.vnode_x86_32_ptable_mapping.pte,
                                  cap->u.vnode_x86_32_ptable_mapping.pte_count);

    case ObjType_VNode_ARM_l1_Mapping:
        return snprintf(buf, len, "ARM l1 Mapping (ARM l1 cap @%p, "
                                  "pte @0x%"PRIxLVADDR", pte_count=%hu)",
                                  cap->u.vnode_arm_l1_mapping.cap,
                                  cap->u.vnode_arm_l1_mapping.pte,
                                  cap->u.vnode_arm_l1_mapping.pte_count);

    case ObjType_VNode_ARM_l2_Mapping:
        return snprintf(buf, len, "ARM l2 Mapping (ARM l2 cap @%p, "
                                  "pte @0x%"PRIxLVADDR", pte_count=%hu)",
                                  cap->u.vnode_arm_l2_mapping.cap,
                                  cap->u.vnode_arm_l2_mapping.pte,
                                  cap->u.vnode_arm_l2_mapping.pte_count);

    case ObjType_VNode_AARCH64_l0_Mapping:
        return snprintf(buf, len, "AARCH64 l0 Mapping (AARCH64 l0 cap @0x%p, "
                                  "pte @0x%"PRIxLVADDR", pte_count=%hu)",
                                  cap->u.vnode_aarch64_l0_mapping.cap,
                                  cap->u.vnode_aarch64_l0_mapping.pte,
                                  cap->u.vnode_aarch64_l0_mapping.pte_count);

    case ObjType_VNode_AARCH64_l1_Mapping:
        return snprintf(buf, len, "AARCH64 l1 Mapping (AARCH64 l1 cap @%p, "
                                  "pte @0x%"PRIxLVADDR", pte_count=%hu)",
                                  cap->u.vnode_aarch64_l1_mapping.cap,
                                  cap->u.vnode_aarch64_l1_mapping.pte,
                                  cap->u.vnode_aarch64_l1_mapping.pte_count);

    case ObjType_VNode_AARCH64_l2_Mapping:
        return snprintf(buf, len, "AARCH64 l2 Mapping (AARCH64 l2 cap @%p, "
                                  "pte @0x%"PRIxLVADDR", pte_count=%hu)",
                                  cap->u.vnode_aarch64_l2_mapping.cap,
                                  cap->u.vnode_aarch64_l2_mapping.pte,
                                  cap->u.vnode_aarch64_l2_mapping.pte_count);

    case ObjType_VNode_AARCH64_l3_Mapping:
        return snprintf(buf, len, "AARCH64 l3 Mapping (AARCH64 l3 cap @%p, "
                                  "pte @0x%"PRIxLVADDR", pte_count=%hu)",
                                  cap->u.vnode_aarch64_l3_mapping.cap,
                                  cap->u.vnode_aarch64_l3_mapping.pte,
                                  cap->u.vnode_aarch64_l3_mapping.pte_count);


    case ObjType_IRQTable:
        return snprintf(buf, len, "IRQTable cap");

    case ObjType_EndPoint:
        return snprintf(buf, len, "EndPoint cap (disp %p offset 0x%" PRIxLVADDR ")",
                        cap->u.endpoint.listener, cap->u.endpoint.epoffset);

    case ObjType_IO:
        return snprintf(buf, len, "IO cap (0x%hx-0x%hx)",
                        cap->u.io.start, cap->u.io.end);

    case ObjType_Kernel:
        return snprintf(buf, len, "Kernel cap");

    case ObjType_KernelControlBlock:
        return snprintf(buf, len, "Kernel control block cap");

    case ObjType_ID:
        return snprintf(buf, len, "ID capability (coreid 0x%" PRIxCOREID
                        " core_local_id 0x%" PRIx32 ")", cap->u.id.coreid,
                        cap->u.id.core_local_id);

    case ObjType_PerfMon:
        return snprintf(buf, len, "PerfMon cap");

    case ObjType_Null:
        return snprintf(buf, len, "Null cap (empty slot)");

    default:
        return snprintf(buf, len, "UNKNOWN TYPE! (%d)", cap->type);
    }
}

int debug_print_cap_at_capref(char *buf, size_t len, struct capref cap)
{
    struct capability capability;
    errval_t err;

    if (capref_is_null(cap)) {
        return snprintf(buf, len, "(null cap)");
    }

    err = debug_cap_identify(cap, &capability);
    if (err_is_fail(err)) {
        return snprintf(buf, len, "(ERROR identifying cap!)");
    } else {
        return debug_print_cap(buf, len, &capability);
    }
}

/**
 * \brief Walk the cspace printing all non-null capabilities
 *
 * \param cnode         cnode to walk
 * \param level         depth in the cspace
 *
 * \bug assumes guards are always zero
 */
static void walk_cspace(struct cnoderef cnode, uint8_t level)
{
    USER_PANIC("walk_cspace NYI for 2-level cspace layout\n");
#if 0
    struct capability cap;
    errval_t err;

    struct capref pos = {
        .cnode = cnode, .slot = 0
    };

    // If too many bits resolved, return
    if (pos.cnode.address_bits + pos.cnode.guard_size + pos.cnode.size_bits
        > CPTR_BITS) {
        return;
    }

    // Walk through all the slots in the CNode
    for (pos.slot = 0; pos.slot < (((capaddr_t)1) << cnode.size_bits); pos.slot++) {
        // Get cap data
        err = debug_cap_identify(pos, &cap);

        // If cap type was Null, kernel returns error
        if (err_no(err) == SYS_ERR_IDENTIFY_LOOKUP ||
            err_no(err) == SYS_ERR_CAP_NOT_FOUND ||
            err_no(err) == SYS_ERR_LMP_CAPTRANSFER_SRC_LOOKUP) {
            continue;
        } else if (err_is_fail(err)) {
            DEBUG_ERR(err, "debug_cap_identify failed");
            return;
        }

        char buf[256];
        size_t prpos = 0;

        // Print the stats for the child slot
        for(int i = 0; i < level; i++) {
            prpos += snprintf(&buf[prpos], sizeof(buf) - prpos, "  ");
            assert(prpos < sizeof(buf));
        }
        prpos += snprintf(&buf[prpos], sizeof(buf) - prpos,
                          "slot %" PRIuCADDR " caddr 0x%" PRIxCADDR " (%u bits) is a ",
                          pos.slot, get_cap_addr(pos), get_cap_valid_bits(pos));
        assert(prpos < sizeof(buf));
        prpos += debug_print_cap(&buf[prpos], sizeof(buf) - prpos, &cap);
        assert(prpos < sizeof(buf));
        debug_printf("%s\n", buf);

        // If CNode type, descend into it
        if (cap.type == ObjType_CNode) {
            struct cnoderef childcn = {
                .address = get_cap_addr(pos),
                .address_bits = get_cap_valid_bits(pos),
                .size_bits = cap.u.cnode.bits,
                .guard_size = cap.u.cnode.guard_size,
            };
            walk_cspace(childcn, level + 1);
        }
    }
#endif
}

/**
 * \brief Dump an arbitrary cspace, given the root
 */
void debug_cspace(struct capref root)
{
    struct capability cap;

    /* find out size of root cnode */
    errval_t err = debug_cap_identify(root, &cap);
    assert(err_is_ok(err));

    struct cnoderef cnode = build_cnoderef(root, 0);
    walk_cspace(cnode, 0);
}

void debug_my_cspace(void)
{
    walk_cspace(cnode_root, 0);
}

int debug_print_capref(char *buf, size_t len, struct capref cap)
{
    return snprintf(buf, len, "CSpace root addr 0x%08" PRIxCADDR", "
                              "CNode addr 0x%08" PRIxCADDR
                              ", level = %d, slot %" PRIuCADDR ", level = %d",
                    get_croot_addr(cap), get_cnode_addr(cap),
                    get_cnode_level(cap), cap.slot, get_cap_level(cap));
}

int debug_print_cnoderef(char *buf, size_t len, struct cnoderef cnode)
{
    return snprintf(buf, len, "CSpace root addr 0x%08"PRIxCADDR", "
                              "CNode addr 0x%08"PRIxCADDR", level = %d",
                              cnode.croot, cnode.cnode, cnode.level);
}

void debug_dump_mem(lvaddr_t start_addr, lvaddr_t end_addr, lvaddr_t point)
{
    debug_printf("Dumping memory in range 0x%" PRIxLVADDR
                 " to 0x%" PRIxLVADDR ":\n",
                 start_addr, end_addr);

    for (uintptr_t *p = (void *)start_addr; (uintptr_t)p < end_addr; p++) {
        uint8_t *bytes = (void *)p;
        char buf[32];
        size_t bufpos = 0;
        for (int i = 0; i < sizeof(uintptr_t); i++) {
            bufpos += snprintf(&buf[bufpos], sizeof(buf) - bufpos, "%02x ", bytes[i]);
            assert(bufpos < sizeof(buf));
        }
        debug_printf("%p: %.*s %*" PRIxPTR "%s\n", p, (int)sizeof(buf), buf,
                     (int)sizeof(uintptr_t) * 2, *p,
                     p == (uintptr_t *)point ? " <== We are here" : "");
    }
}

void debug_dump_mem_around_addr(lvaddr_t addr)
{
    /* lvaddr_t page_aligned_addr = ROUND_DOWN(addr, BASE_PAGE_SIZE); */
    lvaddr_t start_addr = ROUND_DOWN(addr - DISP_MEMORY_SIZE/2, sizeof(uintptr_t));
    lvaddr_t end_addr = ROUND_UP(addr + DISP_MEMORY_SIZE/2, sizeof(uintptr_t));

    /* if (start_addr < page_aligned_addr) { */
    /*     start_addr = page_aligned_addr; */
    /* } */
    /* if (end_addr > page_aligned_addr + BASE_PAGE_SIZE) { */
    /*     end_addr = page_aligned_addr + BASE_PAGE_SIZE; */
    /* } */

    debug_dump_mem(start_addr, end_addr, addr);
}

void debug_err(const char *file, const char *func, int line, errval_t err,
               const char *msg, ...)
{
    va_list ap;

    char str[256];
    char *leader = (err == 0) ? "SUCCESS" : "ERROR";
    //int strcc =
        snprintf(str, sizeof(str), "%s: %.*s.%u in %s() %s:%d\n%s: ",
                     leader, DISP_NAME_LEN, disp_name(), disp_get_core_id(),
                     func, file, line, leader);
    sys_print(str, sizeof(str));

    if (msg != NULL) {
        va_start(ap, msg);
        //int strcc2 =
            vsnprintf(str, sizeof(str), msg, ap);
        va_end(ap);
        sys_print(str, sizeof(str));
    }
    sys_print("\n", 1);

    if (err != 0) {
        err_print_calltrace(err);
    }
}

bool debug_notify_syscall = false;

void debug_control_plane_forbidden(void);
void debug_control_plane_forbidden(void)
{
    debug_notify_syscall = true;
}
