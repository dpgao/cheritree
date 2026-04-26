/*-
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2023, rtegrity ltd. All rights reserved.
 *  Copyright (c) 2026, Dapeng Gao.
 */

#ifndef _CHERITREE_MAPPING_H_
#define _CHERITREE_MAPPING_H_

#include <memory>
#include <string>

#include "util.h"

struct image_t;

struct mapping_t {
    std::string name;
    mapping_t *base;
    std::shared_ptr<image_t> image;
    addr_t start;
    addr_t end;
    unsigned int prot_read:1;
    unsigned int prot_write:1;
    unsigned int prot_exec:1;
    unsigned int prot_read_cap:1;
    unsigned int prot_write_cap:1;

    mapping_t(addr_t start, addr_t end, int prot);

    int prot() const;
};

mapping_t &cheritree_resolve_mapping(addr_t addr);
void cheritree_set_mapping_name(mapping_t &mapping,
    const std::string &owner, const std::string &name);
void cheritree_print_mappings();

int cheritree_dereference_address(void ***pptr, void **paddr);

#endif // _CHERITREE_MAPPING_H_
