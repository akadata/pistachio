/*********************************************************************
 *                
 * Copyright (C) 2002-2003, 2007,  Karlsruhe University
 *                
 * File path:     kdb/platform/pc99/tracebuffer.cc
 * Description:   Tracebuffer for PC99 platform
 *                
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *                
 ********************************************************************/
#include <debug.h>
#include <linear_ptab.h>
#include <generic/lib.h>
#include <kdb/kdb.h>
#include <kdb/input.h>
#include <kdb/tracepoints.h>
#include <kdb/tracebuffer.h>
#include INC_API(thread.h)
#include INC_API(tcb.h)

#if defined(CONFIG_TRACEBUFFER)

#if defined(CONFIG_TBUF_PERFMON)
#define IF_PERFMON(a...) a
#else
#define IF_PERFMON(a...)
#endif


DECLARE_CMD_GROUP (tracebuf);

word_t tbufcnt = 0;


/*
 * Submenu for tracebuffer related commands.
 */

DECLARE_CMD (cmd_tracebuffer, root, 'y', "tracebuffer",
	     "Dump/manipulate tracebuffer");

CMD (cmd_tracebuffer, cg)
{
    return tracebuf.interact (cg, "tracebuffer");
}


/*
 * Reset tracebuffer.
 */

DECLARE_CMD (cmd_tb_reset, tracebuf, 'r', "reset", "Reset buffer");

CMD (cmd_tb_reset, cg)
{	
    memset (get_tracebuffer ()->tracerecords, 0,
	    TRACEBUFFER_SIZE - sizeof (tracerecord_t));
    get_tracebuffer ()->current = 0;
    return CMD_NOQUIT;
}


/*
 * Reset counters.
 */

DECLARE_CMD (cmd_tb_reset_ctr, tracebuf, 'R', "resetctr", "Reset counters");

CMD (cmd_tb_reset_ctr, cg)
{
    tracebuffer_t * tracebuffer = get_tracebuffer ();

    for (word_t i = 0; i < 8; i++)
	tracebuffer->counters[i] = 0;

    return CMD_NOQUIT;
}


/*
 * Dump counters.
 */

DECLARE_CMD (cmd_tb_dump_ctr, tracebuf, 'c', "counters", "Dump counters");

CMD (cmd_tb_dump_ctr, cg)
{
    tracebuffer_t * tracebuffer = get_tracebuffer ();

    for (word_t i = 0; i < 8; i++)
	printf ("Counter %d = %10d\n", i, tracebuffer->counters[i]);

    return CMD_NOQUIT;
}


/*
 * Apply filter for tracebuffer events.
 */

DECLARE_CMD (cmd_tb_filter, tracebuf, 'f', "filter", "Filter events");

CMD (cmd_tb_filter, cg)
{	
    tracebuffer_t * tracebuffer = get_tracebuffer ();
    tbufcnt++;

    switch (get_choice ("Keep which events",
			"All/Kernel/No tracept/User/Mask", 'a'))
    {
    case 'a':
	tracebuffer->mask = tracebuffer->eventsel = 0;
	break;
    case 'k':
	tracebuffer->mask = 0xffff;
	tracebuffer->eventsel = 0;
	break;
    case 'n':
	tracebuffer->mask = 0xc0000000;
	tracebuffer->eventsel = 0;
	break;
    case 'u':
	tracebuffer->mask = 0xffff0000;
	tracebuffer->eventsel = 0;
	break;
    case 'm':
	tracebuffer->mask = get_hex ("Mask", 0xffffffff);
	tracebuffer->eventsel = get_hex ("Selector", 0);
	break;
    }

    return CMD_NOQUIT;
}


/*
 * Dump current tracebuffer.
 */

DECLARE_CMD (cmd_tb_dump, tracebuf, 'd', "dump", "Dump tracebuffer");

