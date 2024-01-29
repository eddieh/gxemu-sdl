/*
 *  Copyright (C) 2003-2021  Anders Gavare.  All rights reserved.
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
 *  SDL-related functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "console.h"
#include "emul.h"
#include "machine.h"
#include "misc.h"
#include "sdl.h"
#include "display.h"

#undef CTRL
#define CTRL(x) ((x) & 0x1F)

#ifdef WITH_SDL

static bool left_ctrl = false;
static bool left_alt = false;

static struct display *grabbed = NULL;

static bool mouseExplicityMoved = false;
static int mouseXbeforeGrab = 0;
static int mouseYbeforeGrab = 0;
static int mouseXofLastEvent = 0;
static int mouseYofLastEvent = 0;

static bool mouseCursorHidden = false;


static void sdl_unhide_cursor()
{

}


static void sdl_hide_cursor()
{

}


static void setMousePointerCoordinates(struct display *disp,
    int x, int y)
{

}


static void mouseMouseToCenterOfScreen(struct display *disp)
{

}

static void grab(struct display *disp)
{

}


static void ungrab()
{

}


/*
 *  sdl_redraw_cursor():
 *
 *  Redraw a framebuffer's SDL cursor.
 *
 *  NOTE: It is up to the caller to call XFlush.
 */
void sdl_redraw_cursor(struct machine *m, int i)
{

}


/*
 *  sdl_redraw():
 *
 *  Redraw SDL windows.
 */
void sdl_redraw(struct machine *m, int i)
{
	struct display *disp;
	struct sdl_window *fbwin;

	if (i < 0)
		return;
	if (i >= mda(m).n_displays)
		return;

	disp = mda(m).displays[i];
	fbwin = disp->sdl_window;

#if 0
	/* update texture from framebuffer*/
	SDL_UpdateTexture(fbwin->texture, NULL,
	    /* TODO: need ref `d' to vfb_data or directly to fb */
	    d->framebuffer,
	    d->fb_xsize * sizeof(unsigned char));
#endif

#if 0
	/* update texture from surface */
	SDL_UpdateTexture(fbwin->texture, NULL,
	    fbwin->surface->pixels,
	    fbwin->surface->pitch);
#endif

#if 0
	SDL_RenderClear(fbwin->renderer);
	SDL_RenderCopy(fbwin->renderer,
	    fbwin->texture, NULL, NULL);
	SDL_RenderPresent(fbwin->renderer);
#endif
}


/*
 *  sdl_putpixel_fb():
 *
 *  Output a framebuffer pixel. i is the framebuffer number.
 */
void sdl_putpixel_fb(struct machine *m, int i, int x, int y, int color)
{

}


/*
 *  sdl_putimage_fb():
 *
 *  Output an entire XImage to a framebuffer window. i is the
 *  framebuffer number.
 */
void sdl_putimage_fb(struct machine *m, int i)
{

}


/*
 *  sdl_init():
 *
 *  Initialize SDL stuff (but doesn't create any windows).
 *
 *  It is then up to individual drivers, for example framebuffer devices,
 *  to initialize their own windows.
 */
void sdl_init(struct machine *m)
{

}


/*
 *  sdl_fb_resize():
 *
 *  Set a new size for an SDL framebuffer window.  (NOTE: I didn't think of
 *  this kind of functionality during the initial design, so it is probably
 *  buggy. It also needs some refactoring.)
 */
void sdl_fb_resize(struct display *disp, int new_xsize, int new_ysize)
{

}


/*
 *  sdl_set_standard_properties():
 *
 *  Right now, this only sets the title of a window.
 */
void sdl_set_standard_properties(struct display *disp)
{

}


/*
 *  sdl_fb_init():
 *
 *  Initialize a framebuffer window.
 */
struct display *sdl_fb_init(int xsize, int ysize, char *name,
	int scaledown, struct machine *m)
{
	int x, y, fb_number = 0;
	struct display *disp;
	struct sdl_window *fbwin;
	char *title;

	fb_number = mda(m).n_displays - 1;

	disp = mda(m).displays[fb_number];
	CHECK_ALLOCATION(fbwin = disp->sdl_window =
	    (struct sdl_window *) calloc(1, sizeof(struct sdl_window)));

	title = name;

	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		debugmsg(SUBSYS_SDL, "fb_init", VERBOSITY_ERROR,
		    "could not initialize SDL: %s\n", SDL_GetError());
		exit(1);
	}

	fbwin->window = SDL_CreateWindow(title,
	    SDL_WINDOWPOS_UNDEFINED,
	    SDL_WINDOWPOS_UNDEFINED,
	    xsize, ysize, 0);
	if (!fbwin->window) {
		debugmsg(SUBSYS_SDL, "fb_init", VERBOSITY_ERROR,
		    "could not create window '%s': %s", title,
		    SDL_GetError());
		exit(1);
	}
	SDL_SetWindowData(fbwin->window, "machine", m);

	fbwin->renderer = SDL_CreateRenderer(fbwin->window, -1, 0);
	if (!fbwin->renderer) {
		debugmsg(SUBSYS_SDL, "fb_init", VERBOSITY_ERROR,
		    "could not create renderer for window '%s': %s", title,
		    SDL_GetError());
		exit(1);
	}

