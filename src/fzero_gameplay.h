#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum FzeroRule {
  FZERO_RULE_TUNING,
  FZERO_RULE_BOOST,
  FZERO_RULE_EXHAUST,
  FZERO_RULE_ANIMATION,
  FZERO_RULE_DASH,
  FZERO_RULE_SPIN,
  FZERO_RULE_BOUNCE,
  FZERO_RULE_CPU_SPIN,
  FZERO_RULE_ROTATION,
  FZERO_RULE_LEGEND,
  FZERO_RULE_DMAG,
  FZERO_RULE_LAPS,
  FZERO_RULE_RAINBOW,
  FZERO_RULE_RED_BUMPER,
  FZERO_RULE_HITBOX,
  FZERO_RULE_UP_MAGNET,
  FZERO_RULE_RETIRED_FOG, /* Retired texture filter; preserve saved bit 16. */
  FZERO_RULE_MSU,
  FZERO_RULE_CREDITS, /* Retired; keep the saved bit number reserved. */
  FZERO_RULE_COUNT
} FzeroRule;
/* Credits are source attribution only; never install their SNES ending patch. */
#define FZERO_RULE_COURSE_MASK ((1u << FZERO_RULE_DMAG) | (1u << FZERO_RULE_UP_MAGNET) | (1u << FZERO_RULE_RAINBOW))
#define FZERO_RULE_ACTIVE_MASK (((1u << FZERO_RULE_COUNT) - 1u) & ~((1u << FZERO_RULE_CREDITS) | (1u << FZERO_RULE_RETIRED_FOG) | FZERO_RULE_COURSE_MASK))
#define FZERO_RULE_SELECTABLE_MASK (FZERO_RULE_ACTIVE_MASK & ~7u)
#define FZERO_RULE_DEFAULT_MASK (1u << FZERO_RULE_LEGEND)
typedef struct FzeroGameplaySettings {
  uint32_t enabled;
  unsigned tuning, boost, exhaust; /* 0=P1, 1=P2, 2=P3. Retained while off. */
  unsigned vehicle_packs; /* CGP P1/P2/P3 bits; launcher/config enables all (7) or none. */
  unsigned stock_rebalance; /* Explicit BF/WG/GF/FS identity bits. */
  bool always_records; /* Testing: expose the title's records browser on empty saves. */
} FzeroGameplaySettings;
typedef struct FzeroRuleInfo {
  const char *id, *name, *description;
} FzeroRuleInfo;
extern const FzeroRuleInfo fzero_rules[FZERO_RULE_COUNT];
bool FzeroRuleEnabled(FzeroRule rule);
void FzeroGameplayConfigure(const FzeroGameplaySettings *settings, bool cars,
                            bool tracks);
void FzeroGameplayHeadless(bool deluxe);
bool FzeroBsCars(void);
bool FzeroBsTracks(void);
bool FzeroGameplayActive(void);
const FzeroGameplaySettings *FzeroGameplaySettingsCurrent(void);
const uint8_t *FzeroGameplaySignature(void);
bool FzeroGameplayPrepare(uint8_t **rom, size_t *size);
const char *FzeroGameplayError(void);
void FzeroGameplayInstallHooks(void);
unsigned FzeroGameplayMusicTrack(unsigned command);
void FzeroGameplayActivateVehicles(const FzeroGameplaySettings *settings, uint8_t *rom);
void FzeroGameplaySetSignature(const uint8_t hash[32]);
uint16_t FzeroGameplayMenuInput(uint16_t input, const uint8_t *ram);

/* Build an immutable authored VS vehicle resource image without selecting it
 * as the live single-player cartridge or changing saved gameplay settings. */
void FzeroGameplayBuildVehicleProfile(uint8_t *image,unsigned group);
