#include "urpc_server.h"
#include "lrpc_server.h"
#include "terminal.h"

static inline void dmb(void) { __asm volatile ("dmb"); }

void send_char_back(void *dst, char c);

uint32_t current_write_offset = 0;
uint32_t current_read_offset = 0;
void *urpc_shared_base = NULL;

void urpc_write(const uint32_t message[URPC_PAYLOAD_LEN], coreid_t core)
{
    volatile uint32_t *tx = (uint32_t *)urpc_shared_base + core * N_LINES * LINE_WORDS + current_write_offset*LINE_WORDS;
    while (*(tx + LINE_WORDS -1));

    dmb();

    for (int i = 0; i < URPC_PAYLOAD_LEN; i++) {
        tx[i] = message[i];
    }

    dmb();

    tx[LINE_WORDS - 1] = 1;

    current_write_offset++;
    current_write_offset %= N_LINES;
}


void urpc_read_until_ack(uint32_t *ack_response, coreid_t core)
{
    while (true) {
        volatile uint32_t *rx = (uint32_t *)urpc_shared_base + core * N_LINES * LINE_WORDS + current_read_offset * LINE_WORDS;
        while (!*(rx + LINE_WORDS - 1));

        dmb();

        if (rx[0] == 0 && rx[1] == AOS_RPC_ID_ACK) {  // ack, it's a response
            for (int i = 0; i < URPC_PAYLOAD_LEN - 1; i++) {
                ack_response[i] = rx[i+2];
            }

            dmb();

            rx[LINE_WORDS - 1] = 0;

            current_read_offset++;
            current_read_offset %= N_LINES;
            return;
        }

        urpc_read_and_process((uint32_t *) rx, core);
    }
}

void urpc_read_and_process(uint32_t *rx, coreid_t core)
{
    if (rx[1] != AOS_RPC_ID_ACK) {
        if (rx[0] == 0) {
            switch (rx[1]) {
            case AOS_RPC_ID_PROCESS:
                urpc_spawn_handler(core, (uint32_t *) rx + 2);
                break;
            case AOS_RPC_ID_GET_PIDS:
                urpc_get_pids_handler(core, 0);
                break;
            case AOS_RPC_ID_GET_PNAME:
                urpc_get_pname_handler(core, (domainid_t) rx[2]);
                break;
            case AOS_RPC_ID_GET_CHAR:
                urpc_get_char_handler(core, (void *) rx[2]);
                break;
            case AOS_RPC_ID_RETURN_CHAR:
                urpc_return_char_handler((char) rx[2], (void *)rx[3]);
                break;
            default:
                debug_printf("Message type not supported by init %lu\n", rx[1]);
                return;
            }
        } else {
            debug_printf("Forwarding not implemented!\n");
        }


        rx[LINE_WORDS - 1] = 0;

        current_read_offset++;
        current_read_offset %= N_LINES;
    } else {
        debug_printf("unhandled AOS_RPC_ID_ACK %i!\n", rx[0]);
    }
}

errval_t urpc_read_and_process_non_block(coreid_t core) {
    volatile uint32_t *rx = (uint32_t *)urpc_shared_base + core * N_LINES * LINE_WORDS + current_read_offset * LINE_WORDS;
    if (*(rx + LINE_WORDS - 1) && rx[1] != AOS_RPC_ID_ACK) {
        // there's something new
        dmb();
        urpc_read_and_process((uint32_t *) rx, my_core_id);
        return SYS_ERR_OK;
    }
    return LIB_ERR_NO_EVENT;
}

void urpc_spawn_handler(coreid_t core, void *name) {
    struct spawninfo *si;
    si = malloc(sizeof(struct spawninfo));

    errval_t err = spawn_load_by_name(name, si);
    uint32_t process_ack[URPC_PAYLOAD_LEN];
    process_ack[0] = 0;
    process_ack[1] = AOS_RPC_ID_ACK;
    process_ack[2] = si->domain_id;
    process_ack[3] = err;
    urpc_write(process_ack, 1 - core);
}

void urpc_get_pids_handler(coreid_t core, domainid_t pid) {
    size_t length;
    domainid_t *arr;
    process_manager_get_all_pids(&pm, &arr, &length);

    uint32_t pname_ack[URPC_PAYLOAD_LEN];
    pname_ack[0] = 0;
    pname_ack[1] = AOS_RPC_ID_ACK;
    pname_ack[2] = length;

    // put into msg[3..9], 7 words
    memcpy((void *)&pname_ack[3], (const void *)arr, sizeof(domainid_t) * MIN(7, length));
    urpc_write(pname_ack, 1 - core);

    if (length <= 7)
        return;

    // if there are remaining elements
    length -= 7;
    arr += 7;
    for (size_t i = 0; i < length / 8; ++i) {
        // put into msg[2..9]
        memcpy((void *)&pname_ack[2], (const void *)arr, sizeof(domainid_t) * 8);

        urpc_write(pname_ack, 1 - core);

        arr += 8;
        length -= 8;
    }

    // remaining partial packet
    if (length) {
        memcpy((void *)&pname_ack[2], (const void*)arr, sizeof(domainid_t) * length);

        urpc_write(pname_ack, 1 - core);
    }
}

void urpc_get_pname_handler(coreid_t core, domainid_t pid) {
    const char *name = process_manager_get_name(&pm, pid);
    size_t length = strlen(name);

    uint32_t pname_ack[URPC_PAYLOAD_LEN];
    pname_ack[0] = 0;
    pname_ack[1] = AOS_RPC_ID_ACK;
    pname_ack[2] = length;

    // put into msg[3..9], 7 words <=> 28 bytes
    strncpy((char *)&pname_ack[3], name, 28);
    urpc_write(pname_ack, 1 - core);

    if (length <= 28)
        return;

    // if there are remaining characters
    length -= 28;
    name += 28;
    for (size_t i = 0; i < length / 32; ++i) {
        // put into msg[2..9]
        strncpy((char *)&pname_ack[2], (const char *)name, 32);

        urpc_write(pname_ack, 1 - core);

        name += 32;
        length -= 32;
    }

    // remaining partial packet
    if (length) {
        strncpy((char *)&pname_ack[2], (const char*) name, length);

        urpc_write(pname_ack, 1 - core);
    }

    // we don't send the '\0'
}

void urpc_get_char_handler(coreid_t core, void *arg)
{
    // write
    uint32_t char_ack[URPC_PAYLOAD_LEN];
    char_ack[0] = INIT_PROCESS_ID;
    char_ack[1] = AOS_RPC_ID_ACK;

    urpc_write(char_ack, 1 - core);
    register_receiver_fn(send_char_back, arg);
}

void urpc_return_char_handler(char c, void *arg)
{
    char_forward(arg, c);
}

void send_char_back(void *arg, char c)
{
    uint32_t sent_char[URPC_PAYLOAD_LEN];
    sent_char[0] = INIT_PROCESS_ID;
    sent_char[1] = AOS_RPC_ID_RETURN_CHAR;
    sent_char[2] = c;
    sent_char[3] = (uintptr_t) arg;
    urpc_write(sent_char, 1 - my_core_id);
}

