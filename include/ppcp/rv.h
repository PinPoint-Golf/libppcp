/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * rv.h — PPCP-RV: the pairing-code payload, the key derivation, the resolvable
 * identifiers and the PSK identity.
 *
 * What is here (plan L12): RV §3.4, §4, §5.1, §5.3 and the vectors of §10.
 * What is deliberately NOT here, and never will be (plan A7, A8):
 *
 *   - TLS.  The library produces K_tls and the PSK identity; the applications
 *     hold the socket and the TLS stack.  RV 5.2i says compliance on the device
 *     is demonstrated by observed handshake, not by an API assertion.
 *   - Discovery.  The library computes rn/rid and resolves them; the app
 *     registers and browses `_ppcp._tcp`.
 *   - Storage, and a random number generator.  Every random value below is a
 *     PARAMETER.  RV 7.2a requires secrets to come from a platform CSPRNG at
 *     full width, and a library that called rand() would be the single point at
 *     which the whole model fails silently (RT-12 is a review method for
 *     exactly this reason).
 */
#ifndef PPCP_RV_H
#define PPCP_RV_H

#include "ppcp/hash.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------- base64url (4.1a) */

/* Unpadded, per RV 4.1a. */
PPCP_API ppcp_result ppcp_base64url_encode(const uint8_t *in, size_t in_len,
                                           char *out, size_t cap, size_t *out_len);
PPCP_API ppcp_result ppcp_base64url_decode(const char *in, size_t in_len,
                                           uint8_t *out, size_t cap, size_t *out_len);

/* -------------------------------------------------------- key derivation (5.1) */

#define PPCP_RV_KEY_BYTES 32
#define PPCP_RV_SID_BYTES 16
#define PPCP_RV_PSK_MIN   16
#define PPCP_RV_PSK_MAX   32

typedef struct ppcp_rv_keys {
    uint8_t prk[PPCP_RV_KEY_BYTES];    /* 5.1c: this is what a peer persists */
    uint8_t k_tls[PPCP_RV_KEY_BYTES];  /* 5.1a: the TLS external PSK, and nothing else */
    uint8_t k_id[PPCP_RV_KEY_BYTES];   /* 5.1b: the identifiers, and nothing else */
} ppcp_rv_keys;

/* PRK = HKDF-Extract(salt = sid, IKM = psk); K_tls and K_id expand from it.
 * The pairing secret is never used directly as a protocol key (RV §5.1) —
 * domain separation is what lets an identifier be published in the clear on a
 * multicast network without revealing anything about the handshake key. */
PPCP_API ppcp_result ppcp_rv_derive(const uint8_t *sid, size_t sid_len,
                                    const uint8_t *psk, size_t psk_len,
                                    ppcp_rv_keys *out);

/* 5.1c — a peer that persists a pairing persists PRK and derives from it,
 * never the original psk.  7.4f additionally forbids persisting a PRK derived
 * from a code whose `mu` exceeded 1; that is the embedding's decision and
 * ppcp_rv_may_persist() below is the predicate for it. */
PPCP_API ppcp_result ppcp_rv_derive_from_prk(const uint8_t prk[PPCP_RV_KEY_BYTES],
                                             ppcp_rv_keys *out);

/* --------------------------------------------- resolvable identifiers (3.4, 5.3) */

#define PPCP_RV_RN_BYTES           8
#define PPCP_RV_RID_BYTES          8
#define PPCP_RV_PSK_IDENTITY_BYTES 17
#define PPCP_RV_INSTANCE_NAME_MAX  14   /* "PPCP-" + 8 hex + NUL */

/* 3.4a — regenerated on every service registration and at least every 15
 * minutes thereafter. */
#define PPCP_RV_RN_MAX_AGE_NS (900LL * 1000000000LL)

/* rid = HMAC-SHA256(K_id, "ppcp1 rid" || rn)[0..7].
 * `rn` is 8 bytes the CALLER obtained from a CSPRNG. */
PPCP_API ppcp_result ppcp_rv_rid(const uint8_t k_id[PPCP_RV_KEY_BYTES],
                                 const uint8_t rn[PPCP_RV_RN_BYTES],
                                 uint8_t rid[PPCP_RV_RID_BYTES]);

/* 3.2a — "PPCP-" followed by the first four bytes of rid in uppercase hex. */
PPCP_API ppcp_result ppcp_rv_instance_name(const uint8_t rid[PPCP_RV_RID_BYTES],
                                           char out[PPCP_RV_INSTANCE_NAME_MAX]);

