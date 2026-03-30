/*-
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2026, Dapeng Gao.
 */

#ifndef _CHERITREE_CAPABILITIES_H_
#define _CHERITREE_CAPABILITIES_H_

#define CHERITREE_ROOT_PCC 31
#define CHERITREE_ROOT_CSP 32
#define CHERITREE_ROOT_TPIDR 33
#define CHERITREE_NREGS 34

extern uint64_t excluded_roots;
extern void *cheritree_regs[CHERITREE_NREGS];

void cheritree_print_capabilities();

#endif // _CHERITREE_CAPABILITIES_H_
