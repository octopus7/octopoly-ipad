# OctoPoly project format

This document describes the format implemented by `core/src/project_codec.cpp`. It is a description of the current v1.0 codec, not a proposal for future fields.

## File-level rules

An OctoPoly project is a 32-byte header followed immediately by one payload. All multibyte integers and floating-point bit patterns are encoded little-endian, byte by byte; C++ structs, padding, pointers, and native integer object representations are never written directly.

The total file size must be exactly `32 + payload_length`. A shorter file is rejected as truncated, and any bytes after that exact size are rejected as trailing bytes.

### 32-byte header

| Absolute offset | Length | Field | Required value / meaning |
| ---: | ---: | --- | --- |
| 0 | 8 | Magic | ASCII `OCTOPOLY` (`4f 43 54 4f 50 4f 4c 59`) |
| 8 | 2 | Major version, `u16` | `1` |
| 10 | 2 | Minor version, `u16` | `0` |
| 12 | 1 | Endian marker, `u8` | `1`, meaning the canonical little-endian encoding described here |
| 13 | 3 | Reserved | All zero |
| 16 | 8 | Payload length, `u64` | Number of bytes beginning at absolute offset 32 |
| 24 | 4 | Payload checksum, `u32` | CRC-32/IEEE over the complete payload only |
| 28 | 4 | Reserved | All zero |

The decoder accepts exactly version 1.0. Any other major **or minor** value is `unsupportedVersion`; there is currently no forward-minor compatibility or migration path. Any endian marker other than `1` is rejected. Every reserved header byte is checked and must be zero.

The checksum is the reflected CRC-32/IEEE algorithm used by the implementation: initial value `0xffffffff`, reflected polynomial `0xedb88320`, and final bitwise complement. Its coverage is exactly bytes `[32, 32 + payload_length)`. The magic, versions, endian marker, payload-length field, checksum field itself, and reserved header bytes are not covered by the CRC; they are checked separately where applicable. CRC-32 detects accidental corruption and is not an authentication mechanism.

## Payload

The payload begins with a 48-byte payload prefix, followed by all vertex records and then all face records. There are no sections, tags, strings, alignment gaps, or extension records in v1.0.

### 48-byte payload prefix

| Payload offset | Absolute offset | Length | Field |
| ---: | ---: | ---: | --- |
| 0 | 32 | 8 | Vertex count, `u64` (`V`) |
| 8 | 40 | 8 | Face count, `u64` (`F`) |
| 16 | 48 | 8 | Total face-corner count, `u64` (`C`) |
| 24 | 56 | 8 | Next vertex ID, `u64` |
| 32 | 64 | 8 | Next face ID, `u64` |
| 40 | 72 | 8 | Mesh revision, `u64` |

The declared payload length must equal this exact checked-arithmetic formula:

```text
payload_length = 48 + (32 * V) + (16 * F) + (8 * C)
```

A value that overflows the wire arithmetic or the host's addressable `size_t`, or a value inconsistent with the file size/counts, is rejected.

### Vertex records

Vertex records start at absolute offset 80. Exactly `V` records are stored, each 32 bytes:

| Record offset | Length | Field |
| ---: | ---: | --- |
| 0 | 8 | Stable vertex ID, `u64` |
| 8 | 8 | X position: 64-bit `double` bit pattern |
| 16 | 8 | Y position: 64-bit `double` bit pattern |
| 24 | 8 | Z position: 64-bit `double` bit pattern |

`std::bit_cast<std::uint64_t>` supplies each floating-point bit pattern and that integer is emitted little-endian. Consequently the implementation requires an 8-byte C++ `double`; the tested platform uses IEEE-754 binary64. Decoding rejects NaN and positive or negative infinity. Finite bit patterns, including signed zero, are otherwise preserved rather than normalized.

The first face record begins at:

```text
80 + (32 * V)
```

### Face records

Exactly `F` variable-length face records follow in stored face order:

| Record offset | Length | Field |
| ---: | ---: | --- |
| 0 | 8 | Stable face ID, `u64` |
| 8 | 8 | Corner count for this face, `u64` (`N`) |
| 16 | `8 * N` | Ordered stable vertex IDs, each `u64` |

