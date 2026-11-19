#include "davxml.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define PATH_MAX_LEN 1024

// Finds the next element named local (any prefix) at or after p. *end gets the position after its start tag,
// *empty whether it is self-closing. Returns the '<' of the tag or NULL.
static const char *find_tag(const char *p, const char *limit, const char *local, const char **end, bool *empty)
{
    size_t n = strlen(local);
    while (p && p < limit && (p = memchr(p, '<', limit - p))) {
        const char *name = p + 1;
        if (*name == '/' || *name == '?' || *name == '!') {
            p++;
            continue;
        }
        const char *q = name;
        while (q < limit && !isspace((unsigned char)*q) && *q != '>' && *q != '/') q++;
        const char *colon = memchr(name, ':', q - name);
        const char *base = colon ? colon + 1 : name;
        if ((size_t)(q - base) == n && strncasecmp(base, local, n) == 0) {
            const char *gt = memchr(q, '>', limit - q);
            if (!gt) return NULL;
            *empty = gt > q && gt[-1] == '/';
            *end = gt + 1;
            return p;
        }
        p = q;
    }
    return NULL;
}

// Position of the closing tag </prefix:local> after p, or limit.
static const char *find_close(const char *p, const char *limit, const char *local)
{
    size_t n = strlen(local);
    while (p < limit && (p = memchr(p, '<', limit - p))) {
        if (p[1] == '/') {
            const char *name = p + 2, *q = name;
            while (q < limit && *q != '>' && !isspace((unsigned char)*q)) q++;
            const char *colon = memchr(name, ':', q - name);
            const char *base = colon ? colon + 1 : name;
            if ((size_t)(q - base) == n && strncasecmp(base, local, n) == 0) return p;
        }
        p++;
    }
    return limit;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = tolower(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

void davxml_unescape(char *s, bool xml)
{
    char *w = s;
    for (char *r = s; *r;) {
        if (*r == '%' && hexval(r[1]) >= 0 && hexval(r[2]) >= 0) {
            *w++ = (char)(hexval(r[1]) * 16 + hexval(r[2]));
            r += 3;
        } else if (xml && *r == '&') {
            static const struct {
                const char *ent;
                char c;
            } ents[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
            size_t i;
            for (i = 0; i < sizeof(ents) / sizeof(ents[0]); i++) {
                size_t n = strlen(ents[i].ent);
                if (strncmp(r, ents[i].ent, n) == 0) {
                    *w++ = ents[i].c;
                    r += n;
                    break;
                }
            }
            if (i == sizeof(ents) / sizeof(ents[0])) {
                if (r[1] == '#') {  // numeric, ASCII range is enough for hrefs
                    long v = r[2] == 'x' || r[2] == 'X' ? strtol(r + 3, NULL, 16) : strtol(r + 2, NULL, 10);
                    char *semi = strchr(r, ';');
                    if (semi && v > 0 && v < 128) {
                        *w++ = (char)v;
                        r = semi + 1;
                        continue;
                    }
                }
                *w++ = *r++;
            }
        } else {
            *w++ = *r++;
        }
    }
    *w = 0;
}

// href -> decoded path without scheme, host and trailing slash.
static void href_path(const char *start, const char *end, char *out, size_t size)
{
    while (start < end && isspace((unsigned char)*start)) start++;
    while (end > start && isspace((unsigned char)end[-1])) end--;
    size_t n = (size_t)(end - start) < size - 1 ? (size_t)(end - start) : size - 1;
    memcpy(out, start, n);
    out[n] = 0;
    davxml_unescape(out, true);
    char *scheme = strstr(out, "://");
    if (scheme) {
        char *slash = strchr(scheme + 3, '/');
        memmove(out, slash ? slash : "/", strlen(slash ? slash : "/") + 1);
    }
    size_t len = strlen(out);
    while (len > 1 && out[len - 1] == '/') out[--len] = 0;
}

int davxml_parse(const char *xml, const char *self_path, davxml_entry_cb cb, void *ctx)
{
    const char *limit = xml + strlen(xml), *end;
    bool empty;
    if (!find_tag(xml, limit, "multistatus", &end, &empty)) return -1;
    char self[PATH_MAX_LEN];
    href_path(self_path, self_path + strlen(self_path), self, sizeof(self));

    char *path = malloc(PATH_MAX_LEN);
    if (!path) return -1;
    int count = 0;
    const char *p = xml;
    while ((p = find_tag(p, limit, "response", &end, &empty))) {
        const char *rend = find_close(end, limit, "response");
        const char *h = find_tag(end, rend, "href", &end, &empty);
        if (!h || empty) {
            p = rend;
            continue;
        }
        href_path(end, find_close(end, rend, "href"), path, PATH_MAX_LEN);
        const char *props = end;  // properties come in any order after the href
        p = rend;
        if (strcmp(path, self) == 0) continue;
        const char *name = strrchr(path, '/');
        name = name ? name + 1 : path;
        if (!*name) continue;

        bool is_dir = false;
        const char *rt = find_tag(props, rend, "resourcetype", &end, &empty);
        if (rt && !empty) {
            const char *rt_end = find_close(end, rend, "resourcetype");
            const char *e2;
            is_dir = find_tag(end, rt_end, "collection", &e2, &empty) != NULL;
        }
        uint32_t size = 0;
        const char *e3;
        if (!is_dir && find_tag(props, rend, "getcontentlength", &e3, &empty) && !empty)
            size = (uint32_t)strtoul(e3, NULL, 10);
        cb(name, is_dir, size, ctx);
        count++;
    }
    free(path);
    return count;
}
