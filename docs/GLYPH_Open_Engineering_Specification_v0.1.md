# GLYPH

Open Optical Data Transport Protocol — Engineering Specification and Reference Architecture

Draft v0.1 — 28 September 2026 — The Hidden Canopy / Open Source Engineering

A native, open, offline optical data link that uses display pixels as a communications medium and considers a transfer complete only after byte-identical reconstruction is cryptographically verified.

# Document Status

This specification is an engineering draft for an open-source reference implementation. Normative keywords define proposed requirements. Performance figures are engineering targets and analytical examples unless explicitly identified as measured results.

Glyph is not a media codec. It transports arbitrary binary objects. Media-aware validation is additional conformance testing, not a substitute for whole-object verification.

# Open-Source Contract

- No account is required to use the core software or protocol.

- No Internet connection is required for the core sender, receiver, calibration, transfer, reconstruction, verification, or local benchmark path.

- No payment, hosted service, or proprietary activation server is required to unlock core transfer capability.

- Mandatory telemetry is prohibited. Diagnostic capture is local and opt-in.

- The protocol, manifest format, wire formats, state machines, conformance vectors, and reference implementation SHALL be documented publicly.

- The reference implementation SHALL build without Hidden Canopy infrastructure or access to private repositories.

- No remote kill switch, license expiration mechanism, or network dependency may disable already-installed core functionality.

- User data remains locally accessible. A successful receiver may export reconstructed objects without a proprietary application.

- Commercial services may improve convenience, integration, hardware support, or managed deployment, but may not ransom the core protocol or intentionally cripple local export.

- Interoperability is a design requirement: independent implementations should be able to exchange objects by following this specification.



# 1. Executive Summary

Glyph is a camera/display data transport designed for reliable offline exchange without requiring an Internet connection, cloud service, account, remote pairing broker, or proprietary server. Its core physical medium is visible RGB light emitted by a display and sampled by a camera. A Glyph surface is not a QR code and is not constrained by QR layout, module count, or payload model. The display is treated as a packet transmitter whose usable area is divided into calibrated optical cells.

The primary Glyph modulation model is binary activation of the display’s red, green, and blue channels. A data cell therefore carries three binary channel states: R, G, and B. The eight resulting symbols are not an arbitrary color palette; cyan, magenta, yellow, and white occur only as simultaneous activation of primary channels. The receiver estimates the three channel states after calibration and may retain per-bit confidence instead of prematurely classifying a named color.

Glyph defines reliability above the optical symbol layer. The receiver rectifies geometry, classifies optical cells, rejects uncertain cells as erasures when appropriate, verifies tile integrity, reconstructs missing data with forward-error correction, reassembles object blocks, and finally hashes the reconstructed byte stream. A transfer SHALL NOT enter COMPLETE state until the mandatory object hash matches the manifest.

For video with audio, Glyph transports the source container bytes rather than decoding and re-encoding media. If the source is MP4 with H.264 video and AAC audio, the receiver reconstructs the exact MP4 byte stream. When the final hash matches, timestamps, audio synchronization, compressed samples, metadata, and container indexing are preserved because the file itself is preserved. Optional media validation can additionally parse tracks and timing to detect implementation defects early.

The public reference implementation is proposed as a standalone C++20 project with a stable C ABI and optional Python and C# bindings. GPU acceleration should be used where it materially improves rendering, color classification, rectification, and FEC, but conformance must not require a specific GPU vendor. The first release should prioritize correctness and reproducibility over maximum bitrate.

```text
SOURCE OBJECT -> manifest/hash -> chunk/FEC -> RGB modulation -> display -> camera -> calibration/decoding -> FEC recovery -> reassembly -> SHA-256 verification -> COMPLETE
```

# 2. Requirements and Non-Goals

## 2.1 Normative core requirements

- GLY-R001: The transport SHALL accept an arbitrary finite binary object without interpreting its application semantics.

- GLY-R002: Whole-object integrity verification SHALL be mandatory. SHA-256 is the baseline conformance hash for v0.1.

- GLY-R003: COMPLETE SHALL mean byte-identical reconstruction as established by the authenticated or otherwise trusted manifest hash.

- GLY-R004: The core optical path SHALL function without Internet connectivity.

- GLY-R005: Color is native to the physical layer. A conforming RGB optical profile SHALL use binary primary-channel activation rather than a large arbitrary color palette.

- GLY-R006: The receiver SHALL distinguish detected corruption from missing/uncertain data wherever possible and SHALL NOT silently substitute guessed bytes.

- GLY-R007: Frames and tiles SHALL be independently identifiable so that receipt order need not equal transmission order.

- GLY-R008: Implementations SHALL bound memory allocation from untrusted manifests and frames.

- GLY-R009: Received files SHALL be written to a temporary object and atomically promoted only after successful verification, unless the API explicitly exposes a partial object.

- GLY-R010: The public reference implementation SHALL be self-contained and SHALL NOT require access to non-public Hidden Canopy repositories.



## 2.2 Non-goals for v0.1

- Replacing Wi-Fi, USB, Ethernet, Bluetooth, or NFC in every environment.

- Guaranteeing a specific bitrate across arbitrary displays, cameras, lighting conditions, distances, or operating systems.

- Embedding a general-purpose media codec. Glyph transports media files; it does not need to transcode them.

- Invisible or covert background transfer. The reference UI should make active sending and receiving obvious.

- Automatic execution of received binaries or scripts.

- Requiring machine learning for decoding. Statistical or ML-based decoders may be optional accelerators, but a deterministic baseline decoder is required.

- Requiring QR compatibility. QR may be supported only as a non-core interoperability/bootstrap adapter.



# 3. Design Principles

