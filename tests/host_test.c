// Host tests for the hardware-independent code. Run through tests/run.sh.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#include "config.h"
#include "davxml.h"
#include "mp3s.h"
#include "ring.h"
#include "search.h"
#include "util.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void test_util(void)
{
    char b[64];
    CHECK(nat_cmp("Folge 2", "Folge 10") < 0);
    CHECK(nat_cmp("CD 02", "CD 2") != 0);
    CHECK(nat_cmp("abc", "ABD") < 0);
    CHECK(nat_cmp("\xC3\x84pfel", "Birne") < 0);
    CHECK(nat_cmp("Ma\xCC\x88rchen", "Marz") < 0);
    CHECK(nat_cmp("01-02 x", "01-10 x") < 0);
    CHECK(nat_cmp("a", "a") == 0);

    url_encode("/H\xC3\xB6r spiele&a", b, sizeof(b));
    CHECK(strcmp(b, "%2FH%C3%B6r%20spiele%26a") == 0);
    CHECK(url_encode("abcd", b, 4) == 0);
    CHECK(url_encode("abc", b, 4) == 3);

    display_name("Ba\xCC\x88r.mp3", b, sizeof(b), true);
    CHECK(strcmp(b, "B\xC3\xA4r") == 0);
    display_name("Kuchenst\xEF\xBF\xBC" "ck", b, sizeof(b), false);  // U+FFFC: Latin-1 ü
    CHECK(strcmp(b, "Kuchenst\xC3\xBC" "ck") == 0);
    display_name("M\xEF\xBE\x8A" "rchen", b, sizeof(b), false);  // U+FF8A: MacRoman ä
    CHECK(strcmp(b, "M\xC3\xA4rchen") == 0);
    display_name("Dr. Brumm", b, sizeof(b), false);
    CHECK(strcmp(b, "Dr. Brumm") == 0);
    display_name("abcd\xC3\xB6xyz", b, 7, false);
    CHECK(strcmp(b, "abc") == 0 || strcmp(b, "abcd") == 0);

    CHECK(json_string_array("/a \"b\"\\c", b, sizeof(b)) && strcmp(b, "[\"/a \\\"b\\\"\\\\c\"]") == 0);
    CHECK(!json_string_array("abcdef", b, 8));
    CHECK(is_audio_name("x.MP3") && !is_audio_name("x.mp2") && !is_audio_name(".x.mp3") && !is_audio_name("x"));
    CHECK(fnv1a("a") == 0xE40C292C);
}

