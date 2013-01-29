#include <sys/types.h>
#include <sys/param.h>

#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

MALLOC_DECLARE(M_DEBUGMOD);

struct debug_desc {
	void		*d_data;
	size_t		 d_dlen;
	uint32_t	 d_csum;
	char		 d_ver;
	struct callout	 d_callout;
};

struct debug_desc *desc;

static void
debug_memver()
{

	desc->d_data = malloc(desc->d_dlen, M_DEBUGMOD, M_WAITOK);
}

static int
debug_set_memver(SYSCTL_HANDLER_ARGS)
{
	int val, error;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || !req->newptr)
		return (error);

	if (val != 0) {
		if (desc->d_ver == 1) {
			printf("debug: verification is already running\n");
			return (EINVAL);
		} else if (val < 0 || val > 4096) {
			printf("debug: invalid block size\n");
			return (EINVAL);
		}
		callout_init(&desc->d_callout, 1 /* MPSAFE */);
		desc->d_dlen = val;
		/* Kick off the verification loop. */
		debug_memver();
	} else {
		callout_drain(&desc->d_callout);
		free(desc->d_data, M_DEBUGMOD);
		desc->d_ver = 0;
	}

	return (0);
}

SYSCTL_PROC(_debug, OID_AUTO, memver, CTLTYPE_INT | CTLFLAG_RW, 0, 0,
    debug_set_memver, "I", "enable and disable malloc verification");

static int
debug_lor(SYSCTL_HANDLER_ARGS)
{
	static struct mtx mtx1, mtx2;
	static int i = 0;
	char mtxname1[32], mtxname2[32];
	int val, error;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || !req->newptr)
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

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || !req->newptr)
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
debug_modevent(struct module *m, int what, void *arg)
{

	switch (what) {
	case MOD_LOAD:
		desc = malloc(sizeof(*desc), M_DEBUGMOD, M_ZERO | M_WAITOK);
		printf("debug module loaded\n");
		break;
	case MOD_UNLOAD:
		free(desc->d_data, M_DEBUGMOD);
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
