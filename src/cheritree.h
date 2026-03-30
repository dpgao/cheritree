/*-
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2023, rtegrity ltd. All rights reserved.
 *  Copyright (c) 2026, Dapeng Gao.
 */

#ifndef _CHERITREE_H_
#define _CHERITREE_H_

#include <cheri/cheric.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int cheritree_json_output;

void cheritree_print(void);
static inline void cheritree_init(void) {
    extern void _cheritree_init(void *, void *);
    _cheritree_init(cheri_pcc_get(), cheri_stack_get());
}

#ifdef __cplusplus
}
#endif

#endif // _CHERITREE_H_
