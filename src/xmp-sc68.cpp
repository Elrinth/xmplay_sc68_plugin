/*
 * xmp-sc68 — crash-safe native XMPlay input plugin for Atari ST / Amiga
 * music via official sc68 (libsc68 + file68 + unice68).
 *
 * Not a Winamp in_sc68 wrapper. Classic XMPlay is 32-bit only.
 */
#if defined(__GNUC__)
#define XMPIN_GetInterface XMPIN_GetInterface_Declared
#endif
#include "xmpin.h"
#if defined(__GNUC__)
#undef XMPIN_GetInterface
#endif

#include "sc68_player.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define PLUGIN_NAME    "SC68 (Atari ST / Amiga)"
#define PLUGIN_VERSION "1.0.2"
#define PREFIX_BYTES   2048
#define MAX_MODULE_BYTES ((size_t)16u * 1024u * 1024u)
#define INFO_WRITE_MAX 32766
#define DEFAULT_RATE   44100
#define MIN_RATE       8000
#define MAX_RATE       96000

typedef struct {
  float gain_db;
  int   boost;
} sc68_cfg_t;

static XMPFUNC_IN   *xmpfin;
static XMPFUNC_MISC *xmpfmisc;
static XMPFUNC_FILE *xmpffile;

static sc68_player *g_play;
static char         g_name_hint[512];
static int32_t      g_rate = DEFAULT_RATE;
static sc68_cfg_t   g_cfg = { 0.0f, 0 };

#ifdef _WIN32
static HINSTANCE g_hinst;
#endif

