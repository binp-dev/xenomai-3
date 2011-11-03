/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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

/**
 * @addtogroup posix_time
 *
 *@{*/

#include <cobalt/semaphore.h>
#include "timer.h"
#include "thread.h"
#include "timer.h"
#include "sem.h"

#define COBALT_TIMER_MAX  128

struct cobalt_timer {

	xntimer_t timerbase;

	unsigned queued;
	unsigned overruns;

	xnholder_t link; /* link in process or global timers queue. */

#define link2tm(laddr, member)							\
    ((struct cobalt_timer *)(((char *)laddr) - offsetof(struct cobalt_timer, member)))

	xnholder_t tlink; /* link in thread timers queue. */

	cobalt_siginfo_t si;

	clockid_t clockid;
	pthread_t owner;
	cobalt_kqueues_t *owningq;
};

static xnqueue_t timer_freeq;

static struct cobalt_timer timer_pool[COBALT_TIMER_MAX];

static void cobalt_base_timer_handler(xntimer_t *xntimer)
{
	struct cobalt_timer *timer;
	struct cobalt_sem *sem;

	timer = container_of(xntimer, struct cobalt_timer, timerbase);
	if (timer->si.info.si_signo) {
		if (!timer->queued) {
			timer->queued = 1;
			cobalt_sigqueue_inner(timer->owner, &timer->si);
		}
		return;
	}

	/* Null signo means to post a semaphore instead. */
	sem = timer->si.info.si_value.sival_ptr;
	if (sem && sem_post_inner(sem, NULL, 0))
		/*
		 * On error, forget sema4 for next shots. In essence,
		 * the timer stops notifying anyone at expiry.
		 */
		timer->si.info.si_value.sival_ptr = NULL;
}

/* Must be called with nklock locked, irq off. */
void cobalt_timer_notified(cobalt_siginfo_t * si)
{
	struct cobalt_timer *timer = link2tm(si, si);
	xnticks_t now;

	timer->queued = 0;
	/* We need this two staged overruns count. The overruns count returned by
	   timer_getoverrun is the count of overruns which occured between the time
	   the signal was queued and the time this signal was accepted by the
	   application.
	   In other words, if the timer elapses again after cobalt_timer_notified get
	   called (i.e. the signal is accepted by the application), the signal shall
	   be queued again, and later overruns should count for that new
	   notification, not the one the application is currently handling. */

	if (!xntimer_interval(&timer->timerbase)) {
		timer->overruns = 0;
		return;
	}

	now = xntbase_get_rawclock(cobalt_tbase);

	timer->overruns = xntimer_get_overruns(&timer->timerbase, now);
}

/**
 * Create a timer object.
 *
 * This service creates a time object using the clock @a clockid.
 *
 * If @a evp is not @a NULL, it describes the notification mechanism used on
 * timer expiration. Only notification via signal delivery is supported (member
 * @a sigev_notify of @a evp set to @a SIGEV_SIGNAL).  The signal will be sent to
 * the thread starting the timer with the timer_settime() service. If @a evp is
 * @a NULL, the SIGALRM signal will be used.
 *
 * Note that signals sent to user-space threads will cause them to switch to
 * secondary mode.
 *
 * If this service succeeds, an identifier for the created timer is returned at
 * the address @a timerid. The timer is unarmed until started with the
 * timer_settime() service.
 *
 * @param clockid clock used as a timing base;
 *
 * @param evp description of the asynchronous notification to occur when the
 * timer expires;
 *
 * @param timerid address where the identifier of the created timer will be
 * stored on success.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, the clock @a clockid is invalid;
 * - EINVAL, the member @a sigev_notify of the @b sigevent structure at the
 *   address @a evp is not SIGEV_SIGNAL;
 * - EINVAL, the  member @a sigev_signo of the @b sigevent structure is an
 *   invalid signal number;
 * - EAGAIN, the maximum number of timers was exceeded, recompile with a larger
 *   value.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/timer_create.html">
 * Specification.</a>
 *
 */
