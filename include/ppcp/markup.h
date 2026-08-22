/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * markup.h — Annotations.  Work package L11.
 *
 * CORE §5.18; MSG §9.0.
 *
 * AN ANNOTATION IS A USER ARTEFACT, NOT AN OBSERVATION, and that distinction
 * is the point of the type existing.  Everything else in PPCP that carries
 * payload is a Capture realising a Shot, a Candidate or an interval of a
 * Stream — all of them produced by a Source, which has a clock, a calibration
 * and an owning peer.  A person has none of those.
 *
 * ⚠ I37 / 5.18c — THERE IS NO PATH FROM ANYTHING HERE TO A SHOT, A CANDIDATE,
 * A CALIBRATION OR ANY COMPUTED QUANTITY.  Not a weak one, not a convenience
 * one.  No function in this header takes an Annotation and a Shot, or an
 * Annotation and a TimebaseRelation, or returns an instant derived from one;
 * `kind: nav_anchor` in particular is never persisted or interpreted as phase
 * data.  CONF §3 says CT-I37 is asserted "by API surface, not behaviour", so
 * that absence is checked by tests/api_surface.cmake and this paragraph is
 * what the check is for.
 *
 * WHAT L4 ALREADY DID.  The Annotation type and its codec are in model.h,
 * because I24 makes every conformant peer parse an `annotation` whether or not
 * it declares Markup.  What is here is the BEHAVIOUR: supersession, the store
 * that makes both delivery orders converge, and the two placement rules.
 */
#ifndef PPCP_MARKUP_H
#define PPCP_MARKUP_H

#include "ppcp/peer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 5.18e / 9.0c — the total order.
 *
 *   > 0   `a` supersedes `b`
 *   < 0   `b` supersedes `a`
 *   = 0   neither: they are the same revision from the same author, or they
 *         are annotations with different `id`s and so nothing to do with each
 *         other.
 *
 * By `id`, then `revision`, then `author_peer_id` BYTEWISE.  The tiebreak is
 * not decoration: revision 7 of the specification claimed two peers editing
 * concurrently converge on the higher revision, and they do not — both hold
 * revision 1, both produce revision 2, each receives an equal revision and
 * ignores it, and the two ends diverge permanently while each believes it
 * converged.  A coach at a host and a golfer at a device drawing on one shot
 * is the case markup exists for, not a race.
 *
 * It is a tiebreak and not a merge, which I9 forbids. */
PPCP_API int ppcp_annotation_supersedes(const ppcp_annotation *a, const ppcp_annotation *b);

/* 5.18g / 5.18j — where an Annotation belongs.
 *
 *   5.18j  a view-specific `kind` (`line`, `plane`) carries `stream_id`; a
 *          non-view-specific one (`text`, `nav_anchor`) does not.  An
 *          unrecognised `kind` is treated as view-specific if and only if
 *          `stream_id` is present, which is the conservative default.
 *   5.18g  where `stream_id` is present, `at` is in THAT STREAM'S timebase and
 *          names a frame it contains; where it is absent, `at` is in
 *          `Session.timebase_ref`.
 *
 * `stream` is the Stream named by `a->stream_id`, or NULL where the annotation
 * carries none.  `timebase_ref` is the Session's.  PPCP_ERR_INVALID on any
 * breach; PPCP_ERR_NOT_FOUND where `stream_id` is present and `stream` is
 * NULL, which is a dangling reference rather than a placement error.
 *
 * ⚠ This checks WHERE the annotation is anchored.  It does not read `body`,
 * cannot read `body`, and produces nothing an analysis could consume (I37). */
PPCP_API ppcp_result ppcp_annotation_validate_placement(const ppcp_annotation *a,
                                                        const ppcp_id *timebase_ref,
                                                        const ppcp_stream *stream);

/* MSG 9.0a — `annotation` travels EITHER direction.  It is the only content in
 * PPCP that does; every payload message elsewhere describes a Capture the
 * sender owns.  Markup confers it, and C2 refuses a peer that has not declared
 * it — which is the negative half of CONF §1d. */
PPCP_API ppcp_result ppcp_peer_annotate(ppcp_peer *p, const ppcp_annotation *a);

/* ---------------------------------------------------------------- the store
 *
 * One current revision per `id`.  It exists because both applications need the
 * same convergence rule and it is the same rule for both (ground rule 1), and
 * because "converges in both delivery orders" is a property of a COLLECTION
 * and cannot be asserted about a comparison function alone.
 *
 * `body` is copied in, byte for byte, and returned byte for byte: 5.18a makes
 * lossless round-tripping the requirement and interpreting the format
 * explicitly not one.  A peer that does not understand a `format` stores and
 * returns it unchanged, and this is the object that does. */

#define PPCP_ANNOTATION_STORE_MAX 32

typedef struct ppcp_annotation_store ppcp_annotation_store;

PPCP_API size_t      ppcp_annotation_store_sizeof(void);
PPCP_API ppcp_result ppcp_annotation_store_new(void *storage, size_t storage_len,
                                               ppcp_annotation_store **out);

/* Applies 5.18e.  `out_replaced`, where non-NULL, says whether the store
 * changed: false means the incoming revision was superseded by what was
 * already held and was IGNORED, which is 9.0c's "lower is ignored".
 *
 * PPCP_ERR_LIMIT when the store is full; the embedding then owns a larger one.
 * PPCP_ERR_MALFORMED for a `body` over 8 KiB (5.18f) — but the codec refuses
 * that on the way in too, so it should never be reached from a wire. */
PPCP_API ppcp_result ppcp_annotation_store_observe(ppcp_annotation_store *s,
                                                   const ppcp_annotation *a,
                                                   bool *out_replaced);

PPCP_API size_t                 ppcp_annotation_store_count(const ppcp_annotation_store *s);
PPCP_API const ppcp_annotation *ppcp_annotation_store_at(const ppcp_annotation_store *s,
                                                         size_t index);
PPCP_API const ppcp_annotation *ppcp_annotation_store_find(const ppcp_annotation_store *s,
                                                           const ppcp_id *id);

#ifdef __cplusplus
}
#endif
#endif /* PPCP_MARKUP_H */
