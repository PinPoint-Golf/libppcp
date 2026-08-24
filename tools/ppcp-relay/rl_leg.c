/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * rl_leg.c — one leg of the relay: L20's engine pumped over one socket.
 *
 * ⛔ THE ENGINE IS NOT MODIFIED AND IS NOT REIMPLEMENTED.  Every leg here —
 * the relay's two, and the honest stand-in peers `--peer` runs — is
 * `ppcp_bs_engine`, the same code both applications embed (CA2).  A harness
 * that wrote its own §11.5 would be a fourth implementation to keep honest,
 * and any divergence in it would read as a defect in the peer under test.
 *
 * ⛔ WHERE THE ATTACK LIVES, THEREFORE, IS ABOVE THE ENGINE AND ON THE WIRE.
 * The engine still returns `bs_accept` in the same call that consumed
 * `bs_offer` (11.5c) — there is no API in it that could defer that, which was
 * deliberate.  What this file can do, and what a relay standing on the link
 * can do, is decline to PUT a frame on the wire.  That is what
 * `withhold_reveal` and `no_reply` are, and it is the only vantage point from
 * which RT-20b(ii) is observable at all.
 *
 * ⛔ recv() CONSUMES EXACTLY ONE FRAME PER CALL and reports `consumed`, so
 * this loops until it stops consuming.  A pump that called it once per read
 * would silently drop the second frame of any pair that arrived in one
 * segment — and TCP coalesces `bs_confirm` behind `bs_reveal` routinely on
 * loopback, so it would work in testing and fail in the field.
 *
 * ⛔ THE ENGINE OWNS NO CLOCK (ground rule 8), so 11.3e's two timers are
 * supplied from here, through ppcp_bs_engine_abort(…, TIMEOUT, …): 30 seconds
 * to reach 11.5f and 60 more awaiting an affirmation.
 */
#include "relay.h"

#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

const char *rl_reason_name(ppcp_bs_reason rc)
{
    switch (rc) {
    case PPCP_BS_RC_UNSUPPORTED_VERSION: return "unsupported_version";
    case PPCP_BS_RC_COMMITMENT_MISMATCH: return "commitment_mismatch";
    case PPCP_BS_RC_INVALID_KEY:         return "invalid_key";
    case PPCP_BS_RC_REJECTED:            return "rejected";
    case PPCP_BS_RC_TIMEOUT:             return "timeout";
    case PPCP_BS_RC_WINDOW_CLOSED:       return "window_closed";
    case PPCP_BS_RC_MALFORMED:           return "malformed";
    default:                             return "?";
    }
}

static void fail(rl_leg *l, const char *fmt, ...);

static void fail(rl_leg *l, const char *fmt, ...)
{
    va_list ap;
    if (l->failed)
        return;
    l->failed = true;
    va_start(ap, fmt);
    vsnprintf(l->err, sizeof(l->err), fmt, ap);
    va_end(ap);
}

/* The type of a frame this leg is about to send.  Decoding our own output
 * rather than inferring it from state is what makes `withhold_reveal` and
 * `no_reply` name the FRAME they withhold instead of a state they guess at. */
static bool out_type(const ppcp_bs_step *st, ppcp_bs_type *ty)
{
    ppcp_bs_frame f;
    size_t        used = 0;
    if (ppcp_bs_frame_read(st->out, st->out_len, &f, &used) != PPCP_OK)
        return false;
    *ty = f.ty;
    return true;
}

bool rl_leg_init(rl_leg *l, const char *name, ppcp_bs_role role, uint8_t v,
                 int fd, const char *helper, const rl_leg_ctl *ctl)
{
    uint8_t pk[PPCP_RV_BS_KEY_BYTES];

    memset(l, 0, sizeof(*l));
    l->name = name;
    l->role = role;
    l->v    = v;
    l->fd   = fd;
    l->ctl  = *ctl;
    if (l->ctl.exchange_timeout_ms <= 0)
        l->ctl.exchange_timeout_ms = RL_TIMEOUT_EXCHANGE_MS;
    if (l->ctl.affirm_timeout_ms <= 0)
        l->ctl.affirm_timeout_ms = RL_TIMEOUT_AFFIRM_MS;

    if (!rl_agree_open(&l->ag, helper, l->err, sizeof(l->err))) {
        l->failed = true;
        l->done   = true;
        return false;
    }
    /* 11.5a — fresh for this attempt, and this attempt only.  ⛔ The relay
     * never sees the scalar behind it (§11.11, CA8). */
    if (!rl_agree_keygen(&l->ag, pk, l->err, sizeof(l->err))) {
        l->failed = true;
        l->done   = true;
        return false;
    }
    /* ⛔ 11.5c is why `pk_own` is taken at init.  By the time a frame arrives
     * an acceptor's key is already fixed, so there is no ordering left for
     * this harness to get wrong even by accident. */
    if (ppcp_bs_engine_init(&l->eng, role, v, pk) != PPCP_OK) {
        fail(l, "ppcp_bs_engine_init failed");
        l->done = true;
        return false;
    }
    l->t_start = rl_now_ms();
    return true;
}

