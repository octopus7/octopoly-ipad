#include "octopoly/mesh.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace octopoly {

namespace {

bool hasIdCapacity(std::uint64_t nextId, std::size_t allocationCount) noexcept {
    return nextId != 0 &&
           allocationCount <= std::numeric_limits<std::uint64_t>::max() - nextId;
}

static_assert(std::is_nothrow_move_assignable_v<Mesh>,
              "atomic mesh operations require non-throwing Mesh move assignment");
static_assert(std::is_nothrow_move_constructible_v<OperationResult>,
              "atomic mesh operations require non-throwing OperationResult moves");

OperationResult commitCandidate(Mesh& live, Mesh&& candidate,
                                OperationResult&& success) noexcept {
    live = std::move(candidate);
    return std::move(success);
}

}  // namespace

Mesh Mesh::makeDefaultCube() {
    Mesh mesh;
    const std::vector<Vec3> positions{
        {-1.0, -1.0, -1.0}, {1.0, -1.0, -1.0}, {1.0, 1.0, -1.0}, {-1.0, 1.0, -1.0},
        {-1.0, -1.0, 1.0},  {1.0, -1.0, 1.0},  {1.0, 1.0, 1.0},  {-1.0, 1.0, 1.0},
    };
    for (const auto& position : positions) {
        mesh.vertices_.push_back({mesh.nextVertexId_++, position});
    }
    const std::vector<std::vector<VertexId>> polygons{
        {1, 4, 3, 2}, {5, 6, 7, 8}, {1, 2, 6, 5},
        {4, 8, 7, 3}, {1, 5, 8, 4}, {2, 3, 7, 6},
    };
    for (const auto& polygon : polygons) {
        mesh.faces_.push_back({mesh.nextFaceId_++, polygon});
    }
    mesh.rebuildVertexLookup();
    return mesh;
}

Mesh& Mesh::operator=(const Mesh& other) {
    if (this == &other) {
        return *this;
    }
    Mesh candidate(other);
    *this = std::move(candidate);
    return *this;
}

const std::vector<Vertex>& Mesh::vertices() const noexcept { return vertices_; }
const std::vector<Face>& Mesh::faces() const noexcept { return faces_; }

const Vertex* Mesh::vertex(VertexId id) const noexcept {
    if (vertexLookup_.size() != vertices_.size()) {
        return nullptr;
    }
    const auto found = std::lower_bound(
        vertexLookup_.begin(), vertexLookup_.end(), id,
        [](const auto& entry, VertexId sought) { return entry.first < sought; });
    if (found == vertexLookup_.end() || found->first != id || found->second >= vertices_.size()) {
        return nullptr;
    }
    const Vertex& resolved = vertices_[found->second];
    return resolved.id == id ? &resolved : nullptr;
}

const Face* Mesh::face(FaceId id) const noexcept {
    const auto found = std::find_if(faces_.begin(), faces_.end(),
                                    [id](const Face& item) { return item.id == id; });
    return found == faces_.end() ? nullptr : &*found;
}

VertexId Mesh::nextVertexId() const noexcept { return nextVertexId_; }
FaceId Mesh::nextFaceId() const noexcept { return nextFaceId_; }
std::uint64_t Mesh::revision() const noexcept { return revision_; }

std::vector<Triangle> Mesh::triangulate() const {
    std::vector<Triangle> result;
    visitTriangles([&result](const Triangle& triangle) { result.push_back(triangle); });
    return result;
}

void Mesh::rebuildVertexLookup() {
    std::vector<std::pair<VertexId, std::size_t>> rebuilt;
    rebuilt.reserve(vertices_.size());
    for (std::size_t index = 0; index < vertices_.size(); ++index) {
        rebuilt.emplace_back(vertices_[index].id, index);
    }
    std::sort(rebuilt.begin(), rebuilt.end());
    vertexLookup_ = std::move(rebuilt);
}

OperationResult Mesh::preflightLoopCut(FaceId faceId) const {
    const auto found = std::find_if(faces_.begin(), faces_.end(),
                                    [faceId](const Face& item) { return item.id == faceId; });
    if (found == faces_.end()) {
        return {false, OperationError::notFound, "loop cut face does not exist", {}, {}};
    }
    if (found->vertices.size() != 4) {
        return {false, OperationError::unsupported, "loop cut currently supports a single quad", {}, {}};
    }
    return {true, OperationError::none, {}, {}, {}};
}

