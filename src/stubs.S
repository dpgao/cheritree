/*-
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2023, rtegrity ltd. All rights reserved.
 *  Copyright (c) 2026, Dapeng Gao.
 */

#include <machine/asm.h>

ENTRY(cheritree_print)
	/* Stash c0 in the unused cid_el0 register */
1:	msr	cid_el0, c0

	adrp	c0, :got:cheritree_regs
	ldr	c0, [c0, :got_lo12:cheritree_regs]

	/* Save c1 */
	str	c1, [c0, #(CAP_WIDTH * 1)]

	/* Save c0 */
	mrs	c1, cid_el0
	msr	cid_el0, czr
	str	c1, [c0, #(CAP_WIDTH * 0)]

	/* Save c2-c30 */
	stp	c2, c3, [c0, #(CAP_WIDTH * 2)]
	stp	c4, c5, [c0, #(CAP_WIDTH * 4)]
	stp	c6, c7, [c0, #(CAP_WIDTH * 6)]
	stp	c8, c9, [c0, #(CAP_WIDTH * 8)]
	stp	c10, c11, [c0, #(CAP_WIDTH * 10)]
	stp	c12, c13, [c0, #(CAP_WIDTH * 12)]
	stp	c14, c15, [c0, #(CAP_WIDTH * 14)]
	stp	c16, c17, [c0, #(CAP_WIDTH * 16)]
	stp	c18, c19, [c0, #(CAP_WIDTH * 18)]
	stp	c20, c21, [c0, #(CAP_WIDTH * 20)]
	stp	c22, c23, [c0, #(CAP_WIDTH * 22)]
	stp	c24, c25, [c0, #(CAP_WIDTH * 24)]
	stp	c26, c27, [c0, #(CAP_WIDTH * 26)]
	stp	c28, c29, [c0, #(CAP_WIDTH * 28)]
	str	c30, [c0, #(CAP_WIDTH * 30)]

	/* Save pcc */
	adr	c1, 1b + 1
	str	c1, [c0, #(CAP_WIDTH * 31)]

	/* Save csp */
	mov	c1, csp
	str	c1, [c0, #(CAP_WIDTH * 32)]

	/* Save ctpidr_el0 */
	mrs	c1, ctpidr_el0
	str	c1, [c0, #(CAP_WIDTH * 33)]

	/* Restore c0 and c1 */
	ldp	c0, c1, [c0, #(CAP_WIDTH * 0)]

	/* Tail-call */
	b	_cheritree_print
END(cheritree_print)
