#include "fzero_gameplay.h"

const FzeroRuleInfo fzero_rules[FZERO_RULE_COUNT] = {
    {"cgp-tuning", "CGP vehicle tuning",
     "Apply a CGP handling and CPU balance profile on any course. P1/P2/P3 "
     "replace the original four vehicles' stat tables. Extra BS vehicles keep "
     "their stat tables; shared physics changes apply to the roster."},
    {"cgp-boost", "CGP energy boost",
     "Use replenishing boosts that consume energy after the first lap. Choose "
     "the P1, P2 or P3 energy and duration profile. Works on any course, "
     "including with BS vehicles."},
    {"cgp-exhaust", "CGP exhaust placement",
     "Apply the author's P1/P2/P3 exhaust placement profile to the original "
     "four vehicle slots. Cosmetic; independent of handling and tracks."},
    {"cgp-animation", "Blue Falcon / Golden Fox animation fix",
     "Apply the author's vehicle animation table correction."},
    {"cgp-dash", "Dash plate facing fix",
     "Use facing angle for dash plate alignment, and preserve airborne "
     "momentum."},
    {"cgp-spin", "Dynamic collision spin",
     "Scale direct-collision spin by the speed difference between the two "
     "vehicles."},
    {"cgp-bounce", "Softer lateral bounce",
     "Use the author's smaller, constant lateral collision bounce."},
    {"cgp-cpu-spin", "Stronger CPU lateral spin",
     "Make CPU vehicles rotate farther, opposite to the player, after lateral "
     "collisions."},
    {"cgp-rotation", "Lateral collision direction fix",
     "Turn away from the other vehicle after a lateral collision."},
    {"cgp-legend", "Legend difficulty",
     "Enabled by default. Add Legend above Master, with revised CPU speeds, acceleration and "
     "starting lives. Its CPU difficulty tables take precedence over CGP "
     "vehicle tuning."},
    {"cgp-dmag", "CGP grip magnets",
     "Courses declaring CGP grip magnets receive their required behavior "
     "automatically: down-pull adds grip; the MAGNET tile property controls "
     "damage. Other courses keep their original magnet behavior."},
    {"cgp-laps", "Require finish-line checkpoints",
     "Prevent gaining or undoing a lap by crossing the finish line from a "
     "distant checkpoint."},
    {"cgp-rainbow", "Rainbow Road course rules",
     "Combine CGP Illusion and Rainbow Gravity: falling from the road and "
     "stronger gravity on CGP Rainbow Road. Has no effect on other courses."},
    {"cgp-red-bumper", "Red bumper fix",
     "Apply the author's red bumper branch correction."},
    {"cgp-hitbox", "Refined collision hitboxes",
     "Slightly reduce lateral and direct collision thresholds for closer "
     "racing."},
    {"cgp-up-magnet", "Reverse magnets",
     "Courses declaring CGP up magnets receive their required lift behavior "
     "automatically. Other courses keep their original magnet behavior."},
    {"cgp-fog", "Track texture smoothing",
     "Single-player and versus: soften road texture banding with a 2x2 texture filter. Keeps HUD and vehicles sharp. Off preserves original pixel sampling. Independent of HD Mode 7; increases rendering cost."},
    {"cgp-msu", "CGP MSU music adapter",
     "Optional CGP/FZEdit music mapping. Select your own music folder under "
     "Sound. Missing tracks fall back to the original soundtrack. No music is "
     "included."},
    {"cgp-credits", "CGP ending credits",
     "Retired console ending patch; reserved identity for older configurations."}};