OperationResult Mesh::loopCut(FaceId faceId) {
    OperationResult preflight = preflightLoopCut(faceId);
    if (!preflight.ok) {
        return preflight;
    }
    return knifeCut(faceId, 0, 0.5, 2, 0.5);
}

OperationResult Mesh::preflightKnifeCut(FaceId faceId, std::size_t firstEdge,
                                        double firstT, std::size_t secondEdge,
                                        double secondT) const {
    const Face* source = face(faceId);
    if (source == nullptr) {
        return {false, OperationError::notFound, "knife face does not exist", {}, {}};
    }
    const std::size_t count = source->vertices.size();
    if (firstEdge >= count || secondEdge >= count || firstEdge == secondEdge ||
        (firstEdge + 1) % count == secondEdge || (secondEdge + 1) % count == firstEdge) {
        return {false, OperationError::invalidArgument,
                "knife edges must be distinct, in range, and nonadjacent", {}, {}};
    }
    if (!std::isfinite(firstT) || !std::isfinite(secondT) || firstT <= 0.0 || firstT >= 1.0 ||
        secondT <= 0.0 || secondT >= 1.0) {
        return {false, OperationError::invalidArgument, "knife edge parameters must be inside (0, 1)", {}, {}};
    }
    return {true, OperationError::none, {}, {}, {}};
}

OperationResult Mesh::knifeCut(FaceId faceId, std::size_t firstEdge, double firstT,
                               std::size_t secondEdge, double secondT) {
    OperationResult preflight =
        preflightKnifeCut(faceId, firstEdge, firstT, secondEdge, secondT);
    if (!preflight.ok) {
        return preflight;
    }
    const Face* source = face(faceId);
    const std::size_t count = source->vertices.size();
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        return {false, OperationError::revisionExhausted,
                "knife cut cannot advance the terminal mesh revision", {}, {}};
    }
    if (!hasIdCapacity(nextVertexId_, 2) || !hasIdCapacity(nextFaceId_, 1)) {
        return {false, OperationError::idExhausted,
                "knife cut cannot allocate stable IDs without exhausting the ID space", {}, {}};
    }
    if (firstEdge > secondEdge) {
        std::swap(firstEdge, secondEdge);
        std::swap(firstT, secondT);
    }

    Mesh candidate = *this;
    Face* target = nullptr;
    for (auto& polygon : candidate.faces_) {
        if (polygon.id == faceId) {
            target = &polygon;
            break;
        }
    }
    const auto original = target->vertices;
    const Vec3 firstA = candidate.vertex(original[firstEdge])->position;
    const Vec3 firstB = candidate.vertex(original[(firstEdge + 1) % original.size()])->position;
    const Vec3 secondA = candidate.vertex(original[secondEdge])->position;
    const Vec3 secondB = candidate.vertex(original[(secondEdge + 1) % original.size()])->position;
    const auto addEdgePoint = [&candidate](Vec3 a, Vec3 b, double t) {
        const VertexId id = candidate.nextVertexId_++;
        candidate.vertices_.push_back({id, {a.x + (b.x - a.x) * t,
                                            a.y + (b.y - a.y) * t,
                                            a.z + (b.z - a.z) * t}});
        return id;
    };
    const VertexId first = addEdgePoint(firstA, firstB, firstT);
    const VertexId second = addEdgePoint(secondA, secondB, secondT);

    std::vector<FaceId> affected{faceId};
    const auto insertSharedEdgePoint = [&candidate, faceId, &affected](VertexId edgeStart,
                                                                      VertexId edgeEnd,
                                                                      VertexId added) {
        for (auto& polygon : candidate.faces_) {
            if (polygon.id == faceId) {
                continue;
            }
            const std::size_t vertexCount = polygon.vertices.size();
            for (std::size_t index = 0; index < vertexCount; ++index) {
                const VertexId current = polygon.vertices[index];
                const VertexId next = polygon.vertices[(index + 1) % vertexCount];
                if (!((current == edgeStart && next == edgeEnd) ||
                      (current == edgeEnd && next == edgeStart))) {
                    continue;
                }
                polygon.vertices.insert(polygon.vertices.begin() + static_cast<std::ptrdiff_t>(index + 1),
                                        added);
                affected.push_back(polygon.id);
                break;
            }
        }
    };
    insertSharedEdgePoint(original[firstEdge], original[(firstEdge + 1) % count], first);
    insertSharedEdgePoint(original[secondEdge], original[(secondEdge + 1) % count], second);

    std::vector<VertexId> firstRegion{first};
    for (std::size_t index = firstEdge + 1; index <= secondEdge; ++index) {
        firstRegion.push_back(original[index]);
    }
    firstRegion.push_back(second);
    std::vector<VertexId> secondRegion{second};
    for (std::size_t index = secondEdge + 1; index < count; ++index) {
        secondRegion.push_back(original[index]);
    }
    for (std::size_t index = 0; index <= firstEdge; ++index) {
        secondRegion.push_back(original[index]);
    }
    secondRegion.push_back(first);

    target = nullptr;
    for (auto& polygon : candidate.faces_) {
        if (polygon.id == faceId) {
            target = &polygon;
            break;
        }
    }
    target->vertices = std::move(firstRegion);
    const FaceId addedFace = candidate.nextFaceId_++;
    candidate.faces_.push_back({addedFace, std::move(secondRegion)});
    affected.push_back(addedFace);
    std::sort(affected.begin(), affected.end());
    affected.erase(std::unique(affected.begin(), affected.end()), affected.end());
    candidate.rebuildVertexLookup();
    const ValidationResult validation = candidate.validate();
    if (!validation.ok) {
        return {false, OperationError::topologyInvalid, validation.error, {}, {}};
    }
    ++candidate.revision_;
    OperationResult success{true, OperationError::none, {}, {first, second},
                            std::move(affected)};
    return commitCandidate(*this, std::move(candidate), std::move(success));
}