/* Handles one step and everything it chains into.  Returns false on a
 * HARNESS fault — never on a protocol verdict, which is data, not an error. */
static bool handle(rl_leg *l, ppcp_bs_step *st)
{
    for (;;) {
        /* ⛔ ONCE THIS RELAY HAS DELIBERATELY FALLEN SILENT IT STAYS SILENT,
         * and getting that wrong is how the first run of this harness failed.
         * RT-20b(ii) says the initiator mirror is observed by a relay that
         * "simply does not reply".  A relay that withholds `bs_reveal` and
         * then goes on to send `bs_confirm` has not stopped replying: it has
         * sent a frame that arrives out of order, and a CONFORMING peer must
         * reject it as `malformed` (11.4c).  The peer then aborts for a
         * reason the harness manufactured, its own 11.3e timer never runs,
         * and the abort code in the report describes the instrument rather
         * than the implementation.  The assertion itself still held — `pk_a`
         * is recorded when it arrives, before any of this — but a probe whose
         * later behaviour provokes the peer is a probe whose next reader will
         * be misled. */
        bool silenced = l->withheld_reveal || l->withheld_accept;

        if (st->has_out) {
            ppcp_bs_type ty;
            bool         withhold = silenced;

            if (!out_type(st, &ty)) {
                fail(l, "engine emitted a frame this tool cannot decode");
                return false;
            }
            if (ty == PPCP_BS_REVEAL && l->ctl.withhold_reveal) {
                withhold = true;
                l->withheld_reveal = true;
            } else if (ty == PPCP_BS_ACCEPT && l->ctl.no_reply) {
                withhold = true;
                l->withheld_accept = true;
            } else if (ty == PPCP_BS_ACCEPT && l->ctl.defer_accept) {
                /* Held, not dropped: released the moment `pk_i` arrives,
                 * which is what an implementation carrying trap 2 does. */
                withhold = true;
                if (st->out_len <= sizeof(l->deferred)) {
                    memcpy(l->deferred, st->out, st->out_len);
                    l->deferred_len = st->out_len;
                }
            }
            if (!withhold) {
                if (!rl_write_all(l->fd, st->out, st->out_len, l->err, sizeof(l->err))) {
                    l->failed = true;
                    return false;
                }
                switch (ty) {
                case PPCP_BS_OFFER:   l->sent_offer   = true; break;
                case PPCP_BS_ACCEPT:  l->sent_accept  = true; break;
                case PPCP_BS_REVEAL:  l->sent_reveal  = true; break;
                case PPCP_BS_CONFIRM: l->sent_confirm = true; break;
                default: break;
                }
            }
        }
        if (st->close) {
            rl_close(l->fd);
            l->fd = -1;
        }

        switch (st->event) {
        case PPCP_BS_EV_NEED_SECRET: {
            uint8_t      z[PPCP_RV_BS_KEY_BYTES];
            ppcp_bs_step next;
            rl_agree_rc  rc = rl_agree_shared(&l->ag, st->peer_pk, z,
                                              l->err, sizeof(l->err));
            if (rc == RL_AGREE_BROKEN) {
                /* ⛔ A broken SUPPLIER is a harness fault and is reported as
                 * one.  Reporting it as `invalid_key` would manufacture an
                 * attack signal out of a missing `openssl`, which is 11.11f's
                 * mistake run backwards. */
                l->failed = true;
                (void)ppcp_bs_engine_abort(&l->eng, PPCP_BS_RC_TIMEOUT, &next);
                *st = next;
                continue;
            }
            if (rc == RL_AGREE_REJECTED) {
                /* ⛔ 11.6b / 11.11f / trap 7.  An attack signal.  Aborted
                 * once, with `invalid_key`, and NOT RETRIED — a retry loop
                 * here eats 3.7b's single-attempt bound, which is what
                 * §11.8's whole argument rests on. */
                (void)ppcp_bs_engine_abort(&l->eng, PPCP_BS_RC_INVALID_KEY, &next);
                *st = next;
                continue;
            }
            {
                ppcp_result pr = ppcp_bs_engine_supply_secret(&l->eng, z, &next);
                /* `Z` has been consumed; it does not stay in this frame. */
                memset(z, 0, sizeof(z));
                if (pr != PPCP_OK && pr != PPCP_ERR_RV_INVALID_KEY) {
                    fail(l, "supply_secret returned %d", (int)pr);
                    return false;
                }
            }
            *st = next;
            continue;
        }
        case PPCP_BS_EV_COMPARE: {
            ppcp_bs_step next;
            uint32_t     sas = 0;
            if (ppcp_bs_engine_sas(&l->eng, &sas) != PPCP_OK) {
                fail(l, "engine reached COMPARE but refused to yield the digits");
                return false;
            }
            l->sas       = sas;
            l->have_sas  = true;
            l->t_compare = rl_now_ms();

            if (l->ctl.decline) {
                /* RT-20b(iii).  11.9c: this is reported as a refusal, never
                 * as something to try again. */
                (void)ppcp_bs_engine_abort(&l->eng, PPCP_BS_RC_REJECTED, &next);
                *st = next;
                continue;
            }
            if (l->ctl.never_affirm || silenced)
                return true;   /* sit here; 11.3e's 60 seconds will fire */

            /* ⛔ THIS IS NOT A USER'S AFFIRMATION AND MUST NEVER BE READ AS
             * ONE (11.1d, 11.7c, trap 8).  On the relay's own legs it is the
             * ATTACKER affirming its own two exchanges, which is exactly what
             * an attacker has and is what makes the demonstration work.  On a
             * `--peer` stand-in it is a test fixture stepping over the one
             * decision a conforming product must put to a person — which is
             * why `--peer` is documented as NOT a conformant implementation
             * and claims nothing. */
            (void)ppcp_bs_engine_affirm(&l->eng, &next);
            *st = next;
            continue;
        }
        case PPCP_BS_EV_PAIRED: {
            ppcp_bs_pairing p;
            if (ppcp_bs_engine_take_pairing(&l->eng, &p) == PPCP_OK) {
                memcpy(l->sid, p.sid, sizeof(l->sid));
                l->paired = true;
            } else {
                fail(l, "engine said PAIRED but refused take_pairing");
            }
            /* ⛔ Out and erased in one motion (trap 6).  `PRK`, `K_tls` and
             * `K_id` are NOT copied out of `p` and NOT printed anywhere: a
             * harness that rendered them would be an attack tool and would
             * put key material in a CI log. */
            memset(&p, 0, sizeof(p));
            l->done = true;
            return !l->failed;
        }
        case PPCP_BS_EV_ABORTED:
            l->aborted  = true;
            l->abort_rc = st->rc;
            l->done     = true;
            return !l->failed;
        default:
            return true;
        }
    }
}

