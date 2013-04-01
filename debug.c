#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>

#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/libkern.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/sched.h>
#include <sys/sysctl.h>
#include <sys/unistd.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/uma.h>

MALLOC_DECLARE(M_DEBUGMOD);
MALLOC_DEFINE(M_DEBUGMOD, "debug", "Memory used by the debug module");

struct debug_desc {
	void		*d_data;
	size_t		 d_dlen;
	uint32_t	 d_crc;
	char		 d_ver;
	struct callout	 d_callout;
};

struct debug_desc *desc;

static int		debug_enable_memver(SYSCTL_HANDLER_ARGS);
static void		debug_start_memver(void *);
static void		debug_verify_memver(void *);

static void
debug_start_memver(void *arg __unused)
{
	void *data;

	data = desc->d_data;
	/* Allocate a new block before freeing the old one. */
	desc->d_data = malloc(desc->d_dlen, M_DEBUGMOD, M_WAITOK);
	free(data, M_DEBUGMOD);
	data = desc->d_data;

	arc4rand(data, desc->d_dlen, 0);
	desc->d_crc = crc32(data, desc->d_dlen);

	callout_reset(&desc->d_callout, hz / 4, debug_verify_memver, NULL);
}

static void
debug_verify_memver(void *arg __unused)
{
	uint32_t crc;
	unsigned int i, j;

	crc = crc32(desc->d_data, desc->d_dlen);
	if (crc != desc->d_crc) {
		printf("debug: CRC mismatch, dumping block\n");
		for (i = 0; i < desc->d_dlen; i += 8) {
			printf("\t%p:", (char *)desc->d_data + i);
			for (j = 0; j < min(desc->d_dlen - i, 8); j++)
				printf(" %02x",
				    *((u_char *)desc->d_data + i + j));
			printf("\n");
		}
	}

	callout_reset(&desc->d_callout, 1, debug_start_memver, NULL);
}

static int
debug_enable_memver(SYSCTL_HANDLER_ARGS)
{
	int val, error;

	val = desc->d_dlen;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || req->newptr == NULL)
		return (error);

	if (val != 0) {
		if (desc->d_ver == 1) {
			uprintf("debug: verification is already running\n");
			return (EINVAL);
		} else if (val < 0 || val > 4096) {
			uprintf("debug: invalid block size %d\n", val);
			return (EINVAL);
		}
		desc->d_data = NULL;
		desc->d_dlen = val;

		callout_init(&desc->d_callout, 1 /* MPSAFE */);
		callout_reset(&desc->d_callout, 1, debug_start_memver, NULL);
		desc->d_ver = 1;

		uprintf(
		    "debug: starting memory verification on %zu-byte blocks\n",
		    desc->d_dlen);
	} else if (desc->d_ver != 0) {
		uprintf("debug: verification finished\n");
		callout_drain(&desc->d_callout);
		free(desc->d_data, M_DEBUGMOD);
		desc->d_data = NULL;
		desc->d_dlen = 0;

		desc->d_ver = 0;
	}

	return (0);
}

SYSCTL_PROC(_debug, OID_AUTO, memver, CTLTYPE_INT | CTLFLAG_RW, 0, 0,
    debug_enable_memver, "I", "enable and disable malloc verification");

static int
debug_lor(SYSCTL_HANDLER_ARGS)
{
	static struct mtx mtx1, mtx2;
	static int i = 0;
	char mtxname1[32], mtxname2[32];
	int val, error;

	val = 0;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || req->newptr == NULL)
		return (error);
	else if (val == 0)
		return (0);

	/* mtx_init doesn't make a copy of the name. */
	snprintf(mtxname1, sizeof(mtxname1), "mtx%d", i++);
	mtx_init(&mtx1, NULL, mtxname1, MTX_DEF);
	snprintf(mtxname2, sizeof(mtxname2), "mtx%d", i++);
	mtx_init(&mtx2, NULL, mtxname2, MTX_DEF);

	mtx_lock(&mtx1);
	mtx_lock(&mtx2);
	mtx_unlock(&mtx2);
	mtx_unlock(&mtx1);

	mtx_lock(&mtx2);
	mtx_lock(&mtx1);
	mtx_unlock(&mtx1);
	mtx_unlock(&mtx2);

	mtx_destroy(&mtx1);
	mtx_destroy(&mtx2);

	return (0);
}

SYSCTL_PROC(_debug, OID_AUTO, lor, CTLTYPE_INT | CTLFLAG_RW, 0, 0, debug_lor,
    "I", "trigger a lock order reversal");

static int
debug_grab_giant(SYSCTL_HANDLER_ARGS)
{
	int val, error;

	val = 0;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || req->newptr == NULL)
		return (error);
	else if (val == 0)
		return (0);

	mtx_lock(&Giant);

	while (1) ;

	return (0);
}

SYSCTL_PROC(_debug, OID_AUTO, grab_giant, CTLTYPE_INT | CTLFLAG_RW, 0, 0,
    debug_grab_giant, "I", "acquire the Giant lock");

static int
debug_print_line(SYSCTL_HANDLER_ARGS)
{
	int val, error;

	val = 0;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || req->newptr == NULL)
		return (error);

	printf("debug_print_line: arg %d ----------\n", val);

	return (0);
}

