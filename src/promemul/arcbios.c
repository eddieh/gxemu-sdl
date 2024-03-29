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
 *  COMMENT: ARCBIOS and ARCS emulation
 *
 *  Some good info here:
 *	https://www.linux-mips.org/wiki/ARC
 *
 */

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/resource.h>

#include "arcbios.h"
#include "console.h"
#include "cpu.h"
#include "cpu_mips.h"
#include "diskimage.h"
#include "machine.h"
#include "machine_arc.h"
#include "memory.h"
#include "misc.h"

#include "thirdparty/arcbios_other.h"


extern int quiet_mode;
extern int verbose;


/*
 *  arcbios_add_string_to_component():
 */
void arcbios_add_string_to_component(struct machine *machine,
	char *str, uint64_t component)
{
	if (machine->md.arc->n_string_to_components
	    >= MAX_STRING_TO_COMPONENT) {
		printf("Too many string-to-component mappings.\n");
		exit(1);
	}

	CHECK_ALLOCATION(machine->md.arc->string_to_component[machine->
	    md.arc->n_string_to_components] = strdup(str));

	debug("adding ARC component mapping: 0x%08x = %s\n",
	    (int)component, str);

	machine->md.arc->string_to_component_value[
	    machine->md.arc->n_string_to_components] = component;

	machine->md.arc->n_string_to_components ++;
}


/*
 *  arcbios_get_dsp_stat():
 *
 *  Fills in an arcbios_dsp_stat struct with valid data.
 */
static void arcbios_get_dsp_stat(struct cpu *cpu,
	struct arcbios_dsp_stat *dspstat)
{
	memset(dspstat, 0, sizeof(struct arcbios_dsp_stat));

	store_16bit_word_in_host(cpu, (unsigned char *)&dspstat->
	    CursorXPosition, cpu->machine->md.arc->console_curx + 1);
	store_16bit_word_in_host(cpu, (unsigned char *)&dspstat->
	    CursorYPosition, cpu->machine->md.arc->console_cury + 1);
	store_16bit_word_in_host(cpu, (unsigned char *)&dspstat->
	    CursorMaxXPosition, ARC_CONSOLE_MAX_X);
	store_16bit_word_in_host(cpu, (unsigned char *)&dspstat->
	    CursorMaxYPosition, ARC_CONSOLE_MAX_Y);
	dspstat->ForegroundColor = cpu->machine->md.arc->console_curcolor;
	dspstat->HighIntensity = cpu->machine->md.arc->console_curcolor ^ 0x08;
}


/*
 *  arcbios_putcell():
 */
static void arcbios_putcell(struct cpu *cpu, int ch, int x, int y)
{
	unsigned char buf[2];
	buf[0] = ch;
	buf[1] = cpu->machine->md.arc->console_curcolor;
	if (cpu->machine->md.arc->console_reverse)
		buf[1] = ((buf[1] & 0x70) >> 4) | ((buf[1] & 7) << 4)
		    | (buf[1] & 0x88);
	cpu->memory_rw(cpu, cpu->mem, cpu->machine->md.arc->console_vram +
	    2*(x + cpu->machine->md.arc->console_maxx * y),
	    &buf[0], sizeof(buf), MEM_WRITE,
	    CACHE_NONE | PHYSICAL);
}


/*
 *  handle_esc_seq():
 *
 *  Used by arcbios_putchar().
 */
static void handle_esc_seq(struct cpu *cpu)
{
	int i, len = strlen(cpu->machine->md.arc->escape_sequence);
	int row, col, color, code, start, stop;
	char *p;

	if (cpu->machine->md.arc->escape_sequence[0] != '[')
		return;

	code = cpu->machine->md.arc->escape_sequence[len-1];
	cpu->machine->md.arc->escape_sequence[len-1] = '\0';

	switch (code) {
	case 'm':
		color = atoi(cpu->machine->md.arc->escape_sequence + 1);
		switch (color) {
		case 0:	/*  Default.  */
			cpu->machine->md.arc->console_curcolor = 0x1f;
			cpu->machine->md.arc->console_reverse = 0; break;
		case 1:	/*  "Bold".  */
			cpu->machine->md.arc->console_curcolor |= 0x08; break;
		case 7:	/*  "Reverse".  */
			cpu->machine->md.arc->console_reverse = 1; break;
		case 30: /*  Black foreground.  */
			cpu->machine->md.arc->console_curcolor &= 0xf0;
			cpu->machine->md.arc->console_curcolor |= 0x00; break;
		case 31: /*  Red foreground.  */
			cpu->machine->md.arc->console_curcolor &= 0xf0;
			cpu->machine->md.arc->console_curcolor |= 0x04; break;
		case 32: /*  Green foreground.  */
			cpu->machine->md.arc->console_curcolor &= 0xf0;
			cpu->machine->md.arc->console_curcolor |= 0x02; break;
		case 33: /*  Yellow foreground.  */
			cpu->machine->md.arc->console_curcolor &= 0xf0;
			cpu->machine->md.arc->console_curcolor |= 0x06; break;
		case 34: /*  Blue foreground.  */
			cpu->machine->md.arc->console_curcolor &= 0xf0;
			cpu->machine->md.arc->console_curcolor |= 0x01; break;
		case 35: /*  Red-blue foreground.  */
			cpu->machine->md.arc->console_curcolor &= 0xf0;
			cpu->machine->md.arc->console_curcolor |= 0x05; break;
		case 36: /*  Green-blue foreground.  */
			cpu->machine->md.arc->console_curcolor &= 0xf0;
			cpu->machine->md.arc->console_curcolor |= 0x03; break;
		case 37: /*  White foreground.  */
			cpu->machine->md.arc->console_curcolor &= 0xf0;
			cpu->machine->md.arc->console_curcolor |= 0x07; break;
		case 40: /*  Black background.  */
			cpu->machine->md.arc->console_curcolor &= 0x0f;
			cpu->machine->md.arc->console_curcolor |= 0x00; break;
		case 41: /*  Red background.  */
			cpu->machine->md.arc->console_curcolor &= 0x0f;
			cpu->machine->md.arc->console_curcolor |= 0x40; break;
		case 42: /*  Green background.  */
			cpu->machine->md.arc->console_curcolor &= 0x0f;
			cpu->machine->md.arc->console_curcolor |= 0x20; break;
		case 43: /*  Yellow background.  */
			cpu->machine->md.arc->console_curcolor &= 0x0f;
			cpu->machine->md.arc->console_curcolor |= 0x60; break;
		case 44: /*  Blue background.  */
			cpu->machine->md.arc->console_curcolor &= 0x0f;
			cpu->machine->md.arc->console_curcolor |= 0x10; break;
		case 45: /*  Red-blue background.  */
			cpu->machine->md.arc->console_curcolor &= 0x0f;
			cpu->machine->md.arc->console_curcolor |= 0x50; break;
		case 46: /*  Green-blue background.  */
			cpu->machine->md.arc->console_curcolor &= 0x0f;
			cpu->machine->md.arc->console_curcolor |= 0x30; break;
		case 47: /*  White background.  */
			cpu->machine->md.arc->console_curcolor &= 0x0f;
			cpu->machine->md.arc->console_curcolor |= 0x70; break;
		default:fatal("{ handle_esc_seq: color %i }\n", color);
		}
		return;
	case 'H':
		p = strchr(cpu->machine->md.arc->escape_sequence, ';');
		if (p == NULL)
			return;		/*  TODO  */
		row = atoi(cpu->machine->md.arc->escape_sequence + 1);
		col = atoi(p + 1);
		if (col < 1)
			col = 1;
		if (row < 1)
			row = 1;
		cpu->machine->md.arc->console_curx = col - 1;
		cpu->machine->md.arc->console_cury = row - 1;
		return;
	case 'J':
		/*
		 *  J = clear screen below cursor, and the rest of the
		 *      current line,
		 *  2J = clear whole screen.
		 */
		i = atoi(cpu->machine->md.arc->escape_sequence + 1);
		if (i != 0 && i != 2)
			fatal("{ handle_esc_seq(): %iJ }\n", i);
		if (i == 0)
			for (col = cpu->machine->md.arc->console_curx;
			    col < cpu->machine->md.arc->console_maxx; col++)
				arcbios_putcell(cpu, ' ', col,
				    cpu->machine->md.arc->console_cury);
		for (col = 0; col < cpu->machine->md.arc->console_maxx; col++)
			for (row = i? 0 : cpu->machine->md.arc->console_cury+1;
			    row < cpu->machine->md.arc->console_maxy; row++)
				arcbios_putcell(cpu, ' ', col, row);
		return;
	case 'K':
		col = atoi(cpu->machine->md.arc->escape_sequence + 1);
		/*  2 = clear line to the right. 1 = to the left (?)  */
		start = 0; stop = cpu->machine->md.arc->console_curx;
		if (col == 2) {
			start = cpu->machine->md.arc->console_curx;
			stop = cpu->machine->md.arc->console_maxx - 1;
		}
		for (i=start; i<=stop; i++)
			arcbios_putcell(cpu, ' ', i,
			    cpu->machine->md.arc->console_cury);

		return;
	}

	fatal("{ handle_esc_seq(): unimplemented escape sequence: ");
	for (i=0; i<len; i++) {
		int x = cpu->machine->md.arc->escape_sequence[i];
		if (i == len-1)
			x = code;

		if (x >= ' ' && x < 127)
			fatal("%c", x);
		else
			fatal("[0x%02x]", x);
	}
	fatal(" }\n");
}


/*
 *  scroll_if_necessary():
 */
static void scroll_if_necessary(struct cpu *cpu)
{
	/*  Scroll?  */
	if (cpu->machine->md.arc->console_cury >=
	    cpu->machine->md.arc->console_maxy) {
		unsigned char buf[2];
		int x, y;
		for (y=0; y<cpu->machine->md.arc->console_maxy-1; y++)
			for (x=0; x<cpu->machine->md.arc->console_maxx;
			    x++) {
				cpu->memory_rw(cpu, cpu->mem,
				    cpu->machine->md.arc->console_vram +
				    2*(x + cpu->machine->md.arc->
					console_maxx * (y+1)),
				    &buf[0], sizeof(buf), MEM_READ,
				    CACHE_NONE | PHYSICAL);
				cpu->memory_rw(cpu, cpu->mem,
				    cpu->machine->md.arc->console_vram +
				    2*(x + cpu->machine->md.arc->
					console_maxx * y),
				    &buf[0], sizeof(buf), MEM_WRITE,
				    CACHE_NONE | PHYSICAL);
			}

		cpu->machine->md.arc->console_cury =
		    cpu->machine->md.arc->console_maxy - 1;

		for (x=0; x<cpu->machine->md.arc->console_maxx; x++)
			arcbios_putcell(cpu, ' ', x,
			    cpu->machine->md.arc->console_cury);
	}
}


/*
 *  arcbios_putchar():
 *
 *  If we're using X11 with VGA-style console, then output to that console.
 *  Otherwise, use console_putchar().
 */
static void arcbios_putchar(struct cpu *cpu, int ch)
{
	int addr;
	unsigned char byte;

	if (!cpu->machine->md.arc->vgaconsole) {
		/*  Text console output:  */

		/*  Hack for Windows NT, which uses 0x9b instead of ESC + [  */
		if (ch == 0x9b) {
			console_putchar(cpu->machine->main_console_handle, 27);
			ch = '[';
		}
		console_putchar(cpu->machine->main_console_handle, ch);
		return;
	}

	if (cpu->machine->md.arc->in_escape_sequence) {
		int len = strlen(cpu->machine->md.arc->escape_sequence);
		cpu->machine->md.arc->escape_sequence[len] = ch;
		len++;
		if (len >= ARC_MAX_ESC)
			len = ARC_MAX_ESC;
		cpu->machine->md.arc->escape_sequence[len] = '\0';
		if ((ch >= 'a' && ch <= 'z') ||
		    (ch >= 'A' && ch <= 'Z') || len >= ARC_MAX_ESC) {
			handle_esc_seq(cpu);
			cpu->machine->md.arc->in_escape_sequence = 0;
		}
	} else {
		if (ch == 27) {
			cpu->machine->md.arc->in_escape_sequence = 1;
			cpu->machine->md.arc->escape_sequence[0] = '\0';
		} else if (ch == 0x9b) {
			cpu->machine->md.arc->in_escape_sequence = 1;
			cpu->machine->md.arc->escape_sequence[0] = '[';
			cpu->machine->md.arc->escape_sequence[1] = '\0';
		} else if (ch == '\b') {
			if (cpu->machine->md.arc->console_curx > 0)
				cpu->machine->md.arc->console_curx --;
		} else if (ch == '\r') {
			cpu->machine->md.arc->console_curx = 0;
		} else if (ch == '\n') {
			cpu->machine->md.arc->console_cury ++;
		} else if (ch == '\t') {
			cpu->machine->md.arc->console_curx =
			    ((cpu->machine->md.arc->console_curx - 1)
			    | 7) + 1;
			/*  TODO: Print spaces?  */
		} else {
			/*  Put char:  */
			if (cpu->machine->md.arc->console_curx >=
			    cpu->machine->md.arc->console_maxx) {
				cpu->machine->md.arc->console_curx = 0;
				cpu->machine->md.arc->console_cury ++;
				scroll_if_necessary(cpu);
			}
			arcbios_putcell(cpu, ch,
			    cpu->machine->md.arc->console_curx,
			    cpu->machine->md.arc->console_cury);
			cpu->machine->md.arc->console_curx ++;
		}
	}

	scroll_if_necessary(cpu);

	/*  Update cursor position:  */
	addr = (cpu->machine->md.arc->console_curx >=
	    cpu->machine->md.arc->console_maxx?
	    cpu->machine->md.arc->console_maxx - 1 :
		cpu->machine->md.arc->console_curx) +
	    cpu->machine->md.arc->console_cury *
	    cpu->machine->md.arc->console_maxx;
	byte = 0x0e;
	cpu->memory_rw(cpu, cpu->mem, cpu->machine->md.arc->
	    console_ctrlregs + 0x14,
	    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE | PHYSICAL);
	byte = (addr >> 8) & 255;
	cpu->memory_rw(cpu, cpu->mem, cpu->machine->md.arc->
	    console_ctrlregs + 0x15,
	    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE | PHYSICAL);
	byte = 0x0f;
	cpu->memory_rw(cpu, cpu->mem, cpu->machine->md.arc->
	    console_ctrlregs + 0x14,
	    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE | PHYSICAL);
	byte = addr & 255;
	cpu->memory_rw(cpu, cpu->mem, cpu->machine->md.arc->
	    console_ctrlregs + 0x15,
	    &byte, sizeof(byte), MEM_WRITE, CACHE_NONE | PHYSICAL);
}


/*
 *  arcbios_putstring():
 */
static void arcbios_putstring(struct cpu *cpu, const char *s)
{
	while (*s) {
		if (*s == '\n')
			arcbios_putchar(cpu, '\r');
		arcbios_putchar(cpu, *s++);
	}
}


/*
 *  arcbios_register_scsicontroller():
 */
void arcbios_register_scsicontroller(struct machine *machine,
	uint64_t scsicontroller_component)
{
	machine->md.arc->scsicontroller = scsicontroller_component;
}


/*
 *  arcbios_get_scsicontroller():
 */
uint64_t arcbios_get_scsicontroller(struct machine *machine)
{
	return machine->md.arc->scsicontroller;
}


/*
 *  arcbios_add_memory_descriptor():
 *
 *  NOTE: arctype is the ARC type, not the SGI type. This function takes
 *  care of converting, when necessary.
 */
