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
    /* debug_printf("nameserver registered a new name: \"%s\"\n", name); */
    return NS_ERR_OK;
}

char *ns_enum_records(unsigned *len)
{
    unsigned total_size = 0, cur_pos = 0;
    struct name_node_t *cur = names_list_head;
    while (cur) {
        total_size += strlen(cur->name) + 1; // + separator or null-byte
        cur = cur->next;
    }

    char *answer = malloc(total_size);
    cur = names_list_head;
    while (cur) {
        int cur_len = strlen(cur->name);
        strncpy(&answer[cur_pos], cur->name, cur_len);
        cur_pos += cur_len;
        answer[cur_pos] = ';';
        cur_pos += 1;
        cur = cur->next;
    }
    *len = total_size;
    answer[total_size - 1] = '\0';
    return answer;
}

ns_err_names_t ns_delete_record(char *name)
{
    struct name_node_t *cur = names_list_head, *prev = names_list_head;
    while (cur) {
        if (!strcmp(cur->name, name)) {
            if (cur == names_list_head) {
                names_list_head = NULL;
            } else {
                prev->next = cur->next;
            }
            debug_printf("service: %s died\n", name);
            return NS_ERR_OK;
        }
        prev = cur;
        cur = cur->next;
    }
    return NS_ERR_NAME_NOT_FOUND;
}

