/*-
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2026, Dapeng Gao.
 */

#include <cheri/cheric.h>

#include <cstdio>
#include <map>

#include "capabilities.h"
#include "mapping.h"
#include "symbol.h"

extern "C" __attribute__((weak))
uintptr_t dl_c18n_get_trampoline_target(const void *)
{
    return 0;
}

extern int cheritree_json_output;
extern FILE *cheritree_output;

uint64_t excluded_roots;
void *cheritree_regs[CHERITREE_NREGS];

static bool root_is_excluded(int root)
{
    return (excluded_roots & (UINT64_C(1) << root)) != 0;
}

// Permission-aware range map.
//
// Stores non-overlapping ranges [start, end) each associated with a permission
// bitmask (the union of all permissions seen for that range).  Adjacent ranges
// with identical permissions are coalesced.
struct perm_region_t {
    struct rights_t {
        unsigned long perms;
        bool sealed;
        bool via_trampoline;

        bool operator!=(const rights_t &other) const
        {
            return perms != other.perms ||
                sealed != other.sealed ||
                via_trampoline != other.via_trampoline;
        }

        rights_t &operator|=(const rights_t &other)
        {
            perms |= other.perms;
            sealed = sealed && other.sealed;
            via_trampoline = via_trampoline && other.via_trampoline;
            return *this;
        }

        rights_t operator|(const rights_t &other) const
        {
            rights_t merged = *this;

            merged |= other;
            return merged;
        }
    };

    addr_t end;
    rights_t rights;
};

using permmap_t = std::map<addr_t, perm_region_t>;

// Add [start, end) with the given permissions to the map.  Returns 0 if the
// range was already fully covered with at least the given permissions, 1
// otherwise (after updating the map).
static bool permmap_add(
    permmap_t &m, addr_t start, addr_t end, perm_region_t::rights_t rights)
{
    // Invariant on entry: the map contains non-overlapping, sorted ranges and
    // no two adjacent entries carry identical rights (fully coalesced).

    // Fast path: return false without modifying the map if [start, end) is
    // already entirely covered and every sub-range already carries at least
    // the requested rights (i.e. `rights` is a subset of each entry's rights).
    {
        auto it = m.upper_bound(start);
        if (it != m.begin()) {
            --it;
            addr_t cursor = start;
            do {
                if (it->second.end <= cursor)
                    break;
                if ((it->second.rights | rights) != it->second.rights)
                    break;
                cursor = it->second.end;
                if (cursor >= end)
                    return false;
                ++it;
            } while (it != m.end() && it->first <= cursor);
        }
    }

    // 1. Split any range that straddles `start`.
    // Invariant after: `start` is an exact entry boundary; no entry spans
    // across `start`.
    {
        auto it = m.upper_bound(start);
        if (it != m.begin()) {
            auto prev = std::prev(it);
            if (prev->second.end > start) {
                addr_t old_end = prev->second.end;
                prev->second.end = start;
                m[start] = {old_end, prev->second.rights};
            }
        }
    }

    // 2. Split any range that straddles `end`.
    // Invariant after: both `start` and `end` are exact entry boundaries; no
    // entry crosses either boundary.
    {
        auto it = m.upper_bound(end);
        if (it != m.begin()) {
            auto prev = std::prev(it);
            if (prev->first < end && prev->second.end > end) {
                addr_t old_end = prev->second.end;
                prev->second.end = end;
                m[end] = {old_end, prev->second.rights};
            }
        }
    }

    // 3. OR `rights` into every existing sub-range within [start, end), and
    // fill any gaps with new entries carrying just `rights`.
    // Invariant after: [start, end) is fully covered; every entry within it
    // carries the requested rights OR'd in; entries outside [start, end) are
    // unchanged.  Adjacent entries within the modified region may now have
    // equal rights, violating the coalesced invariant.
    addr_t cursor = start;
    auto it = m.lower_bound(start);

    while (cursor < end) {
        if (it != m.end() && it->first == cursor) {
            it->second.rights |= rights;
            cursor = it->second.end;
            ++it;
        } else {
            addr_t gap_end = end;
            if (it != m.end() && it->first < gap_end)
                gap_end = it->first;
            m[cursor] = {gap_end, rights};
            cursor = gap_end;
        }
    }

    // 4. Restore the coalesced invariant by merging adjacent entries with
    // identical rights.  Only entries in or touching [start, end) can gain new
    // merge opportunities, so we sweep from one entry before `start` and stop
    // after checking the pair that touches the `end` boundary.
    it = m.lower_bound(start);
    if (it != m.begin())
        it = std::prev(it);
    do {
        auto next = std::next(it);
        if (next == m.end())
            break;
        if (it->second.end != next->first ||
            it->second.rights != next->second.rights)
            it = next;
        else {
            it->second.end = next->second.end;
            m.erase(next);
        }
    } while (it->second.end <= end);

    // Invariant restored: the map is once again fully coalesced.
    return true;
}

static bool is_visited(permmap_t &map, void *vaddr, bool via_trampoline = false)
{
    addr_t start = cheri_base_get(vaddr);
    addr_t end = cheri_top_get(vaddr);
    perm_region_t::rights_t rights = {
        cheri_perms_get(vaddr),
        cheri_is_sealed(vaddr),
        via_trampoline,
    };

    return !permmap_add(map, start, end, rights);
}