| Principle | Implication |

| --- | --- |

| Bit exactness over visual similarity | A video that “looks right” is not a successful transfer. The file hash must match. |

| Erasures over guesses | When confidence is low, mark a symbol or tile missing and let FEC recover it rather than injecting silent corruption. |

| Simplex works; duplex improves | The base protocol can complete with no return channel. Feedback, acknowledgements, and transport negotiation are accelerators. |

| Control is more robust than payload | Headers and pilots use larger cells and a smaller RGB constellation than dense payload regions. |

| Adaptive density, fixed semantics | Cell pitch, logical frame rate, FEC overhead, and profile may adapt; object semantics and verification do not. |

| No hidden dependency | Every mandatory algorithm must have an open specification and at least one practical open implementation path. |

| Fail closed at trust boundaries | Malformed manifests, impossible dimensions, invalid cryptographic state, overflow, and integrity failure terminate or quarantine the transfer. |

| Measured claims only | Published bitrate, distance, and error-rate claims require exact hardware/software configuration and raw benchmark artifacts. |



# 4. Reuse from Existing Hidden Canopy Engineering

The connected repositories already contain useful engineering patterns. The public Glyph repository must build and test independently. The following donor areas were reviewed on 28 September 2026.

| Repository | Relevant area | Observed assets | Glyph boundary |

| --- | --- | --- | --- |

| VANTA_Engine | Native rendering and capture | Vulkan/RHI, frame graph, offscreen viewport, bounded readback, CaptureRing/CaptureReceipt, exact RGBA/BGRA formats, GPU compute, frame receipts. Media code includes mp4.cpp, h264_syntax.cpp, h264_decoder.cpp. | Extract generic pixel-surface and readback concepts; do not make VANTA a build dependency. |

| DRIFT | Secure/resumable session semantics | core/src/auth.rs, framing.rs, handshake.rs, key_exchange.rs, session.rs, storage.rs. README documents X25519/HKDF, ChaCha20-Poly1305, Ed25519, replay/counter binding, journals and idempotency. | Port protocol semantics or expose a clean library boundary; preserve independent Glyph build. |

| HAVEN | Offline local-device adapters | Native Windows/WinRT BLE central implementation, stable C ABI, GATT read/write, scanning; cursor-based sync provider contracts. | Use as design precedent for optional BLE feedback/negotiation adapters. |

| Semantically-Aware_ISR | Loss/outage simulation | transmission/link_simulator.py models rate, loss, jitter, outages; video ingestion and OpenCV pipelines exist. | Generalize into an optical-channel simulator with spatial, temporal and chromatic impairment models. |

| OMNI-Q_Weird_Stuff_Machine | Camera/perception boundaries | FrameObserver and camera broker preserve capture metadata and frame-to-world provenance. | Reuse capture-metadata discipline, not planning/AI semantics. |

| TraceGlass / NAVWAR | Receipts and provenance | Integrity state, event lineage, raw-capture lineage and explicit provenance gaps. | Adopt receipt/audit patterns for benchmark and transfer evidence. |

| IDA-TRAIN-V2 | Native SHA-256 and high-performance native code | native/src/sha256.cpp provides a self-contained streaming SHA-256 implementation; native C++/CUDA patterns exist. | Use SHA-256 semantics as baseline; public Glyph should include or depend on a maintained public implementation. |



# 5. System Architecture

```text
Application/CLI
Object layer
Session layer
Reliability/FEC
Frame layer
Optical PHY
Display + camera backends
Hardware
```

## 5.1 Operating modes

| Mode | Definition | Use |

| --- | --- | --- |

| Simplex Optical | One display → one camera. No acknowledgement path is required. Sender periodically repeats manifest/control information and emits planned repair shards. | Air-gapped transfer, kiosk/device provisioning, demonstrations, one-way export. |

| Duplex Optical | Both endpoints provide a screen and camera. Receiver returns rank/receipt information using a low-rate robust control surface. | Efficient repair, negotiated adaptation, no radio dependency. |

| Hybrid Offline | Optical Glyph authenticates/negotiates a session; bulk payload moves over local Wi-Fi, BLE, USB, or Ethernet without Internet. | Large objects when a faster local link exists. |

| Stored Glyph Stream | Pre-rendered Glyph frames can be stored as ordinary video and later displayed/decoded. | Archival/testing, reproducible impairment experiments, broadcast-like distribution. |



# 6. Object and Manifest Layer

The object layer is media-agnostic. The sender determines exact length, computes SHA-256, and emits a deterministic manifest.

| Field | Type | Meaning |

| --- | --- | --- |

| protocol | text | "glyph/1" |

| transfer_id | 16-byte bstr | Random session-scoped transfer identifier. |

| object_sha256 | 32-byte bstr | Mandatory expected object digest. |

| object_size | uint | Exact byte length. |

| display_name | text | Advisory filename; receiver sanitizes before filesystem use. |

| media_type | text/null | Optional MIME-style hint; never trusted for parsing decisions. |

| block_size | uint | Logical reconstruction block size. |

| shard_size | uint | Bytes carried by one systematic FEC shard before inner ECC. |

| fec_profile | text | Mandatory profile identifier. |

| encryption | map/null | Session/cipher metadata when encryption is enabled. |

| created_at | text/null | Optional informational timestamp; not required for identity. |

| extensions | map | Namespaced future fields; unknown non-critical extensions are preserved. |



Deterministic CBOR is recommended for the v0.1 manifest. JSON may be exposed for diagnostics, but canonical on-wire bytes must be deterministic.

# 7. Session and Cryptographic Layer

Integrity is mandatory even when confidentiality is not. The secure profile adds authenticated key establishment and per-packet AEAD without requiring a server.

