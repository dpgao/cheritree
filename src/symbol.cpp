/*-
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2023, rtegrity ltd. All rights reserved.
 *  Copyright (c) 2026, Dapeng Gao.
 */

#include <sys/elf_common.h>
#include <sys/types.h>

#include <fcntl.h>
#include <gelf.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
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

            GElf_Sym sym;
            for (size_t i = 1; gelf_getsym(data, i, &sym) != NULL; ++i) {

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

static std::vector<compart_t> load_comparts(Elf *elf)
{
    std::vector<compart_t> comparts;
    size_t shstrndx;

    if (elf_getshdrstrndx(elf, &shstrndx) != 0)
        return comparts;

    const char *c18nstrtab = NULL;
    size_t c18nstrsize = 0;

    Elf_Scn *scn = NULL;
    while ((scn = elf_nextscn(elf, scn)) != NULL) {

        GElf_Shdr shdr;
        if (gelf_getshdr(scn, &shdr) == NULL || shdr.sh_type != SHT_STRTAB ||
            (shdr.sh_flags & SHF_ALLOC) == 0)
            continue;

        const char *name = elf_strptr(elf, shstrndx, shdr.sh_name);
        if (name == NULL || strcmp(name, ".c18nstrtab") != 0)
            continue;

        Elf_Data *data = elf_getdata(scn, NULL);
        if (data == NULL || data->d_buf == NULL)
            return comparts;

        c18nstrtab = static_cast<const char *>(data->d_buf);
        c18nstrsize = data->d_size;
        break;
    }

    if (c18nstrtab == NULL || c18nstrsize == 0)
        return comparts;

    size_t phnum;
    if (elf_getphdrnum(elf, &phnum) != 0)
        return comparts;

    for (size_t i = 0; i < phnum; i++) {
        GElf_Phdr phdr;

        if (gelf_getphdr(elf, i, &phdr) == NULL || phdr.p_type != PT_C18N_NAME)
            continue;
        if (phdr.p_paddr >= c18nstrsize)
            continue;

        const char *name = c18nstrtab + phdr.p_paddr;
        const char *terminator = static_cast<const char *>(memchr(name, '\0',
            c18nstrsize - phdr.p_paddr));
        if (terminator == NULL)
            continue;

        comparts.emplace_back(compart_t{
            static_cast<addr_t>(phdr.p_vaddr),
            static_cast<addr_t>(phdr.p_vaddr + phdr.p_memsz),
            std::string(name, terminator)
        });
    }

    std::stable_sort(comparts.begin(), comparts.end(),
        [](const compart_t &a, const compart_t &b) {
            return a.start < b.start;
        });

    return comparts;
}

std::shared_ptr<image_t> load_image(const std::string &path)
{
    if (path.empty() || elf_version(EV_CURRENT) == EV_NONE)
        return {};

    auto [it, inserted] = images.try_emplace(path, nullptr);
    if (!inserted)
        return it->second;

    std::vector<symbol_t> symbols;
    std::vector<compart_t> comparts;

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
                comparts = load_comparts(elf);
            }
            elf_end(elf);
        }
        close(fd);
    }

    auto image = std::make_shared<image_t>(image_t{
        path,
        std::move(symbols),
        std::move(comparts)
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

const std::string *image_t::find_compart(addr_t start, addr_t end) const
{
    auto it = std::upper_bound(comparts.begin(), comparts.end(), start,
        [](addr_t value, const compart_t &compart) {
            return value < compart.start;
        });

    if (it != comparts.begin() && end <= --it->end)
        return &it->name;

    return NULL;
}
