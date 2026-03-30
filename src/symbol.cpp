/*-
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2023, rtegrity ltd. All rights reserved.
 *  Copyright (c) 2026, Dapeng Gao.
 */

#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <vector>

#include "symbol.h"

using image = std::vector<symbol_t>;

static std::unordered_map<std::string, image> images;

static image *find_image(const std::string &path)
{
    if (path.empty())
        return NULL;

    auto it = images.find(path);
    return it != images.end() ? &it->second : NULL;
}

static int load_symbol(char *buffer, std::vector<symbol_t> &syms)
{
    addr_t value;
    char type, name[1024];

    if (sscanf(buffer, "%" PRIxADDR " %c %1023s", &value, &type, name) != 3)
        return 0;

    if (name[0] == '$')
        return 1;

    syms.emplace_back(symbol_t{name, value, type});
    return 1;
}

void cheritree_load_symbols(const std::string &path)
{
    char cmd[2048], buffer[2048];

    if (find_image(path))
        return;

    auto &img = images[path];

    sprintf(cmd, "nm -ne --defined-only %s 2>/dev/null", path.c_str());
    FILE *fp = popen(cmd, "r");
    if (fp) {
        while (fgets(buffer, sizeof(buffer), fp) != NULL)
            if (!load_symbol(buffer, img))
                break;
        pclose(fp);
    }

    if (!img.empty()) return;

    // Retry with dynamic symbols
    sprintf(cmd, "nm -Dne --defined-only %s", path.c_str());
    fp = popen(cmd, "r");
    if (fp) {
        while (fgets(buffer, sizeof(buffer), fp) != NULL)
            if (!load_symbol(buffer, img))
                break;
        pclose(fp);
    }

    if (img.empty()) {
        fprintf(stderr, "Unable to load symbols");
        exit(1);
    }
}

symbol_t *cheritree_find_symbol(const std::string &path,
    addr_t base, addr_t addr)
{
    auto *img = find_image(path);

    if (!img) return NULL;

    int i = img->size();
    while (--i >= 0) {
        symbol_t &sym = (*img)[i];
        if (base + sym.value <= addr)
            return &sym;
    }

    return NULL;
}

const char *cheritree_find_type(const std::string &path,
    addr_t base, addr_t start, addr_t end)
{
    symbol_t *sym = cheritree_find_symbol(path, base, end);

    if (sym && start <= base + sym->value) {
        if (strchr("Tt", sym->type)) return "text";
        if (strchr("BCb", sym->type)) return "bss";
        if (strchr("DRVdr", sym->type)) return "data";
    }

    return NULL;
}
