/* display.c */

#include "misc.h"
#include "machine.h"
#include "display.h"
#include "x11.h"
#include "sdl.h"

/*  Dummy functions:  */

void display_redraw_cursor(struct machine *m, int i) { }
void display_redraw(struct machine *m, int x) { }
void display_putpixel_fb(struct machine *m, int fb, int x, int y, int color) { }
void display_putimage_fb(struct machine *m, int fb) {}

void display_init_noop(struct machine *m) {}

void display_fb_resize(struct display *disp, int new_xsize, int new_ysize) { }
void display_set_standard_properties(struct display *disp) { }
struct display *display_fb_init(int xsize, int ysize, char *name,
	int scaledown, struct machine *machine)
    { return NULL; }
void display_check_event(struct emul *emul) { }

void display_init(struct machine *m)
{
	int display_n = 0;
	struct display *disp;

	display_n = mda(m).n_displays;

	CHECK_ALLOCATION(mda(m).displays =
	    (struct display **) realloc(mda(m).displays,
	    sizeof(struct display *) * (mda(m).n_displays + 1)));
	CHECK_ALLOCATION(disp = mda(m).displays[display_n] =
	    (struct display *) calloc(1, sizeof(struct display)));

	mda(m).n_displays ++;

	if (mda_using_x11(m)) {
		disp->display_redraw_cursor = x11_redraw_cursor;
		disp->display_redraw = x11_redraw;
		disp->display_putpixel_fb = x11_putpixel_fb;
		disp->display_putimage_fb = x11_putimage_fb;
		disp->display_init = x11_init;
		disp->display_fb_resize = x11_fb_resize;
		disp->display_set_standard_properties =
		    x11_set_standard_properties;
		disp->display_fb_init = x11_fb_init;
		disp->display_check_event = x11_check_event;
	} else if (mda_using_sdl(m)) {
		disp->display_redraw_cursor = sdl_redraw_cursor;
		disp->display_redraw = sdl_redraw;
		disp->display_putpixel_fb = sdl_putpixel_fb;
		disp->display_putimage_fb = sdl_putimage_fb;
		disp->display_init = sdl_init;
		disp->display_fb_resize = sdl_fb_resize;
		disp->display_set_standard_properties =
		    sdl_set_standard_properties;
		disp->display_fb_init = sdl_fb_init;
		disp->display_check_event = sdl_check_event;
	} else {
		disp->display_redraw_cursor = display_redraw_cursor;
		disp->display_redraw = display_redraw;
		disp->display_putpixel_fb = display_putpixel_fb;
		disp->display_putimage_fb = display_putimage_fb;
		disp->display_init = display_init_noop;
		disp->display_fb_resize = display_fb_resize;
		disp->display_set_standard_properties =
		    display_set_standard_properties;
		disp->display_fb_init = display_fb_init;
		disp->display_check_event = display_check_event;
	}

	disp->display_init(m);
}