int timer_create(clockid_t clockid,
		 const struct sigevent *__restrict__ evp,
		 timer_t * __restrict__ timerid)
{
	struct __shadow_sem *shadow_sem = NULL;
	int err = EINVAL, semval, signo;
	struct cobalt_timer *timer;
	xnholder_t *holder;
	sem_t *sem;
	spl_t s;

	if (clockid != CLOCK_MONOTONIC &&
	    clockid != CLOCK_MONOTONIC_RAW &&
	    clockid != CLOCK_REALTIME)
		goto error;

	/*
	 * XXX: We implement a tweaked form of SIGEV_THREAD_ID, purely
	 * for internal purpose, which does not match the Linux
	 * behavior at all. Instead of sending a signal to a specific
	 * thread upon expiry, it posts a semaphore whose address is
	 * fetched from sigev_value.sival_ptr. SIGEV_THREAD_ID is not
	 * officially supported by the POSIX skin.
	 */
	signo = SIGALRM;
	if (evp) {
		switch (evp->sigev_notify) {
		case SIGEV_THREAD_ID:
			/* Quick check to detect trivial mistakes early. */
			sem = evp->sigev_value.sival_ptr;
			if (sem == NULL)
				goto error;
			err = sem_getvalue(sem, &semval);
			if (err)
				return -1; /* errno already set */
			shadow_sem = &((union __xeno_sem *)sem)->shadow_sem;
			signo = 0;
			break;

		case SIGEV_SIGNAL:
			signo = evp->sigev_signo;
			if ((unsigned)(signo - 1) <= SIGRTMAX - 1)
				break;
		default:
			goto error;
		}
	}

	xnlock_get_irqsave(&nklock, s);

	holder = getq(&timer_freeq);
	if (holder == NULL) {
		err = EAGAIN;
		goto unlock_and_error;
	}

	timer = link2tm(holder, link);
	timer->si.info.si_code = SI_TIMER;
	timer->si.info.si_signo = signo;

	if (evp) {
		if (shadow_sem)
			timer->si.info.si_value.sival_ptr = shadow_sem->sem;
		else
			timer->si.info.si_value = evp->sigev_value;
	} else
		timer->si.info.si_value.sival_int = (timer - timer_pool);

	xntimer_init(&timer->timerbase, cobalt_tbase,
		     cobalt_base_timer_handler);

	timer->overruns = 0;
	timer->owner = NULL;
	timer->clockid = clockid;
	timer->owningq = cobalt_kqueues(0);
	inith(&timer->link);
	appendq(&cobalt_kqueues(0)->timerq, &timer->link);
	xnlock_put_irqrestore(&nklock, s);

	*timerid = (timer_t) (timer - timer_pool);

	return 0;

      unlock_and_error:
	xnlock_put_irqrestore(&nklock, s);
      error:
	thread_set_errno(err);
	return -1;
}

int cobalt_timer_delete_inner(timer_t timerid, cobalt_kqueues_t *q, int force)
{
	struct cobalt_timer *timer;
	spl_t s;
	int err;

	if ((unsigned)timerid >= COBALT_TIMER_MAX) {
		err = EINVAL;
		goto error;
	}

	xnlock_get_irqsave(&nklock, s);

	timer = &timer_pool[(unsigned long)timerid];

	if (!xntimer_active_p(&timer->timerbase)) {
		err = EINVAL;
		goto unlock_and_error;
	}

	if (!force && timer->owningq != cobalt_kqueues(0)) {
		err = EPERM;
		goto unlock_and_error;
	}

	removeq(&q->timerq, &timer->link);

	if (timer->queued) {
		/* timer signal is queued, unqueue it. */
		cobalt_sigunqueue(timer->owner, &timer->si);
		timer->queued = 0;
	}

	xntimer_destroy(&timer->timerbase);
	if (timer->owner)
		removeq(&timer->owner->timersq, &timer->tlink);
	timer->owner = NULL;	/* Used for debugging. */
	prependq(&timer_freeq, &timer->link);	/* Favour earliest reuse. */

	xnlock_put_irqrestore(&nklock, s);

	return 0;

      unlock_and_error:
	xnlock_put_irqrestore(&nklock, s);
      error:
	thread_set_errno(err);
	return -1;
}

