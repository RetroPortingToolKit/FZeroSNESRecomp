# F-Zero Forever controls and mods

Choose each player's input source in the launcher's **Controls** page before
playing: for example, P1 Keyboard and P2 the selected Switch Pro controller.
**Local 2 Player** defaults on in Mods. Press Play and select **VS MODE** on the
title screen. Each device controls only its assigned player's car selection
and race. Assign different controllers when both players use gamepads.

If Steam Input maps a controller to keyboard keys, those keys are ordinary
keyboard input to the game. Disable Steam desktop keyboard mappings or run
outside Steam when using a separate keyboard player. The game cannot identify
which physical device generated an operating-system keyboard event.

Multiplayer never pauses. Start opens a live menu: Continue, Restart Race,
Change Race or Main Menu. The race and the other player's controls continue;
the player operating the menu sends neutral driving input. B closes the menu.
Focus loss and disconnects never freeze the other player. A disconnected seat
sends neutral input until its assigned device returns.
After the race, use **Race Again**, **Change Race**, or **Main Menu** locally;
the online host selects Race Again or Change Race. Save/rewind browser hotkeys
and quick saves/loads cannot interrupt a multiplayer race. Single-player keeps
its normal pause behavior.

Lobby Mods lists the required VS rules and the shared course catalog. Change
the catalog in the main Mods screen before entering a lobby; both players
must match. Presentation and music are local choices, and single-player
handling switches do not change the fixed VS machine roster.

| Setting | Where it applies |
| --- | --- |
| Aspect, presentation FPS, HD Mode 7 | Single-player and versus; online aspect follows the host. |
| Track texture smoothing | Single-player and versus, independently of HD Mode 7. The former Smooth fog control is now in Presentation. Enable to soften road dithering/banding; disable for original sharp pixel sampling. Costs extra rendering work. |
| Enabled course packs and imports | Add courses to the shared single-player/versus catalog. Online peers must have the same catalog. |
| Single-player vehicle packs and original-car rebalances | Single-player only. Versus always has its own twelve-machine selector; each chosen machine carries its authored handling and boost behavior. |
| CGP gameplay rules | Single-player only. Versus uses fixed shared rules. Terrain mechanics required by an imported course follow that course in both modes. |
| MSU-1 and installed music | Single-player and versus; online music choices are local to each peer. Fresh settings default on. Missing songs use SNES music. Explicit saved opt-outs remain off. |

CGP original-car rebalances default on for single-player; conflicting BS
vehicles default off. Saved settings take precedence. Extra CGP vehicles remain
a separate choice.

Full-course rendering is always active in races, including 4:3 and both local
views. It does not depend on widescreen or texture smoothing. The renderer
resolves beyond the original streamed tilemap and removes stepped distance haze.
Texture smoothing also addresses the separate bands produced by projecting the
original low-color road artwork; it does not change physics or collision data.
Broad stripes painted into the road texture remain part of the course artwork.

Automated validation mutes its own audio. Owner-facing playtests and QA builds
use normal audio by default.
