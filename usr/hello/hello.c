/**
 * \file
 * \brief Hello world application
 */

/*
 * Copyright (c) 2016 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, CAB F.78, Universitaetstr. 6, CH-8092 Zurich,
 * Attn: Systems Group.
 */


#include <stdio.h>
#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/waitset.h>
#include <aos/except.h>

// For now the exception stack is a statically allocated buffer
#define stack_size 8*BASE_PAGE_SIZE
static char exception_stack[stack_size];

void page_fault_handler(enum exception_type type, int subtype, void *addr, 
        arch_registers_state_t *regs, arch_registers_fpu_state_t *fpuregs);

__attribute__((noreturn))
void page_fault_handler(enum exception_type type, int subtype, void *addr, 
        arch_registers_state_t *regs, arch_registers_fpu_state_t *fpuregs) {
    debug_printf("a page fault occured\n");
    while (1) {};
}

int main(int argc, char *argv[])
{
    errval_t err = thread_set_exception_handler(page_fault_handler, NULL,
            exception_stack, &exception_stack[stack_size-1], NULL, NULL);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "failed setting a page fault handler in hello.c");
        return err;
    }
    debug_printf("handler set, accessing!\n");
    int *invalid_ptr = (int *) 0xffff0000;
    int a = *invalid_ptr;
    debug_printf("a is %d\n", a);
    return 0;
}
