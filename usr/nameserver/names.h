#ifndef _NS_NAMES_H_
#define _NS_NAMES_H_

#include <aos/aos.h>
#include <aos/nameserver.h>

struct name_node_t {
    struct capref cap;
    char *name;
    struct name_node_t *next;
};

struct capref *ns_find_record(char *name);
ns_err_names_t ns_add_record(char *name, struct capref cap);

// TODO: ns_delete_record
// TODO: ns_enum_records





#endif
