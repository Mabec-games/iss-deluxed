#ifndef ISSD_LAUNCHER_H
#define ISSD_LAUNCHER_H

#include <stdbool.h>
#include <stddef.h>

#include "issd_config.h"
#include "issd_online.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { ISSD_LAUNCH_QUIT, ISSD_LAUNCH_PLAY, ISSD_LAUNCH_ONLINE } IssdLaunchChoice;

/* The pre-game launcher: the ISS DELUXE'd logo as a darkened background and
 * a vertical menu (Play, Online, Options, Controls, Quit) with the golden
 * ball as cursor. It runs in its own window before the ROM is loaded. On
 * PLAY or ONLINE the valid ROM is in `rom_path`; ONLINE also fills `online`
 * with the match the lobby arranged. Options are written to `cfg`; the
 * caller saves it. */
IssdLaunchChoice issd_launcher_run(IssdConfig *cfg, char *rom_path, size_t rom_path_size,
                                   IssdOnlineLaunch *online);

/* Whether `path` is a supported International Superstar Soccer Deluxe
 * (USA) image. `why` receives a short reason when it is not. */
bool issd_launcher_rom_ok(const char *path, char *why, size_t why_size);

#ifdef __cplusplus
}
#endif

#endif
