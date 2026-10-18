#ifndef ISSD_LAUNCHER_H
#define ISSD_LAUNCHER_H

#include <stdbool.h>
#include <stddef.h>

#include "issd_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The pre-game launcher: the ISS DELUXE'd logo as a darkened background and
 * a vertical menu (Play, Online, Options, Controls, Quit) with the golden
 * ball as cursor. It runs in its own window before the ROM is loaded and
 * returns true when the player chose Play with a valid ROM, which is then in
 * `rom_path`. Options are written to `cfg`; the caller saves it. */
bool issd_launcher_run(IssdConfig *cfg, char *rom_path, size_t rom_path_size);

/* Whether `path` is a supported International Superstar Soccer Deluxe
 * (USA) image. `why` receives a short reason when it is not. */
bool issd_launcher_rom_ok(const char *path, char *why, size_t why_size);

#ifdef __cplusplus
}
#endif

#endif
