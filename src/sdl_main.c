/*
 * F-Zero desktop host: recomp-ui launcher, SDL window/audio/input, save
 * states, SRAM and the shared crash-report path.
 */

#include "fzero_runtime.h"
#include "fzero_mods.h"
#include "fzero_deluxe.h"
#include "fzero_tracks.h"
#include "fzero_vehicles.h"
#include "fzero_course_runtime.h"
#include "fzero_hotkeys.h"
#include "fzero_gamepad.h"
#include "fzero_msu.h"
#include "fzero_replay.h"
#include "fzero_state_mode.h"
#include "fzero_diagnostics.h"
#include "fzero_build.h"

#include "common_rtl.h"
#include "host_paths.h"
#include "snes_overlay_draw.h"    /* SNES_PAD_*, panel compositing */
#include "snes_rewind.h"          /* local rewind ring + filmstrip */
#include "snes_savestate_menu.h"  /* slot browser overlay */
#include "snes_osd.h"
#include "cpu_trace.h"
#include "debug_server.h"
#include "host_report.h"
#include "keybinds.h"
#include "launcher_binds.h" /* launcher_ini_kv_write: surgical config.ini edits */
#include "launcher_cache.h"
#include "launcher_profile.h"
#include "recomp_launcher.h"
#include "fzero_content_ui.h"
#include "sha256.h"
#include "snes/cart.h"
#include "snes/ppu.h"
#include "snes/snes.h"
#include "spc_player.h"
#include "types.h"

#include "third_party/gl_core/gl_core_3_1.h"
#include "glsl_shader.h"
#include "desktop/config.h"
#include "desktop/sdl_compat.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  kFrameWidth = 256,
  kFrameHeight = 224,
  kBytesPerPixel = 4,
  kFzeroRomSize = 0x80000,
};

/* F-Zero (USA), 512 KiB LoROM, headerless. */
static const uint8_t kFzeroSha256[32] = {
    0xbf, 0x16, 0xc3, 0xc8, 0x67, 0xc5, 0x8e, 0x2a,
    0xb0, 0x61, 0xc7, 0x0d, 0xe9, 0x29, 0x5b, 0x69,
    0x30, 0xd6, 0x3f, 0x29, 0xf8, 0x1c, 0xc9, 0x86,
    0xf5, 0xec, 0xae, 0x03, 0xe0, 0xad, 0x18, 0xd2,
};
static const char *const kFzeroSha1[] = {
    "d3efd32b68f1fe37a82db9d9929b7ca7cc1a3af4",
};

/* Baked in by -DSNESRECOMP_BUILD_VERSION=<ver> (see CMakeLists.txt); local
 * builds report "dev". tools/make_release.ps1 verifies the stamp by scanning
 * the packaged binary for this literal, so it must reach .rodata. */
#ifndef SNESRECOMP_BUILD_VERSION
#define SNESRECOMP_BUILD_VERSION "dev"
#endif
static const char kBuildVersion[] = SNESRECOMP_BUILD_VERSION;

bool g_new_ppu = true;
static SDL_mutex *g_audio_mutex;
static const char kWindowTitle[] = "F-Zero";
static const char *const kQuickLabels[] = {"Quick Save", "Quick Load"};
static const char *const kQuickKeys[] = {"QuickSaveKey", "QuickLoadKey"};
static const char *const kQuickPads[] = {"QuickSavePad", "QuickLoadPad"};
static int g_quick_keys[2], g_quick_pads[2];
static uint32_t g_previous_quick_actions;
static FzeroVideoSettings g_video;
Config g_config;
static bool g_reset_presentation_clock;
static const char *kVideoConfig = "fzero-video.ini";
static char video_config_path[1024];
/* config.ini, exe-anchored: the launcher's [KeyMap] and the game's hotkeys. */
static char g_config_path[1024];
static const char *const kFzeroAspectLabels[] = {
    "4:3",
    "16:9",
    "21:9",
    "32:9",
    "Fit to window",
};

static void spc_initialize(SpcPlayer *player) { (void)player; }
static void spc_upload(SpcPlayer *player, const uint8_t *data) {
  (void)player;
  (void)data;
}

static SpcPlayer g_sdl_spc_player = {
    .initialize = spc_initialize,
    .upload = spc_upload,
};

SpcPlayer *g_spc_player = &g_sdl_spc_player;

void NORETURN Die(const char *error) {
  fprintf(stderr, "fatal: %s\n", error ? error : "unknown error");
  fflush(stderr);
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "F-Zero",
                           error ? error : "Unknown error", NULL);
  exit(EXIT_FAILURE);
}

#define GLSL_CODE(...) #__VA_ARGS__

typedef struct FzeroGlRenderer {
  SDL_Window *window;
  SDL_GLContext context;
  uint program;
  uint vao;
  uint vbo;
  uint overlay_vao;
  uint overlay_vbo;
  GlTextureWithSize texture;
  GlTextureWithSize overlay;
  GlslShader *shader;
} FzeroGlRenderer;

static void fzero_gl_prepare_window(void) {
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
}

static bool fzero_gl_compile(uint shader, const char *kind) {
  int ok = 0;
  char info[512] = {0};
  glCompileShader(shader);
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  glGetShaderInfoLog(shader, sizeof(info), NULL, info);
  if (ok != GL_TRUE) {
    fprintf(stderr, "[fzero-gl] %s shader compile failed:\n%s\n", kind, info);
    return false;
  }
  if (info[0]) fprintf(stderr, "[fzero-gl] %s shader compile log:\n%s\n", kind, info);
  return true;
}

static bool fzero_gl_link(uint program) {
  int ok = 0;
  char info[512] = {0};
  glLinkProgram(program);
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  glGetProgramInfoLog(program, sizeof(info), NULL, info);
  if (ok != GL_TRUE) {
    fprintf(stderr, "[fzero-gl] program link failed:\n%s\n", info);
    return false;
  }
  if (info[0]) fprintf(stderr, "[fzero-gl] program link log:\n%s\n", info);
  return true;
}

/* A full-screen textured quad, as its own VAO + VBO.
 *
 * Two of these exist on purpose. GlslShader_Render binds its own array buffer
 * and enables its own attribute arrays inside whatever VAO is current, then
 * disables them again — so the VAO handed to a shader preset comes back with
 * its attributes off, and the next plain glDrawArrays through it draws
 * nothing. That is exactly what happened to the first version of the overlay
 * on the GL path: the panel was uploaded, the draw was issued, and the screen
 * was unchanged. The overlay gets a VAO no shader has ever touched. */
static bool fzero_gl_create_quad(uint *vao, uint *vbo) {
  static const float vertices[] = {
      -1.0f,  1.0f, 0.0f, 0.0f, 0.0f,
      -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,
       1.0f,  1.0f, 0.0f, 1.0f, 0.0f,
       1.0f, -1.0f, 0.0f, 1.0f, 1.0f,
  };
  glGenVertexArrays(1, vao);
  glGenBuffers(1, vbo);
  glBindVertexArray(*vao);
  glBindBuffer(GL_ARRAY_BUFFER, *vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
  return *vao != 0 && *vbo != 0;
}

static bool fzero_gl_create_passthrough(FzeroGlRenderer *glr) {
  static const GLchar *vs_code = "#version 330 core\n" GLSL_CODE(
    layout(location = 0) in vec3 aPos;
    layout(location = 1) in vec2 aTexCoord;
    out vec2 TexCoord;
    void main(void) {
      gl_Position = vec4(aPos, 1.0);
      TexCoord = aTexCoord;
    }
  );
  static const GLchar *fs_code = "#version 330 core\n" GLSL_CODE(
    out vec4 FragColor;
    in vec2 TexCoord;
    uniform sampler2D texture1;
    void main(void) {
      FragColor = texture(texture1, TexCoord);
    }
  );

  uint vs = glCreateShader(GL_VERTEX_SHADER);
  uint fs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(vs, 1, &vs_code, NULL);
  glShaderSource(fs, 1, &fs_code, NULL);
  bool ok = fzero_gl_compile(vs, "vertex") && fzero_gl_compile(fs, "fragment");
  if (ok) {
    glr->program = glCreateProgram();
    glAttachShader(glr->program, vs);
    glAttachShader(glr->program, fs);
    ok = fzero_gl_link(glr->program);
  }
  glDeleteShader(vs);
  glDeleteShader(fs);
  if (!ok) return false;
  return fzero_gl_create_quad(&glr->vao, &glr->vbo) &&
         fzero_gl_create_quad(&glr->overlay_vao, &glr->overlay_vbo);
}

static bool fzero_gl_init(FzeroGlRenderer *glr, SDL_Window *window,
                          const char *shader_path) {
  memset(glr, 0, sizeof(*glr));
  glr->window = window;
  glr->context = SDL_GL_CreateContext(window);
  if (!glr->context) return false;
  SDL_GL_SetSwapInterval(1);
  ogl_LoadFunctions();
  if (!ogl_IsVersionGEQ(3, 3)) {
    fprintf(stderr, "[fzero-gl] OpenGL 3.3 is required for GLSL shaders\n");
    return false;
  }
  glGenTextures(1, &glr->texture.gl_texture);
  if (!fzero_gl_create_passthrough(glr)) return false;
  if (shader_path && shader_path[0]) {
    glr->shader = GlslShader_CreateFromFile(shader_path);
    if (!glr->shader) {
      fprintf(stderr, "[fzero-gl] Unable to load shader preset: %s; using unfiltered output\n", shader_path);
    }
  }
  return true;
}

static void fzero_gl_render(FzeroGlRenderer *glr, const uint8_t *pixels,
                            int logical_width, int logical_height, FzeroViewport viewport,
                            int drawable_width, int drawable_height) {
  uint64_t diagnostic_start = FzeroDiagnosticsBegin();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, glr->texture.gl_texture);
  if (glr->texture.width == logical_width && glr->texture.height == logical_height) {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, logical_width, logical_height,
                    GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
  } else {
    glr->texture.width = (uint16)logical_width;
    glr->texture.height = (uint16)logical_height;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, logical_width, logical_height, 0,
                 GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
  }

  FzeroDiagnosticsEnd(FZERO_DIAG_UPLOAD, diagnostic_start);
  diagnostic_start = FzeroDiagnosticsBegin();
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  FzeroRect rect = FzeroDestination(viewport, drawable_width, drawable_height);
  int viewport_y = drawable_height - rect.y - rect.h;
  if (glr->shader) {
    glBindVertexArray(glr->vao);
    GlslShader_Render(glr->shader, &glr->texture, rect.x, viewport_y, rect.w,
                      rect.h);
    glBindVertexArray(0);
  } else {
    int filter = g_config.linear_filtering ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glViewport(rect.x, viewport_y, rect.w, rect.h);
    glUseProgram(glr->program);
    glUniform1i(glGetUniformLocation(glr->program, "texture1"), 0);
    glBindVertexArray(glr->vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glUseProgram(0);
  }
  FzeroDiagnosticsEnd(FZERO_DIAG_DRAW, diagnostic_start);
}

/* An overlay panel over the GL frame, through the passthrough program rather
 * than the user's shader preset: a CRT curve applied to the save-state
 * browser would bend its text, and the panel is host UI, not a game image.
 * Drawn before the swap so the SDL_Renderer path and this one present the
 * same composition. */
static void fzero_gl_draw_overlay(FzeroGlRenderer *glr, const uint32_t *panel,
                                  int pw, int ph, FzeroRect rect,
                                  int drawable_height) {
  if (!panel || pw <= 0 || ph <= 0) return;
  if (!glr->overlay.gl_texture) glGenTextures(1, &glr->overlay.gl_texture);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, glr->overlay.gl_texture);
  if (glr->overlay.width == pw && glr->overlay.height == ph) {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pw, ph, GL_BGRA,
                    GL_UNSIGNED_INT_8_8_8_8_REV, panel);
  } else {
    glr->overlay.width = (uint16)pw;
    glr->overlay.height = (uint16)ph;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pw, ph, 0, GL_BGRA,
                 GL_UNSIGNED_INT_8_8_8_8_REV, panel);
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glViewport(rect.x, drawable_height - rect.y - rect.h, rect.w, rect.h);
  glUseProgram(glr->program);
  glUniform1i(glGetUniformLocation(glr->program, "texture1"), 0);
  glBindVertexArray(glr->overlay_vao);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);
  glUseProgram(0);
  glDisable(GL_BLEND);
}

static void fzero_gl_destroy(FzeroGlRenderer *glr) {
  if (glr->shader) GlslShader_Destroy(glr->shader);
  if (glr->overlay.gl_texture) glDeleteTextures(1, &glr->overlay.gl_texture);
  glDeleteTextures(1, &glr->texture.gl_texture);
  glDeleteProgram(glr->program);
  glDeleteBuffers(1, &glr->vbo);
  glDeleteVertexArrays(1, &glr->vao);
  if (glr->overlay_vbo) glDeleteBuffers(1, &glr->overlay_vbo);
  if (glr->overlay_vao) glDeleteVertexArrays(1, &glr->overlay_vao);
  if (glr->context) SDL_GL_DestroyContext(glr->context);
  memset(glr, 0, sizeof(*glr));
}

void RtlApuLock(void) {
  if (g_audio_mutex) SDL_LockMutex(g_audio_mutex);
}