Each face record therefore occupies `16 + 8*N` bytes. The sum of all per-face `N` values must equal prefix field `C`, and parsing must end exactly at the end of the file.

## Validation and resource limits

`decodeProject` applies checks before installing a mesh:

1. Reject an input larger than the configured byte limit before reading the header. The production default is 64 MiB (`64 * 1024 * 1024` bytes).
2. Require a complete header; validate magic, exact version 1.0, endian marker, and zero reserved bytes.
3. Checked-add header and payload sizes; require exact file length, with neither truncation nor trailing bytes.
4. Verify the CRC-32/IEEE payload checksum.
5. Require at least the 48-byte fixed payload prefix.
6. Enforce configured count limits before reserving record or auxiliary ID vectors. Defaults are 1,000,000 vertices, 1,000,000 faces, and 4,000,000 total face corners.
7. Reject counts that exceed host `size_t` and reject multiplication/addition overflow while deriving the exact payload length.
8. Require every vertex ID to be nonzero and unique and every position component to be finite.
9. Require every face ID to be nonzero and unique. Each face needs at least three corners, all referenced vertex IDs must exist, and a vertex ID may appear only once within a face loop.
10. Require parsed per-face corner counts to total `C` and consume the payload exactly.
11. Require `next_vertex_id` and `next_face_id` to be nonzero and strictly greater than their respective maximum stored IDs.
12. Construct a detached candidate mesh, build its derived stable-vertex lookup index, and require the complete `Mesh::validate()` invariant check to pass.

The decoder reports typed category/code/offset/message errors. Allocation failure, including allocation while building the derived lookup index, is caught and reported; unexpected exceptions are converted to an internal error. The resource caps bound declared records, but successful decoding temporarily holds both the existing live mesh and the detached candidate.

All file-controlled ID duplicate and membership checks have deterministic worst-case bounds. The decoder copies IDs into vectors, sorts for duplicate detection, and uses binary search for vertex-reference membership. It then builds a private derived vector of `(stable VertexId, vertices_ index)` entries sorted by ID without changing `vertices_` storage order. `Mesh::validate()` fails closed unless that index is sorted, complete, unique, in range, and maps every ID to the correct stored vertex; face membership and `Mesh::vertex()` use binary search over the verified index. Per-face loop duplicate checks sort an auxiliary vector for that loop. No hash set, unordered container, or attacker-controlled hash table processes decoded IDs. Index construction is O(V log V) with O(V) derived memory, allocation-free lookup is O(log V), and complete decode/validation remains O(V log V + F log F + sum(N log N) + C log V), where the per-face corner counts `N` sum to `C`.

Encoding also requires `Mesh::validate()` success, valid future-ID counters, checked corner/size arithmetic, and successful allocation. Invalid live state is not serialized.

`next_vertex_id` and `next_face_id` are future allocation cursors, not merely hints. A value of `UINT64_MAX` is valid when it remains strictly above every stored ID, but it is terminal: an edit that needs IDs must reject it. Knife and loop cut require capacity for two vertex IDs and one face ID; inset and extrude of an `N`-corner face each require `N` vertex IDs and `N` face IDs. Each operation checks both counters before copying or mutating a candidate and returns typed `OperationError::idExhausted` if the post-allocation cursor would wrap or fail to remain greater than every allocated ID. Therefore `UINT64_MAX` itself is never allocated. Merge allocates no IDs and does not apply an ID-exhaustion check.

The revision field accepts the complete `u64` range. `UINT64_MAX` is a valid persisted and in-memory terminal revision, not a malformed codec value. For knife/loop cut, inset, extrude, and merge, normal argument and referenced-element checks run first; before any detached candidate is copied or mutated, a valid edit at terminal revision returns `OperationError::revisionExhausted`. Revision `UINT64_MAX - 1` may perform one successful mutation, advances exactly to `UINT64_MAX`, remains valid and deterministic-encodable/decodable, and rejects every later mutation without changing vertices, faces, future-ID cursors, revision, or canonical bytes.

