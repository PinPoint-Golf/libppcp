# PPCP — Wire Encoding

**Framing, encoding, bulk transfer and the bundle container.**

| | |
|---|---|
| Document | `PPCP-ENC` |
| Version | **1.0** |
| Status | **APPROVED for implementation**, 22 August 2026 |
| Date | 22 August 2026 |
| Depends on | [`PPCP-CORE`](ppcp-core.md), [`PPCP-MSG`](ppcp-messages.md) |

---

## 1. Scope

This document specifies how a PPCP message becomes bytes, and how those bytes are delimited, on a live transport and in a bundle file. It defines nothing about the meaning of a message; that is [`PPCP-MSG`](ppcp-messages.md).

**One rule drives the whole design:** the bytes of a live session and the bytes of a bundle are the *same* bytes. An implementation that can parse one parses the other, with no importer, no second schema and no second conformance suite.

---

## 2. Channels

[`PPCP-CORE` §3](ppcp-core.md#3-transport-contract) requires at least two independently flow-controlled channels.

- **(2a) MUST** Channel `0` is the control channel. Channels `1` and above are bulk channels. Channel `255` is reserved.
- **(2b) MUST** A transport provides each channel with its own ordering and its own flow control. Two TCP connections, two QUIC streams, or an application multiplexer with per-channel windows all satisfy this; one TCP connection does not, however the multiplexing is arranged.
- **(2c) MUST** Where a transport carries exactly one channel per underlying stream, the channel number in each frame header still matches the stream it arrives on. A mismatch is `error` / `malformed`.
- **(2d) MUST** Ordering is guaranteed **within** a channel and MUST NOT be assumed **across** channels. A `capture_announce` on control and its `payload_begin` on bulk may arrive in either order; a receiver handles both.

2d is the practical consequence of the split and the most common source of ordering bugs: the event is deliberately allowed to overtake the payload, which is the entire point of having two channels.

### 2.1 Binding streams to a link

*Erratum E1, 22 August 2026 — added after the first implementation session. Two implementations built the two-connection transport of [`PPCP-CORE` §3.1](ppcp-core.md#31-why-two-channels-is-not-negotiable) and associated a peer's connections two different ways, both of which worked against themselves and neither of which would have met the other.*

A **link** is the set of underlying streams between two peers that together carry one PPCP session — one stream per channel where the transport gives each channel its own stream. A listener receiving several streams needs two facts the transport does not supply: **which streams belong to one peer**, and **which of them is channel 0**. Neither is inferable: arrival order breaks the moment a dialler opens channels concurrently or a dial is abandoned and retried; a transport address is shared by every peer behind one NAT; and a TLS identity exists only on the rendezvous path, not on the direct one.

- **(2.1a) MUST** Where the transport carries one channel per underlying stream, the **dialling peer** mints a **`link_id`** — 16 bytes from a cryptographically secure random number generator, fresh per link — and sends a **`link_bind`** frame as the **first frame on every stream it opens**, on every channel including channel 0. The frame header carries the channel number the stream will carry; the payload is `link_bind { link_id, channel }` ([`PPCP-MSG` §3.0](ppcp-messages.md#30-link_bind)), with `channel` equal to the header's.
- **(2.1b) MUST** The listening peer associates streams into a link by `link_id` and takes each stream's channel from the header, which [2c](#2-channels) already requires to match every frame on that stream. It MUST NOT infer either from arrival order, from the transport address, or from a rendezvous identity.
- **(2.1c) MUST** A listener closes a stream whose first frame is not `link_bind`, whose `channel` disagrees with its header, or whose `link_id` names a link that already holds that channel. A `link_bind` naming an unknown `link_id` opens a new link. A link that has not bound channel 0 within the listener's own timeout is discarded with every stream it holds; the timeout is the embedding's policy.
- **(2.1d) MUST** A dialler opens channel 0 and sends `hello` on it only after the `link_bind` on that stream; bulk channels MAY be opened before, after, or concurrently with channel 0. A bulk channel MAY be opened at any later point in the session — a `preview` channel after the session is established is the expected case — by a further stream carrying `link_bind` with the same `link_id`.
- **(2.1e) MUST NOT** `link_bind` be sent where the transport itself identifies streams and the channels on them — QUIC stream identifiers, an application multiplexer with per-channel windows — and it never appears in a bundle ([§7](#7-bundle-container)): a file has one stream and its channels are the header byte.
- **(2.1f)** `link_id` is a transport-binding token and nothing else. It is not `Peer.id`, is never persisted, is never reused across links, and carries no identity a stranger could correlate: on a rendezvous path it travels inside TLS ([`PPCP-RV` 7.6a](ppcp-rv.md#76-peer-identity) is unaffected), and on a direct path the embedding has already accepted that the stream is unauthenticated.

**Why an explicit frame, and not the implicit rules the two implementations chose.** The first grouped by the pairing that the TLS PSK identity resolved to and ordered channels by serialising the dialler's handshakes; the second took the channels in arrival order. Both are correct for a dialler that behaves exactly as that listener assumes, which is the definition of an interoperability failure. The implicit rules also fail on the `direct` path — a tunnel, a socket handed in by an embedding application, the synthetic peer of [`PPCP-CONF` §2c](ppcp-conformance.md#2-required-test-infrastructure) — where there is no PSK identity to group by, and they forbid opening a third channel later, which a `preview` Stream wants. One frame of roughly forty bytes per stream, once, buys an association that is explicit, concurrent-safe and transport-independent. The channel byte was already in every header; the link needed a name.


---

## 3. Framing

Every frame, on every channel, in a live session and in a bundle, is:

```
+--------------------------------------------------------------+
|  payload_len : uint32   (big-endian, bytes of payload)        |
|  channel     : uint8                                          |
|  flags       : uint8    (MUST be 0 in ppcp/1.0)               |
|  reserved    : uint16   (MUST be 0)                           |
+--------------------------------------------------------------+
|  payload     : payload_len bytes of CBOR                      |
+--------------------------------------------------------------+
```

An 8-byte header, then the payload. All multi-byte header fields are big-endian.

- **(3a) MUST** A receiver reads `payload_len` before allocating and rejects a frame exceeding the limits of [§8](#8-limits) **without allocating for it**.
- **(3b) MUST** `flags` and `reserved` are zero in `ppcp/1.0`. A receiver ignores unknown bits rather than failing, so a later minor version may use them.
- **(3c) MUST** A truncated frame at the end of a byte stream is not an error in a bundle ([§7](#7-bundle-container)) and is a fatal `malformed` on a live transport.

### 3.1 Why frame even when the transport delimits

A transport that supplies message boundaries makes this header redundant on the wire. It is mandatory anyway, for two reasons:

1. **A file has no channels.** The bundle is one byte stream; without a channel tag, a control frame and a bulk chunk are indistinguishable and the "same messages" claim collapses into a second format.
2. **Live and file bytes are then identical.** A recorded session can be replayed byte-for-byte through the same parser, which is what makes a real range session usable as a regression fixture at no additional cost.

Eight bytes per frame is the price. On the control channel the traffic is small and infrequent; on the bulk channel, at the default 256 KiB chunk, the header is 0.003% overhead.

---

## 4. Primitive types

Payloads are CBOR (RFC 8949).

| PPCP type | CBOR |
|---|---|
| `Id`, enum, `Kind`, string | text string (major 3), UTF-8 |
| integer, `Duration` | unsigned or negative integer (majors 0, 1). MUST fit in `int64`. |
| float, `Sigma`, confidence | IEEE-754 **double**, CBOR `0xFB`. Half and single precision MUST NOT be emitted; a decoder MUST accept them. |
| `bool` | `0xF4` / `0xF5` |
| bytes | byte string (major 2) |
| absent | **the key is omitted**. `null` (`0xF6`) MUST NOT be used to mean absent. |
| list | array (major 4) |
| structure | map (major 5) with text-string keys |

- **(4a) MUST** Map keys are text strings. Integer keys MUST NOT be used; a decoder MUST reject them as `malformed`.
- **(4b) MUST** A decoder ignores map keys it does not recognise, at every nesting level (I13). This is the mechanism by which a MINOR version adds a field.
- **(4c) MUST NOT** An encoder emit `null` for an absent optional value. Absence is the absence of the key, and every optional field's absence has a stated meaning in [`PPCP-CORE` §5](ppcp-core.md#5-data-model).
- **(4d) MUST NOT** An encoder emit CBOR tags, indefinite-length items, or duplicate keys. A decoder rejects duplicate keys as `malformed` and MAY reject the others.
- **(4e) SHOULD** Encoders use deterministic encoding (RFC 8949 §4.2.1). Not required for correctness — digests are computed over payload bytes, never over CBOR — but it makes fixtures byte-reproducible, which is what makes a regression suite useful.

### 4.1 Composite types

| Type | Encoding |
|---|---|
| `Instant` | `{ "tb": tstr, "ns": int }` |
| `Series` | `{ "tb": tstr, "ns": [int, ...] }` |
| `Interval` | `{ "tb": tstr, "start_ns": int, "end_ns": int }` |
| `Digest` | `{ "alg": "sha-256", "value": bstr }` |
| `Anchor` | A map with **exactly one** key: `{"shot_id": tstr}`, `{"candidate_id": tstr}` or `{"stream": true}`. A decoder rejects zero keys or more than one as `malformed`, which is I27 made structural. |
| `Matrix3` | `[f64 × 9]`, row-major |
| `Estimate` | `{ "value_ns": int, "sigma_ns": f64 }`. Both keys mandatory together; neither has a defined meaning alone. |

- **(4.1a) MUST** There is no encoding for a bare timestamp. Every point in time is an `Instant` or an element of a `Series`, and both carry `tb`. This is I1 made structural: **a timestamp without a timebase is unwriteable**, not merely forbidden.
- **(4.1b) MUST** Durations are plain integers and carry no `tb`. A duration is not a point in time.
- **(4.1c) MUST** Parallel arrays in `AchievedFrames` — `exposure_ns`, `iso`, `intrinsics` — have the same length as `frames.ns` where present.
- **(4.1d) MUST** A per-frame field in `AchievedFrames` is encoded **either** as an array of that length **or** as a single value of the element type, which means the value was constant for every frame. A decoder distinguishes the two by CBOR major type, not by length: a one-frame Capture still encodes an array of one. **`intrinsics` is the exception, because its element type is itself an array:** there the forms are distinguished by the type of the **first element** — a number means one `Matrix3` constant across the Capture, an array means one `Matrix3` per frame. An **empty** `intrinsics` array MUST NOT be emitted and is `malformed` on receipt: it has no first element to branch on, and a Capture with no frames carries no `AchievedFrames` at all ([`PPCP-CORE` §5.8d](ppcp-core.md#58-capability)). `frames.ns` has no scalar form (I2).

`intrinsics` needs the exception spelled out because it is the field most likely to *be* constant — focus is locked for a session's lifetime, so the matrix does not change — and therefore the field the scalar form was most worth having for. A decoder applying the major-type rule literally cannot tell `[f64 × 9]` from `[[f64 × 9], …]`, and would silently read one constant matrix as a nine-frame series.
- **(4.1e) MUST** An `Estimate` carries both keys. An encoder cannot emit a value without its sigma, which is I29 and I3 made structural in the same way `Instant` makes I1 structural.

---

## 5. Message envelope

Every frame payload is a CBOR map carrying these reserved keys, with the message's own fields flat alongside them.

| Key | Type | Card. | Meaning |
|---|---|---|---|
| `type` | tstr | 1 | Message type from [`PPCP-MSG` §11](ppcp-messages.md#11-message-index). |
| `msg_id` | uint | 1 | Unique per sender per connection, monotonically increasing from 1. |
| `reply_to` | uint | responses: 1 | The `msg_id` being answered. |
| `session_id` | tstr | 0..1 | Session context. Absent before the session exists. |

- **(5a) MUST NOT** A message body use `type`, `msg_id`, `reply_to` or `session_id` as a field name for any other purpose.
- **(5a1) MUST** *Erratum E6, 23 August 2026.* Where a message body names the Session — `session_open`, `session_joined`, `session_resume`, `session_state`, `session_close`, `session_offer`, `session_accept` and `session_manifest` all list a `session_id` field in [`PPCP-MSG`](ppcp-messages.md) — that field **is** the envelope's `session_id` and is written **once**, in the envelope position of the same flat map. A body MUST NOT emit a second `session_id` key, which [4d](#4-primitive-types) makes `malformed` anyway, and a body decoder reads the value back out of the envelope. 5a forbids a body using a reserved name *for any other purpose*; using it for the envelope's own purpose is not another purpose, and eight of the forty-five messages depend on that reading.

5a1 is written down because the two documents read together said something impossible. `PPCP-MSG` lists `session_id` in eight bodies; 5a reserves the name; the envelope and the body share one CBOR map; and 4d makes a duplicate key malformed. An implementation obeying all four could not encode `session_open` at all. Hoisting is the only reading that keeps every clause true, and it costs nothing on the wire (`libppcp`, session S2).
- **(5b) MUST** A response carries `reply_to`. A request that receives no response within the sender's timeout is the sender's problem; PPCP defines no retransmission ([`PPCP-CORE` §3](ppcp-core.md#3-transport-contract) T1 makes it unnecessary).
- **(5c) MUST** `msg_id` is per-sender. Two peers may use the same `msg_id` concurrently; `reply_to` is interpreted against the recipient's own outgoing sequence.
- **(5d) MUST** A receiver that cannot decode a payload responds `error` / `malformed` with `reply_to` where it could recover `msg_id`, and without it otherwise. It does not close the transport.

### 5.1 Worked example

*Erratum E5, 23 August 2026 — re-emitted in deterministic key order. As first written the example ordered its keys `type`, `msg_id`, `probe_seq`, `timebase_id`, `t1`, and its `Instant` `tb` before `ns`. That is a **legal** encoding — [4e](#4-primitive-types) is a SHOULD — but it is not RFC 8949 §4.2.1 order, so an encoder honouring 4e could not reproduce the one worked example in the document: `"t1"` encodes `62 74 31` and `"type"` encodes `64 74 79 70 65`, and `0x62` sorts before `0x64`. The message, the field values and the byte count are unchanged; only the order moved (finding by `libppcp`, session S1).*

A `sync_probe` on the control channel: `probe_seq` 3, `msg_id` 7, timebase `tb:device`, `t1` = 1 723 000 000 000 ns.

```
frame header
  00 00 00 57                    payload_len = 87
  00                             channel 0 (control)
  00 00 00                       flags, reserved

payload (CBOR, 87 bytes, deterministic per 4e)
  a5                             map(5)
  62 74 31                       "t1"
  a2                             map(2)
  62 6e 73                       "ns"
  1b 00 00 01 91 2a cd 8e 00     1723000000000
  62 74 62                       "tb"
  69 74 62 3a 64 65 76 69 63 65  "tb:device"
  64 74 79 70 65                 "type"
  6a 73 79 6e 63 5f 70 72 6f 62 65    "sync_probe"
  66 6d 73 67 5f 69 64           "msg_id"
  07                             7
  69 70 72 6f 62 65 5f 73 65 71  "probe_seq"
  03                             3
  6b 74 69 6d 65 62 61 73 65 5f 69 64    "timebase_id"
  69 74 62 3a 64 65 76 69 63 65  "tb:device"
```

Ninety-five bytes on the wire for the highest-frequency control message in the protocol. A 20-exchange burst per timebase is under 4 KB.

- **(5.1a)** The reserved keys of [§5](#5-message-envelope) and the body's own keys sort into **one** sequence, because they are one flat map. `t1` genuinely precedes `type`; an encoder that writes the envelope first and the body afterwards cannot produce deterministic output, which is why this example is worth transcribing exactly.
- **(5.1b)** A decoder MUST accept **any** key order (4b, 4d): 4e binds encoders and is a SHOULD, so a peer that reproduces the pre-erratum ordering is conformant and is decoded normally.

---

## 6. Bulk transfer

- **(6a) MUST** A Capture's payload is transferred as `payload_begin`, then `payload_chunk` in ascending `index` from 0, then `payload_end` ([`PPCP-MSG` §8.3](ppcp-messages.md#83-the-payload_-family)).
- **(6a1) MUST** `payload_begin` carries the Capture's `AchievedFrames` for a camera Capture. The per-frame series belong on this channel, with the frames they describe, and never on control (I30). At 1080p150 for three seconds they are roughly 44 KB in parallel form, and a few hundred bytes where a locked exposure lets three of the four collapse to scalars.
- **(6b) MUST** `payload_chunk.data` is a CBOR byte string. `offset` equals `index × chunk_bytes` for every chunk, and `data` is exactly `chunk_bytes` long except for the last.
- **(6c) MUST** `payload_chunk.digest` is SHA-256 of `data`. `payload_begin.digest` and `payload_end.digest` are SHA-256 of the concatenation of every chunk's `data` in index order, and are the value carried in `Capture.digest`.
- **(6d) MUST** A receiver verifies each chunk digest on arrival and the whole-payload digest at `payload_end`. A mismatch is `payload_abort` / `malformed`; the transfer may be retried from `payload_resume`.
- **(6e) MUST** Content addressing is over the **payload bytes**, never over the CBOR encoding of the enclosing message, so a re-encode does not change a Capture's identity.
- **(6f) SHOULD** `chunk_bytes` is 262144. It MUST NOT exceed 4194304.
- **(6g) MUST** *Erratum E7, 23 August 2026.* `payload_begin` carries **`container`** — an IANA media type, as a text string, e.g. `"video/quicktime"`, `"audio/wav"`, `"application/octet-stream"` — **whenever the payload bytes are a container-framed file**. It is absent only where the bytes are raw samples fully described by the Stream's `CaptureProfile` ([`PPCP-CORE` §5.7](ppcp-core.md#57-captureprofile)), and a receiver that meets an absent `container` on a container-framed payload MUST NOT guess: it stores the bytes under the Capture's identity and reports the container as unknown.
- **(6h) MUST NOT** A receiver derive a container from `format.codec`, from `Stream.kind`, from `Capture.bytes`, or from sniffing the first chunk. `codec` is a codec — H.264 is carried in QuickTime, in fragmented MP4 and in Annex B, and the three are different files.

6g exists because nothing in this document said what the bytes **are**. `payload_begin` carried `bytes`, `digest` and `chunk_bytes`; `format.codec` is three hops away on the Source's profile and answers a different question; and a receiver writing a clip to disk had to choose a file extension from nothing. Every implementation would have chosen the same one, correctly, for its own counterpart — which is [§2.1](#21-binding-streams-to-a-link)'s failure again, one layer up (finding by PinPointStudio, session S2).

Digest-based identity is what makes re-import a no-op rather than a duplicate. Users connect twice.

---

## 7. Bundle container

A bundle is a Session serialised. It is not a distinct entity and not a distinct format.

```
+----------------------------------------------------------+
|  magic     : 8 bytes  "PPCPBNDL" (0x50504350424e444c)     |
|  major     : uint16   1                                   |
|  minor     : uint16   0                                   |
|  reserved  : uint32   0                                   |
+----------------------------------------------------------+
|  frames    : a sequence of frames exactly as in §3        |
+----------------------------------------------------------+
```

- **(7a) MUST** The frame sequence is byte-identical in form to a live session's, including the `channel` byte in every header.
- **(7b) MUST** Frames appear in the order they would have been sent, and the ordering rules of [§2d](#2-channels) apply as they do live: control frames for a Capture may precede its payload frames arbitrarily, and MUST for the manifest.
- **(7c) MUST** `session_manifest` appears before any `payload_*` frame ([`PPCP-MSG` §9.2a](ppcp-messages.md#92-session_manifest)), so an interrupted read still yields an analysable session.
- **(7d) MUST** A truncated final frame means the bundle is **truncated**. Completeness is a separate question, asserted by the owner and never inferred (I10): the reader reports `Session.completeness` **as the bundle asserted it**, and where the bundle asserted nothing the Session is `unknown` — not `partial`. It never upgrades a `partial` or `unknown` Session to `complete` on the strength of what happened to be present.
- **(7d1) MUST** *Erratum E8, 23 August 2026.* A reader reports the **assertion** and the **truncation** as two facts, not one. There are three completeness states in this protocol ([`PPCP-MSG` §4.4](ppcp-messages.md#44-session_state-context_change-session_close)) and 7d as first written named only two, so an unasserted, untruncated bundle was neither `complete` nor `partial` and an implementation had to invent the answer. `unknown` is what I10 requires — completeness is asserted, never inferred — and keeping truncation separate is what makes [CT-I36](ppcp-conformance.md#3-the-invariant-test-matrix) (c) and (d) distinguishable, since they are the same bytes with a different assertion (finding by `libppcp`, session S2).
- **(7h) MUST** *Erratum E9, 23 August 2026.* A bundle carries a **`declare`** frame from the peer that owns the Session's data **before any frame naming a Capture, Stream, Shot or Candidate**. [`PPCP-CORE` §8.5c](ppcp-core.md#85-reconciliation) scopes Capture identity by the minting peer's `Peer.id`, and a bundle states that nowhere else: a file of bare `capture_announce` frames is unattributable, and therefore un-deduplicable on re-import, which is exactly what I34 exists to prevent. This sits alongside [7c](#7-bundle-container)'s ordering rule and is checked the same way (finding by PinPointStudio, session S2).
- **(7g) MUST NOT** A bundle contain a `link_bind` frame ([§2.1e](#21-binding-streams-to-a-link)). A reader that meets one ignores it (I13).
- **(7e) MUST NOT** A bundle contain a trailing index, footer or table of contents in `ppcp/1.0`. Random access is deliberately not supported at v1; adding it is a MINOR change that appends a frame type, not a container change.
- **(7f) MUST** A bundle reader accepts a bundle whose `minor` exceeds its own, ignoring frames it does not understand (I13).

**Three artefacts collapse into this one format:** the export bundle, the regression fixture, and the store-and-forward path. One parser, one schema, one set of conformance tests. A separate "import" feature is how two ingest paths and a drifting schema come about.

---

## 8. Limits

A decoder MUST enforce these before allocating.

| Limit | Value | On breach |
|---|---|---|
| Control frame `payload_len` | 1 MiB | fatal `malformed` |
| Bulk frame `payload_len` | 8 MiB | fatal `malformed` |
| `chunk_bytes` | 4 MiB | `payload_abort` / `malformed` |
| Text string | 64 KiB | `malformed` |
| Byte string | equal to the frame limit | `malformed` |
| Thumbnail bytes | 64 KiB | `malformed` |
| `Annotation.body` | 8 KiB | `malformed` |
| CBOR nesting depth | 16 | `malformed` |
| Array or map elements | 2²⁰ | `malformed` |

- **(8a) MUST** A `payload_len` beyond the channel's limit is **fatal**: it indicates stream desynchronisation, and a peer cannot skip a frame it cannot trust the length of.
- **(8b) MUST** Every other breach is non-fatal and answered with `error`, so a single bad message does not end a session that is otherwise capturing.

8a and 8b are deliberately different. Desynchronisation is unrecoverable; a malformed message is not, and dropping a live capture session because one field was too long would violate the principle that capture degrades last.

---

## 9. Design rationale

*Non-normative. Recorded so the choice is not re-litigated by habit, and so a reviewer who disagrees knows what was weighed.*

**Why CBOR.**

| Considered | Why not |
|---|---|
| **JSON** | No binary type. Thumbnails and audio evidence would be base64, at 33% overhead, and integer timestamps become floats in several popular parsers — a silent precision loss on nanosecond values, which is disqualifying. |
| **Protocol Buffers** | Good unknown-field handling, but requires a schema compiler in the build of every implementer, and the C runtime options are a heavier dependency than an MIT library carrying a portable core wants. Open registries of string `kind` values also fit awkwardly onto enums. |
| **FlatBuffers / Cap'n Proto** | Zero-copy matters for bulk payload, which PPCP does not put through the serialiser at all — chunks are opaque byte strings. The benefit lands where it is not needed. |
| **A bespoke binary format** | Cheapest to encode and the most expensive to extend, debug and get third parties to implement. The protocol is published; a third party should not need our tooling to read a frame. |
| **CBOR** | Chosen. Self-describing, binary-native, integers stay integers, unknown keys are skippable by construction, and a conformant encoder/decoder is a few hundred lines — small enough to vendor into an MIT library with no dependency. |

**Why text keys rather than integer keys.** Integer keys would save roughly 40% on control-message size. Control traffic is small and infrequent — the whole sync burst is under 4 KB — while bulk traffic is unaffected because chunks are opaque. The saving is real and irrelevant; a wire that can be read in a hex dump during a field diagnosis is worth more, and a published protocol is read by people who did not write the encoder.

**Why absence rather than `null`.** Every optional field in this protocol has a stated meaning for its absence, and several of them — `measured` meaning "not measured" (I28), `evidence_ref` meaning "not retained" — are load-bearing. Two ways to spell absence invites two meanings.

**Why big-endian in the frame header and CBOR's own encoding inside.** The header is read by hand during debugging and by a length-prefix reader before any CBOR decoder exists. CBOR is already big-endian internally, so there is one byte order in the whole format.