void RtlApuUnlock(void) {
  if (g_audio_mutex) SDL_UnlockMutex(g_audio_mutex);
}

static uint8_t *read_rom(const char *path, size_t *size_out) {
  FILE *stream = fopen(path, "rb");
  if (!stream) return NULL;
  if (fseek(stream, 0, SEEK_END) != 0) {
    fclose(stream);
    return NULL;
  }
  long length = ftell(stream);
  if ((length != kFzeroRomSize && length != kFzeroRomSize + 512) ||
      fseek(stream, 0, SEEK_SET) != 0) {
    fclose(stream);
    return NULL;
  }
  uint8_t *rom = (uint8_t *)malloc((size_t)length);
  if (!rom || fread(rom, 1, (size_t)length, stream) != (size_t)length) {
    free(rom);
    fclose(stream);
    return NULL;
  }
  fclose(stream);

  size_t skip = (size_t)length % 1024u == 512u ? 512u : 0u;
  *size_out = (size_t)length - skip;
  if (skip) memmove(rom, rom + skip, *size_out);
  return rom;
}

static int verify_rom(const uint8_t *rom, size_t size) {
  uint8_t actual[32];
  if (size != (size_t)kFzeroRomSize) return 0;
  sha256_compute(rom, size, actual);
  return memcmp(actual, kFzeroSha256, sizeof(actual)) == 0;
}

/*
 * Rewind's three settings persist in config.ini, because recomp-ui leaves
 * Settings persistence to the host and this host previously persisted none of
 * it. Without this the launcher's checkbox would come back off on every
 * launch, which reads as the feature not working.
 *
 * [Rewind] Enabled / Depth / Interval, written back through the framework's
 * own surgical ini writer so the rest of the file — [KeyMap] above all — is
 * untouched.
 */
/* Strip the whitespace an ini value carries either side of it. The string
 * reader hands back everything after the '=', which for `Shader = foo.slangp`
 * starts with a space -- and a leading space turns a real path into a missing
 * file. */
static void trim_ini_value(char *s) {
  char *start = s;
  size_t len;
  while (*start == ' ' || *start == '\t') start++;
  if (start != s) memmove(s, start, strlen(start) + 1);
  len = strlen(s);
  while (len && (s[len - 1] == ' ' || s[len - 1] == '\t')) s[--len] = '\0';
}

static void load_launcher_settings(RecompLauncherCSettings *settings) {
  int value = 0;
  char text[sizeof(settings->shader_path)];
  for (int i = 0; i < 2; ++i) {
    FzeroIniReadInt(g_config_path, "SaveStates", kQuickKeys[i], &settings->assist_key_bind[i]);
    FzeroIniReadInt(g_config_path, "SaveStates", kQuickPads[i], &settings->assist_pad_bind[i]);
  }

  if (FzeroIniReadInt(g_config_path, "General", "SkipLauncher", &value))
    settings->skip_launcher = value != 0;

  /* Display. Section and key spellings match the shared snesrecomp host's
   * config.ini so one file reads the same across every port. */
  if (FzeroIniReadInt(g_config_path, "Graphics", "WindowScale", &value) &&
      value >= 1 && value <= 8)
    settings->window_scale = value;
  if (FzeroIniReadInt(g_config_path, "Graphics", "Fullscreen", &value))
    settings->fullscreen = value;
  if (FzeroIniReadInt(g_config_path, "Graphics", "LinearFiltering", &value))
    settings->linear_filter = value != 0;
  if (FzeroIniReadString(g_config_path, "Graphics", "Shader", text,
                         sizeof(text))) {
    trim_ini_value(text);
    snprintf(settings->shader_path, sizeof(settings->shader_path), "%s", text);
  }

  /* Sound. */
  if (FzeroIniReadInt(g_config_path, "Sound", "AudioFreq", &value) &&
      value >= 8000 && value <= 96000)
    settings->audio_freq = value;
  if (FzeroIniReadInt(g_config_path, "Sound", "Volume", &value) &&
      value >= 0 && value <= 100)
    settings->volume = value;
  if (FzeroIniReadInt(g_config_path, "Sound", "Msu1Enabled", &value))
    settings->msu1_enabled = value != 0;

  if (FzeroIniReadString(g_config_path, "Controller", "GuidP1", text, sizeof(text))) {
    trim_ini_value(text);
    snprintf(settings->player_gamepad_guid[0], sizeof(settings->player_gamepad_guid[0]), "%s", text);
  }
  if (FzeroIniReadInt(g_config_path, "Controller", "SourceP1", &value) && value >= 0 && value <= 2)
    settings->player_src[0] = value;
  if (FzeroIniReadInt(g_config_path, "Controller", "DeadzoneP1", &value) && value >= 0 && value <= 100)
    settings->deadzone[0] = value;

  if (FzeroIniReadInt(g_config_path, "Rewind", "Enabled", &value))
    settings->rewind_enabled = value != 0;
  if (FzeroIniReadInt(g_config_path, "Rewind", "Depth", &value))
    settings->rewind_depth = value;
  if (FzeroIniReadInt(g_config_path, "Rewind", "Interval", &value))
    settings->rewind_interval = value;
}

/*
 * Every row the launcher draws, written back.
 *
 * This used to cover the three Rewind keys and nothing else: the rest of the
 * Settings page was memset to a constant on the way in and dropped on the way
 * out, so Fullscreen, Linear filtering, the shader, the sample rate and the
 * volume all reverted on every single launch. That is the whole of "the
 * launcher doesn't remember anything" for this title. Aspect ratio is the one
 * row that stays elsewhere -- FzeroVideoSave owns it, because the built-in
 * presentation mod shares it.
 */
static void save_launcher_settings(const RecompLauncherCSettings *settings) {
  char number[32];
  for (int i = 0; i < 2; ++i) {
    snprintf(number, sizeof(number), "%d", settings->assist_key_bind[i]);
    launcher_ini_kv_write(g_config_path, "SaveStates", kQuickKeys[i], number);
    snprintf(number, sizeof(number), "%d", settings->assist_pad_bind[i]);
    launcher_ini_kv_write(g_config_path, "SaveStates", kQuickPads[i], number);
  }
  snprintf(number, sizeof(number), "%d", settings->skip_launcher ? 1 : 0);
  launcher_ini_kv_write(g_config_path, "General", "SkipLauncher", number);
  snprintf(number, sizeof(number), "%d", settings->window_scale);
  launcher_ini_kv_write(g_config_path, "Graphics", "WindowScale", number);
  snprintf(number, sizeof(number), "%d", settings->fullscreen);
  launcher_ini_kv_write(g_config_path, "Graphics", "Fullscreen", number);
  snprintf(number, sizeof(number), "%d", settings->linear_filter ? 1 : 0);
  launcher_ini_kv_write(g_config_path, "Graphics", "LinearFiltering", number);
  launcher_ini_kv_write(g_config_path, "Graphics", "Shader",
                        settings->shader_path);

  snprintf(number, sizeof(number), "%d", settings->audio_freq);
  launcher_ini_kv_write(g_config_path, "Sound", "AudioFreq", number);
  snprintf(number, sizeof(number), "%d", settings->volume);
  launcher_ini_kv_write(g_config_path, "Sound", "Volume", number);
  snprintf(number, sizeof(number), "%d", settings->msu1_enabled ? 1 : 0);
  launcher_ini_kv_write(g_config_path, "Sound", "Msu1Enabled", number);

  launcher_ini_kv_write(g_config_path, "Controller", "GuidP1", settings->player_gamepad_guid[0]);
  snprintf(number, sizeof(number), "%d", settings->player_src[0]);
  launcher_ini_kv_write(g_config_path, "Controller", "SourceP1", number);
  snprintf(number, sizeof(number), "%d", settings->deadzone[0]);
  launcher_ini_kv_write(g_config_path, "Controller", "DeadzoneP1", number);

  snprintf(number, sizeof(number), "%d", settings->rewind_enabled ? 1 : 0);
  launcher_ini_kv_write(g_config_path, "Rewind", "Enabled", number);
  snprintf(number, sizeof(number), "%d", settings->rewind_depth);
  launcher_ini_kv_write(g_config_path, "Rewind", "Depth", number);
  snprintf(number, sizeof(number), "%d", settings->rewind_interval);
  launcher_ini_kv_write(g_config_path, "Rewind", "Interval", number);
}

static int resolve_rom(const char *executable, const char *explicit_rom,
                       bool force_launcher, char *path, size_t path_size,
                       RecompLauncherCSettings *settings) {
  bool bundled_music = FzeroMsuHasInstalledMusic();
  memset(settings, 0, sizeof(*settings));
  settings->window_scale = 3;
  settings->enable_audio = 1;
  settings->audio_freq = 32040;
  settings->volume = 100;
  settings->player_src[0] = 1;
  settings->deadzone[0] = 25;
  settings->rewind_enabled = 1;
  settings->rewind_depth = 50;
  settings->rewind_interval = 15;
  settings->aspect_index = (int)g_video.aspect;
  /* The built-in mod owns native presentation settings. */
  settings->adaptive_view = 0;
  settings->widescreen_hud = 0;
  /* Before either exit below: a run with a ROM on the command line skips the
   * launcher entirely, and must still honour what the player saved. */
  load_launcher_settings(settings);
  {
    const char *shader_override = getenv("FZERO_SHADER");
    if (shader_override && shader_override[0])
      snprintf(settings->shader_path, sizeof(settings->shader_path), "%s", shader_override);
  }

  if (explicit_rom && !force_launcher) {
    snprintf(path, path_size, "%s", explicit_rom);
    return 1;
  }

  RecompLauncherCGameInfo game;
  memset(&game, 0, sizeof(game));
  launcher_profile_apply("snes", &game);
  game.name = "F-Zero";
  game.region = "(USA)";
  game.known_sha1_hex = kFzeroSha1;
  game.num_known_sha1 = 1;
  game.known_sha256 = &kFzeroSha256;
  game.num_known_sha256 = 1;
  game.num_players = 1;
  game.sram_path = "saves/fzero.srm";
  game.widescreen_supported = 0;
  game.adaptive_view_supported = 0;
  game.aspect_labels = kFzeroAspectLabels;
  game.num_aspect_labels = FZERO_ASPECT_COUNT;
  game.aspect_setting_label = "Aspect ratio";
  game.aspect_setting_help =
      "Used for stock presentation. The built-in Presentation mod's aspect "
      "option overrides this when that mod is enabled.";
  game.has_shader = 1;
  game.msu1_supported = 1;
  game.msu1_managed = 1;
  game.msu1_note = "Music loads from each pack's music folder. Match the course filename: courses/huckmine.zip uses music/huckmine.pcm. Missing songs use the course's SNES soundtrack. Menu and event music is configured in Mods.";
  game.mods = FzeroModsProvider(&g_video, kVideoConfig, bundled_music);
  game.rom_cache_path = "rom.cfg";
  /* Draws the Controls page's SaveStateMenu and Rewind rows, and the
   * Settings page's rewind enable / depth / interval controls. The hotkey
   * editor rewrites [KeyMap] in this file, which is the one resolve_hotkey
   * reads back. */
  game.config_path = g_config_path;
  game.has_rewind_depth = 1;
  game.assist_binding_labels = kQuickLabels;
  game.assist_binding_count = 2;

  char initial_rom[1024] = {0};
  char assets_dir[1024] = ".";
  if (executable && executable[0]) {
    snprintf(assets_dir, sizeof(assets_dir), "%s", executable);
    char *slash = strrchr(assets_dir, '/');
    char *backslash = strrchr(assets_dir, '\\');
    char *separator = slash > backslash ? slash : backslash;
    if (separator)
      *separator = '\0';
    else
      snprintf(assets_dir, sizeof(assets_dir), "%s", ".");
  }
  if (explicit_rom) {
    snprintf(initial_rom, sizeof(initial_rom), "%s", explicit_rom);
  } else {
    FILE *probe = fopen("fzero.sfc", "rb");
    if (probe) {
      fclose(probe);
      snprintf(initial_rom, sizeof(initial_rom), "%s", "fzero.sfc");
    } else {
      snesrecomp_rom_cache_read(initial_rom, sizeof(initial_rom));
    }
  }

  if (settings->skip_launcher && !force_launcher && initial_rom[0]) {
    size_t size = 0;
    uint8_t *rom = read_rom(initial_rom, &size);
    bool valid = rom && verify_rom(rom, size);
    free(rom);
    if (valid) {
      snprintf(path, path_size, "%s", initial_rom);
      fprintf(stderr, "[fzero-launcher] skipped: verified remembered ROM\n");
      return 1;
    }
    fprintf(stderr, "[fzero-launcher] remembered ROM unavailable or invalid; opening launcher\n");
  }

  fprintf(stderr, "[fzero-launcher] opening%s\n", force_launcher ? " (--launcher)" : "");
  game.custom_content = FzeroContentProvider("mods", assets_dir);
  int action =
      recomp_launcher_run_window("F-Zero \xE2\x80\x94 Launcher", settings,
                                 &game, assets_dir, initial_rom, path,
                                 path_size);
  int content_changed = FzeroContentShutdown();
  /* Whatever the launcher did, keep what the player chose there. Quitting is
   * as good a moment to persist as pressing Play. */
  save_launcher_settings(settings);
  /* Mod choices are settings too. recomp-ui commits its provider on Play;
   * persist a typed resolution (and other mod choices) when quitting as well. */
  if (!FzeroTracksSave())
    fprintf(stderr, "[fzero-launcher] unable to save track-pack settings: %s\n", FzeroTracksError());
  if (!FzeroVideoSave(&g_video, kVideoConfig))
    fprintf(stderr, "[fzero-launcher] unable to save video/mod settings\n");
  if (content_changed) {
    char content_settings_root[1024];
    snprintf(content_settings_root, sizeof(content_settings_root), "%s", FzeroTracksRoot());
    FzeroTracksInit(content_settings_root,
#ifdef FZERO_HAS_DELUXE
                   true
#else
                   false
#endif
                   );
  }
  if (action == 1) return 0;
  if (action == 0 && path[0]) return 1;
  if (initial_rom[0]) {
    snprintf(path, path_size, "%s", initial_rom);
    return 1;
  }
  return -1;
}