void arcbios_add_memory_descriptor(struct cpu *cpu,
	uint64_t base, uint64_t len, int arctype)
{
	uint64_t memdesc_addr;
	int s;
	struct arcbios_mem arcbios_mem;
	struct arcbios_mem64 arcbios_mem64;

	if (verbose >= 2)
		debug("arcbios_add_memory_descriptor: base=0x%llx len=0x%llx arctype=%i\n",
			(long long)base, (long long)len, arctype);

	base /= 4096;
	len /= 4096;

	/*
	 *  TODO: Huh? Why isn't it necessary to convert from arc to sgi types?
	 *
	 *  TODO 2: It seems that it _is_ necessary, but NetBSD's arcdiag
	 *  doesn't handle the sgi case separately.
	 */
#if 1
	if (cpu->machine->machine_type == MACHINE_SGI) {
		/*  arctype is SGI style  */
		/*  printf("%i => ", arctype); */
		switch (arctype) {
		case 0:	arctype = 0; break;
		case 1:	arctype = 1; break;
		case 2:	arctype = 3; break;
		case 3:	arctype = 4; break;
		case 4:	arctype = 5; break;
		case 5:	arctype = 6; break;
		case 6:	arctype = 7; break;
		case 7:	arctype = 2; break;
		}
		/*  printf("%i\n", arctype);  */
	}
#endif
	if (cpu->machine->md.arc->arc_64bit)
		s = sizeof(arcbios_mem64);
	else
		s = sizeof(arcbios_mem);

	memdesc_addr = cpu->machine->md.arc->memdescriptor_base +
	    cpu->machine->md.arc->n_memdescriptors * s;

	if (cpu->machine->md.arc->arc_64bit) {
		memset(&arcbios_mem64, 0, s);
		store_32bit_word_in_host(cpu,
		    (unsigned char *)&arcbios_mem64.Type, arctype);
		store_64bit_word_in_host(cpu,
		    (unsigned char *)&arcbios_mem64.BasePage, base);
		store_64bit_word_in_host(cpu,
		    (unsigned char *)&arcbios_mem64.PageCount, len);
		store_buf(cpu, memdesc_addr, (char *)&arcbios_mem64, s);
	} else {
		memset(&arcbios_mem, 0, s);
		store_32bit_word_in_host(cpu,
		    (unsigned char *)&arcbios_mem.Type, arctype);
		store_32bit_word_in_host(cpu,
		    (unsigned char *)&arcbios_mem.BasePage, base);
		store_32bit_word_in_host(cpu,
		    (unsigned char *)&arcbios_mem.PageCount, len);
		store_buf(cpu, memdesc_addr, (char *)&arcbios_mem, s);
	}

	cpu->machine->md.arc->n_memdescriptors ++;
}


/*
 *  arcbios_addchild():
 *
 *  host_tmp_component is a temporary component, with data formated for
 *  the host system.  It needs to be translated/copied into emulated RAM.
 *
 *  Return value is the virtual (emulated) address of the added component.
 *
 *  TODO:  This function doesn't care about memory management, but simply
 *         stores the new child after the last stored child.
 *  TODO:  This stuff is really ugly.
 */
static uint64_t arcbios_addchild(struct cpu *cpu,
	struct arcbios_component *host_tmp_component,
	const char *identifier, uint32_t parent)
{
	struct machine *machine = cpu->machine;
	uint64_t a = machine->md.arc->next_component_address;
	uint32_t peer=0;
	uint32_t child=0;
	int n_left;
	uint64_t peeraddr = FIRST_ARC_COMPONENT;

	/*
	 *  This component has no children yet, but it may have peers (that is,
	 *  other components that share this component's parent) so we have to
	 *  set the peer value correctly.
	 *
	 *  Also, if this is the first child of some parent, the parent's child
	 *  pointer should be set to point to this component.  (But only if it
	 *  is the first.)
	 *
	 *  This is really ugly:  scan through all components, starting from
	 *  FIRST_ARC_COMPONENT, to find a component with the same parent as
	 *  this component will have.  If such a component is found, and its
	 *  'peer' value is NULL, then set it to this component's address (a).
	 *
	 *  TODO:  make this nicer
	 */

	n_left = machine->md.arc->n_components;
	while (n_left > 0) {
		/*  Load parent, child, and peer values:  */
		uint32_t eparent, echild, epeer, tmp;
		unsigned char buf[4];

		/*  debug("[ addchild: peeraddr = 0x%08x ]\n",
		    (int)peeraddr);  */

		cpu->memory_rw(cpu, cpu->mem,
		    peeraddr + 0 * machine->md.arc->wordlen, &buf[0],
		    sizeof(eparent), MEM_READ, CACHE_NONE);
		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			unsigned char tmp2;
			tmp2 = buf[0]; buf[0] = buf[3]; buf[3] = tmp2;
			tmp2 = buf[1]; buf[1] = buf[2]; buf[2] = tmp2;
		}
		epeer   = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24);

		cpu->memory_rw(cpu, cpu->mem, peeraddr + 1 *
		    machine->md.arc->wordlen,
		    &buf[0], sizeof(eparent), MEM_READ, CACHE_NONE);
		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			unsigned char tmp2;
			tmp2 = buf[0]; buf[0] = buf[3]; buf[3] = tmp2;
			tmp2 = buf[1]; buf[1] = buf[2]; buf[2] = tmp2;
		}
		echild  = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24);

		cpu->memory_rw(cpu, cpu->mem, peeraddr + 2 *
		    machine->md.arc->wordlen,
		    &buf[0], sizeof(eparent), MEM_READ, CACHE_NONE);
		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			unsigned char tmp2;
			tmp2 = buf[0]; buf[0] = buf[3]; buf[3] = tmp2;
			tmp2 = buf[1]; buf[1] = buf[2]; buf[2] = tmp2;
		}
		eparent = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24);

		/*  debug("  epeer=%x echild=%x eparent=%x\n",
		    (int)epeer,(int)echild,(int)eparent);  */

		if ((uint32_t)eparent == (uint32_t)parent &&
		    (uint32_t)epeer == 0) {
			epeer = a;
			store_32bit_word(cpu, peeraddr + 0x00, epeer);
			/*  debug("[ addchild: adding 0x%08x as peer "
			    "to 0x%08x ]\n", (int)a, (int)peeraddr);  */
		}
		if ((uint32_t)peeraddr == (uint32_t)parent &&
		    (uint32_t)echild == 0) {
			echild = a;
			store_32bit_word(cpu, peeraddr + 0x04, echild);
			/*  debug("[ addchild: adding 0x%08x as "
			    "child to 0x%08x ]\n", (int)a, (int)peeraddr);  */
		}

		/*  Go to the next component:  */
		cpu->memory_rw(cpu, cpu->mem, peeraddr + 0x28, &buf[0],
		    sizeof(eparent), MEM_READ, CACHE_NONE);
		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			unsigned char tmp2;
			tmp2 = buf[0]; buf[0] = buf[3]; buf[3] = tmp2;
			tmp2 = buf[1]; buf[1] = buf[2]; buf[2] = tmp2;
		}

		tmp = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24);
		peeraddr += 0x30;
		peeraddr += tmp + 1;
		peeraddr = ((peeraddr - 1) | 3) + 1;

		n_left --;
	}

	store_32bit_word(cpu, a + 0x00, peer);
	store_32bit_word(cpu, a + 0x04, child);
	store_32bit_word(cpu, a + 0x08, parent);
	store_32bit_word(cpu, a+  0x0c, host_tmp_component->Class);
	store_32bit_word(cpu, a+  0x10, host_tmp_component->Type);
	store_32bit_word(cpu, a+  0x14, host_tmp_component->Flags +
	    65536 * host_tmp_component->Version);
	store_32bit_word(cpu, a+  0x18, host_tmp_component->Revision);
	store_32bit_word(cpu, a+  0x1c, host_tmp_component->Key);
	store_32bit_word(cpu, a+  0x20, host_tmp_component->AffinityMask);
	store_32bit_word(cpu, a+  0x24, host_tmp_component->
	    ConfigurationDataSize);
	store_32bit_word(cpu, a+  0x28, host_tmp_component->IdentifierLength);
	store_32bit_word(cpu, a+  0x2c, host_tmp_component->Identifier);

	machine->md.arc->next_component_address += 0x30;

	if (host_tmp_component->IdentifierLength != 0) {
		store_32bit_word(cpu, a + 0x2c, a + 0x30);
		store_string(cpu, a + 0x30, identifier);
		if (identifier != NULL)
			machine->md.arc->next_component_address +=
			    strlen(identifier) + 1;
	}

	machine->md.arc->next_component_address ++;

	/*  Round up to next 0x4 bytes:  */
	machine->md.arc->next_component_address =
	    ((machine->md.arc->next_component_address - 1) | 3) + 1;

	machine->md.arc->n_components ++;

	return a;
}


/*
 *  arcbios_addchild64():
 *
 *  host_tmp_component is a temporary component, with data formated for
 *  the host system.  It needs to be translated/copied into emulated RAM.
 *
 *  Return value is the virtual (emulated) address of the added component.
 *
 *  TODO:  This function doesn't care about memory management, but simply
 *         stores the new child after the last stored child.
 *  TODO:  This stuff is really ugly.
 */
static uint64_t arcbios_addchild64(struct cpu *cpu,
	struct arcbios_component64 *host_tmp_component,
	const char *identifier, uint64_t parent)
{
	struct machine *machine = cpu->machine;
	uint64_t a = machine->md.arc->next_component_address;
	uint64_t peer=0;
	uint64_t child=0;
	int n_left;
	uint64_t peeraddr = FIRST_ARC_COMPONENT;

	/*
	 *  This component has no children yet, but it may have peers (that is,
	 *  other components that share this component's parent) so we have to
	 *  set the peer value correctly.
	 *
	 *  Also, if this is the first child of some parent, the parent's child
	 *  pointer should be set to point to this component.  (But only if it
	 *  is the first.)
	 *
	 *  This is really ugly:  scan through all components, starting from
	 *  FIRST_ARC_COMPONENT, to find a component with the same parent as
	 *  this component will have.  If such a component is found, and its
	 *  'peer' value is NULL, then set it to this component's address (a).
	 *
	 *  TODO:  make this nicer
	 */

	n_left = machine->md.arc->n_components;
	while (n_left > 0) {
		/*  Load parent, child, and peer values:  */
		uint64_t eparent, echild, epeer, tmp;
		unsigned char buf[8];

		/*  debug("[ addchild: peeraddr = 0x%016" PRIx64" ]\n",
		    (uint64_t) peeraddr);  */

		cpu->memory_rw(cpu, cpu->mem,
		    peeraddr + 0 * machine->md.arc->wordlen, &buf[0],
		    sizeof(eparent), MEM_READ, CACHE_NONE);
		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			unsigned char tmp2;
			tmp2 = buf[0]; buf[0] = buf[7]; buf[7] = tmp2;
			tmp2 = buf[1]; buf[1] = buf[6]; buf[6] = tmp2;
			tmp2 = buf[2]; buf[2] = buf[5]; buf[5] = tmp2;
			tmp2 = buf[3]; buf[3] = buf[4]; buf[4] = tmp2;
		}
		epeer   = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24)
		    + ((uint64_t)buf[4] << 32) + ((uint64_t)buf[5] << 40)
		    + ((uint64_t)buf[6] << 48) + ((uint64_t)buf[7] << 56);

		cpu->memory_rw(cpu, cpu->mem, peeraddr + 1 *
		    machine->md.arc->wordlen,
		    &buf[0], sizeof(eparent), MEM_READ, CACHE_NONE);
		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			unsigned char tmp2;
			tmp2 = buf[0]; buf[0] = buf[7]; buf[7] = tmp2;
			tmp2 = buf[1]; buf[1] = buf[6]; buf[6] = tmp2;
			tmp2 = buf[2]; buf[2] = buf[5]; buf[5] = tmp2;
			tmp2 = buf[3]; buf[3] = buf[4]; buf[4] = tmp2;
		}
		echild  = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24)
		    + ((uint64_t)buf[4] << 32) + ((uint64_t)buf[5] << 40)
		    + ((uint64_t)buf[6] << 48) + ((uint64_t)buf[7] << 56);

		cpu->memory_rw(cpu, cpu->mem, peeraddr + 2 *
		    machine->md.arc->wordlen,
		    &buf[0], sizeof(eparent), MEM_READ, CACHE_NONE);
		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			unsigned char tmp2;
			tmp2 = buf[0]; buf[0] = buf[7]; buf[7] = tmp2;
			tmp2 = buf[1]; buf[1] = buf[6]; buf[6] = tmp2;
			tmp2 = buf[2]; buf[2] = buf[5]; buf[5] = tmp2;
			tmp2 = buf[3]; buf[3] = buf[4]; buf[4] = tmp2;
		}
		eparent = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24)
		    + ((uint64_t)buf[4] << 32) + ((uint64_t)buf[5] << 40)
		    + ((uint64_t)buf[6] << 48) + ((uint64_t)buf[7] << 56);

		/*  debug("  epeer=%" PRIx64" echild=%" PRIx64" eparent=%" PRIx64
		    "\n", (uint64_t) epeer, (uint64_t) echild,
		    (uint64_t) eparent);  */

		if (eparent == parent && epeer == 0) {
			epeer = a;
			store_64bit_word(cpu, peeraddr + 0 *
			    machine->md.arc->wordlen, epeer);
			/*  debug("[ addchild: adding 0x%016" PRIx64" as peer "
			    "to 0x%016" PRIx64" ]\n", (uint64_t) a,
			    (uint64_t) peeraddr);  */
		}
		if (peeraddr == parent && echild == 0) {
			echild = a;
			store_64bit_word(cpu, peeraddr + 1 *
			    machine->md.arc->wordlen, echild);
			/*  debug("[ addchild: adding 0x%016" PRIx64" as child "
			    "to 0x%016" PRIx64" ]\n", (uint64_t) a,
			    (uint64_t) peeraddr);  */
		}

		/*  Go to the next component:  */
		cpu->memory_rw(cpu, cpu->mem, peeraddr + 0x34,
		    &buf[0], sizeof(uint32_t), MEM_READ, CACHE_NONE);
		if (cpu->byte_order == EMUL_BIG_ENDIAN) {
			unsigned char tmp2;
			tmp2 = buf[0]; buf[0] = buf[3]; buf[3] = tmp2;
			tmp2 = buf[1]; buf[1] = buf[2]; buf[2] = tmp2;
		}
		tmp = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24);

		tmp &= 0xfffff;

		peeraddr += 0x50;
		peeraddr += tmp + 1;
		peeraddr = ((peeraddr - 1) | 3) + 1;

		n_left --;
	}

	store_64bit_word(cpu, a + 0x00, peer);
	store_64bit_word(cpu, a + 0x08, child);
	store_64bit_word(cpu, a + 0x10, parent);
	store_32bit_word(cpu, a+  0x18, host_tmp_component->Class);
	store_32bit_word(cpu, a+  0x1c, host_tmp_component->Type);
	store_32bit_word(cpu, a+  0x20, host_tmp_component->Flags);
	store_32bit_word(cpu, a+  0x24, host_tmp_component->Version +
	    ((uint64_t)host_tmp_component->Revision << 16));
	store_32bit_word(cpu, a+  0x28, host_tmp_component->Key);
	store_64bit_word(cpu, a+  0x30, host_tmp_component->AffinityMask);
	store_64bit_word(cpu, a+  0x38, host_tmp_component->
	    ConfigurationDataSize);
	store_64bit_word(cpu, a+  0x40, host_tmp_component->IdentifierLength);
	store_64bit_word(cpu, a+  0x48, host_tmp_component->Identifier);

	/*  TODO: Find out how a REAL ARCS64 implementation does it.  */

	machine->md.arc->next_component_address += 0x50;

	if (host_tmp_component->IdentifierLength != 0) {
		store_64bit_word(cpu, a + 0x48, a + 0x50);
		store_string(cpu, a + 0x50, identifier);
		if (identifier != NULL)
			machine->md.arc->next_component_address +=
			    strlen(identifier) + 1;
	}

	machine->md.arc->next_component_address ++;

	/*  Round up to next 0x8 bytes:  */
	machine->md.arc->next_component_address =
	    ((machine->md.arc->next_component_address - 1) | 7) + 1;

	machine->md.arc->n_components ++;
	return a;
}


/*
 *  arcbios_addchild_manual():
 *
 *  Used internally to set up component structures.
 *  Parent may only be NULL for the first (system) component.
 *
 *  Return value is the virtual (emulated) address of the added component.
 */
