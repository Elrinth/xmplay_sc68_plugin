/*
 * sc68_player — thin native wrapper around official libsc68.
 * Seek is sc68_cntl(SC68_SET_POS) / sc68_xmp_seek_ms (restart+skip+snaps).
 */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "sc68_player.h"

#include <sc68/sc68.h>
#include <sc68/file68.h>
#include <sc68/file68_ice.h>
#include <unice68.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>

#ifndef SC68_PCM_S16
# define SC68_PCM_S16 1
#endif

/* Implemented in vendored api68.c (xmp-sc68 seek patch). */
int  sc68_xmp_seek_ms(sc68_t *sc68, int dest_ms);
void sc68_xmp_snap_reset(sc68_t *sc68);

#define DEFAULT_LEN_SEC 180.0
#define MAX_FILE (16u * 1024u * 1024u)

static int g_inited;

struct sc68_player {
  sc68_t *sc68;
  unsigned char *owned;
  size_t owned_len;
  int rate;
  int tracks;
  int track; /* 0-based */
  int is_sc68;
  int ended;
  float gain_db;
  float gain_lin;
  int boost;
  int boost_applied;
  double pos_sec;
  float lens[SC68_PLAYER_MAX_TRACKS];
  float total_len;
  char title[SC68_PLAYER_STR];
  char artist[SC68_PLAYER_STR];
  char album[SC68_PLAYER_STR];
  char year[32];
  char format[64];
  char hw[32];
  char replay[64];
  char comment[SC68_PLAYER_STR];
  char track_title[SC68_PLAYER_MAX_TRACKS][128];
};

