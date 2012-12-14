#include <sys/types.h>
#include <sys/param.h>

#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>

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
