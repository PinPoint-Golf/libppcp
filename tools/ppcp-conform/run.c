/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * run.c — one conformance row, run as one or two `ppcp-sim` processes.
 *
 * The counterpart is the synthetic peer of CONF §2c, unmodified and driven the
 * way `tools/run-pair.sh` and the `*-sockets` ctest rows already drive it.  Its
 * exit code is the verdict — 0 means the run completed AND every `--expect`
 * held; 1 means a protocol violation it observed, an unmet expectation, or a
 * transport failure, with the reason on stderr.  This file adds process
 * management, a captured reason, and the command line the JSON reports so a row
 * can be re-run by hand.
 */
#include "conform.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define CF_MAX_ARGV 32

static int64_t now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void append_cmd(char *dst, size_t cap, const char *const *argv)
{
    size_t n = strlen(dst), i;
    for (i = 0; argv[i] != NULL; i++) {
        size_t len = strlen(argv[i]);
        if (n + len + 2 >= cap)
            return;
        if (n > 0)
            dst[n++] = ' ';
        memcpy(dst + n, argv[i], len);
        n += len;
        dst[n] = '\0';
    }
}

/* The last non-empty line the child wrote, which is where `ppcp-sim` puts the
 * one-line reason it exits non-zero with. */
static void last_line(const char *path, char *out, size_t cap)
{
    FILE *f = fopen(path, "r");
    char  line[512];
    out[0] = '\0';
    if (f == NULL)
        return;
    while (fgets(line, sizeof(line), f) != NULL) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        if (n == 0)
            continue;
        /* Frame logs are not reasons; the reason is prose. */
        if (strncmp(line, "  ", 2) == 0)
            continue;
        snprintf(out, cap, "%s", line);
    }
    fclose(f);
}

static pid_t spawn(const char *const *argv, const char *err_path)
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        int fd = open(err_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd >= 0) {
            dup2(fd, 2);
            close(fd);
        }
        execv(argv[0], (char *const *)argv);
        _exit(127);
    }
    return pid;
}

static int wait_for(pid_t pid)
{
    int status = 0;
    if (pid <= 0)
        return 127;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR)
            return 127;
    }
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}

/* Waits for the listener to report the port it bound.  `--listen 0` plus a port
 * file is what lets any number of rows run without a port to collide over. */
static int read_port(const char *path, int timeout_ms)
{
    int  waited = 0;
    for (;;) {
        FILE *f = fopen(path, "r");
        if (f != NULL) {
            int port = 0;
            int got  = fscanf(f, "%d", &port);
            fclose(f);
            if (got == 1 && port > 0)
                return port;
        }
        if (waited >= timeout_ms)
            return -1;
        usleep(10000);
        waited += 10;
    }
}

