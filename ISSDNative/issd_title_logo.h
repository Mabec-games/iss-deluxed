#ifndef ISSD_TITLE_LOGO_H
#define ISSD_TITLE_LOGO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A replacement for the finished title-screen logo.
 *
 * `<mods_dir>/title_logo/` holds two 32-bit BMPs of the same size:
 * reference.bmp is the original logo exactly as the game draws it once its
 * animation has finished, and logo.bmp is drawn in its place. The swap only
 * happens while the frame matches the reference, so the original animation,
 * fades and every other screen are left alone. Both images sit at a fixed
 * position in the authentic 256-pixel view. */

/* Load the pair, replacing any previous one. False when the folder is
 * missing or the images are unusable; the overlay is then off. */
bool issd_title_logo_load(const char *mods_dir);

/* Draw the replacement into a native frame `width` x `height` whose
 * authentic 256 columns start at `margin_left`. Returns true when drawn,
 * and reports the box it worked in plus a box_w*box_h mask of the pixels it
 * changed, so higher-resolution passes leave just those alone. The mask
 * stays valid until the next call. */
bool issd_title_logo_apply(uint32_t *frame, int width, int height, int margin_left,
                           int *box_x, int *box_y, int *box_w, int *box_h,
                           const uint8_t **changed);

#ifdef __cplusplus
}
#endif

#endif