/* 5.3a — the 17 octets 0x01 || rn2 || HMAC-SHA256(K_id, "ppcp1 psk-id" || rn2)[0..7].
 * `rn2` is 8 CSPRNG bytes the caller supplies, fresh per connection (5.3a), and
 * nothing stable across connections appears in the result (5.3e).
 *
 * The identity is binary and need not be valid UTF-8 (5.3f): a peer MUST NOT
 * transcode, validate as text, or truncate it. */
PPCP_API ppcp_result ppcp_rv_psk_identity(const uint8_t k_id[PPCP_RV_KEY_BYTES],
                                          const uint8_t rn2[PPCP_RV_RN_BYTES],
                                          uint8_t identity[PPCP_RV_PSK_IDENTITY_BYTES]);

PPCP_API ppcp_result ppcp_rv_psk_identity_parse(const uint8_t *identity, size_t len,
                                                uint8_t rn2[PPCP_RV_RN_BYTES],
                                                uint8_t tag[PPCP_RV_RID_BYTES]);

/* 5.3a1 (erratum E21) — NO OCTET OF THE IDENTITY MAY BE 0x00.
 *
 * Several widely-used TLS stacks carry a PSK identity as a C string and take
 * its length with strlen: an embedded zero truncates it, the server resolves
 * nothing, and the handshake fails INTERMITTENTLY — one connection in sixteen,
 * because 17 octets each have a 1-in-256 chance of being zero.  That is
 * diagnosed at a driving range as a network fault.
 *
 * ppcp_rv_psk_identity_usable() answers whether a computed identity is safe.
 * ppcp_rv_psk_identity_draw() is the one to call: give it a CSPRNG (the library
 * owns none) and it draws `rn2` until neither it nor the resulting tag carries
 * a zero — 1.07 draws on average — leaving `rn2` with better than 63 bits of
 * entropy.  Nothing at the server changes: 5.3b recomputes the tag from the
 * `rn2` it received exactly as before.
 *
 * ppcp_rv_psk_identity() is unchanged and does NOT reject a zero-bearing draw,
 * because §10.2's vector must still reproduce byte for byte.  It is the wrong
 * entry point for a live connection. */
PPCP_API bool ppcp_rv_psk_identity_usable(const uint8_t identity[PPCP_RV_PSK_IDENTITY_BYTES]);

/* Fills `out` with PPCP_RV_RN_BYTES of CSPRNG output.  Returns false if it
 * cannot, which aborts the draw rather than falling back to anything. */
typedef bool (*ppcp_rv_random_fn)(void *ctx, uint8_t *out, size_t len);

PPCP_API ppcp_result ppcp_rv_psk_identity_draw(const uint8_t k_id[PPCP_RV_KEY_BYTES],
                                               ppcp_rv_random_fn random_fn, void *ctx,
                                               uint8_t rn2[PPCP_RV_RN_BYTES],
                                               uint8_t identity[PPCP_RV_PSK_IDENTITY_BYTES]);

/* ------------------------------------------------------------- the resolver */

/* One held pairing: outstanding codes and persisted pairings alike (5.3b).
 * `user` is whatever the embedding needs to find the pairing again — an index,
 * a pointer, a row id.  The library stores nothing. */
typedef struct ppcp_rv_pairing {
    const uint8_t *k_id;   /* PPCP_RV_KEY_BYTES */
    void          *user;
} ppcp_rv_pairing;

/* 3.4b — resolve a discovered advertisement.  Returns PPCP_ERR_NOT_FOUND when
 * no held pairing matches, which 3.4c makes a refusal to connect. */
PPCP_API ppcp_result ppcp_rv_resolve_rid(const ppcp_rv_pairing *pairings, size_t count,
                                         const uint8_t rn[PPCP_RV_RN_BYTES],
                                         const uint8_t rid[PPCP_RV_RID_BYTES],
                                         size_t *out_index);

/* 5.3b — resolve an offered PSK identity by recomputing the tag with each held
 * K_id.  Every pairing is tried, with no early exit and a constant-time
 * comparison, because 5.3c requires an unresolvable identity and a wrong key to
 * fail uniformly and 5.3d asks for them to be indistinguishable in timing. */
PPCP_API ppcp_result ppcp_rv_resolve_psk_identity(const ppcp_rv_pairing *pairings,
                                                  size_t count,
                                                  const uint8_t *identity, size_t len,
                                                  size_t *out_index);

/* -------------------------------------------------------- the payload (4.3) */