uint64_t arcbios_addchild_manual(struct cpu *cpu,
	uint64_t cclass, uint64_t type, uint64_t flags,
	uint64_t version, uint64_t revision, uint64_t key,
	uint64_t affinitymask, const char *identifier, uint64_t parent,
	void *config_data, size_t config_len)
{
	struct machine *machine = cpu->machine;
	/*  This component is only for temporary use:  */
	struct arcbios_component component;
	struct arcbios_component64 component64;

	if (config_data != NULL) {
		unsigned char *p = (unsigned char *) config_data;
		size_t i;

		if (machine->md.arc->n_configuration_data >= MAX_CONFIG_DATA) {
			printf("fatal error: you need to increase "
			    "MAX_CONFIG_DATA\n");
			exit(1);
		}

		for (i=0; i<config_len; i++) {
			unsigned char ch = p[i];
			cpu->memory_rw(cpu, cpu->mem,
			    machine->md.arc->configuration_data_next_addr + i,
			    &ch, 1, MEM_WRITE, CACHE_NONE);
		}

		machine->md.arc->configuration_data_len[
		    machine->md.arc->n_configuration_data] = config_len;
		machine->md.arc->configuration_data_configdata[
		    machine->md.arc->n_configuration_data] =
		    machine->md.arc->configuration_data_next_addr;
		machine->md.arc->configuration_data_next_addr += config_len;
		machine->md.arc->configuration_data_component[
		    machine->md.arc->n_configuration_data] =
		    machine->md.arc->next_component_address +
		    (cpu->machine->md.arc->arc_64bit? 0x18 : 0x0c);

		/*  printf("& ADDING %i: configdata=0x%016" PRIx64" "
		    "component=0x%016" PRIx64"\n",
		     machine->md.arc->n_configuration_data,
		    (uint64_t) machine->md.arc->configuration_data_configdata[
			machine->md.arc->n_configuration_data],
		    (uint64_t) machine->md.arc->configuration_data_component[
			machine->md.arc->n_configuration_data]);  */

		machine->md.arc->n_configuration_data ++;
	}

	if (!cpu->machine->md.arc->arc_64bit) {
		component.Class                 = cclass;
		component.Type                  = type;
		component.Flags                 = flags;
		component.Version               = version;
		component.Revision              = revision;
		component.Key                   = key;
		component.AffinityMask          = affinitymask;
		component.ConfigurationDataSize = config_len;
		component.IdentifierLength      = 0;
		component.Identifier            = 0;
		if (identifier != NULL) {
			component.IdentifierLength = strlen(identifier) + 1;
		}

		return arcbios_addchild(cpu, &component, identifier, parent);
	} else {
		component64.Class                 = cclass;
		component64.Type                  = type;
		component64.Flags                 = flags;
		component64.Version               = version;
		component64.Revision              = revision;
		component64.Key                   = key;
		component64.AffinityMask          = affinitymask;
		component64.ConfigurationDataSize = config_len;
		component64.IdentifierLength      = 0;
		component64.Identifier            = 0;
		if (identifier != NULL) {
			component64.IdentifierLength = strlen(identifier) + 1;
		}

		return arcbios_addchild64(cpu, &component64, identifier,
		    parent);
	}
}


/*
 *  arcbios_get_msdos_partition_size():
 *
 *  This function tries to parse MSDOS-style partition tables on a disk
 *  image, and return the starting offset (counted in bytes), and the
 *  size, of a specific partition.
 *
 *  NOTE: partition_nr is 1-based!
 *
 *  TODO: This is buggy, it doesn't really handle extended partitions.
 *
 *  See http://www.nondot.org/sabre/os/files/Partitions/Partitions.html
 *  for more info.
 */
static void arcbios_get_msdos_partition_size(struct machine *machine,
	int disk_id, int disk_type, int partition_nr, uint64_t *start,
	uint64_t *size)
{
	int res, i, partition_type, cur_partition = 0;
	unsigned char sector[512];
	unsigned char buf[16];
	uint64_t offset = 0, st;

	/*  Partition 0 is the entire disk image:  */
	*start = 0;
	*size = diskimage_getsize(machine, disk_id, disk_type);
	if (partition_nr == 0)
		return;

ugly_goto:
	*start = 0; *size = 0;

	/*  printf("reading MSDOS partition from offset 0x%" PRIx64"\n",
	    (uint64_t) offset);  */

	res = diskimage_access(machine, disk_id, disk_type, 0, offset,
	    sector, sizeof(sector));
	if (!res) {
		fatal("[ arcbios_get_msdos_partition_size(): couldn't "
		    "read the disk image, id %i, offset 0x%" PRIx64" ]\n",
		    disk_id, (uint64_t) offset);
		return;
	}

	if (sector[510] != 0x55 || sector[511] != 0xaa) {
		fatal("[ arcbios_get_msdos_partition_size(): not an "
		    "MSDOS partition table ]\n");
	}

#if 0
	/*  Debug dump:  */
	for (i=0; i<4; i++) {
		int j;
		printf("  partition %i: ", i+1);
		for (j=0; j<16; j++)
			printf(" %02x", sector[446 + i*16 + j]);
		printf("\n");
	}
#endif

	for (i=0; i<4; i++) {
		memmove(buf, sector + 446 + 16*i, 16);

		partition_type = buf[4];

		if (partition_type == 0)
			continue;

		st = (buf[8] + (buf[9] << 8) + (buf[10] << 16) +
		    (buf[11] << 24)) * 512;

		if (start != NULL)
			*start = st;
		if (size != NULL)
			*size = (buf[12] + (buf[13] << 8) + (buf[14] << 16) +
			    (buf[15] << 24)) * 512;

		/*  Extended DOS partition:  */
		if (partition_type == 5) {
			offset += st;
			goto ugly_goto;
		}

		/*  Found the right partition? Then return.  */
		cur_partition ++;
		if (cur_partition == partition_nr)
			return;
	}

	fatal("[ partition(%i) NOT found ]\n", partition_nr);
}


/*
 *  arcbios_handle_to_disk_id_and_type():
 */
static int arcbios_handle_to_disk_id_and_type(struct machine *machine, int handle, int *typep)
{
	const char *s;

	if (handle < 0 || handle >= ARC_MAX_HANDLES)
		return -1;

	s = machine->md.arc->file_handle_string[handle];
	if (s == NULL)
		return -1;

	/*
	 *  s is something like "scsi(0)disk(0)rdisk(0)partition(0)".
	 *  TODO: This is really ugly and hardcoded.
	 */

	if (strncmp(s, "pci(0)", 6) == 0)
		s += 6;

	if (strncmp(s, "scsi(0)", 7) == 0) {
		*typep = DISKIMAGE_SCSI;
		s += 7;
	} else {
		return -1;
	}

	if (strncmp(s, "disk(", 5) == 0) {
		return atoi(s + 5);
	}

	if (strncmp(s, "cdrom(", 6) == 0) {
		return atoi(s + 6);
	}

	return -1;
}


/*
 *  arcbios_handle_to_start_and_size():
 */
static void arcbios_handle_to_start_and_size(struct machine *machine,
	int handle, uint64_t *start, uint64_t *size)
{
	const char *s = machine->md.arc->file_handle_string[handle];
	const char *s2;
	int disk_id, disk_type;

	disk_id = arcbios_handle_to_disk_id_and_type(machine, handle, &disk_type);

	if (disk_id < 0)
		return;

	// Default to "full disk":
	*start = 0;
	*size = diskimage_getsize(machine, disk_id, disk_type);

	if (machine->machine_type == MACHINE_SGI) {
		/*
		 *  TODO:
		 *
		 *  Read the SGI volume header / disk label. e.g.:
		 *
		 *  partitions:
		 *	partition 0: 592820 blocks at 4096 (type 4)
		 *	partition 8: 4096 blocks at 0 (type 0)
		 *	partition 10: 596916 blocks at 0 (type 6)
		 *
		 *  Warn about accessing partitions that are not specified
		 *  with a size > 0.
		 */
		debugmsg(SUBSYS_PROMEMUL, "arcbios", VERBOSITY_WARNING, "TODO: sgi partition");
		return;
	}

	s2 = strstr(s, "partition(");
	if (s2 != NULL) {
		int partition_nr = atoi(s2 + 10);
		/*  printf("partition_nr = %i\n", partition_nr);  */
		if (partition_nr != 0)
			arcbios_get_msdos_partition_size(machine,
			    disk_id, disk_type, partition_nr, start, size);
	}
}


/*
 *  arcbios_getfileinformation():
 *
 *  Fill in a GetFileInformation struct in emulated memory,
 *  for a specific file handle. (This is used to get the size
 *  and offsets of partitions on disk images.)
 */
static int arcbios_getfileinformation(struct cpu *cpu)
{
	int handle = cpu->cd.mips.gpr[MIPS_GPR_A0];
	uint64_t addr = cpu->cd.mips.gpr[MIPS_GPR_A1];
	uint64_t start, size;

	arcbios_handle_to_start_and_size(cpu->machine, handle, &start, &size);

	store_64bit_word(cpu, addr + 0, 0);
	store_64bit_word(cpu, addr + 8, size);
	store_64bit_word(cpu, addr + 16, 0);
	store_32bit_word(cpu, addr + 24, 1);
	store_32bit_word(cpu, addr + 28, 0);
	store_32bit_word(cpu, addr + 32, 0);

	/*  printf("\n!!! size=0x%x start=0x%x\n", (int)size, (int)start);  */

	return ARCBIOS_ESUCCESS;
}


/*
 *  arcbios_private_emul():
 *
 *  The "normal" vector entries are probably from the ARC standard, whereas
 *  the "private" entries are unknown SGI specific entries. In the emulator,
 *  these are dummy implementations, just enough to fool IRIX to continue
 *  past these calls.
 *
 *  These names are guesses based on symbols on the callstack when "sash"
 *  calls these entries, perhaps the real function is something completely
 *  different.
 *
 *	0x00	ioctl (?)
 *	0x04	get nvram table (?)
 *	0x14	fs_register (?)
 *	0x1c	Signal (?)
 *	0x20	initGfxGui (?)
 *	0x24	sgivers (get version?)
 *	0x30	cpufreq (?)
 */
void arcbios_sgi_emul(struct cpu *cpu)
{
	int vector = cpu->pc & 0xfff;

	switch (vector) {

	case 0x00:
		debugmsg(SUBSYS_PROMEMUL, "arcbios", VERBOSITY_WARNING, "SGI: ioctl? TODO");
		cpu->cd.mips.gpr[MIPS_GPR_V0] = 0;
		break;
/*
	case 0x04:
		debugmsg(SUBSYS_PROMEMUL, "arcbios", VERBOSITY_WARNING, "SGI: get nvram table(): TODO");
		cpu->cd.mips.gpr[MIPS_GPR_V0] = 0;
		break;
*/
	case 0x14:
		debugmsg(SUBSYS_PROMEMUL, "arcbios", VERBOSITY_WARNING, "SGI: fs_register? TODO");
		cpu->cd.mips.gpr[MIPS_GPR_V0] = 0;
		break;

	case 0x1c:
		debugmsg(SUBSYS_PROMEMUL, "arcbios", VERBOSITY_WARNING, "SGI: Signal? TODO");
		cpu->cd.mips.gpr[MIPS_GPR_V0] = 0;
		break;

	case 0x20:
		debugmsg(SUBSYS_PROMEMUL, "arcbios", VERBOSITY_WARNING, "SGI: initGfxGui? TODO");
		cpu->cd.mips.gpr[MIPS_GPR_V0] = 0;
		break;

	case 0x24:
		debugmsg(SUBSYS_PROMEMUL, "arcbios", VERBOSITY_WARNING, "SGI: sgivers? TODO");
		cpu->cd.mips.gpr[MIPS_GPR_V0] = 0;
		break;

	case 0x30:
		// This value is displayed by hinv (in sash) as "Processor: xxx MHz"
		debugmsg(SUBSYS_PROMEMUL, "arcbios", VERBOSITY_DEBUG, "SGI: cpufreq");
		cpu->cd.mips.gpr[MIPS_GPR_V0] = 175;
		break;

	default:
		cpu_register_dump(cpu->machine, cpu, 1, 0x1);
		debug("a0 points to: ");
		dump_mem_string(cpu, cpu->cd.mips.gpr[MIPS_GPR_A0]);
		debug("\n");
		fatal("ARCBIOS: unimplemented SGI vector 0x%x\n", vector);
		cpu->running = 0;
	}
}


/*
 *  arcbios_emul():  ARCBIOS emulation
 *
 *	0x0c	Halt()
 *	0x10	PowerDown()
 *	0x14	Restart()
 *	0x18	Reboot()
 *	0x1c	EnterInteractiveMode()
 *	0x20	ReturnFromMain()
 *	0x24	GetPeer(node)
 *	0x28	GetChild(node)
 *	0x2c	GetParent(node)
 *	0x30	GetConfigurationData(config_data, node)
 *	0x3c	GetComponent(name)
 *	0x44	GetSystemId()
 *	0x48	GetMemoryDescriptor(void *)
 *	0x50	GetTime()
 *	0x54	GetRelativeTime()
 *	0x5c	Open(path, mode, &fileid)
 *	0x60	Close(handle)
 *	0x64	Read(handle, &buf, len, &actuallen)
 *	0x6c	Write(handle, buf, len, &returnlen)
 *	0x70	Seek(handle, &offset, len)
 *	0x78	GetEnvironmentVariable(char *)
 *	0x7c	SetEnvironmentVariable(char *, char *)
 *	0x80	GetFileInformation(handle, buf)
 *	0x88	FlushAllCaches()
 *	0x90	GetDisplayStatus(uint32_t handle)
 *	0x100	undocumented IRIX (?)
 */
