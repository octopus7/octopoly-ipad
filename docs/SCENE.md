# Portable scene, object, primitive, and scene-project contract

Phase 4 is complete at the portable-execution and static Apple-integration boundary. This document describes both the C++20 scene contract and the Objective-C++/SwiftUI wiring. No parenting is introduced and no Xcode, simulator, signing, installation, or device-run result is claimed.

## Scene ownership and stable IDs

`Scene` owns a `std::vector<SceneObject>` in insertion/storage order. A `SceneObject` owns its name, `Mesh`, and complete local transform. Object IDs are nonzero `uint64_t` values. They are stable for an object's lifetime and are not reused after deletion. `nextObjectId` is persisted and must be nonzero and greater than every stored ID.

`UINT64_MAX` is a valid terminal future-ID cursor but is never allocated. `UINT64_MAX - 1` can be allocated once, advancing the cursor to `UINT64_MAX`; later creation fails with `SceneError::idExhausted` without changing state. The scene revision has the same terminal arithmetic: `UINT64_MAX - 1` can advance once, while every state-changing operation at `UINT64_MAX` fails with `revisionExhausted`.

Creation appends and selects the new object. Selection changes are scene mutations and advance revision once. Selecting the already-selected ID is an explicit successful no-op and does not advance revision. ID zero clears selection. Deleting the selected object chooses the object now at the deleted storage position, otherwise the previous final object, otherwise none. Deleting an unselected object preserves selection.

A private sorted vector of `(ObjectId, storage index)` entries is derived from object storage. It is complete, strictly sorted, unique, in range, and checked against stored IDs by `Scene::validate()`. Lookup uses binary search, performs no allocation, and is `O(log N)`. No unordered container or attacker-controlled hash is used. Storage order is never changed by index construction.

Phase 4 has **no parenting or hierarchy**. `worldTransform()` is exactly the object's own local `T * R * S` matrix. No parent IDs, inherited transforms, or placeholder hierarchy are present.

## Names and transforms

Names are nonempty strict UTF-8, contain no NUL byte, and contain at most 255 encoded bytes. Overlong encodings, invalid continuation bytes, surrogate code points, and values above U+10FFFF are rejected.

The serializable `Transform` is:

- finite `Vec3 translation`;
- finite quaternion `(x, y, z, w)` whose squared norm differs from 1 by at most `1e-12`;
- finite `Vec3 scale` with every component strictly nonzero. Negative scale is supported.

Invalid names and transforms return typed errors before any live scene change. Matrices contain 16 doubles in column-major order: element `(row, column)` is `values[column * 4 + row]`. Points are column vectors and `transformPoint` computes `M * [x y z 1]^T`. The matrix is deterministic `T * R * S`, so scale applies first, then quaternion rotation, then translation.

## Primitive contracts

All factories produce finite positions, sequential vertex and face IDs starting at 1, valid future-ID cursors, mesh revision zero, a rebuilt sorted vertex index, deterministic storage/loop order, and valid polygon faces with at least three unique vertices.

- **Cube:** 8 vertices, 6 outward quads, positions at each `(-1|1, -1|1, -1|1)` corner. It is byte/order-compatible with `Mesh::makeDefaultCube()`.
- **Plane:** 4 vertices at `y = 0`, one outward `+Y` quad loop `[1,4,3,2]`, spanning `[-1,1]` in X/Z.
- **Tetrahedron:** 4 vertices at alternating cube corners and 4 outward triangles.
- **Cylinder:** default 16 radial segments; two polygon caps plus one outward side quad per segment. Vertices are the bottom ring in increasing angle followed by the top ring. Counts are `2R` vertices and `R+2` faces.
- **Cone:** default 16 radial segments; one bottom polygon cap plus one outward triangle per segment. Counts are `R+1` vertices and `R+1` faces.
- **UV sphere:** default 16 radial segments and 8 pole-to-pole subdivisions. It stores one north pole, `R*(K-1)` interior ring vertices, and one south pole. Pole faces are triangles; intermediate bands are quads. Counts are `2 + R*(K-1)` vertices and `R*K` faces.

Radial segments are bounded to `[3,256]`; UV-sphere subdivisions are bounded to `[2,256]`. Unsupported values return `resourceLimit` before geometry allocation. Cylinder and cone ignore the rings field; fixed primitives ignore tessellation fields.

Unknown `Primitive` underlying values fail closed with `unsupportedPrimitive` before any tessellation arithmetic or allocation. Primitive kind and tessellation-resource validation occurs after name validation but before terminal scene revision/object-ID checks, so malformed geometry requests retain their argument error even when the live scene counters are terminal.

`SceneMeshAccess` is a source-private friend factory for deterministic primitives. Mesh topology storage remains private. The public integration API adds transactional `createMeshObject(Mesh, name)` for detached imported/legacy meshes; it validates the mesh (including derived lookup and future-ID counters), preserves its stable IDs/counters/revision, appends in storage order, selects it, and advances the Scene revision once.

## Mutation and exception contract

Create primitive/imported mesh, delete, rename, selection change, complete local-TRS replacement, and selected-object mesh editing use one transaction boundary:

1. preflight arguments, referenced IDs, resource bounds, and terminal counters;
2. create/copy and fully modify a detached candidate;
3. rebuild its derived object index and validate every scene/object/mesh invariant;
4. construct the success result;
5. commit with one non-throwing `Scene` move assignment.

