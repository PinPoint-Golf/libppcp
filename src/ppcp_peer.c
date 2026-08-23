/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * The peer engine — CORE §2.2.2, §7, §10; MSG §3–§5; ENC §2.1.  Work package
 * L6.  See include/ppcp/peer.h for the contract; this file is how it is kept.
 *
 * Three rules shape the code more than anything else:
 *
 *   - Nothing is allocated, so the engine is one flat struct in the caller's
 *     storage and every queue in it is a fixed byte array.
 *   - Nothing is buffered on input.  ppcp_peer_feed() consumes whole frames
 *     out of the caller's buffer and reports what it took, which is what lets
 *     the same function serve a socket and a bundle file (L8) unchanged.
 *   - Origination is checked in exactly one place — peer_queue() — so C2
 *     cannot be forgotten by a later work package adding a message.
 */
#include "ppcp/peer.h"
#include "ppcp_codec.h"
#include "ppcp_sync.h"

#include <string.h>

/* ---------------------------------------------------------------- sizing
 *
 * The outbound queue is per channel.  A `declare` carrying a dozen Sources is
 * the largest control frame this engine originates and is a few kilobytes;
 * 64 KiB is room for a burst of them without ever being the reason a caller
 * must drain mid-operation. */
#define PPCP_PEER_TXQ_BYTES     (64u * 1024u)
#define PPCP_PEER_MAX_CHANNELS  3u            /* plan A6: control, bulk, preview */
#define PPCP_PEER_MAX_STREAMS   32u
/* The counterpart's declaration outlives the frame it arrived in — 3.3a makes
 * it a complete snapshot, so it is the peer's current state until the next
 * one replaces it.  It therefore gets an arena of its own that the ordinary
 * per-frame decode never touches. */
#define PPCP_PEER_DECL_ARENA    (32u * 1024u)
#define PPCP_PEER_SCRATCH_ARENA (8u * 1024u)
/* I21 — one estimator, and so one probe sequence, per LOCAL timebase.  Four is
 * a host with three cameras on independent clocks plus its own. */
#define PPCP_PEER_MAX_SYNC      4u
/* The declared Sources this peer owns, kept as (source_id, timebase_id) pairs
 * so I26 can be enforced on `candidate` without holding the caller's whole
 * declaration alive. */
#define PPCP_PEER_MAX_OWN_SOURCES 16u
#define PPCP_PEER_MAX_OWN_TIMEBASES 8u

typedef struct tx_queue {
    uint8_t buf[PPCP_PEER_TXQ_BYTES];
    size_t  used;
    /* Bytes at the front already handed to the transport and acknowledged by
     * ppcp_peer_drain_commit().  They stay in place until `off` lands on a
     * frame boundary, because compacting mid-frame would lose the one fact
     * ppcp_peer_drain() needs: whether the head of the queue is a frame. */
    size_t  off;
} tx_queue;

/* One probe sequence's schedule.  A sequence is keyed on the PAIR (local
 * timebase, remote timebase), not on the local one alone: F-H5-1 found that
 * keying on the local timebase made I21's remote half unreachable — a host with
 * one clock could not probe a device's camera clock and its audio clock
 * separately, because the second registration collided with the first. */
typedef struct sync_sched {
    /* True when this sequence names the REMOTE timebase in `sync_probe`'s
     * `timebase_id` — the peer is selecting which of the responder's clocks to
     * measure (erratum E2).  False is 6.1d's plain case: the field names this
     * peer's own local timebase and the responder answers on whichever clock
     * it chose. */
    bool     probe_remote;
    uint64_t probe_seq;
    uint32_t burst_left;
    int64_t  next_due_ns;
    /* 6.1a — the `t1` of each outstanding probe, so an echo that is not the
     * one we sent is detectable rather than folded into the fit. */
    struct { uint64_t seq; int64_t t1_ns; bool live; } out[PPCP_SYNC_BURST * 2u];
    size_t   out_next;
} sync_sched;

struct ppcp_peer {
    /* configuration, copied — the caller's strings may be temporary */
    ppcp_role   role;
    ppcp_id     peer_id;
    ppcp_id     profiles[PPCP_MAX_PROFILES];
    size_t      profile_count;
    ppcp_id     versions[PPCP_MAX_VERSIONS];
    size_t      version_count;
    ppcp_id     min_version;
    bool        listener;
    ppcp_ingest_policy_fn ingest_policy;
    void       *ctx;
    ppcp_clock  clock;
    ppcp_result (*health)(void *ctx, ppcp_readiness *out);
    ppcp_health_fn health_report;
    bool        has_sync_tb;
    ppcp_id     sync_tb;

    /* connection */
    ppcp_peer_state state;
    bool         has_version;
    ppcp_id      version;
    ppcp_msg_seq seq;
    uint64_t     generation;
    bool         has_link_id;
    uint8_t      link_id[PPCP_LINK_ID_BYTES];
    uint32_t     opened_channels;

    /* counterpart */
    bool           has_remote_hello;
    ppcp_id        remote_peer_id;
    ppcp_role      remote_role;
    ppcp_id        remote_profiles[PPCP_MAX_PROFILES];
    size_t         remote_profile_count;
    bool           has_remote_desc;
    ppcp_peer_desc remote;
    uint64_t       remote_generation;

    /* session */
    bool     has_session;
    ppcp_id  session_id;
    ppcp_id  timebase_ref;
    bool     armed;
    /* 8.3g / I16 — the parameters as they arrived, so a link loss can be shown
     * to change none of them. */
    bool                   has_session_params;
    ppcp_body_session_open session_params;

    /* this peer's own declaration, reduced to what I26 needs (5.12a, 7.1a) */
    struct { ppcp_id source_id; ppcp_id timebase_id; } own_sources[PPCP_PEER_MAX_OWN_SOURCES];
    size_t   own_source_count;
    ppcp_id  own_timebases[PPCP_PEER_MAX_OWN_TIMEBASES];
    size_t   own_timebase_count;

    /* L9 — clock synchronisation.  One estimator per local timebase (I21). */
    ppcp_sync_estimator sync[PPCP_PEER_MAX_SYNC];
    sync_sched          sched[PPCP_PEER_MAX_SYNC];
    size_t              sync_count;

    /* L9 — the relations this peer currently holds, filled from every
     * `relation_update` that arrives and from its own estimators.  Never
     * composed (I18); ppcp_relations_convert applies at most one. */
    ppcp_relation_set relations;

    /* L9 — liveness (7.4).  All of it moves in ppcp_peer_liveness_pump(),
     * because that is the one function in this engine that is given a clock
     * reading, and a library with no timer must not pretend otherwise. */
    ppcp_link_state link_state;
    uint32_t        missed_beats;
    bool            beat_since_pump;
    bool            has_last_beat;
    int64_t         last_beat_ns;
    int64_t         next_beat_ns;
    uint64_t        beat_seq;

    /* streams (5.1a: a Stream's identity is fixed for its lifetime) */
    ppcp_stream streams[PPCP_PEER_MAX_STREAMS];
    size_t      stream_count;

    /* 5.14f — the owner's view of where each Capture's payload has got to.
     * `confirmed` enters it only through `capture_committed` (8.4b). */
    ppcp_transfer_table transfers;

    /* transport */
    tx_queue tx[PPCP_PEER_MAX_CHANNELS];

    /* events */
    ppcp_msg        ev_msg[PPCP_PEER_EVENT_QUEUE];
    ppcp_event_kind ev_kind[PPCP_PEER_EVENT_QUEUE];
    ppcp_result     ev_status[PPCP_PEER_EVENT_QUEUE];
    uint8_t         ev_channel[PPCP_PEER_EVENT_QUEUE];
    size_t          ev_head, ev_count;
    uint64_t        ev_dropped;   /* F-L13-1: what the ring lost, readable */
    bool            feed_stalled; /* F-L13-1: feed stopped for want of event room */

    ppcp_arena decl_arena;
    ppcp_arena scratch_arena;
    uint8_t    decl_buf[PPCP_PEER_DECL_ARENA];
    uint8_t    scratch_buf[PPCP_PEER_SCRATCH_ARENA];
};

/* ================================================== CORE §10.1 versioning */

static bool version_parse(const ppcp_id *v, uint32_t *major, uint32_t *minor)
{
    uint8_t  i = 0;
    uint32_t m = 0, n = 0;
    bool     any = false;

    if (v == NULL || v->len == 0)
        return false;
    for (; i < v->len && v->v[i] != '.'; i++) {
        if (v->v[i] < '0' || v->v[i] > '9')
            return false;
        m = m * 10u + (uint32_t)(v->v[i] - '0');
        any = true;
    }
    if (!any || i >= v->len || v->v[i] != '.')
        return false;
    i++;
    any = false;
    for (; i < v->len; i++) {
        if (v->v[i] < '0' || v->v[i] > '9')
            return false;
        n = n * 10u + (uint32_t)(v->v[i] - '0');
        any = true;
    }
    if (!any)
        return false;
    *major = m;
    *minor = n;
    return true;
}

/* 10.1c — "the highest MINOR both support within a common MAJOR".
 *
 * A MINOR is additive by 10.1b, so a peer that lists 1.2 also speaks 1.0 and
 * 1.1 whether or not it enumerated them.  Taking the MINIMUM of the two ends'
 * highest MINOR within the highest common MAJOR therefore satisfies both
 * readings of the clause — "the highest it supports" and "the highest both
 * support" — without requiring either end to spell out its whole history.  A
 * literal set intersection would refuse 1.2-against-1.0, which is precisely
 * the old-application/new-host case 10.1e calls the permanent normal one. */