SYSCTL_PROC(_debug, OID_AUTO, print_line, CTLTYPE_INT | CTLFLAG_WR, 0, 0,
    debug_print_line, "I", "print a separator");

static void
debug_bind(void *arg)
{
	static int count = 0;

	printf("debug_bind: binding to CPU 0 (count: %d)\n", count);

	thread_lock(curthread);
	sched_bind(curthread, 0);
	thread_unlock(curthread);

	printf("debug_bind: running on CPU 0 (count: %d)\n", count);
	count++;

	thread_lock(curthread);
	sched_unbind(curthread);
	thread_unlock(curthread);

	printf("debug_bind: unbound from CPU 0 (count: %d)\n", count);

	callout_reset((struct callout *)arg, hz, debug_bind, arg);
}

static void
debug_hipri(void *arg __unused)
{
	int one, two;

	pause("hipri", hz);

	printf("debug_hipri: starting\n");

	thread_lock(curthread);
	sched_bind(curthread, 0);
	thread_unlock(curthread);

	printf("debug_hipri: spinning\n");

	two = 0;
	while (two++ < 50) {
		one = 0;
		while (one++ < 1000000000);
	}

	printf("debug_hipri: finished spinning\n");

	thread_lock(curthread);
	sched_unbind(curthread);
	thread_unlock(curthread);

	printf("debug_hipri: returning\n");
}

/*
 * This sysctl aims to help track down a possible issue with sched_bind().
 *
 * It starts a self-scheduling callout which binds to CPU 0 and spins for
 * a short time before unbinding itself, rescheduling and returning. It
 * also starts a new high-priority kernel thread which binds to CPU 0,
 * spins for a while, and returns. The idea is to get the callout thread
 * to sit on a runqueue for a while since it can't preempt the higher-priority
 * thread that's hogging CPU 0.
 *
 * To use this sysctl, set it to a non-zero value.
 */
static int
debug_co_preempt(SYSCTL_HANDLER_ARGS)
{
	struct thread *td;
	struct callout *co;
	struct mtx *m;
	int val, error;

	val = 0;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || req->newptr == NULL)
		return (error);
	if (val == 0)
		return (0);

	m = malloc(sizeof(*m), M_DEBUGMOD, M_WAITOK | M_ZERO);
	co = malloc(sizeof(*co), M_DEBUGMOD, M_WAITOK | M_ZERO);

	mtx_init(m, "co mtx", NULL, MTX_DEF);
	callout_init_mtx(co, m, 0);
	callout_reset(co, hz, debug_bind, co);

	error = kthread_add(debug_hipri, NULL, curproc, &td, RFSTOPPED,
	    0, "hipri");
	if (error != 0) {
		uprintf("debug_co_preempt: error creating kthread\n");
		return (EINVAL); /* XXX What should this be? */
	}
	thread_lock(td);
	sched_prio(td, 10);
	sched_add(td, SRQ_BORING);
	thread_unlock(td);

	return (error);
}

SYSCTL_PROC(_debug, OID_AUTO, co_preempt, CTLTYPE_INT | CTLFLAG_WR, 0, 0,
    debug_co_preempt, "I", "start a CPU-binding callout");

static int
debug_zone_alloc(SYSCTL_HANDLER_ARGS)
{
	static int c = 0;
	int error, val, i;
	struct vm_object *debug_obj;
	char *buf;
	uma_zone_t zone;
	void *item;

	val = 0;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || req->newptr == NULL)
		return (error);

	buf = malloc(32, M_DEBUGMOD, M_WAITOK);
	snprintf(buf, 32, "DEBUGZ%d", c++);

	debug_obj = malloc(sizeof(*debug_obj), M_DEBUGMOD, M_WAITOK | M_ZERO);

	zone = uma_zcreate(buf, 128, NULL, NULL, NULL, NULL, UMA_ALIGN_PTR,
	    UMA_ZONE_NOFREE | UMA_ZONE_VM);
	if (zone == NULL) {
		printf("debug: failed to allocate zone\n");
		return (EINVAL);
	} else if (uma_zone_set_obj(zone, debug_obj, 10000) == 0) {
		printf("debug: failed to set VM object for zone\n");
		return (EINVAL);
	}

	return (0);
}

SYSCTL_PROC(_debug, OID_AUTO, alloc_zone, CTLTYPE_INT | CTLFLAG_WR, 0, 0,
    debug_zone_alloc, "I", "allocate a zone");

static int
debug_modevent(struct module *m, int what, void *arg __unused)
{

	switch (what) {
	case MOD_LOAD:
		desc = malloc(sizeof(*desc), M_DEBUGMOD, M_ZERO | M_WAITOK);
		printf("debug module loaded\n");
		break;
	case MOD_UNLOAD:
		if (desc->d_ver != 0)
			callout_drain(&desc->d_callout);
		free(desc->d_data, M_DEBUGMOD);
		free(desc, M_DEBUGMOD);
		printf("debug module unloaded\n");
		break;
	}

	return (0);
}

static moduledata_t debug_moddata = {
	"debug",
	debug_modevent,
	NULL,
};

DECLARE_MODULE(debug, debug_moddata, SI_SUB_KLD, SI_ORDER_ANY);
