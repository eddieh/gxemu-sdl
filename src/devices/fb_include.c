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
 *  Included from dev_fb.c.
 *
 *  FB_SCALEDOWN should be defined if d->vfb_scaledown > 1.
 *  FB_BO for d->x11_window->fb_ximage->byte_order non-zero
 *  FB_24 for 24-bit X11 color.
 *  FB_16 for 16-bit X11 color.
 *  FB_15 for 15-bit X11 color.
 *  (Default is to fallback to grayscale.)
 */

#ifdef x11_set_color
#undef x11_set_color
#endif

/*  Combine the color into an X11 long and display it:  */
/*  TODO:  construct color in a more portable way:  */

#ifdef FB_24
#ifdef FB_BO
#define x11_set_color color = (b << 16) + (g << 8) + r
#else
#define x11_set_color color = (r << 16) + (g << 8) + b
#endif

#else	/*  !24  */
#ifdef FB_16
#ifdef FB_BO
#define x11_set_color color = ((b >> 3) << 11) + ((g >> 2) << 5) + (r >> 3)
#else
#define x11_set_color color = ((r >> 3) << 11) + ((g >> 2) << 5) + (b >> 3)
#endif

#else	/*  !16  */
#ifdef FB_15
#ifdef FB_BO
#define x11_set_color color = ((b >> 3) << 10) + ((g >> 3) << 5) + (r >> 3)
#else
#define x11_set_color color = ((r >> 3) << 10) + ((g >> 3) << 5) + (b >> 3)
#endif

#else	/*  !15  */
#define	x11_set_color color = disp_x11_window(d)->x11_graycolor[15 * 	\
		(r + g + b) / (255 * 3)].pixel

#endif	/*  !15  */

#endif	/*  !16  */

#endif	/*  !24  */


#ifdef macro_put_pixel
#undef macro_put_pixel
#endif

#define x11_put_pixel x11_set_color;					      \
	if (x>=0 && x<d->fb_xsize && y>=0 && y<d->fb_ysize)		      \
		XPutPixel(disp_x11_window(d)->fb_ximage, x, y, color);



#define sdl_set_color SDL_SetRenderDrawColor(disp_sdl_window(d)->renderer, r, g, b, 255)

#define sdl_put_pixel sdl_set_color;					      \
	if (x >= 0 && x < d->fb_xsize && y >= 0 && y < d->fb_ysize)	      \
		SDL_RenderDrawPoint(disp_sdl_window(d)->renderer, x, y);


#define sdl_put_pixel_surface						      \
	if (x >= 0 && x < d->fb_xsize && y >= 0 && y < d->fb_ysize) {	      \
	uint32_t *pixels = disp_sdl_window(d)->surface->pixels;		      \
	size_t surf_addr = (y * disp_sdl_window(d)->surface->w + x) * 1;      \
	x11_set_color;							      \
	pixels[surf_addr] = color; }


#define macro_put_pixel if (disp_using_x11(d)) {			      \
		x11_put_pixel;						      \
	} else if (disp_using_sdl(d)) {					      \
		sdl_put_pixel_surface;					      \
	}