static void bounded_copy(char *dst, size_t cap, const char *src)
{
  size_t n;
  if (!dst || cap == 0)
    return;
  if (!src) { dst[0] = '\0'; return; }
  n = strlen(src);
  if (n >= cap) n = cap - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

static void sanitize_line(char *s)
{
  if (!s) return;
  for (; *s; ++s)
    if (*s == '\t' || *s == '\r' || *s == '\n')
      *s = ' ';
}

static void write_kv(char **cursor, char *end, const char *name, const char *value)
{
  size_t nl, vl, need;
  if (!cursor || !*cursor || !end || !name || !value || !value[0])
    return;
  nl = strlen(name);
  vl = strlen(value);
  need = nl + 1 + vl + 1;
  if (*cursor + need >= end)
    return;
  memcpy(*cursor, name, nl); *cursor += nl;
  **cursor = '\t'; *cursor += 1;
  memcpy(*cursor, value, vl); *cursor += vl;
  **cursor = '\r'; *cursor += 1;
  **cursor = '\0';
}

static void *xmp_alloc(DWORD n)
{
  if (!xmpfmisc || !xmpfmisc->Alloc || n == 0)
    return NULL;
  return xmpfmisc->Alloc(n);
}

static void remember_hint(const char *filename)
{
  size_t n;
  g_name_hint[0] = '\0';
  if (!filename || !filename[0])
    return;
  n = strlen(filename);
  if (n >= sizeof g_name_hint)
    n = sizeof g_name_hint - 1;
  memcpy(g_name_hint, filename, n);
  g_name_hint[n] = '\0';
}

static int read_prefix(XMPFILE file, unsigned char *buf, size_t cap, size_t *got)
{
  DWORD n, pos, type;
  if (got) *got = 0;
  if (!file || !buf || cap == 0 || !xmpffile)
    return 0;
  if (!xmpffile->GetType || !xmpffile->Read)
    return 0;
  type = xmpffile->GetType(file);
  if (type == XMPFILE_TYPE_MEMORY) {
    const void *mem;
    DWORD sz;
    if (!xmpffile->GetMemory || !xmpffile->GetSize)
      return 0;
    mem = xmpffile->GetMemory(file);
    sz = xmpffile->GetSize(file);
    if (!mem || sz == 0)
      return 0;
    if ((size_t)sz > cap) sz = (DWORD)cap;
    memcpy(buf, mem, sz);
    if (got) *got = sz;
    return 1;
  }
  pos = xmpffile->Tell ? xmpffile->Tell(file) : 0;
  if (xmpffile->Seek)
    xmpffile->Seek(file, 0);
  n = xmpffile->Read(file, buf, (DWORD)cap);
  if (xmpffile->Seek)
    xmpffile->Seek(file, pos);
  if (got) *got = n;
  return n > 0;
}

static int slurp_xmpfile(XMPFILE file, unsigned char **out, size_t *out_len)
{
  DWORD type, sz, got, pos;
  unsigned char *buf;
  if (out) *out = NULL;
  if (out_len) *out_len = 0;
  if (!file || !out || !out_len || !xmpffile || !xmpffile->Read)
    return 0;
  type = xmpffile->GetType(file);
  if (type == XMPFILE_TYPE_MEMORY) {
    const void *mem;
    if (!xmpffile->GetMemory || !xmpffile->GetSize)
      return 0;
    mem = xmpffile->GetMemory(file);
    sz = xmpffile->GetSize(file);
    if (!mem || sz < 4 || (size_t)sz > MAX_MODULE_BYTES)
      return 0;
    buf = (unsigned char *)malloc(sz);
    if (!buf) return 0;
    memcpy(buf, mem, sz);
    *out = buf;
    *out_len = sz;
    return 1;
  }
  sz = xmpffile->GetSize ? xmpffile->GetSize(file) : 0;
  pos = xmpffile->Tell ? xmpffile->Tell(file) : 0;
  if (xmpffile->Seek)
    xmpffile->Seek(file, 0);
  if (sz > 0) {
    if (sz < 4 || (size_t)sz > MAX_MODULE_BYTES) {
      if (xmpffile->Seek) xmpffile->Seek(file, pos);
      return 0;
    }
    buf = (unsigned char *)malloc(sz);
    if (!buf) {
      if (xmpffile->Seek) xmpffile->Seek(file, pos);
      return 0;
    }
    got = xmpffile->Read(file, buf, sz);
    if (xmpffile->Seek) xmpffile->Seek(file, pos);
    if (got < 4) { free(buf); return 0; }
    *out = buf;
    *out_len = got;
    return 1;
  }
  {
    size_t cap = 64 * 1024, total = 0;
    buf = (unsigned char *)malloc(cap);
    if (!buf) return 0;
    for (;;) {
      DWORD chunk;
      if (total == cap) {
        size_t ncap = cap * 2;
        unsigned char *nb;
        if (ncap > MAX_MODULE_BYTES) ncap = MAX_MODULE_BYTES;
        if (ncap <= cap) { free(buf); return 0; }
        nb = (unsigned char *)realloc(buf, ncap);
        if (!nb) { free(buf); return 0; }
        buf = nb;
        cap = ncap;
      }
      chunk = xmpffile->Read(file, buf + total, (DWORD)(cap - total));
      if (chunk == 0) break;
      total += chunk;
      if (total >= MAX_MODULE_BYTES) break;
    }
    if (xmpffile->Seek) xmpffile->Seek(file, pos);
    if (total < 4) { free(buf); return 0; }
    *out = buf;
    *out_len = total;
    return 1;
  }
}

static XMPFILE open_if_needed(const char *filename, XMPFILE file, int *opened)
{
  *opened = 0;
  if (file) return file;
  if (!filename || !xmpffile || !xmpffile->Open)
    return NULL;
  file = xmpffile->Open(filename);
  if (file) *opened = 1;
  return file;
}

static void close_if_opened(XMPFILE file, int opened)
{
  if (opened && file && xmpffile && xmpffile->Close)
    xmpffile->Close(file);
}

static void apply_cfg(void)
{
  if (!g_play) return;
  sc68_player_set_gain_db(g_play, g_cfg.gain_db);
  sc68_player_set_boost(g_play, g_cfg.boost);
}

static void unload_playback(void)
{
  if (g_play) {
    sc68_player_close(g_play);
    g_play = NULL;
  }
  g_rate = DEFAULT_RATE;
  g_name_hint[0] = '\0';
}

static void append_tag(char **p, char *end, const char *key, const char *val)
{
  size_t kl, vl;
  if (!p || !*p || !end || !key || !val || !val[0])
    return;
  kl = strlen(key);
  vl = strlen(val);
  if (*p + kl + 1 + vl + 1 + 1 >= end)
    return;
  memcpy(*p, key, kl); *p += kl;
  **p = '\0'; *p += 1;
  memcpy(*p, val, vl); *p += vl;
  **p = '\0'; *p += 1;
}

static char *finish_tags(char *stack, char *p, size_t stack_sz)
{
  char *end = stack + stack_sz;
  char *out;
  size_t n;
  if (p + 1 < end)
    *p++ = '\0';
  n = (size_t)(p - stack);
  out = (char *)xmp_alloc((DWORD)n);
  if (!out) return NULL;
  memcpy(out, stack, n);
  return out;
}

static char *build_tags_info(const sc68_info *inf, int track0)
{
  char stack[8192];
  char *p = stack;
  char *end = stack + sizeof stack;
  char trk[16];
  const char *title;
  if (!xmpfmisc) return NULL;
  title = inf->title;
  if (track0 >= 0 && track0 < inf->tracks && inf->track_title[track0][0])
    title = inf->track_title[track0];
  append_tag(&p, end, "filetype", inf->is_sc68 ? "SC68" : "SNDH");
  append_tag(&p, end, "title", title);
  append_tag(&p, end, "artist", inf->artist);
  append_tag(&p, end, "album", inf->album);
  append_tag(&p, end, "date", inf->year);
  if (inf->tracks > 1) {
    snprintf(trk, sizeof trk, "%d", track0 + 1);
    append_tag(&p, end, "track", trk);
  }
  append_tag(&p, end, "comment", inf->comment);
  return finish_tags(stack, p, sizeof stack);
}

static char *build_tags_play(sc68_player *pl)
{
  char stack[8192];
  char *p = stack;
  char *end = stack + sizeof stack;
  char trk[16];
  if (!pl || !xmpfmisc) return NULL;
  append_tag(&p, end, "filetype", sc68_player_is_sc68(pl) ? "SC68" : "SNDH");
  append_tag(&p, end, "title", sc68_player_title(pl));
  append_tag(&p, end, "artist", sc68_player_artist(pl));
  append_tag(&p, end, "album", sc68_player_album(pl));
  append_tag(&p, end, "date", sc68_player_year(pl));
  if (sc68_player_tracks(pl) > 1) {
    snprintf(trk, sizeof trk, "%d", sc68_player_track(pl) + 1);
    append_tag(&p, end, "track", trk);
  }
  append_tag(&p, end, "comment", sc68_player_comment(pl));
  return finish_tags(stack, p, sizeof stack);
}

/* ---- XMPIN methods ---------------------------------------------------- */

static void WINAPI sc68_About(HWND win)
{
  char buf[1024];
  snprintf(buf, sizeof buf,
    PLUGIN_NAME " " PLUGIN_VERSION "\r\n"
    "Native XMPlay input plugin for Atari ST and Amiga music.\r\n"
    "Engine: official sc68 (libsc68 + file68 + unice68)\r\n"
    "by Benjamin Gerard.\r\n\r\n"
    "Formats: .sc68 .sndh .snd (including Pack-Ice ICE!)\r\n"
    "Seek works on every format. Multi-track files are NSF-style\r\n"
    "subsongs (Shift+Left / Shift+Right).\r\n\r\n"
    "Not a Winamp in_sc68 wrapper. 32-bit XMPlay only.\r\n"
    "License: GPLv3 (sc68 is GPLv3).");
#ifdef _WIN32
  MessageBoxA(win, buf, PLUGIN_NAME, MB_OK | MB_ICONINFORMATION);
#else
  (void)win;
  (void)buf;
#endif
}

#ifdef _WIN32
#define IDC_GAIN 1001
#define IDC_BOOST 1002

static INT_PTR CALLBACK cfg_dlg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  char buf[64];
  (void)lp;
  switch (msg) {
  case WM_INITDIALOG:
    snprintf(buf, sizeof buf, "%.1f", g_cfg.gain_db);
    SetDlgItemTextA(hwnd, IDC_GAIN, buf);
    CheckDlgButton(hwnd, IDC_BOOST, g_cfg.boost ? BST_CHECKED : BST_UNCHECKED);
    return TRUE;
  case WM_COMMAND:
    if (LOWORD(wp) == IDOK) {
      GetDlgItemTextA(hwnd, IDC_GAIN, buf, (int)sizeof buf);
      g_cfg.gain_db = (float)atof(buf);
      if (g_cfg.gain_db < -24.0f) g_cfg.gain_db = -24.0f;
      if (g_cfg.gain_db > 24.0f) g_cfg.gain_db = 24.0f;
      g_cfg.boost = IsDlgButtonChecked(hwnd, IDC_BOOST) == BST_CHECKED;
      apply_cfg();
      EndDialog(hwnd, IDOK);
      return TRUE;
    }
    if (LOWORD(wp) == IDCANCEL) {
      EndDialog(hwnd, IDCANCEL);
      return TRUE;
    }
    break;
  }
  return FALSE;
}

