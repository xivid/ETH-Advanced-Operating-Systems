/**
 * \file process_manager.h
 * \brief Manages processes on the local core, responsible for
 * 1) allocating Process IDs (PIDs),
 * 2) querying process name by PID
 * 3) querying the list of PIDs of all active processes
 */

//
// Created by zhiyang on 11/23/17.
//

#ifndef _INIT_PROCESS_MANAGER_H_
#define _INIT_PROCESS_MANAGER_H_

struct domain_info {
    char *name;
    /* pid: [ 1 byte for coreid, 3 bytes for process id] */
    domainid_t pid;
    struct domain_info *next;
};

struct process_manager {
    struct domain_info *head;
    coreid_t core;
    domainid_t count;
    domainid_t next_pid;
};

/**
 * Initialize a process manager. Automatically adds the init into the manager.
 * @param pm
 */
errval_t process_manager_init(struct process_manager *pm);

/**
 * Allocates a new domain_info node on the heap.
 * @param domainid must be < 2^24 as we use only three bytes to store a process id
 * @return Pointer to the newly allocated node
 */
struct domain_info* process_manager_new_node(const char *name, coreid_t core, domainid_t domainid);

/**
 * allocates an unused process id
 * @param pm
 * @return a new process id
 */
domainid_t process_manager_pid_alloc(struct process_manager *pm);

/**
 * adds a process into the process manager
 * @param pm the process manager
 * @param name name of the process
 * @param pid PID of the process, must be some value allocated by process_pid_alloc()!
 */
domainid_t process_manager_add(struct process_manager *pm, const char *name);

/**
 * get the name of a process by PID
 * @param pm
 * @param domain
 * @return The char string of the process's name. It shouldn't be freed by the caller.
 */
const char *process_manager_get_name(struct process_manager *pm, domainid_t domain);

/**
 * get an array of all processes' PIDs in this process manager
 * @param pm
 * @return An array allocated by the process manager implementation. Freeing is
 * the caller's responsibility.
 */
void process_manager_get_all_pids(struct process_manager *pm, domainid_t **arr, domainid_t *length);

/**
 * helper function for getting core id from PID
 * @param pid
 * @return
 */
coreid_t pid_get_core_id(domainid_t pid);
#endif //_INIT_PROCESS_MANAGER_H_
