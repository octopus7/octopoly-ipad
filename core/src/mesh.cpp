#include "octopoly/mesh.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace octopoly {

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
    return mesh;
}

const std::vector<Vertex>& Mesh::vertices() const noexcept { return vertices_; }
const std::vector<Face>& Mesh::faces() const noexcept { return faces_; }

const Vertex* Mesh::vertex(VertexId id) const noexcept {
    const auto found = std::find_if(vertices_.begin(), vertices_.end(),
                                    [id](const Vertex& item) { return item.id == id; });
    return found == vertices_.end() ? nullptr : &*found;
}

const Face* Mesh::face(FaceId id) const noexcept {
    const auto found = std::find_if(faces_.begin(), faces_.end(),
                                    [id](const Face& item) { return item.id == id; });
    return found == faces_.end() ? nullptr : &*found;
}

std::uint64_t Mesh::revision() const noexcept { return revision_; }

std::vector<Triangle> Mesh::triangulate() const {
    std::vector<Triangle> result;
    for (const auto& polygon : faces_) {
        for (std::size_t index = 1; index + 1 < polygon.vertices.size(); ++index) {
            result.push_back({polygon.id, {polygon.vertices[0], polygon.vertices[index], polygon.vertices[index + 1]}});
        }
    }
    return result;
}

OperationResult Mesh::loopCut(FaceId faceId) {
    const auto found = std::find_if(faces_.begin(), faces_.end(),
                                    [faceId](const Face& item) { return item.id == faceId; });
    if (found == faces_.end()) {
        return {false, OperationError::notFound, "loop cut face does not exist", {}, {}};
    }
    if (found->vertices.size() != 4) {
        return {false, OperationError::unsupported, "loop cut currently supports a single quad", {}, {}};
    }
    return knifeCut(faceId, 0, 0.5, 2, 0.5);
}

OperationResult Mesh::knifeCut(FaceId faceId, std::size_t firstEdge, double firstT,
                               std::size_t secondEdge, double secondT) {
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
    const auto addEdgePoint = [&candidate, &original](std::size_t edge, double t) {
        const Vec3 a = candidate.vertex(original[edge])->position;
        const Vec3 b = candidate.vertex(original[(edge + 1) % original.size()])->position;
        const VertexId id = candidate.nextVertexId_++;
        candidate.vertices_.push_back({id, {a.x + (b.x - a.x) * t,
                                            a.y + (b.y - a.y) * t,
                                            a.z + (b.z - a.z) * t}});
        return id;
    };
    const VertexId first = addEdgePoint(firstEdge, firstT);
    const VertexId second = addEdgePoint(secondEdge, secondT);

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
    const ValidationResult validation = candidate.validate();
    if (!validation.ok) {
        return {false, OperationError::topologyInvalid, validation.error, {}, {}};
    }
    ++candidate.revision_;
    *this = std::move(candidate);
    return {true, OperationError::none, {}, {first, second}, affected};
}

OperationResult Mesh::insetFace(FaceId faceId, double factor) {
    const Face* source = face(faceId);
    if (source == nullptr) {
        return {false, OperationError::notFound, "inset face does not exist", {}, {}};
    }
    if (!std::isfinite(factor) || factor <= 0.0 || factor >= 1.0) {
        return {false, OperationError::invalidArgument, "inset factor must be inside (0, 1)", {}, {}};
    }

    Mesh candidate = *this;
    const auto original = source->vertices;
    Vec3 centroid{};
    for (const VertexId id : original) {
        const Vec3 position = candidate.vertex(id)->position;
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
    for (const VertexId id : original) {
        const Vec3 position = candidate.vertex(id)->position;
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
    const ValidationResult validation = candidate.validate();
    if (!validation.ok) {
        return {false, OperationError::topologyInvalid, validation.error, {}, {}};
    }
    ++candidate.revision_;
    *this = std::move(candidate);
    return {true, OperationError::none, {}, inner, affected};
}

OperationResult Mesh::extrudeFace(FaceId faceId, Vec3 offset) {
    const Face* source = face(faceId);
    if (source == nullptr) {
        return {false, OperationError::notFound, "extrude face does not exist", {}, {}};
    }
    if (!std::isfinite(offset.x) || !std::isfinite(offset.y) || !std::isfinite(offset.z) ||
        (offset.x == 0.0 && offset.y == 0.0 && offset.z == 0.0)) {
        return {false, OperationError::invalidArgument,
                "extrude offset must be finite and nonzero", {}, {}};
    }

    Mesh candidate = *this;
    const std::vector<VertexId> original = source->vertices;
    std::vector<VertexId> cap;
    cap.reserve(original.size());
    for (const VertexId id : original) {
        const Vec3 position = candidate.vertex(id)->position;
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

    const ValidationResult validation = candidate.validate();
    if (!validation.ok) {
        return {false, OperationError::topologyInvalid, validation.error, {}, {}};
    }
    ++candidate.revision_;
    *this = std::move(candidate);
    return {true, OperationError::none, {}, cap, affected};
}

OperationResult Mesh::mergeVertices(VertexId targetId, VertexId sourceId) {
    if (targetId == sourceId) {
        return {false, OperationError::invalidArgument,
                "merge target and source must be different vertices", {}, {}};
    }
    const Vertex* target = vertex(targetId);
    const Vertex* source = vertex(sourceId);
    if (target == nullptr || source == nullptr) {
        return {false, OperationError::notFound, "merge vertex does not exist", {}, {}};
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
    const ValidationResult validation = candidate.validate();
    if (!validation.ok) {
        return {false, OperationError::topologyInvalid, validation.error, {}, {}};
    }
    ++candidate.revision_;
    *this = std::move(candidate);
    return {true, OperationError::none, {}, {}, affected};
}

ValidationResult Mesh::validate() const {
    std::unordered_set<VertexId> vertexIds;
    for (const auto& item : vertices_) {
        if (item.id == 0 || !vertexIds.insert(item.id).second) {
            return {false, "vertex IDs must be nonzero and unique"};
        }
        if (!std::isfinite(item.position.x) || !std::isfinite(item.position.y) ||
            !std::isfinite(item.position.z)) {
            return {false, "vertex positions must be finite"};
        }
    }
    std::unordered_set<FaceId> faceIds;
    for (const auto& item : faces_) {
        if (item.id == 0 || !faceIds.insert(item.id).second) {
            return {false, "face IDs must be nonzero and unique"};
        }
        if (item.vertices.size() < 3) {
            return {false, "faces need at least three vertices"};
        }
        std::unordered_set<VertexId> references;
        for (const VertexId id : item.vertices) {
            if (!vertexIds.contains(id)) {
                return {false, "face references a missing vertex"};
            }
            if (!references.insert(id).second) {
                return {false, "face vertices must be unique"};
            }
        }
    }
    return {true, {}};
}

}  // namespace octopoly