static void bounded(char *dst, size_t cap, const char *src)
{
  size_t n;
  if (!dst || cap == 0)
    return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  n = strlen(src);
  if (n >= cap)
    n = cap - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

static int looks_sc68(const unsigned char *p, size_t n)
{
  static const char idv1[] = "SC68 Music-file";
  if (!p || n < 4)
    return 0;
  if (n >= 7 && memcmp(p, "SC68 M2", 7) == 0)
    return 1;
  if (n >= 4 && memcmp(p, "SC68", 4) == 0)
    return 1;
  if (n >= sizeof(idv1) - 1 && memcmp(p, idv1, sizeof(idv1) - 1) == 0)
    return 1;
  return 0;
}

static int looks_ice(const unsigned char *p, size_t n)
{
  if (!p || n < 4)
    return 0;
  if (p[0] == 'I' && (p[1] == 'C' || p[1] == 'c') &&
      (p[2] == 'E' || p[2] == 'e') && p[3] == '!')
    return 1;
  if (n >= 12 && unice68_depacked_size(p, 0) > 0)
    return 1;
  return 0;
}

static int looks_sndh(const unsigned char *p, size_t n)
{
  size_t i, max;
  if (!p || n < 16)
    return 0;
  max = n > 2048 ? 2048 : n;
  for (i = 0; i + 4 <= max; ++i) {
    if (p[i] == 'S' && p[i + 1] == 'N' && p[i + 2] == 'D' && p[i + 3] == 'H')
      return 1;
  }
  return 0;
}

int sc68_player_probe(const unsigned char *data, size_t len)
{
  if (!data || len < 4)
    return 0;
  if (looks_sc68(data, len))
    return 1;
  if (looks_ice(data, len))
    return 1;
  if (looks_sndh(data, len))
    return 1;
  return 0;
}

static void silent_msg(void)
{
}

int sc68_player_global_init(void)
{
  sc68_init_t init;
  if (g_inited)
    return 0;
  memset(&init, 0, sizeof init);
  init.msg_handler = (sc68_msg_t)silent_msg;
  init.flags.no_load_config = 1;
  init.flags.no_save_config = 1;
  if (sc68_init(&init) != 0)
    return -1;
  g_inited = 1;
  return 0;
}

void sc68_player_global_shutdown(void)
{
  if (!g_inited)
    return;
  sc68_shutdown();
  g_inited = 0;
}

static float track_len_sec(const sc68_music_info_t *inf)
{
  double s;
  if (!inf)
    return (float)DEFAULT_LEN_SEC;
  s = (double)inf->trk.time_ms / 1000.0;
  if (!(s > 0.05) || s > 86400.0)
    s = DEFAULT_LEN_SEC;
  return (float)s;
}

static int fill_info_from_disk(sc68_disk_t disk, sc68_info *out)
{
  sc68_music_info_t inf;
  int i, n, shared;
  char first_title[128];

  if (!disk || !out)
    return -1;
  memset(out, 0, sizeof *out);
  memset(&inf, 0, sizeof inf);
  if (sc68_music_info(0, &inf, 1, disk) != 0)
    return -1;

  n = inf.tracks;
  if (n < 1)
    n = 1;
  if (n > SC68_PLAYER_MAX_TRACKS)
    n = SC68_PLAYER_MAX_TRACKS;
  out->tracks = n;

  bounded(out->album, sizeof out->album, inf.album);
  bounded(out->title, sizeof out->title, inf.title && inf.title[0] ? inf.title : inf.album);
  bounded(out->artist, sizeof out->artist, inf.artist);
  bounded(out->year, sizeof out->year, inf.year);
  bounded(out->format, sizeof out->format, inf.format && inf.format[0] ? inf.format : inf.genre);
  bounded(out->hw, sizeof out->hw, inf.trk.hw);
  bounded(out->replay, sizeof out->replay, inf.replay);
  {
    char *cmt = sc68_tag(0, "comment", 0, disk);
    if (cmt) {
      bounded(out->comment, sizeof out->comment, cmt);
      free(cmt);
    }
  }

  out->is_sc68 = 1;
  if (inf.format && (strstr(inf.format, "SNDH") || strstr(inf.format, "sndh")))
    out->is_sc68 = 0;
  if (inf.genre && (strstr(inf.genre, "SNDH") || strstr(inf.genre, "sndh")))
    out->is_sc68 = 0;

  first_title[0] = '\0';
  shared = 1;
  out->total_len = 0.0f;
  for (i = 0; i < n; ++i) {
    sc68_music_info_t t;
    memset(&t, 0, sizeof t);
    if (sc68_music_info(0, &t, i + 1, disk) != 0) {
      out->len[i] = (float)DEFAULT_LEN_SEC;
      bounded(out->track_title[i], sizeof out->track_title[i], out->title);
    } else {
      out->len[i] = track_len_sec(&t);
      bounded(out->track_title[i], sizeof out->track_title[i],
              t.title && t.title[0] ? t.title : out->title);
    }
    out->total_len += out->len[i];
    if (i == 0)
      bounded(first_title, sizeof first_title, out->track_title[i]);
    else if (strcmp(first_title, out->track_title[i]) != 0)
      shared = 0;
  }
  out->shared_tags = shared;
  return 0;
}

int sc68_player_info_mem(const unsigned char *data, size_t len, sc68_info *out)
{
  sc68_disk_t disk;
  int rc;

  if (!data || len < 4 || len > MAX_FILE || !out)
    return -1;
  if (sc68_player_global_init() != 0)
    return -1;
  disk = sc68_disk_load_mem(data, (int)len);
  if (!disk)
    return -1;
  rc = fill_info_from_disk(disk, out);
  sc68_disk_free(disk);
  return rc;
}

static void refresh_meta(sc68_player *p)
{
  sc68_music_info_t inf;
  if (!p || !p->sc68)
    return;
  memset(&inf, 0, sizeof inf);
  if (sc68_music_info(p->sc68, &inf, p->track + 1, 0) != 0)
    return;
  bounded(p->album, sizeof p->album, inf.album);
  bounded(p->title, sizeof p->title, inf.title && inf.title[0] ? inf.title : inf.album);
  bounded(p->artist, sizeof p->artist, inf.artist);
  bounded(p->year, sizeof p->year, inf.year);
  bounded(p->format, sizeof p->format, inf.format && inf.format[0] ? inf.format : inf.genre);
  bounded(p->hw, sizeof p->hw, inf.trk.hw);
  bounded(p->replay, sizeof p->replay, inf.replay);
  if (p->track >= 0 && p->track < SC68_PLAYER_MAX_TRACKS)
    bounded(p->track_title[p->track], sizeof p->track_title[p->track], p->title);
}

static void apply_gain(sc68_player *p)
{
  float db = p->gain_db;
  if (p->boost && p->boost_applied)
    db += 6.0f;
  if (db < -24.0f)
    db = -24.0f;
  if (db > 24.0f)
    db = 24.0f;
  p->gain_lin = powf(10.0f, db / 20.0f) * (1.0f / 32768.0f);
}

static int start_track(sc68_player *p, int track0)
{
  int loop = 1;
  if (!p || !p->sc68)
    return -1;
  if (track0 < 0)
    track0 = 0;
  if (track0 >= p->tracks)
    track0 = p->tracks - 1;
  if (sc68_play(p->sc68, track0 + 1, loop) < 0)
    return -1;
  /* Apply posted track change so the first Process emits audio. */
  sc68_process(p->sc68, 0, 0);
  p->track = track0;
  p->pos_sec = 0.0;
  p->ended = 0;
  p->boost_applied = 0;
  refresh_meta(p);
  apply_gain(p);
  return 0;
}

sc68_player *sc68_player_open(const unsigned char *data, size_t len, int rate)
{
  sc68_player *p;
  sc68_create_t cr;
  sc68_info info;
  sc68_disk_t disk;

  if (!data || len < 4 || len > MAX_FILE)
    return 0;
  if (sc68_player_global_init() != 0)
    return 0;
  if (rate < 8000 || rate > 96000)
    rate = 44100;

  disk = sc68_disk_load_mem(data, (int)len);
  if (!disk)
    return 0;
  memset(&info, 0, sizeof info);
  if (fill_info_from_disk(disk, &info) != 0) {
    sc68_disk_free(disk);
    return 0;
  }

  p = (sc68_player *)calloc(1, sizeof *p);
  if (!p) {
    sc68_disk_free(disk);
    return 0;
  }
  p->owned = (unsigned char *)malloc(len);
  if (!p->owned) {
    free(p);
    sc68_disk_free(disk);
    return 0;
  }
  memcpy(p->owned, data, len);
  p->owned_len = len;
  p->rate = rate;
  p->tracks = info.tracks;
  p->is_sc68 = info.is_sc68;
  p->total_len = info.total_len;
  memcpy(p->lens, info.len, sizeof p->lens);
  memcpy(p->track_title, info.track_title, sizeof p->track_title);
  bounded(p->title, sizeof p->title, info.title);
  bounded(p->artist, sizeof p->artist, info.artist);
  bounded(p->album, sizeof p->album, info.album);
  bounded(p->year, sizeof p->year, info.year);
  bounded(p->format, sizeof p->format, info.format);
  bounded(p->hw, sizeof p->hw, info.hw);
  bounded(p->replay, sizeof p->replay, info.replay);
  bounded(p->comment, sizeof p->comment, info.comment);
  p->gain_db = 0.0f;
  apply_gain(p);

  memset(&cr, 0, sizeof cr);
  cr.sampling_rate = (unsigned)rate;
  cr.name = "xmp-sc68";
  p->sc68 = sc68_create(&cr);
  if (!p->sc68) {
    sc68_disk_free(disk);
    sc68_player_close(p);
    return 0;
  }
  /* sc68_open does not free the disk on failure only if we pass ownership
   * incorrectly. load_disk with free_on_close=0: we free disk ourselves
   * only if open fails. On success sc68 does NOT free it (tobe3=0).
   * So we must either let sc68 own it or keep it. Use sc68_load_mem instead
   * so the instance owns a fresh load. */
  sc68_disk_free(disk);
  if (sc68_load_mem(p->sc68, p->owned, (int)p->owned_len) != 0) {
    sc68_player_close(p);
    return 0;
  }
  if (start_track(p, 0) != 0) {
    sc68_player_close(p);
    return 0;
  }
  return p;
}

void sc68_player_close(sc68_player *p)
{
  if (!p)
    return;
  if (p->sc68) {
    sc68_xmp_snap_reset(p->sc68);
    sc68_destroy(p->sc68);
    p->sc68 = 0;
  }
  free(p->owned);
  free(p);
}

int sc68_player_tracks(const sc68_player *p)
{
  return p && p->tracks > 0 ? p->tracks : 0;
}

int sc68_player_track(const sc68_player *p)
{
  return p ? p->track : 0;
}

int sc68_player_set_track(sc68_player *p, int track0)
{
  if (!p || !p->sc68)
    return -1;
  if (track0 < 0 || track0 >= p->tracks)
    return -1;
  return start_track(p, track0);
}

int sc68_player_set_rate(sc68_player *p, int rate)
{
  int got;
  if (!p || !p->sc68)
    return -1;
  if (rate < 8000 || rate > 96000)
    return p->rate;
  got = sc68_cntl(p->sc68, SC68_SET_SPR, rate);
  if (got > 0)
    p->rate = got;
  return p->rate;
}

int sc68_player_rate(const sc68_player *p)
{
  return p ? p->rate : 0;
}

double sc68_player_track_len(const sc68_player *p, int track0)
{
  if (!p || track0 < 0 || track0 >= p->tracks)
    return 0.0;
  return p->lens[track0];
}

double sc68_player_total_len(const sc68_player *p)
{
  return p ? p->total_len : 0.0;
}

double sc68_player_tell(const sc68_player *p)
{
  int ms;
  if (!p || !p->sc68)
    return 0.0;
  ms = sc68_cntl(p->sc68, SC68_GET_POS);
  if (ms >= 0)
    return (double)ms / 1000.0;
  return p->pos_sec;
}

double sc68_player_seek(sc68_player *p, double seconds)
{
  int dest, got;
  if (!p || !p->sc68)
    return -1.0;
  if (seconds < 0.0)
    seconds = 0.0;
  if (p->lens[p->track] > 0.0 && seconds > p->lens[p->track])
    seconds = p->lens[p->track];
  dest = (int)(seconds * 1000.0 + 0.5);
  got = sc68_xmp_seek_ms(p->sc68, dest);
  if (got < 0)
    got = sc68_cntl(p->sc68, SC68_SET_POS, dest);
  if (got < 0)
    return -1.0;
  p->pos_sec = (double)got / 1000.0;
  p->ended = 0;
  return p->pos_sec;
}

int sc68_player_process(sc68_player *p, float *stereo, int frames)
{
  int n, code, i;
  float scale, peak;
  uint32_t *pcm;
  size_t bytes;

  if (!p || !p->sc68 || !stereo || frames <= 0 || p->ended)
    return 0;
  if (frames > 8192)
    frames = 8192;

  bytes = (size_t)frames * sizeof(uint32_t);
  pcm = (uint32_t *)malloc(bytes);
  if (!pcm)
    return 0;
  n = frames;
  code = sc68_process(p->sc68, pcm, &n);
  if (code == SC68_ERROR || n <= 0) {
    free(pcm);
    p->ended = 1;
    return 0;
  }
  if (code & SC68_END)
    p->ended = 1;

  scale = p->gain_lin;
  peak = 0.0f;
  for (i = 0; i < n; ++i) {
    int16_t l = (int16_t)(pcm[i] & 0xFFFFu);
    int16_t r = (int16_t)(pcm[i] >> 16);
    float fl = (float)l * scale;
    float fr = (float)r * scale;
    stereo[i * 2]     = fl;
    stereo[i * 2 + 1] = fr;
    if (fl < 0)
      fl = -fl;
    if (fr < 0)
      fr = -fr;
    if (fl > peak)
      peak = fl;
    if (fr > peak)
      peak = fr;
  }
  free(pcm);

  p->pos_sec += (double)n / (double)p->rate;

  if (p->boost && !p->boost_applied && p->pos_sec >= 0.25) {
    if (peak > 0.0f && peak < 0.08f) {
      p->boost_applied = 1;
      apply_gain(p);
    } else {
      p->boost_applied = -1; /* decided: no boost */
    }
  }
  return n;
}

const char *sc68_player_title(const sc68_player *p)
{
  if (!p)
    return "";
  if (p->track >= 0 && p->track < p->tracks && p->track_title[p->track][0])
    return p->track_title[p->track];
  return p->title;
}
const char *sc68_player_artist(const sc68_player *p)  { return p ? p->artist : ""; }
const char *sc68_player_album(const sc68_player *p)   { return p ? p->album : ""; }
const char *sc68_player_year(const sc68_player *p)    { return p ? p->year : ""; }
const char *sc68_player_format(const sc68_player *p)  { return p ? p->format : ""; }
const char *sc68_player_hw(const sc68_player *p)      { return p ? p->hw : ""; }
const char *sc68_player_replay(const sc68_player *p)  { return p ? p->replay : ""; }
const char *sc68_player_comment(const sc68_player *p) { return p ? p->comment : ""; }
int sc68_player_is_sc68(const sc68_player *p)         { return p ? p->is_sc68 : 0; }

void sc68_player_set_gain_db(sc68_player *p, float db)
{
  if (!p)
    return;
  if (db < -24.0f) db = -24.0f;
  if (db > 24.0f) db = 24.0f;
  p->gain_db = db;
  apply_gain(p);
}
float sc68_player_gain_db(const sc68_player *p) { return p ? p->gain_db : 0.0f; }
void sc68_player_set_boost(sc68_player *p, int on)
{
  if (!p)
    return;
  p->boost = on ? 1 : 0;
  p->boost_applied = 0;
  apply_gain(p);
}
int sc68_player_boost(const sc68_player *p) { return p ? p->boost : 0; }
