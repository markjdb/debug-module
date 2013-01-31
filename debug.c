#include <sys/types.h>
#include <sys/param.h>

#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

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

static int		debug_enable_memver();
static void		debug_start_memver();
static void		debug_verify_memver();

static void
debug_start_memver()
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
debug_verify_memver()
{
	uint32_t crc;
	int i, j;

	crc = crc32(desc->d_data, desc->d_dlen);
	if (crc != desc->d_crc) {
		printf("debug: CRC mismatch, dumping block\n");
		for (i = 0; i < desc->d_dlen; i += 8) {
			printf("\t%p:", (char *)desc->d_data + i);
			for (j = 0; j < min(desc->d_dlen - i, 8); j++)
				printf(" %02x", *((u_char *)desc->d_data + i + j));
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
			printf("debug: verification is already running\n");
			return (EINVAL);
		} else if (val < 0 || val > 4096) {
			printf("debug: invalid block size %d\n", val);
			return (EINVAL);
		}
		desc->d_data = NULL;
		desc->d_dlen = val;

		callout_init(&desc->d_callout, 1 /* MPSAFE */);
		callout_reset(&desc->d_callout, 1, debug_start_memver, NULL);
		desc->d_ver = 1;

		printf("debug: starting memory verification on %zd-byte blocks\n",
		    desc->d_dlen);
	} else if (desc->d_ver != 0) {
		printf("debug: verification finished\n");
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
debug_modevent(struct module *m, int what, void *arg)
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
