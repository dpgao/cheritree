/*-
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2023, rtegrity ltd. All rights reserved.
 *  Copyright (c) 2026, Dapeng Gao.
 */

#include <cheriintrin.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "mapping.h"
#include "symbol.h"

// Mapping type.
#define CT_TYPE_NONE            0
#define CT_TYPE_DEFAULT         1
#define CT_TYPE_VNODE           2
#define CT_TYPE_SWAP            3
#define CT_TYPE_DEVICE          4
#define CT_TYPE_PHYS            5
#define CT_TYPE_DEAD            6
#define CT_TYPE_SG              7
#define CT_TYPE_MGTDEVICE       8
#define CT_TYPE_GUARD           9
#define CT_TYPE_UNKNOWN         10
#define CT_TYPE_MASK            0xff

// Access protection.
#define CT_PROT_NONE            0
#define CT_PROT_EXEC            0x0100
#define CT_PROT_WRITE           0x0200
#define CT_PROT_READ            0x0400
#define CT_PROT_WRITE_CAP       0x0800
#define CT_PROT_READ_CAP        0x1000
#define CT_PROT_MASK            0xff00

// Mapping flags.
#define CT_FLAG_COW             0x00010000
#define CT_FLAG_GUARD           0x00020000
#define CT_FLAG_UNMAPPED        0x00040000
#define CT_FLAG_NEEDS_COPY      0x00080000
#define CT_FLAG_SUPER           0x00100000
#define CT_FLAG_GROWS_UP        0x00200000
#define CT_FLAG_GROWS_DOWN      0x00400000
#define CT_FLAG_USER_WIRED      0x00800000
#define CT_FLAG_SHARED          0x01000000
#define CT_FLAG_PRIVATE         0x02000000
#define CT_FLAG_HOLD_CAP        0x04000000

extern "C" int cheritree_json_output;
extern "C" FILE *cheritree_output;

static std::vector<std::unique_ptr<mapping_t>> mappings;
static mapping_t mapping_zero{0, 0, CT_PROT_NONE, ""};

static void flags_to_str(int flags, char *s, size_t len);
static int str_to_flags(char *s, size_t len);

inline int mapping_t::prot() const { return flags & CT_PROT_MASK; }

void cheritree_set_mapping_name(mapping_t &mapping,
    const std::string &owner, const std::string &name)
{
    if (owner.empty())
        mapping.name = "[" + name + "]";
    else
        mapping.name = "[" + owner + "!" + name + "]";
}

