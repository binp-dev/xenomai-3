/*
 * Copyright (C) 2012 Philippe Gerum <rpm@xenomai.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _COBALT_POSIX_EVENT_H
#define _COBALT_POSIX_EVENT_H

#include <cobalt/kernel/synch.h>
#include <cobalt/uapi/event.h>
#include <xenomai/posix/syscall.h>

struct cobalt_kqueues;

struct cobalt_event {
	unsigned int magic;
	unsigned long value;
	struct xnsynch synch;
	struct cobalt_event_data *data;
	struct cobalt_kqueues *owningq;
	struct list_head link;
	int flags;
	xnhandle_t handle;
};

COBALT_SYSCALL_DECL(event_init,
		    int, (struct cobalt_event_shadow __user *u_evtsh,
			  unsigned long value,
			  int flags));

COBALT_SYSCALL_DECL(event_wait,
		    int, (struct cobalt_event_shadow __user *u_evtsh,
			  unsigned long bits,
			  unsigned long __user *u_bits_r,
			  int mode,
			  struct timespec __user *u_ts));

COBALT_SYSCALL_DECL(event_sync,
		    int, (struct cobalt_event_shadow __user *u_evtsh));

COBALT_SYSCALL_DECL(event_destroy,
		    int, (struct cobalt_event_shadow __user *u_evtsh));

COBALT_SYSCALL_DECL(event_inquire,
		    int, (struct cobalt_event_shadow __user *u_event,
			  struct cobalt_event_info __user *u_info,
			  pid_t __user *u_waitlist,
			  size_t waitsz));

void cobalt_eventq_cleanup(struct cobalt_kqueues *q);

void cobalt_event_pkg_init(void);

void cobalt_event_pkg_cleanup(void);

#endif /* !_COBALT_POSIX_EVENT_H */
