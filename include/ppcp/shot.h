/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * shot.h — Detect, Mint and Arbitrate.  Work package L10.
 *
 * CORE §5.12, §5.13, §5.16, §8; MSG §7, §9.3.
 *
 * THREE PROFILES, THREE OBJECTS, AND THE SPLIT IS THE POINT.
 *
 *   Detect     nominates.  ppcp_peer_nominate() emits a Candidate, and EVERY
 *              nomination is emitted — winners, losers, and ones the peer's
 *              own detector does not believe (7.1d, I8).
 *   Mint       promotes.  ppcp_mint holds this peer's own Candidates until it
 *              is entitled to mint (8.2i) or until a `shot` answers them, and
 *              promotes only what its own policy would have promoted hostless
 *              (I32).
 *   Arbitrate  decides.  ppcp_arbiter is available to a peer with `role: host`
 *              and to no other (I20).
 *
 * ⚠ WHAT IS NOT IN HERE, AND WILL NOT BE.
 *
 *   - No promotion threshold and no arbitration threshold.  Both are callbacks
 *     (I14).  "Which candidates a Mint peer promotes" is explicitly outside
 *     conformance (CONF §6), and a library holding the number would have taken
 *     the decision the detector is supposed to take.
 *   - No merge.  A Shot is never rewritten and two Shots are never combined
 *     (I9, 8.5a); the one amendment 5.13d permits is EXTENDING `candidates`,
 *     which is ppcp_shot_attach_candidate() and ppcp_shot_adopt_extension()
 *     below, and both refuse any other difference.
 *   - No second canonical-instant conversion.  `Candidate.at` arrives
 *     canonical because the nominator converted it (5.12e, I33); the arbiter
 *     applies a TimebaseRelation and nothing else (8.2a).  A host that
 *     converted again would double the correction, and the error is
 *     exposure-dependent, which is why it looks like clock bias.
 */
#ifndef PPCP_SHOT_H
#define PPCP_SHOT_H

#include "ppcp/peer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------ minting ids
 *
 * 8.3e: Shot ids are unique within the Session and SHOULD be UUIDs, and a peer
 * MUST NOT mint an id in another peer's namespace.  The library has no random
 * source (ground rule 8) so the embedding supplies them, exactly as ENC 2.1a
 * does for `link_id` and RV 7.2a for the pairing nonces. */
typedef ppcp_result (*ppcp_id_fn)(void *ctx, ppcp_id *out);

/* I14 — promotion is a callback, not a threshold, and I32's negative half
 * depends on the peer having a policy of its own to decline with. */
typedef bool (*ppcp_promotion_policy)(void *ctx, const ppcp_candidate *c);

/* 8.2d — exclude-and-retain.  Called only where a relation EXISTS: a missing
 * or `unrelated` relation is decided by the specification, not by policy.
 * `sigma_ns` is the conversion's uncertainty at this Candidate's instant, so a
 * host with a policy on "too uncertain" has the number it needs and the
 * library still holds none.  Returning false excludes the Candidate from
 * setting `t0`; it stays in `Shot.candidates` either way (I8). */
typedef bool (*ppcp_arbitration_policy)(void *ctx, const ppcp_candidate *c,
                                        const ppcp_timebase_relation *rel,
                                        double sigma_ns);

/* ==================================================== CORE §5.12 — Detect */

/* I33 / 5.12e — builds a Candidate whose `at` is the CANONICAL instant.
 *
 * The conversion is done HERE, by the nominator, because it needs that frame's
 * exposure duration and a Candidate carries neither a frame reference nor an
 * exposure: `evidence_capture_id` points at the audio window, and there is no
 * route from a Candidate back to the exposure of the frame it came from.  The
 * nominating peer is the only party holding both.
 *
 * `profile` is the CaptureProfile of the Source named by `src`, and supplies
 * `timing.convention` and, for `nominal_frame_start`, the offset.  It may be
 * NULL for a Source whose profile has no `format` — a microphone, an IMU —
 * where 6.1d fixes `convention: mid` and the canonical instant is the raw
 * instant; `exposure_ns` is then ignored.
 *
 * 5.12f: the applied correction is reported in `canonical_correction_ns`, on
 * the same principle as `tof_correction` — the observer corrects, and the
 * correction is visible.  It is a bare integer and not an Estimate, and that
 * asymmetry is deliberate (5.12): its trustworthiness is
 * `frame_start_to_exposure_offset_provenance`, which lives on the profile
 * under I31 and is one hop away through `source_id`.
 *
 * `tof` is the acoustic time-of-flight correction (8.1d), already APPLIED to
 * `raw_ns` by the caller; passing it here records the correction and its
 * dispersion.  NULL where there was none.  I29 is in the type: the parameter
 * is a ppcp_estimate and ppcp_estimate_make is the only way to obtain one. */