OperationResult Mesh::preflightInsetFace(FaceId faceId, double factor) const {
    const Face* source = face(faceId);
    if (source == nullptr) {
        return {false, OperationError::notFound, "inset face does not exist", {}, {}};
    }
    if (!std::isfinite(factor) || factor <= 0.0 || factor >= 1.0) {
        return {false, OperationError::invalidArgument, "inset factor must be inside (0, 1)", {}, {}};
    }
    return {true, OperationError::none, {}, {}, {}};
}

OperationResult Mesh::insetFace(FaceId faceId, double factor) {
    OperationResult preflight = preflightInsetFace(faceId, factor);
    if (!preflight.ok) {
        return preflight;
    }
    const Face* source = face(faceId);
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        return {false, OperationError::revisionExhausted,
                "inset cannot advance the terminal mesh revision", {}, {}};
    }
    const std::size_t allocationCount = source->vertices.size();
    if (!hasIdCapacity(nextVertexId_, allocationCount) ||
        !hasIdCapacity(nextFaceId_, allocationCount)) {
        return {false, OperationError::idExhausted,
                "inset cannot allocate stable IDs without exhausting the ID space", {}, {}};
    }

    Mesh candidate = *this;
    const auto original = source->vertices;
    Vec3 centroid{};
    std::vector<Vec3> originalPositions;
    originalPositions.reserve(original.size());
    for (const VertexId id : original) {
        const Vec3 position = candidate.vertex(id)->position;
        originalPositions.push_back(position);
        centroid.x += position.x;
        centroid.y += position.y;
        centroid.z += position.z;
    }
    const double divisor = static_cast<double>(original.size());
    centroid.x /= divisor;
    centroid.y /= divisor;
    centroid.z /= divisor;

    std::vector<VertexId> inner;
    inner.reserve(original.size());
    for (const Vec3 position : originalPositions) {
        const VertexId added = candidate.nextVertexId_++;
        candidate.vertices_.push_back({added, {position.x + (centroid.x - position.x) * factor,
                                               position.y + (centroid.y - position.y) * factor,
                                               position.z + (centroid.z - position.z) * factor}});
        inner.push_back(added);
    }

    for (auto& polygon : candidate.faces_) {
        if (polygon.id == faceId) {
            polygon.vertices = inner;
            break;
        }
    }
    std::vector<FaceId> affected{faceId};
    for (std::size_t index = 0; index < original.size(); ++index) {
        const std::size_t next = (index + 1) % original.size();
        const FaceId side = candidate.nextFaceId_++;
        candidate.faces_.push_back({side, {original[index], original[next], inner[next], inner[index]}});
        affected.push_back(side);
    }
    candidate.rebuildVertexLookup();
    const ValidationResult validation = candidate.validate();
    if (!validation.ok) {
        return {false, OperationError::topologyInvalid, validation.error, {}, {}};
    }
    ++candidate.revision_;
    OperationResult success{true, OperationError::none, {}, std::move(inner),
                            std::move(affected)};
    return commitCandidate(*this, std::move(candidate), std::move(success));
}