int arcbios_emul(struct cpu *cpu)
{
	struct machine *machine = cpu->machine;
	int vector = cpu->pc & 0xfff;
	int i, j, handle;
	unsigned char ch2;
	unsigned char buf[40];

	if (cpu->pc >= ARC_PRIVATE_ENTRIES &&
	    cpu->pc < ARC_PRIVATE_ENTRIES + 100*sizeof(uint32_t)) {
		if (machine->machine_type == MACHINE_SGI)
			arcbios_sgi_emul(cpu);
		else {
			fatal("[ ARCBIOS private call for non-SGI. ]\n");
			exit(1);
		}

		return 1;
	}

	if (machine->md.arc->arc_64bit)
		vector /= 2;

	/*  Special case for reboot by jumping to 0xbfc00000:  */
	if (vector == 0 && (cpu->pc & 0xffffffffULL) == 0xbfc00000ULL)
		vector = 0x18;

	switch (vector) {
	case 0x0c:		/*  Halt()  */
	case 0x10:		/*  PowerDown()  */
	case 0x14:		/*  Restart()  */
	case 0x18:		/*  Reboot()  */
	case 0x1c:		/*  EnterInteractiveMode()  */
	case 0x20:		/*  ReturnFromMain()  */
		switch (vector) {
		case 0x0c:
			debugmsg(SUBSYS_PROMEMUL, "arcbios", VERBOSITY_DEBUG, "Halt()");
			break;
		case 0x10:
			debugmsg(SUBSYS_PROMEMUL, "arcbios", VERBOSITY_DEBUG, "PowerDown()");
			break;
		case 0x14:
			debugmsg(SUBSYS_PROMEMUL, "arcbios", VERBOSITY_DEBUG, "Restart()");
			break;
		case 0x18:
			debugmsg(SUBSYS_PROMEMUL, "arcbios", VERBOSITY_DEBUG, "Reboot()");
			break;
		case 0x1c:
			debugmsg(SUBSYS_PROMEMUL, "arcbios", VERBOSITY_DEBUG, "EnterInteractiveMode()");
			break;
		case 0x20:
			debugmsg(SUBSYS_PROMEMUL, "arcbios", VERBOSITY_DEBUG, "ReturnFromMain()");
			break;
		}

		/*  Halt all CPUs.  */
		for (i=0; i<machine->ncpus; i++) {
			machine->cpus[i]->running = 0;
		}

		break;
	case 0x24:		/*  GetPeer(node)  */
		if (cpu->cd.mips.gpr[MIPS_GPR_A0] == 0) {
			/*  NULL ptr argument: return NULL.  */
			cpu->cd.mips.gpr[MIPS_GPR_V0] = 0;
		} else {
			uint64_t peer;
			cpu->memory_rw(cpu, cpu->mem,
			    cpu->cd.mips.gpr[MIPS_GPR_A0] - 3 *
			    machine->md.arc->wordlen, &buf[0],
			    machine->md.arc->wordlen, MEM_READ, CACHE_NONE);
			if (machine->md.arc->arc_64bit) {
				if (cpu->byte_order == EMUL_BIG_ENDIAN) {
					unsigned char tmp; tmp = buf[0];
					buf[0] = buf[7]; buf[7] = tmp;
					tmp = buf[1]; buf[1] = buf[6];
					buf[6] = tmp;
					tmp = buf[2]; buf[2] = buf[5];
					buf[5] = tmp;
					tmp = buf[3]; buf[3] = buf[4];
					buf[4] = tmp;
				}
				peer = (uint64_t)buf[0] + ((uint64_t)buf[1]<<8)
				    + ((uint64_t)buf[2]<<16)
				    + ((uint64_t)buf[3]<<24)
				    + ((uint64_t)buf[4]<<32)
				    + ((uint64_t)buf[5]<<40)
				    + ((uint64_t)buf[6]<<48)
				    + ((uint64_t)buf[7]<<56);
			} else {
				if (cpu->byte_order == EMUL_BIG_ENDIAN) {
					unsigned char tmp; tmp = buf[0];
					buf[0] = buf[3]; buf[3] = tmp;
					tmp = buf[1]; buf[1] = buf[2];
					buf[2] = tmp;
				}
				peer = buf[0] + (buf[1]<<8) + (buf[2]<<16)
				    + (buf[3]<<24);
			}

			cpu->cd.mips.gpr[MIPS_GPR_V0] = peer?
			    (peer + 3 * machine->md.arc->wordlen) : 0;
			if (!machine->md.arc->arc_64bit)
				cpu->cd.mips.gpr[MIPS_GPR_V0] = (int64_t)
				    (int32_t) cpu->cd.mips.gpr[MIPS_GPR_V0];
		}
		debug("[ ARCBIOS GetPeer(node 0x%016" PRIx64"): 0x%016" PRIx64
		    " ]\n", (uint64_t) cpu->cd.mips.gpr[MIPS_GPR_A0],
		    (uint64_t) cpu->cd.mips.gpr[MIPS_GPR_V0]);
		break;
	case 0x28:		/*  GetChild(node)  */
		/*  0 for the root, non-0 for children:  */
		if (cpu->cd.mips.gpr[MIPS_GPR_A0] == 0)
			cpu->cd.mips.gpr[MIPS_GPR_V0] = FIRST_ARC_COMPONENT
			    + machine->md.arc->wordlen * 3;
		else {
			uint64_t child = 0;
			cpu->memory_rw(cpu, cpu->mem,
			    cpu->cd.mips.gpr[MIPS_GPR_A0] - 2 *
			    machine->md.arc->wordlen, &buf[0], machine->
			    md.arc->wordlen, MEM_READ, CACHE_NONE);
			if (machine->md.arc->arc_64bit) {
				if (cpu->byte_order == EMUL_BIG_ENDIAN) {
					unsigned char tmp; tmp = buf[0];
					buf[0] = buf[7]; buf[7] = tmp;
					tmp = buf[1]; buf[1] = buf[6];
					buf[6] = tmp;
					tmp = buf[2]; buf[2] = buf[5];
					buf[5] = tmp;
					tmp = buf[3]; buf[3] = buf[4];
					buf[4] = tmp;
				}
				child = (uint64_t)buf[0] +
				    ((uint64_t)buf[1]<<8) +
				    ((uint64_t)buf[2]<<16) +
				    ((uint64_t)buf[3]<<24) +
				    ((uint64_t)buf[4]<<32) +
				    ((uint64_t)buf[5]<<40) +
				    ((uint64_t)buf[6]<<48) +
				    ((uint64_t)buf[7]<<56);
			} else {
				if (cpu->byte_order == EMUL_BIG_ENDIAN) {
					unsigned char tmp; tmp = buf[0];
					buf[0] = buf[3]; buf[3] = tmp;
					tmp = buf[1]; buf[1] = buf[2];
					buf[2] = tmp;
				}
				child = buf[0] + (buf[1]<<8) + (buf[2]<<16) +
				    (buf[3]<<24);
			}

			cpu->cd.mips.gpr[MIPS_GPR_V0] = child?
			    (child + 3 * machine->md.arc->wordlen) : 0;
			if (!machine->md.arc->arc_64bit)
				cpu->cd.mips.gpr[MIPS_GPR_V0] = (int64_t)
				    (int32_t)cpu->cd.mips.gpr[MIPS_GPR_V0];
		}
		debug("[ ARCBIOS GetChild(node 0x%016" PRIx64"): 0x%016"
		    PRIx64" ]\n", (uint64_t) cpu->cd.mips.gpr[MIPS_GPR_A0],
		    (uint64_t) cpu->cd.mips.gpr[MIPS_GPR_V0]);
		break;
	case 0x2c:		/*  GetParent(node)  */
		{
			uint64_t parent;

			cpu->memory_rw(cpu, cpu->mem,
			    cpu->cd.mips.gpr[MIPS_GPR_A0] - 1 * machine->
			    md.arc->wordlen, &buf[0], machine->md.arc->wordlen,
			    MEM_READ, CACHE_NONE);

			if (machine->md.arc->arc_64bit) {
				if (cpu->byte_order == EMUL_BIG_ENDIAN) {
					unsigned char tmp; tmp = buf[0];
					buf[0] = buf[7]; buf[7] = tmp;
					tmp = buf[1]; buf[1] = buf[6];
					buf[6] = tmp;
					tmp = buf[2]; buf[2] = buf[5];
					buf[5] = tmp;
					tmp = buf[3]; buf[3] = buf[4];
					buf[4] = tmp;
				}
				parent = (uint64_t)buf[0] +
				    ((uint64_t)buf[1]<<8) +
				    ((uint64_t)buf[2]<<16) +
				    ((uint64_t)buf[3]<<24) +
				    ((uint64_t)buf[4]<<32) +
				    ((uint64_t)buf[5]<<40) +
				    ((uint64_t)buf[6]<<48) +
				    ((uint64_t)buf[7]<<56);
			} else {
				if (cpu->byte_order == EMUL_BIG_ENDIAN) {
					unsigned char tmp; tmp = buf[0];
					buf[0] = buf[3]; buf[3] = tmp;
					tmp = buf[1]; buf[1] = buf[2];
					buf[2] = tmp;
				}
				parent = buf[0] + (buf[1]<<8) +
				    (buf[2]<<16) + (buf[3]<<24);
			}

			cpu->cd.mips.gpr[MIPS_GPR_V0] = parent?
			    (parent + 3 * machine->md.arc->wordlen) : 0;
			if (!machine->md.arc->arc_64bit)
				cpu->cd.mips.gpr[MIPS_GPR_V0] = (int64_t)
				    (int32_t) cpu->cd.mips.gpr[MIPS_GPR_V0];
		}
		debug("[ ARCBIOS GetParent(node 0x%016" PRIx64"): 0x%016"
		    PRIx64" ]\n", (uint64_t) cpu->cd.mips.gpr[MIPS_GPR_A0],
		    (uint64_t) cpu->cd.mips.gpr[MIPS_GPR_V0]);
		break;
	case 0x30:  /*  GetConfigurationData(void *configdata, void *node)  */
		/*  fatal("[ ARCBIOS GetConfigurationData(0x%016" PRIx64","
		    "0x%016" PRIx64") ]\n",
		    (uint64_t) cpu->cd.mips.gpr[MIPS_GPR_A0],
		    (uint64_t) cpu->cd.mips.gpr[MIPS_GPR_A1]);  */
		cpu->cd.mips.gpr[MIPS_GPR_V0] = ARCBIOS_EINVAL;
		for (i=0; i<machine->md.arc->n_configuration_data; i++) {
			/*  fatal("configuration_data_component[%i] = "
			    "0x%016" PRIx64"\n", i, (uint64_t) machine->
			    md.arc->configuration_data_component[i]);  */
			if (cpu->cd.mips.gpr[MIPS_GPR_A1] ==
			    machine->md.arc->configuration_data_component[i]) {
				cpu->cd.mips.gpr[MIPS_GPR_V0] = 0;
				for (j=0; j<machine->
				    md.arc->configuration_data_len[i]; j++) {
					unsigned char ch;
					cpu->memory_rw(cpu, cpu->mem,
					    machine->md.arc->
					    configuration_data_configdata[i] +
					    j, &ch, 1, MEM_READ, CACHE_NONE);
					cpu->memory_rw(cpu, cpu->mem,
					    cpu->cd.mips.gpr[MIPS_GPR_A0] + j,
					    &ch, 1, MEM_WRITE, CACHE_NONE);
				}
				break;
			}
		}
		break;
	case 0x3c:		/*  GetComponent(char *name)  */
		debug("[ ARCBIOS GetComponent(\"");
		dump_mem_string(cpu, cpu->cd.mips.gpr[MIPS_GPR_A0]);
		debug("\") ]\n");

		if (cpu->cd.mips.gpr[MIPS_GPR_A0] == 0) {
			fatal("[ ARCBIOS GetComponent: NULL ptr ]\n");
		} else {
			unsigned char buf2[500];
			int match_index = -1;
			int match_len = 0;

			memset(buf2, 0, sizeof(buf2));
			for (i=0; i<(ssize_t)sizeof(buf2); i++) {
				cpu->memory_rw(cpu, cpu->mem,
				    cpu->cd.mips.gpr[MIPS_GPR_A0] + i,
				    &buf2[i], 1, MEM_READ, CACHE_NONE);
				if (buf2[i] == '\0')
					i = sizeof(buf);
			}
			buf2[sizeof(buf2) - 1] = '\0';

			/*  "scsi(0)disk(0)rdisk(0)partition(0)" and such.  */
			/*  printf("GetComponent(\"%s\")\n", buf2);  */

			/*  Default to NULL return value.  */
			cpu->cd.mips.gpr[MIPS_GPR_V0] = 0;

			/*  Scan the string to component table:  */
			for (i=0; i<machine->md.arc->n_string_to_components;
			    i++) {
				int m = 0;
				while (buf2[m] && machine->md.arc->
				    string_to_component[i][m] &&
				    machine->md.arc->string_to_component[i][m]
				    == buf2[m])
					m++;
				if (m > match_len) {
					match_len = m;
					match_index = i;
				}
			}

			if (match_index >= 0) {
				/*  printf("Longest match: '%s'\n",
				    machine->md.arc->string_to_component[
				    match_index]);  */
				cpu->cd.mips.gpr[MIPS_GPR_V0] =
				    machine->md.arc->string_to_component_value[
				    match_index];
			}
		}
		break;
	case 0x44:		/*  GetSystemId()  */
		debugmsg(SUBSYS_PROMEMUL, "arcbios", VERBOSITY_DEBUG, "GetSystemId()");
		cpu->cd.mips.gpr[MIPS_GPR_V0] = SGI_SYSID_ADDR;
		break;
	case 0x48:		/*  void *GetMemoryDescriptor(void *ptr)  */
		debugmsg(SUBSYS_PROMEMUL, "arcbios", VERBOSITY_DEBUG, "GetMemoryDescriptor(0x%08x)",
		    (int)cpu->cd.mips.gpr[MIPS_GPR_A0]);

		/*  If a0=NULL, then return the first descriptor:  */
		if ((uint32_t)cpu->cd.mips.gpr[MIPS_GPR_A0] == 0)
			cpu->cd.mips.gpr[MIPS_GPR_V0] =
			    machine->md.arc->memdescriptor_base;
		else {
			int s = machine->md.arc->arc_64bit?
			    sizeof(struct arcbios_mem64)
			    : sizeof(struct arcbios_mem);
			int nr = cpu->cd.mips.gpr[MIPS_GPR_A0] -
			    machine->md.arc->memdescriptor_base;
			nr /= s;
			nr ++;
			cpu->cd.mips.gpr[MIPS_GPR_V0] =
			    machine->md.arc->memdescriptor_base + s * nr;
			if (nr >= machine->md.arc->n_memdescriptors)
				cpu->cd.mips.gpr[MIPS_GPR_V0] = 0;
		}
		break;
	case 0x50:		/*  GetTime()  */
		debug("[ ARCBIOS GetTime() ]\n");
		cpu->cd.mips.gpr[MIPS_GPR_V0] = 0xffffffff80001000ULL;
		/*  TODO!  */
		break;
	case 0x54:		/*  GetRelativeTime()  */
		debug("[ ARCBIOS GetRelativeTime() ]\n");
		cpu->cd.mips.gpr[MIPS_GPR_V0] = (int64_t)(int32_t)time(NULL);
		break;
	case 0x5c:  /*  Open(char *path, uint32_t mode, uint32_t *fileID)  */
		debug("[ ARCBIOS Open(\"");
		dump_mem_string(cpu, cpu->cd.mips.gpr[MIPS_GPR_A0]);
		debug("\",0x%x,0x%x)", (int)cpu->cd.mips.gpr[MIPS_GPR_A0],
		    (int)cpu->cd.mips.gpr[MIPS_GPR_A1],
		    (int)cpu->cd.mips.gpr[MIPS_GPR_A2]);

		cpu->cd.mips.gpr[MIPS_GPR_V0] = ARCBIOS_ENOENT;

		handle = 3;
		/*  TODO: Starting at 0 would require some updates...  */
		while (machine->md.arc->file_handle_in_use[handle]) {
			handle ++;
			if (handle >= ARC_MAX_HANDLES) {
				cpu->cd.mips.gpr[MIPS_GPR_V0] = ARCBIOS_EMFILE;
				break;
			}
		}

		if (handle >= ARC_MAX_HANDLES) {
			fatal("[ ARCBIOS Open: out of file handles ]\n");
		} else if (cpu->cd.mips.gpr[MIPS_GPR_A0] == 0) {
			fatal("[ ARCBIOS Open: NULL ptr ]\n");
		} else {
			/*
			 *  TODO: This is hardcoded to successfully open
			 *  anything. It is used by the Windows NT SETUPLDR
			 *  program to load stuff from the boot partition.
			 */
			unsigned char *buf2;
			CHECK_ALLOCATION(buf2 = (unsigned char *) malloc(MAX_OPEN_STRINGLEN));
			memset(buf2, 0, MAX_OPEN_STRINGLEN);
			for (i=0; i<MAX_OPEN_STRINGLEN; i++) {
				cpu->memory_rw(cpu, cpu->mem,
				    cpu->cd.mips.gpr[MIPS_GPR_A0] + i,
				    &buf2[i], 1, MEM_READ, CACHE_NONE);
				if (buf2[i] == '\0')
					i = MAX_OPEN_STRINGLEN;
			}
			buf2[MAX_OPEN_STRINGLEN - 1] = '\0';
			machine->md.arc->file_handle_string[handle] =
			    (char *)buf2;
			machine->md.arc->current_seek_offset[handle] = 0;
			cpu->cd.mips.gpr[MIPS_GPR_V0] = ARCBIOS_ESUCCESS;
		}

		if (cpu->cd.mips.gpr[MIPS_GPR_V0] == ARCBIOS_ESUCCESS) {
			debug(" = handle %i ]\n", (int)handle);
			store_32bit_word(cpu, cpu->cd.mips.gpr[MIPS_GPR_A2],
			    handle);
			machine->md.arc->file_handle_in_use[handle] = 1;
		} else
			debug(" = ERROR %i ]\n",
			    (int)cpu->cd.mips.gpr[MIPS_GPR_V0]);
		break;
	case 0x60:		/*  Close(uint32_t handle)  */
		debugmsg(SUBSYS_PROMEMUL, "arcbios", VERBOSITY_DEBUG, "Close(%i)",
		    (int)cpu->cd.mips.gpr[MIPS_GPR_A0]);
		if (!machine->md.arc->file_handle_in_use[cpu->cd.mips.gpr[
		    MIPS_GPR_A0]]) {
			fatal("ARCBIOS Close(%i): bad handle\n",
			    (int)cpu->cd.mips.gpr[MIPS_GPR_A0]);
			cpu->cd.mips.gpr[MIPS_GPR_V0] = ARCBIOS_EBADF;
		} else {
			machine->md.arc->file_handle_in_use[
			    cpu->cd.mips.gpr[MIPS_GPR_A0]] = 0;
			// TODO: Yes, this is a memory leak. But it will be
			// scrapped in favor of real code after the rewrite (I
			// hope).
			//if (machine->md.arc->file_handle_string[
			//    cpu->cd.mips.gpr[MIPS_GPR_A0]] != NULL)
			//	free(machine->md.arc->file_handle_string[
			//	    cpu->cd.mips.gpr[MIPS_GPR_A0]]);
			machine->md.arc->file_handle_string[cpu->cd.mips.
			    gpr[MIPS_GPR_A0]] = NULL;
			cpu->cd.mips.gpr[MIPS_GPR_V0] = ARCBIOS_ESUCCESS;
		}
		break;
	case 0x64:  /*  Read(handle, void *buf, length, uint32_t *count)  */
		if (cpu->cd.mips.gpr[MIPS_GPR_A0] == ARCBIOS_STDIN) {
			int j2, nread = 0, a2;

			/*
			 *  Before going into the loop, make sure stdout
			 *  is flushed.  If we're using an X11 VGA console,
			 *  then it needs to be flushed as well.
			 */
			fflush(stdin);
			fflush(stdout);
			/*  NOTE/TODO: This gives a tick to _everything_  */
			for (j2=0; j2<machine->tick_functions.n_entries; j2++)
				machine->tick_functions.f[j2](cpu,
				    machine->tick_functions.extra[j2]);

			a2 = cpu->cd.mips.gpr[MIPS_GPR_A2];
			for (j2=0; j2<a2; j2++) {
				int x;
				unsigned char ch;

				/*  Read from STDIN is blocking (at least
				    that seems to be how NetBSD's arcdiag
				    wants it)  */
				x = console_readchar(
				    machine->main_console_handle);
				if (x < 0)
					return 0;

				/*
				 *  ESC + '[' should be transformed into 0x9b:
				 *
				 *  NOTE/TODO: This makes the behaviour of just
				 *  pressing ESC a bit harder to define.
				 */
				if (x == 27) {
					x = console_readchar(cpu->
					    machine->main_console_handle);
					if (x == '[' || x == 'O')
						x = 0x9b;
				}

				ch = x;
				nread ++;
				cpu->memory_rw(cpu, cpu->mem,
				    cpu->cd.mips.gpr[MIPS_GPR_A1] + j2,
				    &ch, 1, MEM_WRITE, CACHE_NONE);

				/*  NOTE: Only one char, from STDIN:  */
				j2 = cpu->cd.mips.gpr[MIPS_GPR_A2];  /*  :-)  */
			}

			store_32bit_word(cpu, cpu->cd.mips.gpr[MIPS_GPR_A3],
			    nread);
			/*  TODO: not EAGAIN?  */
			cpu->cd.mips.gpr[MIPS_GPR_V0] =
			    nread? ARCBIOS_ESUCCESS: ARCBIOS_EAGAIN;
		} else {
			int handleTmp = cpu->cd.mips.gpr[MIPS_GPR_A0];
			int disk_type = 0;
			int disk_id = arcbios_handle_to_disk_id_and_type(
			    machine, handleTmp, &disk_type);
			uint64_t partition_offset = 0;
			int res;
			uint64_t size;		/*  dummy  */
			unsigned char *tmp_buf;

			arcbios_handle_to_start_and_size(machine, handleTmp,
			    &partition_offset, &size);

			debugmsg(SUBSYS_PROMEMUL, "arcbios", VERBOSITY_DEBUG, "Read(%i,0x%08x,0x%08x,0x%08x)",
			    (int)cpu->cd.mips.gpr[MIPS_GPR_A0],
			    (int)cpu->cd.mips.gpr[MIPS_GPR_A1],
			    (int)cpu->cd.mips.gpr[MIPS_GPR_A2],
			    (int)cpu->cd.mips.gpr[MIPS_GPR_A3]);

			CHECK_ALLOCATION(tmp_buf = (unsigned char *)
			    malloc(cpu->cd.mips.gpr[MIPS_GPR_A2]));

			res = diskimage_access(machine, disk_id, disk_type,
			    0, partition_offset + machine->md.arc->
			    current_seek_offset[handleTmp], tmp_buf,
			    cpu->cd.mips.gpr[MIPS_GPR_A2]);

			/*  If the transfer was successful, transfer the
			    data to emulated memory:  */
			if (res) {
				uint64_t dst = cpu->cd.mips.gpr[MIPS_GPR_A1];
				store_buf(cpu, dst, (char *)tmp_buf,
				    cpu->cd.mips.gpr[MIPS_GPR_A2]);
				store_32bit_word(cpu,
				    cpu->cd.mips.gpr[MIPS_GPR_A3],
				    cpu->cd.mips.gpr[MIPS_GPR_A2]);
				machine->md.arc->current_seek_offset[handleTmp] +=
				    cpu->cd.mips.gpr[MIPS_GPR_A2];
				cpu->cd.mips.gpr[MIPS_GPR_V0] = 0;
			} else {
				debug("[ ... res = %i ]\n", res);
				cpu->cd.mips.gpr[MIPS_GPR_V0] = ARCBIOS_EIO;
			}
			free(tmp_buf);
		}
		break;
	case 0x68:		/*  GetReadStatus(handle)  */
		/*
		 *  According to arcbios_tty_getchar() in NetBSD's
		 *  dev/arcbios/arcbios_tty.c, GetReadStatus should
		 *  return 0 if there is something available.
		 *
		 *  TODO: Error codes are things like ARCBIOS_EAGAIN.
		 */
		if (cpu->cd.mips.gpr[MIPS_GPR_A0] == ARCBIOS_STDIN) {
			cpu->cd.mips.gpr[MIPS_GPR_V0] = console_charavail(
			    machine->main_console_handle)? 0 : 1;
		} else {
			fatal("[ ARCBIOS GetReadStatus(%i) from "
			    "something other than STDIN: TODO ]\n",
			    (int)cpu->cd.mips.gpr[MIPS_GPR_A0]);
			/*  TODO  */
			cpu->cd.mips.gpr[MIPS_GPR_V0] = 1;
		}
		break;
	case 0x6c:		/*  Write(handle, buf, len, &returnlen)  */
		if (cpu->cd.mips.gpr[MIPS_GPR_A0] != ARCBIOS_STDOUT) {
			/*
			 *  TODO: this is just a test
			 */
			int handleTmp = cpu->cd.mips.gpr[MIPS_GPR_A0];
			int disk_type = 0;
			int disk_id = arcbios_handle_to_disk_id_and_type(
			    machine, handleTmp, &disk_type);
			uint64_t partition_offset = 0;
			int res, tmpi;
			uint64_t size;		/*  dummy  */
			unsigned char *tmp_buf;

			arcbios_handle_to_start_and_size(machine,
			    handleTmp, &partition_offset, &size);

			debug("[ ARCBIOS Write(%i,0x%08" PRIx64",%i,0x%08"
			    PRIx64") ]\n", (int) cpu->cd.mips.gpr[MIPS_GPR_A0],
			    (uint64_t) cpu->cd.mips.gpr[MIPS_GPR_A1],
			    (int) cpu->cd.mips.gpr[MIPS_GPR_A2],
			    (uint64_t) cpu->cd.mips.gpr[MIPS_GPR_A3]);

			CHECK_ALLOCATION(tmp_buf = (unsigned char *)
			    malloc(cpu->cd.mips.gpr[MIPS_GPR_A2]));

			for (tmpi=0; tmpi<(int32_t)cpu->cd.mips.gpr[MIPS_GPR_A2]; tmpi++)
				cpu->memory_rw(cpu, cpu->mem,
				    cpu->cd.mips.gpr[MIPS_GPR_A1] + tmpi,
				    &tmp_buf[tmpi], sizeof(char), MEM_READ,
				    CACHE_NONE);

			res = diskimage_access(machine, disk_id, disk_type,
			    1, partition_offset + machine->md.arc->
			    current_seek_offset[handleTmp], tmp_buf,
			    cpu->cd.mips.gpr[MIPS_GPR_A2]);

			if (res) {
				store_32bit_word(cpu,
				    cpu->cd.mips.gpr[MIPS_GPR_A3],
				    cpu->cd.mips.gpr[MIPS_GPR_A2]);
				machine->md.arc->current_seek_offset[handleTmp] +=
				    cpu->cd.mips.gpr[MIPS_GPR_A2];
				cpu->cd.mips.gpr[MIPS_GPR_V0] = 0;
			} else
				cpu->cd.mips.gpr[MIPS_GPR_V0] = ARCBIOS_EIO;
			free(tmp_buf);
		} else {
			for (i=0; i<(int32_t)cpu->cd.mips.gpr[MIPS_GPR_A2];
			    i++) {
				unsigned char ch = '\0';
				cpu->memory_rw(cpu, cpu->mem,
				    cpu->cd.mips.gpr[MIPS_GPR_A1] + i,
				    &ch, sizeof(ch), MEM_READ, CACHE_NONE);

				arcbios_putchar(cpu, ch);
			}
		}
		store_32bit_word(cpu, cpu->cd.mips.gpr[MIPS_GPR_A3],
		    cpu->cd.mips.gpr[MIPS_GPR_A2]);
		cpu->cd.mips.gpr[MIPS_GPR_V0] = 0;	/*  Success.  */
		break;
	case 0x70:	/*  Seek(uint32_t handle, int64_t *ofs,
				 uint32_t whence): uint32_t  */
		debug("[ ARCBIOS Seek(%i,0x%08" PRIx64",%i): ",
		    (int) cpu->cd.mips.gpr[MIPS_GPR_A0],
		    (uint64_t)cpu->cd.mips.gpr[MIPS_GPR_A1],
		    (int) cpu->cd.mips.gpr[MIPS_GPR_A2]);

		if (cpu->cd.mips.gpr[MIPS_GPR_A2] != 0) {
			fatal("[ ARCBIOS Seek(%i,0x%08" PRIx64",%i): "
			    "UNIMPLEMENTED whence=%i ]\n",
			    (int) cpu->cd.mips.gpr[MIPS_GPR_A0],
			    (uint64_t) cpu->cd.mips.gpr[MIPS_GPR_A1],
			    (int) cpu->cd.mips.gpr[MIPS_GPR_A2],
			    (int) cpu->cd.mips.gpr[MIPS_GPR_A2]);
		}

		{
			unsigned char bufTmp[8];
			uint64_t ofs;
			cpu->memory_rw(cpu, cpu->mem,
			    cpu->cd.mips.gpr[MIPS_GPR_A1], &bufTmp[0],
			    sizeof(bufTmp), MEM_READ, CACHE_NONE);
			if (cpu->byte_order == EMUL_BIG_ENDIAN) {
				unsigned char tmp;
				tmp = bufTmp[0]; bufTmp[0] = bufTmp[7]; bufTmp[7] = tmp;
				tmp = bufTmp[1]; bufTmp[1] = bufTmp[6]; bufTmp[6] = tmp;
				tmp = bufTmp[2]; bufTmp[2] = bufTmp[5]; bufTmp[5] = tmp;
				tmp = bufTmp[3]; bufTmp[3] = bufTmp[4]; bufTmp[4] = tmp;
			}
			ofs = bufTmp[0] + (bufTmp[1] << 8) + (bufTmp[2] << 16) +
			    (bufTmp[3] << 24) + ((uint64_t)bufTmp[4] << 32) +
			    ((uint64_t)bufTmp[5] << 40) + ((uint64_t)bufTmp[6] << 48)
			    + ((uint64_t)bufTmp[7] << 56);

			machine->md.arc->current_seek_offset[
			    cpu->cd.mips.gpr[MIPS_GPR_A0]] = ofs;
			debug("%016" PRIx64" ]\n", (uint64_t) ofs);
		}

		cpu->cd.mips.gpr[MIPS_GPR_V0] = 0;	/*  Success.  */

		break;
	case 0x78:		/*  GetEnvironmentVariable(char *)  */
		/*  Find the environment variable given by a0:  */
		for (i=0; i<(ssize_t)sizeof(buf); i++)
			cpu->memory_rw(cpu, cpu->mem,
			    cpu->cd.mips.gpr[MIPS_GPR_A0] + i,
			    &buf[i], sizeof(char), MEM_READ, CACHE_NONE);
		buf[sizeof(buf)-1] = '\0';
		debugmsg(SUBSYS_PROMEMUL, "arcbios", VERBOSITY_DEBUG, "GetEnvironmentVariable(\"%s\")", buf);
		for (i=0; i<0x1000; i++) {
			/*  Matching string at offset i?  */
			int nmatches = 0;
			uint64_t envptr = machine->machine_type == MACHINE_SGI ? ARC_ENV_STRINGS_SGI : ARC_ENV_STRINGS;
			for (j=0; j<(ssize_t)strlen((char *)buf); j++) {
				cpu->memory_rw(cpu, cpu->mem,
				    (uint64_t)(envptr + i + j),
				    &ch2, sizeof(char), MEM_READ, CACHE_NONE);
				if (ch2 == buf[j])
					nmatches++;
			}
			cpu->memory_rw(cpu, cpu->mem,
			    (uint64_t)(envptr + i +
			    strlen((char *)buf)), &ch2, sizeof(char),
			    MEM_READ, CACHE_NONE);
			if (nmatches == (int)strlen((char *)buf) && ch2=='=') {
				cpu->cd.mips.gpr[MIPS_GPR_V0] =
				    envptr + i +
				    strlen((char *)buf) + 1;
				return 1;
			}
		}
		/*  Return NULL if string wasn't found.  */
		cpu->cd.mips.gpr[MIPS_GPR_V0] = 0;
		break;
	case 0x7c:		/*  SetEnvironmentVariable(char *, char *)  */
		debug("[ ARCBIOS SetEnvironmentVariable(\"");
		dump_mem_string(cpu, cpu->cd.mips.gpr[MIPS_GPR_A0]);
		debug("\",\"");
		dump_mem_string(cpu, cpu->cd.mips.gpr[MIPS_GPR_A1]);
		debug("\") ]\n");
		/*  TODO: This is a dummy.  */
		cpu->cd.mips.gpr[MIPS_GPR_V0] = ARCBIOS_ESUCCESS;
		break;
	case 0x80:		/*  GetFileInformation()  */
		debug("[ ARCBIOS GetFileInformation(%i,0x%x): ",
		    (int)cpu->cd.mips.gpr[MIPS_GPR_A0],
		    (int)cpu->cd.mips.gpr[MIPS_GPR_A1]);

		if (cpu->cd.mips.gpr[MIPS_GPR_A0] >= ARC_MAX_HANDLES) {
			debug("invalid file handle ]\n");
			cpu->cd.mips.gpr[MIPS_GPR_V0] = ARCBIOS_EINVAL;
		} else if (!machine->md.arc->file_handle_in_use[cpu->cd.
		    mips.gpr[MIPS_GPR_A0]]) {
			debug("file handle not in use! ]\n");
			cpu->cd.mips.gpr[MIPS_GPR_V0] = ARCBIOS_EBADF;
		} else {
			debug("'%s' ]\n", machine->md.arc->file_handle_string[
			    cpu->cd.mips.gpr[MIPS_GPR_A0]]);
			cpu->cd.mips.gpr[MIPS_GPR_V0] =
			    arcbios_getfileinformation(cpu);
		}
		break;
	case 0x88:		/*  FlushAllCaches()  */
		debug("[ ARCBIOS FlushAllCaches(): TODO ]\n");
		cpu->cd.mips.gpr[MIPS_GPR_V0] = 0;
		break;
	case 0x90:		/*  void *GetDisplayStatus(handle)  */
		debug("[ ARCBIOS GetDisplayStatus(%i) ]\n",
		    (int)cpu->cd.mips.gpr[MIPS_GPR_A0]);
		/*  TODO:  handle different values of 'handle'?  */
		cpu->cd.mips.gpr[MIPS_GPR_V0] = ARC_DSPSTAT_ADDR;
		break;
	case 0x100:
		/*
		 *  Undocumented, used by IRIX.
		 */
		debug("[ ARCBIOS: IRIX 0x100 (?) ]\n");
		/*  TODO  */
		cpu->cd.mips.gpr[MIPS_GPR_V0] = 0;
		break;
	case 0x888:
		/*
		 *  Magical crash if there is no exception handling code.
		 */
		fatal("EXCEPTION, but no exception handler installed yet.\n");
		quiet_mode = 0;
		cpu_register_dump(machine, cpu, 1, 0x1);
		cpu->running = 0;
		break;
	default:
		quiet_mode = 0;
		cpu_register_dump(machine, cpu, 1, 0x1);
		debug("a0 points to: ");
		dump_mem_string(cpu, cpu->cd.mips.gpr[MIPS_GPR_A0]);
		debug("\n");
		fatal("ARCBIOS: unimplemented vector 0x%x\n", vector);
		cpu->running = 0;
	}

	return 1;
}


