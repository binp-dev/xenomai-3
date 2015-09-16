/*
 * Copyright (C) 2005-2015 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <semaphore.h>
#include <asm/xenomai/syscall.h>
#include "current.h"
#include "internal.h"

/**
 * @ingroup cobalt_api
 * @defgroup cobalt_api_scheduler Process scheduling
 *
 * Cobalt/POSIX process scheduling
 *
 * @see
 * <a href="http://pubs.opengroup.org/onlinepubs/000095399/functions/xsh_chap02_08.html#tag_02_08_04">
 * Specification.</a>
 *
 *@{
 */

/**
 * Yield the processor.
 *
 * This function move the current thread at the end of its priority group.
 *
 * @retval 0
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sched_yield.html">
 * Specification.</a>
 *
 */
COBALT_IMPL(int, sched_yield, (void))
{
	if (cobalt_get_current() == XN_NO_HANDLE ||
	    (cobalt_get_current_mode() & (XNWEAK|XNRELAX)) == (XNWEAK|XNRELAX))
		return __STD(sched_yield());

	return -XENOMAI_SYSCALL0(sc_cobalt_sched_yield);
}

/**
 * Get minimum priority of the specified scheduling policy.
 *
 * This service returns the minimum priority of the scheduling policy @a
 * policy.
 *
 * @param policy scheduling policy.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, @a policy is invalid.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sched_get_priority_min.html">
 * Specification.</a>
 *
 */
COBALT_IMPL(int, sched_get_priority_min, (int policy))
{
	int ret;

	switch (policy) {
	case SCHED_FIFO:
	case SCHED_RR:
		break;
	default:
		ret = XENOMAI_SYSCALL1(sc_cobalt_sched_minprio, policy);
		if (ret >= 0)
			return ret;
		if (ret != -EINVAL) {
			errno = -ret;
			return -1;
		}
	}

	return __STD(sched_get_priority_min(policy));
}

/**
 * Get extended minimum priority of the specified scheduling policy.
 *
 * This service returns the minimum priority of the scheduling policy
 * @a policy, reflecting any Cobalt extension to the standard classes.
 *
 * @param policy scheduling policy.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, @a policy is invalid.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sched_get_priority_min.html">
 * Specification.</a>
 *
 */
int sched_get_priority_min_ex(int policy)
{
	int ret;

	ret = XENOMAI_SYSCALL1(sc_cobalt_sched_minprio, policy);
	if (ret >= 0)
		return ret;
	if (ret != -EINVAL) {
		errno = -ret;
		return -1;
	}

	return __STD(sched_get_priority_min(policy));
}

/**
 * Get maximum priority of the specified scheduling policy.
 *
 * This service returns the maximum priority of the scheduling policy @a
 * policy.
 *
 * @param policy scheduling policy.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, @a policy is invalid.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sched_get_priority_max.html">
 * Specification.</a>
 *
 */
COBALT_IMPL(int, sched_get_priority_max, (int policy))
{
	int ret;

	switch (policy) {
	case SCHED_FIFO:
	case SCHED_RR:
		break;
	default:
		ret = XENOMAI_SYSCALL1(sc_cobalt_sched_maxprio, policy);
		if (ret >= 0)
			return ret;
		if (ret != -EINVAL) {
			errno = -ret;
			return -1;
		}
	}

	return __STD(sched_get_priority_max(policy));
}

/**
 * Get extended maximum priority of the specified scheduling policy.
 *
 * This service returns the maximum priority of the scheduling policy
 * @a policy, reflecting any Cobalt extension to standard classes.
 *
 * @param policy scheduling policy.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, @a policy is invalid.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sched_get_priority_max.html">
 * Specification.</a>
 *
 */
int sched_get_priority_max_ex(int policy)
{
	int ret;

	ret = XENOMAI_SYSCALL1(sc_cobalt_sched_maxprio, policy);
	if (ret >= 0)
		return ret;
	if (ret != -EINVAL) {
		errno = -ret;
		return -1;
	}

	return __STD(sched_get_priority_max(policy));
}

