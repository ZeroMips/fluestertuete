// The CD ripping station (cd-ripper project): status and a few actions over its JSON API (cd-web).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define RIPPER_TEXT_MAX 128

typedef enum { RIP_IDLE, RIP_READING, RIP_RIPPING, RIP_DELIVERING, RIP_DONE, RIP_ERROR, RIP_UNKNOWN } rip_state_t;

typedef enum { RIP_CLS_KIDS, RIP_CLS_SPOKEN, RIP_CLS_MUSIC, RIP_CLS_UNSORTED, RIP_CLS_COUNT } rip_cls_t;
#define RIP_CLS_NONE (-1)

typedef struct {
    bool online;    // last poll answered
    bool seen;      // the ripper answered at least once since boot
    rip_state_t state;
    int progress;   // percent while ripping or delivering, else -1
    char artist[RIPPER_TEXT_MAX], album[RIPPER_TEXT_MAX], cls[32], message[RIPPER_TEXT_MAX];
    // The type of the disc being ripped: what was detected, and what it ends up as (a correction wins).
    int detected, chosen;  // rip_cls_t or RIP_CLS_NONE
    uint32_t choices;      // bit per rip_cls_t the formats being ripped allow switching to
    bool release_pending;  // another MusicBrainz release was chosen on the web page
    bool already;          // the disc was skipped because it is already in the library
    bool force;            // the next disc is ripped even if it is already there
    int waiting;           // finished rips waiting for the NAS
    int failed;            // failed rips kept on the ripper
    char last[2 * RIPPER_TEXT_MAX + 8];  // newest finished album, "Artist – Album"
    uint32_t finished;  // albums finished since boot (counts up while the T-Deck watches)
    bool last_kids;     // the newest finished album is a Kinderhörspiel
} ripper_status_t;

bool ripper_available(void);  // a ripper address is set up
void ripper_start(void);
void ripper_set_watching(bool on);  // polls every 2 s while the ripper screen is shown, else every 20 s
void ripper_get(ripper_status_t *out);
// All run in the background; the result arrives as ripper_take_reply().
void ripper_correct(rip_cls_t cls);  // type of the disc being ripped; the detected type undoes a correction
void ripper_set_force(bool on);
void ripper_eject(void);
// A reply to the last action, once: false when there is none.
bool ripper_take_reply(char *text, int size);
const char *ripper_state_name(rip_state_t s);
const char *ripper_cls_name(rip_cls_t c);