OperationResult Mesh::preflightExtrudeFace(FaceId faceId, Vec3 offset) const {
    const Face* source = face(faceId);
    if (source == nullptr) {
        return {false, OperationError::notFound, "extrude face does not exist", {}, {}};
    }
    if (!std::isfinite(offset.x) || !std::isfinite(offset.y) || !std::isfinite(offset.z) ||
        (offset.x == 0.0 && offset.y == 0.0 && offset.z == 0.0)) {
        return {false, OperationError::invalidArgument,
                "extrude offset must be finite and nonzero", {}, {}};
    }
    return {true, OperationError::none, {}, {}, {}};
}

OperationResult Mesh::extrudeFace(FaceId faceId, Vec3 offset) {
    OperationResult preflight = preflightExtrudeFace(faceId, offset);
    if (!preflight.ok) {
        return preflight;
    }
    const Face* source = face(faceId);
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        return {false, OperationError::revisionExhausted,
                "extrude cannot advance the terminal mesh revision", {}, {}};
    }
    const std::size_t allocationCount = source->vertices.size();
    if (!hasIdCapacity(nextVertexId_, allocationCount) ||
        !hasIdCapacity(nextFaceId_, allocationCount)) {
        return {false, OperationError::idExhausted,
                "extrude cannot allocate stable IDs without exhausting the ID space", {}, {}};
    }

    Mesh candidate = *this;
    const std::vector<VertexId> original = source->vertices;
    std::vector<Vec3> originalPositions;
    originalPositions.reserve(original.size());
    for (const VertexId id : original) {
        originalPositions.push_back(candidate.vertex(id)->position);
    }
    std::vector<VertexId> cap;
    cap.reserve(original.size());
    for (const Vec3 position : originalPositions) {
        const VertexId added = candidate.nextVertexId_++;
        candidate.vertices_.push_back(
            {added, {position.x + offset.x, position.y + offset.y, position.z + offset.z}});
        cap.push_back(added);
    }

    for (auto& polygon : candidate.faces_) {
        if (polygon.id == faceId) {
            polygon.vertices = cap;
            break;
        }
    }
    std::vector<FaceId> affected{faceId};
    for (std::size_t index = 0; index < original.size(); ++index) {
        const std::size_t next = (index + 1) % original.size();
        const FaceId side = candidate.nextFaceId_++;
        candidate.faces_.push_back(
            {side, {original[index], original[next], cap[next], cap[index]}});
        affected.push_back(side);
    }

    candidate.rebuildVertexLookup();
    const ValidationResult validation = candidate.validate();
    if (!validation.ok) {
        return {false, OperationError::topologyInvalid, validation.error, {}, {}};
    }
    ++candidate.revision_;
    OperationResult success{true, OperationError::none, {}, std::move(cap),
                            std::move(affected)};
    return commitCandidate(*this, std::move(candidate), std::move(success));
}

OperationResult Mesh::preflightMergeVertices(VertexId targetId,
                                             VertexId sourceId) const {
    if (targetId == sourceId) {
        return {false, OperationError::invalidArgument,
                "merge target and source must be different vertices", {}, {}};
    }
    const Vertex* target = vertex(targetId);
    const Vertex* source = vertex(sourceId);
    if (target == nullptr || source == nullptr) {
        return {false, OperationError::notFound, "merge vertex does not exist", {}, {}};
    }
    return {true, OperationError::none, {}, {}, {}};
}

