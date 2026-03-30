/*-
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2023, rtegrity ltd. All rights reserved.
 */

#ifndef _CHERITREE_H_
#define _CHERITREE_H_

#include <cheri/cheric.h>


extern int cheritree_print_mappings();
extern void cheritree_print_capabilities();


static void cheritree_init() {
    extern void _cheritree_init(void *function, void *stack);
    _cheritree_init(cheri_getpcc(), cheri_getstack());
}

#endif /* _CHERITREE_H_ */