/**
 * Set CPU-specific scheduler settings for a policy
 *
 * A configuration is strictly local to the target @a cpu, and may
 * differ from other processors.
 *
 * @param cpu processor to load the configuration of.
 *
 * @param policy scheduling policy to which the configuration data
 * applies. Currently, SCHED_TP and SCHED_QUOTA are valid.
 *
 * @param config a pointer to the configuration data to load on @a
 * cpu, applicable to @a policy.
 *
 * @par Settings applicable to SCHED_TP
 *
 * This call controls the temporal partitions for @a cpu, depending on
 * the operation requested.
 *
 * - config.tp.op specifies the operation to perform:
 *
 * - @a sched_tp_install installs a new TP schedule on @a cpu, defined
 *   by config.tp.windows[]. The global time frame is not activated
 *   upon return from this request yet; @a sched_tp_start must be
 *   issued to activate the temporal scheduling on @a CPU.
 *
 * - @a sched_tp_uninstall removes the current TP schedule from @a
 *   cpu, releasing all the attached resources. If no TP schedule
 *   exists on @a CPU, this request has no effect.
 *
 * - @a sched_tp_start enables the temporal scheduling on @a cpu,
 * starting the global time frame. If no TP schedule exists on @a cpu,
 * this action has no effect.
 *
 * - @a sched_tp_stop disables the temporal scheduling on @a cpu.  The
 * current TP schedule is not uninstalled though, and may be
 * re-started later by a @a sched_tp_start request.
 * @caution As a consequence of this request, threads assigned to the
 * un-scheduled partitions may be starved from CPU time.
 *
 * - for a @a sched_tp_install operation, config.tp.nr_windows
 * indicates the number of elements present in the config.tp.windows[]
 * array. If config.tp.nr_windows is zero, the action taken is
 * identical to @a sched_tp_uninstall.
 *
 * - if config.tp.nr_windows is non-zero, config.tp.windows[] is a set
 * scheduling time slots for threads assigned to @a cpu. Each window
 * is specified by its offset from the start of the global time frame
 * (windows[].offset), its duration (windows[].duration), and the
 * partition id it should activate during such period of time
 * (windows[].ptid). This field is not considered for other requests
 * than @a sched_tp_install.
 *
 * Time slots must be strictly contiguous, i.e. windows[n].offset +
 * windows[n].duration shall equal windows[n + 1].offset.  If
 * windows[].ptid is in the range
 * [0..CONFIG_XENO_OPT_SCHED_TP_NRPART-1], SCHED_TP threads which
 * belong to the partition being referred to may be given CPU time on
 * @a cpu, from time windows[].offset to windows[].offset +
 * windows[].duration, provided those threads are in a runnable state.
 *
 * Time holes between valid time slots may be defined using windows
 * activating the pseudo partition -1. When such window is active in
 * the global time frame, no CPU time is available to SCHED_TP threads
 * on @a cpu.
 *
 * @note The sched_tp_confsz(nr_windows) macro returns the length of
 * config.tp depending on the number of time slots to be defined in
 * config.tp.windows[], as specified by config.tp.nr_windows.
 *
 * @par Settings applicable to SCHED_QUOTA
 *
 * This call manages thread groups running on @a cpu, defining
 * per-group quota for limiting their CPU consumption.
 *
 * - config.quota.op should define the operation to be carried
 * out. Valid operations are:
 *
 *    - sched_quota_add for creating a new thread group on @a cpu.
 *      The new group identifier will be written back to info.tgid
 *      upon success. A new group is given no initial runtime budget
 *      when created. sched_quota_set should be issued to enable it.
 *
 *    - sched_quota_remove for deleting a thread group on @a cpu. The
 *      group identifier should be passed in config.quota.remove.tgid.
 *
 *    - sched_quota_set for updating the scheduling parameters of a
 *      thread group defined on @a cpu. The group identifier should be
 *      passed in config.quota.set.tgid, along with the allotted
 *      percentage of the quota interval (config.quota.set.quota), and
 *      the peak percentage allowed (config.quota.set.quota_peak).
 *
 * All three operations fill in the @a config.info structure with the
 * information reflecting the state of the scheduler on @a cpu with
 * respect to @a policy, after the requested changes have been
 * applied.
 *
 * @param len overall length of the configuration data (in bytes).
 *
 * @return 0 on success;
 * @return an error number if:
 *
 * - EINVAL, @a cpu is invalid, or @a policy is unsupported by the
 * current kernel configuration, @a len is invalid, or @a config
 * contains invalid parameters.
 *
 * - ENOMEM, lack of memory to perform the operation.
 *
 * - EBUSY, with @a policy equal to SCHED_QUOTA, if an attempt is made
 *   to remove a thread group which still manages threads.
 *
 * - ESRCH, with @a policy equal to SCHED_QUOTA, if the group
 *   identifier required to perform the operation is not valid.
 */
int sched_setconfig_np(int cpu, int policy,
		       const union sched_config *config, size_t len)
{
	return -XENOMAI_SYSCALL4(sc_cobalt_sched_setconfig_np,
				 cpu, policy, config, len);
}

/**
 * Retrieve CPU-specific scheduler settings for a policy
 *
 * A configuration is strictly local to the target @a cpu, and may
 * differ from other processors.
 *
 * @param cpu processor to retrieve the configuration of.
 *
 * @param policy scheduling policy to which the configuration data
 * applies. Currently, only SCHED_TP and SCHED_QUOTA are valid input.
 *
 * @param config a pointer to a memory area which receives the
 * configuration settings upon success of this call.
 *
 * @par SCHED_TP specifics
 *
 * On successful return, config->quota.tp contains the TP schedule
 * active on @a cpu.
 *
 * @par SCHED_QUOTA specifics
 *
 * On entry, config->quota.get.tgid must contain the thread group
 * identifier to inquire about.
 *
 * On successful exit, config->quota.info contains the information
 * related to the thread group referenced to by
 * config->quota.get.tgid.
 *
 * @param[in, out] len_r a pointer to a variable for collecting the
 * overall length of the configuration data returned (in bytes). This
 * variable must contain the amount of space available in @a config
 * when the request is issued.
 *
 * @return the number of bytes copied to @a config on success;
 * @return a negative error number if:
 *
 * - EINVAL, @a cpu is invalid, or @a policy is unsupported by the
 * current kernel configuration, or @a len cannot hold the retrieved
 * configuration data.
 *
 * - ESRCH, with @a policy equal to SCHED_QUOTA, if the group
 *   identifier required to perform the operation is not valid
 *   (i.e. config->quota.get.tgid is invalid).
 *
 * - ENOMEM, lack of memory to perform the operation.
 *
 * - ENOSPC, @a len is too short.
 */
ssize_t sched_getconfig_np(int cpu, int policy,
			   union sched_config *config, size_t *len_r)
{
	ssize_t ret;

	ret = XENOMAI_SYSCALL4(sc_cobalt_sched_getconfig_np,
			       cpu, policy, config, *len_r);
	if (ret < 0)
		return -ret;

	*len_r = ret;

	return 0;
}

/** @} */