static void add_mapping_name(mapping_t &mapping)
{
    addr_t start = mapping.start, end = mapping.end;

    // Copy any previously identified name
    auto it = std::lower_bound(mappings.begin(), mappings.end(), start,
        [](const std::unique_ptr<mapping_t> &mp, addr_t value) {
            return mp->start < value;
        });
    if (it != mappings.end() &&
        (*it)->start == start && (*it)->end == end && (*it)->path.empty()) {
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
    addr_t start, addr_t end, int flags, const std::string &path)
{
    mapping_t *base = nullptr;
    auto mapping = std::make_unique<mapping_t>(start, end, flags, path);

    // Find a candidate base
    for (auto &mp : v)
        if (mp->base == mp.get() && !mp->path.empty()) {
            base = mp.get();
            if (path == mp->path) {
                mapping->base = base;
                break;
            }
        }

    if (path.empty()) {
        if (base && cheritree_find_type(base->path, base->start, start, end)) {
            mapping->base = base;
            mapping->name = base->name;
        } else
            add_mapping_name(*mapping);
    } else {
        size_t pos = path.rfind('/');
        if (pos == std::string::npos)
            mapping->name = path;
        else
            mapping->name = path.substr(pos + 1);

        cheritree_load_symbols(path);
    }

    v.push_back(std::move(mapping));
}

#ifdef __FreeBSD__
static struct flagmap { int i; char s[7]; int f; } flagmap[] = {
    { 0, "-----", 0 }, { 0, "r", CT_PROT_READ },
    { 1, "w", CT_PROT_WRITE }, { 2, "x", CT_PROT_EXEC },
    { 3, "R", CT_PROT_READ_CAP }, { 4, "W", CT_PROT_WRITE_CAP },

    { 6, "------", 0 }, { 6, "u", CT_FLAG_UNMAPPED },
    { 6, "G", CT_FLAG_GUARD }, { 6, "C", CT_FLAG_COW },
    { 7, "N", CT_FLAG_NEEDS_COPY }, { 8, "S", CT_FLAG_SUPER },
    { 9, "D", CT_FLAG_GROWS_DOWN }, { 9, "U", CT_FLAG_GROWS_UP },
    { 10, "W", CT_FLAG_USER_WIRED }, { 11, "c", CT_FLAG_HOLD_CAP },

    { 13, "--", 0 }, { 13, "df", CT_TYPE_DEFAULT },
    { 13, "vn", CT_TYPE_VNODE }, { 13, "sw", CT_TYPE_SWAP },
    { 13, "dv", CT_TYPE_DEVICE }, { 13, "ph", CT_TYPE_PHYS },
    { 13, "dd", CT_TYPE_DEAD }, { 13, "sg", CT_TYPE_SG },
    { 13, "md", CT_TYPE_MGTDEVICE }, { 13, "gd", CT_TYPE_GUARD },
    { 13, "??", CT_TYPE_UNKNOWN },
    { 0 }
};

static void print_mapping(const mapping_t &mapping)
{
    int flags = mapping.flags;
    char s[16];

    flags_to_str(flags, s, sizeof(s));

    if (cheritree_json_output)
        fprintf(cheritree_output,
            "\t{ \"start\": %" PRIuADDR ", \"end\": %" PRIuADDR ","
            " \"prot_read\": %s, \"prot_write\": %s, \"prot_exec\": %s,"
            " \"prot_read_cap\": %s, \"prot_write_cap\": %s,"
            " \"flags\": \"%s\", \"type\": \"%s\","
            " \"mapping\": \"%s\", \"base\": %" PRIuADDR " }",
            mapping.start, mapping.end,
            (flags & CT_PROT_READ)      ? "true" : "false",
            (flags & CT_PROT_WRITE)     ? "true" : "false",
            (flags & CT_PROT_EXEC)      ? "true" : "false",
            (flags & CT_PROT_READ_CAP)  ? "true" : "false",
            (flags & CT_PROT_WRITE_CAP) ? "true" : "false",
            &s[6], &s[13],
            (mapping.path.empty() ? mapping.name : mapping.path).c_str(),
            mapping.base->start);
    else
        fprintf(cheritree_output,
            "%#" PRIxADDR "-%#" PRIxADDR " %s %s %s %s [%#" PRIxADDR "]\n",
            mapping.start, mapping.end, &s[0], &s[6], &s[13],
            (mapping.path.empty() ? mapping.name : mapping.path).c_str(),
            mapping.base->start);
}

static int load_mapping(char *buffer, std::vector<std::unique_ptr<mapping_t>> &v)
{
    addr_t start, end;
    char s[16], path[PATH_MAX];

    s[0] = '\0';
    path[0] = '\0';

    if (sscanf(buffer, "%*d %" PRIxADDR " %" PRIxADDR
            " %5s %*d %*d %*d %*d %6s %2s %s", &start, &end,
            &s[0], &s[6], &s[13], path) < 5)
        return 0;

    add_mapping(v, start, end, str_to_flags(s, sizeof(s)), path);

    return 1;
}

static void load_mappings()
{
    char buffer[1024];
    std::vector<std::unique_ptr<mapping_t>> v;

    sprintf(buffer, "procstat -v -h %d", getpid());

    FILE *fp = popen(buffer, "r");
    if (fp) {
        while (fgets(buffer, sizeof(buffer), fp) != NULL)
            if (!load_mapping(buffer, v)) {
                fprintf(stderr,
                    "CheriTree: Cannot parse mapping: %s\n", buffer);
                exit(1);
            }
        pclose(fp);
    }

    if (v.empty()) {
        fprintf(stderr, "CheriTree: Cannot load mappings\n");
        exit(1);
    }

    mappings = std::move(v);
}
#endif // __FreeBSD__

#ifdef __linux__
static struct flagmap { int i; char s[5]; int f; } flagmap[] = {
    { 0, "----", 0 }, { 0, "r", CT_PROT_READ },
    { 1, "w", CT_PROT_WRITE }, { 2, "x", CT_PROT_EXEC },
    { 3, "p", CT_FLAG_SHARED }, { 3, "p", CT_FLAG_PRIVATE },
    { 0 }
};

static void print_mapping(const mapping_t &mapping)
{
    int flags = mapping.flags;
    char s[5];

    flags_to_str(flags, s, sizeof(s));

    if (cheritree_json_output)
        fprintf(cheritree_output,
            "\t{ \"start\": %" PRIuADDR ", \"end\": %" PRIuADDR ","
            " \"prot_read\": %s, \"prot_write\": %s, \"prot_exec\": %s,"
            " \"flag_private\": %s,"
            " \"mapping\": \"%s\", \"base\": %" PRIuADDR " }",
            mapping.start, mapping.end,
            (flags & CT_PROT_READ)    ? "true" : "false",
            (flags & CT_PROT_WRITE)   ? "true" : "false",
            (flags & CT_PROT_EXEC)    ? "true" : "false",
            (flags & CT_FLAG_PRIVATE) ? "true" : "false",
            (mapping.path.empty() ? mapping.name : mapping.path).c_str(),
            mapping.base->start);
    else
        fprintf(cheritree_output,
            "%#" PRIxADDR "-%#" PRIxADDR " %s %s [%#" PRIxADDR "]\n",
            mapping.start, mapping.end, s,
            (mapping.path.empty() ? mapping.name : mapping.path).c_str(),
            mapping.base->start);
}

static int load_mapping(char *buffer, std::vector<std::unique_ptr<mapping_t>> &v)
{
    addr_t start, end;
    char s[5], path[PATH_MAX];

    s[0] = '\0';
    path[0] = '\0';

    if (sscanf(buffer, "%" PRIxADDR "-%" PRIxADDR
            " %4s %*x %*d:%*d %*d %s", &start, &end, s, path) < 4)
        return 0;

    add_mapping(v, start, end, str_to_flags(s, sizeof(s)), path);

    return 1;
}

static void load_mappings()
{
    char buffer[1024];
    std::vector<std::unique_ptr<mapping_t>> v;

    sprintf(buffer, "/proc/%d/maps", getpid());

    FILE *fp = fopen(buffer, "r");
    if (fp) {
        while (fgets(buffer, sizeof(buffer), fp) != NULL)
            if (!load_mapping(buffer, v)) {
                fprintf(stderr,
                    "CheriTree: Cannot parse mapping: %s\n", buffer);
                exit(1);
            }
        fclose(fp);
    }

    if (v.empty()) {
        fprintf(stderr, "CheriTree: Cannot load mappings\n");
        exit(1);
    }

    mappings = std::move(v);
}
#endif // __linux__

static void flags_to_str(int flags, char *s, size_t len)
{
    int type = (flags & CT_TYPE_MASK);
    struct flagmap *fp;

    memset(s, 0, len);

    for (fp = flagmap; *fp->s; fp++)
        if ((fp->f & CT_TYPE_MASK) ? (type == fp->f) : (flags & fp->f) == fp->f)
            if (fp->i + strlen(fp->s) < len)
                strncpy(&s[fp->i], fp->s, strlen(fp->s));
}

static int str_to_flags(char *s, size_t len)
{
    struct flagmap *fp;
    int flags = 0;

    for (fp = flagmap; *fp->s; fp++)
        if (fp->f && fp->i + strlen(fp->s) < len)
            if (!strncmp(&s[fp->i], fp->s, strlen(fp->s)))
                flags |= fp->f;

    return flags;
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
    if (find_mapping(addr).prot() == CT_PROT_NONE)
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

    if ((mapping.prot() & CT_PROT_READ) == 0 ||
        (mapping.prot() & CT_PROT_READ_CAP) == 0) {
        *pptr = (void **)cheri_address_set(*pptr, mapping.end);
        return 0;
    }

    *paddr = **pptr;
    return 1;
}
