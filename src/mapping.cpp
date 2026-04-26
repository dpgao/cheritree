/*-
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2023, rtegrity ltd. All rights reserved.
 *  Copyright (c) 2026, Dapeng Gao.
 */

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/user.h>

#include <cheriintrin.h>
#include <libprocstat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <vector>

#include "mapping.h"
#include "symbol.h"

extern "C" int cheritree_json_output;
extern "C" FILE *cheritree_output;

static std::vector<std::unique_ptr<mapping_t>> mappings;
static mapping_t mapping_zero{
    std::numeric_limits<addr_t>::max(),
    std::numeric_limits<addr_t>::max(),
    0
};

mapping_t::mapping_t(addr_t start, addr_t end, int prot)
    : base(this), start(start), end(end),
      prot_read((prot & KVME_PROT_READ) != 0),
      prot_write((prot & KVME_PROT_WRITE) != 0),
      prot_exec((prot & KVME_PROT_EXEC) != 0),
      prot_read_cap((prot & (KVME_PROT_READ | KVME_PROT_CAP)) ==
          (KVME_PROT_READ | KVME_PROT_CAP)),
      prot_write_cap((prot & (KVME_PROT_WRITE | KVME_PROT_CAP)) ==
          (KVME_PROT_WRITE | KVME_PROT_CAP))
{
}

static const std::string &mapping_label(const mapping_t &mapping)
{
    if (mapping.image)
        return mapping.image->path;

    return mapping.name;
}

inline int mapping_t::prot() const
{
    return (prot_read ? KVME_PROT_READ : 0) |
        (prot_write ? KVME_PROT_WRITE : 0) |
        (prot_exec ? KVME_PROT_EXEC : 0) |
        ((prot_read_cap || prot_write_cap) ? KVME_PROT_CAP : 0);
}

void cheritree_set_mapping_name(mapping_t &mapping,
    const std::string &owner, const std::string &name)
{
    if (owner.empty())
        mapping.name = "[" + name + "]";
    else
        mapping.name = "[" + owner + "!" + name + "]";
}

static void infer_mapping_name(mapping_t &mapping)
{
    addr_t start = mapping.start, end = mapping.end;

    // Copy any previously identified name
    auto it = std::lower_bound(mappings.begin(), mappings.end(), start,
        [](const std::unique_ptr<mapping_t> &mp, addr_t value) {
            return mp->start < value;
        });
    if (it != mappings.end() &&
        (*it)->start == start && (*it)->end == end && !(*it)->image) {
        mapping.name = (*it)->name;
        return;
    }

    // Check for current stack mapping
    if (start <= (addr_t)&start && (addr_t)&start < end) {
        cheritree_set_mapping_name(mapping, {}, "stack");
        return;
    }

    // Check for current heap mapping
    if (start <= (addr_t)&mapping && (addr_t)&mapping < end) {
        cheritree_set_mapping_name(mapping, {}, "heap");
        return;
    }
}

static void add_mapping(std::vector<std::unique_ptr<mapping_t>> &v,
    addr_t start, addr_t end, int prot, const std::string &path)
{
    auto mapping = std::make_unique<mapping_t>(start, end, prot);
    mapping_t *base = nullptr;
    auto it = v.rbegin();

    for (addr_t cursor = start; it != v.rend(); ++it) {
        mapping_t *mp = it->get();

        if (mp->end != cursor)
            break;
        cursor = mp->start;

        // The first file-backed mapping in the chain decides the base.
        if (mp->image) {
            base = mp->base;
            break;
        }
    }

    if (path.empty()) {
        if (base && base->image->has_symbol(base->start, start, end)) {
            while (it != v.rbegin()) {
                --it;
                it->get()->base = base;
                it->get()->image = base->image;
            }
            mapping->base = base;
            mapping->image = base->image;
            mapping->name = base->name;
        } else
            infer_mapping_name(*mapping);
    } else {
        mapping->image = load_image(path);

        if (base && base->image == mapping->image) {
            while (it != v.rbegin()) {
                --it;
                it->get()->base = base;
                it->get()->image = base->image;
            }
            mapping->base = base;
            mapping->image = base->image;
            mapping->name = base->name;
        } else
            mapping->name = path.rfind('/') == std::string::npos ?
                            path :
                            path.substr(path.rfind('/') + 1);
    }

    v.push_back(std::move(mapping));
}

