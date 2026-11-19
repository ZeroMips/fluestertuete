// Reads a WebDAV PROPFIND (Depth: 1) answer: one entry per child of the folder that was asked for.
// Tolerant on purpose: any namespace prefix (D:, d:, lp1:, none), absolute or relative hrefs, percent and
// XML escapes in names. Pure C, also compiled by the host tests.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef void (*davxml_entry_cb)(const char *name, bool is_dir, uint32_t size, void *ctx);

// self_path: the URL path that was asked for, percent-encoded or not; its own entry is left out.
// Returns the number of entries reported, -1 when the text is not a multistatus answer.
int davxml_parse(const char *xml, const char *self_path, davxml_entry_cb cb, void *ctx);

// "%C3%A4" -> "ä" in place; also turns XML escapes into characters when xml is true.
void davxml_unescape(char *s, bool xml);