## Deterministic encoding rules

For the same complete in-memory `Mesh` state, encoding is byte-for-byte deterministic:

- Header and payload fields are emitted in fixed order with fixed widths and little-endian byte assembly.
- Reserved bytes are always zero.
- Vertices and faces retain their `std::vector` order; face corner order is retained. The encoder does **not** sort or canonicalize semantically equivalent reordered meshes.
- The sorted vertex lookup index is derived in memory only and is never serialized. Rebuilding it therefore does not reorder records or change canonical bytes.
- IDs, future-ID counters, revision, and finite `double` bit patterns are emitted exactly.
- No timestamps, filesystem paths, capacities, pointer values, locale-dependent text, random values, or uninitialized bytes are included.
- The checksum is derived only from the deterministic payload bytes.

The committed tests pin the default cube to 624 total bytes, a 592-byte payload, and a golden 32-byte v1.0 header.

## Atomicity and app file location

Portable loading is transactional at the live-mesh boundary. `installProject` calls `decodeProject` into a detached candidate. The candidate's vertex index is rebuilt before final validation. On any decode, resource, index-allocation, or validation failure it returns without assigning to the live mesh. On success it performs one move assignment; a compile-time assertion requires that `Mesh` move assignment be non-throwing. Mesh copy construction carries the derived index with the stored topology. Copy assignment first copy-constructs a complete temporary and commits it through the non-throwing move assignment, so allocation failure leaves the destination unchanged instead of exposing a partial topology/index mixture.

Mesh edits use the same commit boundary. Knife/loop cut, inset, extrude, and merge construct a detached `Mesh` candidate, finish all vertex additions/removals, rebuild the derived index, and validate before commit. They then fully construct the successful `OperationResult` before assigning the candidate to the live mesh. Created/affected ID vectors are moved into that result rather than copied after commit. Compile-time assertions require non-throwing `Mesh` move assignment and non-throwing `OperationResult` move construction; the commit helper is `noexcept`, assigns once, and returns through that non-throwing result move. A dedicated allocation-fault executable replaces all ordinary/array and aligned allocation/deallocation forms used by the test process, injects `std::bad_alloc` at successive allocation ordinals, and checks complete mesh state plus canonical bytes whenever an allocation failure escapes. Its install sweep also verifies that decode/index allocation failures retain the codec's typed allocation error and leave live state and bytes unchanged.

## Bounded render traversal

The portable core exposes allocation-free triangle visitation and implements `triangulate()` by collecting that same visitation sequence, so both paths use one deterministic fan rule. The Objective-C++ bridge makes one allocation-free counting pass, checks triangle-to-vertex and vertex-to-byte multiplication plus the Foundation `NSUInteger` capacity bound, creates only the final `NSMutableData`, and makes a second pass that resolves each referenced stable ID through the sorted index and appends packed vertices directly. Capacity or reference failure sets `lastError` and returns nonnull empty `NSData`, matching the Swift-imported nonoptional property.

For `T = sum(N - 2)` emitted triangles, the bridge traversal is O(T log V): two O(T) visits plus three O(log V) indexed lookups per emitted triangle. Extra core traversal memory is O(1); persistent derived index memory is O(V); and render output memory is the required final `3 * T * sizeof(RenderVertex)` buffer, with no transient `std::vector<Triangle>` copy.

The iPad shell reads with `Data(contentsOf:)`, passes those bytes through `MeshBridge` to `installProject`, and refreshes published render geometry only after a successful install.

Saving is split between the codec and Foundation. The codec first returns a complete byte vector. The Swift layer then calls `Data.write(to:options: .atomic)` (written in source as `data.write(to: projectURL, options: .atomic)`). Foundation owns the temporary-file/replacement behavior; the portable codec performs no filesystem writes. This is replacement atomicity at the API level, not a claim that the implementation explicitly calls `fsync` or guarantees crash durability on every filesystem.

The app uses its sandbox Documents directory and the filename `OctoPoly.octopoly`. The concrete sandbox container path is assigned by iPadOS and may change; only the Documents-directory URL plus that filename is part of the application behavior.