#define PPCP_RV_MAX_ENDPOINTS 8
#define PPCP_RV_DN_MAX        64    /* 4.3: at most 64 bytes; 4.4d: untrusted */
#define PPCP_RV_MAX_PAYLOAD   1024  /* 4.5a guides under 400; this is the hard cap */
#define PPCP_RV_MAX_URI       (5 + ((PPCP_RV_MAX_PAYLOAD + 2) / 3) * 4 + 1)

typedef struct ppcp_rv_endpoint {
    const char *h;      /* literal address or hostname; points into the payload buffer */
    size_t      h_len;
    uint16_t    p;      /* TCP port */
} ppcp_rv_endpoint;

/* RV §6.  `s` is the network name, `k` the passphrase (absent means open),
 * `h` whether the network is hidden (default false). */
typedef struct ppcp_rv_wifi {
    const char *s;      size_t s_len;
    bool        has_k;
    const char *k;      size_t k_len;
    bool        has_h;
    bool        h;
} ppcp_rv_wifi;

typedef struct ppcp_rv_payload {
    uint64_t         v;                 /* 4.2a: the first key, always */
    bool             has_dn;
    const char      *dn;   size_t dn_len;
    ppcp_rv_endpoint ep[PPCP_RV_MAX_ENDPOINTS];
    size_t           ep_count;          /* 1..n, most preferred first */
    bool             has_mu;
    uint64_t         mu;                /* default 1 */
    bool             has_exp;
    uint64_t         exp;               /* seconds since the Unix epoch */
    uint8_t          psk[PPCP_RV_PSK_MAX];
    size_t           psk_len;           /* 16 or 32 */
    uint8_t          sid[PPCP_RV_SID_BYTES];
    bool             has_wifi;
    ppcp_rv_wifi     wifi;
} ppcp_rv_payload;

PPCP_API void ppcp_rv_payload_init(ppcp_rv_payload *p);
PPCP_API ppcp_result ppcp_rv_payload_add_endpoint(ppcp_rv_payload *p, const char *host,
                                                  size_t host_len, uint16_t port);
PPCP_API ppcp_result ppcp_rv_payload_set_secret(ppcp_rv_payload *p,
                                                const uint8_t *psk, size_t psk_len,
                                                const uint8_t sid[PPCP_RV_SID_BYTES]);
/* 4.3 / 4.4d — at most 64 bytes, enforced here so an over-long name is a
 * failed construction rather than a code nobody can scan.  Untrusted display
 * text: never an identifier, a trust signal or a storage key. */
PPCP_API ppcp_result ppcp_rv_payload_set_display_name(ppcp_rv_payload *p,
                                                      const char *dn, size_t dn_len);
/* 7.3a — the maximum PAIRINGS this code may establish.  Default 1; see
 * ppcp_rv_may_persist() for what a value above 1 costs (7.4f).
 *
 * ⚠ PAIRINGS, NOT HANDSHAKES (erratum E3, F-H6-1).  One pairing is one derived
 * `K_tls` and therefore one link, and a link is TWO TLS handshakes — three with
 * a preview channel — because CORE §3.1 and ENC §2.1 give every channel its own
 * connection.  An embedding that decremented a counter per handshake would
 * spend a `mu: 1` code on the control channel and refuse the bulk channel of
 * the same link.  Count links.
 *
 * Exhausting `mu` invalidates the CODE and not the pairings established from it
 * (7.3f), so reconnection within a session (7.5) still works from a `mu: 1`
 * code.  This library holds no counter: 7.3a is the publisher's to enforce, and
 * the publisher is the embedding. */
PPCP_API ppcp_result ppcp_rv_payload_set_max_uses(ppcp_rv_payload *p, uint64_t mu);
/* 7.3c — the shortest expiry the workflow tolerates, seconds since the Unix
 * epoch.  Secondary to 7.3a and 7.3b, which are clock-free. */
PPCP_API ppcp_result ppcp_rv_payload_set_expiry(ppcp_rv_payload *p, uint64_t exp_unix_s);
/* RV §6 — `s` is mandatory, `k` absent means an open network, `h` defaults
 * false.  Once credentials are in the code, a photograph of the code is a
 * photograph of the passphrase (6c). */
PPCP_API ppcp_result ppcp_rv_payload_set_wifi(ppcp_rv_payload *p, const ppcp_rv_wifi *w);

PPCP_API ppcp_result ppcp_rv_payload_validate(const ppcp_rv_payload *p);

/* Deterministic CBOR, always (4.3a): a given pairing reproduces a byte-identical
 * code.  `v` comes out first by construction, because every other top-level key
 * is at least two characters (4.3b) and RFC 8949 §4.2.1 sorts by encoded
 * length first. */
