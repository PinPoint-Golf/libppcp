/* The decode arena holds a whole clip's `payload_begin`.
 *
 * 1 September 2026: the arena was 8 KiB.  A two-second clip's achieved_frames
 * (~495 frames, instant + exposure) squeezed in; a three-second clip's (~720)
 * did not, the frame was "undecodable", and every clip a host ASKED for --
 * 2 s pre-roll + 1 s post-roll -- was lost while the phone's own 2 s clips
 * arrived.  This pins the capacity to the largest clip a ten-second ring at
 * 240 fps can produce, with every optional per-frame series present. */
#include "ppcp/cbor.h"
#include "ppcp/frame.h"
#include "ppcp/message.h"
#include "ppcp/model.h"
#include "ppcp/peer.h"
#include "ppcp/timing.h"

#include "test_util.h"

#include <string.h>

enum { FRAMES = 2400 };   /* CORE 5.21: a ten-second ring, at 240 fps */

static int64_t      frames_ns[FRAMES];
static int64_t      exposure_ns[FRAMES];
static int64_t      iso[FRAMES];
static ppcp_matrix3 intrinsics[FRAMES];
static uint8_t      wire[PPCP_LIMIT_BULK_FRAME];
static uint8_t      arena_buf[PPCP_PEER_SCRATCH_ARENA];

static void test_a_full_ring_of_frames_decodes_into_the_peer_arena(void)
{
    ppcp_achieved_frames af;
    ppcp_per_frame_i64   e, i64;
    ppcp_per_frame_m3    m3;
    ppcp_msg             m, d;
    ppcp_digest          dg;
    uint8_t              v[PPCP_SHA256_BYTES];
    size_t               written = 0, consumed = 0, k;
    ppcp_frame_header    h;
    const uint8_t       *payload = NULL;
    ppcp_arena           a;

    TEST("a 2400-frame payload_begin with every per-frame series decodes");
    for (k = 0; k < FRAMES; k++) {
        frames_ns[k]   = 1000000 + (int64_t)k * 4167;
        exposure_ns[k] = 2000 + (int64_t)(k % 7);
        iso[k]         = 400 + (int64_t)(k % 3);
        memset(&intrinsics[k], 0, sizeof(intrinsics[k]));
        intrinsics[k].m[0] = 1500.0 + (double)k; intrinsics[k].m[4] = 1500.0;
        intrinsics[k].m[2] = 960.0; intrinsics[k].m[5] = 540.0; intrinsics[k].m[8] = 1.0;
    }
    CHECK_EQ_I(ppcp_achieved_frames_make(&af, "tb:dev", frames_ns, FRAMES), PPCP_OK);
    CHECK_EQ_I(ppcp_per_frame_i64_array(&e, exposure_ns, FRAMES), PPCP_OK);
    CHECK_EQ_I(ppcp_achieved_frames_set_exposure(&af, &e, PPCP_EXP_PER_FRAME), PPCP_OK);
    CHECK_EQ_I(ppcp_per_frame_i64_array(&i64, iso, FRAMES), PPCP_OK);
    CHECK_EQ_I(ppcp_achieved_frames_set_iso(&af, &i64), PPCP_OK);
    CHECK_EQ_I(ppcp_per_frame_m3_array(&m3, intrinsics, FRAMES), PPCP_OK);
    CHECK_EQ_I(ppcp_achieved_frames_set_intrinsics(&af, &m3), PPCP_OK);

    memset(v, 0x5a, sizeof(v));
    memset(&dg, 0, sizeof(dg));
    CHECK_EQ_I(ppcp_digest_set(&dg, v), PPCP_OK);
    CHECK_EQ_I(ppcp_msg_init(&m, PPCP_MT_PAYLOAD_BEGIN, 7), PPCP_OK);
    CHECK_EQ_I(ppcp_id_set_z(&m.body.payload_begin.capture_id, "cap:three-seconds"), PPCP_OK);
    m.body.payload_begin.bytes               = 22028256;
    m.body.payload_begin.digest              = dg;
    m.body.payload_begin.chunk_bytes         = PPCP_DEFAULT_CHUNK_BYTES;
    m.body.payload_begin.has_achieved_frames = true;
    m.body.payload_begin.achieved_frames     = af;

    CHECK_EQ_I(ppcp_msg_encode(wire, sizeof(wire), PPCP_CHANNEL_BULK, &m, &written), PPCP_OK);
    CHECK(written > 0);
    CHECK_EQ_I(ppcp_frame_read(wire, written, &h, &payload, &consumed), PPCP_OK);
    CHECK_EQ_I(consumed, written);

    /* Exactly what ppcp_peer_feed does with a bulk frame: the channel's CBOR
     * limits, and the peer's scratch arena. */
    ppcp_arena_init(&a, arena_buf, sizeof(arena_buf));
    memset(&d, 0, sizeof(d));
    CHECK_EQ_I(ppcp_msg_decode(payload, h.payload_len,
                               ppcp_cbor_limits_for_channel(PPCP_CHANNEL_BULK), &a, &d),
               PPCP_OK);
    CHECK_EQ_I(d.type, PPCP_MT_PAYLOAD_BEGIN);
    CHECK(d.body.payload_begin.has_achieved_frames);
    CHECK_EQ_I(d.body.payload_begin.achieved_frames.frame_count, FRAMES);
    if (d.body.payload_begin.achieved_frames.frame_count == FRAMES) {
        int64_t got = 0;
        CHECK_EQ_I(d.body.payload_begin.achieved_frames.frames_ns[FRAMES - 1],
                   frames_ns[FRAMES - 1]);
        CHECK_EQ_I(ppcp_achieved_frames_exposure_at(&d.body.payload_begin.achieved_frames,
                                                    FRAMES - 1, &got), PPCP_OK);
        CHECK_EQ_I(got, exposure_ns[FRAMES - 1]);
    }
}

int main(void)
{
    test_a_full_ring_of_frames_decodes_into_the_peer_arena();
    TEST_MAIN_END();
}