PPCP_API ppcp_result ppcp_candidate_make_canonical(ppcp_candidate *out, const char *id,
                                                   const ppcp_source *src,
                                                   const ppcp_capture_profile *profile,
                                                   const char *basis, int64_t raw_ns,
                                                   ppcp_duration_ns exposure_ns,
                                                   double confidence,
                                                   const ppcp_estimate *tof);

/* Emits a Candidate.  7.1d: EVERY nomination — one this peer promotes, one it
 * does not, and one a host later excludes.  Withholding a loser destroys the
 * only evidence that explains why detection fired.
 *
 * I26 / 5.12a / 7.1a is checked here and refused before a wire sees it:
 * `source_id` names a Source THIS peer declared, that Source names a Timebase
 * it declared, and `at` is expressed in that timebase.  A record with no peer,
 * no timebase and no clock is not a Candidate at all (7.1c, 8.1b) — it is a
 * `shot_link`, and there is no path from here to one. */
PPCP_API ppcp_result ppcp_peer_nominate(ppcp_peer *p, const ppcp_candidate *c);

/* MSG 7.2 — `shot`, from the issuer OR from a peer attaching Candidates to a
 * Shot it did not issue (7.2g). */
PPCP_API ppcp_result ppcp_peer_shot(ppcp_peer *p, const ppcp_shot *s);
/* MSG 9.3 — `shot_link`.  A Core message: `arrival_pairing` and
 * `shared_candidate` links are asserted live by peers that may implement no
 * bundle handling at all (9.3g). */
PPCP_API ppcp_result ppcp_peer_shot_link(ppcp_peer *p, const ppcp_shot_link *l);

/* ===================================================== CORE §5.13 — the Shot */

/* 8.2e / 5.13d — a Candidate arriving after the Shot was issued ATTACHES.
 * `t0` is not revised (I7) and this function cannot revise it: it takes a
 * Candidate, not an instant.
 *
 * The list is kept sorted by `id`, so 5.13e's "additive and order-independent"
 * is byte-identical convergence rather than merely set equality — which is
 * what CT-I35's "applying two extensions in either order" asks for.  A
 * Candidate already present is a no-op and PPCP_OK. */
PPCP_API ppcp_result ppcp_shot_attach_candidate(ppcp_shot *s, const ppcp_candidate *c);
PPCP_API ppcp_result ppcp_shot_attach_candidate_id(ppcp_shot *s, const ppcp_id *candidate_id);

/* 5.13d — "a peer receiving an extension to a Shot it issued MUST adopt the
 * extended list."  This is that adoption, and it is the ONLY operation in this
 * library that writes one Shot from another.
 *
 * It refuses — PPCP_ERR_INVALID — unless `id`, `t0`, `authority` and
 * `issued_by` are identical, because those four are the issuer's and are never
 * changed by another peer (5.13d, 7.2c, I7).  So it cannot be used to merge
 * two Shots, which I9 forbids; it can only grow one Shot's candidate list.
 * `captures` is extended on the same terms. */
PPCP_API ppcp_result ppcp_shot_adopt_extension(ppcp_shot *dst, const ppcp_shot *incoming);

/* ⚠ There is deliberately no ppcp_shot_merge(), no ppcp_session_merge() and no
 * ppcp_candidate_merge().  I9: reconciliation creates links; no entity is
 * rewritten.  CT-I9 asserts the absence by API surface (tests/api_surface.cmake). */

/* ======================================================= CORE §8.2i — Mint */

typedef struct ppcp_mint ppcp_mint;

#define PPCP_MINT_MAX_PENDING 32

PPCP_API size_t ppcp_mint_sizeof(void);

/* The peer supplies the Session parameters, the relations and the transport.
 * `id_fn` mints Shot ids (8.3e).  PPCP_ERR_INVALID unless the peer declares
 * Mint: 8.3d makes issuing a Shot the Mint profile's, and a peer that minted
 * without declaring it fails CONF §1d. */
PPCP_API ppcp_result ppcp_mint_new(void *storage, size_t storage_len, ppcp_peer *p,
                                   ppcp_id_fn id_fn, void *id_ctx, ppcp_mint **out);
PPCP_API ppcp_result ppcp_mint_set_promotion_policy(ppcp_mint *m,
                                                    ppcp_promotion_policy fn, void *ctx);

