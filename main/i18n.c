#include "i18n.h"

#include <stddef.h>

static lang_t s_lang = LANG_DE;

// UTF-8 written out as escapes: \xC3\xA4 ä, \xC3\xB6 ö, \xC3\xBC ü, \xC3\x9F ß, \xE2\x80\xA6 …, \xE2\x80\x93 –, \xC2\xB7 ·
static const char *const T[S_COUNT][LANG_COUNT] = {
    [S_SETUP_AT] = {"Einrichten unter", "Set up at"},
    [S_FW_NO_NAS_TYPE] = {"Diese Firmware kennt den NAS-Typ nicht", "This firmware lacks the NAS type"},
    [S_CONNECT_WIFI] = {"Verbinde mit WLAN \xE2\x80\xA6", "Connecting to Wi-Fi \xE2\x80\xA6"},
    [S_WIFI_NOT_FOUND] = {"WLAN nicht gefunden \xE2\x80\xA6", "Wi-Fi not found \xE2\x80\xA6"},
    [S_CONNECT_NAS] = {"Verbinde mit dem NAS \xE2\x80\xA6", "Connecting to the NAS \xE2\x80\xA6"},
    [S_NAS_REFUSED] = {"NAS: Anmeldung abgelehnt", "NAS: login refused"},
    [S_NAS_UNREACHABLE] = {"NAS nicht erreichbar \xE2\x80\xA6", "NAS not reachable \xE2\x80\xA6"},

    [S_CFG_MISSING] = {"Noch nicht eingerichtet", "Not set up yet"},
    [S_CFG_VERSION] = {"Einstellungen aus einer anderen Version", "Settings from another version"},
    [S_CFG_DAMAGED] = {"Einstellungen besch\xC3\xA4" "digt", "Settings damaged"},
    [S_CFG_UNREADABLE] = {"Einstellungen nicht lesbar", "Settings not readable"},
    [S_CFG_INCOMPLETE] = {"Einstellungen unvollst\xC3\xA4ndig", "Settings incomplete"},
    [S_CFG_NO_SHARE] = {"Freigabelink fehlt", "Share link missing"},
    [S_CFG_NAS_TYPE] = {"Unbekannter NAS-Typ", "Unknown NAS type"},
    [S_CFG_ROOT] = {"Ordner muss mit / beginnen", "Folder must start with /"},

    [S_LIBRARY] = {"H\xC3\xB6rspiele", "Stories"},
    [S_CONTINUE] = {"Weiterh\xC3\xB6ren: %s", "Continue: %s"},
    [S_PLAY_ALL] = {"Alle Titel hier abspielen", "Play all tracks here"},
    [S_RESCAN] = {"Bibliothek neu einlesen", "Rescan library"},
    [S_READING] = {"Wird eingelesen \xE2\x80\xA6", "Reading \xE2\x80\xA6"},
    [S_READING_N] = {"Wird eingelesen \xE2\x80\xA6 %d Ordner", "Reading \xE2\x80\xA6 %d folders"},
    [S_LIB_UPDATED] = {"Bibliothek aktualisiert", "Library updated"},
    [S_NOTHING_NEW] = {"Nichts Neues", "Nothing new"},
    [S_RESCAN_FAILED] = {"Einlesen fehlgeschlagen", "Rescan failed"},
    [S_NOTHING_TO_HEAR] = {"Hier gibt es nichts zu h\xC3\xB6ren", "Nothing to listen to here"},
    [S_INDEX_BUILDING] = {"Suchindex wird erstellt \xE2\x80\xA6 %d Ordner", "Building search index \xE2\x80\xA6 %d folders"},
    [S_INDEX_MISSING] = {"Suchindex fehlt noch", "No search index yet"},
    [S_NOTHING_FOUND] = {"Nichts gefunden", "Nothing found"},

    [S_NO_CONNECTION] = {"Keine Verbindung \xE2\x80\xA6", "No connection \xE2\x80\xA6"},
    [S_LOADING] = {"L\xC3\xA4" "dt \xE2\x80\xA6", "Loading \xE2\x80\xA6"},
    [S_FILE_UNREADABLE] = {"Datei nicht lesbar", "File not readable"},
    [S_FINISHED] = {"Zu Ende", "Finished"},
    [S_TRACK_OF] = {"Titel %d von %d", "Track %d of %d"},
    [S_TRACK_OF_BOOMBOX] = {"Titel %d von %d \xC2\xB7 Boombox", "Track %d of %d \xC2\xB7 boombox"},
    [S_PLAYS_HERE] = {"Spielt wieder hier", "Playing here again"},
    [S_PLAYS_BOOMBOX] = {"Spielt auf der Boombox", "Playing on the boombox"},
    [S_CONNECTING_BOOMBOX] = {"Verbinde mit der Boombox \xE2\x80\xA6", "Connecting to the boombox \xE2\x80\xA6"},
    [S_UNEXPECTED_ANSWER] = {"Unerwartete Antwort", "Unexpected answer"},
    [S_BB_SPEAKER_UNREACHABLE] = {"Boombox nicht erreichbar. Ist sie eingeschaltet?", "Boombox not reachable. Is it switched on?"},
    [S_BB_NO_SPEAKER] = {"Keine Boombox gekoppelt", "No boombox paired"},
    [S_BB_ABORTED] = {"Wiedergabe abgebrochen (Boombox aus?)", "Playback stopped (boombox off?)"},
    [S_BB_ALBUM_NOT_FOUND] = {"Album auf dem Pi nicht gefunden", "Album not found on the Pi"},
    [S_BB_NO_MP3S] = {"Keine MP3s im Album", "No MP3s in the album"},
    [S_BB_NOTHING_TO_PLAY] = {"Nichts zum Abspielen", "Nothing to play"},
    [S_BB_PLAYER_MISSING] = {"Auf dem Pi fehlt mpg123", "mpg123 is missing on the Pi"},

    [S_CD_RIPPER] = {"CD-Ripper", "CD ripper"},
    [S_RIPPER_UNREACHABLE] = {"CD-Ripper nicht erreichbar", "CD ripper not reachable"},
    [S_RIP_IDLE] = {"Bereit \xE2\x80\x93 CD einlegen", "Ready \xE2\x80\x93 insert a CD"},
    [S_RIP_READING] = {"Lese CD \xE2\x80\xA6", "Reading CD \xE2\x80\xA6"},
    [S_RIP_RIPPING] = {"Rippen", "Ripping"},
    [S_RIP_DELIVERING] = {"Kopiere aufs NAS", "Copying to the NAS"},
    [S_RIP_DONE] = {"Fertig", "Done"},
    [S_RIP_ERROR] = {"Fehler", "Error"},
    [S_RIP_UNKNOWN] = {"Unbekannt", "Unknown"},
    [S_CLS_KIDS] = {"Kinderh\xC3\xB6rspiel", "Kids' story"},
    [S_CLS_SPOKEN] = {"H\xC3\xB6rbuch", "Audiobook"},
    [S_CLS_MUSIC] = {"Musik", "Music"},
    [S_CLS_UNSORTED] = {"Unsortiert", "Unsorted"},
    [S_BTN_KIDS] = {"Kinder", "Kids"},
    [S_EJECT] = {"Auswerfen", "Eject"},
    [S_RIP_ANYWAY] = {"Trotzdem rippen", "Rip anyway"},
    [S_WILL_RIP] = {"Wird gerippt", "Will be ripped"},
    [S_LAST_DONE] = {"Zuletzt fertig:", "Last finished:"},
    [S_EJECTING] = {"Werfe aus \xE2\x80\xA6", "Ejecting \xE2\x80\xA6"},
    [S_EJECTED] = {"Ausgeworfen", "Ejected"},
    [S_TYPE_ONLY_WHILE_RIPPING] = {"Den Typ kann man nur w\xC3\xA4hrend des Rippens \xC3\xA4ndern",
                                   "The type can only be changed while ripping"},
    [S_TYPE_WRONG_FORMAT] = {"Daf\xC3\xBCr wird diese CD im falschen Format gerippt", "This CD is ripped in the wrong format for that"},
    [S_NOT_REACHABLE] = {"Nicht erreichbar", "Not reachable"},
    [S_SEARCHING_RIPPER] = {"Suche CD-Ripper \xE2\x80\xA6", "Looking for the CD ripper \xE2\x80\xA6"},
    [S_PI_ON] = {"Ist der Raspberry Pi an?", "Is the Raspberry Pi on?"},
    [S_NEW_DRAMA] = {"Neues H\xC3\xB6rspiel: %s", "New story: %s"},
    [S_CD_DONE] = {"CD fertig: %s", "CD done: %s"},
    [S_TYPE_CHANGE_WHILE] = {"Typ \xC3\xA4ndern geht w\xC3\xA4hrend des Rippens", "The type can be changed while ripping"},
    [S_TYPE_CHANGED] = {"Typ ge\xC3\xA4ndert, erkannt war %s", "Type changed, detected: %s"},
    [S_RIP_WAITING] = {"%d warten aufs NAS, %d fehlgeschlagen", "%d waiting for the NAS, %d failed"},
    [S_TYPE_OF_CD] = {"Typ dieser CD:", "Type of this CD:"},
    [S_OTHER_ALBUM] = {" \xC2\xB7 anderes Album vorgemerkt", " \xC2\xB7 other album chosen"},
    [S_TYPE_SET] = {"Typ: %s", "Type: %s"},
    [S_NEXT_CD_RIPPED] = {"N\xC3\xA4" "chste CD wird auf jeden Fall gerippt", "The next CD will be ripped anyway"},
    [S_CANCELLED] = {"Abgebrochen", "Cancelled"},
    [S_ERROR_N] = {"Fehler %d", "Error %d"},
};

void i18n_set(lang_t lang) { s_lang = lang < LANG_COUNT ? lang : LANG_DE; }
lang_t i18n_lang(void) { return s_lang; }

const char *tr_lang(str_t id, lang_t lang)
{
    const char *s = id < S_COUNT ? T[id][lang < LANG_COUNT ? lang : LANG_DE] : NULL;
    return s ? s : (id < S_COUNT && T[id][LANG_DE] ? T[id][LANG_DE] : "?");
}

const char *tr(str_t id) { return tr_lang(id, s_lang); }