static FzeroAspect launcher_aspect(int index) {
  if (index < 0 || index >= FZERO_ASPECT_COUNT) return FZERO_ASPECT_STOCK;
  return (FzeroAspect)index;
}

static uint32_t keyboard_input_from_keys(const uint8_t *keys) {
  /* keybinds_read_player() is the engine's shared keybinds.ini reader, so the
   * launcher's Controls page and the game agree on the bindings. Its bitmask
   * layout (keybinds.h) is bit0=R,1=L,2=X,3=A,4=Right,5=Left,6=Down,7=Up,
   * 8=Start,9=Select,10=Y,11=B; this host's own input word packs B=bit0,
   * Y=bit1,Select=bit2,Start=bit3,Up=bit4,Down=bit5,Left=bit6,Right=bit7,
   * A=bit8,X=bit9,L=bit10,R=bit11 (the convention controller_input() ORs
   * into). Map by button identity rather than relying on the fact that it
   * happens to be a straight bit reversal. */
  static const uint8_t kKbBitForInputBit[12] = {
      11, /* input bit0  B      <- keybinds bit11 B      */
      10, /* input bit1  Y      <- keybinds bit10 Y      */
      9,  /* input bit2  Select <- keybinds bit9  Select */
      8,  /* input bit3  Start  <- keybinds bit8  Start  */
      7,  /* input bit4  Up     <- keybinds bit7  Up     */
      6,  /* input bit5  Down   <- keybinds bit6  Down   */
      5,  /* input bit6  Left   <- keybinds bit5  Left   */
      4,  /* input bit7  Right  <- keybinds bit4  Right  */
      3,  /* input bit8  A      <- keybinds bit3  A      */
      2,  /* input bit9  X      <- keybinds bit2  X      */
      1,  /* input bit10 L      <- keybinds bit1  L      */
      0,  /* input bit11 R      <- keybinds bit0  R      */
  };
  uint16_t kb = keybinds_read_player(keys, 1); /* F-Zero is single-player */
  uint32_t input = 0;
  for (int i = 0; i < 12; i++)
    if ((kb >> kKbBitForInputBit[i]) & 1) input |= (1u << i);
  return input;
}

static uint32_t keyboard_input(void) {
  return keyboard_input_from_keys(snesrecomp_sdl_get_keyboard_state());
}

static uint32_t keyboard_guest_input(void) {
  uint8_t keys[SDL_SCANCODE_COUNT];
  memcpy(keys, snesrecomp_sdl_get_keyboard_state(), sizeof(keys));
  for (int i = 0; i < 2; ++i)
    if (g_quick_keys[i] > 0 && g_quick_keys[i] < SDL_SCANCODE_COUNT) keys[g_quick_keys[i]] = 0;
  return keyboard_input_from_keys(keys);
}

static uint32_t quick_actions(SDL_GameController *pad) {
  uint32_t active = 0;
  (void)FzeroGamepadReadShortcuts(pad, g_quick_pads, 2, &active);
  const uint8_t *keys = snesrecomp_sdl_get_keyboard_state();
  for (int i = 0; i < 2; ++i)
    if (g_quick_keys[i] > 0 && g_quick_keys[i] < SDL_SCANCODE_COUNT && keys[g_quick_keys[i]]) active |= 1u << i;
  return active;
}

static uint32_t controller_input(SDL_GameController *pad) {
  return FzeroGamepadRead(pad);
}

/* The launcher's Volume slider, 0..100. Applied to the rendered block rather
 * than to the device, so it works identically on both SDL generations and on
 * whatever backend the player has. */
static int g_audio_volume = 100;

static void fill_audio(Uint8 *stream, int len) {
  if (!g_snes || len < 4) {
    SDL_memset(stream, 0, (size_t)len);
    return;
  }
  RtlRenderAudio((int16_t *)stream, len / 4, 2);
  if (g_audio_volume < 100) {
    int16_t *samples = (int16_t *)stream;
    int count = len / (int)sizeof(int16_t);
    int gain = g_audio_volume < 0 ? 0 : g_audio_volume;
    for (int i = 0; i < count; i++)
      samples[i] = (int16_t)((samples[i] * gain) / 100);
  }
}

#if SNESRECOMP_SDL3
/* SDL3 replaced the pull callback with an SDL_AudioStream the app pushes into.
 * The stream sizes each pull itself, so render into a scratch buffer grown on
 * demand and hand the whole block over. */
static SDL_AudioStream *g_audio_stream;
static Uint8 *g_audio_scratch;
static size_t g_audio_scratch_size;

static void SDLCALL audio_stream_callback(void *userdata,
                                          SDL_AudioStream *stream,
                                          int additional_amount,
                                          int total_amount) {
  (void)userdata;
  (void)total_amount;
  if (additional_amount <= 0) return;

  /* Keep a cushion queued rather than feeding exactly what was asked for:
   * SDL3 pulls in whatever chunk the backend wants (~10 ms), and with no
   * reserve any hitch in the APU underruns immediately and is audible as
   * static. Top the queue back up to the historical 1024-frame depth; the
   * surplus is not lost, the stream keeps it for the next pull. */
  const int frame_bytes = 2 * (int)sizeof(int16_t); /* stereo S16 */
  const int target_queued = 1024 * frame_bytes;     /* 32 ms @ 32040 Hz */
  int queued = SDL_GetAudioStreamQueued(stream);
  if (queued < 0) queued = 0;
  int want_bytes = additional_amount;
  if (target_queued - queued > want_bytes) want_bytes = target_queued - queued;
  /* Byte counts "might be slightly overestimated" and nothing promises frame
   * alignment; a partial frame would permanently swap L/R. */
  want_bytes = ((want_bytes + frame_bytes - 1) / frame_bytes) * frame_bytes;

  if ((size_t)want_bytes > g_audio_scratch_size) {
    Uint8 *resized = (Uint8 *)realloc(g_audio_scratch, (size_t)want_bytes);
    if (!resized) return;
    g_audio_scratch = resized;
    g_audio_scratch_size = (size_t)want_bytes;
  }
  fill_audio(g_audio_scratch, want_bytes);
  SDL_PutAudioStreamData(stream, g_audio_scratch, want_bytes);
}
#else
static void SDLCALL audio_callback(void *userdata, Uint8 *stream, int len) {
  (void)userdata;
  fill_audio(stream, len);
}
#endif

static void wait_until(double deadline) {
  double frequency = (double)SDL_GetPerformanceFrequency();
  double target = deadline * frequency;
  double now = (double)SDL_GetPerformanceCounter();
  while (now < target) {
    double remaining_ms = (target - now) * 1000.0 / frequency;
    if (remaining_ms > 1.5)
      SDL_Delay((Uint32)(remaining_ms - 0.5));
    else
      SDL_Delay(0);
    now = (double)SDL_GetPerformanceCounter();
  }
}

static double monotonic_seconds(void) {
  return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
}

static double display_refresh(SDL_Window *window) {
#if SNESRECOMP_SDL3
  const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
  return mode ? mode->refresh_rate : 0;
#else
  SDL_DisplayMode mode;
  return SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(window), &mode) == 0 ? mode.refresh_rate : 0;
#endif
}

static int write_frame_bmp(const char *path, const uint8_t *pixels, int width,
                           int height) {
  if (!path || !path[0]) return 1;
  FILE *stream = fopen(path, "wb");
  if (!stream) return 0;
  uint32_t image_size = (uint32_t)width * (uint32_t)height * 4u;
  uint32_t pixel_offset = 54;
  uint32_t file_size = pixel_offset + image_size;
  uint8_t header[54] = {'B', 'M'};
  uint32_t dib_size = 40;
  int32_t bmp_width = width;
  int32_t bmp_height = -height;
  uint16_t planes = 1;
  uint16_t bits_per_pixel = 32;
  memcpy(header + 2, &file_size, 4);
  memcpy(header + 10, &pixel_offset, 4);
  memcpy(header + 14, &dib_size, 4);
  memcpy(header + 18, &bmp_width, 4);
  memcpy(header + 22, &bmp_height, 4);
  memcpy(header + 26, &planes, 2);
  memcpy(header + 28, &bits_per_pixel, 2);
  memcpy(header + 34, &image_size, 4);
  int ok = fwrite(header, 1, sizeof(header), stream) == sizeof(header) &&
           fwrite(pixels, 1, image_size, stream) == image_size;
  if (fclose(stream) != 0) ok = 0;
  return ok;
}

/* Uint64 deadlines: SDL3's SDL_GetTicks returns Uint64 and SDL_TICKS_PASSED is
 * gone, so compare absolute deadlines directly. Widening is harmless under
 * SDL2, where SDL_GetTicks returns Uint32. */
static void set_title_message(SDL_Window *window, const char *message,
                              Uint64 *until) {
  char title[192];
  snprintf(title, sizeof(title), "%s - %s", kWindowTitle, message);
  SDL_SetWindowTitle(window, title);
  *until = (Uint64)SDL_GetTicks() + 2500u;
}

static void set_state_feedback(SDL_Window *window, const char *operation,
                               int slot, int ok, Uint64 *until) {
  char message[160];
  if (slot == SNES_SAVESTATE_QUICK_SLOT)
    snprintf(message, sizeof(message), "Quick %s %s", operation, ok ? "succeeded" : "failed");
  else
    snprintf(message, sizeof(message), "State %s %s (slot %d)", operation,
             ok ? "succeeded" : "failed", slot + 1);
  set_title_message(window, message, until);
  snes_osd_push(message, 0);
}

static int perform_state_action(SDL_Window *window, int save, int slot,
                                Uint64 *feedback_until) {
  /* 256, matching the framework's own slot browser: the engine's save root is
   * 96 bytes and a prefixed slot name is short, but a 128-byte buffer here
   * silently truncated a long SNESRECOMP_SAVE_ROOT during testing. */
  char path[256];
  if (save) RtlEnsureSaveDir();
  RtlSaveSlotPath(slot, path, sizeof(path));
  int ok;
  if (save) {
    ok = RtlSaveSnapshot(path);
    if (ok) snes_savestate_menu_note_slot_saved(slot);
  } else if (!FzeroStateFileAcceptable(path)) {
    /* Checked before the engine is called at all. RtlLoadSnapshot applies the
     * guest blob before the game's trailer is read, so a snapshot taken on
     * the other cartridge would already be in RAM by the time anything could
     * object — and the two modes' ROMs do not agree about what that RAM
     * means. Stock and Deluxe slots live in different directories under
     * different prefixes, so this only fires on a file moved there by hand,
     * but that is precisely the case that used to crash. */
    FzeroStateMode mode = kFzeroStateModeUnknown;
    if (FzeroStateFileMode(path, &mode))
      fprintf(stderr,
              "[fzero-state] slot %d holds a %s snapshot; this session is %s\n",
              slot + 1, FzeroStateModeName(mode),
              FzeroStateModeName(FzeroStateModeCurrent()));
    ok = 0;
  } else {
    ok = RtlLoadSnapshot(path);
  }
  FzeroDiagnosticsEvent(save ? (ok ? "save" : "save_failed") : (ok ? "load" : "load_failed"), slot);
  if (!save && ok) g_reset_presentation_clock = true;
  set_state_feedback(window, save ? "save" : "load", slot, ok, feedback_until);
  fprintf(stderr, "[fzero-state] %s slot %d: %s\n", save ? "save" : "load",
          slot + 1, ok ? "ok" : "FAILED");
  return ok;
}

/*
 * FZERO_STATE_SAVE_AT / FZERO_STATE_LOAD_AT = <frame>[:<slot>]
 *
 * Drive one slot operation from a scripted run. The quick-slot keys and the
 * browser are the two ways a player reaches a save state, and neither can be
 * exercised without a person at the keyboard — but the cartridge guard on a
 * load only matters against a file some OTHER session wrote, which no
 * single-process self-test can produce. These two hooks let a harness write a
 * state in one run and try to load it in the next, which is exactly the shape
 * of the crossing they defend against. They go through perform_state_action,
 * so they test the shipped path rather than a parallel one. */
typedef struct FzeroScriptedState {
  long frame;
  int slot;
} FzeroScriptedState;