#if 0
	fbwin->surface = SDL_CreateRGBSurface(0, xsize, ysize, 8,
	    0, 0, 0, 0);
	/* fbwin->surface = SDL_CreateRGBSurfaceWithFormat(); */
	if (!fbwin->surface) {
		debugmsg(SUBSYS_SDL, "fb_init", VERBOSITY_ERROR,
		    "could not create surface: %s",
		    SDL_GetError());
		exit(1);
	}

	fbwin->texture = SDL_CreateTexture(fbwin->renderer,
	    SDL_PIXELFORMAT_ARGB8888,
	    SDL_TEXTUREACCESS_STREAMING,
	    xsize, ysize);
	if (!fbwin->texture) {
		debugmsg(SUBSYS_SDL, "fb_init", VERBOSITY_ERROR,
		    "could not create texture: %s",
		    SDL_GetError());
		exit(1);
	}
#endif

	/* paint it black */
	SDL_SetRenderDrawColor(fbwin->renderer, 0, 0, 0, 255);
	SDL_RenderClear(fbwin->renderer);
	SDL_RenderPresent(fbwin->renderer);

	return disp;
}

static void
sdl_handle_window_event(struct emul *emul,
    struct machine *m, SDL_Event *event)
{
}

static void
sdl_handle_mouse_motion_event(struct emul *emul,
    struct machine *m, SDL_Event *event)
{
}

static void
sdl_handle_mouse_button_event(struct emul *emul,
    struct machine *m, SDL_Event *event)
{
}

static void
sdl_handle_mouse_wheel_event(struct emul *emul,
    struct machine *m, SDL_Event *event)
{
}

static void
sdl_handle_key_event(struct emul *emul,
    struct machine *m, SDL_KeyboardEvent *event)
{
	Uint32 windowID;
	SDL_Window *win;
	SDL_Keycode kc;
	SDL_Keymod kmod;
	int pressed;

#if 0
	char text[32];
	char *kn;
#endif

	windowID = event->windowID;
	win = SDL_GetWindowFromID(windowID);
	m = (struct machine *)SDL_GetWindowData(win, "machine");

	kc = event->keysym.sym;
	kmod = event->keysym.mod;
	pressed = event->type == SDL_KEYDOWN;

#if 0
	kn = SDL_GetKeyName(kc);
	strcpy(text, kn);
	fprintf(stderr, "%d[%s] => %s\n", kc, text,
	     pressed ? "Down" : "Up");
	fflush(stderr);
#endif

	if (pressed)
		return;

	if (kmod & KMOD_CTRL) {
		if (kc >= 32 && kc <= 127)
			console_makeavail(m->main_console_handle,
			    CTRL(kc));
	}

	switch (kc) {
	case SDLK_BACKSPACE:
		console_makeavail(m->main_console_handle,
		    SDLK_BACKSPACE);
		break;
	case SDLK_TAB:
		console_makeavail(m->main_console_handle,
		    SDLK_TAB);
		break;
	case SDLK_RETURN:
		console_makeavail(m->main_console_handle,
		    SDLK_RETURN);
		break;
	case SDLK_ESCAPE:
		console_makeavail(m->main_console_handle,
		    SDLK_ESCAPE);
		break;
	default:
		break;
	}
}

static void
sdl_handle_text_input_event(struct emul *emul,
    struct machine *m, SDL_TextInputEvent *event)
{
	Uint32 windowID;
	SDL_Window *win;
	char text[32];

	windowID = event->windowID;
	win = SDL_GetWindowFromID(windowID);
	m = (struct machine *)SDL_GetWindowData(win, "machine");

	strcpy(text, event->text);
	for (int i = 0; i < strlen(text); i++)
		console_makeavail(m->main_console_handle, text[i]);
}

/*
 *  sdl_check_events_machine():
 *
 *  Check for SDL events on a specific machine.
 *
 *  TODO:  Yuck! This has to be rewritten. Each display should be checked,
 *         and _then_ only those windows that are actually exposed should
 *         be redrawn!
 */
static void sdl_check_events_machine(struct emul *emul, struct machine *m)
{
	SDL_Event event;

	while (SDL_PollEvent(&event)) {
		switch (event.type) {
		case SDL_QUIT:
			exit(0);
			break;
		case SDL_WINDOWEVENT:
			sdl_handle_window_event(emul, m, &event);
			break;
		case SDL_MOUSEMOTION:
			sdl_handle_mouse_motion_event(emul, m, &event);
			break;
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			sdl_handle_mouse_button_event(emul, m, &event);
			break;
		case SDL_MOUSEWHEEL:
			sdl_handle_mouse_wheel_event(emul, m, &event);
			break;
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			sdl_handle_key_event(emul, m, &event);
			break;
		case SDL_TEXTINPUT:
			sdl_handle_text_input_event(emul, m, &event);
			break;
		default:
			break;
		}
	}

}


/*
 *  sdl_check_event():
 *
 *  Check for SDL events.
 */
void sdl_check_event(struct emul *emul)
{
	sdl_check_events_machine(emul, NULL);
}

#endif	/*  WITH_SDL  */
