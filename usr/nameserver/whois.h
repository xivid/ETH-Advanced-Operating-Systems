#ifndef _WHOIS_H_
#define _WHOIS_H_

#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/waitset.h>
#include <aos/except.h>

struct ns_client_t {
    unsigned id;
    struct aos_rpc *rpc;
    struct ns_client_t *next, *prev;
};

struct ns_client_t *ns_find_client(unsigned id);
int ns_add_client(struct capref cap, struct aos_rpc **rpc, unsigned *id);


#endif
