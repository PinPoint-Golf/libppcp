/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * rl_agree.c — the relay's side of PPCP-RV §11.11's boundary.
 *
 * ⛔ THE WHOLE FILE IS ABOUT WHAT DOES *NOT* HAPPEN HERE.  No curve
 * arithmetic, no private scalar, no crypto library linked, and no key-shaped
 * constant.  Ground rule 13 keeps X25519 out of `libppcp`; §11.11 and CA1
 * make it a PARAMETER rather than a dependency or a callback, and this is
 * that parameter arriving.  A helper process holds the scalar; two 32-octet
 * values come back, `pk` and `Z`, and 11.11d says only those two may cross.
 *
 * ⛔ `Z` IS NEVER PASSED ON A COMMAND LINE, in either direction.  argv is
 * world-readable through `ps` on every platform this runs on, and a harness
 * that leaked a shared secret to the process table would be a worse artefact
 * than the one it is testing for.  Both values cross on a pipe.
 */
#include "relay.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

static void wipe(void *p, size_t n)
{
    volatile unsigned char *q = (volatile unsigned char *)p;
    while (n-- > 0)
        *q++ = 0;
}

void rl_hex(const uint8_t *b, size_t n, char *out)
{
    static const char d[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; i++) {
        out[2 * i]     = d[(b[i] >> 4) & 0xf];
        out[2 * i + 1] = d[b[i] & 0xf];
    }
    out[2 * n] = '\0';
}

static bool unhex(const char *s, uint8_t *out, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        int hi, lo;
        char a = s[2 * i], b = s[2 * i + 1];
        hi = (a >= '0' && a <= '9') ? a - '0'
           : (a >= 'a' && a <= 'f') ? a - 'a' + 10 : -1;
        lo = (b >= '0' && b <= '9') ? b - '0'
           : (b >= 'a' && b <= 'f') ? b - 'a' + 10 : -1;
        if (hi < 0 || lo < 0)
            return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return s[2 * n] == '\0';
}

static void seterr(char *err, size_t n, const char *fmt, const char *a)
{
    if (err != NULL && n > 0)
        snprintf(err, n, fmt, a);
}

bool rl_agree_open(rl_agree *a, const char *helper, char *err, size_t errlen)
{
    int to_pipe[2], from_pipe[2];

    memset(a, 0, sizeof(*a));
    a->to_fd = a->from_fd = -1;

    if (pipe(to_pipe) != 0) {
        seterr(err, errlen, "pipe: %s", strerror(errno));
        return false;
    }
    if (pipe(from_pipe) != 0) {
        seterr(err, errlen, "pipe: %s", strerror(errno));
        close(to_pipe[0]); close(to_pipe[1]);
        return false;
    }

    a->pid = fork();
    if (a->pid < 0) {
        seterr(err, errlen, "fork: %s", strerror(errno));
        close(to_pipe[0]); close(to_pipe[1]);
        close(from_pipe[0]); close(from_pipe[1]);
        return false;
    }
    if (a->pid == 0) {
        /* child: the only process in this system that ever holds a private
         * scalar (11.11h). */
        dup2(to_pipe[0], STDIN_FILENO);
        dup2(from_pipe[1], STDOUT_FILENO);
        close(to_pipe[0]); close(to_pipe[1]);
        close(from_pipe[0]); close(from_pipe[1]);
        execl("/bin/sh", "sh", helper, (char *)NULL);
        _exit(127);
    }
    close(to_pipe[0]);
    close(from_pipe[1]);
    a->to_fd   = to_pipe[1];
    a->from_fd = from_pipe[0];
    a->open    = true;
    return true;
}

/* One line in, one line out.  Blocking: the exchange is two `openssl`
 * invocations and takes milliseconds, and there is no loop here to starve. */
static bool ask(rl_agree *a, const char *cmd, char *reply, size_t reply_len,
                char *err, size_t errlen)
{
    size_t  n = strlen(cmd), got = 0;
    ssize_t w;

    if (!a->open) {
        seterr(err, errlen, "agreement helper is not running%s", "");
        return false;
    }
    while (n > 0) {
        w = write(a->to_fd, cmd, n);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            seterr(err, errlen, "write to helper: %s", strerror(errno));
            return false;
        }
        cmd += (size_t)w;
        n   -= (size_t)w;
    }
    for (;;) {
        char c;
        ssize_t r = read(a->from_fd, &c, 1);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            seterr(err, errlen, "read from helper: %s", strerror(errno));
            return false;
        }
        if (r == 0) {
            /* ⛔ THE MOST LIKELY CAUSE BY FAR IS A MISSING `openssl`, AND IT
             * MUST NOT READ AS A CONFORMANCE FAILURE.  H and D run this on
             * their own machines; a setup fault reported as "the handshake
             * did not complete" sends a team looking for a defect in its own
             * §11 code that is not there. */
            seterr(err, errlen,
                   "the §11.11 agreement helper exited before replying — is "
                   "`openssl` on PATH? ⛔ THIS IS A HARNESS FAULT, NOT A "
                   "CONFORMANCE FAILURE%s", "");
            return false;
        }
        if (c == '\n')
            break;
        if (got + 1 < reply_len)
            reply[got++] = c;
    }
    reply[got] = '\0';
    return true;
}

