// Pure helpers, also compiled by the host tests.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Natural, case-insensitive order: "Folge 2" < "Folge 10", umlauts sort with their base letter.
int nat_cmp(const char *a, const char *b);

// Percent-encodes everything but RFC 3986 unreserved characters. Returns the length, or 0 if it does not fit.
size_t url_encode(const char *in, char *out, size_t out_size);
// The same, but '/' stays: for a path inside a URL.
size_t url_encode_path(const char *in, char *out, size_t out_size);

// Text for the screen: NFD umlauts composed, optionally without the file extension.
void display_name(const char *in, char *out, size_t out_size, bool strip_ext);

// Writes ["in"] with JSON escaping. Returns false if it does not fit.
bool json_string_array(const char *in, char *out, size_t out_size);

bool is_audio_name(const char *name);
bool is_hidden_name(const char *name);
uint32_t fnv1a(const char *s);