static void print_symbol(addr_t addr)
{
    mapping_t &mapping = cheritree_resolve_mapping(addr);
    symbol_t *symbol = NULL;
    addr_t offset = 0;

    if (cheritree_json_output)
        fprintf(cheritree_output, ", \"mapping\": \"%s\",", mapping.name.c_str());
    else
        fprintf(cheritree_output, "%s", mapping.name.c_str());

    if (!mapping.name.empty()) {
        offset = addr - mapping.base->start;
        symbol = cheritree_find_symbol(mapping.path, mapping.base->start, addr);
        if (symbol && !symbol->name.empty())
            offset -= symbol->value;
    }

    if (symbol && !symbol->name.empty()) {
        if (cheritree_json_output)
            fprintf(cheritree_output, " \"symbol\": \"%s\",", symbol->name.c_str());
        else
            fprintf(cheritree_output, "!%s", symbol->name.c_str());
    }

    if (cheritree_json_output)
        fprintf(cheritree_output, " \"offset\": %" PRIuADDR, offset);
    else if (offset)
        fprintf(cheritree_output, "+%#" PRIxADDR, offset);
}

static void print_address(void *vaddr, void **origin, int depth,
    bool via_trampoline, bool &first)
{
    addr_t addr = (addr_t)vaddr;
    addr_t base = cheri_base_get(vaddr);
    addr_t top = cheri_top_get(vaddr);
    int tag = cheri_tag_get(vaddr);
    int sealed = cheri_is_sealed(vaddr);
    int perms = cheri_perms_get(vaddr);
    int perm_load      = (perms & CHERI_PERM_LOAD) != 0;
    int perm_store     = (perms & CHERI_PERM_STORE) != 0;
    int perm_execute   = (perms & CHERI_PERM_EXECUTE) != 0;
    int perm_load_cap  = (perms & CHERI_PERM_LOAD_CAP) != 0;
    int perm_store_cap = (perms & CHERI_PERM_STORE_CAP) != 0;

    if (cheritree_json_output) {
        if (!first)
            fprintf(cheritree_output, ",\n");
        first = false;
        fprintf(cheritree_output, "\t{ \"depth\": %d,", depth);
    } else
        for (int i = 0; i < depth; ++i)
            fputc(' ', cheritree_output);

    if (cheritree_json_output)
        fprintf(cheritree_output, " \"origin\": \"%p\",", origin);
    else
        fprintf(cheritree_output, "%p:", origin);

    if (cheritree_json_output)
        fprintf(cheritree_output,
            " \"address\": %" PRIuADDR ","
            " \"base\": %" PRIuADDR ","
            " \"top\": %" PRIuADDR ","
            " \"tag\": %s,"
            " \"sealed\": %s,"
            " \"perm_load\": %s,"
            " \"perm_store\": %s,"
            " \"perm_execute\": %s,"
            " \"perm_load_cap\": %s,"
            " \"perm_store_cap\": %s,"
            " \"via_trampoline\": %s",
            addr, base, top,
            tag ? "true" : "false",
            sealed ? "true" : "false",
            perm_load ? "true" : "false",
            perm_store ? "true" : "false",
            perm_execute ? "true" : "false",
            perm_load_cap ? "true" : "false",
            perm_store_cap ? "true" : "false",
            via_trampoline ? "true" : "false");
    else
        fprintf(cheritree_output, " %#p  ", vaddr);

    if (perm_load || perm_store || perm_execute)
        print_symbol(addr);

    if (cheritree_json_output)
        fprintf(cheritree_output, " }");
    else
        fputc('\n', cheritree_output);
}

static void print_capability_tree(permmap_t &map,
    void *vaddr, void **origin, int depth, bool &first)
{
    void **ptr, **end, *p;

    if (!cheri_tag_get(vaddr) || is_visited(map, vaddr))
        return;

    print_address(vaddr, origin, depth, false, first);

    void *target = (void *)dl_c18n_get_trampoline_target(vaddr);
    if (target && cheri_base_get(target) && !is_visited(map, target, true))
        print_address(target, origin, depth, true, first);

    if ((cheri_perms_get(vaddr) & CHERI_PERM_LOAD) == 0 ||
        (cheri_perms_get(vaddr) & CHERI_PERM_LOAD_CAP) == 0)
        return;

    ptr = (void **)cheri_address_set(vaddr,
        __align_up(cheri_base_get(vaddr), sizeof(*ptr)));

    end = (void **)cheri_address_set(vaddr,
        __align_down(cheri_top_get(vaddr), sizeof(*end)));

    // Sanity check
    if (!cheri_tag_get(end))
        return;

    while (cheri_tag_get(ptr) && ptr < end)
        if (cheritree_dereference_address(&ptr, &p)) {
            print_capability_tree(map, p, ptr, depth + 1, first);
            ++ptr;
        }
}

void cheritree_print_capabilities()
{
    permmap_t map;
    bool first = true;

    // Exclude CheriTree's exported global variables
    is_visited(map, &cheritree_json_output);
    is_visited(map, &cheritree_regs);

    if (cheritree_json_output)
        fprintf(cheritree_output, "[\n");

    for (uintptr_t i = 0; i < CHERITREE_NREGS; ++i) {
        if (root_is_excluded(i))
            continue;

        print_capability_tree(map, cheritree_regs[i], (void **)i, 0, first);
    }

    if (cheritree_json_output)
        fprintf(cheritree_output, "\n]");
}