| Purpose | Algorithm | v0.1 rule |

| --- | --- | --- |

| Object integrity | SHA-256 | Mandatory for all v0.1 transfers. |

| Ephemeral key agreement | X25519 | Secure profile. |

| Key derivation | HKDF-SHA-256 | Derives role-separated session keys and nonce prefixes. |

| Packet confidentiality/integrity | ChaCha20-Poly1305 | Secure profile; packet header fields that drive routing are authenticated as associated data. |

| Optional long-term identity | Ed25519 | Signs an ephemeral transcript when devices/users choose authenticated identity. |

| Fast local checks | CRC32C | Detects accidental frame/tile corruption before expensive cryptographic work; never treated as a security primitive. |



```text
X25519 ephemeral exchange -> HKDF-SHA-256 -> role-separated keys -> ChaCha20-Poly1305 packets; optional Ed25519 transcript signature.
```

# 8. Optical Physical Layer

A Glyph frame uses anchors, pilots, a robust control band, and a dense payload field. One logical cell spans multiple display pixels and must project to enough camera samples for stable decoding.

```text
Four orientation anchors + repeated robust header + pilot/calibration edges + tiled dense RGB8 payload.
```

## 8.1 Mandatory color model

Color is native. RGB8 uses three independently interpreted binary primary channels per cell and avoids a large arbitrary palette.

| Bits | Visual result | Physical interpretation |

| --- | --- | --- |

| 000 | Black | R=0 G=0 B=0 |

| 001 | Blue | R=0 G=0 B=1 |

| 010 | Green | R=0 G=1 B=0 |

| 011 | Cyan | G+B primary activation |

| 100 | Red | R=1 G=0 B=0 |

| 101 | Magenta | R+B primary activation |

| 110 | Yellow | R+G primary activation |

| 111 | White | R+G+B primary activation |



Secondary-looking colors are not separate palette entries; they are simultaneous activation of the primary channels.

## 8.2 Robust RGB4 control constellation

Critical control fields use RGB4: black, red, green, blue. Header information is spatially repeated.

# 9. RGB Modulation and Calibration

The receiver learns the display-camera channel at session start and refreshes estimates during transfer.

- Locate the four anchors and solve a homography from display coordinates into camera coordinates.

- Sample black plus the three one-hot primary calibration patches. RGB8 combination patches may also be transmitted as a consistency check.

- Estimate black offset and a 3×3 primary mixing matrix that maps sender channel activation to observed camera RGB.

- Estimate per-tile gain/offset or pilot normalization to compensate for vignetting and uneven ambient illumination.

- For each payload cell, average or robustly aggregate samples from the interior region rather than cell boundaries.

- Invert or approximately decorrelate the mixing matrix, then produce P(R=1), P(G=1), and P(B=1) or equivalent confidence scores.

- When confidence is below the selected threshold, prefer an erasure over a forced symbol decision.

- Recompute thresholds when pilot drift exceeds the profile’s bound.



```text
camera RGB -> black-offset correction -> 3x3 channel unmixing -> per-channel confidence -> RGB bit decisions or erasure.
```

# 10. Geometric and Temporal Synchronization

## 10.1 Geometry

Four unique corner anchors define orientation, crop, and perspective. Three-anchor recovery may be supported when confidence is sufficient.

## 10.2 Temporal clock

Display and camera clocks are asynchronous. Sequence numbers, phase pilots, duplicate suppression, and configurable frame hold time are mandatory.

## 10.3 Rolling shutter

M0 compensates for rolling shutter conservatively; later profiles may estimate or exploit it explicitly.

# 11. Frame and Tile Formats

## 11.1 64-byte frame control header

| Field | Bytes | Meaning |

| --- | --- | --- |

| magic | 4 | ASCII GLY1 |

| version_major | 1 | Protocol major version |

| version_minor | 1 | Protocol minor version |

| frame_type | 1 | CAL, MANIFEST, DATA, REPAIR, CONTROL, END |

| profile_id | 1 | Optical/FEC profile |

| flags | 2 | Critical extension and security flags |

| header_len | 2 | Bytes; 64 for v0.1 |

| transfer_id | 16 | Session transfer identifier |

| frame_seq | 4 | Monotonic logical frame sequence |

| block_id | 4 | Object reconstruction block |

| symbol_group_id | 4 | FEC group/rank scope |

| payload_bytes | 4 | Useful frame payload bytes before optical inner coding |

| tile_cols | 2 | Payload tile columns |

| tile_rows | 2 | Payload tile rows |

| cell_pitch | 2 | Logical display-pixel pitch in current profile |

| reserved | 2 | Zero in v0.1 |

| timestamp_ticks | 8 | Sender monotonic tick, diagnostic/sync use |

| header_crc32c | 4 | CRC32C over preceding header bytes |



Multibyte integers use network byte order. Headers use RGB4, strong inner ECC, and spatial repetition.

## 11.2 Tile miniheader

| Field | Bytes | Meaning |

| --- | --- | --- |

| tile_id | 2 | Tile location/index |

| shard_index | 2 | Systematic or parity shard index |

| payload_len | 2 | Valid data bytes in tile payload |

| fec_group | 2 | Cross-tile/cross-frame recovery group |

| payload_crc32c | 4 | CRC32C of decoded tile payload |



Tile payload is interleaved before optical placement to convert spatial bursts into distributed errors/erasures.

# 12. Error Detection, FEC, and Reconstruction

Glyph separates corruption detection, local correction, and large-scale erasure recovery.

| Layer | Purpose | Rule |

| --- | --- | --- |

| Cell confidence | Detect uncertain R/G/B decisions | Soft confidence or ERASURE marker. No silent guess requirement. |

