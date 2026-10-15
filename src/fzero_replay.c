#include "fzero_replay.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct Span { unsigned first, last; uint32_t mask; } Span;
typedef struct View { unsigned frame; FzeroAspect aspect; unsigned width, height; } View;
static Span spans[128];
static View views[128];
static unsigned span_count, view_count;
static bool has_input;

static bool number(const char **p, unsigned *value) {
  if (!isdigit((unsigned char)**p)) return false;
  char *end;
  errno = 0;
  unsigned long n = strtoul(*p, &end, 0);
  if (end == *p || errno || n > UINT_MAX) return false;
  *p = end; *value = (unsigned)n; return true;
}
bool FzeroReplayConfigure(const char *inputs, const char *viewports) {
  return FzeroReplayConfigurePlayers(inputs,viewports,1);
}
bool FzeroReplayConfigurePlayers(const char *inputs,const char *viewports,unsigned players) {
  if(players!=1 && players!=2)return false;
  span_count = view_count = 0;
  has_input = inputs && *inputs;
  const char *p = inputs ? inputs : "";
  while (*p) {
    if (span_count == 128) return false;
    Span *s = &spans[span_count++];
    if (!number(&p, &s->first)) return false;
    s->last = s->first;
    if (*p == '-') { ++p; if (!number(&p, &s->last)) return false; }
    unsigned mask;
    if (*p++ != ':' || !number(&p, &mask) || s->last < s->first || mask > (players==2?0xffffffu:0xfffu)) return false;
    s->mask = mask;
    if (!*p) break;
    if (*p++ != ',' || !*p) return false;
  }
  p = viewports ? viewports : "";
  while (*p) {
    if (view_count == 128) return false;
    View *v = &views[view_count++];
    if (!number(&p, &v->frame) || *p++ != ':') return false;
    const char *end = strchr(p, ',');
    size_t length = end ? (size_t)(end - p) : strlen(p);
    char aspect[32];
    if (length >= sizeof(aspect)) return false;
    memcpy(aspect, p, length); aspect[length] = 0;
    v->width = v->height = 0;
    char *size = strchr(aspect, '@');
    if (size) {
      *size++ = 0;
      const char *q = size;
      if (!number(&q, &v->width) || *q++ != 'x' || !number(&q, &v->height) || *q ||
          !v->width || !v->height || v->width > 16384 || v->height > 16384) return false;
    }
    if (!FzeroParseAspect(aspect, &v->aspect)) return false;
    if (view_count > 1 && views[view_count - 2].frame >= v->frame) return false;
    p += length;
    if (*p && !*++p) return false;
  }
  return true;
}
bool FzeroReplayHasInput(void) { return has_input; }
uint32_t FzeroReplayInput(unsigned frame) {
  uint32_t mask = 0;
  for (unsigned i = 0; i < span_count; ++i)
    if (frame >= spans[i].first && frame <= spans[i].last) mask |= spans[i].mask;
  return mask;
}
bool FzeroReplayViewport(unsigned frame, FzeroVideoSettings *s) {
  for (unsigned i = 0; i < view_count; ++i) if (views[i].frame == frame) {
    s->aspect = views[i].aspect; s->enhanced = s->aspect != FZERO_ASPECT_STOCK;
    return true;
  }
  return false;
}
bool FzeroReplayWindow(unsigned frame, int *width, int *height) {
  for (unsigned i = 0; i < view_count; ++i) if (views[i].frame == frame && views[i].width) {
    *width = (int)views[i].width; *height = (int)views[i].height; return true;
  }
  return false;
}
