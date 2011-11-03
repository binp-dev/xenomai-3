/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
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

#ifndef _POSIX_MUTEX_H
#define _POSIX_MUTEX_H

#include <asm/xenomai/atomic.h>
#include <pthread.h>

struct cobalt_mutex;

union __xeno_mutex {
	pthread_mutex_t native_mutex;
	struct __shadow_mutex {
		unsigned magic;
		unsigned lockcnt;
		struct cobalt_mutex *mutex;
		xnarch_atomic_t lock;
#ifdef CONFIG_XENO_FASTSYNCH
		union {
			unsigned owner_offset;
			xnarch_atomic_t *owner;
		};
		struct cobalt_mutexattr attr;
#endif /* CONFIG_XENO_FASTSYNCH */
	} shadow_mutex;
};

#if defined(__KERNEL__) || defined(__XENO_SIM__)

#include "internal.h"
#include "thread.h"
#include "cb_lock.h"

typedef struct cobalt_mutex {
	unsigned magic;
	xnsynch_t synchbase;
	xnholder_t link;            /* Link in cobalt_mutexq */

#define link2mutex(laddr)                                               \
	((cobalt_mutex_t *)(((char *)laddr) - offsetof(cobalt_mutex_t, link)))

	pthread_mutexattr_t attr;
	cobalt_kqueues_t *owningq;
} cobalt_mutex_t;

extern pthread_mutexattr_t cobalt_default_mutex_attr;

void cobalt_mutexq_cleanup(cobalt_kqueues_t *q);

void cobalt_mutex_pkg_init(void);

void cobalt_mutex_pkg_cleanup(void);

/* Internal mutex functions, exposed for use by syscall.c. */
int cobalt_mutex_timedlock_break(struct __shadow_mutex *shadow,
				int timed, xnticks_t to);

int cobalt_mutex_check_init(struct __shadow_mutex *shadow,
			   const pthread_mutexattr_t *attr);

int cobalt_mutex_init_internal(struct __shadow_mutex *shadow,
			      cobalt_mutex_t *mutex,
			      xnarch_atomic_t *ownerp,
			      const pthread_mutexattr_t *attr);

void cobalt_mutex_destroy_internal(cobalt_mutex_t *mutex,
				  cobalt_kqueues_t *q);

/* must be called with nklock locked, interrupts off. */
static inline int cobalt_mutex_timedlock_internal(xnthread_t *cur,
						 struct __shadow_mutex *shadow,
						 unsigned count,
						 int timed,
						 xnticks_t abs_to)

{
	cobalt_mutex_t *mutex = shadow->mutex;

	if (xnpod_unblockable_p())
		return -EPERM;

	if (!cobalt_obj_active(shadow, COBALT_MUTEX_MAGIC, struct __shadow_mutex)
	    || !cobalt_obj_active(mutex, COBALT_MUTEX_MAGIC, struct cobalt_mutex))
		return -EINVAL;

#if XENO_DEBUG(POSIX)
	if (mutex->owningq != cobalt_kqueues(mutex->attr.pshared))
		return -EPERM;
#endif /* XENO_DEBUG(POSIX) */

	if (xnsynch_owner_check(&mutex->synchbase, cur) == 0)
		return -EBUSY;

	if (timed)
		xnsynch_acquire(&mutex->synchbase, abs_to, XN_REALTIME);
	else
		xnsynch_acquire(&mutex->synchbase, XN_INFINITE, XN_RELATIVE);

	if (unlikely(xnthread_test_info(cur, XNBREAK | XNRMID | XNTIMEO))) {
		if (xnthread_test_info(cur, XNBREAK))
			return -EINTR;
		else if (xnthread_test_info(cur, XNTIMEO))
			return -ETIMEDOUT;
		else /* XNRMID */
			return -EINVAL;
	}

	shadow->lockcnt = count;

	return 0;
}

#endif /* __KERNEL__ */

#endif /* !_POSIX_MUTEX_H */