Selected Loop Cut, Knife Cut, Inset, Merge, and Extrude use concrete typed Scene wrappers and a private enum request dispatcher; there is no `std::function` allocation. The wrapper edits only a detached Scene candidate through the existing Mesh operation. Mesh-operation failure leaves both revisions and exact canonical Scene bytes unchanged. Success retains created/affected IDs, advances the owned Mesh revision through that operation, then advances the Scene revision exactly once before a non-throwing move commit.

Copy assignment uses copy-then-move and is transactional. Move construction and assignment are `noexcept`. Allocation-ordinal tests cover representative create primitive/imported mesh, delete, rename, transform, selected mesh edit, copy assignment, and decoded scene install. Every escaped `bad_alloc` from a mutation preserves validation and exact canonical scene bytes. Codec allocation failures are returned as typed errors and preserve the live scene.

## Canonical scene project v1.0

The scene codec is separate from the legacy Mesh v1.0 codec. `encodeProject`, `decodeProject`, and `installProject` and their `OCTOPOLY` bytes are unchanged. Scene APIs are `encodeSceneProject`, `decodeSceneProject`, and `installSceneProject`.

All integers and binary64 bit patterns are emitted byte-by-byte in little-endian order. No raw C++ struct, padding, pointer, capacity, timestamp, hash iteration, or derived index/matrix is serialized.

### 32-byte scene header

| Absolute offset | Length | Field |
| ---: | ---: | --- |
| 0 | 8 | ASCII magic `OCTOSCNE` |
| 8 | 2 | major version `1` |
| 10 | 2 | minor version `0` |
| 12 | 1 | endian marker `1` |
| 13 | 3 | zero reserved bytes |
| 16 | 8 | payload length |
| 24 | 4 | CRC-32/IEEE of the complete payload |
| 28 | 4 | zero reserved bytes |

The file must be exactly `32 + payload length` bytes. CRC uses reflected polynomial `0xedb88320`, initial `0xffffffff`, and final complement.

### 56-byte scene payload prefix

| Absolute offset | Length | Field |
| ---: | ---: | --- |
| 32 | 8 | object count |
| 40 | 8 | selected object ID, zero for none |
| 48 | 8 | next object ID |
| 56 | 8 | scene revision |
| 64 | 8 | aggregate nested-mesh vertex count |
| 72 | 8 | aggregate nested-mesh face count |
| 80 | 8 | aggregate nested-mesh face-corner count |

Object records then appear in scene storage order. Each begins with a 104-byte prefix:

| Record offset | Length | Field |
| ---: | ---: | --- |
| 0 | 8 | object ID |
| 8 | 4 | name byte length |
| 12 | 4 | zero reserved bytes |
| 16 | 24 | translation X/Y/Z doubles |
| 40 | 32 | quaternion X/Y/Z/W doubles |
| 72 | 24 | scale X/Y/Z doubles |
| 96 | 8 | nested Mesh-project byte length |
| 104 | name length | exact UTF-8 name bytes |
| following | nested length | complete canonical `OCTOPOLY` Mesh v1.0 project |

Nested Mesh containers preserve each mesh's stable IDs, storage order, positions, faces, future-ID cursors, and revision. The outer codec preflights the scene byte/object/name limits and declared aggregate mesh limits, scans every object and nested Mesh header with checked arithmetic, and verifies parsed aggregate counts before decoding nested meshes. Default limits are 128 MiB, 10,000 objects, 255 name bytes per object, 1,000,000 aggregate vertices, 1,000,000 aggregate faces, and 4,000,000 aggregate corners.

Object IDs are duplicate-checked by sorting `(ID, source offset)` vectors. Selected-ID membership and derived runtime lookup use sorted binary search. Decode builds a detached scene, decodes each nested mesh with the existing strict codec, rebuilds derived indexes, and runs full scene validation. Typed errors carry category, code, and outer-file offset. Successful install is one non-throwing move; failed decode/install leaves the exact live scene encoding unchanged.

## Objective-C++ and SwiftUI integration boundary

`MeshBridge` owns opaque `Scene` storage, initialized to one selected Cube; it has no raw Mesh storage. Outliner snapshots expose stable `ObjectId`, strict UTF-8 name, and selected state in deterministic Scene storage order, alongside selected ID and Scene revision. Selection, delete, rename, all six primitive additions, translation, normalized-quaternion rotation, scale, and five retained mesh edits call public Scene APIs.

Rendering uses two allocation-free passes over every object's `Mesh::visitTriangles` stream. The preflight checked-adds aggregate triangles, checked-multiplies vertices and bytes, validates every referenced vertex, applies `SceneObject::worldTransform()` to each position, and rejects nonfinite or Float32-unrepresentable world positions before allocating one exact final `NSMutableData`. The packing pass repeats the shared core visitor; it does not materialize `std::vector<Triangle>` or duplicate fan triangulation.

Save writes a complete canonical scene to Documents/`OctoPoly.octoscene`. Load dispatches by exact eight-byte magic: `OCTOSCNE` decodes a detached Scene, while legacy `OCTOPOLY` decodes a detached Mesh and then transactionally creates one selected `Legacy Project` Scene object. Unknown magic is rejected rather than trial-decoded. Publication occurs only after total success.

GLB export operates on the selected object's Mesh and fails visibly with no selection. GLB import decodes to a detached Mesh and transactionally appends/selects a new `Imported GLB` object. The UI states selected-only export and geometry-only/new-object import policy, and publishes geometry/outliner state only after successful mutation/import/load.