/* Records a Candidate this peer nominated.  The 8.2i deadline is measured from
 * the Candidate's own instant, converted into `Session.timebase_ref` — which
 * is also 8.2i1's test: a Candidate that cannot be expressed there is retained
 * with no Shot, for ever, and that is a legal and honest state (I8). */
PPCP_API ppcp_result ppcp_mint_observe_own(ppcp_mint *m, const ppcp_candidate *c);

/* A `shot` arrived.  A pending Candidate it references is answered, and 8.2i's
 * deadline no longer applies to it — "with no `shot` referencing it" is the
 * whole condition. */
PPCP_API ppcp_result ppcp_mint_observe_shot(ppcp_mint *m, const ppcp_shot *s);

/* Mints what is due, and sends each `shot` immediately (8.2j).  `now_ref_ns`
 * is a reading of `Session.timebase_ref`, because that is the frame both the
 * deadline and `t0` live in.
 *
 * With NO arbitrating host — a hostless Session, or a host unreachable for
 * three heartbeat intervals (8.3g) — there is no deadline and no coincidence
 * window: a promoted Candidate becomes a Shot carrying exactly that one
 * Candidate, `authority: device` (8.3a, I23).
 *
 * With a host, a Candidate becomes eligible `issue_hold_ns +
 * heartbeat_interval_ms` after its instant, and then ONLY if the promotion
 * policy would have promoted it hostless (8.2i, I32).  Host silence is not a
 * promotion: nothing obliges a host to answer a Candidate it declined, so the
 * silent branch is the one that fires for every nomination it rejected. */
PPCP_API ppcp_result ppcp_mint_pump(ppcp_mint *m, int64_t now_ref_ns, size_t *out_minted);

PPCP_API size_t ppcp_mint_pending_count(const ppcp_mint *m);
PPCP_API size_t ppcp_mint_minted_count(const ppcp_mint *m);

/* The Shots this engine minted, in mint order — `index` runs 0 ..
 * ppcp_mint_minted_count()-1 and NULL is past the end.  The twin of
 * ppcp_arbiter_shot_at() below, and it was missing: F-D5-1 found
 * PinPointCapture decoding its OWN queued frames through drain_peek to learn
 * what a pump had just minted, because the pump reported only how many.
 *
 * Read-only, and `t0` has no setter anywhere in this library — I7 by surface
 * as well as by behaviour.  Valid until the next ppcp_mint_pump(). */
PPCP_API const ppcp_shot *ppcp_mint_shot_at(const ppcp_mint *m, size_t index);
/* The Shot a particular Candidate of this peer's became, or NULL if it has not
 * been minted — answered by a host (8.2i), declined by the promotion policy
 * (I32), or still inside its deadline. */
PPCP_API const ppcp_shot *ppcp_mint_shot_for(const ppcp_mint *m,
                                             const ppcp_id *candidate_id);
/* Candidates held with no Shot referencing them — declined by policy, answered
 * by nobody, or inexpressible in `timebase_ref` (8.2i1).  I8: they are still
 * evidence, and a consumer may re-derive t0 later with a better clock. */
PPCP_API size_t ppcp_mint_retained_count(const ppcp_mint *m);

/* ==================================================== CORE §8.2 — Arbitrate */

typedef struct ppcp_arbiter ppcp_arbiter;

#define PPCP_ARBITER_MAX_GROUPS   16
#define PPCP_ARBITER_MAX_RETAINED 64

PPCP_API size_t ppcp_arbiter_sizeof(void);

/* I20 — arbitration is available only to a peer with `role: host`, and to a
 * peer declaring Arbitrate.  Any other peer is PPCP_ERR_INVALID here, which is
 * CT-I20's "a non-host peer attempting to arbitrate is refused". */
PPCP_API ppcp_result ppcp_arbiter_new(void *storage, size_t storage_len, ppcp_peer *p,
                                      ppcp_id_fn id_fn, void *id_ctx,
                                      ppcp_arbiter **out);
PPCP_API ppcp_result ppcp_arbiter_set_policy(ppcp_arbiter *a,
                                             ppcp_arbitration_policy fn, void *ctx);

/* Every Candidate: this host's own, and every one that arrives.  8.2a converts
 * it into `Session.timebase_ref` with the CURRENT relation set and compares
 * nothing until it has.
 *
 * `out_excluded`, where non-NULL, says whether 8.2d excluded it — a missing
 * relation, an `unrelated` one, or one the policy called too uncertain.  An
 * excluded Candidate is RETAINED: it joins the Shot's `candidates` if a Shot
 * is issued near it, and is held here if not.  Exclusion is a conclusion; the
 * Candidate remains evidence (I8).
 *
 * 8.2b: two Candidates nominate the same Shot if their converted instants fall
 * within `Session.coincidence_window_ns`.  Two of the SAME `basis` from
 * DIFFERENT peers — a host microphone and a device microphone — are both
 * retained and both appear, because there is no per-modality slot here for one
 * of them to fall out of (CT-I8). */
