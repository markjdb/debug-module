#include <sys/types.h>
#include <sys/param.h>

#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

MALLOC_DECLARE(M_DEBUGMODULE);

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
		printf("debug module loaded\n");
		break;
	case MOD_UNLOAD:
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