/*
 *  arcbios_set_default_exception_handler():
 */
void arcbios_set_default_exception_handler(struct cpu *cpu)
{
	/*
	 *  The default exception handlers simply jump to 0xbfc88888,
	 *  which is then taken care of in arcbios_emul() above.
	 *
	 *  3c1abfc8        lui     k0,0xbfc8
	 *  375a8888        ori     k0,k0,0x8888
	 *  03400008        jr      k0
	 *  00000000        nop
	 */
	store_32bit_word(cpu, 0xffffffff80000000ULL, 0x3c1abfc8);
	store_32bit_word(cpu, 0xffffffff80000004ULL, 0x375a8888);
	store_32bit_word(cpu, 0xffffffff80000008ULL, 0x03400008);
	store_32bit_word(cpu, 0xffffffff8000000cULL, 0x00000000);

	store_32bit_word(cpu, 0xffffffff80000080ULL, 0x3c1abfc8);
	store_32bit_word(cpu, 0xffffffff80000084ULL, 0x375a8888);
	store_32bit_word(cpu, 0xffffffff80000088ULL, 0x03400008);
	store_32bit_word(cpu, 0xffffffff8000008cULL, 0x00000000);

	store_32bit_word(cpu, 0xffffffff80000180ULL, 0x3c1abfc8);
	store_32bit_word(cpu, 0xffffffff80000184ULL, 0x375a8888);
	store_32bit_word(cpu, 0xffffffff80000188ULL, 0x03400008);
	store_32bit_word(cpu, 0xffffffff8000018cULL, 0x00000000);
}


