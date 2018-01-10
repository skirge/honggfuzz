#include "libhfnetdriver/netdriver.h"

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#if defined(_HF_ARCH_LINUX)
#include <sched.h>
#endif /* defined(_HF_ARCH_LINUX) */

#include "libhfcommon/common.h"
#include "libhfcommon/log.h"
#include "libhfcommon/ns.h"

const char *LIBHNETDRIVER_module_netdriver = NULL;

#define HF_TCP_PORT_ENV "_HF_TCP_PORT"

static char *initial_server_argv[] = {"fuzzer", NULL};

static struct {
    uint16_t tcp_port;
    int argc_server;
    char **argv_server;
} hfnd_globals = {
    .tcp_port = 8080,
    .argc_server = 1,
    .argv_server = initial_server_argv,
};

__attribute__((weak)) int HonggfuzzNetDriver_main(
    int argc HF_ATTR_UNUSED, char **argv HF_ATTR_UNUSED) {
    LOG_F("The HonggfuzzNetDriver_main function was not defined in your code");
    return EXIT_FAILURE;
}

static void *netDriver_mainProgram(void *unused HF_ATTR_UNUSED) {
    int ret = HonggfuzzNetDriver_main(hfnd_globals.argc_server, hfnd_globals.argv_server);
    LOG_I("Honggfuzz Net Driver (pid=%d): HonggfuzzNetDriver_main() function exited with: %d",
        (int)getpid(), ret);
    _exit(ret);
}

static void netDriver_startOriginalProgramInThread(void) {
    pthread_t t;
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1024 * 1024 * 8);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    if (pthread_create(&t, &attr, netDriver_mainProgram, NULL) != 0) {
        PLOG_F("Couldn't create the 'netDriver_mainProgram' thread");
    }
}

static void netDriver_initNsIfNeeded(void) {
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;

#if defined(_HF_ARCH_LINUX)
    if (!nsEnter(CLONE_NEWUSER | CLONE_NEWNET | CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWUTS)) {
        LOG_F("nsEnter(CLONE_NEWUSER|CLONE_NEWNET|CLONE_NEWNS|CLONE_NEWIPC|CLONE_NEWUTS) failed");
    }
    if (!nsIfaceUp("lo")) {
        LOG_F("nsIfaceUp('lo') failed");
    }
    if (!nsMountTmpfs("/tmp")) {
        LOG_F("nsMountTmpfs('/tmp') failed");
    }
    return;
#endif /* defined(_HF_ARCH_LINUX) */
    LOG_W("Honggfuzz Net Driver (pid=%d): Namespaces not enabled for this OS platform",
        (int)getpid());
}

static int netDriver_sockConnAddr(const struct sockaddr *addr, socklen_t socklen) {
    int sock = socket(addr->sa_family, SOCK_STREAM, IPPROTO_TCP);
    if (sock == -1) {
        PLOG_D("socket(type=%d, SOCK_STREAM, IPPROTO_TCP)", addr->sa_family);
        return -1;
    }
    int sz = (1024 * 1024);
    if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sz, sizeof(sz)) == -1) {
        PLOG_F("setsockopt(type=%d, socket=%d, SOL_SOCKET, SO_SNDBUF, size=%d", addr->sa_family,
            sock, sz);
    }
    if (TEMP_FAILURE_RETRY(connect(sock, addr, socklen)) == -1) {
        PLOG_D("connect(type=%d, loopback)", addr->sa_family);
        close(sock);
        return -1;
    }
    return sock;
}

int netDriver_sockConn(uint16_t portno) {
    int sock = -1;

    if (portno < 1) {
        LOG_F("Specified TCP port (%d) cannot be < 1", portno);
    }

    /* Try IPv4's 127.0.0.1 first */
    const struct sockaddr_in saddr4 = {
        .sin_family = AF_INET,
        .sin_port = htons(portno),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };
    if ((sock = netDriver_sockConnAddr((const struct sockaddr *)&saddr4, sizeof(saddr4))) != -1) {
        return sock;
    }

    /* Next, try IPv6's ::1 */
    const struct sockaddr_in6 saddr6 = {
        .sin6_family = AF_INET6,
        .sin6_port = htons(portno),
        .sin6_flowinfo = 0,
        .sin6_addr = in6addr_loopback,
        .sin6_scope_id = 0,
    };
    if ((sock = netDriver_sockConnAddr((const struct sockaddr *)&saddr6, sizeof(saddr6))) != -1) {
        return sock;
    }

    LOG_W("Honggfuzz Net Driver (pid=%d): couldn't connect(loopback, port:%" PRIu16 ")",
        (int)getpid(), portno);

    return -1;
}