static void test_ring(void)
{
    uint8_t mem[10], out[10], in[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    ring_t r;
    ring_init(&r, mem, sizeof(mem));
    CHECK(ring_write(&r, in, 7) == 7);
    CHECK(ring_read(&r, out, 5) == 5 && out[4] == 4);
    CHECK(ring_write(&r, in, 10) == 8);  // wraps and stops when full
    CHECK(ring_free(&r) == 0);
    CHECK(ring_read(&r, out, 10) == 10);
    CHECK(out[0] == 5 && out[1] == 6 && out[2] == 0 && out[9] == 7);
    CHECK(ring_read(&r, out, 1) == 0);
}

// Decodes a file from offset in odd-sized slices and writes mono-preserving raw PCM.
static int decode(const char *path, long offset, const char *pcm_path)
{
    FILE *in = fopen(path, "rb"), *out = fopen(pcm_path, "wb");
    assert(in && out);
    fseek(in, offset, SEEK_SET);
    static mp3s_t s;
    mp3s_init(&s);
    mp3s_reset(&s, offset == 0);
    bool eof = false;
    long frames = 0, bytes = 0, seed = 12345;
    int rate = 0, ch = 0;
    for (;;) {
        uint8_t *dst;
        size_t space = mp3s_space(&s, &dst);
        seed = seed * 1103515245 + 12345;
        size_t want = 1 + (size_t)((seed >> 16) & 0x7FF);  // arrival sizes as irregular as a network
        if (want > space) want = space;
        if (want && !eof) {
            size_t n = fread(dst, 1, want, in);
            mp3s_commit(&s, n);
            if (n < want) eof = true;
        }
        mp3s_frame_t f;
        mp3s_result_t r = mp3s_next(&s, eof, &f);
        if (r == MP3S_END) break;
        if (r == MP3S_NEED_DATA) continue;
        CHECK(f.channels > 0 && f.samples > 0 && f.sample_rate > 0);
        fwrite(s.pcm, 2, f.samples, out);
        frames++;
        bytes += f.bytes;
        rate = f.sample_rate;
        ch = f.channels;
    }
    long end = ftell(in);
    printf("%s offset=%ld frames=%ld xing=%u rate=%d ch=%d consumed=%llu file_end=%ld\n", path, offset, frames, s.file_frames, rate, ch,
           (unsigned long long)s.consumed, end);
    if (offset == 0 && s.file_frames && labs((long)s.file_frames - frames) > 2) failures++;
    if (offset == 0 && s.consumed + 128 < (unsigned long long)end) failures++;  // whole file used, bar an ID3v1 tag
    mp3s_deinit(&s);
    fclose(in);
    fclose(out);
    return (int)frames;
}

static void test_search(void)
{
    char b[64];
    search_normalize("Gr\xC3\xBCn, Bl\xC3\xA4tter!", b, sizeof(b));
    CHECK(strcmp(b, "grun blatter") == 0);
    search_normalize("Gruen  STRASSE", b, sizeof(b));
    CHECK(strcmp(b, "grun strasse") == 0);
    search_normalize("Stra\xC3\x9F" "e", b, sizeof(b));
    CHECK(strcmp(b, "strasse") == 0);

    const char *text = "S/R/Bibi Blocksberg\nA/R/Bibi Blocksberg/Die Hundebabys\nT/R/Bibi Blocksberg/Die Hundebabys/01 Titelmelodie.mp3\t1234\n"
                       "S/R/Kokosnuss\nA/R/Kokosnuss/Der kleine Drache Kokosnuss\nT/R/Sams/CD1/02 Das Sams legte den Kuchen.mp3\n";
    search_index_t *idx = search_index_parse(strdup(text), strlen(text));
    search_hit_t h[8];
    CHECK(search_index_count(idx) == 6);
    CHECK(search_query(idx, "hundebabis", h, 8) >= 1 && strstr(h[0].path, "Hundebabys") && h[0].kind == 'A');
    CHECK(search_query(idx, "kokosnus", h, 8) >= 1 && strstr(h[0].path, "Kokosnuss"));
    CHECK(search_query(idx, "bibi hunde", h, 8) >= 1 && strstr(h[0].path, "Hundebabys"));
    CHECK(search_query(idx, "kuchen", h, 8) == 1 && h[0].kind == 'T');
    CHECK(search_query(idx, "titelmelodie", h, 8) == 1);
    CHECK(search_query(idx, "xyzzy", h, 8) == 0);
    CHECK(search_query(idx, "hnudebabys", h, 8) >= 1 && strstr(h[0].path, "Hundebabys"));  // swapped letters
    CHECK(search_query(idx, "titelmelodei", h, 8) == 1);  // typo near the end of a long word
    CHECK(search_query(idx, "okosnuss", h, 8) >= 1 && strstr(h[0].path, "Kokosnuss"));  // substring
    CHECK(search_query(idx, "lokosnuss", h, 8) == 0);  // first letter wrong: no typo match
    CHECK(search_query(idx, "bibi", h, 8) == 2);  // series and its album, not every track below them

    search_child_t kids[4];
    CHECK(search_index_children(idx, "/R", kids, 4) == 2);  // root: Bibi Blocksberg, Kokosnuss (Sams has no folder line)
    CHECK(search_index_children(idx, "/R/Bibi Blocksberg/Die Hundebabys", kids, 4) == 1 && kids[0].kind == 'T' &&
          kids[0].size == 1234 && strcmp(kids[0].name, "01 Titelmelodie.mp3") == 0);
    CHECK(search_index_children(idx, "/R/Kokosnuss", kids, 4) == 1);
    CHECK(search_index_children(idx, "/R/Nope", kids, 4) == -1);
    CHECK(search_query(idx, "titelmelodie", h, 8) == 1 && strstr(h[0].path, "\t") == NULL);
    search_index_free(idx);
}

static void query_file(const char *path, const char *q)
{
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = malloc(len + 1);
    fread(text, 1, len, f);
    text[len] = 0;
    fclose(f);
    search_index_t *idx = search_index_parse(text, len);
    search_hit_t h[5];
    int n = search_query(idx, q, h, 5);
    printf("%-22s", q);
    for (int i = 0; i < n; i++) {
        const char *base = strrchr(h[i].path, '/');
        printf(" | %c %.38s", h[i].kind, base + 1);
    }
    printf("\n");
    search_index_free(idx);
}

// Starts decoding at many random positions: every one must lock on and keep the file's format.
static void sweep(const char *path, int runs)
{
    FILE *in = fopen(path, "rb");
    assert(in);
    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    static mp3s_t s;
    static uint8_t buf[300000];
    mp3s_init(&s);
    srand(7);
    int rate = 0, ch = 0, worst = 0;
    for (int i = 0; i < runs; i++) {
        long off = 1 + (long)((double)rand() / RAND_MAX * (size - 400000));
        fseek(in, off, SEEK_SET);
        size_t have = fread(buf, 1, sizeof(buf), in), fed = 0;
        mp3s_reset(&s, false);
        int frames = 0;
        size_t first_at = 0;
        while (frames < 40) {
            uint8_t *dst;
            size_t space = mp3s_space(&s, &dst), n = have - fed < space ? have - fed : space;
            n = n > 700 ? 700 : n;
            memcpy(dst, buf + fed, n);
            mp3s_commit(&s, n);
            fed += n;
            mp3s_frame_t f;
            mp3s_result_t r = mp3s_next(&s, fed == have, &f);
            if (r == MP3S_END) break;
            if (r != MP3S_FRAME) continue;
            if (!frames) first_at = (size_t)s.consumed;
            frames++;
            CHECK(f.channels > 0 && f.samples > 0);
            if (!rate) { rate = f.sample_rate; ch = f.channels; }
            CHECK(f.sample_rate == rate && f.channels == ch);
        }
        CHECK(frames == 40);
        if ((int)first_at > worst) worst = (int)first_at;
    }
    printf("sweep %d positions, rate=%d ch=%d, worst lock-on after %d bytes\n", runs, rate, ch, worst);
    mp3s_deinit(&s);
    fclose(in);
}

// ---- settings block (main/config.c)

static size_t pack_config(const char *json, uint8_t *out)
{
    size_t n = strlen(json);
    uint32_t crc = (uint32_t)crc32(0, (const uint8_t *)json, n);
    memcpy(out, "FLUE", 4);
    out[4] = 1, out[5] = 0, out[6] = 0, out[7] = 0;
    for (int i = 0; i < 4; i++) out[8 + i] = (uint8_t)(n >> (8 * i)), out[12 + i] = (uint8_t)(crc >> (8 * i));
    memcpy(out + 16, json, n);
    return 16 + n;
}

static void test_config(void)
{
    static uint8_t buf[4096];
    config_t c = {0};
    size_t n = pack_config("{\"wifi_ssid\":\"Haus\",\"wifi_password\":\"geheim\",\"nas_type\":\"synology\","
                           "\"nas_url\":\"https://192.168.1.5:5001/\",\"tls_name\":\"nas.example.org\","
                           "\"root\":\"/H\u00f6rspiele/\",\"share_id\":\"abc\",\"share_password\":\"x\"}", buf);
    CHECK(config_parse(buf, n, &c));
    CHECK(c.nas_type == CFG_NAS_SYNOLOGY);
    CHECK(!strcmp(c.nas_url, "https://192.168.1.5:5001"));
    CHECK(!strcmp(c.nas_host, "192.168.1.5"));
    CHECK(!strcmp(c.tls_name, "nas.example.org"));
    CHECK(!strcmp(c.root, "/H\xC3\xB6rspiele"));
    CHECK(!c.ripper_url[0] && !c.ca_pem && !strcmp(c.lang, "de"));

    n = pack_config("{\"wifi_ssid\":\"Haus\",\"nas_type\":\"webdav\",\"nas_url\":\"https://cloud.example.org/remote.php/dav/files/kim\","
                    "\"root\":\"/Musik\",\"lang\":\"en\",\"dav_user\":\"kim\",\"dav_password\":\"pw\",\"ca_pem\":\"-----BEGIN\"}", buf);
    CHECK(config_parse(buf, n, &c));
    CHECK(c.nas_type == CFG_NAS_WEBDAV && !strcmp(c.nas_host, "cloud.example.org") && !strcmp(c.tls_name, "cloud.example.org"));
    CHECK(c.ca_pem && !strcmp(c.ca_pem, "-----BEGIN") && !strcmp(c.lang, "en"));

    buf[20] ^= 1;  // damaged
    CHECK(!config_parse(buf, n, &c) && c.error == CFG_ERR_DAMAGED);
    CHECK(!config_parse((const uint8_t *)"\xff\xff\xff\xff", 4, &c) && c.error == CFG_ERR_MISSING);
    n = pack_config("{\"wifi_ssid\":\"Haus\",\"nas_type\":\"synology\",\"nas_url\":\"https://n\",\"root\":\"/a\"}", buf);
    CHECK(!config_parse(buf, n, &c) && c.error == CFG_ERR_NO_SHARE);
    n = pack_config("{\"wifi_ssid\":\"Haus\",\"nas_type\":\"ftp\",\"nas_url\":\"x\",\"root\":\"/a\"}", buf);
    CHECK(!config_parse(buf, n, &c));
    n = pack_config("{\"nas_type\":\"webdav\",\"nas_url\":\"https://n\",\"root\":\"/a\"}", buf);
    CHECK(!config_parse(buf, n, &c) && c.error == CFG_ERR_INCOMPLETE);
}

// ---- WebDAV listings (main/davxml.c), in the shapes different servers answer with

typedef struct {
    char names[8][64];
    bool dirs[8];
    uint32_t sizes[8];
    int n;
} dav_got_t;

static void dav_cb(const char *name, bool is_dir, uint32_t size, void *ctx)
{
    dav_got_t *g = ctx;
    if (g->n >= 8) return;
    snprintf(g->names[g->n], 64, "%s", name);
    g->dirs[g->n] = is_dir;
    g->sizes[g->n++] = size;
}

static void test_davxml(void)
{
    // Apache mod_dav style: D: prefix, lp1: for live properties, relative hrefs, the folder itself first.
    const char *apache =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<D:multistatus xmlns:D=\"DAV:\" xmlns:ns0=\"DAV:\">"
        "<D:response xmlns:lp1=\"DAV:\"><D:href>/dav/H%c3%b6rspiele/Bibi/</D:href><D:propstat><D:prop>"
        "<lp1:resourcetype><D:collection/></lp1:resourcetype></D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response>"
        "<D:response xmlns:lp1=\"DAV:\"><D:href>/dav/H%c3%b6rspiele/Bibi/Folge%202/</D:href><D:propstat><D:prop>"
        "<lp1:resourcetype><D:collection/></lp1:resourcetype></D:prop></D:propstat></D:response>"
        "<D:response xmlns:lp1=\"DAV:\"><D:href>/dav/H%c3%b6rspiele/Bibi/01%20-%20Intro.mp3</D:href><D:propstat><D:prop>"
        "<lp1:resourcetype/><lp1:getcontentlength>123456</lp1:getcontentlength></D:prop></D:propstat></D:response>"
        "</D:multistatus>";
    dav_got_t g = {0};
    CHECK(davxml_parse(apache, "/dav/H%C3%B6rspiele/Bibi", dav_cb, &g) == 2);
    CHECK(!strcmp(g.names[0], "Folge 2") && g.dirs[0]);
    CHECK(!strcmp(g.names[1], "01 - Intro.mp3") && !g.dirs[1] && g.sizes[1] == 123456);

    // Nextcloud style: d: prefix, size before resourcetype, an XML escape in a name.
    const char *nextcloud =
        "<?xml version=\"1.0\"?><d:multistatus xmlns:d=\"DAV:\" xmlns:oc=\"http://owncloud.org/ns\">"
        "<d:response><d:href>/remote.php/dav/files/kim/Musik/</d:href><d:propstat><d:prop><d:resourcetype><d:collection/></d:resourcetype>"
        "</d:prop></d:propstat></d:response>"
        "<d:response><d:href>/remote.php/dav/files/kim/Musik/Tom%20&amp;%20Jerry.mp3</d:href><d:propstat><d:prop>"
        "<d:getcontentlength>42</d:getcontentlength><d:resourcetype/></d:prop></d:propstat></d:response>"
        "</d:multistatus>";
    memset(&g, 0, sizeof(g));
    CHECK(davxml_parse(nextcloud, "/remote.php/dav/files/kim/Musik", dav_cb, &g) == 1);
    CHECK(!strcmp(g.names[0], "Tom & Jerry.mp3") && g.sizes[0] == 42 && !g.dirs[0]);

    // Absolute hrefs and no prefix at all (some embedded servers), an empty propstat for a missing size.
    const char *plain =
        "<multistatus xmlns=\"DAV:\"><response><href>https://nas:5006/audio/</href><propstat><prop><resourcetype><collection/>"
        "</resourcetype></prop></propstat></response><response><href>https://nas:5006/audio/%C3%84rger.mp3</href><propstat><prop>"
        "<resourcetype></resourcetype><getcontentlength>7</getcontentlength></prop></propstat></response></multistatus>";
    memset(&g, 0, sizeof(g));
    CHECK(davxml_parse(plain, "/audio/", dav_cb, &g) == 1);
    CHECK(!strcmp(g.names[0], "\xC3\x84rger.mp3") && g.sizes[0] == 7);

    CHECK(davxml_parse("<html>Login</html>", "/", dav_cb, &g) == -1);

    char e[32];
    url_encode_path("/H\xC3\xB6r spiele/a&b", e, sizeof(e));
    CHECK(!strcmp(e, "/H%C3%B6r%20spiele/a%26b"));
}

int main(int argc, char **argv)
{
    test_util();
    test_config();
    test_davxml();
    test_ring();
    test_search();
    if (argc >= 4 && !strcmp(argv[2], "query"))
        for (int i = 3; i < argc; i++) query_file(argv[1], argv[i]);
    else if (argc == 4 && !strcmp(argv[2], "sweep")) sweep(argv[1], atoi(argv[3]));
    else if (argc == 4) decode(argv[1], atol(argv[2]), argv[3]);
    printf(failures ? "FAILED: %d\n" : "unit checks ok\n", failures);
    return failures != 0;
}