PPCP_API ppcp_result ppcp_arbiter_observe(ppcp_arbiter *a, const ppcp_candidate *c,
                                          bool *out_excluded);

/* A `shot` arrived.
 *
 * 8.2k: a DEVICE-minted Shot referencing a Candidate this host still holds is
 * not competed with.  The host attaches its own Candidates to it — re-sending
 * `shot` with an extended list and the UNCHANGED device `t0` — and issues
 * nothing of its own (I35).  The cost is that `t0` is the device's rather than
 * the host's arbitrated value, which is the worse estimate; that is accepted,
 * because it only arises when the host has already exceeded 8.2h's bound, and
 * one slightly worse `t0` is a much smaller harm than two Shots for one swing.
 *
 * 8.2l: where BOTH have issued — the two messages crossed — neither is
 * withdrawn.  The host emits `shot_link` with `basis: shared_candidate` and
 * `confirmed_by: observer`, which is exact by construction, and a consumer
 * MUST NOT count them as two events.
 *
 * 5.13d: an extension to a Shot this host issued is adopted. */
PPCP_API ppcp_result ppcp_arbiter_observe_shot(ppcp_arbiter *a, const ppcp_shot *s);

/* 8.2h — issues every group whose earliest contributing Candidate is at least
 * `issue_hold_ns` old, and no later than the mint deadline.  `now_ref_ns` is a
 * reading of `Session.timebase_ref`.
 *
 * Issuing early locks `t0` to whichever modality happened to be fastest, which
 * is not the same as whichever is most accurate, and I7 forbids correcting it
 * afterwards.  Issuing after the deadline overlaps the window in which the
 * nominating peer is entitled to mint, and produces two Shots for one event
 * with no defect on either side — so a group issued late is counted, and
 * ppcp_arbiter_late_count() is how a host finds out it is running slow. */
PPCP_API ppcp_result ppcp_arbiter_pump(ppcp_arbiter *a, int64_t now_ref_ns,
                                       size_t *out_issued);

PPCP_API size_t ppcp_arbiter_group_count(const ppcp_arbiter *a);
PPCP_API size_t ppcp_arbiter_issued_count(const ppcp_arbiter *a);
PPCP_API size_t ppcp_arbiter_late_count(const ppcp_arbiter *a);
/* Candidates held with no Shot: 8.2d's missing or `unrelated` relation, where
 * there is not even an instant to group by.  The interoperability pairing
 * "host ↔ peer declaring `unrelated` timebases" puts EVERY candidate from that
 * peer here, and that is the honest answer (8.2i1, CONF §5). */
PPCP_API size_t ppcp_arbiter_retained_count(const ppcp_arbiter *a);
/* The Shot a group issued, or NULL.  Read-only: `t0` has no setter anywhere in
 * this library, which is I7 by surface as well as by behaviour. */
PPCP_API const ppcp_shot *ppcp_arbiter_shot_at(const ppcp_arbiter *a, size_t group);

/* ============================================ CORE §8.4 — orphan capture requests */

/* MSG 7.3 — asks an owner for an interval around a `t0` it never nominated.
 * Arbitrate confers it (I20 again). */
PPCP_API ppcp_result ppcp_peer_capture_request(ppcp_peer *p, const char *shot_id,
                                               const ppcp_instant *t0,
                                               const ppcp_id *stream_ids, size_t count,
                                               ppcp_duration_ns pre_ns,
                                               ppcp_duration_ns post_ns);

/* 8.4b / 7.3b — the interval is no longer retained.  The answer is a Capture
 * of `completeness: absent` with `absent_reason: outside_buffer`, NOT an
 * `error`: an absent capture is a result, not a failure (I10), and absence is
 * asserted rather than inferred from a missing payload.
 *
 * `in_reply_to` is the `capture_request`'s `msg_id`, from the
 * PPCP_EVENT_CAPTURE_REQUEST event. */
PPCP_API ppcp_result ppcp_peer_capture_absent(ppcp_peer *p, const char *capture_id,
                                              const char *shot_id, const char *stream_id,
                                              const char *absent_reason,
                                              uint64_t in_reply_to);

#ifdef __cplusplus
}
#endif
#endif /* PPCP_SHOT_H */