/*
 *  arcbios_add_other_components():
 *
 *  TODO: How should this be synched with the hardware devices
 *  added in machine.c?
 */
static void arcbios_add_other_components(struct machine *machine,
	uint64_t system)
{
	struct cpu *cpu = machine->cpus[0];

	if (machine->machine_type == MACHINE_ARC &&
	    (machine->machine_subtype == MACHINE_ARC_JAZZ_PICA
	    || machine->machine_subtype == MACHINE_ARC_JAZZ_MAGNUM)) {
		uint64_t jazzbus, ali_s3, vxl;
		uint64_t diskcontroller, kbdctl, ptrctl, scsi;
		/*  uint64_t serial1, serial2;  */

		jazzbus = arcbios_addchild_manual(cpu,
		    COMPONENT_CLASS_AdapterClass,
		    COMPONENT_TYPE_MultiFunctionAdapter,
		    0, 1, 2, 0, 0xffffffff, "Jazz-Internal Bus",
		    system, NULL, 0);

		/*
		 *  DisplayController, needed by NetBSD:
		 *  TODO: NetBSD still doesn't use it :(
		 */
		switch (machine->machine_subtype) {
		case MACHINE_ARC_JAZZ_PICA:
			/*  Default TLB entries on PICA-61:  */

			/* 7: 256K, asid: 0x0, v: 0xe1000000,
			   p0: 0xfff00000(2.VG), p1: 0x0(0..G)  */
			mips_coproc_tlb_set_entry(cpu, 7, 262144,
			    0xffffffffe1000000ULL,
			    0x0fff00000ULL, 0, 1, 0, 0, 0, 1, 0, 2, 0);

			/* 8: 64K, asid: 0x0, v: 0xe0000000,
			   p0: 0x80000000(2DVG), p1: 0x0(0..G) */
			mips_coproc_tlb_set_entry(cpu, 8, 65536,
			    0xffffffffe0000000ULL,
			    0x080000000ULL, 0, 1, 0, 1, 0, 1, 0, 2, 0);

			/* 9: 64K, asid: 0x0, v: 0xe00e0000,
			   p0: 0x800e0000(2DVG), p1: 0x800f0000(2DVG) */
			mips_coproc_tlb_set_entry(cpu, 9, 65536,
			    (uint64_t)0xffffffffe00e0000ULL,
			    (uint64_t)0x0800e0000ULL,
			    (uint64_t)0x0800f0000ULL, 1, 1, 1, 1, 1, 0, 2, 2);

			/* 10: 4K, asid: 0x0, v: 0xe0100000,
			   p0: 0xf0000000(2DVG), p1: 0x0(0..G) */
			mips_coproc_tlb_set_entry(cpu, 10, 4096,
			    (uint64_t)0xffffffffe0100000ULL,
			    (uint64_t)0x0f0000000ULL, 0,1, 0, 1, 0, 1, 0, 2, 0);

			/* 11: 1M, asid: 0x0, v: 0xe0200000,
			   p0: 0x60000000(2DVG), p1: 0x60100000(2DVG) */
			mips_coproc_tlb_set_entry(cpu, 11, 1048576,
			    0xffffffffe0200000ULL,
			    0x060000000ULL, 0x060100000ULL,1,1,1,1,1, 0, 2, 2);

			/* 12: 1M, asid: 0x0, v: 0xe0400000,
			   p0: 0x60200000(2DVG), p1: 0x60300000(2DVG) */
			mips_coproc_tlb_set_entry(cpu, 12, 1048576,
			    0xffffffffe0400000ULL, 0x060200000ULL,
			    0x060300000ULL, 1, 1, 1, 1, 1, 0, 2, 2);

			/* 13: 4M, asid: 0x0, v: 0xe0800000,
			   p0: 0x40000000(2DVG), p1: 0x40400000(2DVG) */
			mips_coproc_tlb_set_entry(cpu, 13, 1048576*4,
			    0xffffffffe0800000ULL, 0x040000000ULL,
			    0x040400000ULL, 1, 1, 1, 1, 1, 0, 2, 2);

			/* 14: 16M, asid: 0x0, v: 0xe2000000,
			   p0: 0x90000000(2DVG), p1: 0x91000000(2DVG) */
			mips_coproc_tlb_set_entry(cpu, 14, 1048576*16,
			    0xffffffffe2000000ULL, 0x090000000ULL,
			    0x091000000ULL, 1, 1, 1, 1, 1, 0, 2, 2);

			if (mda_attached(machine)) {
				ali_s3 = arcbios_addchild_manual(cpu,
				    COMPONENT_CLASS_ControllerClass,
				    COMPONENT_TYPE_DisplayController,
				    COMPONENT_FLAG_ConsoleOut |
					COMPONENT_FLAG_Output,
				    1, 2, 0, 0xffffffff, "ALI_S3",
				    jazzbus, NULL, 0);

				arcbios_addchild_manual(cpu,
				    COMPONENT_CLASS_PeripheralClass,
				    COMPONENT_TYPE_MonitorPeripheral,
				    COMPONENT_FLAG_ConsoleOut |
					COMPONENT_FLAG_Output,
				    1, 2, 0, 0xffffffff, "1024x768",
				    ali_s3, NULL, 0);
			}
			break;
		case MACHINE_ARC_JAZZ_MAGNUM:
			if (mda_attached(machine)) {
				vxl = arcbios_addchild_manual(cpu,
				    COMPONENT_CLASS_ControllerClass,
				    COMPONENT_TYPE_DisplayController,
				    COMPONENT_FLAG_ConsoleOut |
					COMPONENT_FLAG_Output,
				    1, 2, 0, 0xffffffff, "VXL",
				    jazzbus, NULL, 0);

				arcbios_addchild_manual(cpu,
				    COMPONENT_CLASS_PeripheralClass,
				    COMPONENT_TYPE_MonitorPeripheral,
				    COMPONENT_FLAG_ConsoleOut |
					COMPONENT_FLAG_Output,
				    1, 2, 0, 0xffffffff, "1024x768",
				    vxl, NULL, 0);
			}
			break;
		}

		diskcontroller = arcbios_addchild_manual(cpu,
		    COMPONENT_CLASS_ControllerClass,
		    COMPONENT_TYPE_DiskController,
			COMPONENT_FLAG_Input | COMPONENT_FLAG_Output,
		    1, 2, 0, 0xffffffff, "I82077", jazzbus, NULL, 0);

		/* uint64_t floppy = */ arcbios_addchild_manual(cpu,
		    COMPONENT_CLASS_PeripheralClass,
		    COMPONENT_TYPE_FloppyDiskPeripheral,
			COMPONENT_FLAG_Removable |
			COMPONENT_FLAG_Input | COMPONENT_FLAG_Output,
		    1, 2, 0, 0xffffffff, NULL, diskcontroller, NULL, 0);

		kbdctl = arcbios_addchild_manual(cpu,
		    COMPONENT_CLASS_ControllerClass,
		    COMPONENT_TYPE_KeyboardController,
			COMPONENT_FLAG_ConsoleIn | COMPONENT_FLAG_Input,
		    1, 2, 0, 0xffffffff, "I8742", jazzbus, NULL, 0);

		/* uint64_t kbd = */ arcbios_addchild_manual(cpu,
		    COMPONENT_CLASS_PeripheralClass,
		    COMPONENT_TYPE_KeyboardPeripheral,
			COMPONENT_FLAG_ConsoleIn | COMPONENT_FLAG_Input,
		    1, 2, 0, 0xffffffff, "PCAT_ENHANCED", kbdctl, NULL, 0);

		ptrctl = arcbios_addchild_manual(cpu,
		    COMPONENT_CLASS_ControllerClass,
		    COMPONENT_TYPE_PointerController, COMPONENT_FLAG_Input,
		    1, 2, 0, 0xffffffff, "I8742", jazzbus, NULL, 0);

		/* uint64_t ptr = */ arcbios_addchild_manual(cpu,
		    COMPONENT_CLASS_PeripheralClass,
		    COMPONENT_TYPE_PointerPeripheral, COMPONENT_FLAG_Input,
		    1, 2, 0, 0xffffffff, "PS2 MOUSE", ptrctl, NULL, 0);

/*  These cause Windows NT to bug out.  */
#if 0
		serial1 = arcbios_addchild_manual(cpu,
		    COMPONENT_CLASS_ControllerClass,
		    COMPONENT_TYPE_SerialController,
			COMPONENT_FLAG_Input | COMPONENT_FLAG_Output,
		    1, 2, 0, 0xffffffff, "COM1", jazzbus, NULL, 0);

		serial2 = arcbios_addchild_manual(cpu,
		    COMPONENT_CLASS_ControllerClass,
		    COMPONENT_TYPE_SerialController,
			COMPONENT_FLAG_Input | COMPONENT_FLAG_Output,
		    1, 2, 0, 0xffffffff, "COM1", jazzbus, NULL, 0);
#endif

		/* uint64_t paral = */ arcbios_addchild_manual(cpu,
		    COMPONENT_CLASS_ControllerClass,
		    COMPONENT_TYPE_ParallelController,
			COMPONENT_FLAG_Input | COMPONENT_FLAG_Output,
		    1, 2, 0, 0xffffffff, "LPT1", jazzbus, NULL, 0);

		/* uint64_t audio = */ arcbios_addchild_manual(cpu,
		    COMPONENT_CLASS_ControllerClass,
		    COMPONENT_TYPE_AudioController,
			COMPONENT_FLAG_Input | COMPONENT_FLAG_Output,
		    1, 2, 0, 0xffffffff, "MAGNUM", jazzbus, NULL, 0);

		/* uint64_t eisa = */ arcbios_addchild_manual(cpu,
		    COMPONENT_CLASS_AdapterClass, COMPONENT_TYPE_EISAAdapter,
		    0, 1, 2, 0, 0xffffffff, "EISA", system, NULL, 0);

		{
			unsigned char config[78];
			memset(config, 0, sizeof(config));

/*  config data version: 1, revision: 2, count: 4  */
config[0] = 0x01; config[1] = 0x00;
config[2] = 0x02; config[3] = 0x00;
config[4] = 0x04; config[5] = 0x00; config[6] = 0x00; config[7] = 0x00;

/*
          type: Interrupt
           share_disposition: DeviceExclusive, flags: LevelSensitive
           level: 4, vector: 22, reserved1: 0
*/
			config[8] = arc_CmResourceTypeInterrupt;
			config[9] = arc_CmResourceShareDeviceExclusive;
			config[10] = arc_CmResourceInterruptLevelSensitive;
			config[12] = 4;
			config[16] = 22;
			config[20] = 0;

/*
          type: Memory
           share_disposition: DeviceExclusive, flags: ReadWrite
           start: 0x 0 80002000, length: 0x1000
*/
			config[24] = arc_CmResourceTypeMemory;
			config[25] = arc_CmResourceShareDeviceExclusive;
			config[26] = arc_CmResourceMemoryReadWrite;
config[28] = 0x00; config[29] = 0x20; config[30] = 0x00; config[31] = 0x80;
  config[32] = 0x00; config[33] = 0x00; config[34] = 0x00; config[35] = 0x00;
config[36] = 0x00; config[37] = 0x10; config[38] = 0x00; config[39] = 0x00;

/*
          type: DMA
           share_disposition: DeviceExclusive, flags: 0x0
           channel: 0, port: 0, reserved1: 0
*/
			config[40] = arc_CmResourceTypeDMA;
			config[41] = arc_CmResourceShareDeviceExclusive;
/*  42..43 = flags, 44,45,46,47 = channel, 48,49,50,51 = port, 52,53,54,55
 = reserved  */

/*          type: DeviceSpecific
           share_disposition: DeviceExclusive, flags: 0x0
           datasize: 6, reserved1: 0, reserved2: 0
           data: [0x1:0x0:0x2:0x0:0x7:0x30]
*/
			config[56] = arc_CmResourceTypeDeviceSpecific;
			config[57] = arc_CmResourceShareDeviceExclusive;
/*  58,59 = flags  60,61,62,63 = data size, 64..71 = reserved  */
			config[60] = 6;
/*  72..77 = the data  */
			config[72] = 0x01; config[73] = 0x00; config[74] = 0x02;
			config[75] = 0x00; config[76] = 0x07; config[77] = 0x30;
			scsi = arcbios_addchild_manual(cpu,
			    COMPONENT_CLASS_AdapterClass,
			    COMPONENT_TYPE_SCSIAdapter,
			    0, 1, 2, 0, 0xffffffff, "ESP216",
			    system, config, sizeof(config));

			arcbios_register_scsicontroller(machine, scsi);
		}
	}
}


/*
 *  arcbios_console_init():
 *
 *  Called from machine.c whenever an ARC-based machine is running with
 *  a graphical VGA-style framebuffer, which can be used as console.
 */
void arcbios_console_init(struct machine *machine,
	uint64_t vram, uint64_t ctrlregs)
{
	if (machine->md.arc == NULL) {
		CHECK_ALLOCATION(machine->md.arc = (struct machine_arcbios *)
		    malloc(sizeof(struct machine_arcbios)));
		memset(machine->md.arc, 0, sizeof(struct machine_arcbios));
	}

	machine->md.arc->vgaconsole = 1;

	machine->md.arc->console_vram = vram;
	machine->md.arc->console_ctrlregs = ctrlregs;
	machine->md.arc->console_maxx = ARC_CONSOLE_MAX_X;
	machine->md.arc->console_maxy = ARC_CONSOLE_MAX_Y;
	machine->md.arc->in_escape_sequence = 0;
	machine->md.arc->escape_sequence[0] = '\0';
}


struct envstrings
{
	int	n;

	char	**name;
	char	**value;
};

void set_env(struct envstrings* env, const char* name, const char* value)
{
	int found = -1;

	// Linear search. Slow but it works.
	for (int i = 0; i < env->n; ++i) {
		if (strcasecmp(env->name[i], name) == 0) {
			found = i;
			break;
		}
	}

	if (found >= 0) {
		free(env->value[found]);
		env->value[found] = strdup(value);
	} else {
		if (env->n == 0) {
			env->name = malloc(0);
			env->value = malloc(0);
		}

		env->n ++;

		env->name = realloc(env->name, sizeof(const char*) * env->n);
		env->value = realloc(env->value, sizeof(const char*) * env->n);

		env->name[env->n - 1] = strdup(name);
		env->value[env->n - 1] = strdup(value);
	}
}


static char* environment_string(struct machine *machine, struct envstrings* env, const char* variable_name, bool name_and_value)
{
	size_t len = strlen(variable_name);
	int found = -1;

	// Linear search. Slow but it works.
	for (int i = 0; i < env->n; ++i) {
		if (strcasecmp(env->name[i], variable_name) == 0) {
			found = i;
			break;
		}
	}

	size_t len_with_value = len + 1;
	if (found >= 0)
		len_with_value += strlen(env->value[found]);

	char* s = malloc(len_with_value + 1);

	if (name_and_value) {
		if (found < 0)
			snprintf(s, len_with_value + 1, "%s=", variable_name);
		else
			snprintf(s, len_with_value + 1, "%s=%s", variable_name, env->value[found]);
	} else {
		// Value only.
		if (found < 0)
			s[0] = '\0';
		else
			snprintf(s, len_with_value + 1, "%s", env->value[found]);
	}

	if (machine->machine_type == MACHINE_ARC) {
		for (size_t i = 0; i < len; ++i)
			s[i] = toupper(s[i]);
	}

	return s;
}


/*
 *  arc_environment_setup():
 *
 *  Initialize the emulated environment variables.
 */