OperationResult Mesh::mergeVertices(VertexId targetId, VertexId sourceId) {
    OperationResult preflight = preflightMergeVertices(targetId, sourceId);
    if (!preflight.ok) {
        return preflight;
    }
    const Vertex* target = vertex(targetId);
    const Vertex* source = vertex(sourceId);
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        return {false, OperationError::revisionExhausted,
                "merge cannot advance the terminal mesh revision", {}, {}};
    }

    Mesh candidate = *this;
    const Vec3 targetPosition = target->position;
    const Vec3 sourcePosition = source->position;
    for (auto& item : candidate.vertices_) {
        if (item.id == targetId) {
            item.position = {(targetPosition.x + sourcePosition.x) * 0.5,
                             (targetPosition.y + sourcePosition.y) * 0.5,
                             (targetPosition.z + sourcePosition.z) * 0.5};
            break;
        }
    }

    std::vector<FaceId> affected;
    for (auto& polygon : candidate.faces_) {
        bool containsMergedVertex = false;
        bool topologyChanged = false;
        for (auto& id : polygon.vertices) {
            if (id == targetId || id == sourceId) {
                containsMergedVertex = true;
            }
            if (id == sourceId) {
                id = targetId;
                topologyChanged = true;
            }
        }
        if (topologyChanged) {
            std::vector<VertexId> repaired;
            for (const VertexId id : polygon.vertices) {
                if (repaired.empty() || repaired.back() != id) {
                    repaired.push_back(id);
                }
            }
            if (repaired.size() > 1 && repaired.front() == repaired.back()) {
                repaired.pop_back();
            }
            polygon.vertices = std::move(repaired);
        }
        if (containsMergedVertex) {
            affected.push_back(polygon.id);
        }
    }
    candidate.vertices_.erase(
        std::remove_if(candidate.vertices_.begin(), candidate.vertices_.end(),
                       [sourceId](const Vertex& item) { return item.id == sourceId; }),
        candidate.vertices_.end());
    candidate.rebuildVertexLookup();
    const ValidationResult validation = candidate.validate();
    if (!validation.ok) {
        return {false, OperationError::topologyInvalid, validation.error, {}, {}};
    }
    ++candidate.revision_;
    OperationResult success{true, OperationError::none, {}, {}, std::move(affected)};
    return commitCandidate(*this, std::move(candidate), std::move(success));
}

ValidationResult Mesh::validate() const {
    if (vertexLookup_.size() != vertices_.size()) {
        return {false, "vertex lookup index must be complete"};
    }
    for (std::size_t index = 0; index < vertexLookup_.size(); ++index) {
        const auto [id, storageIndex] = vertexLookup_[index];
        if (storageIndex >= vertices_.size() || vertices_[storageIndex].id != id) {
            return {false, "vertex lookup index must map IDs to their stored vertices"};
        }
        if (index != 0 && vertexLookup_[index - 1].first >= id) {
            return {false, "vertex lookup index must be sorted with unique IDs"};
        }
    }
    for (std::size_t storageIndex = 0; storageIndex < vertices_.size(); ++storageIndex) {
        const Vertex& item = vertices_[storageIndex];
        if (item.id == 0) {
            return {false, "vertex IDs must be nonzero and unique"};
        }
        if (!std::isfinite(item.position.x) || !std::isfinite(item.position.y) ||
            !std::isfinite(item.position.z)) {
            return {false, "vertex positions must be finite"};
        }
        const auto found = std::lower_bound(
            vertexLookup_.begin(), vertexLookup_.end(), item.id,
            [](const auto& entry, VertexId sought) { return entry.first < sought; });
        if (found == vertexLookup_.end() || found->first != item.id ||
            found->second != storageIndex) {
            return {false, "vertex lookup index must be complete"};
        }
    }
    if (nextVertexId_ == 0 ||
        (!vertexLookup_.empty() && nextVertexId_ <= vertexLookup_.back().first)) {
        return {false, "next vertex ID must be nonzero and greater than existing IDs"};
    }

    std::vector<FaceId> faceIds;
    faceIds.reserve(faces_.size());
    for (const auto& item : faces_) {
        if (item.id == 0) {
            return {false, "face IDs must be nonzero and unique"};
        }
        faceIds.push_back(item.id);
        if (item.vertices.size() < 3) {
            return {false, "faces need at least three vertices"};
        }
        std::vector<VertexId> references;
        references.reserve(item.vertices.size());
        for (const VertexId id : item.vertices) {
            if (vertex(id) == nullptr) {
                return {false, "face references a missing vertex"};
            }
            references.push_back(id);
        }
        std::sort(references.begin(), references.end());
        if (std::adjacent_find(references.begin(), references.end()) != references.end()) {
            return {false, "face vertices must be unique"};
        }
    }
    std::sort(faceIds.begin(), faceIds.end());
    if (std::adjacent_find(faceIds.begin(), faceIds.end()) != faceIds.end()) {
        return {false, "face IDs must be nonzero and unique"};
    }
    if (nextFaceId_ == 0 || (!faceIds.empty() && nextFaceId_ <= faceIds.back())) {
        return {false, "next face ID must be nonzero and greater than existing IDs"};
    }
    return {true, {}};
}

}  // namespace octopoly
