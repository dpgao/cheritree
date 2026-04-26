/*-
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2023, rtegrity ltd. All rights reserved.
 *  Copyright (c) 2026, Dapeng Gao.
 */

#ifndef _CHERITREE_SYMBOL_H_
#define _CHERITREE_SYMBOL_H_

#include <memory>
#include <string>
#include <vector>

#include "util.h"

struct symbol_t {
    std::string name;
    addr_t value;
};

struct image_t {
    const std::string path;
    std::vector<symbol_t> symbols;

    const symbol_t *find_symbol(addr_t base, addr_t addr) const;
    bool has_symbol(addr_t base, addr_t start, addr_t end) const;
};

std::shared_ptr<image_t> load_image(const std::string &path);

#endif // _CHERITREE_SYMBOL_H_