/**
 * Delete a timer object.
 *
 * This service deletes the timer @a timerid.
 *
 * @param timerid identifier of the timer to be removed;
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, @a timerid is invalid;
 * - EPERM, the timer @a timerid does not belong to the current process.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/timer_delete.html">
 * Specification.</a>
 *
 */
int timer_delete(timer_t timerid)
{
	return cobalt_timer_delete_inner(timerid, cobalt_kqueues(0), 0);
}

static void cobalt_timer_gettime_inner(struct cobalt_timer *__restrict__ timer,
				      struct itimerspec *__restrict__ value)
{
	if (xntimer_running_p(&timer->timerbase)) {
		ticks2ts(&value->it_value,
			 xntimer_get_timeout(&timer->timerbase));
		ticks2ts(&value->it_interval,
			 xntimer_interval(&timer->timerbase));
	} else {
		value->it_value.tv_sec = 0;
		value->it_value.tv_nsec = 0;
		value->it_interval.tv_sec = 0;
		value->it_interval.tv_nsec = 0;
	}
}

/**
 * Start or stop a timer.
 *
 * This service sets a timer expiration date and reload value of the timer @a
 * timerid. If @a ovalue is not @a NULL, the current expiration date and reload
 * value are stored at the address @a ovalue as with timer_gettime().
 *
 * If the member @a it_value of the @b itimerspec structure at @a value is zero,
 * the timer is stopped, otherwise the timer is started. If the member @a
 * it_interval is not zero, the timer is periodic. The current thread must be a
 * POSIX skin thread (created with pthread_create()) and will be notified via
 * signal of timer expirations. Note that these notifications will cause
 * user-space threads to switch to secondary mode.
 *
 * When starting the timer, if @a flags is TIMER_ABSTIME, the expiration value
 * is interpreted as an absolute date of the clock passed to the timer_create()
 * service. Otherwise, the expiration value is interpreted as a time interval.
 *
 * Expiration date and reload value are rounded to an integer count of system
 * clock ticks (see note in section @ref posix_time "Clocks and timers services"
 * for details on the duration of the system tick).
 *
 * @param timerid identifier of the timer to be started or stopped;
 *
 * @param flags one of 0 or TIMER_ABSTIME;
 *
 * @param value address where the specified timer expiration date and reload
 * value are read;
 *
 * @param ovalue address where the specified timer previous expiration date and
 * reload value are stored if not @a NULL.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EPERM, the caller context is invalid;
 * - EINVAL, the specified timer identifier, expiration date or reload value is
 *   invalid;
 * - EPERM, the timer @a timerid does not belong to the current process.
 *
 * @par Valid contexts:
 * - Xenomai kernel-space POSIX skin thread,
 * - kernel-space thread cancellation cleanup routine,
 * - Xenomai POSIX skin user-space thread (switches to primary mode),
 * - user-space thread cancellation cleanup routine.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/timer_settime.html">
 * Specification.</a>
 *
 */
int timer_settime(timer_t timerid,
		  int flags,
		  const struct itimerspec *__restrict__ value,
		  struct itimerspec *__restrict__ ovalue)
{
	pthread_t cur = cobalt_current_thread();
	struct cobalt_timer *timer;
	spl_t s;
	int err;

	if (!cur || xnpod_interrupt_p()) {
		err = EPERM;
		goto error;
	}

	if ((unsigned)timerid >= COBALT_TIMER_MAX) {
		err = EINVAL;
		goto error;
	}