void cf_run_row(const cf_opts *o, const cf_row *r, cf_result *res)
{
    const char *argv[CF_MAX_ARGV];
    const char *put_argv[CF_MAX_ARGV];
    char        decl[1024], self_decl[1024], target[128], runms[32], listen_port[32];
    char        err_path[256], put_err_path[256], port_path[256];
    size_t      n = 0, m = 0;
    pid_t       put_pid = -1, pid;
    int         rc, put_rc = 0;
    int64_t     t0 = now_ms();

    memset(res, 0, sizeof(*res));
    res->row     = r;
    res->verdict = CF_FAIL;

    snprintf(decl, sizeof(decl), "%s/%s", o->scenario_dir, r->declaration);
    snprintf(runms, sizeof(runms), "%d", r->run_ms);
    snprintf(err_path, sizeof(err_path), "/tmp/ppcp-conform-%ld-%s.log",
             (long)getpid(), r->id);
    snprintf(put_err_path, sizeof(put_err_path), "/tmp/ppcp-conform-%ld-%s-put.log",
             (long)getpid(), r->id);
    snprintf(port_path, sizeof(port_path), "/tmp/ppcp-conform-%ld-%s.port",
             (long)getpid(), r->id);
    (void)remove(port_path);

    /* --self: the reference pairing.  A second `ppcp-sim` stands in for the peer
     * under test, listening on a port it reports, and the counterpart dials it.
     * This is how `libppcp` fills its own column (L15) with the same instrument
     * the applications are measured by, rather than with a second one. */
    if (o->self) {
        if (r->self_declaration == NULL) {
            res->verdict = CF_SKIPPED;
            snprintf(res->reason, sizeof(res->reason),
                     "no reference stand-in for this row");
            return;
        }
        snprintf(self_decl, sizeof(self_decl), "%s/%s", o->scenario_dir,
                 r->self_declaration);
        put_argv[m++] = o->sim_path;
        put_argv[m++] = "--role";
        put_argv[m++] = (r->self_role != NULL) ? r->self_role : o->role;
        put_argv[m++] = "--listen";      put_argv[m++] = "0";
        put_argv[m++] = "--port-file";   put_argv[m++] = port_path;
        put_argv[m++] = "--declaration"; put_argv[m++] = self_decl;
        put_argv[m++] = "--scenario";    put_argv[m++] = r->self_scenario;
        put_argv[m++] = "--run-ms";      put_argv[m++] = runms;
        put_argv[m++] = "--log-prefix";  put_argv[m++] = "under-test";
        if (r->self_expect != NULL) {
            put_argv[m++] = "--expect";  put_argv[m++] = r->self_expect;
        }
        put_argv[m] = NULL;
        put_pid = spawn(put_argv, put_err_path);
        if (put_pid < 0) {
            snprintf(res->reason, sizeof(res->reason), "could not start the stand-in peer");
            return;
        }
        {
            int port = read_port(port_path, 5000);
            if (port < 0) {
                kill(put_pid, SIGTERM);
                (void)wait_for(put_pid);
                snprintf(res->reason, sizeof(res->reason),
                         "the stand-in peer never bound a port");
                return;
            }
            snprintf(target, sizeof(target), "127.0.0.1:%d", port);
        }
    } else if (o->connect_host != NULL) {
        snprintf(target, sizeof(target), "%s:%d", o->connect_host, o->connect_port);
    } else {
        snprintf(listen_port, sizeof(listen_port), "%d", o->listen_port);
    }

    argv[n++] = o->sim_path;
    argv[n++] = "--role";        argv[n++] = r->sim_role;
    if (o->self || o->connect_host != NULL) {
        argv[n++] = "--connect";  argv[n++] = target;
    } else {
        argv[n++] = "--listen";   argv[n++] = listen_port;
    }
    argv[n++] = "--declaration"; argv[n++] = decl;
    argv[n++] = "--scenario";    argv[n++] = r->scenario;
    argv[n++] = "--run-ms";      argv[n++] = runms;
    argv[n++] = "--log-prefix";  argv[n++] = "conform";
    if (r->expect != NULL) {
        argv[n++] = "--expect";  argv[n++] = r->expect;
    }
    if (o->psk_hex != NULL) {
        argv[n++] = "--psk";     argv[n++] = o->psk_hex;
        if (o->psk_identity != NULL) {
            argv[n++] = "--psk-identity"; argv[n++] = o->psk_identity;
        }
    }
    argv[n] = NULL;
    append_cmd(res->command, sizeof(res->command), argv);

    pid = spawn(argv, err_path);
    if (pid < 0) {
        snprintf(res->reason, sizeof(res->reason), "could not start the counterpart");
        if (put_pid > 0) { kill(put_pid, SIGTERM); (void)wait_for(put_pid); }
        return;
    }
    rc = wait_for(pid);
    if (put_pid > 0)
        put_rc = wait_for(put_pid);

    res->exit_code = rc;
    res->ms        = now_ms() - t0;
    if (rc == 0 && put_rc == 0) {
        res->verdict = (r->kind == CF_NEGATIVE) ? CF_NA : CF_PASS;
        last_line(err_path, res->reason, sizeof(res->reason));
        res->reason[0] = '\0';
    } else {
        res->verdict = CF_FAIL;
        last_line(rc != 0 ? err_path : put_err_path, res->reason, sizeof(res->reason));
        if (res->reason[0] == '\0')
            snprintf(res->reason, sizeof(res->reason),
                     "counterpart exited %d, peer under test exited %d", rc, put_rc);
    }
    (void)remove(err_path);
    (void)remove(put_err_path);
    (void)remove(port_path);
}