static void arc_environment_setup(struct machine *machine, int is64bit,
	const char *primary_ether_addr)
{
	struct envstrings* env = malloc(sizeof(struct envstrings));
	memset(env, 0, sizeof(struct envstrings));

	uint64_t addr, addr2;
	struct cpu *cpu = machine->cpus[0];

	/*
	 *  Boot string in ARC format:
	 *
	 *  TODO: How about floppies? multi()disk()fdisk()
	 *        Is tftp() good for netbooting?
	 */
	char *boot_device;

	if (machine->bootdev_id < 0 || machine->force_netboot) {
		boot_device = "tftp()";
	} else {
		char* prefix = "";

		if (machine->machine_type == MACHINE_SGI) {
			switch (machine->machine_subtype) {
			case 30:
				prefix = "xio(0)pci(15)";
				break;
			case 32:
				prefix = "pci(0)";
				break;
			default:
				fprintf(stderr, "TODO: prefix for SGI IP%i\n", machine->machine_subtype);
				exit(1);
			}
		}

		boot_device = prefix;

		if (diskimage_is_a_cdrom(machine, machine->bootdev_id, machine->bootdev_type)) {
			size_t len = strlen(boot_device) + 50;
			char* cdrom = malloc(len);
			snprintf(cdrom, len, "%sscsi(0)cdrom(%i)", boot_device, machine->bootdev_id);
			boot_device = cdrom;
		} else {
			size_t len = strlen(boot_device) + 50;
			char* disk = malloc(len);
			snprintf(disk, len, "%sscsi(0)disk(%i)rdisk(0)", boot_device, machine->bootdev_id);
			boot_device = disk;
		}

		char *systempartition;
		if (machine->machine_type == MACHINE_SGI || !diskimage_is_a_cdrom(machine, machine->bootdev_id, machine->bootdev_type)) {
			int partition = 1;	// default ARC partition?
			if (machine->machine_type == MACHINE_SGI)
				partition = 8;

			size_t len = strlen(boot_device) + 50;
			systempartition = malloc(len);
			snprintf(systempartition, len, "%spartition(%i)", boot_device, partition);
		} else {
			size_t len = strlen(boot_device) + 50;
			systempartition = malloc(len);
			snprintf(systempartition, len, "%sfdisk(0)", boot_device);
		}

		set_env(env, "SystemPartition", systempartition);

		char *osloadpartition;
		if (machine->machine_type == MACHINE_SGI || !diskimage_is_a_cdrom(machine, machine->bootdev_id, machine->bootdev_type)) {
			int partition = 1;	// default ARC partition?
			if (machine->machine_type == MACHINE_SGI)
				partition = 0;

			size_t len = strlen(boot_device) + 50;
			osloadpartition = malloc(len);
			snprintf(osloadpartition, len, "%spartition(%i)", boot_device, partition);
		} else {
			size_t len = strlen(boot_device) + 50;
			osloadpartition = malloc(len);
			snprintf(osloadpartition, len, "%sfdisk(0)", boot_device);
		}

		set_env(env, "OSLoadPartition", osloadpartition);
	}

	if (machine->machine_type == MACHINE_ARC) {
		set_env(env, "OSLoader", "\\os\\nt\\osloader.exe");
		set_env(env, "OSLoadFilename", "\\todo");
	} else {
		set_env(env, "OSLoader", "sash");
		set_env(env, "OSLoadFilename", "/unix");
	}

	if (machine->boot_kernel_filename[0] != '\0') {
		size_t len = strlen(machine->boot_kernel_filename) + 10;
		char* s = malloc(len);
		snprintf(s, len, "/%s", machine->boot_kernel_filename);
		set_env(env, "OSLoadFilename", s);
	}

	if (machine->machine_type == MACHINE_SGI) {
		/*  g for graphical mode. G for graphical mode with SGI logo visible on Irix?  */
		if (mda_attached(machine)) {
			set_env(env, "ConsoleIn", "keyboard()");
			set_env(env, "ConsoleOut", "video()");
			set_env(env, "console", "g");
			set_env(env, "gfx", "alive");
		} else {
			/*  'd' or 'd2' in Irix, 'ttyS0' in Linux?  */
			set_env(env, "ConsoleIn", "serial(0)");
			set_env(env, "ConsoleOut", "serial(0)");
			set_env(env, "console", "d");
			set_env(env, "gfx", "dead");
		}

		set_env(env, "AutoLoad", "No");

		if (machine->bootdev_id < 0 || machine->force_netboot) {
			/*
			 *  diskless=1 means boot from network disk? (nfs?)
			 *  diskless=2 means boot from network tape? TODO
			 */
			set_env(env, "diskless", "1");

			// store_pointer_and_advance(cpu, &addr2, addr, is64bit);
			// add_environment_string(cpu, "tapedevice=bootp()10.0.0.2:/dev/tape", &addr);

			set_env(env, "root", "xyz");	// TODO

			set_env(env, "bootfile", "bootp()10.0.0.2:/var/boot/client/unix");
			set_env(env, "SystemPartition", "bootp()10.0.0.2:/var/boot/client");
			set_env(env, "OSLoadPartition", "bootp()10.0.0.2:/var/boot/client");
		} else {
			set_env(env, "diskless", "0");
		}

		/*  TODO: Keep these variables in sync with bootblock loading,
			i.e. OSLoader is the name of the voldir entry to boot
			with.  */

		set_env(env, "kernname", "unix");	// TODO: like OSLoadFilename?

		set_env(env, "volume", "80");
		set_env(env, "sgilogo", "y");
		set_env(env, "monitor", "h");
		set_env(env, "TimeZone", "GMT");
		set_env(env, "nogfxkbd", "1");
		set_env(env, "keybd", "US");
		set_env(env, "cpufreq", "123");
		set_env(env, "dbaud", "9600");
		set_env(env, "rbaud", "9600");
		set_env(env, "rebound", "y");
		set_env(env, "crt_option", "1");
		set_env(env, "netaddr", "10.0.0.1");
		set_env(env, "netmask", "255.0.0.0");
		set_env(env, "dlserver", "10.0.0.2");
		set_env(env, "srvaddr", "10.0.0.2");
		set_env(env, "eaddr", primary_ether_addr);

		// showconfig 0 means don't show. 1 means show some.
		// 2 means show more. TODO: higher values?
		set_env(env, "showconfig", "255");

		set_env(env, "verbose", "1");
		set_env(env, "diagmode", "v");
		set_env(env, "debug_bigmem", "1");
	} else {
		//  General ARC:
		if (mda_attached(machine)) {
			set_env(env, "ConsoleIn", "multi()key()keyboard()console()");
			set_env(env, "ConsoleOut", "multi()video()monitor()console()");
		} else {
			/*  TODO: serial console for ARC?  */
			set_env(env, "ConsoleIn", "multi()serial(0)");
			set_env(env, "ConsoleOut", "multi()serial(0)");
		}
	}

	/*  Boot string is either:
		SystemPartition + OSLoader	  (e.g. sash)
	    or
	    	OSLoadPartition + OSLoadFilename  (e.g. /unix)  */

	char* s1 = environment_string(machine, env, "SystemPartition", true);
	char* s2 = environment_string(machine, env, "OSLoader", true);
	size_t s3len = strlen(s1) + strlen(s2) + 100;
	char* boot_string = malloc(s3len);
	snprintf(boot_string, s3len, "%s%s%s", s1, machine->machine_type == MACHINE_SGI ? "/" : "\\", s2);
	free(s2);
	free(s1);

	// boot_string = env["OSLoadPartition"] + env["OSLoadFilename"];

	/*  Boot args., eg "-a"  */
	machine->bootarg = machine->boot_string_argument;

	if (machine->machine_type == MACHINE_SGI) {
		set_env(env, "OSLoadOptions", "auto");
	} else {
		set_env(env, "OSLoadOptions", machine->bootarg);
	}

	/*  a0 = argc:  */
	cpu->cd.mips.gpr[MIPS_GPR_A0] = 0; /*  note: argc is increased later  */

	/*  sp = just below top of RAM. (TODO: not needed?)  */
	cpu->cd.mips.gpr[MIPS_GPR_SP] = (int64_t)(int32_t)
	    (machine->physical_ram_in_mb * 1048576 + 0x80000000 - 0x2080);

	/*  a1 = argv:  */
	addr = ARC_ENV_STRINGS;
	addr2 = ARC_ARGV_START;
	cpu->cd.mips.gpr[MIPS_GPR_A1] = addr2;

	CHECK_ALLOCATION(machine->bootstr = strdup(boot_string));

	/*  bootstr:  */
	store_pointer_and_advance(cpu, &addr2, addr, is64bit);
	add_environment_string(cpu, machine->bootstr, &addr);
	cpu->cd.mips.gpr[MIPS_GPR_A0] ++;

	/*  bootarg:  */
	if (machine->bootarg[0] != '\0') {
		store_pointer_and_advance(cpu, &addr2, addr, is64bit);
		add_environment_string(cpu, machine->bootarg, &addr);
		cpu->cd.mips.gpr[MIPS_GPR_A0] ++;
	}

	/*  A few of the environment variables are also included as regular
	    arguments, as per
	    https://misc.openbsd.narkive.com/6hkw57ju/openbsd-sgi-snapshot-fails-to-boot-from-harddisk#post2  */
	store_pointer_and_advance(cpu, &addr2, addr, is64bit);
	char* ev = environment_string(machine, env, "OSLoadOptions", true);
	add_environment_string(cpu, ev, &addr);
	free(ev);
	cpu->cd.mips.gpr[MIPS_GPR_A0] ++;

	store_pointer_and_advance(cpu, &addr2, addr, is64bit);
	ev = environment_string(machine, env, "ConsoleIn", true);
	add_environment_string(cpu, ev, &addr);
	free(ev);
	cpu->cd.mips.gpr[MIPS_GPR_A0] ++;

	store_pointer_and_advance(cpu, &addr2, addr, is64bit);
	ev = environment_string(machine, env, "ConsoleOut", true);
	add_environment_string(cpu, ev, &addr);
	free(ev);
	cpu->cd.mips.gpr[MIPS_GPR_A0] ++;

	store_pointer_and_advance(cpu, &addr2, addr, is64bit);
	ev = environment_string(machine, env, "SystemPartition", true);
	add_environment_string(cpu, ev, &addr);
	free(ev);
	cpu->cd.mips.gpr[MIPS_GPR_A0] ++;

	store_pointer_and_advance(cpu, &addr2, addr, is64bit);
	ev = environment_string(machine, env, "OSLoader", true);
	add_environment_string(cpu, ev, &addr);
	free(ev);
	cpu->cd.mips.gpr[MIPS_GPR_A0] ++;

	store_pointer_and_advance(cpu, &addr2, addr, is64bit);
	ev = environment_string(machine, env, "OSLoadPartition", true);
	add_environment_string(cpu, ev, &addr);
	free(ev);
	cpu->cd.mips.gpr[MIPS_GPR_A0] ++;

	store_pointer_and_advance(cpu, &addr2, addr, is64bit);
	ev = environment_string(machine, env, "OSLoadFilename", true);
	add_environment_string(cpu, ev, &addr);
	free(ev);
	cpu->cd.mips.gpr[MIPS_GPR_A0] ++;

	/*  a2 = envp:  */
	cpu->cd.mips.gpr[MIPS_GPR_A2] = addr2;

	if (machine->machine_type == MACHINE_SGI) {
		/*
		 *  The SGI O2 PROM contains an "env" section header like this:
		 *
		 *  00004000  00 00 00 00 00 00 00 00  53 48 44 52 00 00 04 00  |........SHDR....|
		 *  00004010  03 03 00 00 65 6e 76 00  00 00 00 00 00 00 00 00  |....env.........|
		 *  00004020  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
		 *  00004030  00 00 00 00 31 2e 30 00  00 00 00 00 13 18 11 ae  |....1.0.........|
		 *
		 *  followed by environment variables at 0x4040. It is not required
		 *  by NetBSD/OpenBSD/Linux, but Irix seems to hardcodedly look into
		 *  the PROM address space for this header.
		 */
		store_32bit_word(cpu, ARC_ENV_SGI + 0x08, 0x53484452);
		store_32bit_word(cpu, ARC_ENV_SGI + 0x0c, 0x00000400);
		store_32bit_word(cpu, ARC_ENV_SGI + 0x10, 0x03030000);
		store_32bit_word(cpu, ARC_ENV_SGI + 0x14, 0x656e7600);
		store_32bit_word(cpu, ARC_ENV_SGI + 0x34, 0x312e3000);
		store_32bit_word(cpu, ARC_ENV_SGI + 0x3c, 0x131811ae);
		addr = ARC_ENV_STRINGS_SGI;
	}

	/*
	 *  Add environment variables.  For each variable, add it as a string
	 *  using add_environment_string(), and add a pointer to it to the
	 *  pointer array.
	 */
	for (int i = 0; i < env->n; ++i) {
		store_pointer_and_advance(cpu, &addr2, addr, is64bit);
		char* s = environment_string(machine, env, env->name[i], true);
		add_environment_string(cpu, s, &addr);
		free(s);
	}

	/*  End the environment strings with an empty zero-terminated
	    string, and the envp array with a NULL pointer.  */
	add_environment_string(cpu, "", &addr);	/*  the end  */
	store_pointer_and_advance(cpu, &addr2, 0, is64bit);

	/*  Return address:  (0x20 = ReturnFromMain())  */
	cpu->cd.mips.gpr[MIPS_GPR_RA] = ARC_FIRMWARE_ENTRIES + 0x20;
}


/*
 *  arcbios_init():
 *
 *  Should be called before any other arcbios function is used. An exception
 *  is arcbios_console_init(), which may be called before this function.
 *
 *  TODO: Refactor; this is too long.
 */
