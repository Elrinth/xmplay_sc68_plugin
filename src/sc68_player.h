/*
 * Host-testable sc68 player used by xmp-sc68 (native XMPlay plugin).
 * Not a Winamp wrapper.
 */
#ifndef SC68_PLAYER_H
#define SC68_PLAYER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SC68_PLAYER_MAX_TRACKS 63
#define SC68_PLAYER_STR 256

int  sc68_player_global_init(void);
void sc68_player_global_shutdown(void);

/* Fast magic probe. Accepts SC68 header, ICE!, or raw SNDH. */
int sc68_player_probe(const unsigned char *data, size_t len);

typedef struct sc68_info {
  int   tracks;
  int   is_sc68; /* 1=SC68 container, 0=SNDH */
  float len[SC68_PLAYER_MAX_TRACKS];
  float total_len;
  char  title[SC68_PLAYER_STR];
  char  artist[SC68_PLAYER_STR];
  char  album[SC68_PLAYER_STR];
  char  year[32];
  char  format[64];
  char  comment[SC68_PLAYER_STR];
  char  hw[32];
  char  replay[64];
  char  track_title[SC68_PLAYER_MAX_TRACKS][128];
  int   shared_tags; /* 1 if all tracks share the album title */
} sc68_info;

int sc68_player_info_mem(const unsigned char *data, size_t len, sc68_info *out);

typedef struct sc68_player sc68_player;

sc68_player *sc68_player_open(const unsigned char *data, size_t len, int rate);
void         sc68_player_close(sc68_player *p);

int    sc68_player_tracks(const sc68_player *p);
int    sc68_player_track(const sc68_player *p); /* 0-based */
int    sc68_player_set_track(sc68_player *p, int track0);
int    sc68_player_set_rate(sc68_player *p, int rate);
int    sc68_player_rate(const sc68_player *p);

double sc68_player_track_len(const sc68_player *p, int track0);
double sc68_player_total_len(const sc68_player *p);
double sc68_player_tell(const sc68_player *p);
double sc68_player_seek(sc68_player *p, double seconds);

int    sc68_player_process(sc68_player *p, float *stereo, int frames);

const char *sc68_player_title(const sc68_player *p);
const char *sc68_player_artist(const sc68_player *p);
const char *sc68_player_album(const sc68_player *p);
const char *sc68_player_year(const sc68_player *p);
const char *sc68_player_format(const sc68_player *p);
const char *sc68_player_hw(const sc68_player *p);
const char *sc68_player_replay(const sc68_player *p);
const char *sc68_player_comment(const sc68_player *p);
int         sc68_player_is_sc68(const sc68_player *p);

void  sc68_player_set_gain_db(sc68_player *p, float db);
float sc68_player_gain_db(const sc68_player *p);
void  sc68_player_set_boost(sc68_player *p, int on);
int   sc68_player_boost(const sc68_player *p);

#ifdef __cplusplus
}
#endif
#endif