bool rl_agree_keygen(rl_agree *a, uint8_t pk[PPCP_RV_BS_KEY_BYTES],
                     char *err, size_t errlen)
{
    char reply[256];

    /* 11.5a — a FRESH keypair for every attempt, used for that attempt only.
     * One engine is one attempt (the header says so), so one helper is one
     * keypair and the helper refuses a second `keygen`. */
    if (a->keyed) {
        seterr(err, errlen, "keygen called twice on one leg (11.5a)%s", "");
        return false;
    }
    if (!ask(a, "keygen\n", reply, sizeof(reply), err, errlen))
        return false;
    if (strncmp(reply, "pk ", 3) != 0 || !unhex(reply + 3, pk, PPCP_RV_BS_KEY_BYTES)) {
        seterr(err, errlen, "helper did not return a public key: %s", reply);
        return false;
    }
    a->keyed = true;
    return true;
}

rl_agree_rc rl_agree_shared(rl_agree *a, const uint8_t peer_pk[PPCP_RV_BS_KEY_BYTES],
                            uint8_t z[PPCP_RV_BS_KEY_BYTES], char *err, size_t errlen)
{
    char cmd[16 + RL_HEX_LEN];
    char hex[RL_HEX_LEN];
    char reply[256];

    rl_hex(peer_pk, PPCP_RV_BS_KEY_BYTES, hex);
    snprintf(cmd, sizeof(cmd), "agree %s\n", hex);

    if (!ask(a, cmd, reply, sizeof(reply), err, errlen))
        return RL_AGREE_BROKEN;

    if (strncmp(reply, "z ", 2) == 0) {
        if (!unhex(reply + 2, z, PPCP_RV_BS_KEY_BYTES)) {
            seterr(err, errlen, "helper returned a malformed Z: %s", reply);
            return RL_AGREE_BROKEN;
        }
        /* ⛔ 11.11f's other half, and the engine catches it too: an all-zero
         * Z and a REPORTED failure are the SAME EVENT and both mean
         * `invalid_key`.  Checked here as well as in
         * ppcp_bs_engine_supply_secret() because a helper that quietly
         * returned zeros rather than failing would otherwise reach the engine
         * as a valid-looking value, and the point of this boundary is that
         * the caller cannot know which behaviour its supplier has. */
        {
            size_t  i;
            uint8_t acc = 0;
            for (i = 0; i < PPCP_RV_BS_KEY_BYTES; i++)
                acc |= z[i];
            if (acc == 0) {
                wipe(z, PPCP_RV_BS_KEY_BYTES);
                seterr(err, errlen, "key agreement returned an all-zero Z "
                                    "(11.6b: invalid_key)%s", "");
                return RL_AGREE_REJECTED;
            }
        }
        return RL_AGREE_OK;
    }
    if (strncmp(reply, "err ", 4) == 0) {
        /* ⛔ An ATTACK SIGNAL, not a transport error, and the caller must not
         * retry it (11.11f, trap 7). */
        seterr(err, errlen, "key agreement rejected: %s", reply + 4);
        return RL_AGREE_REJECTED;
    }
    seterr(err, errlen, "helper said: %s", reply);
    return RL_AGREE_BROKEN;
}

void rl_agree_close(rl_agree *a)
{
    int status;

    if (!a->open)
        return;
    (void)!write(a->to_fd, "quit\n", 5);
    close(a->to_fd);
    close(a->from_fd);
    a->to_fd = a->from_fd = -1;
    a->open  = false;
    /* 11.11h — the scalar dies with the helper, and we wait for it so that a
     * process holding key material is never left behind by a run that
     * finished. */
    if (a->pid > 0) {
        while (waitpid(a->pid, &status, 0) < 0 && errno == EINTR)
            ;
        a->pid = 0;
    }
}
