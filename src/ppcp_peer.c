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

typedef struct tx_queue {
    uint8_t buf[PPCP_PEER_TXQ_BYTES];
    size_t  used;
} tx_queue;

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

    /* streams (5.1a: a Stream's identity is fixed for its lifetime) */
    ppcp_stream streams[PPCP_PEER_MAX_STREAMS];
    size_t      stream_count;

    /* transport */
    tx_queue tx[PPCP_PEER_MAX_CHANNELS];

    /* events */
    ppcp_msg        ev_msg[PPCP_PEER_EVENT_QUEUE];
    ppcp_event_kind ev_kind[PPCP_PEER_EVENT_QUEUE];
    ppcp_result     ev_status[PPCP_PEER_EVENT_QUEUE];
    size_t          ev_head, ev_count;

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

ppcp_result ppcp_link_binder_offer(ppcp_link_binder *b, uint8_t stream_channel,
                                   const uint8_t *bytes, size_t len,
                                   size_t *out_consumed, size_t *out_link)
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
    /* ENC 2c: the header's channel matches the stream it arrived on.  2.1b
     * then takes the stream's channel from the header, which is only sound
     * because 2c already required them to agree. */
    rc = ppcp_frame_check_stream(hdr.channel, stream_channel);
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
    p->state         = PPCP_PEER_INIT;
    ppcp_msg_seq_init(&p->seq);
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

static ppcp_result peer_encode_into_tx(ppcp_peer *p, uint8_t channel, const ppcp_msg *m)
{
    tx_queue   *q;
    size_t      written = 0;
    ppcp_result rc;

    if (channel >= PPCP_PEER_MAX_CHANNELS)
        return PPCP_ERR_INVALID;
    q  = &p->tx[channel];
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
    return p->tx[channel].used;
}

ppcp_result ppcp_peer_drain(ppcp_peer *p, uint8_t channel, uint8_t *out, size_t cap,
                            size_t *out_len)
{
    tx_queue *q;
    size_t    take = 0;

    if (p == NULL || out == NULL || out_len == NULL || channel >= PPCP_PEER_MAX_CHANNELS)
        return PPCP_ERR_INVALID;
    q = &p->tx[channel];
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

static ppcp_msg *peer_push_event(ppcp_peer *p, ppcp_event_kind kind, ppcp_result status)
{
    size_t slot;
    if (p->ev_count == PPCP_PEER_EVENT_QUEUE) {
        /* The oldest event is dropped, not the newest: an embedding that is
         * not draining has already lost the earlier ones' timeliness, and
         * losing the most recent state change is worse than losing a stale
         * one.  The drop is visible — the queue never silently grows. */
        p->ev_head = (p->ev_head + 1) % PPCP_PEER_EVENT_QUEUE;
        p->ev_count--;
    }
    slot = (p->ev_head + p->ev_count) % PPCP_PEER_EVENT_QUEUE;
    p->ev_count++;
    p->ev_kind[slot]   = kind;
    p->ev_status[slot] = status;
    memset(&p->ev_msg[slot], 0, sizeof(p->ev_msg[slot]));
    return &p->ev_msg[slot];
}

ppcp_result ppcp_peer_next_event(ppcp_peer *p, ppcp_event *out)
{
    if (p == NULL || out == NULL)
        return PPCP_ERR_INVALID;
    if (p->ev_count == 0)
        return PPCP_ERR_NOT_FOUND;
    out->kind   = p->ev_kind[p->ev_head];
    out->status = p->ev_status[p->ev_head];
    out->msg    = &p->ev_msg[p->ev_head];
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

    if (p == NULL || stream_id == NULL || closed_at == NULL || reason == NULL)
        return PPCP_ERR_INVALID;
    rc = ppcp_instant_validate(closed_at);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_msg_init(&m, PPCP_MT_STREAM_CLOSE, 1);
    if (rc != PPCP_OK)
        return rc;
    rc = ppcp_id_set_z(&m.body.stream_close.stream_id, stream_id);
    if (rc != PPCP_OK)
        return rc;
    m.body.stream_close.closed_at = *closed_at;
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

    slot  = peer_push_event(p, event_for(m->type), rc);
    *slot = *m;
    /* A `hello` produces two events, because "who is there" and "what version
     * are we speaking" are different facts and an embedding acts on them at
     * different moments. */
    if (m->type == PPCP_MT_HELLO && p->state == PPCP_PEER_CONNECTED) {
        slot  = peer_push_event(p, PPCP_EVENT_CONNECTED, PPCP_OK);
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
    if (p->state == PPCP_PEER_CLOSED)
        return PPCP_ERR_INVALID;

    while (off + PPCP_FRAME_HEADER_BYTES <= len) {
        ppcp_frame_header hdr;
        const uint8_t    *payload;
        size_t            consumed = 0;
        ppcp_msg          m;
        ppcp_arena       *arena;

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
            (void)peer_push_event(p, PPCP_EVENT_ERROR, rc);
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
