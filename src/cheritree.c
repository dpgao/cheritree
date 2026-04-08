/*-
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2023, rtegrity ltd. All rights reserved.
 */

#include <cheri/cheric.h>

#include <cheriintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mapping.h"
#include "symbol.h"


#define CHERITREE_ENV_EXCLUDE_ROOTS "CHERITREE_EXCLUDE_ROOTS"
#define CHERITREE_ENV_JSON_OUTPUT "CHERITREE_JSON_OUTPUT"
#define CHERITREE_ROOT_PCC 31
#define CHERITREE_ROOT_CSP 32

static uint64_t excluded_roots;
int json_output;


static void exclude_root(int root)
{
    excluded_roots |= UINT64_C(1) << root;
}


static int root_is_excluded(int root)
{
    return (excluded_roots & (UINT64_C(1) << root)) != 0;
}


static int parse_and_exclude_roots(const char *entry)
{
    const char *dash;
    long start, end;
    char *endptr;
    int i;

    if (!entry || !*entry)
        return -1;

    dash = strchr(entry, '-');
    if (!dash) {
        /* Single root ID */
        start = strtol(entry, &endptr, 10);
        if (*endptr != '\0' || start < 0 || start > CHERITREE_ROOT_CSP)
            return -1;
        exclude_root(start);
        return 0;
    }

    /* Range: parse start */
    start = strtol(entry, &endptr, 10);
    if (endptr != dash || start < 0 || start > CHERITREE_ROOT_CSP)
        return -1;

    /* Parse end */
    end = strtol(dash + 1, &endptr, 10);
    if (*endptr != '\0' || end < 0 || end > CHERITREE_ROOT_CSP || end < start)
        return -1;

    /* Exclude all in range */
    for (i = start; i <= end; ++i)
        exclude_root(i);

    return 0;
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

        if (!entry)
            break;

        if (parse_and_exclude_roots(entry) < 0)
            fprintf(stderr,
                "CheriTree: ignoring unknown root id '%s' in %s\n",
                entry, CHERITREE_ENV_EXCLUDE_ROOTS);
    }

    free(config);
}


__attribute__((constructor))
static void cheritree_load_config(void)
{
    const char *value;

    value = getenv(CHERITREE_ENV_EXCLUDE_ROOTS);
    if (value && *value)
        load_excluded_roots(value);

    value = getenv(CHERITREE_ENV_JSON_OUTPUT);
    json_output = (value && *value);
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
    ssize_t len;
    char buf[128];
    int perm_load = 0, perm_store = 0, perm_execute = 0;
    int perm_load_cap = 0, perm_store_cap = 0, perm_executive = 0;

    uintcap_t cap = (uintcap_t)vaddr;
    addr_t addr = (addr_t)vaddr;
    mapping_t *mapping = cheritree_resolve_mapping(addr);
    symbol_t *symbol = NULL;
    addr_t offset = 0;
    int i;

    if (json_output)
        printf("{ \"depth\": %d,", depth);
    else
        for (i = 0; i < depth; ++i) putchar(' ');

    if (depth) {
        if (json_output)
            printf(" \"origin\": \"%p\",", origin);
        else
            printf("%p:", origin);
    } else {
        if (json_output)
            printf(" \"origin\": \"%s\",", name);
        else
            printf("%s", name);
    }

    if (json_output) {
        strfcap(buf, sizeof(buf), "%a", cap);
        printf(" \"address\": %s,", buf);

        strfcap(buf, sizeof(buf), "%b", cap);
        printf(" \"base\": %s,", buf);

        strfcap(buf, sizeof(buf), "%t", cap);
        printf(" \"top\": %s,", buf);

        strfcap(buf, sizeof(buf), "%v", cap);
        printf(" \"tag\": %s,", buf[0] == '1' ? "true" : "false");

        strfcap(buf, sizeof(buf), "%S", cap);
        printf(" \"sealed\": %s,", strcmp(buf, "<unsealed>") != 0 ? "true" : "false");

        len = strfcap(buf, sizeof(buf), "%P", cap);
        if (len > 0) {
            perm_load      = (memchr(buf, 'r', len) != NULL);
            perm_store     = (memchr(buf, 'w', len) != NULL);
            perm_execute   = (memchr(buf, 'x', len) != NULL);
            perm_load_cap  = (memchr(buf, 'R', len) != NULL);
            perm_store_cap = (memchr(buf, 'W', len) != NULL);
            perm_executive = (memchr(buf, 'E', len) != NULL);
        }

        printf(" \"perm_load\": %s,",      perm_load      ? "true" : "false");
        printf(" \"perm_store\": %s,",     perm_store     ? "true" : "false");
        printf(" \"perm_execute\": %s,",   perm_execute   ? "true" : "false");
        printf(" \"perm_load_cap\": %s,",  perm_load_cap  ? "true" : "false");
        printf(" \"perm_store_cap\": %s,", perm_store_cap ? "true" : "false");
        printf(" \"perm_executive\": %s,", perm_executive ? "true" : "false");
    } else
        printf(" %#p  ", vaddr);

    if (json_output)
        printf(" \"mapping\": \"%s\",", getname(mapping));
    else
        printf("%s", getname(mapping));

    if (*getname(mapping)) {
        offset = addr - (addr_t)getbase(mapping);
        symbol = cheritree_find_symbol(getpath(mapping), getbase(mapping), addr);
        if (*getname(symbol))
            offset -= symbol->value;
    }

    if (json_output)
        printf(" \"symbol\": \"%s\",", getname(symbol));
    else if (*getname(symbol))
        printf("!%s", getname(symbol));

    if (json_output)
        printf(" \"offset\": %" PRIuADDR, offset);
    else if (offset)
        printf("+%#" PRIxADDR, offset);

out:
    if (json_output)
        printf(" }\n");
    else
        putchar('\n');
}


