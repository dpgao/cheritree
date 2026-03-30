/*-
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2023, rtegrity ltd. All rights reserved.
 *  Copyright (c) 2026, Dapeng Gao.
 */

#ifndef _CHERITREE_SYMBOL_H_
#define _CHERITREE_SYMBOL_H_

#include <string>

#include "util.h"

struct symbol_t {
    std::string name;
    addr_t value;
    char type;
};

void cheritree_load_symbols(const std::string &path);
symbol_t *cheritree_find_symbol(const std::string &path, addr_t base, addr_t addr);
const char *cheritree_find_type(const std::string &path, addr_t base, addr_t start, addr_t end);

#endif // _CHERITREE_SYMBOL_H_
