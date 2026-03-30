/*-
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2023, rtegrity ltd. All rights reserved.
 *  Copyright (c) 2026, Dapeng Gao.
 */

#ifndef _CHERITREE_UTIL_H_
#define _CHERITREE_UTIL_H_

#include <cinttypes>

// Integer for any valid address.
#ifdef __PTRADDR_TYPE__
typedef ptraddr_t addr_t;
#if __PTRADDR_WIDTH__ == 64
#define PRIxADDR    PRIx64
#define PRIuADDR    PRIu64
#else
#define PRIxADDR    PRIx32
#define PRIuADDR    PRIu32
#endif
#else
typedef __uint64_t addr_t;
#define PRIxADDR    PRIx64
#define PRIuADDR    PRIu64
#endif

#endif // _CHERITREE_UTIL_H_