| Tile CRC32C | Detect residual decoded corruption | A tile with a CRC mismatch is treated as erased for outer recovery. |

| Inner byte ECC | Correct sparse byte errors inside a tile/codeword | Baseline candidate: shortened Reed-Solomon over GF(256); exact parameters are profile-defined. |

| Outer systematic FEC | Recover missing tiles/frames/shards | M0 baseline: bounded systematic Reed-Solomon/Cauchy-style data+parity shards per block. |

| Fountain extension | Recover efficiently under unpredictable loss without targeted retransmission | Optional standardized extension such as RaptorQ after implementation/license review; not required for core v0.1. |

| Whole-object SHA-256 | Final truth | Required before COMPLETE. |



## 12.1 Baseline block profile

A practical M0 starting point is 32 data shards + 8 parity shards per FEC group, adjustable by profile.

## 12.2 Reconstruction rule

- Accept only verified tiles.

- Insert idempotently by FEC group and shard index.

- Reconstruct when sufficient independent shards exist.

- Optionally verify block hashes.

- Compute whole-object SHA-256.

- Promote and COMPLETE only on exact match.



# 13. Sender Pipeline

```text
Open/hash object -> deterministic manifest -> block/shard -> outer FEC -> interleave -> inner ECC -> frame scheduler -> RGB surface -> display.
```

Large objects should be streamed with bounded buffers. Logical frame sequence is independent of physical display-refresh repetitions.

## 13.1 Simplex schedule

| Window | Behavior |

| --- | --- |

| Calibration burst | At start and periodically when long transfers exceed the recalibration interval. |

| Manifest burst | Repeated several times at robust density so late-starting receivers can join. |

| Systematic data window | Transmit each source shard at least once. |

| Repair window | Transmit parity/repair shards according to the selected loss budget. |

| Manifest refresh | Repeat identity/object hash between windows. |

| End cycle | Emit END frames, then optionally repeat repair+manifest cycles until user stops or configured session duration expires. |



# 14. Receiver Pipeline

```text
Camera -> quality gate -> homography -> RGB calibration -> header -> payload confidence -> ECC/CRC -> shard/FEC store -> object assembly -> SHA-256 -> atomic promote.
```

The decoder should use capture metadata and controls when available, but must not require manual camera lock for conformance.

## 14.1 Quality gates

| Condition | Receiver behavior |

| --- | --- |

| Anchor confidence too low | Discard frame; do not attempt dense payload decode. |

| Homography condition poor | Discard or downgrade to larger-cell profile if a feedback path exists. |

| Clipping/saturation | Mark affected channel/cells uncertain; use pilots to identify regional saturation. |

| Transition contamination | Reject rows/tiles whose temporal phase indicates mixed logical frames. |

| CRC failure | Treat tile as erasure; never pass bytes upward. |

| Repeated frame_seq | Accept only missing shards; byte-mismatched duplicate is a protocol/integrity error. |

| Object hash mismatch | Never promote object. Persist diagnostics and, in duplex mode, request additional repair or restart. |



# 15. Exact Video + Audio Reconstruction

Glyph must transport the container bytes, not re-encode decoded media.

```text
Media container bytes are the payload. SHA-256 equality is authoritative; parser/timing/decode checks are diagnostics.
```

| Level | Check |

| --- | --- |

| Mandatory | Whole-file byte count and SHA-256 match. |

| Recommended MP4 test | Track count, codec configuration, sample offsets, keyframe table, DTS/PTS tables parse consistently. |

| Recommended playback test | Decode representative video frames and audio packets without transport-induced error. |

| Synchronization test | Compare source/destination timing tables; do not infer A/V sync merely from successful playback. |

| Progressive option | A receiver may expose verified complete segments before whole-object completion, but must label the object PARTIAL and immutable-complete regions must be range-verified. |



Existing VANTA media code can inform optional validators, but the core remains format-agnostic.

# 16. Offline Hybrid Transports

The session/object layers are transport-neutral; optical can negotiate faster local offline carriers without changing completion semantics.

| Carrier | Status | Notes |

| --- | --- | --- |

| Optical | Core | No network stack required; works across strong isolation boundaries where camera/display are permitted. |

| BLE GATT | Optional control / small payload | Useful for acknowledgements, adaptation, discovery, and compact transfers. HAVEN’s native BLE ABI is a design precedent. |

| Local Wi-Fi / Wi-Fi Direct | Optional bulk | High throughput without Internet. Platform support and user consent vary; never assume “Wi-Fi” means Internet. |

| USB | Optional bulk | Fast, deterministic local cable path; useful when camera transfer bootstraps trust but cable carries payload. |

| Ethernet local link | Optional bulk | Works on isolated LAN or direct cable; no external routing required. |



# 17. Performance Model and Adaptation

Useful throughput is raw cell capacity minus control, calibration, FEC, repetition, rejection, and framing overhead.

```text
raw_bits/frame = active_cells × bits/cell; useful rate accounts for spatial overhead, coding overhead and successful frame ratio.
```

| Scenario | Geometry | Cells | Raw/frame | Raw rate | Illustrative useful |

| --- | --- | --- | --- | --- | --- |

| 1080p example A | 1728×896 active area, 6 px cell pitch, RGB8 | 288×149 = 42,912 | ~16.1 KiB | ~0.47 MiB/s | ~0.33 MiB/s at 30% total overhead |

| 1080p example B | 1728×896 active area, 4 px cell pitch, RGB8 | 432×224 = 96,768 | ~35.4 KiB | ~1.04 MiB/s | ~0.73 MiB/s at 30% total overhead |