static FzeroScriptedState parse_scripted_state(const char *name) {
  FzeroScriptedState out = {-1, 0};
  const char *v = getenv(name);
  if (!v || !v[0]) return out;
  char *end = NULL;
  long frame = strtol(v, &end, 0);
  if (end == v || frame < 0) {
    fprintf(stderr, "[fzero-state] %s: expected <frame>[:<slot>]\n", name);
    return out;
  }
  out.frame = frame;
  if (end && *end == ':') {
    long slot = strtol(end + 1, NULL, 0);
    if (slot >= 0 && slot < 12) out.slot = (int)slot;
  }
  return out;
}

static void update_state_feedback(SDL_Window *window, Uint64 *feedback_until) {
  if (*feedback_until && (Uint64)SDL_GetTicks() >= *feedback_until) {
    SDL_SetWindowTitle(window, kWindowTitle);
    *feedback_until = 0;
  }
}

/* ── System hotkeys ───────────────────────────────────────────────────────
 *
 * The save-state browser and rewind are bound through the launcher's Controls
 * page, which writes config.ini's [KeyMap] the same way the framework desktop
 * host reads it. The defaults match what recomp-ui shows: F7 opens the
 * browser, R opens rewind.
 *
 * These win over the F-key quick slots. F1..F12 (and Shift+F1..F12 to save)
 * are the older, undiscoverable way of doing this and are on their way out;
 * where a hotkey claims a key, the hotkey gets it, and the slot behind it is
 * still reachable — with a thumbnail and a name — from the browser itself.
 * Rebinding the hotkey elsewhere hands the plain key straight back. */
typedef struct FzeroHotkey {
  int bound;
  SDL_Keycode key;
  unsigned mods;
} FzeroHotkey;

static FzeroHotkey g_hotkey_menu;
static FzeroHotkey g_hotkey_rewind;

static FzeroHotkey resolve_hotkey(const char *name, const char *fallback) {
  FzeroHotkeySpec spec;
  FzeroHotkey out;
  memset(&out, 0, sizeof(out));
  if (!FzeroHotkeyFromIni(g_config_path, name, &spec))
    FzeroHotkeyParse(fallback, &spec);
  if (!spec.bound) return out;
  SDL_Keycode key = SDL_GetKeyFromName(spec.key);
  if (key == SDLK_UNKNOWN) {
    fprintf(stderr, "[fzero] [KeyMap] %s: unknown key \"%s\"\n", name, spec.key);
    return out;
  }
  out.bound = 1;
  out.key = key;
  out.mods = spec.mods;
  return out;
}

static int hotkey_matches(const FzeroHotkey *hk, SDL_Keycode key, Uint16 mod) {
  if (!hk->bound || hk->key != key) return 0;
  /* Exact modifier match, so Shift+F7 (save slot 7) is not swallowed by a
   * plain-F7 binding. */
  unsigned have = 0;
  if (mod & KMOD_SHIFT) have |= FZERO_HOTKEY_MOD_SHIFT;
  if (mod & KMOD_CTRL) have |= FZERO_HOTKEY_MOD_CTRL;
  if (mod & KMOD_ALT) have |= FZERO_HOTKEY_MOD_ALT;
  return have == hk->mods;
}

/* ── In-game overlays ─────────────────────────────────────────────────────
 *
 * Both panels are framework modules: which slot is selected, what the panel
 * looks like and the save/load itself live in snesrecomp/runner/src
 * (snes_savestate_menu.c, snes_rewind.c). This host supplies the two things a
 * framework module cannot — SDL events, and pixels on the screen.
 *
 * F-Zero has two presenters, an SDL_Renderer and a GL path with a shader
 * preset, and the overlay has to reach the screen on both. It is drawn as its
 * own layer in each rather than composited into the 256/684-wide game buffer,
 * so the panel lands at window resolution and its text stays crisp at 21:9 as
 * well as at 4:3.
 *
 * The guest is FROZEN while a panel is up: the modal loops below never call
 * RtlRunFrame, which is the only way "save right here" names a definite point
 * in time. */
typedef struct FzeroPresenter {
  SDL_Window *window;
  SDL_Renderer *renderer; /* NULL on the GL path */
  SDL_Texture *texture;
  FzeroGlRenderer *gl;    /* NULL on the SDL_Renderer path */
  const uint8_t *pixels;
  int logical_width;
  FzeroViewport viewport;
  int drawable_width, drawable_height;
} FzeroPresenter;

static SDL_Texture *g_overlay_texture;
static int g_overlay_texture_w, g_overlay_texture_h;
static SDL_Texture *g_osd_texture;
static int g_osd_texture_w, g_osd_texture_h;

/* The browser keeps a centered 4:3 display aspect. The filmstrip annotates
 * the game frame across its bottom third. */
static FzeroRect overlay_rect(const FzeroPresenter *p, int is_menu) {
  FzeroRect game =
      FzeroDestination(p->viewport, p->drawable_width, p->drawable_height);
  if (is_menu) {
    snes_savestate_menu_viewport(p->drawable_width, p->drawable_height,
                                &game.x, &game.y, &game.w, &game.h);
    return game;
  }
  int strip = game.h / 3;
  if (strip < 1) strip = 1;
  game.y += game.h - strip;
  game.h = strip;
  return game;
}

static void overlay_draw_sdl(const FzeroPresenter *p, const uint32_t *panel,
                             int pw, int ph, int is_menu) {
  if (!panel || pw <= 0 || ph <= 0) return;
  if (g_overlay_texture &&
      (g_overlay_texture_w != pw || g_overlay_texture_h != ph)) {
    SDL_DestroyTexture(g_overlay_texture);
    g_overlay_texture = NULL;
  }
  if (!g_overlay_texture) {
    g_overlay_texture =
        SDL_CreateTexture(p->renderer, SDL_PIXELFORMAT_ARGB8888,
                          SDL_TEXTUREACCESS_STREAMING, pw, ph);
    if (!g_overlay_texture) return;
    g_overlay_texture_w = pw;
    g_overlay_texture_h = ph;
    /* The browser's panel is opaque, but the filmstrip is not; and the SNES
     * framebuffer's zero alpha already cost this host one blended-away frame
     * (see snesrecomp_sdl_set_texture_opaque below), so say what these
     * pixels mean rather than inheriting a default. */
    SDL_SetTextureBlendMode(g_overlay_texture, SDL_BLENDMODE_BLEND);
  }
  SDL_UpdateTexture(g_overlay_texture, NULL, panel, pw * 4);
  FzeroRect r = overlay_rect(p, is_menu);
  SDL_Rect destination = {r.x, r.y, r.w, r.h};
  snesrecomp_sdl_render_texture(p->renderer, g_overlay_texture, NULL,
                                &destination);
}

/* Read the composited window back for FZERO_OVERLAY_DUMP / FZERO_REWIND_DUMP;
 * defined below, called from present_frame before the buffers are swapped —
 * after a present, what a read-back returns is undefined. */
static void overlay_dump(const FzeroPresenter *p, int is_menu);

static void present_osd(const FzeroPresenter *p) {
  const uint32_t *image = NULL;
  int w = 0, h = 0;
  if (snes_osd_image(&image, &w, &h)) {
    int scale = p->drawable_height / 448;
    if (scale < 1) scale = 1;
    FzeroRect rect = {12, 12, w * scale, h * scale};
    if (p->gl) {
      fzero_gl_draw_overlay(p->gl, image, w, h, rect, p->drawable_height);
    } else {
      if (g_osd_texture && (g_osd_texture_w != w || g_osd_texture_h != h)) {
        SDL_DestroyTexture(g_osd_texture);
        g_osd_texture = NULL;
      }
      if (!g_osd_texture) {
        g_osd_texture = SDL_CreateTexture(p->renderer, SDL_PIXELFORMAT_ARGB8888,
                                           SDL_TEXTUREACCESS_STREAMING, w, h);
        g_osd_texture_w = w; g_osd_texture_h = h;
        if (g_osd_texture) SDL_SetTextureBlendMode(g_osd_texture, SDL_BLENDMODE_BLEND);
      }
      if (g_osd_texture) {
        SDL_UpdateTexture(g_osd_texture, NULL, image, w * 4);
        SDL_Rect destination = {rect.x, rect.y, rect.w, rect.h};
        snesrecomp_sdl_render_texture(p->renderer, g_osd_texture, NULL, &destination);
      }
    }
  }
  snes_osd_present_done();
}

/* One present, with an optional panel over it. The game image is whatever is
 * already in `pixels`: while a panel is up the guest is frozen, so the frame
 * behind it is the moment the player stopped at. */
static void present_frame(const FzeroPresenter *p, const uint32_t *panel,
                          int pw, int ph, int is_menu) {
  const uint32_t *hd_frame = FzeroHdFrame();
  unsigned scale = FzeroHdScale();
  const uint8_t *pixels = hd_frame ? (const uint8_t *)hd_frame : p->pixels;
  int width = p->logical_width * (int)scale;
  int height = kFrameHeight * (int)scale;
  if (p->gl) {
    fzero_gl_render(p->gl, pixels, width, height, p->viewport,
                    p->drawable_width, p->drawable_height);
    if (panel) {
      fzero_gl_draw_overlay(p->gl, panel, pw, ph, overlay_rect(p, is_menu),
                            p->drawable_height);
    }
    if (!panel || !is_menu) present_osd(p);
    if (panel) overlay_dump(p, is_menu);
    uint64_t diagnostic_start = FzeroDiagnosticsBegin();
    SDL_GL_SwapWindow(p->gl->window);
    FzeroDiagnosticsEnd(FZERO_DIAG_PRESENT, diagnostic_start);
    return;
  }
  SDL_Rect source = {0, 0, width, height};
  uint64_t diagnostic_start = FzeroDiagnosticsBegin();
  SDL_UpdateTexture(p->texture, &source, pixels, width * kBytesPerPixel);
  FzeroDiagnosticsEnd(FZERO_DIAG_UPLOAD, diagnostic_start);
  diagnostic_start = FzeroDiagnosticsBegin();
  SDL_SetRenderDrawColor(p->renderer, 0, 0, 0, 255);
  SDL_RenderClear(p->renderer);
  FzeroRect rect =
      FzeroDestination(p->viewport, p->drawable_width, p->drawable_height);
  SDL_Rect destination = {rect.x, rect.y, rect.w, rect.h};
  snesrecomp_sdl_render_texture(p->renderer, p->texture, &source, &destination);
  overlay_draw_sdl(p, panel, pw, ph, is_menu);
  if (!panel || !is_menu) present_osd(p);
  if (panel) overlay_dump(p, is_menu);
  FzeroDiagnosticsEnd(FZERO_DIAG_DRAW, diagnostic_start);
  diagnostic_start = FzeroDiagnosticsBegin();
  SDL_RenderPresent(p->renderer);
  FzeroDiagnosticsEnd(FZERO_DIAG_PRESENT, diagnostic_start);
}

/*
 * FZERO_OVERLAY_DUMP=<path> / FZERO_REWIND_DUMP=<path>: write the composited
 * window as a PPM the first time that panel is presented.
 *
 * The panels can only be driven by a person, so this is the only way to check
 * that one actually reaches the screen rather than inferring it from the
 * module reporting itself open — and it reads back the real output of
 * whichever presenter is in use, so it covers the SDL_Renderer path and the
 * GL path separately rather than assuming they agree.
 */
static void write_ppm(const char *path, const uint8_t *argb, int w, int h,
                      int bottom_up, int pitch) {
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (int y = 0; y < h; y++) {
    const uint8_t *row = argb + (size_t)(bottom_up ? h - 1 - y : y) * (size_t)pitch;
    for (int x = 0; x < w; x++) {
      /* Little-endian ARGB/BGRA byte order: B, G, R, A. */
      fputc(row[x * 4 + 2], f);
      fputc(row[x * 4 + 1], f);
      fputc(row[x * 4 + 0], f);
    }
  }
  fclose(f);
}

static void overlay_dump(const FzeroPresenter *p, int is_menu) {
  static int dumped_menu = 0, dumped_rewind = 0;
  int *dumped = is_menu ? &dumped_menu : &dumped_rewind;
  const char *path =
      getenv(is_menu ? "FZERO_OVERLAY_DUMP" : "FZERO_REWIND_DUMP");
  if (!path || !path[0] || *dumped) return;
  const int w = p->drawable_width, h = p->drawable_height;
  if (w <= 0 || h <= 0) return;

  if (p->gl) {
    uint8_t *buf = (uint8_t *)malloc((size_t)w * (size_t)h * 4u);
    if (!buf) return;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, buf);
    write_ppm(path, buf, w, h, 1, w * 4); /* GL reads bottom-up */
    free(buf);
  } else {
#if SNESRECOMP_SDL3
    SDL_Surface *shot = SDL_RenderReadPixels(p->renderer, NULL);
    if (!shot) return;
    SDL_Surface *argb = shot->format == SDL_PIXELFORMAT_ARGB8888
                            ? shot
                            : SDL_ConvertSurface(shot, SDL_PIXELFORMAT_ARGB8888);
    if (argb) {
      write_ppm(path, (const uint8_t *)argb->pixels, argb->w, argb->h, 0,
                argb->pitch);
      if (argb != shot) SDL_DestroySurface(argb);
    }
    SDL_DestroySurface(shot);
#else
    uint8_t *buf = (uint8_t *)malloc((size_t)w * (size_t)h * 4u);
    if (!buf) return;
    if (SDL_RenderReadPixels(p->renderer, NULL, SDL_PIXELFORMAT_ARGB8888, buf,
                             w * 4) == 0)
      write_ppm(path, buf, w, h, 0, w * 4);
    free(buf);
#endif
  }
  *dumped = 1;
  fprintf(stderr, "[fzero-overlay] wrote %s (%dx%d, %s)\n", path, w, h,
          is_menu ? "save-state browser" : "rewind filmstrip");
}

