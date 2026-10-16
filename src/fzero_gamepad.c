#include "fzero_gamepad.h"
#include "fzero_hotkeys.h"
#include "recomp_launcher.h"

#include <stdio.h>
#include <string.h>

static char s_config[1024], s_preferred[40];
static uint32_t s_bind[12];
static int s_default_deadzone, s_deadzone;
static unsigned s_profile_generation;

/* SDL's standard button indices are shared by SDL2 and SDL3. Triggers are
 * axes, represented here by bits 15/16 after the standard fifteen buttons. */
static const char *const s_names[] = {
    "A", "B", "X", "Y", "Back", "Guide", "Start", "L3", "R3",
    "Lb", "Rb", "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "L2", "R2"};
static const unsigned s_input_bits[12] = {4, 5, 6, 7, 2, 3, 8, 0, 9, 1, 10, 11};

static void pad_guid(SDL_GameController *pad, char guid[40]) {
#if SNESRECOMP_SDL3
  SDL_GUIDToString(SDL_GetJoystickGUID(SDL_GetGamepadJoystick(pad)), guid, 40);
#else
  SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(SDL_GameControllerGetJoystick(pad)), guid, 40);
#endif
}

static char *trim(char *s) {
  while (*s == ' ' || *s == '\t') ++s;
  size_t n = strlen(s);
  while (n && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\r')) s[--n] = 0;
  return s;
}

static uint32_t binding(char *text) {
  uint32_t mask = 0;
  for (char *part = text; part;) {
    char *next = strchr(part, '+');
    if (next) *next++ = 0;
    part = trim(part);
    int button = -1;
    if (!SDL_strcasecmp(part, "L1")) button = 9;
    if (!SDL_strcasecmp(part, "R1")) button = 10;
    for (int i = 0; i < 17; ++i)
      if (!SDL_strcasecmp(part, s_names[i])) button = i;
    if (button < 0) return 0; /* empty/unbound/unknown stays unbound */
    mask |= 1u << button;
    part = next;
  }
  return mask;
}

static void load_map(SDL_GameController *pad,uint32_t out[12],int *deadzone,int player) {
  char guid[40], section[64];
  pad_guid(pad, guid);
  snprintf(section, sizeof(section), "Controller.%s", guid);
  char controls[1024] = "DpadUp,DpadDown,DpadLeft,DpadRight,Back,Start,B,A,Y,X,Lb,Rb";
  FzeroIniReadString(s_config, "GamepadMap", "Controls", controls, sizeof(controls));
  FzeroIniReadString(s_config, section, "Controls", controls, sizeof(controls));
  /* The launcher's two active seat maps may differ even for identical GUIDs.
   * Keep the single-player device-profile order unchanged. */
  if(player>=0)FzeroIniReadString(s_config,"GamepadMap",player?"ControlsP2":"Controls",controls,sizeof(controls));
  char local_section[32];snprintf(local_section,sizeof(local_section),"Local2P.P%d",player+1);
  if(player>=0)FzeroIniReadString(s_config,local_section,"Controls",controls,sizeof(controls));
  char *comment = strpbrk(controls, "#;");
  if (comment) *comment = 0;
  memset(out, 0, 12*sizeof(*out));
  char *part = controls;
  for (int i = 0; i < 12 && part; ++i) {
    char *next = strchr(part, ',');
    if (next) *next++ = 0;
    out[i] = binding(part);
    part = next;
  }
  int percent = s_default_deadzone;
  FzeroIniReadInt(s_config, section, "Deadzone", &percent);
  if(player>=0) {
    char key[24];snprintf(key,sizeof(key),"DeadzoneP%d",player+1);
    FzeroIniReadInt(s_config,"Controller",key,&percent);
  }
  if(player>=0)FzeroIniReadInt(s_config,local_section,"Deadzone",&percent);
  if (percent < 0 || percent > 100) percent = s_default_deadzone;
  *deadzone = (percent * 32767 + 50) / 100;
  fprintf(stderr, "[fzero-input] %s guid=%s deadzone=%d%%\n",
          SDL_GameControllerName(pad), guid, percent);
}
static void load_profile(SDL_GameController *pad) {load_map(pad,s_bind,&s_deadzone,-1);}

void FzeroGamepadConfigure(const char *config, const char *guid, int deadzone) {
  ++s_profile_generation;
  snprintf(s_config, sizeof(s_config), "%s", config ? config : "config.ini");
  snprintf(s_preferred, sizeof(s_preferred), "%s", guid ? guid : "");
  s_default_deadzone = deadzone >= 0 && deadzone <= 100 ? deadzone : 25;
}

void FzeroGamepadRefresh(SDL_GameController **pad) {
  if (*pad && !SDL_GameControllerGetAttached(*pad)) {
    SDL_GameControllerClose(*pad);
    *pad = NULL;
  }
  if (*pad) return;
#if SNESRECOMP_SDL3
  int count = 0;
  SDL_JoystickID *ids = SDL_GetGamepads(&count);
#else
  int count = SDL_NumJoysticks();
#endif
  for (int i = 0; i < count && !*pad; ++i) {
#if SNESRECOMP_SDL3
    SDL_GameController *candidate = SDL_OpenGamepad(ids[i]);
#else
    if (!SDL_IsGameController(i)) continue;
    SDL_GameController *candidate = SDL_GameControllerOpen(i);
#endif
    if (!candidate) continue;
    char guid[40];
    pad_guid(candidate, guid);
    if (s_preferred[0] && SDL_strcasecmp(guid, s_preferred)) {
      SDL_GameControllerClose(candidate);
      continue;
    }
    *pad = candidate;
    load_profile(*pad);
  }
#if SNESRECOMP_SDL3
  SDL_free(ids);
#endif
}

void FzeroGamepadEvent(SDL_GameController **pad, const SDL_Event *event) {
  if (event->type == SDL_CONTROLLERDEVICEREMOVED && *pad &&
      SNESRECOMP_SDL_EVENT_DEVICE(*event) ==
          SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(*pad))) {
    SDL_GameControllerClose(*pad);
    *pad = NULL;
  }
  if (event->type == SDL_CONTROLLERDEVICEADDED ||
      event->type == SDL_CONTROLLERDEVICEREMOVED)
    FzeroGamepadRefresh(pad);
  if (event->type == SDL_CONTROLLERDEVICEREMAPPED && *pad &&
      SNESRECOMP_SDL_EVENT_DEVICE(*event) ==
          SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(*pad)))
    load_profile(*pad);
}

