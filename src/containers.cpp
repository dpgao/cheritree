/*-
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2023, rtegrity ltd. All rights reserved.
 */

#include <map>

#include "util.h"


struct map {
    std::map<addr_t, addr_t> ranges;
};


struct addrset {
    std::map<addr_t, addr_t> ranges;
};


extern "C" {


map_t *cheritree_map_create(void)
{
    return new map_t;
}


int cheritree_map_add(map_t *v, addr_t start, addr_t end)
{
    auto &m = v->ranges;

    // Check if already fully covered
    auto it = m.upper_bound(start);
    if (it != m.begin()) {
        auto prev = std::prev(it);
        if (prev->first <= start && end <= prev->second)
            return 0;
    }

    // Find and merge overlapping ranges
    addr_t new_start = start;
    addr_t new_end = end;

    it = m.lower_bound(start);
    if (it != m.begin()) {
        auto prev = std::prev(it);
        if (prev->second >= start)
            it = prev;
    }

    while (it != m.end() && it->first <= new_end) {
        if (it->first < new_start) new_start = it->first;
        if (it->second > new_end) new_end = it->second;
        it = m.erase(it);
    }

    m[new_start] = new_end;
    return 1;
}


int cheritree_map_find(map_t *v, addr_t addr, range_t *prange)
{
    auto &m = v->ranges;
    auto it = m.upper_bound(addr);

    if (it == m.begin()) return 0;
    --it;

    if (addr < it->second) {
        prange->start = it->first;
        prange->end = it->second;
        return 1;
    }

    return 0;
}


void cheritree_map_delete(map_t *v)
{
    delete v;
}


addrset_t *cheritree_addrset_create(void)
{
    return new addrset_t;
}


int cheritree_addrset_add(addrset_t *v, addr_t addr)
{
    auto &m = v->ranges;
    addr_t new_start = addr;
    addr_t new_end = addr + 1;

    auto it = m.upper_bound(addr);
    if (it != m.begin()) {
        auto prev = std::prev(it);
        if (addr < prev->second)
            return 0;
        if (prev->second == addr) {
            new_start = prev->first;
            it = m.erase(prev);
        }
    }

    if (it != m.end() && it->first == new_end) {
        new_end = it->second;
        it = m.erase(it);
    }

    m[new_start] = new_end;
    return 1;
}


void cheritree_addrset_delete(addrset_t *v)
{
    delete v;
}


}