static void present_overlay(const FzeroPresenter *p, int is_menu) {
  const uint32_t *panel = NULL;
  int pw = 0, ph = 0;
  int have = is_menu ? snes_savestate_menu_overlay_image(&panel, &pw, &ph)
                     : snes_rewind_overlay_image(&panel, &pw, &ph);
  present_frame(p, have ? panel : NULL, pw, ph, is_menu);
}

/* Buttons still held when a panel closed, masked from the guest until each is
 * released. The button that closed the panel must not also act in the game:
 * B closes rewind, and on the F-Zero title screen B is "confirm". The browser
 * module carries this guard for itself; the rewind module does not, so the
 * host applies one to both. */
static uint32_t g_overlay_release_mask;

static uint32_t overlay_filter_guest_input(uint32_t inputs) {
  g_overlay_release_mask &= inputs; /* a released button drops out */
  return inputs & ~g_overlay_release_mask;
}

static uint32_t overlay_nav_inputs(SDL_GameController *pad) {
  return keyboard_input() | controller_input(pad);
}

/* Pad gestures, for a player who never touches the keyboard. Select + R opens
 * the browser — the framework module edge-detects that one itself, on exactly
 * these bits. Select + L opens rewind, alongside it on the other shoulder.
 * Both take two buttons on purpose: one ordinary button in the middle of a
 * race is not a gesture, it is a boost. */
#define FZERO_MENU_GESTURE (SNES_PAD_SELECT | SNES_PAD_R)
#define FZERO_REWIND_GESTURE (SNES_PAD_SELECT | SNES_PAD_L)

static int rewind_gesture_pressed(uint32_t input) {
  static int held;
  int down = (input & FZERO_REWIND_GESTURE) == FZERO_REWIND_GESTURE;
  int edge = down && !held;
  held = down;
  return edge;
}

/* The overlays' event pump. Quit ends the run; a key press goes to the panel
 * and never to the game's own hotkeys — F3 loading a state behind a browser
 * that is asking which state to load is the bug this prevents. */
static void overlay_pump_events(int *running, SDL_GameController **pad,
                                void (*key_down)(int key, int repeat)) {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_QUIT) {
      *running = 0;
      continue;
    }
    if (event.type == SDL_KEYDOWN) {
      key_down((int)SNESRECOMP_SDL_EVENT_KEY(event), event.key.repeat ? 1 : 0);
      continue;
    }
    /* A pad plugged in while a panel is up must still be able to drive it. */
    FzeroGamepadEvent(pad, &event);
  }
}

/* ── Overlay self-test ────────────────────────────────────────────────────
 *
 * FZERO_OVERLAY_SELFTEST=<frame>: attach a virtual gamepad and drive both
 * panels with it, so the whole path from SDL event to pixels is exercised —
 * device open, the gestures, the modal pumps, and the resume afterwards.
 * Injecting an input word instead would enter below the event layer and prove
 * nothing about a controller, which is where the interesting failures are.
 *
 * Sequence: at <frame> hold Select+R (the browser gesture); inside the
 * browser release, press Down, then B to close. 60 frames later hold Select+L
 * (the rewind gesture) and do the same with Left. Each panel must open, and
 * close from the pad within 40 pumps, or the run prints FAIL.
 *
 * Ported from the framework desktop host's OVERLAY_SELFTEST_PAD, adapted to
 * this host's gestures. */
static SDL_Joystick *g_selftest_pad;
static char g_selftest_guid[40];
static long g_selftest_frame = -1;
static bool g_menu_pad_replay;
static int g_selftest_phase; /* 0 idle, 1 browser, 2 rewind */
static int g_selftest_via_keyboard;
/* bit 0 browser from the pad, bit 1 rewind from the pad, bit 2 browser from
 * the keyboard. */
static int g_selftest_opened;
static int g_selftest_failed;

static void selftest_set(int button, int down) {
  if (!g_selftest_pad) return;
#if SNESRECOMP_SDL3
  SDL_SetJoystickVirtualButton(g_selftest_pad, button, down != 0);
#else
  SDL_JoystickSetVirtualButton(g_selftest_pad, button,
                               down ? SDL_PRESSED : SDL_RELEASED);
#endif
}

static void selftest_release_all(void) {
  for (int b = 0; b < 15; b++) selftest_set(b, 0);
}

/* A synthetic key press, so the keyboard binding is tested through the same
 * event pump a real one goes through — hotkey_matches included. */
static void selftest_push_key(SDL_Keycode key) {
  SDL_Event e;
  memset(&e, 0, sizeof(e));
  e.type = SDL_KEYDOWN;
#if SNESRECOMP_SDL3
  e.key.key = key;
  e.key.scancode = SDL_GetScancodeFromKey(key, NULL);
  e.key.down = true;
  e.key.repeat = false;
#else
  e.key.keysym.sym = key;
  e.key.state = SDL_PRESSED;
  e.key.repeat = 0;
#endif
  SDL_PushEvent(&e);
}

static void selftest_attach(void) {
  const char *v = getenv("FZERO_OVERLAY_SELFTEST");
  g_selftest_frame = v && v[0] ? strtol(v, NULL, 0) : -1;
  g_menu_pad_replay = getenv("FZERO_MENU_PAD_REPLAY") != NULL;
  if (g_selftest_frame < 0 && !g_menu_pad_replay) return;
#if SNESRECOMP_SDL3
  SDL_VirtualJoystickDesc desc;
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
  desc.nbuttons = 15;
  desc.name = "F-Zero overlay self-test pad";
  SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
  g_selftest_pad = id ? SDL_OpenJoystick(id) : NULL;
#else
  int index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,
                                        SDL_CONTROLLER_AXIS_MAX, 15, 0);
  g_selftest_pad = index >= 0 ? SDL_JoystickOpen(index) : NULL;
#endif
  fprintf(stderr, "[fzero-overlay-selftest] virtual gamepad %s\n",
          g_selftest_pad ? "attached" : "FAILED to attach");
  if (!g_selftest_pad) g_selftest_failed = 1;
  else {
#if SNESRECOMP_SDL3
    SDL_GUIDToString(SDL_GetJoystickGUID(g_selftest_pad), g_selftest_guid, sizeof(g_selftest_guid));
#else
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(g_selftest_pad), g_selftest_guid, sizeof(g_selftest_guid));
#endif
  }
}

/* Once per simulated frame. */
static void selftest_main_tick(long frame) {
  if (!g_selftest_pad) return;
  if (g_menu_pad_replay) {
    /* Exercise the actual SDL gamepad path, including the user's button
     * mapping, instead of substituting the final guest input word. */
    static const unsigned buttons[12] = {0,2,4,6,11,12,13,14,1,3,9,10};
    uint32_t input = FzeroReplayInput((unsigned)frame);
    selftest_release_all();
    for (unsigned bit = 0; bit < 12; ++bit)
      if (input & (1u << bit)) selftest_set((int)buttons[bit], 1);
#if SNESRECOMP_SDL3
    SDL_UpdateJoysticks();
#else
    SDL_JoystickUpdate();
#endif
    return;
  }
  if (frame == g_selftest_frame) {
    fprintf(stderr, "[fzero-overlay-selftest] frame %ld: Select+R\n", frame);
    g_selftest_phase = 1;
    selftest_set(SDL_CONTROLLER_BUTTON_BACK, 1);
    selftest_set(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 1);
  } else if (frame == g_selftest_frame + 60) {
    fprintf(stderr, "[fzero-overlay-selftest] frame %ld: Select+L\n", frame);
    g_selftest_phase = 2;
    selftest_set(SDL_CONTROLLER_BUTTON_BACK, 1);
    selftest_set(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 1);
  } else if (frame == g_selftest_frame + 120) {
    if (g_selftest_opened != 3) {
      fprintf(stderr,
              "[fzero-overlay-selftest] FAIL: %s never opened from the pad\n",
              !(g_selftest_opened & 1) ? "the save-state browser" : "rewind");
      g_selftest_failed = 1;
    } else {
      fprintf(stderr, "[fzero-overlay-selftest] ok: browser and rewind both "
                      "opened and closed from the pad\n");
    }
    /* Now the keyboard binding, through the event pump rather than the
     * gesture: the two reach the panel by different routes and a port has
     * shipped with one of them broken before. */
    fprintf(stderr,
            "[fzero-overlay-selftest] frame %ld: pressing the SaveStateMenu "
            "key\n",
            frame);
    g_selftest_phase = 1;
    g_selftest_via_keyboard = 1;
    if (g_hotkey_menu.bound)
      selftest_push_key(g_hotkey_menu.key);
    else {
      fprintf(stderr, "[fzero-overlay-selftest] FAIL: SaveStateMenu is "
                      "unbound\n");
      g_selftest_failed = 1;
    }
  } else if (frame == g_selftest_frame + 180) {
    if (!(g_selftest_opened & 4)) {
      fprintf(stderr, "[fzero-overlay-selftest] FAIL: the save-state browser "
                      "never opened from the keyboard\n");
      g_selftest_failed = 1;
    }
    fprintf(stderr, "[fzero-overlay-selftest] %s\n",
            g_selftest_failed
                ? "FAIL"
                : "ok: browser opened from pad and keyboard, rewind opened "
                  "from the pad, both closed, and the game resumed");
  } else {
    selftest_release_all();
  }
}

/* Once per modal pump, from inside either panel's loop. */
static void selftest_pump_tick(unsigned pump) {
  if (!g_selftest_pad || !g_selftest_phase) return;
  /* SDL's south button is the SNES B in this host's controller_input(), which
   * is the one that CLOSES both panels. The east button is the SNES A, which
   * loads in the browser and commits in rewind — the wrong one to test a
   * close with. */
  const int nav = g_selftest_phase == 1 ? SDL_CONTROLLER_BUTTON_DPAD_DOWN
                                        : SDL_CONTROLLER_BUTTON_DPAD_LEFT;
  if (pump == 0)
    g_selftest_opened |= g_selftest_via_keyboard ? 4 : g_selftest_phase;
  switch (pump) {
    case 2: selftest_release_all(); break;
    case 6: selftest_set(nav, 1); break;
    case 9: selftest_set(nav, 0); break;
    case 14: selftest_set(SDL_CONTROLLER_BUTTON_A, 1); break;
    case 40:
      fprintf(stderr,
              "[fzero-overlay-selftest] FAIL: %s did not close from the pad "
              "within 40 pumps\n",
              g_selftest_phase == 1 ? "the save-state browser" : "rewind");
      g_selftest_failed = 1;
      if (g_selftest_phase == 1)
        snes_savestate_menu_close();
      else
        snes_rewind_close();
      break;
    default: break;
  }
}

static void savestate_menu_loop(const FzeroPresenter *p, int *running,
                                SDL_GameController **pad) {
  /* The browser loads through the engine directly, so the host cannot check
   * the file first the way perform_state_action does. Arm the undo snapshot
   * instead: the guest is frozen from here until the browser closes, so this
   * one snapshot is the exact machine every load from it would replace. */
  FzeroStateGuardArm();
  unsigned pump = 0;
  fprintf(stderr, "[fzero-overlay] save-state browser OPEN - guest frozen "
                  "until it closes (pad B, or Escape on the keyboard)\n");
  while (snes_savestate_menu_is_open() && *running) {
    overlay_pump_events(running, pad, &snes_savestate_menu_handle_key);
    if (!*running) snes_savestate_menu_close();
    selftest_pump_tick(pump);
    snes_savestate_menu_poll_nav(overlay_nav_inputs(*pad),
                                 (uint32_t)SDL_GetTicks());
    present_overlay(p, 1);
    SDL_Delay(8);
    pump++;
  }
  fprintf(stderr, "[fzero-overlay] save-state browser CLOSED after %u pumps - "
                  "guest resuming\n", pump);
  FzeroStateGuardDisarm();
  g_overlay_release_mask |= overlay_nav_inputs(*pad);
}

static void rewind_key_down(int key, int repeat) {
  (void)repeat;
  switch (key) {
    case SDLK_LEFT: snes_rewind_step(-1); break;
    case SDLK_RIGHT: snes_rewind_step(+1); break;
    case SDLK_RETURN:
    case SDLK_SPACE: snes_rewind_commit(); break;
    case SDLK_ESCAPE:
    case SDLK_BACKSPACE: snes_rewind_close(); break;
    default: break;
  }
}

/* Rewind's own pump: snes_rewind exposes step/commit/close rather than the
 * browser's handle_key/poll_nav. Controls match the framework host — Left and
 * Right scrub (hold to keep scrubbing), Enter or Space commits, Escape
 * cancels, and the pad mirrors them. */
