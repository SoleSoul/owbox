/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   xrm.c for owbox
   Copyright (c) 2026        owbox contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   See the COPYING file for a copy of the GNU General Public License.
*/

#include "xrm.h"

#include <string.h>

#define MAX_LEVELS 32

typedef struct {
    gchar **comp;     /* components, "?" is a single level wildcard */
    gboolean *loose;  /* binding before each component: '*' or '.' */
    gint n;
    gchar *value;
} Entry;

struct _RrXrmDb {
    GPtrArray *entries;
    /* exact entries by their full specification, so that later lines
       replace earlier ones like in Xrm */
    GHashTable *by_spec;
};

static void entry_free(gpointer data)
{
    Entry *e = data;
    gint i;
    for (i = 0; i < e->n; ++i) g_free(e->comp[i]);
    g_free(e->comp);
    g_free(e->loose);
    g_free(e->value);
    g_slice_free(Entry, e);
}

/* Unescape a resource value: \n, \\, \ooo; a trailing backslash continues
   the line (handled by the caller). */
static gchar* unescape(const gchar *s)
{
    GString *o = g_string_new(NULL);
    for (; *s; ++s) {
        if (*s == '\\' && s[1]) {
            if (s[1] == 'n') { g_string_append_c(o, '\n'); ++s; }
            else if (s[1] == '\\') { g_string_append_c(o, '\\'); ++s; }
            else if (s[1] >= '0' && s[1] <= '7' && s[2] >= '0' &&
                     s[2] <= '7' && s[3] >= '0' && s[3] <= '7')
            {
                g_string_append_c(o, (gchar)((s[1]-'0') * 64 +
                                             (s[2]-'0') * 8 + (s[3]-'0')));
                s += 3;
            }
            else g_string_append_c(o, *s);
        } else
            g_string_append_c(o, *s);
    }
    return g_string_free(o, FALSE);
}

static void add_line(RrXrmDb *db, const gchar *line)
{
    const gchar *colon, *p;
    gchar *spec, *value;
    Entry *e;
    GPtrArray *comps;
    GArray *loose;
    gboolean next_loose = FALSE;
    GString *cur;

    while (*line == ' ' || *line == '\t') ++line;
    if (*line == '\0' || *line == '!' || *line == '#') return;

    colon = strchr(line, ':');
    if (!colon) return;

    spec = g_strndup(line, colon - line);
    g_strstrip(spec);
    p = colon + 1;
    while (*p == ' ' || *p == '\t') ++p;
    value = unescape(p);

    comps = g_ptr_array_new();
    loose = g_array_new(FALSE, FALSE, sizeof(gboolean));
    cur = g_string_new(NULL);
    for (p = spec; ; ++p) {
        if (*p == '.' || *p == '*' || *p == '\0') {
            if (cur->len) {
                g_ptr_array_add(comps, g_strdup(cur->str));
                g_array_append_val(loose, next_loose);
                g_string_truncate(cur, 0);
                next_loose = FALSE;
            }
            if (*p == '*') next_loose = TRUE;
            if (*p == '\0') break;
        } else if (*p != ' ' && *p != '\t')
            g_string_append_c(cur, *p);
    }
    g_string_free(cur, TRUE);

    if (comps->len == 0 || comps->len > MAX_LEVELS) {
        g_ptr_array_free(comps, TRUE);
        g_array_free(loose, TRUE);
        g_free(spec);
        g_free(value);
        return;
    }

    e = g_slice_new(Entry);
    e->n = comps->len;
    e->comp = (gchar**)g_ptr_array_free(comps, FALSE);
    e->loose = (gboolean*)g_array_free(loose, FALSE);
    e->value = value;

    /* canonical spec: bindings plus components */
    {
        GString *key = g_string_new(NULL);
        gint i;
        Entry *old;
        for (i = 0; i < e->n; ++i) {
            g_string_append_c(key, e->loose[i] ? '*' : '.');
            g_string_append(key, e->comp[i]);
        }
        if ((old = g_hash_table_lookup(db->by_spec, key->str))) {
            /* replace the value, keeping the original entry */
            g_free(old->value);
            old->value = e->value;
            e->value = NULL;
            entry_free(e);
            g_string_free(key, TRUE);
        } else {
            g_hash_table_insert(db->by_spec, g_string_free(key, FALSE), e);
            g_ptr_array_add(db->entries, e);
        }
    }
    g_free(spec);
}

