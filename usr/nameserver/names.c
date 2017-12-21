#include "names.h"

struct name_node_t *names_list_head = NULL;


struct capref *ns_find_record(char *name)
{
    struct name_node_t *cur = names_list_head;
    while (cur) {
        if (!strcmp(name, cur->name)) {
            return &cur->cap;
        }
        cur = cur->next;
    }
    return NULL;
}

ns_err_names_t ns_add_record(char *name, struct capref cap)
{
    if (ns_find_record(name) != NULL) {
        return NS_ERR_NAME_ALREADY_TAKEN;
    }
    struct name_node_t *new_node = malloc(sizeof(struct name_node_t));
    new_node->name = name;
    new_node->cap = cap;
    new_node->next = names_list_head;
    names_list_head = new_node;
    return NS_ERR_NAME_OK;
}