bool rl_leg_begin(rl_leg *l)
{
    ppcp_bs_step st;

    if (l->failed || l->started)
        return !l->failed;
    l->started = true;
    l->t_start = rl_now_ms();

    if (l->role == PPCP_BS_ROLE_INITIATOR) {
        /* 11.5b — `bs_offer` carries the COMMITMENT and not `pk_i`. */
        if (ppcp_bs_engine_start(&l->eng, &st) != PPCP_OK) {
            fail(l, "ppcp_bs_engine_start failed");
            return false;
        }
        return handle(l, &st);
    }
    return true;   /* an acceptor has nothing to say until it is spoken to */
}

bool rl_leg_on_readable(rl_leg *l)
{
    uint8_t buf[RL_RX_CAP];
    ssize_t r;

    if (l->done || l->fd < 0)
        return true;

    r = recv(l->fd, buf, sizeof(buf), 0);
    if (r < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            return true;
        snprintf(l->err, sizeof(l->err), "recv: %s", strerror(errno));
        l->failed = true;
        l->done   = true;
        return false;
    }
    if (r == 0) {
        l->peer_eof = true;
        l->done     = true;
        return true;
    }
    if (l->rx_len + (size_t)r > sizeof(l->rx)) {
        /* ENC 3a — reject before allocating, against a bound that is the real
         * one.  §11's vocabulary is closed (11.10a) so nothing here grows. */
        fail(l, "peer sent more than %zu unconsumed octets", sizeof(l->rx));
        l->done = true;
        return false;
    }
    memcpy(l->rx + l->rx_len, buf, (size_t)r);
    l->rx_len += (size_t)r;

    /* ⛔ ONE FRAME PER CALL — loop until it stops consuming. */
    for (;;) {
        ppcp_bs_step st;
        size_t       consumed = 0;
        ppcp_result  pr;

        if (l->rx_len == 0 || l->done)
            break;

        /* What arrived, recorded before the engine acts on it, so the record
         * is of the WIRE and not of a state the engine reached. */
        {
            ppcp_bs_frame f;
            size_t        used = 0;
            if (ppcp_bs_frame_read(l->rx, l->rx_len, &f, &used) == PPCP_OK) {
                switch (f.ty) {
                case PPCP_BS_OFFER:   l->saw_offer   = true; break;
                case PPCP_BS_ACCEPT:
                    l->saw_accept = true;
                    /* ⛔ RT-20b(ii), the acceptor mirror, in one line: `pk_a`
                     * is here and this relay has sent nothing but a
                     * commitment.  The peer chose its key BLIND, which is
                     * 11.5c and is the entire security of the path. */
                    if (!l->sent_reveal)
                        l->accept_before_reveal = true;
                    break;
                case PPCP_BS_REVEAL:
                    l->saw_reveal = true;
                    if (l->deferred_len > 0) {
                        (void)rl_write_all(l->fd, l->deferred, l->deferred_len,
                                           l->err, sizeof(l->err));
                        l->sent_accept  = true;
                        l->deferred_len = 0;
                    }
                    break;
                case PPCP_BS_CONFIRM: l->saw_confirm = true; break;
                case PPCP_BS_ABORT:
                    l->saw_abort      = true;
                    l->peer_abort_rc  = f.rc;
                    break;
                default: break;
                }
                if (l->withheld_accept || l->withheld_reveal)
                    l->bytes_after_silence += used;
            }
        }

        pr = ppcp_bs_engine_recv(&l->eng, l->rx, l->rx_len, &consumed, &st);
        if (pr == PPCP_ERR_TRUNCATED)
            break;                       /* state untouched; read more */
        if (consumed > 0) {
            memmove(l->rx, l->rx + consumed, l->rx_len - consumed);
            l->rx_len -= consumed;
        }
        if (!handle(l, &st))
            return false;
        if (consumed == 0)
            break;
    }
    return true;
}