ppcp_result ppcp_version_select(const ppcp_id *offered, size_t offered_count,
                                const ppcp_id *supported, size_t supported_count,
                                ppcp_id *out)
{
    size_t   i;
    bool     found = false;
    uint32_t best_major = 0, off_minor = 0, sup_minor = 0;

    if (offered == NULL || supported == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    if (offered_count == 0 || supported_count == 0)
        return PPCP_ERR_INVALID;

    for (i = 0; i < offered_count; i++) {
        uint32_t omaj, omin;
        size_t   j;
        if (!version_parse(&offered[i], &omaj, &omin))
            continue;   /* 10.1d: an unparsable version is ignored, not fatal */
        for (j = 0; j < supported_count; j++) {
            uint32_t smaj, smin;
            if (!version_parse(&supported[j], &smaj, &smin))
                continue;
            if (smaj != omaj)
                continue;
            if (!found || omaj > best_major) {
                found = true;
                best_major = omaj;
                off_minor  = omin;
                sup_minor  = smin;
            } else if (omaj == best_major) {
                if (omin > off_minor) off_minor = omin;
                if (smin > sup_minor) sup_minor = smin;
            }
        }
    }
    if (!found)
        return PPCP_ERR_NOT_FOUND;   /* no common MAJOR — `unsupported_version` */
    {
        char     buf[32];
        uint32_t minor = (off_minor < sup_minor) ? off_minor : sup_minor;
        size_t   n = 0;
        uint32_t d;
        char     tmp[12];
        size_t   t = 0;

        d = best_major;
        do { tmp[t++] = (char)('0' + (d % 10u)); d /= 10u; } while (d != 0);
        while (t > 0) buf[n++] = tmp[--t];
        buf[n++] = '.';
        d = minor;
        do { tmp[t++] = (char)('0' + (d % 10u)); d /= 10u; } while (d != 0);
        while (t > 0) buf[n++] = tmp[--t];
        return ppcp_id_set(out, buf, n);
    }
}

bool ppcp_version_in_window(const ppcp_id *version, const ppcp_id *min_version)
{
    uint32_t vmaj, vmin, mmaj, mmin;
    if (!version_parse(version, &vmaj, &vmin))
        return false;
    if (min_version == NULL || min_version->len == 0)
        return true;
    if (!version_parse(min_version, &mmaj, &mmin))
        return true;
    if (vmaj != mmaj)
        return vmaj > mmaj;
    return vmin >= mmin;
}

/* ================================================= ENC §2.1 link binding */

void ppcp_link_binder_init(ppcp_link_binder *b)
{
    if (b != NULL)
        memset(b, 0, sizeof(*b));
}

size_t ppcp_link_binder_count(const ppcp_link_binder *b)
{
    size_t i, n = 0;
    if (b == NULL)
        return 0;
    for (i = 0; i < PPCP_MAX_LINKS; i++)
        if (b->links[i].in_use)
            n++;
    return n;
}

bool ppcp_link_binder_has_channel(const ppcp_link_binder *b, size_t link, uint8_t channel)
{
    if (b == NULL || link >= PPCP_MAX_LINKS || !b->links[link].in_use || channel >= 32)
        return false;
    return (b->links[link].channels & (1u << channel)) != 0;
}

bool ppcp_link_binder_is_ready(const ppcp_link_binder *b, size_t link)
{
    /* 2.1c: a link that has not bound channel 0 is not yet a link. */
    return ppcp_link_binder_has_channel(b, link, PPCP_CHANNEL_CONTROL);
}

ppcp_result ppcp_link_binder_discard(ppcp_link_binder *b, size_t link)
{
    if (b == NULL || link >= PPCP_MAX_LINKS)
        return PPCP_ERR_INVALID;
    memset(&b->links[link], 0, sizeof(b->links[link]));
    return PPCP_OK;
}

const uint8_t *ppcp_link_binder_id(const ppcp_link_binder *b, size_t link)
{
    if (b == NULL || link >= PPCP_MAX_LINKS || !b->links[link].in_use)
        return NULL;
    return b->links[link].link_id;
}

ppcp_result ppcp_link_binder_offer(ppcp_link_binder *b, const uint8_t *bytes, size_t len,
                                   size_t *out_consumed, size_t *out_link,
                                   uint8_t *out_channel)
{
    ppcp_frame_header hdr;
    const uint8_t    *payload;
    size_t            consumed = 0, i, slot = PPCP_MAX_LINKS;
    ppcp_msg          m;
    ppcp_result       rc;

    if (b == NULL || bytes == NULL || out_consumed == NULL || out_link == NULL)
        return PPCP_ERR_INVALID;

    rc = ppcp_frame_read(bytes, len, &hdr, &payload, &consumed);
    if (rc != PPCP_OK)
        return rc;
    /* ENC 2.1b — the stream's channel IS the header's.  A listener that has
     * just accepted a connection has no other source for it, which is why the
     * L6 signature asked for one it could not supply (plan §9, D). */
    rc = ppcp_channel_validate(hdr.channel);
    if (rc != PPCP_OK)
        return rc;

    memset(&m, 0, sizeof(m));
    rc = ppcp_msg_decode(payload, hdr.payload_len,
                         ppcp_cbor_limits_for_channel(hdr.channel), NULL, &m);
    if (rc != PPCP_OK)
        return rc;
    /* 2.1c, first refusal: the first frame on a stream is `link_bind` or the
     * listener closes the stream. */
    if (m.type != PPCP_MT_LINK_BIND)
        return PPCP_ERR_MALFORMED;
    /* 2.1c, second refusal: `channel` disagreeing with the header. */
    if (m.body.link_bind.channel != hdr.channel)
        return PPCP_ERR_MALFORMED;

    for (i = 0; i < PPCP_MAX_LINKS; i++) {
        if (b->links[i].in_use &&
            memcmp(b->links[i].link_id, m.body.link_bind.link_id,
                   PPCP_LINK_ID_BYTES) == 0) {
            slot = i;
            break;
        }
    }
    if (slot == PPCP_MAX_LINKS) {
        /* 2.1c: a `link_bind` naming an unknown `link_id` opens a new link. */
        for (i = 0; i < PPCP_MAX_LINKS; i++) {
            if (!b->links[i].in_use) {
                slot = i;
                break;
            }
        }
        if (slot == PPCP_MAX_LINKS)
            return PPCP_ERR_LIMIT;
        b->links[slot].in_use = true;
        memcpy(b->links[slot].link_id, m.body.link_bind.link_id, PPCP_LINK_ID_BYTES);
        b->links[slot].channels = 0;
    }
    /* 2.1c, third refusal: a `link_id` that already holds that channel.  A
     * second stream claiming channel 0 of a live link is either a bug or an
     * attempt to hijack one, and neither is served by accepting it. */
    if (ppcp_link_binder_has_channel(b, slot, hdr.channel))
        return PPCP_ERR_MALFORMED;
    b->links[slot].channels |= (uint32_t)(1u << hdr.channel);

    *out_consumed = consumed;
    *out_link     = slot;
    if (out_channel != NULL)
        *out_channel = hdr.channel;
    return PPCP_OK;
}

/* ======================================================== engine internals */

size_t ppcp_peer_sizeof(void) { return sizeof(struct ppcp_peer); }

static bool peer_has_profile(const ppcp_peer *p, const char *profile)
{
    size_t i;
    for (i = 0; i < p->profile_count; i++)
        if (ppcp_cbor_key_is(p->profiles[i].v, p->profiles[i].len, profile))
            return true;
    return false;
}

bool ppcp_peer_declares(const ppcp_peer *p, const char *profile)
{
    if (p == NULL || profile == NULL)
        return false;
    return peer_has_profile(p, profile);
}

ppcp_result ppcp_peer_new(void *storage, size_t storage_len, const ppcp_peer_config *cfg,
                          ppcp_peer **out)
{
    ppcp_peer  *p;
    size_t      i;
    ppcp_result rc;

    if (storage == NULL || cfg == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    if (storage_len < sizeof(*p))
        return PPCP_ERR_NOSPACE;
    if (cfg->peer_id == NULL || cfg->profiles == NULL || cfg->profile_count == 0)
        return PPCP_ERR_INVALID;
    if (cfg->profile_count > PPCP_MAX_PROFILES)
        return PPCP_ERR_LIMIT;

    p = (ppcp_peer *)storage;
    memset(p, 0, sizeof(*p));

    p->role = cfg->role;
    rc = ppcp_id_set_z(&p->peer_id, cfg->peer_id);
    if (rc != PPCP_OK)
        return rc;
    for (i = 0; i < cfg->profile_count; i++) {
        rc = ppcp_id_set_z(&p->profiles[i], cfg->profiles[i]);
        if (rc != PPCP_OK)
            return rc;
    }
    p->profile_count = cfg->profile_count;
    /* CORE §2.2: Core is mandatory, and a peer that has not declared it has no
     * profile set at all — every other profile requires it. */
    if (!peer_has_profile(p, PPCP_PROFILE_CORE))
        return PPCP_ERR_INVALID;

    if (cfg->versions != NULL && cfg->version_count > 0) {
        if (cfg->version_count > PPCP_MAX_VERSIONS)
            return PPCP_ERR_LIMIT;
        for (i = 0; i < cfg->version_count; i++) {
            rc = ppcp_id_set_z(&p->versions[i], cfg->versions[i]);
            if (rc != PPCP_OK)
                return rc;
        }
        p->version_count = cfg->version_count;
    } else {
        /* The one wire version this library speaks, without the "ppcp/"
         * prefix: MSG 3.1 puts "1.0" in `versions`, not the URN form. */
        rc = ppcp_id_set_z(&p->versions[0], "1.0");
        if (rc != PPCP_OK)
            return rc;
        p->version_count = 1;
    }
    /* 10.1e: the support window.  Defaulting it to the LAST entry of
     * `versions` is the honest reading — a peer that lists what it accepts has
     * already stated its window. */
    rc = ppcp_id_set_z(&p->min_version,
                       cfg->min_version != NULL ? cfg->min_version
                                                : p->versions[p->version_count - 1].v);
    if (rc != PPCP_OK)
        return rc;

    p->listener      = cfg->listener;
    p->ingest_policy = cfg->ingest_policy;
    p->ctx           = cfg->ctx;
    p->clock         = cfg->clock;
    p->health        = cfg->health;
    p->health_report = cfg->health_report;
    if (cfg->sync_timebase != NULL) {
        rc = ppcp_id_set_z(&p->sync_tb, cfg->sync_timebase);
        if (rc != PPCP_OK)
            return rc;
        p->has_sync_tb = true;
    }
    p->state         = PPCP_PEER_INIT;
    p->link_state    = PPCP_LINK_LIVE;
    ppcp_relations_init(&p->relations);
    ppcp_msg_seq_init(&p->seq);
    ppcp_transfer_table_init(&p->transfers);
    ppcp_arena_init(&p->decl_arena, p->decl_buf, sizeof(p->decl_buf));
    ppcp_arena_init(&p->scratch_arena, p->scratch_buf, sizeof(p->scratch_buf));

    *out = p;
    return PPCP_OK;
}

void ppcp_peer_free(ppcp_peer *p)
{
    /* Nothing was allocated.  Zeroing is not housekeeping: it turns a
     * use-after-close into a refusal instead of a stale state machine. */
    if (p != NULL)
        memset(p, 0, sizeof(*p));
}

/* --------------------------------------------------------------- queues */

/* Is `off` on a frame boundary?  Walked from the front, which is always a
 * frame start because that is the only place compaction is permitted. */
static bool tx_at_boundary(const tx_queue *q)
{
    size_t at = 0;
    while (at < q->off) {
        ppcp_frame_header h;
        if (ppcp_frame_header_parse(q->buf + at, &h) != PPCP_OK)
            return false;
        at += PPCP_FRAME_HEADER_BYTES + h.payload_len;
    }
    return at == q->off;
}

/* Reclaims the bytes already written to the wire, but only from a frame
 * boundary: half a frame at the front is a half-frame the engine must still be
 * able to recognise as one. */
static void tx_compact(tx_queue *q)
{
    if (q->off == 0)
        return;
    if (!tx_at_boundary(q))
        return;
    if (q->used > q->off)
        memmove(q->buf, q->buf + q->off, q->used - q->off);
    q->used -= q->off;
    q->off = 0;
}

static ppcp_result peer_encode_into_tx(ppcp_peer *p, uint8_t channel, const ppcp_msg *m)
{
    tx_queue   *q;
    size_t      written = 0;
    ppcp_result rc;

    if (channel >= PPCP_PEER_MAX_CHANNELS)
        return PPCP_ERR_INVALID;
    q  = &p->tx[channel];
    tx_compact(q);
    rc = ppcp_msg_encode(q->buf + q->used, sizeof(q->buf) - q->used, channel, m, &written);
    if (rc != PPCP_OK)
        return rc;
    q->used += written;
    return PPCP_OK;
}

/* The ONE place origination is decided.  Every public originator goes through
 * here, so C2 cannot be forgotten by a work package that adds a message. */
static ppcp_result peer_queue(ppcp_peer *p, uint8_t channel, ppcp_msg *m)
{
    ppcp_result rc;

    if (p == NULL || m == NULL)
        return PPCP_ERR_INVALID;
    if (p->state == PPCP_PEER_CLOSED)
        return PPCP_ERR_INVALID;
    rc = ppcp_msg_check_channel(m->type, channel);
    if (rc != PPCP_OK)
        return rc;
    /* C2 — a peer does not originate a message whose profile it has not
     * declared.  The catalogue answers, so the rule lives in one table and
     * one comparison rather than in forty-five call sites. */
    if (!ppcp_msg_profiles_confer(m->type, p->profiles, p->profile_count))
        return PPCP_ERR_INVALID;
    /* ENC 5c — `msg_id` is per sender, from 1, and is assigned here so two
     * frames cannot carry the same one. */
    m->env.msg_id = ppcp_msg_seq_next(&p->seq);
    if (p->has_session && !m->env.has_session_id && m->type != PPCP_MT_LINK_BIND &&
        m->type != PPCP_MT_HELLO && m->type != PPCP_MT_HELLO_ACCEPT)
        (void)ppcp_envelope_set_session_id(&m->env, p->session_id.v, p->session_id.len);
    return peer_encode_into_tx(p, channel, m);
}

ppcp_result ppcp_peer_send(ppcp_peer *p, uint8_t channel, ppcp_msg *m)
{
    return peer_queue(p, channel, m);
}

size_t ppcp_peer_pending(const ppcp_peer *p, uint8_t channel)
{
    if (p == NULL || channel >= PPCP_PEER_MAX_CHANNELS)
        return 0;
    return p->tx[channel].used - p->tx[channel].off;
}

ppcp_result ppcp_peer_drain_peek(const ppcp_peer *p, uint8_t channel,
                                 const uint8_t **out, size_t *out_len)
{
    const tx_queue *q;

    if (p == NULL || out == NULL || out_len == NULL || channel >= PPCP_PEER_MAX_CHANNELS)
        return PPCP_ERR_INVALID;
    q        = &p->tx[channel];
    *out     = q->buf + q->off;
    *out_len = q->used - q->off;
    return PPCP_OK;
}

ppcp_result ppcp_peer_drain_commit(ppcp_peer *p, uint8_t channel, size_t written)
{
    tx_queue *q;

    if (p == NULL || channel >= PPCP_PEER_MAX_CHANNELS)
        return PPCP_ERR_INVALID;
    q = &p->tx[channel];
    /* Exactly what was written, not a frame count.  A channel is an ordered
     * byte stream and half a frame on the wire is half a frame the counterpart
     * will reassemble; rounding to a boundary would re-send bytes that had
     * already left. */
    if (written > q->used - q->off)
        return PPCP_ERR_INVALID;
    q->off += written;
    tx_compact(q);
    return PPCP_OK;
}

bool ppcp_peer_drain_is_partial(const ppcp_peer *p, uint8_t channel)
{
    if (p == NULL || channel >= PPCP_PEER_MAX_CHANNELS)
        return false;
    return p->tx[channel].off != 0 && !tx_at_boundary(&p->tx[channel]);
}

ppcp_result ppcp_peer_drain(ppcp_peer *p, uint8_t channel, uint8_t *out, size_t cap,
                            size_t *out_len)
{
    tx_queue *q;
    size_t    take = 0;

    if (p == NULL || out == NULL || out_len == NULL || channel >= PPCP_PEER_MAX_CHANNELS)
        return PPCP_ERR_INVALID;
    q = &p->tx[channel];
    tx_compact(q);
    /* A short write left the head of the queue mid-frame.  Handing that back
     * as "whole frames" would be a lie; the caller is on the peek/commit path
     * and stays on it. */
    if (q->off != 0)
        return PPCP_ERR_INVALID;
    *out_len = 0;
    if (q->used == 0)
        return PPCP_OK;

    /* Whole frames only.  Handing a caller half a frame would make the
     * transport's job "reassemble what the library split", which is the job
     * the library exists to do. */
    while (take < q->used) {
        ppcp_frame_header hdr;
        size_t            frame;
        if (ppcp_frame_header_parse(q->buf + take, &hdr) != PPCP_OK)
            return PPCP_ERR_MALFORMED;
        frame = PPCP_FRAME_HEADER_BYTES + hdr.payload_len;
        if (take + frame > cap)
            break;
        take += frame;
    }
    if (take == 0)
        return PPCP_ERR_NOSPACE;

    memcpy(out, q->buf, take);
    if (take < q->used)
        memmove(q->buf, q->buf + take, q->used - take);
    q->used -= take;
    *out_len = take;
    return PPCP_OK;
}

/* --------------------------------------------------------------- events */

static ppcp_msg *peer_push_event_ch(ppcp_peer *p, ppcp_event_kind kind, ppcp_result status,
                                    uint8_t channel)
{
    size_t slot;
    if (p->ev_count == PPCP_PEER_EVENT_QUEUE) {
        /* The oldest event is dropped, not the newest: an embedding that is
         * not draining has already lost the earlier ones' timeliness, and
         * losing the most recent state change is worse than losing a stale
         * one.
         *
         * F-L13-1 — this used to be reachable from ppcp_peer_feed(), which
         * consumed every whole frame it was given, so one socket read carrying
         * a replayed bundle lost `capture_announce` and nothing said so.  The
         * feed now stops rather than overrun (see PEER_EVENT_HEADROOM), which
         * leaves only the events the engine raises itself from
         * ppcp_peer_tick().  Those can still overrun an embedding that never
         * drains, so the loss is COUNTED and readable rather than silent. */
        p->ev_dropped++;
        p->ev_head = (p->ev_head + 1) % PPCP_PEER_EVENT_QUEUE;
        p->ev_count--;
    }
    slot = (p->ev_head + p->ev_count) % PPCP_PEER_EVENT_QUEUE;
    p->ev_count++;
    p->ev_kind[slot]    = kind;
    p->ev_status[slot]  = status;
    p->ev_channel[slot] = channel;
    memset(&p->ev_msg[slot], 0, sizeof(p->ev_msg[slot]));
    return &p->ev_msg[slot];
}

/* The engine's own events — link loss, link restored — arrived on no channel.
 * Reported as channel 0 rather than as a lie about where they came from. */
static ppcp_msg *peer_push_event(ppcp_peer *p, ppcp_event_kind kind, ppcp_result status)
{
    return peer_push_event_ch(p, kind, status, PPCP_CHANNEL_CONTROL);
}

/* The most events one fed frame can raise: a `hello` raises PPCP_EVENT_HELLO
 * and PPCP_EVENT_CONNECTED, and nothing raises three.  ppcp_peer_feed() will
 * not start a frame it cannot report in full. */
#define PEER_EVENT_HEADROOM 2

static bool peer_event_room(const ppcp_peer *p)
{
    return p->ev_count + PEER_EVENT_HEADROOM <= PPCP_PEER_EVENT_QUEUE;
}

size_t ppcp_peer_events_pending(const ppcp_peer *p)
{
    return (p == NULL) ? 0 : p->ev_count;
}

size_t ppcp_peer_events_capacity(void)
{
    return (size_t)PPCP_PEER_EVENT_QUEUE;
}

bool ppcp_peer_feed_stalled(const ppcp_peer *p)
{
    return (p != NULL) && p->feed_stalled;
}

uint64_t ppcp_peer_events_dropped(const ppcp_peer *p)
{
    return (p == NULL) ? 0 : p->ev_dropped;
}

ppcp_result ppcp_peer_next_event(ppcp_peer *p, ppcp_event *out)
{
    if (p == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    if (p->ev_count == 0)
        return PPCP_ERR_NOT_FOUND;
    out->kind    = p->ev_kind[p->ev_head];
    out->status  = p->ev_status[p->ev_head];
    out->channel = p->ev_channel[p->ev_head];
    out->msg     = &p->ev_msg[p->ev_head];
    p->ev_head  = (p->ev_head + 1) % PPCP_PEER_EVENT_QUEUE;
    p->ev_count--;
    return PPCP_OK;
}

/* ------------------------------------------------------------ originators */

ppcp_result ppcp_peer_error(ppcp_peer *p, uint8_t channel, const char *code,
                            const char *message, bool has_in_reply_to, uint64_t in_reply_to)
{
    ppcp_msg    m;
    ppcp_result rc;
    size_t      n;

    if (p == NULL || code == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_msg_init(&m, PPCP_MT_ERROR, 1);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_id_set_z(&m.body.error.code, code);
    if (rc != PPCP_OK)
        return rc;
    if (message != NULL) {
        n = strlen(message);
        if (n > PPCP_ERROR_MESSAGE_MAX)
            n = PPCP_ERROR_MESSAGE_MAX;
        memcpy(m.body.error.message, message, n);
        m.body.error.message_len = n;
    }
    m.body.error.has_in_reply_to = has_in_reply_to;
    m.body.error.in_reply_to     = in_reply_to;
    /* 10.1f — `unsupported_version` MUST carry the sender's full supported
     * range.  The engine knows it, so a caller cannot forget it. */
    if (ppcp_cbor_key_is(code, strlen(code), PPCP_ERRCODE_UNSUPPORTED_VERSION)) {
        size_t i;
        for (i = 0; i < p->version_count; i++)
            m.body.error.detail_supported[i] = p->versions[i];
        m.body.error.detail_supported_count = p->version_count;
        m.body.error.has_detail_supported   = true;
    }
    rc = peer_queue(p, channel, &m);
    if (rc != PPCP_OK)
        return rc;
    /* MSG 10b: only a fatal code closes the transport, and it closes it AFTER
     * the frame is queued so the counterpart is told why. */
    if (ppcp_msg_error_is_fatal(code, strlen(code)))
        p->state = PPCP_PEER_CLOSED;
    return PPCP_OK;
}

ppcp_result ppcp_peer_set_link_id(ppcp_peer *p, const uint8_t link_id[PPCP_LINK_ID_BYTES])
{
    if (p == NULL || link_id == NULL)
        return PPCP_ERR_INVALID;
    /* 2.1a: the DIALLER mints it.  A listener that sent `link_bind` would be
     * answering a binding with a binding. */
    if (p->listener)
        return PPCP_ERR_INVALID;
    memcpy(p->link_id, link_id, PPCP_LINK_ID_BYTES);
    p->has_link_id = true;
    return PPCP_OK;
}

ppcp_result ppcp_peer_open_channel(ppcp_peer *p, uint8_t channel)
{
    ppcp_msg    m;
    ppcp_result rc;

    if (p == NULL || channel >= PPCP_PEER_MAX_CHANNELS)
        return PPCP_ERR_INVALID;
    if (p->listener || !p->has_link_id)
        return PPCP_ERR_INVALID;
    if ((p->opened_channels & (1u << channel)) != 0)
        return PPCP_ERR_INVALID;   /* 2.1c: one binding per channel per link */
    rc = ppcp_msg_init(&m, PPCP_MT_LINK_BIND, 1);
    if (rc != PPCP_OK)
        return rc;
    memcpy(m.body.link_bind.link_id, p->link_id, PPCP_LINK_ID_BYTES);
    m.body.link_bind.channel = channel;
    rc = peer_queue(p, channel, &m);
    if (rc != PPCP_OK)
        return rc;
    p->opened_channels |= (uint32_t)(1u << channel);
    return PPCP_OK;
}

ppcp_result ppcp_peer_hello(ppcp_peer *p)
{
    ppcp_msg    m;
    ppcp_result rc;
    size_t      i;

    if (p == NULL)
        return PPCP_ERR_INVALID;
    if (p->state != PPCP_PEER_INIT)
        return PPCP_ERR_INVALID;
    /* 2.1d: a dialler sends `hello` on channel 0 only after the `link_bind` on
     * that stream.  Emitting it here rather than refusing means the ordering
     * cannot be got wrong by an embedding that forgot; the frame is still one
     * frame and still first. */
    if (!p->listener && p->has_link_id && (p->opened_channels & 1u) == 0) {
        rc = ppcp_peer_open_channel(p, PPCP_CHANNEL_CONTROL);
        if (rc != PPCP_OK)
            return rc;
    }
    rc = ppcp_msg_init(&m, PPCP_MT_HELLO, 1);
    if (rc != PPCP_OK)
        return rc;
    for (i = 0; i < p->version_count; i++)
        m.body.hello.versions[i] = p->versions[i];
    m.body.hello.version_count = p->version_count;   /* 3.1b: at least one */
    m.body.hello.peer_id       = p->peer_id;
    m.body.hello.role          = p->role;
    for (i = 0; i < p->profile_count; i++)
        m.body.hello.profiles[i] = p->profiles[i];
    m.body.hello.profile_count = p->profile_count;
    rc = peer_queue(p, PPCP_CHANNEL_CONTROL, &m);
    if (rc != PPCP_OK)
        return rc;
    p->state = PPCP_PEER_HELLO_SENT;
    return PPCP_OK;
}

ppcp_result ppcp_peer_declare(ppcp_peer *p, const ppcp_peer_desc *self)
{
    ppcp_msg    m;
    ppcp_result rc;

    if (p == NULL || self == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_peer_desc_validate(self);
    if (rc != PPCP_OK)
        return rc;
    /* 5.2a: a Peer's `id` and `role` are its own.  A declaration that named a
     * different peer would make the roster meaningless. */
    if (!ppcp_id_equal(&self->id, &p->peer_id) || self->role != p->role)
        return PPCP_ERR_INVALID;
    rc = ppcp_msg_init(&m, PPCP_MT_DECLARE, 1);
    if (rc != PPCP_OK)
        return rc;
    /* 3.3a: a complete snapshot, and the generation increments HERE so a
     * caller cannot send two snapshots under one number. */
    m.body.declare.generation = ++p->generation;
    m.body.declare.peer       = *self;
    rc = peer_queue(p, PPCP_CHANNEL_CONTROL, &m);
    if (rc != PPCP_OK) {
        p->generation--;
        return rc;
    }
    if (p->state == PPCP_PEER_CONNECTED || p->state == PPCP_PEER_INIT)
        p->state = PPCP_PEER_DECLARED;

    /* 3.3a makes the declaration a complete snapshot, so the engine keeps the
     * part of it that a later rule needs to check: I26 requires a `candidate`
     * to name a Source THIS peer declared, on a Timebase it declared.  Only
     * the two ids are kept — holding the caller's arrays would make the
     * declaration's lifetime the engine's problem. */
    {
        size_t i;
        p->own_source_count   = 0;
        p->own_timebase_count = 0;
        for (i = 0; i < self->source_count && i < PPCP_PEER_MAX_OWN_SOURCES; i++) {
            p->own_sources[p->own_source_count].source_id   = self->sources[i].id;
            p->own_sources[p->own_source_count].timebase_id = self->sources[i].timebase_id;
            p->own_source_count++;
        }
        for (i = 0; i < self->timebase_count && i < PPCP_PEER_MAX_OWN_TIMEBASES; i++)
            p->own_timebases[p->own_timebase_count++] = self->timebases[i].id;
    }
    return PPCP_OK;
}

ppcp_result ppcp_peer_session_open(ppcp_peer *p, const ppcp_session *s)
{
    ppcp_msg    m;
    ppcp_result rc;

    if (p == NULL || s == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_session_validate(s);
    if (rc != PPCP_OK)
        return rc;
    /* 5.10e / 4.1d made behavioural: the arbitration parameters travel if and
     * only if the Session has a host, and only a host may open such a Session.
     * A capture peer opening the HOSTLESS form is CORE 4.1b — the frame it
     * records in its own bundle. */
    if (s->has_arbitration && p->role != PPCP_ROLE_HOST)
        return PPCP_ERR_INVALID;
    rc = ppcp_msg_init(&m, PPCP_MT_SESSION_OPEN, 1);
    if (rc != PPCP_OK)
        return rc;
    m.body.session_open.session_id   = s->id;
    m.body.session_open.timebase_ref = s->timebase_ref;
    m.body.session_open.epoch        = s->epoch;
    m.body.session_open.has_arbitration       = s->has_arbitration;
    m.body.session_open.coincidence_window_ns = s->coincidence_window_ns;
    m.body.session_open.issue_hold_ns         = s->issue_hold_ns;
    m.body.session_open.has_heartbeat_interval = s->has_heartbeat_interval;
    m.body.session_open.heartbeat_interval_ms  = s->heartbeat_interval_ms;
    rc = peer_queue(p, PPCP_CHANNEL_CONTROL, &m);
    if (rc != PPCP_OK)
        return rc;
    p->has_session  = true;
    p->session_id   = s->id;
    p->timebase_ref = s->timebase_ref;
    p->state        = PPCP_PEER_JOINED;
    /* F-H5-2 / F-D6-3.  The parameters were recorded on the RECEIVING path
     * only, so the peer that originated `session_open` could not read back its
     * own `timebase_ref`, `coincidence_window_ns` or `issue_hold_ns` (8.2b,
     * 8.2h) and kept a second, drifting copy; and ppcp_peer_zero_host() fell
     * through to the link state because `has_session_params` was false, so
     * CORE 4.1b's hostless case worked only because absent parameters read as
     * zero.  One Session, one record, whichever end opened it. */
    p->session_params     = m.body.session_open;
    p->has_session_params = true;
    return PPCP_OK;
}

ppcp_result ppcp_peer_session_state(ppcp_peer *p, ppcp_session_state state,
                                    ppcp_completeness completeness)
{
    ppcp_msg    m;
    ppcp_result rc;

    if (p == NULL || !p->has_session)
        return PPCP_ERR_INVALID;
    rc = ppcp_msg_init(&m, PPCP_MT_SESSION_STATE, 1);
    if (rc != PPCP_OK)
        return rc;
    m.body.session_state.session_id = p->session_id;
    m.body.session_state.state      = state;
    /* 4.4a / I10: asserted by the peer that owns the data.  The engine has no
     * way to compute it and offers none. */
    m.body.session_state.completeness = completeness;
    return peer_queue(p, PPCP_CHANNEL_CONTROL, &m);
}

ppcp_result ppcp_peer_session_close(ppcp_peer *p, const char *reason)
{
    ppcp_msg    m;
    ppcp_result rc;

    if (p == NULL || !p->has_session || reason == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_msg_init(&m, PPCP_MT_SESSION_CLOSE, 1);
    if (rc != PPCP_OK)
        return rc;
    m.body.session_close.session_id = p->session_id;
    rc = ppcp_id_set_z(&m.body.session_close.reason, reason);
    if (rc != PPCP_OK)
        return rc;
    rc = peer_queue(p, PPCP_CHANNEL_CONTROL, &m);
    if (rc != PPCP_OK)
        return rc;
    p->has_session = false;
    p->armed       = false;
    p->state       = PPCP_PEER_DECLARED;
    return PPCP_OK;
}

ppcp_result ppcp_peer_context_change(ppcp_peer *p, const ppcp_context_change *c)
{
    ppcp_msg    m;
    ppcp_result rc;

    if (p == NULL || c == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_context_change_validate(c);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_msg_init(&m, PPCP_MT_CONTEXT_CHANGE, 1);
    if (rc != PPCP_OK)
        return rc;
    m.body.context_change.context = *c;
    return peer_queue(p, PPCP_CHANNEL_CONTROL, &m);
}

static ppcp_result peer_stream_add(ppcp_peer *p, const ppcp_stream *s)
{
    size_t i;
    for (i = 0; i < p->stream_count; i++)
        if (ppcp_id_equal(&p->streams[i].id, &s->id))
            return PPCP_ERR_INVALID;   /* 5.1a: a Stream's identity is fixed */
    if (p->stream_count == PPCP_PEER_MAX_STREAMS)
        return PPCP_ERR_LIMIT;
    p->streams[p->stream_count++] = *s;
    return PPCP_OK;
}

static void peer_stream_remove(ppcp_peer *p, const ppcp_id *id)
{
    size_t i;
    for (i = 0; i < p->stream_count; i++) {
        if (ppcp_id_equal(&p->streams[i].id, id)) {
            if (i + 1 < p->stream_count)
                memmove(&p->streams[i], &p->streams[i + 1],
                        (p->stream_count - i - 1) * sizeof(p->streams[0]));
            p->stream_count--;
            return;
        }
    }
}

ppcp_result ppcp_peer_stream_open(ppcp_peer *p, const ppcp_stream *s)
{
    ppcp_msg    m;
    ppcp_result rc;

    if (p == NULL || s == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_stream_validate(s);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_msg_init(&m, PPCP_MT_STREAM_OPEN, 1);
    if (rc != PPCP_OK)
        return rc;
    m.body.stream_open.stream = *s;
    rc = peer_queue(p, PPCP_CHANNEL_CONTROL, &m);
    if (rc != PPCP_OK)
        return rc;
    return peer_stream_add(p, s);
}

ppcp_result ppcp_peer_stream_close(ppcp_peer *p, const char *stream_id,
                                   const ppcp_instant *closed_at, const char *reason)
{
    ppcp_msg    m;
    ppcp_result rc;

    if (p == NULL || stream_id == NULL || reason == NULL)
        return PPCP_ERR_INVALID;
    if (closed_at != NULL) {
        rc = ppcp_instant_validate(closed_at);
        if (rc != PPCP_OK)
            return rc;
    }
    rc = ppcp_msg_init(&m, PPCP_MT_STREAM_CLOSE, 1);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_id_set_z(&m.body.stream_close.stream_id, stream_id);
    if (rc != PPCP_OK)
        return rc;
    /* F-H4-2: a CONSUMER closing a Stream it does not own has no reading of
     * the owner's timebase, and 5.1d says it may close all the same.  NULL is
     * therefore accepted and the field omitted, rather than the library
     * inventing an instant in a clock it cannot read (8.1e's principle, one
     * section over). */
    if (closed_at != NULL) {
        m.body.stream_close.has_closed_at = true;
        m.body.stream_close.closed_at     = *closed_at;
    }
    rc = ppcp_id_set_z(&m.body.stream_close.reason, reason);
    if (rc != PPCP_OK)
        return rc;
    rc = peer_queue(p, PPCP_CHANNEL_CONTROL, &m);
    if (rc != PPCP_OK)
        return rc;
    peer_stream_remove(p, &m.body.stream_close.stream_id);
    return PPCP_OK;
}

static ppcp_result peer_stream_ids(ppcp_peer *p, ppcp_msg_type t, const ppcp_id *ids,
                                   size_t count)
{
    ppcp_msg    m;
    ppcp_result rc;
    size_t      i;

    if (count > PPCP_MAX_STREAM_IDS)
        return PPCP_ERR_LIMIT;
    rc = ppcp_msg_init(&m, t, 1);
    if (rc != PPCP_OK)
        return rc;
    for (i = 0; i < count; i++)
        m.body.arm.stream_ids[i] = ids[i];
    /* MSG 5.2: an EMPTY list means every open capture Stream, so zero is a
     * meaning and not a missing argument. */
    m.body.arm.stream_id_count = count;
    return peer_queue(p, PPCP_CHANNEL_CONTROL, &m);
}

ppcp_result ppcp_peer_arm(ppcp_peer *p, const ppcp_id *stream_ids, size_t count)
{
    ppcp_result rc;
    if (p == NULL || (count > 0 && stream_ids == NULL))
        return PPCP_ERR_INVALID;
    /* 7.3a: capture start and stop are host-controlled.  A capture peer does
     * not arm itself, and a hostless one records no `arm` at all (7.3b). */
    if (p->role != PPCP_ROLE_HOST)
        return PPCP_ERR_INVALID;
    rc = peer_stream_ids(p, PPCP_MT_ARM, stream_ids, count);
    if (rc == PPCP_OK)
        p->armed = true;
    return rc;
}

ppcp_result ppcp_peer_disarm(ppcp_peer *p, const ppcp_id *stream_ids, size_t count)
{
    ppcp_result rc;
    if (p == NULL || (count > 0 && stream_ids == NULL))
        return PPCP_ERR_INVALID;
    if (p->role != PPCP_ROLE_HOST)
        return PPCP_ERR_INVALID;
    rc = peer_stream_ids(p, PPCP_MT_DISARM, stream_ids, count);
    if (rc == PPCP_OK)
        p->armed = false;
    return rc;
}

ppcp_result ppcp_peer_readiness(ppcp_peer *p, const ppcp_readiness *r,
                                const ppcp_id *stream_ids, size_t count)
{
    ppcp_msg    m;
    ppcp_result rc;
    size_t      i;

    if (p == NULL || r == NULL || (count > 0 && stream_ids == NULL))
        return PPCP_ERR_INVALID;
    if (count > PPCP_MAX_STREAM_IDS)
        return PPCP_ERR_LIMIT;
    rc = ppcp_readiness_validate(r);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_msg_init(&m, PPCP_MT_READINESS, 1);
    if (rc != PPCP_OK)
        return rc;
    m.body.readiness.readiness = *r;
    for (i = 0; i < count; i++)
        m.body.readiness.stream_ids[i] = stream_ids[i];
    m.body.readiness.stream_id_count = count;
    return peer_queue(p, PPCP_CHANNEL_CONTROL, &m);
}

ppcp_result ppcp_peer_interruption(ppcp_peer *p, const char *kind,
                                   const ppcp_interval *interval, bool recovered,
                                   const ppcp_id *stream_ids, size_t count)
{
    ppcp_msg    m;
    ppcp_result rc;
    size_t      i;

    if (p == NULL || kind == NULL || interval == NULL || (count > 0 && stream_ids == NULL))
        return PPCP_ERR_INVALID;
    if (count > PPCP_MAX_STREAM_IDS)
        return PPCP_ERR_LIMIT;
    rc = ppcp_interval_validate(interval);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_msg_init(&m, PPCP_MT_INTERRUPTION, 1);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_id_set_z(&m.body.interruption.kind, kind);
    if (rc != PPCP_OK)
        return rc;
    m.body.interruption.interval  = *interval;
    m.body.interruption.recovered = recovered;
    for (i = 0; i < count; i++)
        m.body.interruption.stream_ids[i] = stream_ids[i];
    m.body.interruption.stream_id_count = count;
    return peer_queue(p, PPCP_CHANNEL_CONTROL, &m);
}

/* ------------------------------------------ MSG §8 — Captures and payload */

ppcp_result ppcp_peer_capture_announce(ppcp_peer *p, const ppcp_capture *c, bool is_preview,
                                       const char *thumbnail_format,
                                       const uint8_t *thumbnail, size_t thumbnail_len)
{
    ppcp_msg    m;
    ppcp_result rc;

    if (p == NULL || c == NULL)
        return PPCP_ERR_INVALID;

    /* F-D4-1 — the Stream this Capture names is one this peer OPENED, and the
     * engine is holding it.  So the rules that are stated over the pair are
     * checked here, at origination, rather than left to a receiver to notice:
     * 5.14d's `{stream: true}` only on a `continuous` Stream (CT-I27's second
     * assertion), I11's gaps only on a `continuous` Stream, the interval in the
     * Stream's own timebase, and 5.11j's preview rule.  L7 refused only the
     * preview case; the other three were unenforced on the way out although
     * the same table answered all four (plan §9, D, 22 August 2026).
     *
     * A Stream the engine has not seen opened is NOT an error: 8.4b lets a
     * peer announce an `absent` Capture for an interval it no longer holds,
     * and a `capture_request` may name a Stream that was closed. */
    {
        const ppcp_stream *st = NULL;
        size_t             i;
        for (i = 0; i < p->stream_count; i++) {
            if (ppcp_id_equal(&p->streams[i].id, &c->stream_id)) {
                st = &p->streams[i];
                break;
            }
        }
        if (st != NULL) {
            rc = ppcp_capture_validate_in_stream(c, st);
            if (rc != PPCP_OK)
                return rc;
            /* And the caller's belief about the Stream's kind must match the
             * Stream: `is_preview` is a parameter only because a Capture does
             * not carry the kind, not because the caller gets to choose. */
            if (is_preview != ppcp_stream_is_preview(st))
                return PPCP_ERR_INVALID;
        }
    }

    /* 8.1i / 5.11j is refused here rather than noticed later, and the table is
     * updated before the frame is queued so a refusal costs nothing. */
    rc = ppcp_transfer_observe_announce(&p->transfers, c, is_preview);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_msg_init(&m, PPCP_MT_CAPTURE_ANNOUNCE, 1);
    if (rc != PPCP_OK)
        return rc;
    m.body.capture_announce.capture = *c;
    if (thumbnail != NULL && thumbnail_len > 0) {
        if (thumbnail_len > PPCP_THUMBNAIL_MAX)
            return PPCP_ERR_LIMIT;                   /* 8.1d */
        if (thumbnail_format == NULL)
            return PPCP_ERR_INVALID;
        rc = ppcp_id_set_z(&m.body.capture_announce.thumbnail_format, thumbnail_format);
        if (rc != PPCP_OK)
            return rc;
        m.body.capture_announce.has_thumbnail = true;
        m.body.capture_announce.thumbnail     = thumbnail;
        m.body.capture_announce.thumbnail_len = thumbnail_len;
    }
    /* 8.1a: on CONTROL, and it does not wait for payload. */
    return peer_queue(p, PPCP_CHANNEL_CONTROL, &m);
}

ppcp_result ppcp_peer_capture_update(ppcp_peer *p, const ppcp_body_capture_update *u)
{
    ppcp_msg    m;
    ppcp_result rc;

    if (p == NULL || u == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_msg_init(&m, PPCP_MT_CAPTURE_UPDATE, 1);
    if (rc != PPCP_OK)
        return rc;
    m.body.capture_update = *u;
    /* 8.4b: an owner does not set `confirmed` on its own authority, and
     * announcing that it did would be the same act on the wire. */
    if (u->has_transfer && u->transfer == PPCP_TRANSFER_CONFIRMED)
        return PPCP_ERR_INVALID;
    if (u->has_transfer)
        (void)ppcp_transfer_set(&p->transfers, &u->capture_id, u->transfer);
    return peer_queue(p, PPCP_CHANNEL_CONTROL, &m);
}

ppcp_result ppcp_peer_capture_committed(ppcp_peer *p, const char *capture_id,
                                        const ppcp_digest *digest)
{
    ppcp_msg    m;
    ppcp_result rc;

    if (p == NULL || capture_id == NULL || digest == NULL || !digest->present)
        return PPCP_ERR_INVALID;
    rc = ppcp_msg_init(&m, PPCP_MT_CAPTURE_COMMITTED, 1);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_id_set_z(&m.body.capture_committed.capture_id, capture_id);
    if (rc != PPCP_OK)
        return rc;
    m.body.capture_committed.digest = *digest;
    /* 8.4d: on CONTROL — it is what releases storage at the other end and must
     * not queue behind the next clip. */
    return peer_queue(p, PPCP_CHANNEL_CONTROL, &m);
}

ppcp_result ppcp_peer_payload_begin(ppcp_peer *p, uint8_t channel, const char *capture_id,
                                    uint64_t bytes, const ppcp_digest *digest,
                                    uint32_t chunk_bytes,
                                    const ppcp_achieved_frames *frames)
{
    ppcp_msg    m;
    ppcp_result rc;

    if (p == NULL || capture_id == NULL || digest == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_msg_init(&m, PPCP_MT_PAYLOAD_BEGIN, 1);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_id_set_z(&m.body.payload_begin.capture_id, capture_id);
    if (rc != PPCP_OK)
        return rc;
    m.body.payload_begin.bytes       = bytes;
    m.body.payload_begin.digest      = *digest;   /* 8.1e: present by here */
    m.body.payload_begin.chunk_bytes = chunk_bytes;
    /* 8.3g / I30 / ENC 6a1: the per-frame series belong on THIS channel, with
     * the frames they describe, and never on control. */
    if (frames != NULL) {
        rc = ppcp_achieved_frames_validate(frames);
        if (rc != PPCP_OK)
            return rc;
        m.body.payload_begin.has_achieved_frames = true;
        m.body.payload_begin.achieved_frames     = *frames;
    }
    rc = peer_queue(p, channel, &m);
    if (rc == PPCP_OK)
        (void)ppcp_transfer_set(&p->transfers, &m.body.payload_begin.capture_id,
                                PPCP_TRANSFER_IN_FLIGHT);
    return rc;
}

ppcp_result ppcp_peer_payload_chunk(ppcp_peer *p, uint8_t channel, const char *capture_id,
                                    uint32_t index, uint32_t chunk_bytes,
                                    const uint8_t *data, size_t len)
{
    ppcp_msg    m;
    ppcp_result rc;

    if (p == NULL || capture_id == NULL || data == NULL || len == 0)
        return PPCP_ERR_INVALID;
    if (chunk_bytes == 0 || len > chunk_bytes)
        return PPCP_ERR_INVALID;
    rc = ppcp_msg_init(&m, PPCP_MT_PAYLOAD_CHUNK, 1);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_id_set_z(&m.body.payload_chunk.capture_id, capture_id);
    if (rc != PPCP_OK)
        return rc;
    m.body.payload_chunk.index = index;
    /* ENC 6b and 6c computed here, from the index and the bytes, so a sender
     * cannot state an offset or a digest that disagrees with what it sent. */
    m.body.payload_chunk.offset   = (uint64_t)index * (uint64_t)chunk_bytes;
    m.body.payload_chunk.data     = data;
    m.body.payload_chunk.data_len = len;
    rc = ppcp_payload_digest(data, len, &m.body.payload_chunk.digest);
    if (rc != PPCP_OK)
        return rc;
    return peer_queue(p, channel, &m);
}

ppcp_result ppcp_peer_payload_ack(ppcp_peer *p, uint8_t channel, const char *capture_id,
                                  uint32_t index)
{
    ppcp_msg    m;
    ppcp_result rc;

    if (p == NULL || capture_id == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_msg_init(&m, PPCP_MT_PAYLOAD_ACK, 1);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_id_set_z(&m.body.payload_ack.capture_id, capture_id);
    if (rc != PPCP_OK)
        return rc;
    m.body.payload_ack.index = index;
    return peer_queue(p, channel, &m);
}

ppcp_result ppcp_peer_payload_end(ppcp_peer *p, uint8_t channel, const char *capture_id,
                                  const ppcp_digest *digest)
{
    ppcp_msg    m;
    ppcp_result rc;

    if (p == NULL || capture_id == NULL || digest == NULL || !digest->present)
        return PPCP_ERR_INVALID;
    rc = ppcp_msg_init(&m, PPCP_MT_PAYLOAD_END, 1);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_id_set_z(&m.body.payload_end.capture_id, capture_id);
    if (rc != PPCP_OK)
        return rc;
    m.body.payload_end.digest = *digest;
    rc = peer_queue(p, channel, &m);
    if (rc == PPCP_OK)
        (void)ppcp_transfer_set(&p->transfers, &m.body.payload_end.capture_id,
                                PPCP_TRANSFER_PRESENT);
    return rc;
}

ppcp_result ppcp_peer_payload_abort(ppcp_peer *p, uint8_t channel, const char *capture_id,
                                    const char *reason)
{
    ppcp_msg    m;
    ppcp_result rc;

    if (p == NULL || capture_id == NULL || reason == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_msg_init(&m, PPCP_MT_PAYLOAD_ABORT, 1);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_id_set_z(&m.body.payload_abort.capture_id, capture_id);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_id_set_z(&m.body.payload_abort.reason, reason);
    if (rc != PPCP_OK)
        return rc;
    return peer_queue(p, channel, &m);
}

ppcp_result ppcp_peer_payload_resume(ppcp_peer *p, uint8_t channel, const char *capture_id,
                                     uint32_t from_index)
{
    ppcp_msg    m;
    ppcp_result rc;

    if (p == NULL || capture_id == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_msg_init(&m, PPCP_MT_PAYLOAD_RESUME, 1);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_id_set_z(&m.body.payload_resume.capture_id, capture_id);
    if (rc != PPCP_OK)
        return rc;
    m.body.payload_resume.from_index = from_index;
    return peer_queue(p, channel, &m);
}

const ppcp_transfer_table *ppcp_peer_transfers(const ppcp_peer *p)
{
    return (p == NULL) ? NULL : &p->transfers;
}

/* ================================================ L9 — clock synchronisation
 *
 * I21 and 6.1d: one estimator, one probe sequence, one directly-declared
 * relation per LOCAL timebase.  Nothing here composes and nothing here derives
 * a relation that was not measured (I18, 5.4c).
 */

static bool id_is(const ppcp_id *id, const char *s, size_t len)
{
    return id != NULL && id->len == len && memcmp(id->v, s, len) == 0;
}

/* The first sequence on this LOCAL timebase, whatever it is probing.  This is
 * the single-remote case and it is what ppcp_peer_sync_probe() and the older
 * accessors mean. */
static size_t peer_sync_find(const ppcp_peer *p, const char *tb, size_t tb_len)
{
    size_t i;
    for (i = 0; i < p->sync_count; i++) {
        if (id_is(ppcp_sync_estimator_local_tb(&p->sync[i]), tb, tb_len))
            return i;
    }
    return p->sync_count;
}

/* F-H5-1 — the sequence for the PAIR.  `remote` NULL means "the one with no
 * remote yet", which is what a registration that has not seen a reply is. */
static size_t peer_sync_find_pair(const ppcp_peer *p, const char *local, size_t local_len,
                                  const char *remote, size_t remote_len)
{
    size_t i;
    for (i = 0; i < p->sync_count; i++) {
        const ppcp_id *r;
        if (!id_is(ppcp_sync_estimator_local_tb(&p->sync[i]), local, local_len))
            continue;
        r = ppcp_sync_estimator_remote_tb(&p->sync[i]);
        if (remote == NULL) {
            if (r == NULL || r->len == 0)
                return i;
        } else if (id_is(r, remote, remote_len)) {
            return i;
        }
    }
    return p->sync_count;
}

/* Which sequence a `sync_reply` belongs to.  6.1a echoes `t1` with its `tb`,
 * which names the local half; `t2.tb` names the remote half (6.1b).  An exact
 * pair wins; a sequence that has not yet learned its remote timebase takes it
 * otherwise, which is how the FIRST reply of a plain 6.1d exchange lands. */
static size_t peer_sync_find_for_reply(const ppcp_peer *p, const ppcp_body_sync_reply *b)
{
    size_t i = peer_sync_find_pair(p, b->t1.tb.v, b->t1.tb.len,
                                   b->t2.tb.v, b->t2.tb.len);
    if (i != p->sync_count)
        return i;
    return peer_sync_find_pair(p, b->t1.tb.v, b->t1.tb.len, NULL, 0);
}

static ppcp_result peer_sync_add(ppcp_peer *p, const char *local_tb,
                                 const char *remote_tb, bool probe_remote)
{
    ppcp_sync_estimator *e = NULL;
    ppcp_result          rc;
    size_t               n;

    if (p == NULL || local_tb == NULL)
        return PPCP_ERR_INVALID;
    if (probe_remote && (remote_tb == NULL || *remote_tb == '\0'))
        return PPCP_ERR_INVALID;   /* naming nothing is not naming a clock */
    n = strlen(local_tb);
    if (n == 0)
        return PPCP_ERR_INVALID;
    /* One sequence per PAIR (F-H5-1).  Registering the same local timebase
     * twice against the same remote — or twice with no remote named — is still
     * refused; registering it against two DIFFERENT remote clocks is the whole
     * point, and used to be impossible. */
    if (peer_sync_find_pair(p, local_tb, n, remote_tb,
                            (remote_tb == NULL) ? 0 : strlen(remote_tb)) != p->sync_count)
        return PPCP_ERR_INVALID;
    if (p->sync_count == PPCP_PEER_MAX_SYNC)
        return PPCP_ERR_LIMIT;

    rc = ppcp_sync_estimator_new(&p->sync[p->sync_count], sizeof(p->sync[0]),
                                 local_tb, remote_tb, &e);
    if (rc != PPCP_OK)
        return rc;
    memset(&p->sched[p->sync_count], 0, sizeof(p->sched[0]));
    p->sched[p->sync_count].probe_remote = probe_remote;
    /* Due immediately, so a maintenance probe goes out on the first pump even
     * if the embedding never triggers a burst. */
    p->sched[p->sync_count].next_due_ns = INT64_MIN;
    p->sync_count++;
    return PPCP_OK;
}

ppcp_result ppcp_peer_sync_add_timebase(ppcp_peer *p, const char *local_tb,
                                        const char *remote_tb)
{
    return peer_sync_add(p, local_tb, remote_tb, false);
}

ppcp_result ppcp_peer_sync_add_target(ppcp_peer *p, const char *local_tb,
                                      const char *remote_tb)
{
    return peer_sync_add(p, local_tb, remote_tb, true);
}

size_t ppcp_peer_sync_count(const ppcp_peer *p)
{
    return (p == NULL) ? 0 : p->sync_count;
}

const ppcp_sync_estimator *ppcp_peer_sync_estimator_at(const ppcp_peer *p, size_t index)
{
    if (p == NULL || index >= p->sync_count)
        return NULL;
    return &p->sync[index];
}

const ppcp_sync_estimator *ppcp_peer_sync_estimator_for(const ppcp_peer *p,
                                                        const char *local_tb)
{
    size_t i;
    if (p == NULL || local_tb == NULL)
        return NULL;
    i = peer_sync_find(p, local_tb, strlen(local_tb));
    return (i == p->sync_count) ? NULL : &p->sync[i];
}

const ppcp_sync_estimator *ppcp_peer_sync_estimator_for_pair(const ppcp_peer *p,
                                                             const char *local_tb,
                                                             const char *remote_tb)
{
    size_t i;
    if (p == NULL || local_tb == NULL)
        return NULL;
    i = peer_sync_find_pair(p, local_tb, strlen(local_tb), remote_tb,
                            (remote_tb == NULL) ? 0 : strlen(remote_tb));
    return (i == p->sync_count) ? NULL : &p->sync[i];
}

ppcp_relation_set *ppcp_peer_relations(ppcp_peer *p)
{
    return (p == NULL) ? NULL : &p->relations;
}

static void peer_sync_remember(sync_sched *sc, uint64_t seq, int64_t t1_ns)
{
    size_t n = sizeof(sc->out) / sizeof(sc->out[0]);
    sc->out[sc->out_next].seq    = seq;
    sc->out[sc->out_next].t1_ns  = t1_ns;
    sc->out[sc->out_next].live   = true;
    sc->out_next = (sc->out_next + 1u) % n;
}

static ppcp_result peer_sync_probe_at(ppcp_peer *p, size_t i)
{
    const ppcp_id *local = ppcp_sync_estimator_local_tb(&p->sync[i]);
    ppcp_instant   t1;
    ppcp_msg       m;
    ppcp_result    rc;

    if (local == NULL || local->len == 0)
        return PPCP_ERR_INVALID;
    /* 6.3a: the prober's own clock, in the timebase it is probing for.  There
     * is no "now" in this library that does not name a timebase (I1). */
    rc = ppcp_clock_read(&p->clock, local->v, &t1);
    if (rc != PPCP_OK)
        return rc;

    rc = ppcp_msg_init(&m, PPCP_MT_SYNC_PROBE, 1);
    if (rc != PPCP_OK)
        return rc;
    m.body.sync_probe.probe_seq = ++p->sched[i].probe_seq;
    /* 6.1d — `timebase_id` is this peer's own local clock, one sequence per
     * clock.  Erratum E2 (F-H5-1): a peer that wants a NAMED clock of the
     * responder's names it here instead, and a responder that declares it
     * answers on it.  Which of the two this sequence is was fixed at
     * registration, so the field never changes meaning mid-sequence. */
    if (p->sched[i].probe_remote) {
        const ppcp_id *remote = ppcp_sync_estimator_remote_tb(&p->sync[i]);
        if (remote == NULL || remote->len == 0)
            return PPCP_ERR_INVALID;
        m.body.sync_probe.timebase_id = *remote;
    } else {
        m.body.sync_probe.timebase_id = *local;
    }
    m.body.sync_probe.t1 = t1;
    rc = peer_queue(p, PPCP_CHANNEL_CONTROL, &m);
    if (rc != PPCP_OK)
        return rc;
    peer_sync_remember(&p->sched[i], m.body.sync_probe.probe_seq, t1.ns);
    return PPCP_OK;
}

ppcp_result ppcp_peer_sync_probe(ppcp_peer *p, const char *local_tb)
{
    size_t i;
    if (p == NULL || local_tb == NULL)
        return PPCP_ERR_INVALID;
    i = peer_sync_find(p, local_tb, strlen(local_tb));
    if (i == p->sync_count)
        return PPCP_ERR_NOT_FOUND;
    return peer_sync_probe_at(p, i);
}

ppcp_result ppcp_peer_sync_probe_to(ppcp_peer *p, const char *local_tb,
                                    const char *remote_tb)
{
    size_t i;
    if (p == NULL || local_tb == NULL)
        return PPCP_ERR_INVALID;
    i = peer_sync_find_pair(p, local_tb, strlen(local_tb), remote_tb,
                            (remote_tb == NULL) ? 0 : strlen(remote_tb));
    if (i == p->sync_count)
        return PPCP_ERR_NOT_FOUND;
    return peer_sync_probe_at(p, i);
}

ppcp_result ppcp_peer_sync_observe(ppcp_peer *p, const char *local_tb, int64_t t1,
                                   int64_t t2, int64_t t3, int64_t t4)
{
    size_t i;
    if (p == NULL || local_tb == NULL)
        return PPCP_ERR_INVALID;
    i = peer_sync_find(p, local_tb, strlen(local_tb));
    if (i == p->sync_count)
        return PPCP_ERR_NOT_FOUND;
    return ppcp_sync_estimator_observe(&p->sync[i], t1, t2, t3, t4);
}

ppcp_result ppcp_peer_sync_observe_to(ppcp_peer *p, const char *local_tb,
                                      const char *remote_tb, int64_t t1, int64_t t2,
                                      int64_t t3, int64_t t4)
{
    size_t i;
    if (p == NULL || local_tb == NULL)
        return PPCP_ERR_INVALID;
    i = peer_sync_find_pair(p, local_tb, strlen(local_tb), remote_tb,
                            (remote_tb == NULL) ? 0 : strlen(remote_tb));
    if (i == p->sync_count)
        return PPCP_ERR_NOT_FOUND;
    return ppcp_sync_estimator_observe(&p->sync[i], t1, t2, t3, t4);
}

ppcp_result ppcp_peer_sync_trigger(ppcp_peer *p, ppcp_sync_trigger why)
{
    size_t i;
    if (p == NULL)
        return PPCP_ERR_INVALID;
    for (i = 0; i < p->sync_count; i++) {
        p->sched[i].burst_left  = PPCP_SYNC_BURST;
        p->sched[i].next_due_ns = INT64_MIN;
        /* 6.3c — a network change or a thermal event makes the FIT stale, not
         * merely the offset: oscillator frequency shifts with temperature, so
         * the rate estimate is what has gone wrong.  On connect there is
         * nothing yet to discard. */
        if (why != PPCP_SYNC_ON_CONNECT)
            ppcp_sync_estimator_restart(&p->sync[i]);
    }
    return PPCP_OK;
}

ppcp_result ppcp_peer_sync_pump(ppcp_peer *p, int64_t now_ns, size_t *out_probes)
{
    size_t i, sent = 0;

    if (p == NULL)
        return PPCP_ERR_INVALID;
    if (out_probes != NULL)
        *out_probes = 0;

    for (i = 0; i < p->sync_count; i++) {
        const ppcp_id *tb = ppcp_sync_estimator_local_tb(&p->sync[i]);
        int64_t        gap;
        if (tb == NULL || now_ns < p->sched[i].next_due_ns)
            continue;
        /* By INDEX, not by local timebase name: two sequences can share a
         * local clock and probe two different remote ones (F-H5-1). */
        if (peer_sync_probe_at(p, i) != PPCP_OK)
            continue;
        sent++;
        if (p->sched[i].burst_left > 0) {
            p->sched[i].burst_left--;
            gap = (int64_t)PPCP_SYNC_BURST_GAP_MS * 1000000;
        } else {
            /* 6.3g — maintenance, and 6.3d: this number is the sync cadence
             * and has nothing to do with `heartbeat_interval_ms`. */
            gap = (int64_t)PPCP_SYNC_MAINTENANCE_MS * 1000000;
        }
        p->sched[i].next_due_ns = now_ns + gap;
    }
    if (out_probes != NULL)
        *out_probes = sent;
    return PPCP_OK;
}

ppcp_result ppcp_peer_relation_update(ppcp_peer *p, const ppcp_timebase_relation *rels,
                                      size_t count)
{
    ppcp_msg    m;
    ppcp_result rc;
    size_t      i;

    if (p == NULL || rels == NULL || count == 0)
        return PPCP_ERR_INVALID;
    if (count > PPCP_MAX_RELATIONS)
        return PPCP_ERR_LIMIT;
    for (i = 0; i < count; i++) {
        /* I3 on the way out: a relation missing either sigma never reaches a
         * wire from here. */
        rc = ppcp_relation_validate(&rels[i]);
        if (rc != PPCP_OK)
            return PPCP_ERR_INVALID;
    }
    rc = ppcp_msg_init(&m, PPCP_MT_RELATION_UPDATE, 1);
    if (rc != PPCP_OK)
        return rc;
    for (i = 0; i < count; i++) {
        m.body.relation_update.relations[i] = rels[i];
        (void)ppcp_relations_put(&p->relations, &rels[i]);
    }
    m.body.relation_update.relation_count = count;
    return peer_queue(p, PPCP_CHANNEL_CONTROL, &m);
}

ppcp_result ppcp_peer_publish_relations(ppcp_peer *p, size_t *out_count)
{
    ppcp_timebase_relation rels[PPCP_PEER_MAX_SYNC];
    size_t                 i, n = 0;

    if (p == NULL)
        return PPCP_ERR_INVALID;
    if (out_count != NULL)
        *out_count = 0;
    for (i = 0; i < p->sync_count; i++) {
        /* PPCP_ERR_NOT_FOUND until the exchange has produced a rate as well as
         * an offset (6.3a).  Skipped, not faked. */
        if (ppcp_sync_estimator_relation(&p->sync[i], &rels[n]) == PPCP_OK)
            n++;
    }
    if (n == 0)
        return PPCP_ERR_NOT_FOUND;
    if (out_count != NULL)
        *out_count = n;
    return ppcp_peer_relation_update(p, rels, n);
}

ppcp_result ppcp_peer_sync_residual(ppcp_peer *p, const char *shot_id,
                                    const char *timebase_id, int64_t residual_ns,
                                    const char *basis)
{
    ppcp_msg    m;
    ppcp_result rc;

    if (p == NULL || shot_id == NULL || timebase_id == NULL || basis == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_msg_init(&m, PPCP_MT_SYNC_RESIDUAL, 1);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_id_set_z(&m.body.sync_residual.shot_id, shot_id);
    if (rc == PPCP_OK)
        rc = ppcp_id_set_z(&m.body.sync_residual.timebase_id, timebase_id);
    if (rc == PPCP_OK)
        rc = ppcp_id_set_z(&m.body.sync_residual.basis, basis);
    if (rc != PPCP_OK)
        return rc;
    m.body.sync_residual.residual_ns = residual_ns;
    return peer_queue(p, PPCP_CHANNEL_CONTROL, &m);
}

/* ============================================================ L9 — liveness */

static uint32_t peer_heartbeat_interval_ms(const ppcp_peer *p)
{
    if (p->has_session_params && p->session_params.has_heartbeat_interval &&
        p->session_params.heartbeat_interval_ms > 0)
        return p->session_params.heartbeat_interval_ms;
    return PPCP_DEFAULT_HEARTBEAT_MS;   /* CORE 7.4a — the specification's default */
}

ppcp_result ppcp_peer_heartbeat(ppcp_peer *p)
{
    ppcp_msg    m;
    ppcp_result rc;

    if (p == NULL)
        return PPCP_ERR_INVALID;
    /* 7.4a — the HOST sends it.  C2 would refuse a peer with no Live profile;
     * this refuses a Live peer that is not the host, which C2 cannot see. */
    if (p->role != PPCP_ROLE_HOST)
        return PPCP_ERR_INVALID;
    rc = ppcp_msg_init(&m, PPCP_MT_HEARTBEAT, 1);
    if (rc != PPCP_OK)
        return rc;
    m.body.heartbeat.seq = ++p->beat_seq;
    return peer_queue(p, PPCP_CHANNEL_CONTROL, &m);
}

ppcp_result ppcp_peer_liveness_pump(ppcp_peer *p, int64_t now_ns)
{
    int64_t interval_ns;

    if (p == NULL)
        return PPCP_ERR_INVALID;
    interval_ns = (int64_t)peer_heartbeat_interval_ms(p) * 1000000;

    /* A heartbeat or an acknowledgement arrived since the last pump.  It is
     * stamped HERE, because this is the only function in the engine that has
     * been handed a clock reading. */
    if (p->beat_since_pump) {
        p->beat_since_pump = false;
        p->has_last_beat   = true;
        p->last_beat_ns    = now_ns;
        p->missed_beats    = 0;
        if (p->link_state == PPCP_LINK_LOST) {
            p->link_state = PPCP_LINK_LIVE;
            (void)peer_push_event(p, PPCP_EVENT_LINK_RESTORED, PPCP_OK);
        }
    } else if (p->has_last_beat && interval_ns > 0) {
        int64_t late = now_ns - p->last_beat_ns;
        uint32_t missed = (late <= 0) ? 0u : (uint32_t)(late / interval_ns);
        p->missed_beats = missed;
        /* 7.4c — three consecutive missed intervals is a lost link, and 8.3g
         * says what does NOT change when it happens: the roster, the
         * `timebase_ref` and both arbitration parameters are untouched here on
         * purpose. */
        if (missed >= 3u && p->link_state == PPCP_LINK_LIVE) {
            p->link_state = PPCP_LINK_LOST;
            (void)peer_push_event(p, PPCP_EVENT_LINK_LOST, PPCP_OK);
        }
    }

    /* 7.4a — the host's own cadence.  It lives here rather than in the sync
     * pump because 6.3d makes them different concerns that merely share a
     * channel. */
    if (p->role == PPCP_ROLE_HOST && p->has_session) {
        if (p->next_beat_ns == 0 || now_ns >= p->next_beat_ns) {
            if (ppcp_peer_heartbeat(p) == PPCP_OK)
                p->next_beat_ns = now_ns + interval_ns;
        }
    }
    return PPCP_OK;
}

ppcp_link_state ppcp_peer_link_state(const ppcp_peer *p)
{
    return (p == NULL) ? PPCP_LINK_LOST : p->link_state;
}

uint32_t ppcp_peer_missed_heartbeats(const ppcp_peer *p)
{
    return (p == NULL) ? 0u : p->missed_beats;
}

bool ppcp_peer_zero_host(const ppcp_peer *p)
{
    if (p == NULL || !p->has_session)
        return false;
    /* 4.1d / 5.10e — a `session_open` with neither arbitration parameter IS
     * the statement that the Session has no host.  That is the first entry
     * condition; the second is a host that has stopped answering (8.3g).
     *
     * F-D6-3: this is now derived from the PARAMETERS on both paths and never
     * from which end opened the Session.  Before S4 the originator had no
     * parameters recorded, so a device opening the hostless form of 4.1b fell
     * through to `link_state` and read as zero-host only by accident. */
    if (p->has_session_params)
        return !p->session_params.has_arbitration ||
               p->link_state == PPCP_LINK_LOST;
    return p->link_state == PPCP_LINK_LOST;
}

const ppcp_body_session_open *ppcp_peer_session_params(const ppcp_peer *p)
{
    if (p == NULL || !p->has_session_params)
        return NULL;
    return &p->session_params;
}

/* ============================================ MSG §9.1–9.2 — offering a Session */

ppcp_result ppcp_peer_session_offer(ppcp_peer *p, const ppcp_body_session_offer *offer)
{
    ppcp_msg    m;
    ppcp_result rc;

    if (p == NULL || offer == NULL)
        return PPCP_ERR_INVALID;
    if (!ppcp_id_is_set(&offer->session_id) || !ppcp_id_is_set(&offer->minting_peer_id))
        return PPCP_ERR_INVALID;
    rc = ppcp_msg_init(&m, PPCP_MT_SESSION_OFFER, 1);
    if (rc != PPCP_OK)
        return rc;
    m.body.session_offer = *offer;
    /* ENC 5a, as hoisted in L5: the body's `session_id` IS the envelope's. */
    rc = ppcp_msg_set_session_id(&m, offer->session_id.v);
    if (rc != PPCP_OK)
        return rc;
    return peer_queue(p, PPCP_CHANNEL_CONTROL, &m);
}

ppcp_result ppcp_peer_session_accept(ppcp_peer *p, const ppcp_body_session_accept *accept,
                                     uint64_t in_reply_to)
{
    ppcp_msg    m;
    ppcp_result rc;

    if (p == NULL || accept == NULL || !ppcp_id_is_set(&accept->session_id))
        return PPCP_ERR_INVALID;
    if (accept->have_digest_count > PPCP_MAX_HAVE_DIGESTS)
        return PPCP_ERR_LIMIT;
    rc = ppcp_msg_init(&m, PPCP_MT_SESSION_ACCEPT, 1);
    if (rc != PPCP_OK)
        return rc;
    m.body.session_accept = *accept;
    rc = ppcp_msg_set_session_id(&m, accept->session_id.v);
    if (rc == PPCP_OK)
        rc = ppcp_msg_set_reply_to(&m, in_reply_to);
    if (rc != PPCP_OK)
        return rc;
    return peer_queue(p, PPCP_CHANNEL_CONTROL, &m);
}

ppcp_result ppcp_peer_session_manifest(ppcp_peer *p,
                                       const ppcp_body_session_manifest *manifest)
{
    ppcp_msg    m;
    ppcp_result rc;

    if (p == NULL || manifest == NULL || !ppcp_id_is_set(&manifest->session_id))
        return PPCP_ERR_INVALID;
    if (manifest->capture_count > PPCP_MAX_MANIFEST ||
        manifest->stream_count > PPCP_MAX_STREAM_IDS)
        return PPCP_ERR_LIMIT;
    rc = ppcp_msg_init(&m, PPCP_MT_SESSION_MANIFEST, 1);
    if (rc != PPCP_OK)
        return rc;
    m.body.session_manifest = *manifest;
    rc = ppcp_msg_set_session_id(&m, manifest->session_id.v);
    if (rc != PPCP_OK)
        return rc;
    return peer_queue(p, PPCP_CHANNEL_CONTROL, &m);
}

/* --------------------------------------------------------------- receive */

/* C3 — "a peer receiving a message it understands but whose behaviour it does
 * not implement responds with error / profile_not_supported, never by closing
 * the connection."
 *
 * ⚠ The profile a RESPONDER needs is not the profile that confers ORIGINATION,
 * and MSG §11 tabulates only the second.  `candidate` is conferred by Detect
 * and consumed by Arbitrate; a host with no Detect must not be told it cannot
 * understand a Candidate.  So C3 is applied only to the REQUEST class — the
 * messages that oblige an answer — and this table names the profile the
 * answering side needs.  Events are comprehended and ignored, which is C1.
 * The absence of this table from MSG §11 is finding F-L6-1. */
static const char *responder_profile(ppcp_msg_type t)
{
    switch (t) {
    case PPCP_MT_HELLO:           return NULL;                    /* precedes profiles */
    case PPCP_MT_DECLARE:         return PPCP_PROFILE_CORE;
    case PPCP_MT_SESSION_OPEN:    return PPCP_PROFILE_CORE;
    case PPCP_MT_SESSION_RESUME:  return PPCP_PROFILE_LIVE;
    case PPCP_MT_STREAM_OPEN:     return PPCP_PROFILE_CAPTURE;
    case PPCP_MT_ARM:             return PPCP_PROFILE_CAPTURE;
    case PPCP_MT_DISARM:          return PPCP_PROFILE_CAPTURE;
    case PPCP_MT_HEARTBEAT:       return PPCP_PROFILE_LIVE;
    case PPCP_MT_SYNC_PROBE:      return PPCP_PROFILE_LIVE;
    case PPCP_MT_CAPTURE_REQUEST: return PPCP_PROFILE_CAPTURE;
    case PPCP_MT_PAYLOAD_RESUME:  return PPCP_PROFILE_CAPTURE;
    case PPCP_MT_SESSION_OFFER:   return PPCP_PROFILE_OFFLINE;
    default:                      return NULL;
    }
}

static ppcp_event_kind event_for(ppcp_msg_type t)
{
    switch (t) {
    case PPCP_MT_HELLO:              return PPCP_EVENT_HELLO;
    case PPCP_MT_HELLO_ACCEPT:       return PPCP_EVENT_CONNECTED;
    case PPCP_MT_DECLARE:            return PPCP_EVENT_DECLARE;
    case PPCP_MT_DECLARE_ACK:        return PPCP_EVENT_DECLARE_ACK;
    case PPCP_MT_RELATION_UPDATE:    return PPCP_EVENT_RELATION_UPDATE;
    case PPCP_MT_CALIBRATION_UPDATE: return PPCP_EVENT_CALIBRATION_UPDATE;
    case PPCP_MT_DISCONTINUITY:      return PPCP_EVENT_DISCONTINUITY;
    case PPCP_MT_SESSION_OPEN:       return PPCP_EVENT_SESSION_OPEN;
    case PPCP_MT_SESSION_JOINED:     return PPCP_EVENT_SESSION_JOINED;
    case PPCP_MT_SESSION_RESUME:     return PPCP_EVENT_SESSION_RESUME;
    case PPCP_MT_SESSION_STATE:      return PPCP_EVENT_SESSION_STATE;
    case PPCP_MT_CONTEXT_CHANGE:     return PPCP_EVENT_CONTEXT_CHANGE;
    case PPCP_MT_SESSION_CLOSE:      return PPCP_EVENT_SESSION_CLOSE;
    case PPCP_MT_STREAM_OPEN:        return PPCP_EVENT_STREAM_OPEN;
    case PPCP_MT_STREAM_OPEN_ACK:    return PPCP_EVENT_STREAM_OPEN_ACK;
    case PPCP_MT_STREAM_CLOSE:       return PPCP_EVENT_STREAM_CLOSE;
    case PPCP_MT_ARM:                return PPCP_EVENT_ARM;
    case PPCP_MT_DISARM:             return PPCP_EVENT_DISARM;
    case PPCP_MT_READINESS:          return PPCP_EVENT_READINESS;
    case PPCP_MT_INTERRUPTION:       return PPCP_EVENT_INTERRUPTION;
    case PPCP_MT_HEARTBEAT:
    case PPCP_MT_HEARTBEAT_ACK:      return PPCP_EVENT_HEARTBEAT;
    case PPCP_MT_SYNC_PROBE:
    case PPCP_MT_SYNC_REPLY:
    case PPCP_MT_SYNC_RESIDUAL:      return PPCP_EVENT_SYNC;
    case PPCP_MT_SESSION_OFFER:      return PPCP_EVENT_SESSION_OFFER;
    case PPCP_MT_SESSION_ACCEPT:     return PPCP_EVENT_SESSION_ACCEPT;
    case PPCP_MT_SESSION_MANIFEST:   return PPCP_EVENT_SESSION_MANIFEST;
    case PPCP_MT_CAPTURE_REQUEST:    return PPCP_EVENT_CAPTURE_REQUEST;
    case PPCP_MT_CANDIDATE:          return PPCP_EVENT_CANDIDATE;
    case PPCP_MT_SHOT:               return PPCP_EVENT_SHOT;
    case PPCP_MT_CAPTURE_ANNOUNCE:
    case PPCP_MT_CAPTURE_UPDATE:
    case PPCP_MT_CAPTURE_COMMITTED:  return PPCP_EVENT_CAPTURE;
    case PPCP_MT_PAYLOAD_BEGIN:
    case PPCP_MT_PAYLOAD_CHUNK:
    case PPCP_MT_PAYLOAD_ACK:
    case PPCP_MT_PAYLOAD_END:
    case PPCP_MT_PAYLOAD_ABORT:
    case PPCP_MT_PAYLOAD_RESUME:     return PPCP_EVENT_PAYLOAD;
    case PPCP_MT_ANNOTATION:         return PPCP_EVENT_ANNOTATION;
    case PPCP_MT_SHOT_LINK:          return PPCP_EVENT_SHOT_LINK;
    case PPCP_MT_SESSION_LINK:       return PPCP_EVENT_SESSION_LINK;
    case PPCP_MT_ERROR:              return PPCP_EVENT_ERROR;
    case PPCP_MT_UNKNOWN:            return PPCP_EVENT_UNKNOWN;
    default:                         return PPCP_EVENT_NONE;
    }
}

static ppcp_result peer_on_hello(ppcp_peer *p, const ppcp_msg *m)
{
    const ppcp_body_hello *b = &m->body.hello;
    ppcp_msg    r;
    ppcp_id     selected;
    ppcp_result rc;
    size_t      i;

    /* 3.2c / I20 — at most one host per Session, and the responder is the one
     * that can see the collision.  Fatal: two hosts have no session to have. */
    if (p->role == PPCP_ROLE_HOST && b->role == PPCP_ROLE_HOST) {
        (void)ppcp_peer_error(p, PPCP_CHANNEL_CONTROL, PPCP_ERRCODE_ROLE_CONFLICT,
                              "two hosts", true, m->env.msg_id);
        return PPCP_ERR_INVALID;
    }

    rc = ppcp_version_select(b->versions, b->version_count, p->versions, p->version_count,
                             &selected);
    /* 3.2a / 10.1c: no common MAJOR is `unsupported_version` and a close.
     * 10.1e: so is a version below this peer's stated support window — and the
     * initiator is entitled to know that before it is refused, which is what
     * `detail.supported` on the error carries (10.1f). */
    if (rc != PPCP_OK || !ppcp_version_in_window(&selected, &p->min_version)) {
        (void)ppcp_peer_error(p, PPCP_CHANNEL_CONTROL, PPCP_ERRCODE_UNSUPPORTED_VERSION,
                              "no common version", true, m->env.msg_id);
        return PPCP_ERR_INVALID;
    }

    p->remote_peer_id = b->peer_id;
    p->remote_role    = b->role;
    for (i = 0; i < b->profile_count && i < PPCP_MAX_PROFILES; i++)
        p->remote_profiles[i] = b->profiles[i];
    p->remote_profile_count = b->profile_count;
    p->has_remote_hello     = true;
    p->version              = selected;
    p->has_version          = true;

    rc = ppcp_msg_init(&r, PPCP_MT_HELLO_ACCEPT, 1);
    if (rc != PPCP_OK)
        return rc;
    r.body.hello_accept.version = selected;
    /* 3.2d: the support window is always stated. */
    r.body.hello_accept.min_version = p->min_version;
    r.body.hello_accept.peer_id     = p->peer_id;
    r.body.hello_accept.role        = p->role;
    for (i = 0; i < p->profile_count; i++)
        r.body.hello_accept.profiles[i] = p->profiles[i];
    r.body.hello_accept.profile_count = p->profile_count;
    rc = ppcp_msg_set_reply_to(&r, m->env.msg_id);
    if (rc != PPCP_OK)
        return rc;
    rc = peer_queue(p, PPCP_CHANNEL_CONTROL, &r);
    if (rc != PPCP_OK)
        return rc;
    if (p->state == PPCP_PEER_INIT || p->state == PPCP_PEER_HELLO_SENT)
        p->state = PPCP_PEER_CONNECTED;
    return PPCP_OK;
}

static ppcp_result peer_on_hello_accept(ppcp_peer *p, const ppcp_msg *m)
{
    const ppcp_body_hello_accept *b = &m->body.hello_accept;
    size_t i;
    bool   offered = false;

    /* I20 seen from the other end: an initiator that is itself host and is
     * answered by a host has the same collision to refuse. */
    if (p->role == PPCP_ROLE_HOST && b->role == PPCP_ROLE_HOST) {
        (void)ppcp_peer_error(p, PPCP_CHANNEL_CONTROL, PPCP_ERRCODE_ROLE_CONFLICT,
                              "two hosts", true, m->env.msg_id);
        return PPCP_ERR_INVALID;
    }
    /* 3.2b: both peers use the SELECTED version — which has to be one this end
     * offered, or the responder chose a dialect nobody speaks. */
    for (i = 0; i < p->version_count; i++)
        if (ppcp_id_equal(&p->versions[i], &b->version))
            offered = true;
    if (!offered) {
        (void)ppcp_peer_error(p, PPCP_CHANNEL_CONTROL, PPCP_ERRCODE_UNSUPPORTED_VERSION,
                              "version not offered", true, m->env.msg_id);
        return PPCP_ERR_INVALID;
    }

    p->remote_peer_id = b->peer_id;
    p->remote_role    = b->role;
    for (i = 0; i < b->profile_count && i < PPCP_MAX_PROFILES; i++)
        p->remote_profiles[i] = b->profiles[i];
    p->remote_profile_count = b->profile_count;
    p->has_remote_hello     = true;
    p->version              = b->version;
    p->has_version          = true;
    if (p->state == PPCP_PEER_HELLO_SENT || p->state == PPCP_PEER_INIT)
        p->state = PPCP_PEER_CONNECTED;
    return PPCP_OK;
}

static ppcp_result peer_on_declare(ppcp_peer *p, const ppcp_msg *m)
{
    const ppcp_body_declare *b = &m->body.declare;
    ppcp_msg    r;
    ppcp_result rc;
    bool        accept = true;
    ppcp_id     reason;

    memset(&reason, 0, sizeof(reason));

    /* 3.3a: a later generation wholly replaces the previous snapshot.  An
     * older one is stale and is not applied — otherwise a reordered pair would
     * silently roll the roster back. */
    if (p->has_remote_desc && b->generation < p->remote_generation)
        return PPCP_OK;

    p->remote            = b->peer;
    p->remote_generation = b->generation;
    p->has_remote_desc   = true;

    /* I14 / 3.4b — the decision is the embedding's and no threshold appears in
     * this file.  A peer that supplies no policy accepts, because refusing by
     * default would make the library the one with an opinion. */
    if (p->ingest_policy != NULL)
        accept = p->ingest_policy(p->ctx, &p->remote, &reason);

    rc = ppcp_msg_init(&r, PPCP_MT_DECLARE_ACK, 1);
    if (rc != PPCP_OK)
        return rc;
    r.body.declare_ack.generation = b->generation;
    r.body.declare_ack.verdict    = accept ? PPCP_VERDICT_ACCEPTED : PPCP_VERDICT_REJECTED;
    if (!accept) {
        /* 3.4a: a rejection carries a machine-readable reason, and 7.2b says
         * it does NOT close the connection. */
        if (!ppcp_id_is_set(&reason))
            (void)ppcp_id_set_z(&reason, "policy_reject");
        r.body.declare_ack.has_reason = true;
        r.body.declare_ack.reason     = reason;
    }
    rc = ppcp_msg_set_reply_to(&r, m->env.msg_id);
    if (rc != PPCP_OK)
        return rc;
    return peer_queue(p, PPCP_CHANNEL_CONTROL, &r);
}

static ppcp_result peer_on_session_open(ppcp_peer *p, const ppcp_msg *m)
{
    const ppcp_body_session_open *b = &m->body.session_open;
    ppcp_msg    r;
    ppcp_result rc;

    /* 4.1a / I16 — `timebase_ref` is immutable for the life of the Session.  A
     * second `session_open` for the same `session_id` naming a different one
     * is an error, and the Session is not re-opened under it. */
    if (p->has_session && ppcp_id_equal(&p->session_id, &b->session_id) &&
        !ppcp_id_equal(&p->timebase_ref, &b->timebase_ref)) {
        (void)ppcp_peer_error(p, PPCP_CHANNEL_CONTROL, PPCP_ERRCODE_MALFORMED,
                              "timebase_ref is immutable", true, m->env.msg_id);
        return PPCP_ERR_MALFORMED;
    }

    p->has_session  = true;
    p->session_id   = b->session_id;
    p->timebase_ref = b->timebase_ref;
    p->state        = PPCP_PEER_JOINED;
    /* I16 / 8.3g — kept verbatim, so a later link loss can be shown to have
     * changed none of it. */
    p->session_params     = *b;
    p->has_session_params = true;

    rc = ppcp_msg_init(&r, PPCP_MT_SESSION_JOINED, 1);
    if (rc != PPCP_OK)
        return rc;
    r.body.session_joined.session_id = b->session_id;
    r.body.session_joined.peer_id    = p->peer_id;
    r.body.session_joined.verdict    = PPCP_JOINED;
    rc = ppcp_msg_set_reply_to(&r, m->env.msg_id);
    if (rc != PPCP_OK)
        return rc;
    return peer_queue(p, PPCP_CHANNEL_CONTROL, &r);
}

static ppcp_result peer_on_stream_open(ppcp_peer *p, const ppcp_msg *m)
{
    const ppcp_stream *s = &m->body.stream_open.stream;
    ppcp_msg    r;
    ppcp_result rc, add;

    add = peer_stream_add(p, s);
    rc  = ppcp_msg_init(&r, PPCP_MT_STREAM_OPEN_ACK, 1);
    if (rc != PPCP_OK)
        return rc;
    r.body.stream_open_ack.stream_id = s->id;
    if (add == PPCP_OK) {
        r.body.stream_open_ack.verdict       = PPCP_STREAM_OPENED;
        r.body.stream_open_ack.has_opened_at = true;
        r.body.stream_open_ack.opened_at     = s->opened_at;
    } else {
        r.body.stream_open_ack.verdict    = PPCP_STREAM_REFUSED;
        r.body.stream_open_ack.has_reason = true;
        (void)ppcp_id_set_z(&r.body.stream_open_ack.reason,
                            add == PPCP_ERR_LIMIT ? PPCP_ERRCODE_RESOURCE_EXHAUSTED
                                                  : PPCP_ERRCODE_NOT_DECLARED);
    }
    rc = ppcp_msg_set_reply_to(&r, m->env.msg_id);
    if (rc != PPCP_OK)
        return rc;
    return peer_queue(p, PPCP_CHANNEL_CONTROL, &r);
}

/* MSG 6.1 — the responder half.  `t2` is read as close to reception as this
 * engine can see, which is the moment the frame is handled, and `t3` as close
 * to transmission, which is the moment before it is queued.  6.1c permits them
 * to be equal and says what that declares: the residence time is included in
 * the measurement rather than removed from it.  An embedding that can stamp
 * closer to the socket than this uses ppcp_peer_sync_observe() on the other
 * side of the exchange. */
static ppcp_result peer_on_sync_probe(ppcp_peer *p, const ppcp_msg *m)
{
    const ppcp_body_sync_probe *b = &m->body.sync_probe;
    ppcp_instant t2, t3;
    ppcp_msg     r;
    ppcp_result  rc;
    const char  *resp_tb;

    /* 6.1b — `t2` and `t3` are in a timebase the responder DECLARED.  A peer
     * that was given none has no honest answer, and inventing one is exactly
     * what 8.1e forbids elsewhere in this specification. */
    if (!p->has_sync_tb) {
        (void)ppcp_peer_error(p, PPCP_CHANNEL_CONTROL, PPCP_ERRCODE_PROFILE_NOT_SUPPORTED,
                              "no sync timebase declared", true, m->env.msg_id);
        return PPCP_ERR_INVALID;
    }

    /* Erratum E2, F-H5-1 — THE REMOTE HALF OF I21.
     *
     * 6.1d has the prober set `timebase_id` per LOCAL clock, and 6.1b lets the
     * responder answer on whichever clock it likes.  Between them there is no
     * way for a host with one clock to measure a device's camera clock and its
     * audio clock separately: every probe comes back stamped on the responder's
     * single chosen timebase, so I21's remote half is unreachable and CT-I21
     * could only ever be written from the multi-clock side.
     *
     * So: where `timebase_id` names a timebase THIS peer declared, this peer
     * answers on it.  6.1a and 6.1b both still hold — `t1` is echoed unchanged,
     * `t2` and `t3` share one declared responder timebase — and a responder
     * that does not implement this answers on its default, which the prober
     * sees as a `t2.tb` that is not the one it asked for.  That is evidence,
     * not breakage. */
    resp_tb = p->sync_tb.v;
    if (b->timebase_id.len > 0 && ppcp_peer_declares_timebase(p, &b->timebase_id) &&
        ppcp_clock_read(&p->clock, b->timebase_id.v, &t2) == PPCP_OK) {
        resp_tb = b->timebase_id.v;
    } else {
        rc = ppcp_clock_read(&p->clock, resp_tb, &t2);
        if (rc != PPCP_OK)
            return rc;
    }

    rc = ppcp_msg_init(&r, PPCP_MT_SYNC_REPLY, 1);
    if (rc != PPCP_OK)
        return rc;
    r.body.sync_reply.probe_seq = b->probe_seq;
    r.body.sync_reply.t1        = b->t1;      /* 6.1a: echoed unmodified, `tb` included */
    r.body.sync_reply.t2        = t2;
    /* 6.1c: `t3` as close to transmission as this implementation allows, and on
     * the SAME timebase as `t2` (6.1b). */
    rc = ppcp_clock_read(&p->clock, resp_tb, &t3);
    if (rc != PPCP_OK)
        return rc;
    r.body.sync_reply.t3 = t3;
    rc = ppcp_msg_set_reply_to(&r, m->env.msg_id);
    if (rc != PPCP_OK)
        return rc;
    return peer_queue(p, PPCP_CHANNEL_CONTROL, &r);
}

static ppcp_result peer_on_sync_reply(ppcp_peer *p, const ppcp_msg *m)
{
    const ppcp_body_sync_reply *b = &m->body.sync_reply;
    ppcp_instant t4;
    ppcp_result  rc;
    size_t       i, j, n;
    bool         matched = false;

    /* 6.1b — one responder timebase for both stamps.  Two would make the
     * exchange unusable and the relation meaningless.  Checked before the
     * lookup because the lookup now uses `t2.tb`. */
    if (!ppcp_id_equal(&b->t2.tb, &b->t3.tb))
        return PPCP_ERR_MALFORMED;

    i = peer_sync_find_for_reply(p, b);
    if (i == p->sync_count)
        return PPCP_OK;    /* a reply for a pair this peer is not probing */

    /* 6.1a — the echo is the one we sent, or it is not our exchange. */
    n = sizeof(p->sched[i].out) / sizeof(p->sched[i].out[0]);
    for (j = 0; j < n; j++) {
        if (p->sched[i].out[j].live && p->sched[i].out[j].seq == b->probe_seq) {
            if (p->sched[i].out[j].t1_ns != b->t1.ns)
                return PPCP_ERR_MALFORMED;
            p->sched[i].out[j].live = false;
            matched = true;
            break;
        }
    }
    if (!matched)
        return PPCP_OK;    /* stale or duplicated; not an error, just not evidence */

    rc = ppcp_sync_estimator_set_remote_tb(&p->sync[i], b->t2.tb.v, b->t2.tb.len);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_clock_read(&p->clock, b->t1.tb.v, &t4);
    if (rc != PPCP_OK)
        return rc;
    return ppcp_sync_estimator_observe(&p->sync[i], b->t1.ns, b->t2.ns, b->t3.ns, t4.ns);
}

static ppcp_result peer_on_heartbeat(ppcp_peer *p, const ppcp_msg *m)
{
    ppcp_health h;
    ppcp_msg    r;
    ppcp_result rc;

    /* 7.4b / MSG 5.4b — the acknowledgement carries thermal, storage and
     * battery, and this library has none of them.  A peer configured without a
     * health callback answers `profile_not_supported` rather than sending an
     * ack full of zeroes, because a fabricated `nominal` is precisely the
     * "silently accepting worse data" 7.4b exists to prevent. */
    if (p->health_report == NULL) {
        (void)ppcp_peer_error(p, PPCP_CHANNEL_CONTROL, PPCP_ERRCODE_PROFILE_NOT_SUPPORTED,
                              "no health source", true, m->env.msg_id);
        return PPCP_ERR_INVALID;
    }
    memset(&h, 0, sizeof(h));
    rc = p->health_report(p->ctx, &h);
    if (rc != PPCP_OK)
        return rc;

    rc = ppcp_msg_init(&r, PPCP_MT_HEARTBEAT_ACK, 1);
    if (rc != PPCP_OK)
        return rc;
    r.body.heartbeat_ack.seq                  = m->body.heartbeat.seq;
    r.body.heartbeat_ack.thermal              = h.thermal;
    r.body.heartbeat_ack.has_vendor_label     = h.has_vendor_label;
    r.body.heartbeat_ack.vendor_thermal_label = h.vendor_thermal_label;
    r.body.heartbeat_ack.storage_free_bytes   = h.storage_free_bytes;
    r.body.heartbeat_ack.has_battery_pct      = h.has_battery_pct;
    r.body.heartbeat_ack.battery_pct          = h.battery_pct;
    r.body.heartbeat_ack.has_charging         = h.has_charging;
    r.body.heartbeat_ack.charging             = h.charging;
    rc = ppcp_msg_set_reply_to(&r, m->env.msg_id);
    if (rc != PPCP_OK)
        return rc;
    return peer_queue(p, PPCP_CHANNEL_CONTROL, &r);
}

static void peer_handle(ppcp_peer *p, uint8_t channel, const ppcp_msg *m)
{
    const ppcp_msg_info *info = ppcp_msg_for(m->type);
    ppcp_msg            *slot;
    ppcp_result          rc = PPCP_OK;

    /* MSG §2 — a message on the wrong channel is `malformed` and is not acted
     * on.  ENC 2c already proved the header agrees with the stream. */
    if (ppcp_msg_check_channel(m->type, channel) != PPCP_OK) {
        (void)ppcp_peer_error(p, channel, PPCP_ERRCODE_MALFORMED, "wrong channel",
                              true, m->env.msg_id);
        return;
    }

    /* C3 — a REQUEST whose behaviour this peer does not implement.  Answered,
     * never closed: `profile_not_supported` is non-fatal by MSG §10 and by
     * ppcp_msg_error_is_fatal(), which is what keeps the session capturing. */
    if (info != NULL && info->cls == PPCP_MSG_REQUEST) {
        const char *need = responder_profile(m->type);
        if (need != NULL && !peer_has_profile(p, need)) {
            (void)ppcp_peer_error(p, channel, PPCP_ERRCODE_PROFILE_NOT_SUPPORTED,
                                  need, true, m->env.msg_id);
            return;
        }
    }

    switch (m->type) {
    case PPCP_MT_LINK_BIND:
        /* ENC 2.1b: the listener's binder handled this before the bytes ever
         * reached an engine.  Meeting one here is harmless and ignorable. */
        return;
    case PPCP_MT_HELLO:        rc = peer_on_hello(p, m); break;
    case PPCP_MT_HELLO_ACCEPT: rc = peer_on_hello_accept(p, m); break;
    case PPCP_MT_DECLARE:      rc = peer_on_declare(p, m); break;
    case PPCP_MT_SESSION_OPEN: rc = peer_on_session_open(p, m); break;
    case PPCP_MT_STREAM_OPEN:  rc = peer_on_stream_open(p, m); break;
    case PPCP_MT_STREAM_CLOSE:
        /* 5.1d: either peer may close a Stream, so the engine removes it
         * whichever end sent this. */
        peer_stream_remove(p, &m->body.stream_close.stream_id);
        break;
    case PPCP_MT_CAPTURE_ANNOUNCE: {
        /* A Capture somebody else owns.  Recorded so a receiver can answer
         * `capture_committed` for it later (8.4a) and, from a bundle, on its
         * next connection with the owning peer (5.14h).
         *
         * F-H4-1 — 5.11j is enforced on the way IN as well as OUT.  The
         * Capture does not carry the Stream's `kind`, but it carries
         * `stream_id` and the engine already recorded the Stream from
         * `stream_open`, so the receiver can resolve it.  Passing `false`
         * unconditionally, as L6 did, made a consumer either re-run the check
         * itself or silently accept the one announce 5.11j says it never
         * sees. */
        const ppcp_capture *c  = &m->body.capture_announce.capture;
        const ppcp_stream  *st = NULL;
        bool is_preview = false;
        size_t k;
        for (k = 0; k < p->stream_count; k++) {
            if (ppcp_id_equal(&p->streams[k].id, &c->stream_id)) {
                st = &p->streams[k];
                break;
            }
        }
        if (st != NULL)
            is_preview = ppcp_stream_is_preview(st);
        rc = ppcp_transfer_observe_announce(&p->transfers, c, is_preview);
        if (rc != PPCP_OK) {
            /* MSG 8.1i: a preview Capture announced `transfer: pending` is the
             * violation, and it is answered rather than dropped.  Non-fatal —
             * one bad announce does not end a session that is capturing. */
            (void)ppcp_peer_error(p, channel, PPCP_ERRCODE_MALFORMED,
                                  "preview capture may not be announced pending",
                                  true, m->env.msg_id);
        }
        break;
    }
    case PPCP_MT_CAPTURE_COMMITTED:
        /* 8.4a / 5.14f — the one route to `confirmed`, and it arrives. */
        rc = ppcp_transfer_on_committed(&p->transfers, &m->body.capture_committed);
        if (rc == PPCP_ERR_NOT_FOUND)
            rc = PPCP_OK;   /* a Capture this peer never held; not an error here */
        break;
    case PPCP_MT_PAYLOAD_ABORT:
        /* 8.3c / 5.14g exit 3: `already_present` means the receiver
         * demonstrably holds the payload durably, which for eviction is
         * equivalent to a commit. */
        if (ppcp_cbor_key_is(m->body.payload_abort.reason.v,
                             m->body.payload_abort.reason.len,
                             PPCP_ERRCODE_ALREADY_PRESENT))
            (void)ppcp_transfer_on_already_present(&p->transfers,
                                                   &m->body.payload_abort.capture_id);
        break;
    case PPCP_MT_PAYLOAD_ACK:
        (void)ppcp_transfer_on_acked(&p->transfers, &m->body.payload_ack.capture_id,
                                     m->body.payload_ack.index);
        break;
    case PPCP_MT_SYNC_PROBE:  rc = peer_on_sync_probe(p, m); break;
    case PPCP_MT_SYNC_REPLY:  rc = peer_on_sync_reply(p, m);  break;
    case PPCP_MT_HEARTBEAT:
        /* 7.4c counts intervals, and the count lives in the liveness pump
         * because that is where a clock reading arrives.  Here we record only
         * that one came. */
        p->beat_since_pump = true;
        rc = peer_on_heartbeat(p, m);
        break;
    case PPCP_MT_HEARTBEAT_ACK:
        p->beat_since_pump = true;
        break;
    case PPCP_MT_RELATION_UPDATE: {
        /* The counterpart's current relations.  Held so an arbiter can convert
         * a Candidate (8.2a) without the embedding shuttling them; never
         * composed (I18). */
        size_t k;
        for (k = 0; k < m->body.relation_update.relation_count; k++)
            (void)ppcp_relations_put(&p->relations,
                                     &m->body.relation_update.relations[k]);
        break;
    }
    case PPCP_MT_ARM:    p->armed = true;  break;   /* 5.2a: answer with readiness */
    case PPCP_MT_DISARM: p->armed = false; break;
    case PPCP_MT_SESSION_CLOSE:
        p->has_session = false;
        p->armed       = false;
        p->state       = PPCP_PEER_DECLARED;
        break;
    case PPCP_MT_ERROR:
        /* MSG 10b: only a fatal code closes the transport.  An unknown code is
         * not fatal, so a MINOR that adds one does not disconnect anybody.
         * The verdict rides on the event's `status` rather than on this
         * function's return, because an `error` arriving is not a failure of
         * the feed — it is a fact the embedding has to see. */
        if (ppcp_msg_error_is_fatal(m->body.error.code.v, m->body.error.code.len)) {
            p->state = PPCP_PEER_CLOSED;
            rc = PPCP_ERR_FATAL_LIMIT;
        } else {
            rc = PPCP_ERR_MALFORMED;
        }
        break;
    default:
        break;
    }

    slot  = peer_push_event_ch(p, event_for(m->type), rc, channel);
    *slot = *m;
    /* A `hello` produces two events, because "who is there" and "what version
     * are we speaking" are different facts and an embedding acts on them at
     * different moments. */
    if (m->type == PPCP_MT_HELLO && p->state == PPCP_PEER_CONNECTED) {
        slot  = peer_push_event_ch(p, PPCP_EVENT_CONNECTED, PPCP_OK, channel);
        *slot = *m;
    }
}

ppcp_result ppcp_peer_feed(ppcp_peer *p, uint8_t channel, const uint8_t *bytes, size_t len,
                           size_t *out_consumed)
{
    size_t      off = 0;
    ppcp_result rc;

    if (p == NULL || out_consumed == NULL || (len > 0 && bytes == NULL))
        return PPCP_ERR_INVALID;
    if (channel >= PPCP_PEER_MAX_CHANNELS)
        return PPCP_ERR_INVALID;
    *out_consumed = 0;
    p->feed_stalled = false;
    if (p->state == PPCP_PEER_CLOSED)
        return PPCP_ERR_INVALID;

    while (off + PPCP_FRAME_HEADER_BYTES <= len) {
        ppcp_frame_header hdr;
        const uint8_t    *payload;
        size_t            consumed = 0;
        ppcp_msg          m;
        ppcp_arena       *arena;

        /* F-L13-1.  Every frame below raises at least one event, and the
         * borrowed ppcp_msg in an event stays valid only until the ring wraps.
         * Consuming a frame whose events cannot be queued would drop an older
         * one — which is how a replayed bundle lost `capture_announce`.  So the
         * loop stops here instead, `*out_consumed` says how far it got, and the
         * caller drains and re-presents the rest.  This is backpressure, not an
         * error, so the return stays PPCP_OK. */
        if (!peer_event_room(p)) {
            p->feed_stalled = true;
            break;
        }

        rc = ppcp_frame_header_parse(bytes + off, &hdr);
        if (rc != PPCP_OK) {
            /* ENC 8a: a length beyond the channel's limit means the stream has
             * desynchronised.  There is no resynchronisation point, so it is
             * fatal and the engine closes. */
            *out_consumed = off;
            p->state = PPCP_PEER_CLOSED;
            return rc;
        }
        /* ENC 2c: the channel byte matches the stream it arrived on. */
        if (hdr.channel != channel) {
            *out_consumed = off;
            (void)ppcp_peer_error(p, channel, PPCP_ERRCODE_MALFORMED,
                                  "channel mismatch", false, 0);
            return PPCP_ERR_MALFORMED;
        }
        rc = ppcp_frame_read(bytes + off, len - off, &hdr, &payload, &consumed);
        if (rc == PPCP_ERR_TRUNCATED) {
            /* ENC 3c: whether this is an error is the READER'S decision — at
             * the end of a bundle it is `completeness: partial`, and on a live
             * transport it is simply "not yet".  So it stops the loop and is
             * not itself a failure. */
            break;
        }
        if (rc != PPCP_OK) {
            *out_consumed = off;
            p->state = PPCP_PEER_CLOSED;
            return rc;
        }

        /* 3.3a: the counterpart's declaration outlives its frame, so it gets
         * the arena that is not reset per message. */
        {
            ppcp_envelope env;
            uint32_t      pairs = 0;
            ppcp_msg_info info;
            bool          is_declare = false;
            if (ppcp_envelope_decode(payload, hdr.payload_len,
                                     ppcp_cbor_limits_for_channel(channel),
                                     &env, &pairs) == PPCP_OK &&
                ppcp_msg_lookup(env.type, env.type_len, &info) == PPCP_OK)
                is_declare = (info.id == PPCP_MT_DECLARE);
            if (is_declare) {
                ppcp_arena_reset(&p->decl_arena);
                arena = &p->decl_arena;
            } else {
                ppcp_arena_reset(&p->scratch_arena);
                arena = &p->scratch_arena;
            }
        }

        memset(&m, 0, sizeof(m));
        rc = ppcp_msg_decode(payload, hdr.payload_len,
                             ppcp_cbor_limits_for_channel(channel), arena, &m);
        if (rc != PPCP_OK) {
            /* ENC 5d — answer `malformed` with `reply_to` where `msg_id` could
             * be recovered, and without it otherwise.  The transport stays
             * open: one bad message does not end a session that is capturing. */
            uint64_t mid = 0;
            bool     got = ppcp_envelope_recover_msg_id(payload, hdr.payload_len,
                                                        ppcp_cbor_limits_for_channel(channel),
                                                        &mid) == PPCP_OK;
            (void)ppcp_peer_error(p, channel, PPCP_ERRCODE_MALFORMED, "undecodable",
                                  got, mid);
            (void)peer_push_event_ch(p, PPCP_EVENT_ERROR, rc, channel);
        } else {
            peer_handle(p, channel, &m);
        }
        off += consumed;
        if (p->state == PPCP_PEER_CLOSED)
            break;
    }
    *out_consumed = off;
    return PPCP_OK;
}

/* ------------------------------------------------------------- accessors */

ppcp_peer_state ppcp_peer_get_state(const ppcp_peer *p)
{
    return (p == NULL) ? PPCP_PEER_CLOSED : p->state;
}

const ppcp_id *ppcp_peer_id(const ppcp_peer *p)
{
    return (p == NULL) ? NULL : &p->peer_id;
}

ppcp_role ppcp_peer_get_role(const ppcp_peer *p)
{
    return (p == NULL) ? PPCP_ROLE_OBSERVER : p->role;
}

const char *ppcp_peer_version(const ppcp_peer *p)
{
    if (p == NULL || !p->has_version)
        return NULL;
    return p->version.v;
}

const ppcp_peer_desc *ppcp_peer_counterpart(const ppcp_peer *p)
{
    if (p == NULL || !p->has_remote_desc)
        return NULL;
    return &p->remote;
}

bool ppcp_peer_is_armed(const ppcp_peer *p)
{
    return (p != NULL) && p->armed;
}

size_t ppcp_peer_stream_count(const ppcp_peer *p)
{
    return (p == NULL) ? 0 : p->stream_count;
}

const ppcp_stream *ppcp_peer_stream_at(const ppcp_peer *p, size_t index)
{
    if (p == NULL || index >= p->stream_count)
        return NULL;
    return &p->streams[index];
}

const ppcp_stream *ppcp_peer_stream_find(const ppcp_peer *p, const char *stream_id)
{
    size_t i;
    if (p == NULL || stream_id == NULL)
        return NULL;
    for (i = 0; i < p->stream_count; i++)
        if (ppcp_cbor_key_is(p->streams[i].id.v, p->streams[i].id.len, stream_id))
            return &p->streams[i];
    return NULL;
}

size_t ppcp_peer_own_source_count(const ppcp_peer *p)
{
    return (p == NULL) ? 0 : p->own_source_count;
}

bool ppcp_peer_owns_source(const ppcp_peer *p, const ppcp_id *source_id,
                           ppcp_id *out_timebase_id)
{
    size_t i;
    if (p == NULL || source_id == NULL)
        return false;
    for (i = 0; i < p->own_source_count; i++) {
        if (ppcp_id_equal(&p->own_sources[i].source_id, source_id)) {
            if (out_timebase_id != NULL)
                *out_timebase_id = p->own_sources[i].timebase_id;
            return true;
        }
    }
    return false;
}

bool ppcp_peer_declares_timebase(const ppcp_peer *p, const ppcp_id *timebase_id)
{
    size_t i;
    if (p == NULL || timebase_id == NULL)
        return false;
    for (i = 0; i < p->own_timebase_count; i++)
        if (ppcp_id_equal(&p->own_timebases[i], timebase_id))
            return true;
    return false;
}

const ppcp_id *ppcp_peer_session_id(const ppcp_peer *p)
{
    if (p == NULL || !p->has_session)
        return NULL;
    return &p->session_id;
}

const ppcp_id *ppcp_peer_timebase_ref(const ppcp_peer *p)
{
    if (p == NULL || !p->has_session)
        return NULL;
    return &p->timebase_ref;
}
