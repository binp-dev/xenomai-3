#include <errno.h>
#include <pthread.h>
#include <cobalt/syscall.h>
#include <sys/select.h>

extern int __cobalt_muxid;

int __wrap_select (int __nfds, fd_set *__restrict __readfds,
		   fd_set *__restrict __writefds,
		   fd_set *__restrict __exceptfds,
		   struct timeval *__restrict __timeout)
{
	int err, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	err = XENOMAI_SKINCALL5(__cobalt_muxid, __cobalt_select, __nfds,
				__readfds, __writefds, __exceptfds, __timeout);

	pthread_setcanceltype(oldtype, NULL);

	if (err == -EBADF || err == -EPERM || err == -ENOSYS)
		return __STD(select(__nfds, __readfds,
				    __writefds, __exceptfds, __timeout));

	if (err >= 0)
		return err;

	errno = -err;
	return -1;
}