/* In-memory dialog: Gain (dB) edit + Boost checkbox + OK/Cancel. */
static void WINAPI sc68_Config(HWND win)
{
  WORD *p;
  DLGTEMPLATE *dlg;
  unsigned char raw[512];
  memset(raw, 0, sizeof raw);
  dlg = (DLGTEMPLATE *)raw;
  dlg->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT;
  dlg->cdit = 6;
  dlg->x = 10; dlg->y = 10; dlg->cx = 180; dlg->cy = 70;
  p = (WORD *)(dlg + 1);
  *p++ = 0; /* menu */
  *p++ = 0; /* class */
  {
    const wchar_t *cap = L"SC68";
    size_t i;
    for (i = 0; cap[i]; ++i) *p++ = (WORD)cap[i];
    *p++ = 0;
  }
  *p++ = 9; /* font size */
  {
    const wchar_t *fnt = L"MS Shell Dlg";
    size_t i;
    for (i = 0; fnt[i]; ++i) *p++ = (WORD)fnt[i];
    *p++ = 0;
  }
#define ADDCTL(_id, _x, _y, _w, _h, _style, _clsid, _title) do { \
    DLGITEMTEMPLATE *item; \
    if (((uintptr_t)p) & 3) p = (WORD *)((((uintptr_t)p) + 3) & ~(uintptr_t)3); \
    item = (DLGITEMTEMPLATE *)p; \
    item->style = WS_CHILD | WS_VISIBLE | (_style); \
    item->x = (short)(_x); item->y = (short)(_y); item->cx = (short)(_w); item->cy = (short)(_h); \
    item->id = (WORD)(_id); \
    p = (WORD *)(item + 1); \
    *p++ = 0xFFFF; *p++ = (WORD)(_clsid); \
    { const wchar_t *_t = (_title); size_t _i; \
      for (_i = 0; _t[_i]; ++_i) *p++ = (WORD)_t[_i]; *p++ = 0; } \
    *p++ = 0; \
  } while (0)
  ADDCTL(-1, 8, 8, 50, 10, 0, 0x0082, L"Gain (dB)");
  ADDCTL(IDC_GAIN, 64, 6, 40, 12, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 0x0081, L"0.0");
  ADDCTL(IDC_BOOST, 8, 24, 160, 12, WS_TABSTOP | BS_AUTOCHECKBOX, 0x0080, L"Boost quiet tunes (+6 dB)");
  ADDCTL(IDOK, 70, 48, 46, 14, WS_TABSTOP | BS_DEFPUSHBUTTON, 0x0080, L"OK");
  ADDCTL(IDCANCEL, 122, 48, 46, 14, WS_TABSTOP | BS_PUSHBUTTON, 0x0080, L"Cancel");