| 4K example | 3456×1792 active area, 6 px cell pitch, RGB8 | 576×298 = 171,648 | ~62.9 KiB | ~3.69 MiB/s @60 fps | ~2.58 MiB/s at 30% total overhead |



## 17.1 Adaptation knobs

| Knob | Policy |

| --- | --- |

| Cell pitch | Increase when geometric/color confidence falls; decrease only after sustained margin. |

| Logical frame hold | Increase when transition contamination or camera FPS mismatch rises. |

| RGB profile | Use RGB4 for control; RGB8 for data. A degraded payload profile may temporarily use one-hot RGB4 if required, but color remains native. |

| Outer parity | Increase with observed erasure rate when feedback exists; simplex uses configured safety margin. |

| Tile size | Smaller tiles localize glare/occlusion but increase header/ECC overhead. |

| Calibration cadence | Increase under auto-exposure/white-balance drift or changing ambient light. |

| Interleaver depth | Increase for bursty spatial/temporal loss at the cost of latency/memory. |



# 18. Persistence and Resume

Resume state includes transfer identity, manifest, shard receipt map, verified blocks, session state where applicable, and diagnostics. Unverified bytes are disposable.

| Persisted element | Reason |

| --- | --- |

| transfer_id + object_sha256 | Prevents accidentally resuming into a different object. |

| manifest hash/canonical bytes | Locks reconstruction parameters. |

| verified shard bitmap / rank state | Avoids reprocessing already accepted data. |

| verified block ranges | Allows safe partial persistence. |

| session key epoch + counters | Required only for secure profile; counters must never rewind/reuse nonces. |

| decoder calibration cache | Advisory; must be revalidated after camera/display change. |

| receipts / metrics | Supports reproducible debugging and benchmarks. |



DRIFT’s journal/replay model is a useful precedent; Glyph should expose an independent compact transfer journal.

# 19. Security and Abuse Resistance

Optical input is untrusted input and must be parsed like hostile network traffic.

| Threat | Failure | Mitigation |

| --- | --- | --- |

| Malformed dimensions / lengths | Integer overflow or huge allocation | Hard protocol maxima; checked arithmetic; bounded arenas. |

| Path traversal in filename | Overwrite arbitrary files | Treat filename as display metadata; sanitize and root output path. |

| Replay of old encrypted frames | Duplicate or malicious state | Direction-local counters, transfer_id binding, replay window. |

| MITM during secure handshake | Confidentiality/authentication failure | Transcript-bound X25519; optional Ed25519 identity verification; human short-auth string can be layered above. |

| CRC collision / forged tile | Corrupted or attacker-chosen bytes | CRC is diagnostic only; AEAD in secure profile and SHA-256 final object verification. |

| Decompression bomb | Resource exhaustion | Core does not automatically decompress received archives; validators use limits. |

| Auto-execution | Code execution | Never execute/open with privileged handler automatically after receipt. |

| Optical exfiltration | Unauthorized sending | Reference UI requires explicit send action and displays active transmission; no hidden always-on transmitter mode by default. |

| Camera denial / flashing content | Availability / human factors | Bound refresh patterns, avoid unnecessary high-frequency full-screen flashing, provide brightness controls and accessibility warnings for experimental high-frequency profiles. |



## 19.1 Resource limits

- Maximum manifest bytes before parse.

- Maximum declared object size configurable by user/policy.

- Maximum tiles per frame and cells per tile.

- Maximum FEC k/m values and matrix work size.

- Maximum number of simultaneous transfer IDs.

- Maximum persisted partial-object age/space.

- Maximum media-validator decode duration and dimensions.

- Timeouts for incomplete secure handshakes and stale calibration.



# 20. Reference Implementation Architecture

Proposed reference core: C++20 with a stable C ABI and optional language bindings.

```text
glyph/
  CMakeLists.txt
  LICENSE
  README.md
  SECURITY.md
  CONTRIBUTING.md
  CODE_OF_CONDUCT.md
  docs/
    protocol/
    design/
    benchmarks/
    adr/
  include/glyph/
    core/
    manifest/
    session/
    fec/
    phy/
    frame/
    transport/
    media/
  src/
    core/
    manifest/
    session/
    fec/
    phy/
    frame/
    transport/
    platform/
  shaders/
    encode_payload.comp
    render_glyph.frag
    decode_cells.comp
  apps/
    glyph-send/
    glyph-recv/
    glyph-bench/
    glyph-sim/
  bindings/
    c/
    python/
    csharp/
  tests/
    unit/
    property/
    fuzz/
    conformance/
    hardware/
  vectors/
    manifests/
    frames/
    crypto/
    fec/
  examples/
```

## 20.1 Core APIs

```text
struct ObjectDescriptor {
    std::array<std::byte, 16> transfer_id;
    std::array<std::byte, 32> sha256;
    std::uint64_t size;
    std::string display_name;
    std::string media_type;
};

struct OpticalProfile {
    std::uint16_t cell_pitch_px;
    std::uint16_t tile_cols;
    std::uint16_t tile_rows;
    std::uint8_t  payload_bits_per_cell;   // 3 for RGB8
    std::uint8_t  logical_frame_hold;
    std::uint16_t fec_data_shards;
    std::uint16_t fec_parity_shards;
};

struct DecodeMetrics {
    double anchor_confidence;
    double calibration_condition;
    double cell_erasure_rate;
    double tile_accept_rate;
    std::uint64_t accepted_payload_bytes;
};
```

## 20.2 Backend seams

| Interface | Responsibility | Reference direction |

| --- | --- | --- |

| DisplayBackend | Present a generated Glyph surface with timestamp/presentation evidence. | Vulkan first; D3D12/Metal/CPU later. |