void REDRAW(struct vfb_data *d, int addr, int len)
{
	int x, y, pixel, npixels;
	long color;

#ifndef FB_SCALEDOWN

	/*  Which framebuffer pixel does addr correspond to?  */
	pixel = addr * 8 / d->bit_depth;
	y = pixel / d->xsize;
	x = pixel % d->xsize;

	/*  How many framebuffer pixels? (len doesn't include the last pixel)  */
	npixels = len * 8 / d->bit_depth;
	npixels += d->bit_depth >= 8 ? 1 : (8 / d->bit_depth);

	if (d->bit_depth < 8) {
		for (pixel=0; pixel<npixels; pixel++) {
			int c, r, g, b;
			size_t fb_addr = (y * d->xsize + x) * d->bit_depth;
			/*  fb_addr is now which _bit_ in the framebuffer  */

			c = d->framebuffer[fb_addr >> 3];
			fb_addr &= 7;

			/*  HPC is reverse:  */
			if (d->vfb_type == VFB_HPC || d->vfb_type == VFB_REVERSEBITS)
				fb_addr = 8 - d->bit_depth - fb_addr;

			c = (c >> fb_addr) & ((1<<d->bit_depth) - 1);
			/*  c <<= (8 - d->bit_depth);  */

			r = d->rgb_palette[c*3 + 0];
			g = d->rgb_palette[c*3 + 1];
			b = d->rgb_palette[c*3 + 2];

			macro_put_pixel;
			x++;
		}
	} else if (d->bit_depth == 8) {
		for (pixel=0; pixel<npixels; pixel++) {
			int c, r, g, b;
			size_t fb_addr = y * d->xsize + x;
			/*  fb_addr is now which byte in framebuffer  */

			c = d->framebuffer[fb_addr];
			r = d->rgb_palette[c*3 + 0];
			g = d->rgb_palette[c*3 + 1];
			b = d->rgb_palette[c*3 + 2];

			macro_put_pixel;
			x++;
		}
	} else {	/*  d->bit_depth > 8  */
		for (pixel=0; pixel<npixels; pixel++) {
			int r, g, b;
			size_t fb_addr = (y * d->xsize + x) * d->bit_depth;
			/*  fb_addr is now which byte in framebuffer  */

			/*  > 8 bits color.  */
			fb_addr >>= 3;
			switch (d->bit_depth) {
			case 24:
			case 32:
				r = d->framebuffer[fb_addr];
				g = d->framebuffer[fb_addr + 1];
				b = d->framebuffer[fb_addr + 2];
				break;
			case 16:
				if (d->vfb_type == VFB_HPC) {
					b = d->framebuffer[fb_addr] +
					    (d->framebuffer[fb_addr+1] << 8);

					if (d->color32k) {
						r = b >> 11;
						g = b >> 5;
						r = r & 31;
						g = (g & 31) * 2;
						b = b & 31;
					} else if (d->psp_15bit) {
						int tmp;
						r = (b >> 10) & 0x1f;
						g = (b >>  5) & 0x1f;
						b = b & 0x1f;
						g <<= 1;
						tmp = r; r = b; b = tmp;
					} else {
						r = (b >> 11) & 0x1f;
						g = (b >>  5) & 0x3f;
						b = b & 0x1f;
					}
				} else {
				    r = d->framebuffer[fb_addr] >> 3;
/*  HUH? TODO:  */
				    g = (d->framebuffer[fb_addr] << 5) +
				      (d->framebuffer[fb_addr + 1] >>5);
				    b = d->framebuffer[fb_addr + 1]&31;
				}

				r *= 8;
				g *= 4;
				b *= 8;
				break;
			default:
				r = g = b = random() & 255;
			}

			macro_put_pixel;
			x++;
		}
	}

#else	/*  FB_SCALEDOWN  */

	/*  scaledown > 1:  */
	int scaledown = d->vfb_scaledown;
	int scaledownXscaledown = scaledown * scaledown;

	/*  Which framebuffer pixel does addr correspond to?  */
	pixel = addr * 8 / d->bit_depth;
	y = pixel / d->xsize;
	x = pixel % d->xsize;

	/*  How many framebuffer pixels?  */
	npixels = len * 8 / d->bit_depth;

	/*  Which x11 pixel?  */
	x /= scaledown;
	y /= scaledown;

	/*  How many x11 pixels:  */
	npixels /= scaledown;
	if (npixels == 0)
		npixels = 1;

	if (d->bit_depth < 8) {
		for (pixel=0; pixel<npixels; pixel++) {
			int subx, suby, r, g, b;
			int64_t color_r = 0, color_g = 0, color_b = 0;
			for (suby=0; suby<scaledown; suby++)
			    for (subx=0; subx<scaledown; subx++) {
				int fb_x, fb_y, fb_addr, c;

				fb_x = x * scaledown + subx;
				fb_y = y * scaledown + suby;
				fb_addr = fb_y * d->xsize + fb_x;
				fb_addr = fb_addr * d->bit_depth;
				/*  fb_addr is now which _bit_ in
				    the framebuffer  */

				c = d->framebuffer[fb_addr >> 3];
				fb_addr &= 7;

				/*  HPC is reverse:  */
				if (d->vfb_type == VFB_HPC || d->vfb_type == VFB_REVERSEBITS)
					fb_addr = 8 - d->bit_depth - fb_addr;

				c = (c >> fb_addr) & ((1<<d->bit_depth) - 1);
				/*  c <<= (8 - d->bit_depth);  */

				r = d->rgb_palette[c*3 + 0];
				g = d->rgb_palette[c*3 + 1];
				b = d->rgb_palette[c*3 + 2];

				color_r += r;
				color_g += g;
				color_b += b;
			    }

			r = color_r / scaledownXscaledown;
			g = color_g / scaledownXscaledown;
			b = color_b / scaledownXscaledown;
			macro_put_pixel;
			x++;
		}
	} else if (d->bit_depth == 8) {
		for (pixel=0; pixel<npixels; pixel++) {
			int subx, suby, r, g, b;
			int64_t color_r = 0, color_g = 0, color_b = 0;
			for (suby=0; suby<scaledown; suby++)
			    for (subx=0; subx<scaledown; subx++) {
				int fb_x, fb_y, fb_addr, c;

				fb_x = x * scaledown + subx;
				fb_y = y * scaledown + suby;
				fb_addr = fb_y * d->xsize + fb_x;
				/*  fb_addr is which _byte_ in framebuffer  */
				c = d->framebuffer[fb_addr] * 3;
				r = d->rgb_palette[c + 0];
				g = d->rgb_palette[c + 1];
				b = d->rgb_palette[c + 2];
				color_r += r;
				color_g += g;
				color_b += b;
			    }

			r = color_r / scaledownXscaledown;
			g = color_g / scaledownXscaledown;
			b = color_b / scaledownXscaledown;
			macro_put_pixel;
			x++;
		}
	} else {
		/*  Generic > 8 bit bit-depth:  */
		for (pixel=0; pixel<npixels; pixel++) {
			int subx, suby, r, g, b;
			int64_t color_r = 0, color_g = 0, color_b = 0;
			for (suby=0; suby<scaledown; suby++)
			    for (subx=0; subx<scaledown; subx++) {
				int fb_x, fb_y, fb_addr;

				fb_x = x * scaledown + subx;
				fb_y = y * scaledown + suby;
				fb_addr = fb_y * d->xsize + fb_x;
				fb_addr = (fb_addr * d->bit_depth) >> 3;
				/*  fb_addr is which _byte_ in framebuffer  */

				/*  > 8 bits color.  */
				switch (d->bit_depth) {
				case 24:
				case 32:
					r = d->framebuffer[fb_addr];
					g = d->framebuffer[fb_addr + 1];
					b = d->framebuffer[fb_addr + 2];
					break;
				case 16:
					if (d->vfb_type == VFB_HPC) {
						b = d->framebuffer[fb_addr] +
						    (d->framebuffer[fb_addr+1] << 8);

						if (d->color32k) {
							r = b >> 11;
							g = b >> 5;
							r = r & 31;
							g = (g & 31) * 2;
							b = b & 31;
						} else if (d->psp_15bit) {
							int tmp;
							r = (b >> 10) & 0x1f;
							g = (b >>  5) & 0x1f;
							b = b & 0x1f;
							g <<= 1;
							tmp = r; r = b; b = tmp;
						} else {
							r = (b >> 11) & 0x1f;
							g = (b >>  5) & 0x3f;
							b = b & 0x1f;
						}
					} else {
					    r = d->framebuffer[fb_addr] >> 3;
/*  HUH? TODO:  */
					    g = (d->framebuffer[fb_addr] << 5) +
					      (d->framebuffer[fb_addr + 1] >>5);
					    b = d->framebuffer[fb_addr + 1]&31;
					}

					r *= 8;
					g *= 4;
					b *= 8;
					break;
				default:
					r = g = b = random() & 255;
				}
				color_r += r;
				color_g += g;
				color_b += b;
			    }
			r = color_r / scaledownXscaledown;
			g = color_g / scaledownXscaledown;
			b = color_b / scaledownXscaledown;
			macro_put_pixel;
			x++;
		}
	}
#endif	/*  FB_SCALEDOWN  */
}
