/*-
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2023, rtegrity ltd. All rights reserved.
 */

#include <cheri/cheric.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mapping.h"
#include "symbol.h"


#define CHERITREE_ENV_EXCLUDE_ROOTS "CHERITREE_EXCLUDE_ROOTS"
#define CHERITREE_ROOT_PCC 31
#define CHERITREE_ROOT_CSP 32

static uint64_t excluded_roots;


static void exclude_root(int root)
{
    excluded_roots |= UINT64_C(1) << root;
}


static int root_is_excluded(int root)
{
    return (excluded_roots & (UINT64_C(1) << root)) != 0;
}


static int parse_root_name(const char *name)
{
    char *end;
    long regnum;

    if (!name || !*name)
        return -1;

    regnum = strtol(name, &end, 10);
    if (*end != '\0' || regnum < 0 || regnum > CHERITREE_ROOT_CSP)
        return -1;

    return regnum;
}


static void load_excluded_roots(const char *value)
{
    char *config, *token;

    config = strdup(value);
    if (!config)
        return;

    token = config;
    while (token) {
        char *entry = strsep(&token, ",");
        int root;

        if (!entry)
            break;

        root = parse_root_name(entry);
        if (root < 0)
            fprintf(stderr,
                "CheriTree: ignoring unknown root id '%s' in %s\n",
                entry, CHERITREE_ENV_EXCLUDE_ROOTS);
        else
            exclude_root(root);
    }

    free(config);
}


__attribute__((constructor))
static void cheritree_load_config(void)
{
    const char *value = getenv(CHERITREE_ENV_EXCLUDE_ROOTS);

    if (value && *value)
        load_excluded_roots(value);
}


void _cheritree_init(void *function, void *stack)
{
    mapping_t *functionmap, *stackmap;
    const char *owner;

    functionmap = cheritree_resolve_mapping((addr_t)function);
    owner = getname(functionmap);

    stackmap = cheritree_resolve_mapping((addr_t)stack);
    cheritree_set_mapping_name(stackmap, owner, "stack");
}


static void print_address(void *vaddr, const char *name, void **origin, int depth)
{
    addr_t addr = (addr_t)vaddr;
    mapping_t *mapping = cheritree_resolve_mapping(addr);
    symbol_t *symbol;
    addr_t offset;
    int i;

    for (i = 0; i < depth; ++i) putchar(' ');

    if (depth) printf("%p:", origin);
    else printf("%s", name);

    printf(" %#p  ", vaddr);

    if (!mapping || !*getname(mapping))
        goto out;

    printf("%s", getname(mapping));

    symbol = cheritree_find_symbol(getpath(mapping), getbase(mapping), addr);
    offset = addr - (addr_t)getbase(mapping);

    if (!symbol || !*getname(symbol)) {
        printf("+%#" PRIxADDR, offset);
        goto out;
    }

    printf("!%s", getname(symbol));

    offset -= symbol->value;
    if (offset)
        printf("+%#" PRIxADDR, offset);

out:
    putchar('\n');
}


static int get_pointer_range(void *vaddr, void ***pstart, void ***pend)
{
    void **ptr, **end;

    ptr = cheri_setaddress(vaddr,
        __align_up(cheri_getbase(vaddr), sizeof(*pstart)));

    end = cheri_setaddress(vaddr,
        __align_down(cheri_gettop(vaddr), sizeof(*pend)));

    if (!cheri_gettag(ptr) || !cheri_gettag(end)) return 0;

    *pstart = ptr;
    *pend = end;
    return 1;
}


static int is_printed(map_t *map, void *addr)
{
    addr_t start = cheri_getbase(addr);
    addr_t end = cheri_gettop(addr);

    return !cheritree_map_add(map, start, end);
}


static int is_exclude(map_t *exclude, void ***pptr)
{
    addr_t addr = (addr_t)*pptr;
    range_t range;

    if (!cheritree_map_find(exclude, addr, &range))
        return 0;

    *pptr = cheri_setaddress(*pptr, range.end);
    return 1;
}


static void print_capability_tree(map_t *map, map_t *exclude,
    void *vaddr, const char *name, void **origin, int depth)
{
    void **ptr, **end, *p;

    if (!cheri_gettag(vaddr) || is_printed(map, vaddr)) return;

    print_address(vaddr, name, origin, depth);

    if (get_pointer_range(vaddr, &ptr, &end))
        while (ptr < end)
            if (!is_exclude(exclude, &ptr) &&
                cheritree_dereference_address(&ptr, &p)) {
                print_capability_tree(map, exclude, p, name, ptr, depth + 1);
                ++ptr;
            }
}


void _cheritree_print_capabilities(void **regs, int nregs)
{
    map_t map, exclude;
    mapping_t *stack;
    char reg[20];
    int i;

    if (nregs > 30)
        _cheritree_init(regs[30], regs);

    cheritree_map_init(&map, 1024);
    cheritree_map_init(&exclude, 100);

    // Exclude cheritree stack frames

    stack = cheritree_resolve_mapping((addr_t)regs);
    cheritree_map_add(&exclude, (stack) ? stack->start : (addr_t)regs,
        (addr_t)(regs + nregs));

    if (nregs > 31 && !root_is_excluded(CHERITREE_ROOT_PCC))
        print_capability_tree(&map, &exclude, regs[31], "pcc", NULL, 0);

    if (!root_is_excluded(CHERITREE_ROOT_CSP))
        print_capability_tree(&map, &exclude, regs, "csp", NULL, 0);

    for (i = 0; i < nregs && i < 31; i++) {
        if (root_is_excluded(i))
            continue;

        sprintf(reg, "c%d", i);
        print_capability_tree(&map, &exclude, regs[i], reg, NULL, 0);
    }

    cheritree_map_delete(&map);
    cheritree_map_delete(&exclude);
}
