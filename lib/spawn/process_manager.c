#include <aos/aos.h>
#include <spawn/process_manager.h>

/*------------------------------------------Implementations------------------------------------------------*/

/* private */
struct domain_info* process_manager_new_node(const char *name, coreid_t core, domainid_t domainid) {
    assert(name != NULL);
    assert(domainid >> 24 == 0);

    struct domain_info *di = (struct domain_info *) malloc(sizeof(struct domain_info));

    di->name = (char *) malloc(strlen(name));
    strcpy(di->name, name);
    di->pid = (core << 24) + domainid;
    di->next = NULL;

    return di;
}

domainid_t process_manager_pid_alloc(struct process_manager *pm) {
    return pm->next_pid++;
}

/* public */
errval_t process_manager_init(struct process_manager *pm) {
    pm->core = disp_get_core_id();
    pm->head = process_manager_new_node("init", pm->core, 0);
    pm->count = 1;
    pm->next_pid = pm->count;

    return SYS_ERR_OK;
}

domainid_t process_manager_add(struct process_manager *pm, const char *name) {
    assert(pm->head != NULL);

    domainid_t new_pid = process_manager_pid_alloc(pm);
    struct domain_info *di = process_manager_new_node(name, pm->core, new_pid);
    di->next = pm->head;
    pm->head = di;

    pm->count++;

    debug_printf("Added new process to the manager: name = [%s], pid = [%lx]\n", di->name, di->pid);
    return new_pid;
}

const char *process_manager_get_name(struct process_manager *pm, domainid_t pid) {
    struct domain_info *p = pm->head;
    while (p != NULL && p->pid != pid) { p = p->next; }
    if (p == NULL) {
        return NULL;
    }

    return p->name;
}

void process_manager_get_all_pids(struct process_manager *pm, domainid_t **arr, domainid_t *length) {
    *arr = (domainid_t *) malloc(sizeof(domainid_t) * pm->count);
    *length = pm->count;

    domainid_t *ptr = *arr;
    for (struct domain_info* di = pm->head; di != NULL; di = di->next, ++ptr) {
        *ptr = di->pid;
    }
}

coreid_t pid_get_core_id(domainid_t pid) {
    return pid >> 24;
}