static void rewind_loop(const FzeroPresenter *p, int *running,
                        SDL_GameController **pad) {
  uint32_t prev_pad = 0, held_dir = 0, held_since = 0, last_repeat = 0;
  unsigned pump = 0;
  fprintf(stderr, "[fzero-overlay] rewind filmstrip OPEN - guest frozen until "
                  "it closes (pad B, or Escape; Left/Right scrub, A or Enter "
                  "commits)\n");
  while (snes_rewind_is_open() && *running) {
    overlay_pump_events(running, pad, &rewind_key_down);
    if (!*running) snes_rewind_close();
    selftest_pump_tick(pump);
    {
      const uint32_t now = (uint32_t)SDL_GetTicks();
      const uint32_t nav = overlay_nav_inputs(*pad);
      const uint32_t pressed = nav & ~prev_pad;
      const uint32_t dir = nav & (SNES_PAD_LEFT | SNES_PAD_RIGHT);
      if (pressed & SNES_PAD_LEFT) snes_rewind_step(-1);
      if (pressed & SNES_PAD_RIGHT) snes_rewind_step(+1);
      if (pressed & SNES_PAD_A) snes_rewind_commit();
      if (pressed & SNES_PAD_B) snes_rewind_close();
      /* Edge-triggered with hold-to-repeat: holding Left must not sprint
       * through the whole ring in one pass of this loop. */
      if (dir && dir != (SNES_PAD_LEFT | SNES_PAD_RIGHT)) {
        if (dir != held_dir) {
          held_dir = dir;
          held_since = now;
          last_repeat = now;
        } else if (now - held_since >= SNES_OVL_REPEAT_DELAY &&
                   now - last_repeat >= SNES_OVL_REPEAT_RATE) {
          snes_rewind_step((dir & SNES_PAD_LEFT) ? -1 : +1);
          last_repeat = now;
        }
      } else {
        held_dir = 0;
      }
      prev_pad = nav;
    }
    present_overlay(p, 0);
    SDL_Delay(8);
    pump++;
  }
  fprintf(stderr, "[fzero-overlay] rewind filmstrip CLOSED after %u pumps - "
                  "guest resuming\n", pump);
  g_overlay_release_mask |= overlay_nav_inputs(*pad);
}

/* The rewind ring reads its size and cadence from the environment
 * (SNESRECOMP_REWIND, _DEPTH, _INTERVAL) because that is the contract every
 * SNES port shares with it. The launcher's Settings page is this host's way
 * of writing those, so translate one into the other — and leave an
 * explicitly exported value alone, so a capture run or a bisect script still
 * overrides the UI. */
static void set_env_default(const char *name, const char *value) {
  const char *existing = getenv(name);
  if (existing && existing[0]) return;
#ifdef _WIN32
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}

static void configure_rewind(const RecompLauncherCSettings *settings) {
  char number[32];
  if (!settings->rewind_enabled) {
    set_env_default("SNESRECOMP_REWIND", "0");
    return;
  }
  set_env_default("SNESRECOMP_REWIND", "1");
  if (settings->rewind_depth > 0) {
    snprintf(number, sizeof(number), "%d", settings->rewind_depth);
    set_env_default("SNESRECOMP_REWIND_DEPTH", number);
  }
  if (settings->rewind_interval > 0) {
    snprintf(number, sizeof(number), "%d", settings->rewind_interval);
    set_env_default("SNESRECOMP_REWIND_INTERVAL", number);
  }
}

int main(int argc, char **argv) {
  SDL_SetMainReady();
  /* Launcher paths, imported shaders/music and saves are installation-relative,
   * even when a shortcut or terminal starts us in a different directory.
   * Resolve an explicit ROM against the caller's cwd before changing it. */
  char command_line_rom[1024];
  const char *explicit_rom = NULL;
  bool force_launcher = false, positional_only = false, profile_mode7 = false;
  for (int i = 1; i < argc; ++i) {
    if (!positional_only && !strcmp(argv[i], "--")) { positional_only = true; continue; }
    if (!positional_only && !strcmp(argv[i], "--launcher")) { force_launcher = true; continue; }
    if (!positional_only && !strcmp(argv[i], "--profile-mode7")) { profile_mode7 = true; continue; }
    if (explicit_rom || (!positional_only && argv[i][0] == '-')) {
      fprintf(stderr, "usage: FZeroSNESRecomp [--launcher] [--profile-mode7] [path-to-rom.sfc]\n");
      return 2;
    }
    explicit_rom = argv[i];
  }
  if (explicit_rom) {
    if (!snesrecomp_abspath(explicit_rom, command_line_rom, sizeof(command_line_rom))) {
      fprintf(stderr, "Unable to resolve the command-line ROM path\n");
      return 2;
    }
    explicit_rom = command_line_rom;
  }
  snesrecomp_anchor_to_exe_dir();
  const char *config_override = getenv("FZERO_VIDEO_CONFIG");
  if (config_override && *config_override) kVideoConfig = config_override;
  else {
    const char *base = SDL_GetBasePath();
    if (base && snprintf(video_config_path, sizeof(video_config_path), "%sfzero-video.ini", base) < (int)sizeof(video_config_path))
      kVideoConfig = video_config_path;
#if !SNESRECOMP_SDL3
    SDL_free((void *)base);
#endif
  }
  if (!FzeroVideoLoad(&g_video, kVideoConfig))
    fprintf(stderr, "[fzero] Invalid video settings; invalid fields use defaults\n");
#ifndef FZERO_HAS_DELUXE
  /* BS Deluxe is on by default, but a build configured without the native
   * module cannot honour it and FzeroDeluxePrepare would refuse to start. */
  if (g_video.bs_deluxe || g_video.bs_tracks) {
    fprintf(stderr, "[bs-deluxe] This build has no BS Deluxe module; starting stock\n");
    g_video.bs_deluxe = g_video.bs_tracks = false;
  }
#endif
  const char *track_root = getenv("FZERO_TRACK_PACKS");
  if (!FzeroTracksInit(track_root ? track_root : "mods/track-packs",
#ifdef FZERO_HAS_DELUXE
                       true
#else
                       false
#endif
                       )) { fprintf(stderr, "%s\n", FzeroTracksError()); return 2; }
  /* Exe-anchored, like fzero-video.ini: config.ini belongs next to the
   * executable so a launch from any working directory finds the same
   * settings and the same key bindings. */
  if (!snesrecomp_exe_dir_path("config.ini", g_config_path,
                               sizeof(g_config_path)))
    snprintf(g_config_path, sizeof(g_config_path), "config.ini");
  if (!FzeroReplayConfigure(getenv("SNESRECOMP_INPUT_SCRIPT"), getenv("FZERO_VIEWPORT_SCRIPT"))) {
    fprintf(stderr, "[fzero] Invalid validation replay\n");
    return 2;
  }
  host_report_init(kWindowTitle, kBuildVersion);
  char rom_path[1024] = {0};
  RecompLauncherCSettings launcher_settings;
  int resolve_result =
      resolve_rom(argv[0], explicit_rom, force_launcher, rom_path, sizeof(rom_path), &launcher_settings);
  if (resolve_result <= 0) return resolve_result == 0 ? 0 : 2;
  if (!g_video.enhanced) {
    g_video.aspect = launcher_aspect(launcher_settings.aspect_index);
    if (!FzeroVideoSave(&g_video, kVideoConfig))
      fprintf(stderr, "[fzero] Unable to save video settings\n");
  }
  g_config.linear_filtering = launcher_settings.linear_filter != 0;
  /* After the launcher, which may have just rewritten [KeyMap]. */
  g_hotkey_menu = resolve_hotkey("SaveStateMenu", "F7");
  g_hotkey_rewind = resolve_hotkey("Rewind", "R");
  memcpy(g_quick_keys, launcher_settings.assist_key_bind, sizeof(g_quick_keys));
  memcpy(g_quick_pads, launcher_settings.assist_pad_bind, sizeof(g_quick_pads));
  configure_rewind(&launcher_settings);
  size_t rom_size = 0;
  uint8_t *rom = read_rom(rom_path, &rom_size);
  if (!rom || !verify_rom(rom, rom_size)) {
    fprintf(stderr, "A verified F-Zero (USA) ROM is required: %s\n", rom_path);
    free(rom);
    return 2;
  }

  char deluxe_path[2048];
  const char *deluxe_override = getenv("FZERO_DELUXE_DATA");
  const char *deluxe_base = SDL_GetBasePath();
  snprintf(deluxe_path, sizeof(deluxe_path), "%smods/bs-deluxe.dat", deluxe_base ? deluxe_base : "");
#if !SNESRECOMP_SDL3
  SDL_free((void *)deluxe_base);
#endif
  /* Deluxe never stops the game from starting. The payload is compiled in, so
   * a failure here means a deliberately damaged build or an override file that
   * does not verify; say so on stderr and run the stock cartridge for this
   * session. The user's settings file is left alone, so fixing the build or
   * removing the override brings Deluxe back without touching it. */
  const char *msu_override = getenv("SNESRECOMP_MSU1");
  bool bundled_music = (!msu_override || !*msu_override) && launcher_settings.msu1_enabled;
  const char *msu_pack = msu_override && *msu_override ? msu_override :
      !launcher_settings.msu1_enabled ? "" : "mods/packs/installed-music";
  bool use_cgp_music = *msu_pack && strcmp(msu_pack,"off") && strcmp(msu_pack,"0") &&
                      (bundled_music || !FzeroMsuHasLegacyPatch(msu_pack));
  g_video.gameplay.enabled &= ~(1u << FZERO_RULE_MSU);
  if (use_cgp_music) g_video.gameplay.enabled |= 1u << FZERO_RULE_MSU;
  if (!FzeroMsuConfigure(msu_pack,use_cgp_music,rom_path)) {
    fprintf(stderr,"[fzero-msu1] %s Starting with SNES music.\n",FzeroMsuError());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING,"Music pack not loaded",FzeroMsuError(),NULL);
    FzeroMsuConfigure("",false,rom_path);
    g_video.gameplay.enabled &= ~(1u << FZERO_RULE_MSU);
  }
  FzeroGameplayConfigure(&g_video.gameplay,g_video.bs_deluxe,g_video.bs_tracks);
  if (!FzeroTracksPrepare(&rom, &rom_size, g_video.bs_deluxe,
                          deluxe_override ? deluxe_override : deluxe_path)) {
    if(FzeroVehiclesActive()) {
      fprintf(stderr,"[vehicles] %s\n",FzeroTracksError());free(rom);return 2;
    }
    fprintf(stderr, "[bs-deluxe] %s starting stock\n", FzeroDeluxeError());
    if (*FzeroGameplayError()) { fprintf(stderr,"%s\n",FzeroGameplayError()); free(rom); return 2; }
    g_video.bs_deluxe = g_video.bs_tracks = false;
    FzeroGameplayConfigure(&g_video.gameplay,false,false);
    if (!FzeroTracksPrepare(&rom, &rom_size, false, NULL)) { free(rom); return 2; }
  }
  /* Match the shared host: keep gamepads live through launcher/game focus
   * transitions and host overlays (SDL otherwise suppresses their state). */
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  /* SDL_Init flipped to true-on-success in SDL3 and inverts silently in its
   * old `!= 0` form, so it must route through the shim. */
  if (!snesrecomp_sdl_init(SDL_INIT_VIDEO | SDL_INIT_AUDIO |
                           SDL_INIT_GAMECONTROLLER)) {
    fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
    free(rom);
    return 3;
  }
  g_audio_mutex = SDL_CreateMutex();
  if (!g_audio_mutex) Die("Unable to create the audio mutex");

  /* Load (or generate) keybinds.ini next to the executable (cwd is
   * exe-anchored; the config-exe-anchoring convention shared by every SNES
   * recomp in this family). Must precede the first keyboard_input() call. */
  keybinds_init(NULL);

  RtlRegisterGame(FzeroGameInfo());
  if (!SnesInit(rom, (int)rom_size))
    Die("SNESRecomp rejected the F-Zero cartridge");
  cpu_trace_init();
  debug_server_set_ram(g_snes->ram, 0x20000);
  {
    int debug_port = 4385;
    const char *port_value = getenv("SNESRECOMP_DEBUG_PORT");
    if (port_value && port_value[0]) {
      long parsed = strtol(port_value, NULL, 0);
      if (parsed > 0 && parsed <= 65535) debug_port = (int)parsed;
    }
    if (debug_server_init(debug_port) == 0) {
#if SNESRECOMP_TRACE
      fprintf(stderr, "[fzero] Debug server ready on port %d\n", debug_port);
#endif
    }
  }
  const char *save_root = getenv("SNESRECOMP_SAVE_ROOT");
  if (save_root && *save_root) RtlSetSaveRoot(save_root);
  if (!FzeroTracksSelectSaveRoot()) Die(FzeroDeluxeError());
  if (!FzeroMsuSelectSaveRoot()) Die(FzeroMsuError());
  RtlReadSram();
  /* After the machine exists: the ring's slots are whole-machine snapshots
   * and it sizes them from a real one. */
  snes_rewind_configure();

  /* SDL_WINDOW_ALLOW_HIGHDPI is one of the few old names SDL3 does NOT alias
   * in SDL_oldnames.h; it became SDL_WINDOW_HIGH_PIXEL_DENSITY. */
#if SNESRECOMP_SDL3
  const SDL_WindowFlags kHighDpiFlag = SDL_WINDOW_HIGH_PIXEL_DENSITY;
#else
  const Uint32 kHighDpiFlag = SDL_WINDOW_ALLOW_HIGHDPI;