#undef ADDCTL
  DialogBoxIndirectParamA(g_hinst, dlg, win, cfg_dlg, 0);
}
#else
static void WINAPI sc68_Config(HWND win) { (void)win; }
#endif

static BOOL WINAPI sc68_CheckFile(const char *filename, XMPFILE file)
{
  unsigned char prefix[PREFIX_BYTES];
  size_t got = 0;
  int opened = 0;
  int ok;

  file = open_if_needed(filename, file, &opened);
  if (!file)
    return FALSE;
  if (!read_prefix(file, prefix, sizeof prefix, &got)) {
    close_if_opened(file, opened);
    return FALSE;
  }
  ok = sc68_player_probe(prefix, got);
  close_if_opened(file, opened);
  return ok ? TRUE : FALSE;
}

static DWORD WINAPI sc68_GetFileInfo(const char *filename, XMPFILE file,
                                     float **length, char **tags)
{
  unsigned char *data = NULL;
  size_t len = 0;
  int opened = 0;
  sc68_info inf;
  int n;

  if (length) *length = NULL;
  if (tags) *tags = NULL;

  file = open_if_needed(filename, file, &opened);
  if (!file)
    return 0;
  if (!slurp_xmpfile(file, &data, &len)) {
    close_if_opened(file, opened);
    return 0;
  }
  close_if_opened(file, opened);

  if (sc68_player_info_mem(data, len, &inf) != 0) {
    free(data);
    return 0;
  }
  free(data);

  n = inf.tracks > 0 ? inf.tracks : 1;
  if (length) {
    float *lens = (float *)xmp_alloc((DWORD)(sizeof(float) * (unsigned)n));
    if (lens)
      memcpy(lens, inf.len, sizeof(float) * (unsigned)n);
    *length = lens;
  }
  if (tags)
    *tags = build_tags_info(&inf, 0);
  return (DWORD)n | (inf.shared_tags ? XMPIN_INFO_NOSUBTAGS : 0);
}