RrXrmDb* RrXrmLoadString(const gchar *data)
{
    RrXrmDb *db = g_slice_new(RrXrmDb);
    gchar **lines, **it;
    GString *logical = g_string_new(NULL);

    db->entries = g_ptr_array_new_with_free_func(entry_free);
    db->by_spec = g_hash_table_new_full(g_str_hash, g_str_equal,
                                        g_free, NULL);

    lines = g_strsplit(data, "\n", -1);
    for (it = lines; *it; ++it) {
        gsize len = strlen(*it);
        if (len && (*it)[len-1] == '\r') (*it)[--len] = '\0';
        /* a backslash at the end of the line continues it */
        if (len && (*it)[len-1] == '\\' &&
            (len < 2 || (*it)[len-2] != '\\'))
        {
            g_string_append_len(logical, *it, len - 1);
            continue;
        }
        g_string_append(logical, *it);
        add_line(db, logical->str);
        g_string_truncate(logical, 0);
    }
    if (logical->len) add_line(db, logical->str);
    g_string_free(logical, TRUE);
    g_strfreev(lines);
    return db;
}

RrXrmDb* RrXrmLoadFile(const gchar *path)
{
    gchar *data;
    RrXrmDb *db;

    if (!g_file_get_contents(path, &data, NULL, NULL))
        return NULL;
    db = RrXrmLoadString(data);
    g_free(data);
    return db;
}

void RrXrmFree(RrXrmDb *db)
{
    if (db) {
        g_hash_table_destroy(db->by_spec);
        g_ptr_array_free(db->entries, TRUE);
        g_slice_free(RrXrmDb, db);
    }
}

/* Per query level score.  Compared lexicographically from the leftmost level
   this implements Xrm's precedence rules:
   1. a level matched by a component beats a level elided by '*'
   2. a name match beats a class match, which beats '?'
   3. a tight binding beats a loose binding */
static gint level_score(gint matchtype, gboolean tight)
{
    return 100 + matchtype * 10 + (tight ? 1 : 0);
}

static gboolean better(const gint *a, const gint *b, gint n)
{
    gint i;
    for (i = 0; i < n; ++i)
        if (a[i] != b[i]) return a[i] > b[i];
    return FALSE;
}

static gboolean match(Entry *e, gint i, gchar **names, gchar **classes,
                      gint j, gint n, gint *score, gint *best)
{
    gboolean found = FALSE;

    if (i == e->n) {
        if (j == n && better(score, best, n)) {
            memcpy(best, score, n * sizeof(gint));
            return TRUE;
        }
        return j == n;
    }
    if (j >= n) return FALSE;
    /* not enough levels left to match the remaining components */
    if (e->n - i > n - j) return FALSE;

    {
        gint type = 0;
        if (!strcmp(e->comp[i], names[j])) type = 3;
        else if (!strcmp(e->comp[i], classes[j])) type = 2;
        else if (!strcmp(e->comp[i], "?")) type = 1;
        if (type) {
            score[j] = level_score(type, !e->loose[i]);
            found |= match(e, i + 1, names, classes, j + 1, n, score, best);
        }
    }
    if (e->loose[i]) {
        score[j] = 0;
        found |= match(e, i, names, classes, j + 1, n, score, best);
    }
    return found;
}

gchar* RrXrmGetResource(RrXrmDb *db, const gchar *name, const gchar *klass)
{
    gchar **names, **classes;
    gint n, i;
    gint score[MAX_LEVELS], best[MAX_LEVELS], entry_best[MAX_LEVELS];
    Entry *winner = NULL;

    if (!db) return NULL;

    names = g_strsplit(name, ".", -1);
    classes = g_strsplit(klass, ".", -1);
    n = g_strv_length(names);
    if (n == 0 || n > MAX_LEVELS || (gint)g_strv_length(classes) != n) {
        g_strfreev(names);
        g_strfreev(classes);
        return NULL;
    }

    for (i = 0; i < n; ++i) best[i] = -1;
    for (i = 0; i < (gint)db->entries->len; ++i) {
        Entry *e = g_ptr_array_index(db->entries, i);
        gint k;
        for (k = 0; k < n; ++k) entry_best[k] = -1;
        if (match(e, 0, names, classes, 0, n, score, entry_best) &&
            better(entry_best, best, n))
        {
            memcpy(best, entry_best, n * sizeof(gint));
            winner = e;
        }
    }

    g_strfreev(names);
    g_strfreev(classes);
    return winner ? winner->value : NULL;
}