uint32_t FzeroGamepadRead(SDL_GameController *pad) {
  return FzeroGamepadReadShortcuts(pad, NULL, 0, NULL);
}

static uint32_t read_map(SDL_GameController *pad,const uint32_t map[12],int deadzone,
                         const int *bindings,unsigned count,uint32_t *active) {
  if (active) *active = 0;
  if (!pad || !SDL_GameControllerGetAttached(pad)) return 0;
  uint32_t raw = 0, consumed = 0, axes_consumed = 0;
  int axes[SDL_CONTROLLER_AXIS_MAX];
  for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX && i < 32; ++i)
    if (SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)i)) raw |= 1u << i;
  for (int i = 0; i < SDL_CONTROLLER_AXIS_MAX; ++i)
    axes[i] = SDL_GameControllerGetAxis(pad, (SDL_GameControllerAxis)i);
  for (unsigned i = 0; bindings && i < count && i < 32; ++i) {
    int value = bindings[i], axis = -1;
    uint32_t mask = 0;
    if (RECOMP_LAUNCHER_PAD_IS_BUTTON(value)) {
      int code = RECOMP_LAUNCHER_PAD_BUTTON_CODE(value);
      if (code < SDL_CONTROLLER_BUTTON_MAX && code < 32) mask = 1u << code;
    } else if (RECOMP_LAUNCHER_PAD_IS_BUTTON_COMBO(value)) {
      mask = (uint32_t)RECOMP_LAUNCHER_PAD_BUTTON_COMBO_MASK(value);
    } else if (RECOMP_LAUNCHER_PAD_IS_AXIS(value)) {
      int code = RECOMP_LAUNCHER_PAD_AXIS_CODE(value);
      if (code < SDL_CONTROLLER_AXIS_MAX &&
          (RECOMP_LAUNCHER_PAD_AXIS_POSITIVE(value) ? axes[code] > deadzone : axes[code] < -deadzone))
        axis = code;
    }
    if ((mask && (raw & mask) == mask) || axis >= 0) {
      if (active) *active |= 1u << i;
      consumed |= mask;
      if (axis >= 0) axes_consumed |= 1u << axis;
    }
  }
  raw &= ~consumed;
  for (int i = 0; i < SDL_CONTROLLER_AXIS_MAX; ++i)
    if (axes_consumed & (1u << i)) axes[i] = 0;
  uint32_t buttons = 0, input = 0;
  buttons = raw & 0x7fffu;
  if (axes[SDL_CONTROLLER_AXIS_TRIGGERLEFT] > deadzone)
    buttons |= 1u << 15;
  if (axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] > deadzone)
    buttons |= 1u << 16;
  for (int i = 0; i < 12; ++i)
    if (map[i] && (buttons & map[i]) == map[i]) input |= 1u << s_input_bits[i];
  int x = axes[SDL_CONTROLLER_AXIS_LEFTX];
  int y = axes[SDL_CONTROLLER_AXIS_LEFTY];
  if (x < -deadzone) input |= 0x0040u;
  if (x > deadzone) input |= 0x0080u;
  if (y < -deadzone) input |= 0x0010u;
  if (y > deadzone) input |= 0x0020u;
  return input;
}
uint32_t FzeroGamepadReadShortcuts(SDL_GameController *pad,const int *bindings,unsigned count,uint32_t *active) {
  return read_map(pad,s_bind,s_deadzone,bindings,count,active);
}
uint32_t FzeroGamepadReadPlayer(SDL_GameController *pad,unsigned player) {
  if(player>=2 || !pad || !SDL_GameControllerGetAttached(pad))return 0;
  static struct {SDL_JoystickID id;uint32_t map[12];int deadzone;bool valid;unsigned generation;} profiles[2];
  SDL_JoystickID id=SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad));
  if(!profiles[player].valid || profiles[player].id!=id || profiles[player].generation!=s_profile_generation) {
    load_map(pad,profiles[player].map,&profiles[player].deadzone,(int)player);profiles[player].id=id;profiles[player].valid=true;
    profiles[player].generation=s_profile_generation;
  }
  return read_map(pad,profiles[player].map,profiles[player].deadzone,NULL,0,NULL);
}