void rl_leg_check_timers(rl_leg *l)
{
    ppcp_bs_step st;
    int64_t      now = rl_now_ms();

    if (l->done || !l->started)
        return;

    /* 11.3e — 60 seconds awaiting THIS end's affirmation. */
    if (l->have_sas && !l->sent_confirm && l->t_compare > 0 &&
        now - l->t_compare > l->ctl.affirm_timeout_ms) {
        (void)ppcp_bs_engine_abort(&l->eng, PPCP_BS_RC_TIMEOUT, &st);
        (void)handle(l, &st);
        return;
    }
    /* 11.3e — 30 seconds to reach 11.5f. */
    if (!l->sent_confirm && now - l->t_start > l->ctl.exchange_timeout_ms) {
        (void)ppcp_bs_engine_abort(&l->eng, PPCP_BS_RC_TIMEOUT, &st);
        (void)handle(l, &st);
    }
}

void rl_leg_finish(rl_leg *l)
{
    /* ⛔ CA8 / 11.6f / trap 6.  Every exit path, including the ones nothing
     * inside the engine can see — an abandoned run, a killed process's
     * cleanup, a peer that walked away.  Idempotent by design. */
    ppcp_bs_engine_wipe(&l->eng);
    memset(l->rx, 0, sizeof(l->rx));
    l->rx_len = 0;
    rl_agree_close(&l->ag);
    if (l->fd >= 0) {
        rl_close(l->fd);
        l->fd = -1;
    }
}

bool rl_pump(rl_leg **legs, size_t n, int64_t overall_deadline_ms)
{
    struct pollfd p[RL_MAX_LEGS];
    size_t        i;

    for (;;) {
        nfds_t  np = 0;
        size_t  map[RL_MAX_LEGS];
        int     r;
        bool    any = false;

        for (i = 0; i < n; i++) {
            rl_leg_check_timers(legs[i]);
            if (legs[i]->done || legs[i]->fd < 0)
                continue;
            any        = true;
            map[np]    = i;
            p[np].fd   = legs[i]->fd;
            p[np].events  = POLLIN;
            p[np].revents = 0;
            np++;
        }
        if (!any)
            return true;
        if (rl_now_ms() > overall_deadline_ms)
            return false;

        r = poll(p, np, 200);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        for (i = 0; i < (size_t)np; i++) {
            if (p[i].revents != 0)
                (void)rl_leg_on_readable(legs[map[i]]);
        }
    }
}