#endif
  bool use_gl_renderer = launcher_settings.shader_path[0] != 0;
  if (use_gl_renderer) fzero_gl_prepare_window();
  /* Window scale is a real row on the Settings page, so it has to size the
   * window: it was drawn, saved and then ignored in favour of a hardcoded
   * 768x576 -- which is exactly the 3x this still falls back to. */
  int window_scale = launcher_settings.window_scale;
  if (window_scale < 1 || window_scale > 8) window_scale = 3;
  SDL_Window *window = snesrecomp_sdl_create_window(
      kWindowTitle, 256 * window_scale, 192 * window_scale,
      SDL_WINDOW_RESIZABLE | kHighDpiFlag |
          (use_gl_renderer ? SDL_WINDOW_OPENGL : 0));
  if (!window) Die("Unable to create the game window");
  if (launcher_settings.fullscreen)
    snesrecomp_sdl_set_fullscreen(window, true);
  FzeroGlRenderer gl_renderer;
  SDL_Renderer *renderer = NULL;
  SDL_Texture *texture = NULL;
  unsigned texture_scale = g_video.hd_mode7 ? g_video.hd_scale : 1;
  const unsigned requested_texture_scale = texture_scale;
  if (use_gl_renderer) {
    if (!fzero_gl_init(&gl_renderer, window, launcher_settings.shader_path))
      Die("Unable to initialize the OpenGL shader renderer");
    GLint max_texture_size = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    while (texture_scale > 1 && max_texture_size > 0 &&
           FZERO_MAX_WIDTH * texture_scale > (unsigned)max_texture_size)
      texture_scale = texture_scale > 4 ? 4 : texture_scale > 2 ? 2 : 1;
  } else {
    renderer = snesrecomp_sdl_create_renderer(window, false, false);
    if (!renderer) renderer = snesrecomp_sdl_create_renderer(window, true, false);
    if (!renderer) Die("Unable to create the game renderer");
    for (;;) {
      texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_STREAMING, FZERO_MAX_WIDTH * texture_scale,
                                  kFrameHeight * texture_scale);
      if (texture || texture_scale == 1) break;
      texture_scale = texture_scale > 4 ? 4 : texture_scale > 2 ? 2 : 1;
    }
    if (!texture) Die("Unable to create the game texture");
    /* Scale quality is per-texture in SDL3 (the SDL2 render hint is gone), and
     * the SNES framebuffer leaves alpha zero, so it must be marked opaque or
     * SDL3 blends the whole frame away and presents only the clear color. */
    snesrecomp_sdl_set_texture_linear(texture,
                                      launcher_settings.linear_filter != 0);
    snesrecomp_sdl_set_texture_opaque(texture);
  }
  if (texture_scale != requested_texture_scale) {
    char message[256];
    snprintf(message, sizeof(message),
        "This renderer could not create the %ux HD Mode 7 texture. Using %ux for this session. "
        "Choose a lower resolution in Mods > HD Mode 7 if this persists.",
        requested_texture_scale, texture_scale);
    fprintf(stderr, "[fzero-hd] %s\n", message);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "HD Mode 7 resolution", message, window);
  }

  static uint8_t pixels[FZERO_MAX_WIDTH * kFrameHeight * kBytesPerPixel];
  uint32_t *hd_pixels = NULL;
  size_t hd_capacity = (size_t)FZERO_MAX_WIDTH * kFrameHeight * texture_scale * texture_scale;
  if (texture_scale > 1) {
    hd_pixels = calloc(hd_capacity, sizeof(*hd_pixels));
    if (!hd_pixels) Die("Unable to allocate HD Mode 7 frame");
  }
  FzeroSetMode7Hd(texture_scale > 1 ? texture_scale : 0, hd_pixels, hd_capacity);
  int drawable_width = 256 * window_scale, drawable_height = 192 * window_scale;
  if (use_gl_renderer)
    snesrecomp_sdl_get_drawable_size(window, &drawable_width, &drawable_height);
  else
    snesrecomp_sdl_get_render_output_size(renderer, &drawable_width, &drawable_height);
  FzeroViewport viewport = FzeroCalculateViewport(&g_video, drawable_width, drawable_height);
  FzeroSetViewport(viewport);
  FzeroSetDeferredPresentation(true);
  int logical_width = viewport.width;
  FzeroBeginDrawing(pixels, (size_t)logical_width * kBytesPerPixel);

  SDL_AudioSpec wanted = {0};
  /* 32040 is the SPC's true rate and stays the default, so the conversion is
   * a no-op; the launcher's Sample rate row may name another. Both rows were
   * drawn and then ignored, which is why changing either did nothing. */
  int audio_freq = launcher_settings.audio_freq;
  if (audio_freq < 8000 || audio_freq > 96000) audio_freq = 32040;
  wanted.freq = audio_freq;
  RtlSetAudioOutputRate(audio_freq);
  g_audio_volume = launcher_settings.volume;
  if (g_audio_volume < 0 || g_audio_volume > 100) g_audio_volume = 100;
  wanted.format = AUDIO_S16SYS;
  wanted.channels = 2;
  SDL_AudioDeviceID audio = 0;
#if SNESRECOMP_SDL3
  /* SDL_AudioSpec has no `samples`/`callback` in SDL3: the device is opened as
   * a stream and the callback is supplied separately. */
  g_audio_stream = SDL_OpenAudioDeviceStream(
      SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &wanted, audio_stream_callback, NULL);
  if (g_audio_stream) audio = SDL_GetAudioStreamDevice(g_audio_stream);
#else
  SDL_AudioSpec obtained = {0};
  wanted.samples = 1024;
  wanted.callback = audio_callback;
  audio = SDL_OpenAudioDevice(NULL, 0, &wanted, &obtained, 0);
