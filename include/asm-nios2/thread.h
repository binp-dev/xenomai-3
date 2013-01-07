/*
 * Copyright (C) 2009,2013 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _XENO_ASM_NIOS2_THREAD_H
#define _XENO_ASM_NIOS2_THREAD_H

#include <asm-generic/xenomai/thread.h>

#ifndef CONFIG_MMU
#error "Xenomai: please use Xenomai 2.5.x for MMU-less support"
#endif

struct xnarchtcb {
	struct xntcb core;
	struct {
		unsigned long ea;
		unsigned long r2;
		unsigned long r3;
	} mayday;
};

#define xnarch_fpu_ptr(tcb)     NULL
#define xnarch_fault_trap(d)   ((d)->exception)
#define xnarch_fault_code(d)   (0) /* None on this arch. */
#define xnarch_fault_pc(d)     ((d)->regs->ea)
#define xnarch_fault_fpu_p(d)  (0) /* Can't be. */
#define xnarch_fault_pf_p(d)   (0) /* No page faults. */
#define xnarch_fault_bp_p(d)   ((current->ptrace & PT_PTRACED) &&	\
				((d)->exception == IPIPE_TRAP_BP))

#define xnarch_fault_notify(d) (xnarch_fault_bp_p(d) == 0)

static inline void xnarch_init_root_tcb(struct xnarchtcb *tcb) { }
static inline void xnarch_init_shadow_tcb(struct xnarchtcb *tcb) { }
static inline void xnarch_enter_root(struct xnarchtcb *rootcb) { }
static inline void xnarch_leave_root(struct xnarchtcb *rootcb) { }
static inline void xnarch_enable_fpu(struct xnarchtcb *current_tcb) { }
static inline void xnarch_save_fpu(struct xnarchtcb *tcb) { }
static inline void xnarch_restore_fpu(struct xnarchtcb *tcb) { }

static inline int xnarch_handle_fpu_fault(struct xnarchtcb *tcb)
{
	return 0;
}

void xnarch_switch_to(struct xnarchtcb *out_tcb, struct xnarchtcb *in_tcb);

int xnarch_escalate(void);

#endif /* !_XENO_ASM_NIOS2_THREAD_H */