/*
 * Decide which TCP port should be used for sending inputs
 * Define this function in your code to provide custom TCP port choice
 */
__attribute__((weak)) uint16_t HonggfuzzNetDriverPort(
    int argc HF_ATTR_UNUSED, char **argv HF_ATTR_UNUSED) {
    const char *port_str = getenv(HF_TCP_PORT_ENV);
    if (port_str == NULL) {
        return hfnd_globals.tcp_port;
    }
    errno = 0;
    signed long portsl = strtol(port_str, NULL, 0);
    if (errno != 0) {
        PLOG_F("Couldn't convert '%s'='%s' to a number", HF_TCP_PORT_ENV, port_str);
    }

    if (portsl < 1) {
        LOG_F(
            "Specified TCP port '%s'='%s' (%ld) cannot be < 1", HF_TCP_PORT_ENV, port_str, portsl);
    }
    if (portsl > 65535) {
        LOG_F("Specified TCP port '%s'='%s' (%ld) cannot be > 65535", HF_TCP_PORT_ENV, port_str,
            portsl);
    }

    return (uint16_t)portsl;
}

/*
 * Split: ./httpdserver -max_input=10 -- --config /etc/httpd.confg
 * so:
 * This code (e.g. libfuzzer) will only see "./httpdserver -max_input=10",
 * while the httpdserver will only see: "./httpdserver --config /etc/httpd.confg"
 *
 * The return value is a number of arguments passed to libfuzzer (if used)
 *
 * Define this function in your code to manipulate the arguments as desired
 */
__attribute__((weak)) int HonggfuzzNetDriverArgsForServer(
    int argc, char **argv, int *server_argc, char ***server_argv) {
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) {
            *server_argc = argc - i;
            *server_argv = &argv[i];
            return argc - i;
        }
    }

    LOG_I(
        "Honggfuzz Net Driver (pid=%d): No '--' was found in the commandline, and therefore no "
        "arguments will be passed to the TCP server program",
        (int)getpid());
    *server_argc = 1;
    *server_argv = &argv[0];
    return argc;
}

void netDriver_waitForServerReady(uint16_t portno) {
    for (;;) {
        int fd = netDriver_sockConn(portno);
        if (fd >= 0) {
            close(fd);
            break;
        }
        LOG_I(
            "Honggfuzz Net Driver (pid=%d): Waiting for the TCP server process to start accepting "
            "TCP connections at 127.0.0.1:%" PRIu16 " ...",
            (int)getpid(), portno);
        sleep(1);
    }

    LOG_I(
        "Honggfuzz Net Driver (pid=%d): The TCP server process ready to accept connections at "
        "127.0.0.1:%" PRIu16 ". TCP fuzzing starts now!",
        (int)getpid(), portno);
}

__attribute__((weak)) int LLVMFuzzerInitialize(int *argc, char ***argv) {
    hfnd_globals.tcp_port = HonggfuzzNetDriverPort(*argc, *argv);
    *argc = HonggfuzzNetDriverArgsForServer(
        *argc, *argv, &hfnd_globals.argc_server, &hfnd_globals.argv_server);

    LOG_I("Honggfuzz Net Driver (pid=%d): TCP port:%d will be used", (int)getpid(),
        hfnd_globals.tcp_port);

    netDriver_initNsIfNeeded();
    netDriver_startOriginalProgramInThread();
    netDriver_waitForServerReady(hfnd_globals.tcp_port);
    return 0;
}

__attribute__((weak)) int LLVMFuzzerTestOneInput(const uint8_t *buf, size_t len) {
    int sock = netDriver_sockConn(hfnd_globals.tcp_port);
    if (sock == -1) {
        LOG_F("Couldn't connect to the server TCP port");
    }
    if (TEMP_FAILURE_RETRY(send(sock, buf, len, MSG_NOSIGNAL)) == -1) {
        PLOG_F("send(sock=%d, len=%zu) failed", sock, len);
    }
    /*
     * Indicate EOF (via the FIN flag) to the TCP server
     *
     * Well-behaved TCP servers should process the input and responsd/close the TCP connection at
     * this point
     */
    if (TEMP_FAILURE_RETRY(shutdown(sock, SHUT_WR)) == -1) {
        if (errno == ENOTCONN) {
            close(sock);
            return 0;
        }
        PLOG_F("shutdown(sock=%d, SHUT_WR)", sock);
    }

    /*
     * Try to read data from the server, assuming that an early TCP close would sometimes cause the
     * TCP server to drop the input data, instead of processing it
     */
    static char b[1024 * 1024 * 8];
    while (TEMP_FAILURE_RETRY(recv(sock, b, sizeof(b), MSG_WAITALL)) > 0)
        ;

    close(sock);

    return 0;
}