static DWORD WINAPI sc68_Open(const char *filename, XMPFILE file)
{
  unsigned char *data = NULL;
  size_t len = 0;
  int opened = 0;
  double dur;

  unload_playback();
  remember_hint(filename);

  file = open_if_needed(filename, file, &opened);
  if (!file)
    return 0;
  if (!slurp_xmpfile(file, &data, &len)) {
    close_if_opened(file, opened);
    return 0;
  }
  close_if_opened(file, opened);

  g_play = sc68_player_open(data, len, g_rate);
  free(data);
  if (!g_play)
    return 0;
  apply_cfg();
  g_rate = sc68_player_rate(g_play);
  dur = sc68_player_track_len(g_play, 0);
  if (xmpfin && xmpfin->SetLength && dur > 0.0 && dur < 86400.0)
    xmpfin->SetLength((float)dur, TRUE);
  return 2;
}

static void WINAPI sc68_Close(void)
{
  unload_playback();
}

static void WINAPI sc68_SetFormat(XMPFORMAT *form)
{
  if (!form)
    return;
  if (!g_play) {
    form->rate = 0;
    form->chan = 0;
    form->res = 0;
    form->chanmask = 0;
    return;
  }
  if (form->rate >= MIN_RATE && form->rate <= MAX_RATE)
    g_rate = sc68_player_set_rate(g_play, (int)form->rate);
  else
    g_rate = sc68_player_rate(g_play);
  form->rate = (DWORD)g_rate;
  form->chan = 2;
  form->res = 4; /* float */
  form->chanmask = 0;
}

static char *WINAPI sc68_GetTags(void)
{
  if (!g_play)
    return NULL;
  return build_tags_play(g_play);
}