static int get_pointer_range(void *vaddr, void ***pstart, void ***pend)
{
    void **ptr, **end;

    ptr = cheri_address_set(vaddr,
        __align_up(cheri_base_get(vaddr), sizeof(*pstart)));

    end = cheri_address_set(vaddr,
        __align_down(cheri_top_get(vaddr), sizeof(*pend)));

    if (!cheri_tag_get(ptr) || !cheri_tag_get(end)) return 0;

    *pstart = ptr;
    *pend = end;
    return 1;
}


static int is_exclude(map_t *exclude, void ***pptr)
{
    addr_t addr = (addr_t)*pptr;
    range_t range;

    if (!cheritree_map_find(exclude, addr, &range))
        return 0;

    *pptr = cheri_address_set(*pptr, range.end);
    return 1;
}


static void print_capability_tree(addrset_t *visited, map_t *exclude,
    void *vaddr, const char *name, void **origin, int depth)
{
    void **ptr, **end, *p;

    if (!cheri_tag_get(vaddr) ||
        !cheritree_addrset_add(visited, (addr_t)origin))
        return;

    print_address(vaddr, name, origin, depth);

    if (get_pointer_range(vaddr, &ptr, &end))
        while (ptr < end)
            if (!is_exclude(exclude, &ptr) &&
                cheritree_dereference_address(&ptr, &p)) {
                print_capability_tree(visited, exclude, p, name, ptr, depth + 1);
                ++ptr;
            }
}


void _cheritree_print_capabilities(void **regs, int nregs)
{
    addrset_t *visited;
    map_t *exclude;
    mapping_t *stack;
    char reg[20];
    int i;

    if (nregs > 30)
        _cheritree_init(regs[30], regs);

    visited = cheritree_addrset_create();
    exclude = cheritree_map_create();

    // Exclude cheritree stack frames

    stack = cheritree_resolve_mapping((addr_t)regs);
    cheritree_map_add(exclude, (stack) ? stack->start : (addr_t)regs,
        (addr_t)(regs + nregs));

    if (nregs > 31 && !root_is_excluded(CHERITREE_ROOT_PCC))
        print_capability_tree(visited, exclude, regs[31], "pcc", NULL, 0);

    if (!root_is_excluded(CHERITREE_ROOT_CSP))
        print_capability_tree(visited, exclude, regs, "csp", NULL, 0);

    for (i = 0; i < nregs && i < 31; i++) {
        if (root_is_excluded(i))
            continue;

        sprintf(reg, "c%d", i);
        print_capability_tree(visited, exclude, regs[i], reg, NULL, 0);
    }

    cheritree_addrset_delete(visited);
    cheritree_map_delete(exclude);
}