void arcbios_init(struct machine *machine, int is64bit, uint64_t sgi_ram_offset,
	const char *primary_ether_addr, uint8_t *primary_ether_macaddr)
{
	int i, alloclen = 20;
	char *name;
	uint64_t arc_reserved, mem_base;
	struct cpu *cpu = machine->cpus[0];
	struct arcbios_dsp_stat arcbios_dsp_stat;
	uint64_t system = 0;

	if (machine->md.arc == NULL) {
		CHECK_ALLOCATION(machine->md.arc = (struct machine_arcbios *)
		    malloc(sizeof(struct machine_arcbios)));
		memset(machine->md.arc, 0, sizeof(struct machine_arcbios));
	}

	machine->md.arc->arc_64bit = is64bit;
	machine->md.arc->wordlen = is64bit? sizeof(uint64_t) : sizeof(uint32_t);

	machine->md.arc->next_component_address = FIRST_ARC_COMPONENT;
	machine->md.arc->configuration_data_next_addr = ARC_CONFIG_DATA_ADDR;

	if (machine->physical_ram_in_mb < 16)
		fprintf(stderr, "WARNING! The ARC platform specification "
		    "doesn't allow less than 16 MB of RAM. Continuing "
		    "anyway.\n");

	/*  File handles 0, 1, and 2 are stdin, stdout, and stderr.  */
	for (i=0; i<ARC_MAX_HANDLES; i++) {
		machine->md.arc->file_handle_in_use[i] = i<3? 1 : 0;
		machine->md.arc->file_handle_string[i] = i>=3? NULL :
		    (i==0? "(stdin)" : (i==1? "(stdout)" : "(stderr)"));
		machine->md.arc->current_seek_offset[i] = 0;
	}

	if (!mda_attached(machine))
		machine->md.arc->vgaconsole = 0;

	if (machine->md.arc->vgaconsole) {
		char tmpstr[100];
		int x, y;

		machine->md.arc->console_curcolor = 0x1f;
		for (y=0; y<machine->md.arc->console_maxy; y++)
			for (x=0; x<machine->md.arc->console_maxx; x++)
				arcbios_putcell(cpu, ' ', x, y);

		machine->md.arc->console_curx = 0;
		machine->md.arc->console_cury = 0;

		arcbios_putstring(cpu, "GXemul " VERSION"  ARCBIOS emulation\n");

		snprintf(tmpstr, sizeof(tmpstr), "%i cpu%s (%s), %i MB "
		    "memory\n\n", machine->ncpus, machine->ncpus > 1? "s" : "",
		    cpu->cd.mips.cpu_type.name,
		    machine->physical_ram_in_mb);
		arcbios_putstring(cpu, tmpstr);
	}

	arcbios_set_default_exception_handler(cpu);

	struct arcbios_sysid arcbios_sysid;
	memset(&arcbios_sysid, 0, sizeof(arcbios_sysid));
	if (machine->machine_type == MACHINE_SGI) {
		/*  Vendor ID, max 8 chars:  */
		memcpy(arcbios_sysid.VendorId, "SGI", 3);

		switch (machine->machine_subtype) {
		case 22:
			memcpy(arcbios_sysid.ProductId, "87654321", 8);	/*  some kind of ID?  */
			break;
		case 32:
			memcpy(arcbios_sysid.ProductId, "f", 1);    /*  "6", "8", of "f"? It's "f" on my O2.  */
			break;
		default:
			snprintf(arcbios_sysid.ProductId, 8, "IP%i", machine->machine_subtype);
		}
	} else {
		switch (machine->machine_subtype) {
		case MACHINE_ARC_JAZZ_PICA:
			memcpy(arcbios_sysid.VendorId,  "MIPS MAG", 8);
			memcpy(arcbios_sysid.ProductId, "ijkl", 4);
			break;
		case MACHINE_ARC_JAZZ_MAGNUM:
			memcpy(arcbios_sysid.VendorId,  "MIPS MAG", 8);
			memcpy(arcbios_sysid.ProductId, "ijkl", 4);
			break;
		default:
			fatal("error in machine.c sysid\n");
			exit(1);
		}
	}

	store_buf(cpu, SGI_SYSID_ADDR, (char *)&arcbios_sysid, sizeof(arcbios_sysid));

	arcbios_get_dsp_stat(cpu, &arcbios_dsp_stat);
	store_buf(cpu, ARC_DSPSTAT_ADDR, (char *)&arcbios_dsp_stat, sizeof(arcbios_dsp_stat));

	/*
	 *  The first 12 MBs of RAM are simply reserved... this simplifies
	 *  things a lot.  If there's more than 512MB of RAM, it has to be
	 *  split in two, according to the ARC spec.
	 *
	 *  However, the region of physical address space between 0x10000000
	 *  and 0x1fffffff (256 - 512 MB) is sometimes occupied by memory
	 *  mapped devices, for example in the SGI O2, so that portion can not
	 *  be used.
	 *
	 *  Instead, any high memory needs to be added using a machine-specific
	 *  high address.
	 */
	int reserved_bottom_mem_in_mb = 12;
	int free_type = ARCBIOS_MEM_FreeMemory;

	machine->md.arc->memdescriptor_base = ARC_MEMDESC_ADDR;

	arc_reserved = 0x2000;
	if (machine->machine_type == MACHINE_SGI)
		arc_reserved = 0x4000;

	arcbios_add_memory_descriptor(cpu, 0, arc_reserved,
	    ARCBIOS_MEM_FirmwarePermanent);
	arcbios_add_memory_descriptor(cpu, sgi_ram_offset + arc_reserved,
	    0x60000-arc_reserved, ARCBIOS_MEM_FirmwareTemporary);

	uint64_t ram = 0;

	// Default is to use a physical memory base around zero (0).
	mem_base = sgi_ram_offset / 1048576;

	while (ram < machine->physical_ram_in_mb) {
		uint64_t to_add = machine->physical_ram_in_mb - ram;

		if (to_add > 256)
			to_add = 256;

		if (ram == 0) {
			// Skip first few MB of RAM, for reserved structures.
			ram += reserved_bottom_mem_in_mb;
			mem_base += reserved_bottom_mem_in_mb;
			to_add -= reserved_bottom_mem_in_mb;
		}

		arcbios_add_memory_descriptor(cpu, mem_base * 1048576,
		    to_add * 1048576, free_type);

		ram += to_add;
		mem_base += to_add;

		if (mem_base == 256) {
			if (machine->machine_type == MACHINE_SGI &&
			    machine->machine_subtype == 32) {
				// mem_base = 0x50000000 / 1048576;
				// Actually, the above does not seem to work.
				// Perhaps the ARCS in the O2 simply says
				// 256 MB regardless of how much more there
				// is in the machine?
				break;
			} else {
				fatal("Ignoring RAM above 256 MB! (Not yet "
					"implemented for this machine type.)\n");
				break;
			}
		}
	}

	/*
	 *  Components:   (this is an example of what a system could look like)
	 *
	 *  [System]
	 *	[CPU]  (one for each cpu)
	 *	    [FPU]  (one for each cpu)
	 *	    [CPU Caches]
	 *	[Memory]
	 *	[Ethernet]
	 *	[Serial]
	 *	[SCSI]
	 *	    [Disk]
	 *
	 *  Here's a good list of what hardware is in different IP-models:
	 *  http://www.linux-mips.org/archives/linux-mips/2001-03/msg00101.html
	 */

	if (machine->machine_name == NULL)
		fatal("ERROR: machine_name == NULL\n");

	/*  Add the root node:  */
	switch (machine->machine_type) {

	case MACHINE_SGI:
		debug("ARCS:\n");
		debug_indentation(1);

		CHECK_ALLOCATION(name = (char *) malloc(alloclen));
		snprintf(name, alloclen, "SGI-IP%i", machine->machine_subtype);

		/*  A very special case for IP24 (which identifies itself
		    as an IP22):  */
		if (machine->machine_subtype == 24)
			snprintf(name, alloclen, "SGI-IP22");
		break;

	case MACHINE_ARC:
		debug("ARC:\n");
		debug_indentation(1);

		switch (machine->machine_subtype) {
		case MACHINE_ARC_JAZZ_PICA:
			name = strdup("PICA-61");
			break;
		case MACHINE_ARC_JAZZ_MAGNUM:
			name = strdup("Microsoft-Jazz");
			break;
		default:
			fatal("Unimplemented ARC machine type %i\n",
			    machine->machine_subtype);
			exit(1);
		}
		break;

	default:
		fatal("ERROR: non-SGI and non-ARC?\n");
		exit(1);
	}

	system = arcbios_addchild_manual(cpu, COMPONENT_CLASS_SystemClass,
	    COMPONENT_TYPE_ARC, 0,1,2,0, 0xffffffff, name, 0/*ROOT*/, NULL, 0);
	debug("system @ 0x%" PRIx64" (\"%s\")\n", (uint64_t) system, name);


	/*
	 *  Add tree nodes for CPUs and their caches:
	 */

	for (i=0; i<machine->ncpus; i++) {
		uint64_t cpuaddr, fpu=0, picache, pdcache, sdcache=0;
		int cache_size, cache_line_size;
		unsigned int jj;
		char arc_cpu_name[100];
		char arc_fpc_name[105];

		snprintf(arc_cpu_name, sizeof(arc_cpu_name),
		    "MIPS-%s", machine->cpu_name);

		arc_cpu_name[sizeof(arc_cpu_name)-1] = 0;
		for (jj=0; jj<strlen(arc_cpu_name); jj++)
			if (arc_cpu_name[jj] >= 'a' && arc_cpu_name[jj] <= 'z')
				arc_cpu_name[jj] += ('A' - 'a');

		strlcpy(arc_fpc_name, arc_cpu_name, sizeof(arc_fpc_name));
		strlcat(arc_fpc_name, "FPC", sizeof(arc_fpc_name));

		cpuaddr = arcbios_addchild_manual(cpu,
		    COMPONENT_CLASS_ProcessorClass, COMPONENT_TYPE_CPU,
		    0, 1, 2, i, 0xffffffff, arc_cpu_name, system, NULL, 0);

		/*
		 *  TODO: This was in the ARC specs, but it isn't really used
		 *  by ARC implementations?   At least SGI-IP32 uses it.
		 */
		if (machine->machine_type == MACHINE_SGI)
			fpu = arcbios_addchild_manual(cpu,
			    COMPONENT_CLASS_ProcessorClass, COMPONENT_TYPE_FPU,
			    0, 1, 2, 0, 0xffffffff, arc_fpc_name, cpuaddr,
			    NULL, 0);

		cache_size = DEFAULT_PCACHE_SIZE - 12;
		if (cpu->cd.mips.cache_picache)
			cache_size = cpu->cd.mips.cache_picache - 12;
		if (cache_size < 0)
			cache_size = 0;

		cache_line_size = DEFAULT_PCACHE_LINESIZE;
		if (cpu->cd.mips.cache_picache_linesize)
			cache_line_size = cpu->cd.mips.cache_picache_linesize;
		if (cache_line_size < 0)
			cache_line_size = 0;

		picache = arcbios_addchild_manual(cpu,
		    COMPONENT_CLASS_CacheClass, COMPONENT_TYPE_PrimaryICache,
		    0, 1, 2,
		    /*
		     *  Key bits:  0xXXYYZZZZ
		     *  XX is refill-size.
		     *  Cache line size is 1 << YY,
		     *  Cache size is 4KB << ZZZZ.
		     */
		    0x01000000 + (cache_line_size << 16) + cache_size,
			/*  32 bytes per line, default = 32 KB total  */
		    0xffffffff, NULL, cpuaddr, NULL, 0);

		cache_size = DEFAULT_PCACHE_SIZE - 12;
		if (cpu->cd.mips.cache_pdcache)
			cache_size = cpu->cd.mips.cache_pdcache - 12;
		if (cache_size < 0)
			cache_size = 0;

		cache_line_size = DEFAULT_PCACHE_LINESIZE;
		if (cpu->cd.mips.cache_pdcache_linesize)
			cache_line_size = cpu->cd.mips.cache_pdcache_linesize;
		if (cache_line_size < 0)
			cache_line_size = 0;

		pdcache = arcbios_addchild_manual(cpu,
		    COMPONENT_CLASS_CacheClass,
		    COMPONENT_TYPE_PrimaryDCache, 0, 1, 2,
		    /*
		     *  Key bits:  0xYYZZZZ
		     *  Cache line size is 1 << YY,
		     *  Cache size is 4KB << ZZZZ.
		     */
		    0x01000000 + (cache_line_size << 16) + cache_size,
			/*  32 bytes per line, default = 32 KB total  */
		    0xffffffff, NULL, cpuaddr, NULL, 0);

		if (cpu->cd.mips.cache_secondary >= 12) {
			cache_size = cpu->cd.mips.cache_secondary - 12;

			cache_line_size = 6;	/*  64 bytes default  */
			if (cpu->cd.mips.cache_secondary_linesize)
				cache_line_size = cpu->cd.mips.
				    cache_secondary_linesize;
			if (cache_line_size < 0)
				cache_line_size = 0;

			sdcache = arcbios_addchild_manual(cpu,
			    COMPONENT_CLASS_CacheClass,
			    COMPONENT_TYPE_SecondaryDCache, 0, 1, 2,
			    /*
			     *  Key bits:  0xYYZZZZ
			     *  Cache line size is 1 << YY,
			     *  Cache size is 4KB << ZZZZ.
			     */
			    0x01000000 + (cache_line_size << 16) + cache_size,
				/*  64 bytes per line, default = 1 MB total  */
			    0xffffffff, NULL, cpuaddr, NULL, 0);
		}

		debug("cpu%i @ 0x%" PRIx64, i, (uint64_t) cpuaddr);

		if (fpu != 0)
			debug(" (fpu @ 0x%" PRIx64")\n", (uint64_t) fpu);
		else
			debug("\n");

		debug("    picache @ 0x%" PRIx64", pdcache @ 0x%" PRIx64"\n",
		    (uint64_t) picache, (uint64_t) pdcache);

		if (cpu->cd.mips.cache_secondary >= 12)
			debug("    sdcache @ 0x%" PRIx64"\n",
			    (uint64_t) sdcache);

		if (machine->machine_type == MACHINE_SGI) {
			/*  TODO:  Memory amount (and base address?)!  */
			uint64_t memory = arcbios_addchild_manual(cpu,
			    COMPONENT_CLASS_MemoryClass,
			    COMPONENT_TYPE_MemoryUnit, 0, 1, 2, 0,
			    0xffffffff, "memory", cpuaddr, NULL, 0);
			debug("memory @ 0x%" PRIx64"\n", (uint64_t) memory);
		}
	}


	/*
	 *  Add other components:
	 *
	 *  TODO: How should this be synched with the hardware devices
	 *  added in machine.c?
	 */

	arcbios_add_other_components(machine, system);


	/*
	 *  Defalt TLB entry for 64-bit SGI machines:
	 */
	if (machine->machine_type == MACHINE_SGI) {
		switch (machine->machine_subtype) {
		case 12:
			/*  TODO: Not on 12?  */
			break;
		case 32:
			/*  Not needed for SGI O2?  */
			break;
		default:
			mips_coproc_tlb_set_entry(cpu, 0, 1048576*16,
			    0xc000000000000000ULL, 0, 1048576*16, 1,1,1,1,1, 0, 2, 2);
		}
	}


	/*
	 *  Set up Firmware Vectors:
	 */
	add_symbol_name(&machine->symbol_context, ARC_FIRMWARE_ENTRIES, 0x10000, "[ARCBIOS entry]", 0, 1);

	for (i=0; i<100; i++) {
		if (is64bit) {
			store_64bit_word(cpu, ARC_FIRMWARE_VECTORS + i*8, ARC_FIRMWARE_ENTRIES + i*8);
			store_64bit_word(cpu, ARC_PRIVATE_VECTORS + i*8, ARC_PRIVATE_ENTRIES + i*8);

			/*  "Magic trap" instruction:  */
			store_32bit_word(cpu, ARC_FIRMWARE_ENTRIES + i*8, 0x00c0de0c);
			store_32bit_word(cpu, ARC_PRIVATE_ENTRIES + i*8, 0x00c0de0c);
		} else {
			store_32bit_word(cpu, ARC_FIRMWARE_VECTORS + i*4, ARC_FIRMWARE_ENTRIES + i*4);
			store_32bit_word(cpu, ARC_PRIVATE_VECTORS + i*4, ARC_PRIVATE_ENTRIES + i*4);

			/*  "Magic trap" instruction:  */
			store_32bit_word(cpu, ARC_FIRMWARE_ENTRIES + i*4, 0x00c0de0c);
			store_32bit_word(cpu, ARC_PRIVATE_ENTRIES + i*4, 0x00c0de0c);
		}
	}


	/*
	 *  Set up the ARC SPD:
	 */
	if (is64bit) {
		/*  ARCS64 SPD (TODO: This is just a guess)  */
		struct arcbios_spb_64 arcbios_spb_64;
		memset(&arcbios_spb_64, 0, sizeof(arcbios_spb_64));
		store_64bit_word_in_host(cpu, (unsigned char *) &arcbios_spb_64.SPBSignature, ARCBIOS_SPB_SIGNATURE);
		store_16bit_word_in_host(cpu, (unsigned char *) &arcbios_spb_64.Version, 64);
		store_16bit_word_in_host(cpu, (unsigned char *) &arcbios_spb_64.Revision, 0);
		store_64bit_word_in_host(cpu, (unsigned char *) &arcbios_spb_64.FirmwareVector, ARC_FIRMWARE_VECTORS);
		store_buf(cpu, SGI_SPB_ADDR, (char *)&arcbios_spb_64, sizeof(arcbios_spb_64));
	} else {
		/*  ARCBIOS SPB:  (For ARC and 32-bit SGI modes)  */
		struct arcbios_spb arcbios_spb;
		memset(&arcbios_spb, 0, sizeof(arcbios_spb));
		store_32bit_word_in_host(cpu, (unsigned char *) &arcbios_spb.SPBSignature, ARCBIOS_SPB_SIGNATURE);
		store_32bit_word_in_host(cpu, (unsigned char *) &arcbios_spb.SPBLength, sizeof(arcbios_spb));
		store_16bit_word_in_host(cpu, (unsigned char *) &arcbios_spb.Version, 1);
		store_16bit_word_in_host(cpu, (unsigned char *) &arcbios_spb.Revision, machine->machine_type == MACHINE_SGI? 10 : 2);
		store_32bit_word_in_host(cpu, (unsigned char *) &arcbios_spb.FirmwareVector, ARC_FIRMWARE_VECTORS);
		store_32bit_word_in_host(cpu, (unsigned char *) &arcbios_spb.FirmwareVectorLength, 35 * sizeof(uint32_t));
		store_32bit_word_in_host(cpu, (unsigned char *) &arcbios_spb.PrivateVector, ARC_PRIVATE_VECTORS);
		store_32bit_word_in_host(cpu, (unsigned char *) &arcbios_spb.PrivateVectorLength, 13 * sizeof(uint32_t));

		if (machine->machine_type == MACHINE_SGI) {
			// Mimic the "BTSR" Restart block struct on my real O2:
			// "sash" clears the lowest bit of the word at offset 0x1c, which
			// is the "Boot Status" word according to the ARC spec.
			// The lowest bit means "boot started".
			const uint64_t btsr_addr = SGI_SPB_ADDR + 0x80;
			store_32bit_word_in_host(cpu, (unsigned char *) &arcbios_spb.RestartBlock, btsr_addr);

			store_32bit_word(cpu, btsr_addr + 0x00, 0x42545352);
			store_32bit_word(cpu, btsr_addr + 0x1c, 0x40);	// 0x40 means "Processor Ready"
		}

		store_buf(cpu, SGI_SPB_ADDR, (char *)&arcbios_spb, sizeof(arcbios_spb));
	}


	/*
	 *  TODO: How to build the component tree intermixed with
	 *  the rest of device initialization?
	 */

	arc_environment_setup(machine, is64bit, primary_ether_addr);

	debug_indentation(-1);
}
