/*-
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2023, rtegrity ltd. All rights reserved.
 */

#ifndef _CHERITREE_H_
#define _CHERITREE_H_

#include <cheriintrin.h>


extern int cheritree_print_mappings();
extern void cheritree_print_capabilities();


static void cheritree_init() {
    extern void _cheritree_init(void *function, void *stack);
    _cheritree_init(cheri_pcc_get(), __builtin_cheri_stack_get());
}

#endif /* _CHERITREE_H_ */
