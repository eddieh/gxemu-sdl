#ifndef	SDL_H
#define	SDL_H

/*
 *  Copyright (C) 2003-2021  Anders Gavare.  All rights reserved.
 *  Copyright (C) 2023-2024  Eddie Hillenbrand.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *  SUCH DAMAGE.
 *
 *
 *  Headerfile for src/display/sdl.c.
 */

#include <stdbool.h>
#include "misc.h"

struct emul;
struct display;

#ifdef WITH_SDL

#include <SDL2/SDL.h>
#include <SDL2/SDL_main.h>

struct sdl_window {

};

void sdl_redraw_cursor(struct machine *, int);
void sdl_redraw(struct machine *, int);
void sdl_putpixel_fb(struct machine *, int, int x, int y, int color);
void sdl_putimage_fb(struct machine *, int);
void sdl_init(struct machine *);
void sdl_fb_resize(struct display *disp, int new_xsize, int new_ysize);
void sdl_set_standard_properties(struct display *disp);
struct display *sdl_fb_init(int xsize, int ysize, char *name,
	int scaledown, struct machine *);
void sdl_check_event(struct emul *emul);

#endif	/* WITH_SDL */

#endif	/* SDL_H */