	if ((unsigned long)value->it_value.tv_nsec >= ONE_BILLION ||
	    ((unsigned long)value->it_interval.tv_nsec >= ONE_BILLION &&
	     (value->it_value.tv_sec != 0 || value->it_value.tv_nsec != 0))) {
		err = EINVAL;
		goto error;
	}

	xnlock_get_irqsave(&nklock, s);

	timer = &timer_pool[(unsigned long)timerid];

	if (!xntimer_active_p(&timer->timerbase)) {
		err = EINVAL;
		goto unlock_and_error;
	}

#if XENO_DEBUG(POSIX)
	if (timer->owningq != cobalt_kqueues(0)) {
		err = EPERM;
		goto unlock_and_error;
	}
#endif /* XENO_DEBUG(POSIX) */

	if (ovalue)
		cobalt_timer_gettime_inner(timer, ovalue);

	if (timer->queued) {
		/* timer signal is queued, unqueue it. */
		cobalt_sigunqueue(timer->owner, &timer->si);
		timer->queued = 0;
	}

	if (timer->owner)
		removeq(&timer->owner->timersq, &timer->tlink);

	if (value->it_value.tv_nsec == 0 && value->it_value.tv_sec == 0) {
		xntimer_stop(&timer->timerbase);
		timer->owner = NULL;
	} else {
		xnticks_t start = ts2ticks_ceil(&value->it_value) + 1;
		xnticks_t period = ts2ticks_ceil(&value->it_interval);

		xntimer_set_sched(&timer->timerbase, xnpod_current_sched());
		if (xntimer_start(&timer->timerbase, start, period,
				  clock_flag(flags, timer->clockid))) {
			/* If the initial delay has already passed, the call
			   shall suceed, so, let us tweak the start time. */
			xnticks_t now = clock_get_ticks(timer->clockid);
			if (period) {
				do {
					start += period;
				} while ((xnsticks_t) (start - now) <= 0);
			} else
				start = now + xntbase_ns2ticks
					(cobalt_tbase,
					 xnarch_tsc_to_ns(nklatency));
			xntimer_start(&timer->timerbase, start, period,
				      clock_flag(flags, timer->clockid));
		}

		timer->owner = cur;
		inith(&timer->tlink);
		appendq(&timer->owner->timersq, &timer->tlink);
	}

	xnlock_put_irqrestore(&nklock, s);

	return 0;

      unlock_and_error:
	xnlock_put_irqrestore(&nklock, s);
      error:
	thread_set_errno(err);
	return -1;
}

/**
 * Get timer next expiration date and reload value.
 *
 * This service stores, at the address @a value, the expiration date (member @a
 * it_value) and reload value (member @a it_interval) of the timer @a
 * timerid. The values are returned as time intervals, and as multiples of the
 * system clock tick duration (see note in section
 * @ref posix_time "Clocks and timers services" for details on the
 * duration of the system clock tick). If the timer was not started, the
 * returned members @a it_value and @a it_interval of @a value are zero.
 *
 * @param timerid timer identifier;
 *
 * @param value address where the timer expiration date and reload value are
 * stored on success.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, @a timerid is invalid;
 * - EPERM, the timer @a timerid does not belong to the current process.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/timer_gettime.html">
 * Specification.</a>
 *
 */
int timer_gettime(timer_t timerid, struct itimerspec *value)
{
	struct cobalt_timer *timer;
	spl_t s;
	int err;

	if ((unsigned)timerid >= COBALT_TIMER_MAX) {
		err = EINVAL;
		goto error;
	}

	xnlock_get_irqsave(&nklock, s);

	timer = &timer_pool[(unsigned long)timerid];

	if (!xntimer_active_p(&timer->timerbase)) {
		err = EINVAL;
		goto unlock_and_error;
	}

#if XENO_DEBUG(POSIX)
	if (timer->owningq != cobalt_kqueues(0)) {
		err = EPERM;
		goto unlock_and_error;
	}
#endif /* XENO_DEBUG(POSIX) */

	cobalt_timer_gettime_inner(timer, value);

	xnlock_put_irqrestore(&nklock, s);

	return 0;

      unlock_and_error:
	xnlock_put_irqrestore(&nklock, s);
      error:
	thread_set_errno(err);
	return -1;
}