static void WINAPI sc68_GetInfoText(char *format, char *length)
{
  char tmp[256];
  int m, s;
  double dur;
  if (format) format[0] = '\0';
  if (length) length[0] = '\0';
  if (!g_play)
    return;
  if (format) {
    snprintf(tmp, sizeof tmp, "%s  %s  (%s)",
             sc68_player_is_sc68(g_play) ? "SC68" : "SNDH",
             sc68_player_hw(g_play),
             sc68_player_replay(g_play));
    sanitize_line(tmp);
    bounded_copy(format, 256, tmp);
  }
  if (length) {
    dur = sc68_player_track_len(g_play, sc68_player_track(g_play));
    m = (int)(dur / 60.0);
    s = (int)dur % 60;
    if (sc68_player_tracks(g_play) > 1)
      snprintf(tmp, sizeof tmp, "%d:%02d  track %d/%d",
               m, s, sc68_player_track(g_play) + 1, sc68_player_tracks(g_play));
    else
      snprintf(tmp, sizeof tmp, "%d:%02d", m, s);
    sanitize_line(tmp);
    bounded_copy(length, 256, tmp);
  }
}

static void WINAPI sc68_GetGeneralInfo(char *buf)
{
  char local[4096];
  char *p, *end;
  char num[32];
  if (!buf) return;
  buf[0] = '\0';
  if (!g_play) return;
  p = local;
  end = local + sizeof local - 2;
  local[0] = '\0';
  write_kv(&p, end, "Title", sc68_player_title(g_play));
  write_kv(&p, end, "Artist", sc68_player_artist(g_play));
  write_kv(&p, end, "Album", sc68_player_album(g_play));
  write_kv(&p, end, "Year", sc68_player_year(g_play));
  write_kv(&p, end, "Format", sc68_player_is_sc68(g_play) ? "SC68" : "SNDH");
  write_kv(&p, end, "Hardware", sc68_player_hw(g_play));
  write_kv(&p, end, "Replay", sc68_player_replay(g_play));
  if (sc68_player_tracks(g_play) > 1) {
    snprintf(num, sizeof num, "%d", sc68_player_tracks(g_play));
    write_kv(&p, end, "Tracks", num);
    snprintf(num, sizeof num, "%d", sc68_player_track(g_play) + 1);
    write_kv(&p, end, "Current track", num);
  }
  write_kv(&p, end, "Player", PLUGIN_NAME " " PLUGIN_VERSION);
  write_kv(&p, end, "Engine", "official sc68 (Benjamin Gerard)");
  bounded_copy(buf, INFO_WRITE_MAX, local);
}

static void WINAPI sc68_GetMessage(char *buf)
{
  const char *c;
  if (!buf) return;
  buf[0] = '\0';
  if (!g_play) return;
  c = sc68_player_comment(g_play);
  if (c && c[0])
    bounded_copy(buf, INFO_WRITE_MAX, c);
}

static double WINAPI sc68_GetGranularity(void)
{
  return 0.001;
}

static double WINAPI sc68_SetPosition(DWORD pos)
{
  int sub;
  double t, dur;

  if (!g_play)
    return -1.0;

  if (pos == (DWORD)XMPIN_POS_LOOP || pos == (DWORD)XMPIN_POS_AUTOLOOP)
    return -2.0;

  if (pos & XMPIN_POS_SUBSONG) {
    sub = (int)(pos & 0xFFFFu);
    if (sc68_player_set_track(g_play, sub) != 0)
      return -1.0;
    apply_cfg();
    dur = sc68_player_track_len(g_play, sub);
    if (xmpfin && xmpfin->SetLength && dur > 0.0 && dur < 86400.0)
      xmpfin->SetLength((float)dur, TRUE);
    if (xmpfin && xmpfin->UpdateTitle)
      xmpfin->UpdateTitle(NULL);
    return 0.0;
  }

  t = sc68_player_seek(g_play, (double)pos * sc68_GetGranularity());
  if (t < 0.0)
    return -1.0;
  return t;
}

static DWORD WINAPI sc68_Process(float *buf, DWORD count)
{
  int frames, got;
  if (!buf || !g_play)
    return 0;
  frames = (int)(count / 2u);
  if (frames <= 0)
    return 0;
  got = sc68_player_process(g_play, buf, frames);
  if (got <= 0)
    return 0;
  return (DWORD)(got * 2);
}

