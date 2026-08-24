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
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>   /* _getpid */
/* getpid()/usleep() are POSIX names this file otherwise uses unchanged;
 * _getpid() takes the same zero arguments and Sleep() takes milliseconds
 * where usleep() takes microseconds, hence the /1000. */
#define getpid _getpid
#define usleep(us) Sleep((DWORD)((us) / 1000))
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define CF_MAX_ARGV 32

/* Windows has no /tmp; GetTempPathA resolves whatever TEMP/TMP actually
 * point at, cached because it costs a syscall and every row asks three
 * times. Returned WITHOUT a trailing separator so every call site's
 * "%s/ppcp-conform-..." format stays the same on both platforms. */
#if defined(_WIN32)
static const char *cf_tmp_dir(void)
{
    static char buf[MAX_PATH] = "";
    if (buf[0] == '\0') {
        DWORD n = GetTempPathA(sizeof(buf), buf);
        if (n == 0 || n >= sizeof(buf))
            snprintf(buf, sizeof(buf), ".");
        else if (buf[n - 1] == '\\' || buf[n - 1] == '/')
            buf[n - 1] = '\0';
    }
    return buf;
}
#define CF_TMP_DIR cf_tmp_dir()
#else
#define CF_TMP_DIR "/tmp"
#endif

#ifdef _MSC_VER
/* fopen()/fscanf() below are portable C, correct at every call site in this
   file, and the only choice that stays true on every platform it builds on;
   the *_s() replacements are a Microsoft/Annex-K extension with no Linux/
   macOS equivalent. Both share warning 4996, so one disable covers both. */
#pragma warning(disable : 4996)
#endif

#if defined(_WIN32)
static int64_t now_ms(void)
{
    /* GetTickCount64: monotonic, millisecond-granularity already, and this
     * value is only ever differenced against another now_ms() from the same
     * boot — unlike gettimeofday's wall clock it can't be stepped backwards
     * by an NTP correction mid-row. */
    return (int64_t)GetTickCount64();
}
#else
static int64_t now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}
#endif

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

#if defined(_WIN32)

/* fork()+dup2()+execv() has no Windows equivalent — Windows processes don't
 * fork, so there is no child-side branch to speak of. CreateProcess spawns
 * the new process image directly, and the stderr redirect that dup2() did
 * AFTER the fork here is instead a handle installed in STARTUPINFO BEFORE
 * the process exists, which is why this is one call rather than fork's two
 * halves. */
typedef HANDLE cf_pid_t;
#define CF_PID_NONE NULL
static bool cf_pid_valid(cf_pid_t p) { return p != CF_PID_NONE; }

static cf_pid_t spawn(const char *const *argv, const char *err_path)
{
    char                 cmdline[2048];
    size_t               n = 0, i;
    SECURITY_ATTRIBUTES  sa;
    HANDLE               herr;
    STARTUPINFOA         si;
    PROCESS_INFORMATION  pi;

    /* CreateProcess wants one command-line string, not an argv array; every
     * argument this tool ever builds (paths, a role/scenario name, decimal
     * numbers, hex) is space- and quote-free, so wrapping each in quotes is
     * enough without a general Windows command-line escaper. */
    cmdline[0] = '\0';
    for (i = 0; argv[i] != NULL; i++) {
        size_t len = strlen(argv[i]);
        if (n + len + 4 >= sizeof(cmdline))
            return CF_PID_NONE;
        if (n > 0)
            cmdline[n++] = ' ';
        cmdline[n++] = '"';
        memcpy(cmdline + n, argv[i], len);
        n += len;
        cmdline[n++] = '"';
        cmdline[n] = '\0';
    }

    sa.nLength              = sizeof(sa);
    sa.bInheritHandle        = TRUE;
    sa.lpSecurityDescriptor  = NULL;
    herr = CreateFileA(err_path, GENERIC_WRITE, FILE_SHARE_READ, &sa,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (herr == INVALID_HANDLE_VALUE)
        return CF_PID_NONE;

    memset(&si, 0, sizeof(si));
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = herr;
    memset(&pi, 0, sizeof(pi));

    if (!CreateProcessA(argv[0], cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(herr);
        return CF_PID_NONE;
    }
    CloseHandle(herr);
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

static int wait_for(cf_pid_t pid)
{
    DWORD code = 127;
    if (!cf_pid_valid(pid))
        return 127;
    WaitForSingleObject(pid, INFINITE);
    if (!GetExitCodeProcess(pid, &code))
        code = 127;
    CloseHandle(pid);
    return (int)code;
}

/* TerminateProcess only — NOT CloseHandle: every kill() call site is
 * immediately followed by wait_for(), which is what closes the handle after
 * confirming the process actually exited. */
static void cf_terminate(cf_pid_t pid)
{
    if (cf_pid_valid(pid))
        TerminateProcess(pid, 1);
}
#define kill(pid, sig) cf_terminate(pid)

#else

typedef pid_t cf_pid_t;
#define CF_PID_NONE ((pid_t)-1)
static bool cf_pid_valid(cf_pid_t p) { return p > 0; }

static cf_pid_t spawn(const char *const *argv, const char *err_path)
{
    pid_t pid = fork();
    if (pid < 0)
        return CF_PID_NONE;
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

static int wait_for(cf_pid_t pid)
{
    int status = 0;
    if (!cf_pid_valid(pid))
        return 127;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR)
            return 127;
    }
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}

#endif

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
    cf_pid_t    put_pid = CF_PID_NONE, pid;
    int         rc, put_rc = 0;
    int64_t     t0 = now_ms();

    memset(res, 0, sizeof(*res));
    res->row     = r;
    res->verdict = CF_FAIL;

    snprintf(decl, sizeof(decl), "%s/%s", o->scenario_dir, r->declaration);
    snprintf(runms, sizeof(runms), "%d", r->run_ms);
    snprintf(err_path, sizeof(err_path), "%s/ppcp-conform-%ld-%s.log",
             CF_TMP_DIR, (long)getpid(), r->id);
    snprintf(put_err_path, sizeof(put_err_path), "%s/ppcp-conform-%ld-%s-put.log",
             CF_TMP_DIR, (long)getpid(), r->id);
    snprintf(port_path, sizeof(port_path), "%s/ppcp-conform-%ld-%s.port",
             CF_TMP_DIR, (long)getpid(), r->id);
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
        if (!cf_pid_valid(put_pid)) {
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
    if (!cf_pid_valid(pid)) {
        snprintf(res->reason, sizeof(res->reason), "could not start the counterpart");
        if (cf_pid_valid(put_pid)) { kill(put_pid, SIGTERM); (void)wait_for(put_pid); }
        return;
    }
    rc = wait_for(pid);
    if (cf_pid_valid(put_pid))
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