PPCP_API ppcp_result ppcp_rv_payload_encode(const ppcp_rv_payload *p, uint8_t *out,
                                            size_t cap, size_t *out_len);

/* Strings in `out` point into `in`, which must outlive it.
 *
 * A `v` this library does not implement returns PPCP_ERR_VERSION_NEWER and
 * fills in nothing else — 4.2b requires the user to be told the code needs a
 * newer application rather than being shown a generic failure, and 4.2d forbids
 * acting on any other field of such a payload. */
PPCP_API ppcp_result ppcp_rv_payload_decode(const uint8_t *in, size_t in_len,
                                            ppcp_rv_payload *out);

/* `ppcp:<base64url(payload)>`, unpadded (4.1a).  The scheme does not change
 * between payload versions (4.1b) and is never http(s) (4.1c). */
PPCP_API ppcp_result ppcp_rv_uri_encode(const ppcp_rv_payload *p, char *out, size_t cap,
                                        size_t *out_len);

/* `scratch` receives the decoded CBOR and must outlive `out`. */
PPCP_API ppcp_result ppcp_rv_uri_decode(const char *uri, size_t uri_len,
                                        uint8_t *scratch, size_t scratch_cap,
                                        ppcp_rv_payload *out);

/* 4.3e — `sid` is the 16 raw bytes of a UUID and `Session.id` is its canonical
 * lowercase text form.  Peers MUST NOT use any other textual encoding: two
 * implementations choosing differently would duplicate every Capture in a
 * re-imported session (CORE 8.5c). */
#define PPCP_RV_SESSION_ID_CHARS 37   /* 36 + NUL */
PPCP_API ppcp_result ppcp_rv_sid_to_session_id(const uint8_t sid[PPCP_RV_SID_BYTES],
                                               char out[PPCP_RV_SESSION_ID_CHARS]);
PPCP_API ppcp_result ppcp_rv_session_id_to_sid(const char *text, size_t len,
                                               uint8_t sid[PPCP_RV_SID_BYTES]);

/* ---------------------------------------------------------------- expiry */

typedef enum ppcp_rv_clock_trust {
    /* 4.4a — a peer whose wall clock it has reason to trust. */
    PPCP_RV_CLOCK_TRUSTED = 0,
    /* 4.4a1 — positive reason to distrust: never synchronised since boot, or
     * reading earlier than the software's own build date. */
    PPCP_RV_CLOCK_UNTRUSTED
} ppcp_rv_clock_trust;

typedef enum ppcp_rv_expiry {
    PPCP_RV_EXPIRY_OK = 0,
    PPCP_RV_EXPIRY_EXPIRED,           /* 4.4a: refuse, and report as expired */
    PPCP_RV_EXPIRY_POSSIBLY_EXPIRED   /* 4.4a1: attempt anyway, report as possibly */
} ppcp_rv_expiry;

/* The decision, not the policy: the caller says what it thinks of its own
 * clock, and this returns which of the three 4.4a/4.4a1 outcomes applies.
 * The publisher holds the authoritative clock and enforces `exp` itself
 * (7.3e), which is what lets a device with a wrong clock at a range attempt
 * the pairing rather than be locked out. */
PPCP_API ppcp_result ppcp_rv_check_expiry(const ppcp_rv_payload *p, uint64_t now_unix_s,
                                          ppcp_rv_clock_trust trust,
                                          ppcp_rv_expiry *out);

/* 7.4f — a pairing established from a code whose `mu` exceeded 1 is
 * session-scoped and its PRK is never persisted, because every peer that
 * scanned that code holds identical key material. */
PPCP_API bool ppcp_rv_may_persist(const ppcp_rv_payload *p);

/* -------------------------------------------------- RV-6 bootstrap (RV §11) */

#define PPCP_RV_BS_KEY_BYTES 32   /* pk, the private scalar and Z (11.11a) */
#define PPCP_RV_BS_CT_BYTES  32   /* the commitment, SHA-256 (11.5b)       */
#define PPCP_RV_BS_MAC_BYTES 16   /* mac_i / mac_a, truncated (11.5f)      */