static void print_mapping(const mapping_t &mapping)
{
    char s[6] = {
        mapping.prot_read ? 'r' : '-',
        mapping.prot_write ? 'w' : '-',
        mapping.prot_exec ? 'x' : '-',
        mapping.prot_read_cap ? 'R' : '-',
        mapping.prot_write_cap ? 'W' : '-',
        '\0',
    };

    if (cheritree_json_output)
        fprintf(cheritree_output,
            "\t{ \"start\": %" PRIuADDR ", \"end\": %" PRIuADDR ","
            " \"prot_read\": %s, \"prot_write\": %s, \"prot_exec\": %s,"
            " \"prot_read_cap\": %s, \"prot_write_cap\": %s,"
            " \"mapping\": \"%s\", \"base\": %" PRIuADDR " }",
            mapping.start, mapping.end,
            mapping.prot_read ? "true" : "false",
            mapping.prot_write ? "true" : "false",
            mapping.prot_exec ? "true" : "false",
            mapping.prot_read_cap ? "true" : "false",
            mapping.prot_write_cap ? "true" : "false",
            mapping_label(mapping).c_str(),
            mapping.base->start);
    else
        fprintf(cheritree_output,
            "%#" PRIxADDR "-%#" PRIxADDR " %s %s [%#" PRIxADDR "]\n",
            mapping.start, mapping.end, s,
            mapping_label(mapping).c_str(),
            mapping.base->start);
}

static void load_mappings()
{
    std::vector<std::unique_ptr<mapping_t>> v;
    struct procstat *procstat;
    struct kinfo_proc *kp;
    struct kinfo_vmentry *vmmap;
    unsigned int count, i;

    procstat = procstat_open_sysctl();
    if (procstat == NULL) {
        fprintf(stderr, "CheriTree: Cannot open procstat\n");
        exit(1);
    }

    kp = procstat_getprocs(procstat, KERN_PROC_PID, getpid(), &count);
    if (kp == NULL || count == 0) {
        procstat_close(procstat);
        fprintf(stderr, "CheriTree: Cannot load process metadata\n");
        exit(1);
    }

    vmmap = procstat_getvmmap(procstat, kp, &count);
    if (vmmap == NULL) {
        procstat_freeprocs(procstat, kp);
        procstat_close(procstat);
        fprintf(stderr, "CheriTree: Cannot load mappings\n");
        exit(1);
    }

    for (i = 0; i < count; i++) {
        const auto &kve = vmmap[i];
        add_mapping(v, kve.kve_start, kve.kve_end,
            kve.kve_protection, kve.kve_path);
    }

    procstat_freevmmap(procstat, vmmap);
    procstat_freeprocs(procstat, kp);
    procstat_close(procstat);

    if (v.empty()) {
        fprintf(stderr, "CheriTree: Cannot load mappings\n");
        exit(1);
    }

    mappings = std::move(v);
}

static mapping_t &find_mapping(addr_t addr)
{
    int retry = 0;

    do {
        auto it = std::upper_bound(mappings.begin(), mappings.end(), addr,
            [](addr_t value, const std::unique_ptr<mapping_t> &mp) {
                return value < mp->start;
            });

        if (it != mappings.begin()) {
            auto &mp = *std::prev(it);
            if (addr < mp->end)
                return *mp;
        }

        load_mappings();
    } while (retry++ < 1);

    fprintf(stderr,
        "CheriTree: cannot resolve mapping for %#" PRIxADDR "\n", addr);
    return mapping_zero;
}

mapping_t &cheritree_resolve_mapping(addr_t addr)
{
    if (find_mapping(addr).prot() == 0)
        load_mappings();
    return find_mapping(addr);
}

void cheritree_print_mappings()
{
    load_mappings();

    if (cheritree_json_output)
        fprintf(cheritree_output, "[\n");

    bool first = true;
    for (auto &mp : mappings) {
        if (cheritree_json_output) {
            if (!first)
                fprintf(cheritree_output, ",\n");
            first = false;
        }
        print_mapping(*mp);
    }

    if (cheritree_json_output)
        fprintf(cheritree_output, "\n]");
}

int cheritree_dereference_address(void ***pptr, void **paddr)
{
    mapping_t &mapping = find_mapping((addr_t)*pptr);

    if (!mapping.prot_read || !mapping.prot_read_cap) {
        *pptr = (void **)cheri_address_set(*pptr, mapping.end);
        return 0;
    }

    *paddr = **pptr;
    return 1;
}
