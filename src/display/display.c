/* display.c */

#include "misc.h"

/*  Dummy functions:  */

void display_redraw_cursor(struct machine *m, int i) { }
void display_redraw(struct machine *m, int x) { }
void display_putpixel_fb(struct machine *m, int fb, int x, int y, int color) { }
void display_init(struct machine *machine) { }
void display_fb_resize(struct display *disp, int new_xsize, int new_ysize) { }
void display_set_standard_properties(struct display *disp) { }
struct display *display_fb_init(int xsize, int ysize, char *name,
	int scaledown, struct machine *machine)
    { return NULL; }
void display_check_event(struct emul *emul) { }