CMD (cmd_tb_dump, cg)
{ 
    word_t mask, eventsel;
    word_t start, size, count, chunk, num, index;
    tracerecord_t * rec;
    struct {
	word_t tsc;
	word_t pmc0;
	word_t pmc1;
    } old = { 0, 0, 0 }, sum = { 0, 0, 0 };

    tracebuffer_t * tracebuffer = get_tracebuffer ();

    if (! tracebuffer->is_valid ())
    {
        printf("Bad tracebuffer signature at %p [%p]\n",
	       (word_t) (&tracebuffer->magic), tracebuffer->magic);
        return CMD_NOQUIT;
    }  

    if (tracebuffer->current == 0)
    {
	printf ("No records\n");
	return CMD_NOQUIT;
    }

    count = (TRACEBUFFER_SIZE / sizeof (tracerecord_t)) - 1;
    start = tracebuffer->current / sizeof (tracerecord_t);
    if (tracebuffer->tracerecords[count - 1].tsc == 0)
    {
	count = start;
	start = 0;
    }
    chunk = (count < 32) ? count : 32;
    size = count;

    switch (get_choice ("Dump tracebuffer", "All/Region/Top/Bottom", 'b'))
    {
    case 'a': 
	break;
    case 'r': 
	start = get_dec ("From record", 0);
	if (start >= count)
	    start = size - 1;
	// Fallthrough
    case 't': 
	count = get_dec ("Record count", chunk);
	if (count > size)
	    count = size;
	break;
    case 'b':
    default: 
	count = get_dec ("Record count", chunk);
	if (count > size)
	    count = size;
	start = (count <= start) ? start - count : start + size - count;
	break;
    } 

    switch (get_choice ("Filter", "All/Kernel/No tracept/User/Mask", 'a'))
    {
    default:
    case 'a':
	mask = eventsel = 0;
	break;
    case 'k':
	mask = 0xffff; eventsel = 0;
	break;
    case 'n':
	mask = 0xc0000000; eventsel = 0;
	break;
    case 'u':
	mask = 0xffff0000; eventsel = 0;
	break;
    case 'm':
	mask = get_hex ("Mask", 0xffffffff);
	eventsel = get_hex ("Selector", 0);
	break;
    }

    printf ("\nRecord Type   %ws Cyclecount "
	    IF_PERFMON (" Perfctr0  Perfctr1 ")
	    " Event\n", "Thread");
    for (num = 0, index = start; count--; index++)
    {
	if (index >= size) index = 0;
        rec = tracebuffer->tracerecords + index;

	if (((rec->utype | (rec->ktype << 16)) & mask) ^ eventsel)
	    continue;
	num++;

        if (! old.tsc)
	{
            old.tsc = rec->tsc;
	    IF_PERFMON (old.pmc0 = rec->pmc0);
	    IF_PERFMON (old.pmc1 = rec->pmc1);
	}

	threadid_t tid;
	if (rec->is_kernel_event ())
	    tid = addr_to_tcb ((addr_t) rec->thread)->get_global_id ();
	else
	    tid = threadid (rec->thread);

        printf ( "%6d %04x %c %wt %10d" IF_PERFMON("%10d%10d") "  ",
		 index, rec->get_type (),
		 rec->is_kernel_event () ? 'k' : 'u', tid.get_raw (),
		 rec->tsc - old.tsc
		 IF_PERFMON (, rec->pmc0-old.pmc0, rec->pmc1-old.pmc1));

        sum.tsc  += (rec->tsc - old.tsc);
        IF_PERFMON (sum.pmc0 += (rec->pmc0 - old.pmc0));
        IF_PERFMON (sum.pmc1 += (rec->pmc1 - old.pmc1));

        old.tsc = rec->tsc;
	IF_PERFMON (old.pmc0 = rec->pmc0);
	IF_PERFMON (old.pmc1 = rec->pmc1);

	if (rec->is_kernel_event ())
	{
	    // Trust kernel strings to be ok

	    printf ((char *) rec->str, rec->data[0],
		    rec->data[1], rec->data[2], rec->data[3]);
	}
	else
	{
	    // For user strings we attempt to look up the string in
	    // the space of the thread.  We don't really bother too
	    // much if this does not work.

	    space_t * space = get_current_space ();
	    tcb_t * tcb = space->get_tcb (tid);

	    // Check if we seem to have a valid space and string pointer

	    if (tcb->get_global_id () != tid ||
		space->is_user_area ((addr_t) tcb->get_space ()) ||
		! space->is_user_area ((addr_t) rec->str))
	    {
		printf ("%p (%p, %p, %p, %p)\n", rec->str,
			rec->data[0], rec->data[1],
			rec->data[2], rec->data[3]);
		continue;
	    }
	    space = tcb->get_space ();

	    static char user_str[64];
	    addr_t p = (addr_t) rec->str;
	    word_t idx = 0;
	    char c;

	    // Safely copy string into kernel

	    while (readmem (space, p, &c) && (c != 0) &&
		   idx < (sizeof (user_str) - 1))
	    {
		user_str[idx++] = c;
		p = addr_offset (p, 1);
	    }
	    user_str[idx] = 0;

	    // Turn '%s' into '%p' (i.e., avoid printing arbitrary
	    // user strings).

	    idx = 0;
	    while (user_str[idx] != 0)
		if (user_str[idx++] == '%')
		{
		    while ((user_str[idx] >= '0' && user_str[idx] <= '9') ||
			   user_str[idx] == 'w' || user_str[idx] == 'l' ||
			   user_str[idx] == '.')
			idx++;
		    if (user_str[idx] == 's')
			user_str[idx] = 'p';
		}

	    printf (user_str, rec->data[0], rec->data[1],
		    rec->data[2], rec->data[3]);
	}

        printf("\n");  
    }
    
    printf ("---------------------------------"
	    IF_PERFMON ("--------------------") "\n");  
    printf ("Mask %04x  Select %04x %10d" IF_PERFMON ("%10d%10d")
	    "  %d entries\n\n", 
	    tracebuffer->mask & 0xffff,
	    tracebuffer->eventsel % 0xffff, sum.tsc, 
	    IF_PERFMON (sum.pmc0, sum.pmc1,) 
	    num);
    
    return CMD_NOQUIT;
}

#endif /* CONFIG_TRACEBUFFER */