| CameraBackend | Deliver frames + capture timestamps + available camera metadata. | Windows Media Foundation first or portable OpenCV/V4L2 adapter for bring-up. |

| ComputeBackend | Optional GPU kernels for pixel generation, rectification, classification, FEC. | CPU reference remains normative for correctness. |

| CryptoBackend | X25519/Ed25519/HKDF/AEAD through maintained library. | No custom primitive implementation requirement. |

| FileBackend | Bounded streaming reads, temporary output, atomic promotion. | Cross-platform filesystem abstraction. |

| TransportAdapter | Optional BLE/Wi-Fi/USB/Ethernet carrier using Glyph packet/session semantics. | Core optical implementation works without it. |



# 21. Testing and Simulation

Testing progresses from deterministic binary correctness to synthetic optical impairment to repeatable hardware fixtures.

## 21.1 Software test layers

| Layer | Coverage |

| --- | --- |

| Unit | Manifest canonicalization, hash vectors, packet encode/decode, tile headers, interleaver, FEC math, state machines. |

| Property-based | Any byte string encoded then reconstructed under allowed erasure sets equals the source exactly. |

| Fuzz | Manifest, frame header, tile payload, media validator, resume journal, secure handshake parser. |

| Golden vectors | Known manifests, optical cell matrices, FEC shard sets, crypto transcript outputs, whole-object hashes. |

| Simulation | Perspective, blur, glare, clipping, channel mixing, exposure/gamma variation, frame loss, duplicate frames, rolling shutter, temporal transition contamination. |

| Hardware loop | Real display → real camera → exact object reconstruction under recorded geometry/brightness/distance. |

| Media conformance | MP4 containing video + audio; exact SHA-256 plus optional parser/timing/decode checks. |

| Soak | Long multi-GB transfer, camera interruptions, app restart, resume, storage pressure. |



## 21.2 Optical impairment simulator

- Perspective and keystone transform

- Radial lens distortion

- Defocus and motion blur

- Sensor noise and quantization

- Display/camera RGB mixing matrix

- Gamma/nonlinearity

- Per-tile vignetting and local glare mask

- Bayer/CFA sampling approximation

- Rolling-shutter row timing

- Display refresh/camera FPS mismatch

- Dropped/duplicated camera frames

- Display PWM/flicker approximation

- Partial occlusion and crop

- Dead/stuck pixel regions

- Compression artifacts for virtual-camera paths



Impairment runs use explicit seeds and emit parameters into benchmark receipts for exact replay.

# 22. Conformance Profiles

| Profile | Scope | Role |

| --- | --- | --- |

| GLYPH-CORE-1 | Manifest, block/shard reconstruction, SHA-256 completion, file safety, test vectors. No camera required. | Required base for every implementation. |

| GLYPH-OPT-RGB8-1 | RGB4 control + RGB8 payload, anchors, calibration, frame/tile format, simplex optical send/receive. | Defines Glyph as an optical link. |

| GLYPH-SECURE-1 | X25519 + HKDF-SHA-256 + ChaCha20-Poly1305; optional Ed25519 identity. | Secure sessions. |

| GLYPH-DUPLEX-1 | Reverse control/ACK path and adaptive profile updates. | Improves efficiency; may be optical or another offline adapter. |

| GLYPH-HYBRID-1 | Optical session bootstrap plus bulk local carrier while preserving Glyph object/session semantics. | Optional high-throughput path. |

| GLYPH-MEDIA-MP4-1 | Optional MP4/H.264/AAC validation vectors in addition to exact file hash. | Diagnostic profile; does not make media special at the transport layer. |



## 22.1 M0 acceptance gate

- One sender and one receiver on ordinary desktop/laptop hardware or a desktop plus phone/webcam fixture.

- 1920×1080 sender surface supported.

- RGB8 payload and RGB4 robust control implemented.

- Perspective correction and calibration from live camera frames.

- Duplicates and missed frames tolerated.

- At least one bounded outer-FEC profile implemented.

- 100 MiB arbitrary binary object transfers with exact SHA-256 equality in the controlled hardware fixture.

- An MP4 with both video and audio transfers byte-identically and passes optional parser/timing smoke checks.

- No Internet connectivity during the entire conformance run.

- Benchmark receipt includes sender display mode, camera mode, distance, angle, brightness setting, logical frame rate, cell pitch, FEC profile, rejection rate, elapsed time, source/destination hashes.



# 23. Benchmark Publication Rules

Public results must publish enough hardware and decoder context to be reproducible.

| Category | Publish |

| --- | --- |

| Object | Name/type, exact bytes, SHA-256. |

| Sender display | Model or panel class when known, resolution, refresh, scaling, brightness, HDR state. |

| Receiver camera | Model, resolution, capture FPS, exposure/white-balance/focus mode where available. |

| Geometry | Distance, approximate angle, fraction of camera image occupied by Glyph. |

| Profile | Cell pitch, tile layout, RGB profile, frame hold, FEC parameters, calibration cadence. |

| Results | Elapsed wall time, useful MiB/s, raw logical payload rate, accepted/rejected frame counts, tile erasure rate, repair overhead. |

| Integrity | Source hash, destination hash, completion state. |

| Software | Glyph commit, build type, OS, GPU/driver if GPU path used. |

| Artifacts | Optional raw camera sample or screen recording sufficient to replay decoder for selected benchmark cases. |



# 24. Roadmap

| Milestone | Deliverable |

| --- | --- |

| M0 — Bit-Exact Optical Core | CPU reference pipeline + Vulkan sender, RGB4/RGB8, fixed lattice, calibration, deterministic frame format, inner/outer ECC, SHA-256, CLI sender/receiver, simulator, 100 MiB + A/V MP4 acceptance. |