static DWORD WINAPI sc68_GetSubSongs(float *length)
{
  if (!g_play)
    return 0;
  if (length)
    *length = (float)sc68_player_total_len(g_play);
  return (DWORD)sc68_player_tracks(g_play);
}

static DWORD WINAPI sc68_GetConfig(void *config)
{
  if (config)
    memcpy(config, &g_cfg, sizeof g_cfg);
  return (DWORD)sizeof g_cfg;
}

static void WINAPI sc68_SetConfig(void *config, DWORD size)
{
  if (!config || size < sizeof g_cfg)
    return;
  memcpy(&g_cfg, config, sizeof g_cfg);
  if (g_cfg.gain_db < -24.0f) g_cfg.gain_db = -24.0f;
  if (g_cfg.gain_db > 24.0f) g_cfg.gain_db = 24.0f;
  apply_cfg();
}

static const char g_exts[] = "SC68 / SNDH\0sc68/sndh/snd";

static XMPIN g_xmpin = {
  XMPIN_FLAG_CONFIG,
  PLUGIN_NAME " " PLUGIN_VERSION,
  g_exts,
  sc68_About,
  sc68_Config,
  sc68_CheckFile,
  sc68_GetFileInfo,
  sc68_Open,
  sc68_Close,
  NULL, /* reserved1 */
  sc68_SetFormat,
  sc68_GetTags,
  sc68_GetInfoText,
  sc68_GetGeneralInfo,
  sc68_GetMessage,
  sc68_SetPosition,
  sc68_GetGranularity,
  NULL, /* GetBuffering */
  sc68_Process,
  NULL, /* WriteFile */
  NULL, /* GetSamples */
  sc68_GetSubSongs,
  NULL, /* reserved3 */
  NULL, /* GetDownloaded */
  NULL, /* visname */
  NULL, NULL, NULL, NULL, NULL, NULL, NULL,
  sc68_GetConfig,
  sc68_SetConfig,
  NULL
};

static XMPIN *WINAPI xmpin_get_interface_impl(DWORD face, InterfaceProc faceproc)
{
  if (face != XMPIN_FACE)
    return NULL;
  if (!faceproc)
    return NULL;
  xmpfin  = (XMPFUNC_IN *)faceproc(XMPFUNC_IN_FACE);
  xmpfmisc = (XMPFUNC_MISC *)faceproc(XMPFUNC_MISC_FACE);
  xmpffile = (XMPFUNC_FILE *)faceproc(XMPFUNC_FILE_FACE);
  if (!xmpfin || !xmpfmisc || !xmpffile)
    return NULL;
  if (!xmpfmisc->Alloc || !xmpffile->Read)
    return NULL;
  return &g_xmpin;
}

extern "C" {

BOOL WINAPI DllMain(HINSTANCE hDLL, DWORD reason, LPVOID reserved)
{
  (void)reserved;
  if (reason == DLL_PROCESS_ATTACH) {
#ifdef _WIN32
    g_hinst = hDLL;
    DisableThreadLibraryCalls(hDLL);
#endif
  }
  return TRUE;
}

#if defined(__GNUC__) && defined(_WIN32) && !defined(_WIN64)
XMPIN *WINAPI XMPIN_GetInterface_(DWORD face, InterfaceProc faceproc)
{
  return xmpin_get_interface_impl(face, faceproc);
}
#if __GNUC__ >= 8
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattribute-alias"
#endif
__attribute__((dllexport)) void XMPIN_GetInterface(void)
  __attribute__((alias("XMPIN_GetInterface_@8")));
#if __GNUC__ >= 8
#pragma GCC diagnostic pop
#endif
#else
__declspec(dllexport) XMPIN *WINAPI XMPIN_GetInterface(DWORD face, InterfaceProc faceproc)
{
  return xmpin_get_interface_impl(face, faceproc);
}
#endif

} /* extern "C" */
