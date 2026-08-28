/*
 * Host-side render / seek / track tests for xmp-sc68.
 */
#include "sc68_player.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

static int g_fail;

static unsigned char *slurp(const char *path, size_t *out_len)
{
  FILE *f;
  unsigned char *buf;
  long sz;
  *out_len = 0;
  f = fopen(path, "rb");
  if (!f) return NULL;
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
  sz = ftell(f);
  if (sz < 4) { fclose(f); return NULL; }
  rewind(f);
  buf = (unsigned char *)malloc((size_t)sz);
  if (!buf) { fclose(f); return NULL; }
  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
    free(buf); fclose(f); return NULL;
  }
  fclose(f);
  *out_len = (size_t)sz;
  return buf;
}

static void rms_peak(const float *s, int frames, double *rms, double *peak)
{
  double acc = 0.0, pk = 0.0;
  int i, n = frames * 2;
  for (i = 0; i < n; ++i) {
    double v = s[i];
    acc += v * v;
    if (v < 0) v = -v;
    if (v > pk) pk = v;
  }
  *rms = n > 0 ? sqrt(acc / n) : 0.0;
  *peak = pk;
}

static int render_sec(sc68_player *p, double sec, double *rms, double *peak)
{
  int rate = sc68_player_rate(p);
  int need = (int)(sec * rate + 0.5);
  int got = 0;
  float *buf;
  if (need < 64) need = 64;
  buf = (float *)calloc((size_t)need * 2, sizeof(float));
  if (!buf) return -1;
  while (got < need) {
    int n = sc68_player_process(p, buf + got * 2, need - got);
    if (n <= 0) break;
    got += n;
  }
  rms_peak(buf, got, rms, peak);
  free(buf);
  return got;
}

static int test_file(const char *path)
{
  unsigned char *data;
  size_t len;
  sc68_info inf;
  sc68_player *p;
  double rms, peak, pos0, pos1, seek_to;
  int frames, tracks, i;
  const char *base;

  base = strrchr(path, '/');
  base = base ? base + 1 : path;
  printf("==== %s ====\n", base);

  data = slurp(path, &len);
  if (!data) {
    printf("FAIL  cannot read\n");
    g_fail++;
    return -1;
  }

  if (!sc68_player_probe(data, len > 2048 ? 2048 : len)) {
    printf("FAIL  probe rejected\n");
    g_fail++;
    free(data);
    return -1;
  }
  printf("probe: accepted (%zu bytes)\n", len);

  if (sc68_player_info_mem(data, len, &inf) != 0) {
    printf("FAIL  info\n");
    g_fail++;
    free(data);
    return -1;
  }
  printf("format: %s  title: %s  artist: %s\n",
         inf.is_sc68 ? "SC68" : "SNDH", inf.title, inf.artist);
  printf("tracks: %d  total: %.1fs\n", inf.tracks, inf.total_len);
  for (i = 0; i < inf.tracks && i < 8; ++i)
    printf("  [%d] %.1fs  %s\n", i + 1, inf.len[i], inf.track_title[i]);

  p = sc68_player_open(data, len, 44100);
  free(data);
  if (!p) {
    printf("FAIL  open\n");
    g_fail++;
    return -1;
  }

  frames = render_sec(p, 2.0, &rms, &peak);
  printf("render 2.0s: frames=%d  rms=%.5f  peak=%.5f\n", frames, rms, peak);
  if (rms < 1e-4) {
    printf("FAIL  silent render\n");
    g_fail++;
  }

  pos0 = sc68_player_tell(p);
  seek_to = inf.len[0] * 0.5;
  if (seek_to < 3.0) seek_to = 3.0;
  if (seek_to > 10.0 && inf.len[0] >= 10.0) seek_to = 10.0;
  if (inf.len[0] > 0.0 && seek_to > inf.len[0] * 0.8)
    seek_to = inf.len[0] * 0.5;
  pos1 = sc68_player_seek(p, seek_to);
  printf("seek to %.2fs -> tell %.2fs (was %.2fs)\n", seek_to, pos1, pos0);
  if (pos1 < 0.0 || pos1 < seek_to * 0.5) {
    printf("FAIL  seek did not advance\n");
    g_fail++;
  }
  frames = render_sec(p, 0.5, &rms, &peak);
  printf("post-seek 0.5s: frames=%d  rms=%.5f  peak=%.5f  tell=%.2f\n",
         frames, rms, peak, sc68_player_tell(p));
  if (rms < 1e-4) {
    printf("FAIL  silent after seek\n");
    g_fail++;
  }

  pos1 = sc68_player_seek(p, 0.0);
  printf("seek to 0 -> %.2fs\n", pos1);
  frames = render_sec(p, 0.3, &rms, &peak);
  if (rms < 1e-4) {
    printf("FAIL  silent after seek-to-0\n");
    g_fail++;
  }

  tracks = sc68_player_tracks(p);
  if (tracks > 1) {
    if (sc68_player_set_track(p, 1) != 0) {
      printf("FAIL  switch track\n");
      g_fail++;
    } else {
      frames = render_sec(p, 1.0, &rms, &peak);
      printf("track 2 render: frames=%d  rms=%.5f  title=%s\n",
             frames, rms, sc68_player_title(p));
      if (rms < 1e-4) {
        printf("FAIL  silent on track 2\n");
        g_fail++;
      }
    }
  }

  sc68_player_close(p);
  printf("OK    %s\n\n", base);
  return 0;
}

int main(int argc, char **argv)
{
  const char *defaults[] = {
    "tests/samples/cream-1996.sndh",
    "tests/samples/sillyshuffle.snd",
    "tests/samples/7-gates-jambala.sc68",
    "tests/samples/lethal-xcess-menu.sc68",
    NULL
  };
  int i, n = 0;

  if (sc68_player_global_init() != 0) {
    fprintf(stderr, "sc68 init failed\n");
    return 1;
  }

  if (argc > 1) {
    for (i = 1; i < argc; ++i) {
      test_file(argv[i]);
      n++;
    }
  } else {
    for (i = 0; defaults[i]; ++i) {
      test_file(defaults[i]);
      n++;
    }
  }

  sc68_player_global_shutdown();
  printf("%s  %d file(s), %d failure(s)\n",
         g_fail ? "FAILED" : "PASSED", n, g_fail);
  return g_fail ? 1 : 0;
}