#endif
  if (!audio) Die("Unable to open the audio device");
  snesrecomp_sdl_pause_audio_device(audio, launcher_settings.enable_audio == 0);

  /* Before the pad scan below, so the virtual pad is the one player 1 gets. */
  selftest_attach();

  SDL_GameController *pad = NULL;
  FzeroGamepadConfigure(g_config_path, g_selftest_pad ? g_selftest_guid : launcher_settings.player_gamepad_guid[0],
                         launcher_settings.deadzone[0]);
  FzeroGamepadRefresh(&pad);

  int running = 1;
  int paused = 0;
  Uint64 state_feedback_until = 0;
  long frames = 0;
  long auto_close_frames = 0;
  const char *auto_close = getenv("SNESRECOMP_AUTOCLOSE_FRAMES");
  if (auto_close) auto_close_frames = strtol(auto_close, NULL, 10);
  FzeroClock clock;
  double actual_refresh = display_refresh(window);
  double hz = g_video.fps_enabled ? FzeroPresentationHz(g_video.fps, actual_refresh) : FZERO_SIMULATION_HZ;
  /* Profiling is session-only: do not persist it as a player's Mods setting. */
  const bool diagnostics_enabled = g_video.diagnostics || profile_mode7;
  if (diagnostics_enabled) {
    FzeroDiagnosticSession session = {
      .version = kBuildVersion, .revision = FZERO_SOURCE_REVISION,
      .framework_revision = FZERO_FRAMEWORK_REVISION, .ui_revision = FZERO_UI_REVISION,
      .build_type = FZERO_BUILD_TYPE, .compiler = FZERO_COMPILER,
      .backend = "SDL", .shader = launcher_settings.shader_path,
      .shader_loaded = use_gl_renderer && gl_renderer.shader != NULL,
      .allocated_scale = texture_scale, .vsync = -99,
      .linear_filter = launcher_settings.linear_filter != 0,
      .audio_enabled = launcher_settings.enable_audio != 0,
      .rewind_enabled = snes_rewind_enabled()
    };
    if (use_gl_renderer) {
      session.backend = "OpenGL";
      session.gpu = (const char *)glGetString(GL_RENDERER);
      session.gpu_vendor = (const char *)glGetString(GL_VENDOR);
      session.driver = (const char *)glGetString(GL_VERSION);
#if SNESRECOMP_SDL3
      SDL_GL_GetSwapInterval(&session.vsync);
#else
      session.vsync = SDL_GL_GetSwapInterval();
#endif
    } else {
#if SNESRECOMP_SDL3
      session.backend = SDL_GetRendererName(renderer);
      SDL_GetRenderVSync(renderer, &session.vsync);
#else
      SDL_RendererInfo info;
      if (!SDL_GetRendererInfo(renderer, &info)) {
        session.backend = info.name;
        session.vsync = (info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
      }
#endif
    }
    char directory[1200];
    if (!snesrecomp_exe_dir_path("diagnostics", directory, sizeof(directory)))
      snprintf(directory, sizeof(directory), "diagnostics");
    FzeroDiagnosticsStart(true, directory, &session);
    if (profile_mode7) FzeroDiagnosticsEvent("mode7_profile", 1);
  }
  FzeroClockReset(&clock, monotonic_seconds(), hz);
  bool suspended = false;
  const FzeroScriptedState scripted_save = parse_scripted_state("FZERO_STATE_SAVE_AT");
  const FzeroScriptedState scripted_load = parse_scripted_state("FZERO_STATE_LOAD_AT");
  double next_display_check = 0;
  uint64_t presentations = 0, missed_presentations = 0;
  FzeroPresenter presenter;
  memset(&presenter, 0, sizeof(presenter));
  presenter.window = window;
  presenter.renderer = renderer;
  presenter.texture = texture;
  presenter.gl = use_gl_renderer ? &gl_renderer : NULL;
  presenter.pixels = pixels;

  /* Outside the loop on purpose. A hotkey press is a request that survives
   * until it is acted on: the event pump runs every host iteration but a
   * simulation frame is only due sixty times a second, so a per-iteration
   * flag consumed inside the simulation batch is dropped on every iteration
   * that only presents — which at 165 Hz is two out of three, and is what
   * made F7 look intermittent on the high-refresh presentation path. */
  int open_savestate_menu = 0;
  int open_rewind = 0;
  FzeroDiagnosticFrame diagnostic_frame = {0};

  while (running) {
    if (diagnostics_enabled) {
      diagnostic_frame = (FzeroDiagnosticFrame){
        .simulation = (uint64_t)frames, .presentations = presentations,
        .missed = missed_presentations + clock.missed_presentations,
        .settings = g_video, .viewport = viewport,
        .output_width = drawable_width, .output_height = drawable_height,
        .effective_scale = FzeroHdScale(), .target_hz = hz, .refresh_hz = actual_refresh,
        .render_workers = FzeroRendererWorkerCount(),
        .fullscreen = (SDL_GetWindowFlags(window) & SNESRECOMP_SDL_WINDOW_FULLSCREEN_DESKTOP) != 0,
        .suspended = suspended, .scene = g_ram[0x54], .subscene = g_ram[0x55]
      };
      FzeroDiagnosticsSample(&diagnostic_frame, false);
    }
    SDL_Event event;
    int panel = 0; /* 0 none, 1 save-state browser, 2 rewind filmstrip */
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) running = 0;
      FzeroGamepadEvent(&pad, &event);
      if (event.type == SDL_KEYDOWN) {
#if SNESRECOMP_SDL3
        int scancode = event.key.scancode;
#else
        int scancode = event.key.keysym.scancode;
#endif
        if (scancode > 0 && (scancode == g_quick_keys[0] || scancode == g_quick_keys[1])) continue;
      }
      if (event.type == SDL_KEYDOWN && !event.key.repeat) {
        /* Hotkeys are tested before the quick slots, so a binding on an
         * F-key takes that key from the slot behind it. */
        if (hotkey_matches(&g_hotkey_menu, SNESRECOMP_SDL_EVENT_KEY(event),
                           (Uint16)SNESRECOMP_SDL_EVENT_MOD(event))) {
          open_savestate_menu = 1;
          continue;
        }
        if (hotkey_matches(&g_hotkey_rewind, SNESRECOMP_SDL_EVENT_KEY(event),
                           (Uint16)SNESRECOMP_SDL_EVENT_MOD(event))) {
          open_rewind = 1;
          continue;
        }
      }
      if (event.type == SDL_KEYDOWN && !event.key.repeat) {
        /* The keysym struct was flattened in SDL3; the shim macros pick the
         * right member for each major. */
        const SDL_Keycode key = SNESRECOMP_SDL_EVENT_KEY(event);
        const Uint16 mod = (Uint16)SNESRECOMP_SDL_EVENT_MOD(event);
        if ((mod & KMOD_CTRL) && (key == SDLK_F6 || key == SDLK_F7)) {
          if (key == SDLK_F6) {
            if (!g_video.enhanced) { g_video.enhanced = true; g_video.aspect = FZERO_ASPECT_16_9; }
            else if (g_video.aspect == FZERO_ASPECT_FIT) g_video.enhanced = false;
            else g_video.aspect++;
          } else {
            g_video.fps_enabled = true;
            static const unsigned rates[] = {0,60,90,120,144,165,240,360};
            for (unsigned i = 0; i < sizeof(rates) / sizeof(*rates); ++i)
              if (g_video.fps == rates[i]) { g_video.fps = rates[(i + 1) % 8]; break; }
          }
          if (!FzeroVideoSave(&g_video, kVideoConfig)) fprintf(stderr, "[fzero] Unable to save video settings\n");
          next_display_check = 0;
          char title[192];
          snprintf(title, sizeof(title), "F-Zero - %s - %s%u FPS",
                   g_video.enhanced ? FzeroAspectName(g_video.aspect) : "4:3",
                   g_video.fps ? "" : "Auto / ", g_video.fps ? g_video.fps : (unsigned)display_refresh(window));
          SDL_SetWindowTitle(window, title);
          state_feedback_until = (Uint64)SDL_GetTicks() + 2500;
          continue;
        }
        if (key >= SDLK_F1 && key <= SDLK_F12) {
          int slot = (int)(key - SDLK_F1);
          perform_state_action(window, (mod & KMOD_SHIFT) != 0, slot,
                               &state_feedback_until);
          continue;
        }
        switch (key) {
          case SDLK_ESCAPE:
            running = 0;
            break;
          case SDLK_p:
            paused = !paused;
            break;
          case SDLK_r:
            if (mod & KMOD_CTRL) {
              FzeroTracksSavesFinish();
              RtlReset(1);
              FzeroGameInfo()->session_reset();
              FzeroSetViewport(viewport);
              g_reset_presentation_clock = true;
            }
            break;
          case SDLK_RETURN: {
            if (!(mod & KMOD_ALT)) break;
            Uint32 flags = (Uint32)SDL_GetWindowFlags(window);
            snesrecomp_sdl_set_fullscreen(
                window,
                (flags & SNESRECOMP_SDL_WINDOW_FULLSCREEN_DESKTOP) == 0);
            break;
          }
          default:
            break;
        }
      }
    }

    uint32_t active_quick = quick_actions(pad);
    uint32_t pressed_quick = active_quick & ~g_previous_quick_actions;
    g_previous_quick_actions = active_quick;
    if (pressed_quick) {
      perform_state_action(window, (pressed_quick & 1u) != 0,
                           SNES_SAVESTATE_QUICK_SLOT, &state_feedback_until);
    }
    update_state_feedback(window, &state_feedback_until);
    {
      int slot = debug_server_consume_loadstate();
      if (slot >= 0)
        perform_state_action(window, 0, slot, &state_feedback_until);
      slot = debug_server_consume_savestate();
      if (slot >= 0)
        perform_state_action(window, 1, slot, &state_feedback_until);
    }
    double debug_wait_start = monotonic_seconds();
    debug_server_wait_if_paused();
    if (monotonic_seconds() - debug_wait_start > 0.1)
      g_reset_presentation_clock = true;
    if (!running) break;
    bool should_suspend = paused || (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED);
    if (should_suspend != suspended) {
      suspended = should_suspend;
      FzeroDiagnosticsEvent(suspended ? "pause" : "resume", 0);
      snesrecomp_sdl_pause_audio_device(audio, suspended || !launcher_settings.enable_audio);
      g_reset_presentation_clock = true;
    }
    if (suspended) {
      uint64_t diagnostic_start = FzeroDiagnosticsBegin();
      SDL_Delay(10);
      FzeroDiagnosticsEnd(FZERO_DIAG_PAUSED, diagnostic_start);
      continue;
    }
    double now = monotonic_seconds();
    if (now >= next_display_check) {
      actual_refresh = display_refresh(window);
      double next_hz = g_video.fps_enabled ? FzeroPresentationHz(g_video.fps, actual_refresh) : FZERO_SIMULATION_HZ;
      if (next_hz != hz) {
        hz = next_hz;
        clock.presentation_hz = hz;
        clock.next_presentation = now;
      }
      next_display_check = now + 0.25;
    }
    if (g_reset_presentation_clock) {
      FzeroDiagnosticsEvent("clock_reset", 0);
      missed_presentations += clock.missed_presentations;
      FzeroClockReset(&clock, now, hz);
      g_reset_presentation_clock = false;
    }
    /* Hotkeys act the moment they are pressed, not on the next simulation
     * boundary: the keyboard is polled every iteration and a request must not
     * wait for one. */
    if (open_savestate_menu) {
      open_savestate_menu = 0;
      if (!snes_savestate_menu_is_open())
        (void)snes_savestate_menu_poll_open(FZERO_MENU_GESTURE);
    }
    if (open_rewind) {
      open_rewind = 0;
      if (!snes_rewind_open())
        set_title_message(window,
                          snes_rewind_enabled()
                              ? "Rewind: nothing recorded yet"
                              : "Rewind is off (enable it in the launcher)",
                          &state_feedback_until);
    }
    if (snes_savestate_menu_is_open()) panel = 1;
    else if (snes_rewind_is_open()) panel = 2;

    /* Viewport policy and input sampling change only at simulation boundaries. */
    for (unsigned batch = 0; !panel && batch < 4 &&
                             FzeroClockSimulationDue(&clock, now); ++batch) {
      selftest_main_tick(frames);
      if (frames == scripted_save.frame)
        (void)perform_state_action(window, 1, scripted_save.slot,
                                   &state_feedback_until);
      if (frames == scripted_load.frame)
        (void)perform_state_action(window, 0, scripted_load.slot,
                                   &state_feedback_until);
      FzeroReplayViewport((unsigned)frames, &g_video);
      int replay_width, replay_height;
      if (FzeroReplayWindow((unsigned)frames, &replay_width, &replay_height))
        SDL_SetWindowSize(window, replay_width, replay_height);
      if (use_gl_renderer)
        snesrecomp_sdl_get_drawable_size(window, &drawable_width, &drawable_height);
      else
        snesrecomp_sdl_get_render_output_size(renderer, &drawable_width, &drawable_height);
      FzeroViewport next = FzeroCalculateViewport(&g_video, drawable_width, drawable_height);
      if (next.width != viewport.width || next.aspect != viewport.aspect) {
        viewport = next;
        FzeroSetViewport(viewport);
        logical_width = viewport.width;
        FzeroBeginDrawing(pixels, (size_t)logical_width * kBytesPerPixel);
      }
      uint32_t input = keyboard_guest_input() |
                       FzeroGamepadReadShortcuts(pad, g_quick_pads, 2, NULL) |
                       debug_server_get_controller_inputs() | (1u << 30) |
                       debug_server_get_controller_active_mask();
      if (FzeroReplayHasInput() && !g_menu_pad_replay) input = FzeroReplayInput((unsigned)frames);
      /* Seat 0's word, before the guest sees it: the overlays are a player-1
       * facility, and the press that closed one must neither reach the game
       * nor re-open the panel. */
      input = overlay_filter_guest_input(
          snes_savestate_menu_filter_guest_input(input));
      if (snes_savestate_menu_poll_open(input) ||
          snes_savestate_menu_is_open()) {
        panel = 1;
        break; /* guest frozen: no frame to run or present */
      }
      if (rewind_gesture_pressed(input) && snes_rewind_open()) {
        panel = 2;
        break;
      }
      if (snes_rewind_is_open()) {
        panel = 2;
        break;
      }
      uint64_t diagnostic_start = FzeroDiagnosticsBegin();
      (void)RtlRunFrame(input);
      FzeroDiagnosticsEnd(FZERO_DIAG_SIMULATION, diagnostic_start);
      if (g_fail || !FzeroLastLleResult()) {
        fprintf(stderr, "[fzero-failure] frame=%ld resume=$%06x bus_fault=%d execution=%d state=%02x,%02x,%02x car=%02x\n",
                frames, (unsigned)FzeroResumePc(), g_fail, FzeroLastLleResult(),
                g_ram[0x54], g_ram[0x55], g_ram[0x56], g_ram[0x52]);
        Die("F-Zero runtime execution failed");
      }
      diagnostic_start = FzeroDiagnosticsBegin();
      FzeroDrawPpuFrame();
      FzeroDiagnosticsEnd(FZERO_DIAG_PPU, diagnostic_start);
      /* One emulated frame elapsed: the ring captures on its own cadence. */
      snes_rewind_note_frame();
      frames++;
      FzeroClockSimulationDone(&clock);
      now = monotonic_seconds();
      if (auto_close_frames > 0 && frames >= auto_close_frames) { running = 0; break; }
    }

    presenter.logical_width = logical_width;
    presenter.viewport = viewport;
    presenter.drawable_width = drawable_width;
    presenter.drawable_height = drawable_height;
    presenter.texture = texture;
    presenter.renderer = renderer;

    if (panel) {
      /* A panel owns the screen: freeze the guest, and let the window keep
       * repainting the frame the player stopped at with the panel over it.
       * Audio goes quiet for the duration, as it would for any paused
       * game. */
      snesrecomp_sdl_pause_audio_device(audio, true);
      FzeroDiagnosticsEvent("menu_open", panel);
      if (panel == 1)
        savestate_menu_loop(&presenter, &running, &pad);
      else
        rewind_loop(&presenter, &running, &pad);
      FzeroDiagnosticsEvent("menu_close", panel);
      if (FzeroStateGuardTripped())
        set_title_message(window, "State refused: taken on the other cartridge",
                          &state_feedback_until);
      snesrecomp_sdl_pause_audio_device(
          audio, suspended || !launcher_settings.enable_audio);
      /* A load or a rewind moved the machine's clock; the presenter's
       * interpolation sources were dropped by FzeroRendererReset in
       * on_state_loaded, and the pacing clock has to stop owing the frames
       * the freeze consumed or the first seconds back run as catch-up. */
      g_reset_presentation_clock = true;
      g_previous_quick_actions = quick_actions(pad);
      continue;
    }

    if (FzeroClockPresentationDue(&clock, now)) {
      uint64_t diagnostic_start = FzeroDiagnosticsBegin();
      FzeroPresent(FzeroClockAlpha(&clock, now));
      FzeroDiagnosticsEnd(FZERO_DIAG_COMPOSITION, diagnostic_start);
      present_frame(&presenter, NULL, 0, 0, 0);
      FzeroDiagnosticsPresented();
      /* Offer what was just presented as the next save's thumbnail and as
       * the filmstrip's frame for the next capture. Both downsample into
       * small fixed buffers and keep nothing else. */
      snes_savestate_menu_note_frame((const uint32_t *)pixels, logical_width,
                                     kFrameHeight);
      snes_rewind_note_framebuffer((const uint32_t *)pixels, logical_width,
                                   kFrameHeight);
      FzeroClockPresentationDone(&clock, monotonic_seconds());
      ++presentations;
    }
    if (running) {
      uint64_t diagnostic_start = FzeroDiagnosticsBegin();
      wait_until(FzeroClockNextDeadline(&clock));
      FzeroDiagnosticsEnd(FZERO_DIAG_WAIT, diagnostic_start);
    }
  }
  if (diagnostics_enabled) {
    diagnostic_frame.simulation = (uint64_t)frames;
    diagnostic_frame.presentations = presentations;
    diagnostic_frame.missed = missed_presentations + clock.missed_presentations;
    FzeroDiagnosticsSample(&diagnostic_frame, true);
    FzeroDiagnosticsStop();
  }
  fprintf(stderr, "[fzero-presentation] simulation=%ld presentations=%llu missed=%llu target_hz=%.3f\n",
          frames, (unsigned long long)presentations,
          (unsigned long long)(missed_presentations + clock.missed_presentations), hz);

  const char *frame_dump = getenv("SNESRECOMP_FRAME_BMP");
  const uint32_t *hd_dump = FzeroHdFrame();
  unsigned dump_scale = FzeroHdScale();
  if (!write_frame_bmp(frame_dump, hd_dump ? (const uint8_t *)hd_dump : pixels,
                       logical_width * (int)dump_scale, kFrameHeight * (int)dump_scale))
    fprintf(stderr, "Unable to write frame dump: %s\n", frame_dump);
  const char *ram_dump = getenv("SNESRECOMP_WRAM_DUMP");
  if (ram_dump && ram_dump[0]) {
    FILE *dump = fopen(ram_dump, "wb");
    if (!dump || fwrite(g_ram, sizeof(g_ram), 1, dump) != 1)
      fprintf(stderr, "Unable to write WRAM capture\n");
    if (dump) fclose(dump);
  }
  FzeroTracksSavesFinish();
  RtlWriteSram();
  FzeroSetMode7Hd(0, NULL, 0);
  FzeroRendererShutdown();
  free(hd_pixels);
  debug_server_shutdown();
  snesrecomp_sdl_pause_audio_device(audio, true);
#if SNESRECOMP_SDL3
  /* Destroying the stream closes the device it was opened against. */
  SDL_DestroyAudioStream(g_audio_stream);
  g_audio_stream = NULL;
  free(g_audio_scratch);
  g_audio_scratch = NULL;
  g_audio_scratch_size = 0;
#else
  SDL_CloseAudioDevice(audio);
#endif
  if (pad) SDL_GameControllerClose(pad);
  snes_rewind_shutdown();
  if (g_overlay_texture) {
    SDL_DestroyTexture(g_overlay_texture);
    g_overlay_texture = NULL;
  }
  if (g_osd_texture) {
    SDL_DestroyTexture(g_osd_texture);
    g_osd_texture = NULL;
  }
  if (use_gl_renderer) {
    fzero_gl_destroy(&gl_renderer);
  } else {
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
  }
  SDL_DestroyWindow(window);
  SDL_DestroyMutex(g_audio_mutex);
  g_audio_mutex = NULL;
  SDL_Quit();
  free(rom);
  /* A self-test that printed FAIL must not exit 0: a harness that only reads
   * the exit status would otherwise record a pass. */
  return g_selftest_failed ? 4 : 0;
}