| M1 — Adaptive Optical Link | Dynamic cell pitch, FEC rate and frame hold; receiver feedback; robust reconnect/resume; GPU cell classification; benchmark suite across multiple displays/cameras. |

| M2 — Temporal/Camera Optimization | Rolling-shutter phase estimation, better temporal pilots, async capture queues, zero/low-copy GPU camera processing where platform permits. |

| M3 — Offline Hybrid | BLE feedback adapter, local Wi-Fi/USB/Ethernet bulk adapters, optical-authenticated carrier migration, cross-platform device discovery. |

| M4 — High-Density Profiles | Higher-order spatial coding only if measured robustness supports it; optional fountain FEC; 4K/120-Hz tuned profiles; mobile-native implementations. |

| M5 — Ecosystem | Independent implementations, interoperability events, language SDKs, protocol registry, hardware qualification recipes, formal spec stabilization to 1.0. |



## 24.1 Work packages for M0

| Package | Scope |

| --- | --- |

| WP-01 Core object model | Manifest, SHA-256, temp-file promotion, deterministic test vectors. |

| WP-02 Frame codec | 64-byte control header, tile miniheader, interleaving, frame scheduler. |

| WP-03 FEC baseline | Inner RS profile + outer systematic data/parity profile; property tests. |

| WP-04 Vulkan sender | Packed shard buffer → RGB surface shader; anchors/pilots/control/data regions. |

| WP-05 Camera receiver | Capture backend, anchor detection, homography, pilot calibration, RGB confidence, tile decoder. |

| WP-06 Simulator | Synthetic frame generation + optical impairment chain + reproducible seeds. |

| WP-07 Resume journal | Persist verified shards/blocks and restart safely. |

| WP-08 Media acceptance | Known MP4 with H.264 video + AAC audio; exact hash and optional structural checks. |

| WP-09 CLI/UX | glyph-send, glyph-recv, progress, calibration status, explicit consent. |

| WP-10 CI/release | Sanitizers, fuzzing, vector artifacts, Windows/Linux builds, signed releases later. |



# 25. Open-Source Governance and Release Engineering

## 25.1 Recommended license

Recommended initial license: Apache-2.0 for the repository, with SPDX-tracked third-party dependencies and automated license checks.

## 25.2 Contribution model

- Use a Developer Certificate of Origin (DCO) sign-off rather than requiring a broad copyright assignment by default.

- Require tests for protocol behavior changes and conformance-vector changes.

- Protocol changes begin as an ADR or Glyph Enhancement Proposal (GEP) with motivation, wire impact, backward compatibility, security impact, and test plan.

- No implementation may call itself conforming based only on successful demo playback; conformance is vector- and hash-based.

- Security reports use a private coordinated-disclosure channel described in SECURITY.md.

- Benchmarks submitted to the project include raw configuration and integrity receipts.

- Breaking wire changes increment the protocol major version. Compatible extensions use namespaced/criticality-aware fields.



## 25.3 Repository policy

| Area | Policy |

| --- | --- |

| main | Always buildable; protected; merges require CI and review. |

