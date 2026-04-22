/*-
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2023, rtegrity ltd. All rights reserved.
 *  Copyright (c) 2026, Dapeng Gao.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "mapping.h"
#include "symbol.h"
#include "capabilities.h"

#define CHERITREE_ENV_EXCLUDE_ROOTS "CHERITREE_EXCLUDE_ROOTS"
#define CHERITREE_ENV_JSON_OUTPUT "CHERITREE_JSON_OUTPUT"
#define CHERITREE_ENV_OUTPUT_PATH "CHERITREE_OUTPUT_PATH"

int cheritree_json_output;
FILE *cheritree_output;

static void exclude_root(int root)
{
    excluded_roots |= UINT64_C(1) << root;
}

static bool parse_and_exclude_roots(const char *entry)
{
    const char *dash;
    long start, end;
    char *endptr;
    int i;

    if (!entry || !*entry)
        return false;

    dash = strchr(entry, '-');
    if (!dash) {
        // Single root ID
        start = strtol(entry, &endptr, 10);
        if (*endptr != '\0' || start < 0 || start >= CHERITREE_NREGS)
            return false;
        exclude_root(start);
        return true;
    }

    // Range: parse start
    start = strtol(entry, &endptr, 10);
    if (endptr != dash || start < 0 || start >= CHERITREE_NREGS)
        return false;

    // Parse end
    end = strtol(dash + 1, &endptr, 10);
    if (*endptr != '\0' || end < 0 || end >= CHERITREE_NREGS || end < start)
        return false;

    // Exclude all in range
    for (i = start; i <= end; ++i)
        exclude_root(i);

    return true;
}

static void load_excluded_roots(const char *value)
{
    char *config, *token;

    config = strdup(value);
    if (!config)
        return;

    for (token = config; token; ) {
        char *entry = strsep(&token, ",");

        if (!parse_and_exclude_roots(entry))
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
    cheritree_json_output = (value && *value);

    value = getenv(CHERITREE_ENV_OUTPUT_PATH);
    if (value && *value) {
        cheritree_output = fopen(value, "w");
        if (cheritree_output)
            setvbuf(cheritree_output, NULL, _IOLBF, 0);
        else
            fprintf(stderr,
                "CheriTree: unable to open output file '%s'\n", value);
    }
}

extern "C"
struct cheritree_dummy_pair {
    uintptr_t a, b;
};

extern "C"
struct cheritree_dummy_pair _cheritree_init(void *function, void *stack)
{
    mapping_t &functionmap = cheritree_resolve_mapping((addr_t)function);
    std::string owner = functionmap.name;

    mapping_t &stackmap = cheritree_resolve_mapping((addr_t)stack);
    cheritree_set_mapping_name(stackmap, owner, "stack");

    return (struct cheritree_dummy_pair) {};
}

extern "C"
struct cheritree_dummy_pair _cheritree_print()
{
    if (!cheritree_output)
        return (struct cheritree_dummy_pair) {};

    _cheritree_init(cheritree_regs[CHERITREE_ROOT_PCC],
        cheritree_regs[CHERITREE_ROOT_CSP]);

    if (cheritree_json_output) {
        fprintf(cheritree_output, "{\n\"mappings\": ");
        cheritree_print_mappings();
        fprintf(cheritree_output, ",\n\"capabilities\": ");
        cheritree_print_capabilities();
        fprintf(cheritree_output, "\n}\n");
    } else {
        cheritree_print_mappings();
        cheritree_print_capabilities();
    }

    return (struct cheritree_dummy_pair) {};
}
