#include "lsp_transport.h"
#include "lsp_server.h"
#include <stdlib.h>

int main(void) {
    lsp_transport_init();
    lsp_server_init();
    lsp_log("mron-lsp started");

    for (;;) {
        char *msg = lsp_read_message();
        if (!msg) break;
        int should_exit = lsp_handle_message(msg);
        free(msg);
        if (should_exit) break;
    }

    lsp_server_shutdown();
    lsp_log("mron-lsp exiting");
    return 0;
}