/* X25519 IS NOT HERE AND NEVER WILL BE — RV 11.11, and the same split as
 * plan A7 and A8.  Two values in RV §11 need key agreement, a peer's own
 * public key and the shared secret Z, and both are PARAMETERS exactly as psk,
 * sid, rn and rn2 already are.  The embedding computes them with the crypto
 * it already links.  Everything below is SHA-256, HMAC and HKDF, which this
 * library carries, so plan A1's "no dependencies" is untouched.
 *
 * 11.11f is on the CALLER and is not optional.  An agreement that FAILS and
 * one that returns an all-zero Z are the same event: OpenSSL fails the call,
 * CryptoKit throws, something else may return zeros.  Map either to
 * invalid_key (11.6b) — never to a transport error, and never retry it.
 * ppcp_rv_bootstrap_derive() catches the zero half; only the caller can see
 * the other half. */

/* ct = SHA-256("ppcp1 bs-commit" || pk_i) — 11.5b.  No key agreement. */
PPCP_API void ppcp_rv_bs_commit(const uint8_t pk_i[PPCP_RV_BS_KEY_BYTES],
                                uint8_t ct[PPCP_RV_BS_CT_BYTES]);

/* Constant-time compare, for 11.5d's commitment check and 11.5f's MACs.
 * Both are MUSTs and both are trivially got wrong with memcmp. */
PPCP_API bool ppcp_rv_ct_equal(const uint8_t *a, const uint8_t *b, size_t len);

/* Everything downstream of Z — 11.6c..11.6e — as one pure function. */
typedef struct ppcp_rv_bootstrap {
    /* --- ephemeral: erase when the handshake ends, success OR failure ------ */
    uint8_t  bk      [PPCP_RV_KEY_BYTES];     /* RT-18 asserts this row (R-16) */
    uint8_t  sas_raw [4];                     /* RT-18 asserts this row (R-16) */
    uint32_t sas;                             /* 0..999999; render "%06u" (11.7a) */
    uint8_t  k_c     [PPCP_RV_KEY_BYTES];
    uint8_t  mac_i   [PPCP_RV_BS_MAC_BYTES];
    uint8_t  mac_a   [PPCP_RV_BS_MAC_BYTES];
    /* --- persistable, and ONLY after 11.5g -------------------------------- */
    uint8_t  sid     [PPCP_RV_SID_BYTES];     /* version/variant bits set (11.6d) */
    uint8_t  prk     [PPCP_RV_KEY_BYTES];
    uint8_t  k_tls   [PPCP_RV_KEY_BYTES];
    uint8_t  k_id    [PPCP_RV_KEY_BYTES];
} ppcp_rv_bootstrap;

/* ⛔ THE STRUCT MIXES WHAT MUST BE ERASED WITH WHAT MAY BE KEPT, AND THE
 * NATURAL THING TO DO WITH IT IS WRONG.  A caller keeps it because it holds
 * the PRK — and keeps k_c and the digits alive with it, against 11.6f and
 * 11.7f.  Copy out sid/prk/k_tls/k_id, then wipe.  On EVERY exit path:
 * 11.6f as amended by E51 erases prk/k_tls/k_id/sid too on a handshake that
 * FAILED, and a peer holds all of them from the moment it has Z — up to the
 * 60 seconds 11.3e allows before either user has affirmed and the pairing
 * exists at all (11.5g). */
PPCP_API void ppcp_rv_bootstrap_wipe(ppcp_rv_bootstrap *out);

/* `v` is 1..255 (11.4h1).  pk_i and pk_a are INITIATOR FIRST (11.6c): the
 * order is bound into the transcript, and transposing it is one of the six
 * causes RV §10.4 lists.
 *
 * TWO DISTINGUISHABLE FAILURES, and the distinction matters.  An all-zero `z`
 * is PPCP_ERR_RV_INVALID_KEY — 11.6b's ATTACK SIGNAL, never retried and never
 * reported as a transport error.  A `v` outside 1..255 is PPCP_ERR_MALFORMED,
 * a programming error.  Returning one code for both would report a caller's
 * bug as an attack (R-18).
 * The transcript is bound into sas and k_c and into NOTHING else — 11.6c1,
 * and 11.6c2 forbids dropping pk_i||pk_a on the grounds that Z implies them.
 * The caller erases `z` and its own scalar afterwards (11.11h). */
PPCP_API ppcp_result ppcp_rv_bootstrap_derive(const uint8_t z[PPCP_RV_BS_KEY_BYTES],
                                              uint8_t v,
                                              const uint8_t pk_i[PPCP_RV_BS_KEY_BYTES],
                                              const uint8_t pk_a[PPCP_RV_BS_KEY_BYTES],
                                              ppcp_rv_bootstrap *out);

#ifdef __cplusplus
}
#endif
#endif /* PPCP_RV_H */