| release/* | Stabilization branches only when necessary; avoid long-lived divergence. |

| tags | Signed semantic versions after 0.1; include protocol profile registry snapshot. |

| vectors/ | Immutable per released protocol version; corrections require new vector IDs. |

| docs/adr/ | Architectural decision record history is append-only except typo fixes. |

| benchmarks/ | Machine-readable receipts + human summary; no cherry-picked claim without configuration. |



# Appendix A — State Machines

## A.1 Sender

```text
Sender states: IDLE -> PREPARE -> CALIBRATE -> ANNOUNCE -> STREAM -> REPAIR -> END/REPEAT.
```

## A.2 Receiver

```text
Receiver states: SEARCH -> geometry/calibration -> MANIFEST -> RECEIVE -> VERIFY -> COMPLETE or repair/failure.
```

# Appendix B — Reference Profile Sketch

| Parameter | Initial value / rule |

| --- | --- |

| Profile ID | RGB8-M0-1080 |

| Control constellation | RGB4 (black/red/green/blue) |

| Payload constellation | RGB8 binary primary activation |

| Display mode | 1920×1080 minimum reference fixture |

| Active area | Implementation-selected inside guard/anchors |

| Cell pitch | 6 px initial; 4 px experimental after calibration margin |

| Logical frame hold | 2 display refreshes initial |

| Tile organization | Regular rectangular grid; independently checkable |

| Inner ECC | Profile-defined shortened RS over GF(256) |

| Outer FEC | Systematic 32 data + 8 parity starting point |

| Object hash | SHA-256 mandatory |

| Encryption | Off for CORE; ChaCha20-Poly1305 for SECURE |

| Manifest | Deterministic CBOR |

| Completion | Exact object length + SHA-256 match |



# Appendix C — Hardware Test Matrix

| Dimension | Cases |

| --- | --- |

| Display resolution | 1920×1080 / 2560×1440 / 3840×2160 |

| Display refresh | 60 / 120 / 144 Hz where available |

| Panel | IPS LCD / VA LCD / OLED / laptop integrated |

| Camera resolution | 1280×720 / 1920×1080 / 3840×2160 |

| Camera FPS | 30 / 60 / 120 where available |

| Capture type | Webcam / phone camera / integrated laptop camera |

| Distance | 0.3 m / 0.6 m / 1.0 m / farther until failure |

| Angle | 0° / 15° / 30° horizontal and vertical |

| Ambient | dim / office / bright diffuse / localized glare |

| Brightness | 25% / 50% / 100% or measured luminance when possible |

| Object size | 1 KiB / 1 MiB / 100 MiB / 1 GiB+ |

| Content | random bytes / text / archive / MP4 video+audio / model weights |

| Interruption | camera occlusion / focus loss / app restart / sender pause |

| Success | only exact SHA-256 match |



# Appendix D — Public API and C ABI Direction

```text
typedef struct glyph_context glyph_context;
typedef struct glyph_sender glyph_sender;
typedef struct glyph_receiver glyph_receiver;

typedef enum glyph_status {
    GLYPH_OK = 0,
    GLYPH_E_INVALID_ARGUMENT,
    GLYPH_E_IO,
    GLYPH_E_PROTOCOL,
    GLYPH_E_INTEGRITY,
    GLYPH_E_CRYPTO,
    GLYPH_E_CAMERA,
    GLYPH_E_DISPLAY,
    GLYPH_E_RESOURCE_LIMIT,
    GLYPH_E_UNSUPPORTED
} glyph_status;

glyph_status glyph_sender_open(glyph_context*, const glyph_sender_config*, glyph_sender**);
glyph_status glyph_sender_set_object(glyph_sender*, const char* path);
glyph_status glyph_sender_step(glyph_sender*, glyph_sender_event* out_event);

glyph_status glyph_receiver_open(glyph_context*, const glyph_receiver_config*, glyph_receiver**);
glyph_status glyph_receiver_submit_frame(glyph_receiver*, const glyph_frame_view*);
glyph_status glyph_receiver_poll(glyph_receiver*, glyph_receiver_event* out_event);
```

The ABI should be event/poll oriented with explicit ownership and versioning.

# Appendix E — Terminology

| Term | Definition |

| --- | --- |

| Optical cell | Smallest spatial unit carrying one RGB symbol in a logical frame. |

| RGB8 | Three-bit symbol formed by independent binary R/G/B channel activation. |

| RGB4 | Robust control symbol set using black plus one-hot red/green/blue. |

| Logical frame | One protocol frame identified by frame_seq; it may be displayed across multiple physical refreshes. |

| Tile | Independently identifiable payload region with its own miniheader, ECC context, and CRC. |

| Shard | FEC unit participating in systematic/parity reconstruction. |

| Erasure | Known missing/uncertain symbol, tile, or shard. Preferable to an undetected wrong value. |

| Pilot | Known optical symbols used to estimate color, exposure, geometry, or temporal phase. |

| Anchor | Robust spatial marker used for orientation and homography. |

| Receipt | Machine-readable evidence describing accepted data, profile, timing, hashes, and/or benchmark state. |

| Complete | Verified exact object length and SHA-256 match. No weaker condition is equivalent. |



# Appendix F — References

- RFC 5869 — HMAC-based Extract-and-Expand Key Derivation Function (HKDF): https://www.rfc-editor.org/rfc/rfc5869

- RFC 6330 — RaptorQ Forward Error Correction Scheme for Object Delivery: https://www.rfc-editor.org/rfc/rfc6330

- RFC 7748 — Elliptic Curves for Security (X25519/X448): https://www.rfc-editor.org/rfc/rfc7748

- RFC 8032 — Edwards-Curve Digital Signature Algorithm (EdDSA): https://www.rfc-editor.org/rfc/rfc8032

- RFC 8439 — ChaCha20 and Poly1305 for IETF Protocols: https://www.rfc-editor.org/rfc/rfc8439

- RFC 8949 — Concise Binary Object Representation (CBOR): https://www.rfc-editor.org/rfc/rfc8949

- FIPS PUB 180-4 — Secure Hash Standard (SHA-256): https://csrc.nist.gov/pubs/fips/180-4/upd1/final

- Apache License, Version 2.0: https://www.apache.org/licenses/LICENSE-2.0

## Reviewed Repository Areas

- The-Hidden-Canopy/VANTA_Engine — README.md; CMakeLists.txt; src/capture.cpp; src/mp4.cpp; src/h264_syntax.cpp; src/h264_decoder.cpp; include/vanta/*.

- The-Hidden-Canopy/DRIFT — README.md; core/src/auth.rs; framing.rs; handshake.rs; key_exchange.rs; session.rs; storage.rs.

- The-Hidden-Canopy/HAVEN — haven/sync/contracts.py; haven/integrations/bluetooth/native.py; native/haven-bt/include/haven_bt.h; native/haven-bt Windows backend documentation.

- The-Hidden-Canopy/Semantically-Aware_ISR — transmission/link_simulator.py; roi_encoder.py; video ingestion/demo pipeline.

- The-Hidden-Canopy/OMNI-Q_Weird_Stuff_Machine — ARCHITECTURE.md; src/omni_q/frame_observer.py; perception broker/camera integration paths.

- The-Hidden-Canopy/TraceGlass and NAVWAR — provenance, integrity, lineage and receipt patterns.

- The-Hidden-Canopy/IDA-TRAIN-V2 — native SHA-256 implementation and native performance-oriented build patterns.



# Closing Engineering Invariants

- **Invariant:** A successful Glyph transfer is an exact object reconstruction, not an approximation.

- **Invariant:** The final SHA-256 digest is authoritative for v0.1 completion.

- **Invariant:** Color is part of the native optical layer; the reference data profile uses binary RGB primary-channel activation.

- **Invariant:** Uncertain optical observations should become erasures instead of guessed bytes.

- **Invariant:** No Internet, account, hosted service, or Hidden Canopy private infrastructure is required for the core reference implementation.

- **Invariant:** Every performance claim must be reproducible from published configuration and integrity evidence.

- **Invariant:** Open-source interoperability is a first-order feature: the protocol is successful when independent implementations can exchange the same object and agree on its hash.
