/*-
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2023, rtegrity ltd. All rights reserved.
 *  Copyright (c) 2026, Dapeng Gao.
 */

#include <fcntl.h>
#include <gelf.h>
#include <unistd.h>

#include <algorithm>
#include <unordered_map>

#include "symbol.h"

static std::unordered_map<std::string, std::shared_ptr<image_t>> images;

static bool keep_symbol(const GElf_Sym &sym)
{
    unsigned char bind = GELF_ST_BIND(sym.st_info);

    if (bind != STB_LOCAL && bind != STB_GLOBAL && bind != STB_WEAK)
        return false;
    if (sym.st_shndx == SHN_UNDEF)
        return false;
    if (sym.st_value == 0 && GELF_ST_TYPE(sym.st_info) == STT_FILE)
        return false;
    if (sym.st_name == 0)
        return false;

    return true;
}

static std::vector<symbol_t> load_symbols(Elf *elf, unsigned int wanted_type)
{
    std::vector<symbol_t> syms;
    Elf_Scn *scn = NULL;

    while ((scn = elf_nextscn(elf, scn)) != NULL) {

        GElf_Shdr shdr;
        if (gelf_getshdr(scn, &shdr) == NULL || shdr.sh_type != wanted_type)
            continue;

        Elf_Data *data = NULL;
        while ((data = elf_getdata(scn, data)) != NULL) {
            size_t i = 1;

            while (true) {

                GElf_Sym sym;
                if (gelf_getsym(data, i++, &sym) == NULL)
                    break;
                if (!keep_symbol(sym))
                    continue;

                const char *name = elf_strptr(elf, shdr.sh_link, sym.st_name);
                if (name == NULL)
                    name = "(null)";

                syms.emplace_back(symbol_t{
                    name,
                    static_cast<addr_t>(sym.st_value)
                });
            }
        }
    }

    std::stable_sort(syms.begin(), syms.end(),
        [](const symbol_t &a, const symbol_t &b) {
            if (a.value == b.value)
                return a.name < b.name;
            return a.value < b.value;
        });

    return syms;
}

std::shared_ptr<image_t> load_image(const std::string &path)
{
    if (path.empty() || elf_version(EV_CURRENT) == EV_NONE)
        return {};

    auto [it, inserted] = images.try_emplace(path, nullptr);
    if (!inserted)
        return it->second;

    std::vector<symbol_t> symbols;

    int fd = open(path.c_str(), O_RDONLY);
    if (fd >= 0) {
        Elf *elf = elf_begin(fd, ELF_C_READ, NULL);
        if (elf != NULL && elf_kind(elf) == ELF_K_ELF) {
            GElf_Ehdr ehdr;
            if (gelf_getehdr(elf, &ehdr) != NULL &&
                (ehdr.e_type == ET_EXEC || ehdr.e_type == ET_DYN)) {
                symbols = load_symbols(elf, SHT_SYMTAB);
                if (symbols.empty())
                    symbols = load_symbols(elf, SHT_DYNSYM);
            }
            elf_end(elf);
        }
        close(fd);
    }

    auto image = std::make_shared<image_t>(image_t{
        path,
        std::move(symbols)
    });
    it->second = image;

    return image;
}

const symbol_t *image_t::find_symbol(addr_t base, addr_t addr) const
{
    if (addr < base)
        return NULL;

    auto it = std::upper_bound(symbols.begin(), symbols.end(), addr - base,
        [](addr_t value, const symbol_t &sym) {
            return value < sym.value;
        });

    if (it != symbols.begin())
        return &*std::prev(it);

    return NULL;
}

bool image_t::has_symbol(addr_t base, addr_t start, addr_t end) const
{
    if (start < base)
        return false;

    auto it = std::lower_bound(symbols.begin(), symbols.end(), start - base,
        [](const symbol_t &sym, addr_t value) {
            return sym.value < value;
        });

    return it != symbols.end() && base + it->value < end;
}