/**
 * Get expiration overruns count since the most recent timer expiration
 * signal delivery.
 *
 * This service returns @a timerid expiration overruns count since the most
 * recent timer expiration signal delivery. If this count is more than @a
 * DELAYTIMER_MAX expirations, @a DELAYTIMER_MAX is returned.
 *
 * @param timerid Timer identifier.
 *
 * @return the overruns count on success;
 * @return -1 with @a errno set if:
 * - EINVAL, @a timerid is invalid;
 * - EPERM, the timer @a timerid does not belong to the current process.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/timer_getoverrun.html">
 * Specification.</a>
 *
 */
int timer_getoverrun(timer_t timerid)
{
	struct cobalt_timer *timer;
	int overruns, err;
	spl_t s;

	if ((unsigned)timerid >= COBALT_TIMER_MAX) {
		err = EINVAL;
		goto error;
	}

	xnlock_get_irqsave(&nklock, s);

	timer = &timer_pool[(unsigned long)timerid];

	if (!xntimer_active_p(&timer->timerbase)) {
		err = EINVAL;
		goto unlock_and_error;
	}

#if XENO_DEBUG(POSIX)
	if (timer->owningq != cobalt_kqueues(0)) {
		err = EPERM;
		goto unlock_and_error;
	}
#endif /* XENO_DEBUG(POSIX) */

	overruns = timer->overruns;

	xnlock_put_irqrestore(&nklock, s);

	return overruns;

  unlock_and_error:
	xnlock_put_irqrestore(&nklock, s);
  error:
	thread_set_errno(err);
	return -1;
}

void cobalt_timer_init_thread(pthread_t new_thread)
{
	initq(&new_thread->timersq);
}

/* Called with nklock locked irq off. */
void cobalt_timer_cleanup_thread(pthread_t zombie)
{
	xnholder_t *holder;
	while ((holder = getq(&zombie->timersq)) != NULL) {
		struct cobalt_timer *timer = link2tm(holder, tlink);
		xntimer_stop(&timer->timerbase);
		timer->owner = NULL;
	}
}

void cobalt_timerq_cleanup(cobalt_kqueues_t *q)
{
	xnholder_t *holder;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	while ((holder = getheadq(&q->timerq))) {
		timer_t tm = (timer_t) (link2tm(holder, link) - timer_pool);
		cobalt_timer_delete_inner(tm, q, 1);
		xnlock_put_irqrestore(&nklock, s);
#if XENO_DEBUG(POSIX)
		xnprintf("Posix timer %u deleted\n", (unsigned) tm);
#endif /* XENO_DEBUG(POSIX) */
		xnlock_get_irqsave(&nklock, s);
	}

	xnlock_put_irqrestore(&nklock, s);
}

int cobalt_timer_pkg_init(void)
{
	int n;

	initq(&timer_freeq);
	initq(&cobalt_global_kqueues.timerq);

	for (n = 0; n < COBALT_TIMER_MAX; n++) {
		inith(&timer_pool[n].link);
		appendq(&timer_freeq, &timer_pool[n].link);
	}

	return 0;
}

void cobalt_timer_pkg_cleanup(void)
{
	cobalt_timerq_cleanup(&cobalt_global_kqueues);
}

/*@}*/

EXPORT_SYMBOL_GPL(timer_create);
EXPORT_SYMBOL_GPL(timer_delete);
EXPORT_SYMBOL_GPL(timer_settime);
EXPORT_SYMBOL_GPL(timer_gettime);
EXPORT_SYMBOL_GPL(timer_getoverrun);