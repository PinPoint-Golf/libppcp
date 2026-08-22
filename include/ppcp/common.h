/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Mark Liversedge
 *
 * common.h — result codes, the API marker, and the identifier type.
 *
 * Nothing in libppcp allocates.  Every function here writes into storage the
 * caller owns and tells the caller how much it used, because ENC 3a requires a
 * receiver to reject an oversized frame *without allocating for it* and the
 * cheapest way to guarantee that is to have no allocator at all.
 */
#ifndef PPCP_COMMON_H
#define PPCP_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Marker on every public entry point.  It is the port surface (plan A3): a
 * symbol without it is not part of the contract the applications bind to. */
#if defined(_WIN32) && defined(PPCP_DLL)
#  define PPCP_API __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#  define PPCP_API __attribute__((visibility("default")))
#else
#  define PPCP_API
#endif

/* Result codes.
 *
 * ENC 8a and 8b are why there are two limit codes rather than one: a
 * payload_len beyond the channel's limit means the stream has desynchronised
 * and cannot be resynchronised, so it is fatal; every other breach is answered
 * with `error` and the session keeps capturing. */
typedef enum ppcp_result {
    PPCP_OK = 0,

    PPCP_ERR_MALFORMED,          /* ENC 4a/4c/4d, structural violation */
    PPCP_ERR_LIMIT,              /* ENC §8 breach, non-fatal (8b) */
    PPCP_ERR_FATAL_LIMIT,        /* ENC 8a — frame length beyond the channel limit */
    PPCP_ERR_TRUNCATED,          /* more bytes needed; ENC 3c decides if fatal */
    PPCP_ERR_NOSPACE,            /* caller's output buffer is too small */
    PPCP_ERR_INVALID,            /* argument would violate a structural invariant */
    PPCP_ERR_NOT_FOUND,          /* no held pairing resolved; key absent */
    PPCP_ERR_VERSION_NEWER,      /* RV 4.2b — the code needs a newer application */
    PPCP_ERR_EXPIRED,            /* RV 4.4a — code past its `exp` */
    PPCP_ERR_UNIMPLEMENTED       /* declared in planned.h, not yet built */
} ppcp_result;

/* Stable, human-readable, and safe to log: no result code carries payload. */
PPCP_API const char *ppcp_result_str(ppcp_result r);

PPCP_API const char *ppcp_library_version(void);
PPCP_API const char *ppcp_wire_version(void);

/* CORE 5.1 `Id` — opaque UTF-8, 1..64 bytes, unique within its stated scope.
 *
 * Held by value and NUL-terminated for the embedding's convenience; the NUL is
 * not part of the identifier and is never encoded.  CORE 5.1a forbids deriving
 * an Id from mutable local state, which this type cannot enforce — it is a
 * property of where the bytes came from, and it is stated in the header that
 * mints them. */
#define PPCP_ID_MAX 64

typedef struct ppcp_id {
    char   v[PPCP_ID_MAX + 1];
    uint8_t len;
} ppcp_id;

/* The only way to fill a ppcp_id.  Rejects an empty identifier and one over 64
 * bytes; an over-long Id is PPCP_ERR_INVALID on the way out and
 * PPCP_ERR_MALFORMED on the way in, which is the difference between a caller
 * bug and a peer's. */
PPCP_API ppcp_result ppcp_id_set(ppcp_id *id, const char *s, size_t len);
PPCP_API ppcp_result ppcp_id_set_z(ppcp_id *id, const char *s);
PPCP_API bool        ppcp_id_is_set(const ppcp_id *id);
PPCP_API bool        ppcp_id_equal(const ppcp_id *a, const ppcp_id *b);

/* A bump region over caller storage.
 *
 * Not an allocator: it hands out slices of a buffer the caller already owns and
 * has no free().  It exists because CORE §5's aggregates nest — a Peer holds
 * Sources, each holding CaptureProfiles — and a decoder that never allocates
 * still has to put them somewhere.  The caller sizes the region; running out is
 * PPCP_ERR_LIMIT on the way in, which is ENC 3a's "reject before allocating"
 * expressed as "there was never anything to allocate from". */
typedef struct ppcp_arena {
    uint8_t *buf;
    size_t   cap;
    size_t   used;
} ppcp_arena;

PPCP_API void  ppcp_arena_init(ppcp_arena *a, void *buf, size_t cap);
PPCP_API void  ppcp_arena_reset(ppcp_arena *a);
PPCP_API void *ppcp_arena_take(ppcp_arena *a, size_t count, size_t elem_size, size_t align);
PPCP_API size_t ppcp_arena_used(const ppcp_arena *a);

/* Constant-time comparison, for the two places RV needs one: resolving a `rid`
 * (3.4b) and resolving a PSK identity tag (5.3b), where 5.3c requires the
 * unresolvable and wrong-key cases to fail uniformly and 5.3d asks for them to
 * be indistinguishable in timing too. */
PPCP_API bool ppcp_ct_equal(const void *a, const void *b, size_t n);

#ifdef __cplusplus
}
#endif
#endif /* PPCP_COMMON_H */
