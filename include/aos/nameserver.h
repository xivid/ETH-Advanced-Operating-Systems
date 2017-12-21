#ifndef _NAMESERVER_H_
#define _NAMESERVER_H_

#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/except.h>

errval_t ns_init_channel(void);
errval_t ns_register(char *name, struct capref endpoint, ns_err_names_t *ns_err);

#endif
