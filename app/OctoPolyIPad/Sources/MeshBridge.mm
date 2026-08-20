#import "MeshBridge.h"

#include "octopoly/glb_codec.hpp"
#include "octopoly/project_codec.hpp"
#include "octopoly/scene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

struct RenderVertex {
    float x;
    float y;
    float z;
};

static octopoly::Scene& sceneFromStorage(void *storage) noexcept {
    return *static_cast<octopoly::Scene *>(storage);
}

static NSString *stringFromUtf8(std::string_view value, NSString *fallback) {
    NSString *text = [[NSString alloc] initWithBytes:value.data()
                                              length:value.size()
                                            encoding:NSUTF8StringEncoding];
    return text != nil ? text : fallback;
}

static void storeText(NSString *__strong *destination, std::string_view value,
                      NSString *fallback) {
    *destination = [stringFromUtf8(value, fallback) copy];
}

static void storeSceneResult(NSString *__strong *lastError,
                             const octopoly::SceneResult& result) {
    if (result.ok) {
        *lastError = @"";
    } else {
        storeText(lastError, result.error, @"Scene operation failed.");
    }
}

static BOOL storeMeshEditResult(NSString *__strong *lastError,
                                const octopoly::OperationResult& result) {
    if (result.ok) {
        *lastError = @"";
        return YES;
    }
    storeText(lastError, result.error, @"Selected-object mesh edit failed.");
    return NO;
}

static void storeCodecError(NSString *__strong *lastError, std::string_view message) {
    storeText(lastError, message, @"Project codec failed.");
}

static void storeGlbDiagnostics(NSString *__strong *destination,
                                const std::vector<octopoly::glb::Diagnostic>& diagnostics) {
    NSMutableString *text = [NSMutableString string];
    for (const octopoly::glb::Diagnostic& diagnostic : diagnostics) {
        NSString *message = stringFromUtf8(
            diagnostic.message, @"GLB import discarded unsupported visual data.");
        if (text.length != 0) {
            [text appendString:@"; "];
        }
        [text appendString:message];
    }
    *destination = [text copy];
}

static bool checkedAddSize(std::size_t first, std::size_t second,
                           std::size_t& result) noexcept {
    if (second > std::numeric_limits<std::size_t>::max() - first) {
        return false;
    }
    result = first + second;
    return true;
}

static bool checkedMultiplySize(std::size_t first, std::size_t second,
                                std::size_t& result) noexcept {
    if (first != 0 && second > std::numeric_limits<std::size_t>::max() / first) {
        return false;
    }
    result = first * second;
    return true;
}

static bool representableAsFloat(double value) noexcept {
    return std::isfinite(value) &&
           value >= -static_cast<double>(std::numeric_limits<float>::max()) &&
           value <= static_cast<double>(std::numeric_limits<float>::max()) &&
           std::isfinite(static_cast<float>(value));
}

static bool pointRepresentable(octopoly::Vec3 point) noexcept {
    return representableAsFloat(point.x) && representableAsFloat(point.y) &&
           representableAsFloat(point.z);
}

static bool dataHasMagic(NSData *data, const std::array<char, 8>& magic) noexcept {
    return data.length >= magic.size() &&
           std::memcmp(data.bytes, magic.data(), magic.size()) == 0;
}

static bool copyUtf8String(NSString *source, std::string& destination) {
    NSData *encoded = [source dataUsingEncoding:NSUTF8StringEncoding
                           allowLossyConversion:NO];
    if (encoded == nil) {
        return false;
    }
    const char *bytes = static_cast<const char *>(encoded.bytes);
    destination.assign(bytes != nullptr ? bytes : "",
                       static_cast<std::size_t>(encoded.length));
    return true;
}

static bool normalizedQuaternion(octopoly::Quaternion input,
                                 octopoly::Quaternion& output) noexcept {
    const double normSquared = input.x * input.x + input.y * input.y +
                               input.z * input.z + input.w * input.w;
    if (!std::isfinite(normSquared) || normSquared <= 0.0) {
        return false;
    }
    const double inverseNorm = 1.0 / std::sqrt(normSquared);
    output = {input.x * inverseNorm, input.y * inverseNorm,
              input.z * inverseNorm, input.w * inverseNorm};
    return std::isfinite(output.x) && std::isfinite(output.y) &&
           std::isfinite(output.z) && std::isfinite(output.w);
}

static octopoly::Quaternion multiplyQuaternion(const octopoly::Quaternion& left,
                                               const octopoly::Quaternion& right) noexcept {
    return {
        left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
        left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
        left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
        left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z,
    };
}

static BOOL prepareSceneSnapshot(
    const octopoly::Scene& scene,
    NSData *__strong *preparedTriangleVertexData,
    NSArray<SceneOutlinerItem *> *__strong *preparedOutlinerItems,
    NSString *__strong *lastError) noexcept {
    try {
        std::size_t triangleCount = 0;
        bool triangleCountFits = true;
        bool referencesValid = true;
        bool positionsRepresentable = true;

        for (const octopoly::SceneObject& object : scene.objects()) {
            const octopoly::Mesh& mesh = object.mesh();
            const octopoly::Mat4 world = object.worldTransform();
            mesh.visitTriangles([&](const octopoly::Triangle& triangle) {
                if (!triangleCountFits || !referencesValid || !positionsRepresentable) {
                    return;
                }
                std::size_t nextTriangleCount = 0;
                if (!checkedAddSize(triangleCount, 1, nextTriangleCount)) {
                    triangleCountFits = false;
                    return;
                }
                triangleCount = nextTriangleCount;
                for (const octopoly::VertexId vertexId : triangle.vertices) {
                    const octopoly::Vertex *vertex = mesh.vertex(vertexId);
                    if (vertex == nullptr) {
                        referencesValid = false;
                        return;
                    }
                    if (!pointRepresentable(world.transformPoint(vertex->position))) {
                        positionsRepresentable = false;
                        return;
                    }
                }
            });
        }

        std::size_t renderVertexCount = 0;
        std::size_t byteCapacity = 0;
        if (!triangleCountFits ||
            !checkedMultiplySize(triangleCount, 3, renderVertexCount) ||
            !checkedMultiplySize(renderVertexCount, sizeof(RenderVertex), byteCapacity) ||
            byteCapacity > std::numeric_limits<NSUInteger>::max()) {
            *lastError = @"Rendered scene triangle data exceeds this platform's capacity.";
            return NO;
        }
        if (!referencesValid) {
            *lastError = @"Scene mesh topology references a missing render vertex.";
            return NO;
        }
        if (!positionsRepresentable) {
            *lastError = @"A world-transformed render position is nonfinite or outside Float32 range.";
            return NO;
        }

        NSData *triangleData = nil;
        if (byteCapacity == 0) {
            triangleData = [NSData data];
        } else {
            NSMutableData *data =
                [NSMutableData dataWithLength:static_cast<NSUInteger>(byteCapacity)];
            if (data == nil || data.mutableBytes == nullptr) {
                *lastError = @"Unable to allocate rendered scene triangle data.";
                return NO;
            }

            auto *output = static_cast<RenderVertex *>(data.mutableBytes);
            std::size_t outputIndex = 0;
            for (const octopoly::SceneObject& object : scene.objects()) {
                const octopoly::Mesh& mesh = object.mesh();
                const octopoly::Mat4 world = object.worldTransform();
                mesh.visitTriangles([&](const octopoly::Triangle& triangle) {
                    for (const octopoly::VertexId vertexId : triangle.vertices) {
                        const octopoly::Vec3 position =
                            world.transformPoint(mesh.vertex(vertexId)->position);
                        output[outputIndex++] = {
                            static_cast<float>(position.x),
                            static_cast<float>(position.y),
                            static_cast<float>(position.z),
                        };
                    }
                });
            }
            if (outputIndex != renderVertexCount) {
                *lastError = @"Rendered scene size changed between preflight and packing.";
                return NO;
            }
            triangleData = data;
        }
        if (triangleData == nil) {
            *lastError = @"Unable to allocate rendered scene triangle data.";
            return NO;
        }

        if (scene.objects().size() > std::numeric_limits<NSUInteger>::max()) {
            *lastError = @"Scene outliner exceeds this platform's capacity.";
            return NO;
        }
        NSMutableArray<SceneOutlinerItem *> *items = [NSMutableArray
            arrayWithCapacity:static_cast<NSUInteger>(scene.objects().size())];
        if (items == nil) {
            *lastError = @"Unable to allocate scene outliner data.";
            return NO;
        }
        for (const octopoly::SceneObject& object : scene.objects()) {
            NSString *name = stringFromUtf8(object.name(), @"Invalid object name");
            SceneOutlinerItem *item = [[SceneOutlinerItem alloc]
                initWithObjectId:object.id()
                            name:name
                        selected:object.id() == scene.selectedObjectId()];
            if (item == nil) {
                *lastError = @"Unable to allocate scene outliner data.";
                return NO;
            }
            [items addObject:item];
        }
        NSArray<SceneOutlinerItem *> *outlinerItems = [items copy];
        if (outlinerItems == nil) {
            *lastError = @"Unable to allocate scene outliner data.";
            return NO;
        }

        *preparedTriangleVertexData = triangleData;
        *preparedOutlinerItems = outlinerItems;
        return YES;
    } catch (const std::bad_alloc&) {
        *lastError = @"Unable to allocate a complete scene snapshot.";
    } catch (...) {
        *lastError = @"Unexpected scene snapshot preparation failure.";
    }
    return NO;
}

static_assert(std::is_nothrow_move_assignable_v<octopoly::Scene>);

static void commitPreparedScene(
    void *storage, octopoly::Scene&& candidate,
    NSData *preparedTriangleVertexData,
    NSArray<SceneOutlinerItem *> *preparedOutlinerItems,
    NSData *__strong *cachedTriangleVertexData,
    NSArray<SceneOutlinerItem *> *__strong *cachedOutlinerItems) noexcept {
    sceneFromStorage(storage) = std::move(candidate);
    *cachedTriangleVertexData = preparedTriangleVertexData;
    *cachedOutlinerItems = preparedOutlinerItems;
}

static BOOL prepareAndCommitScene(
    void *storage, octopoly::Scene&& candidate,
    NSData *__strong *cachedTriangleVertexData,
    NSArray<SceneOutlinerItem *> *__strong *cachedOutlinerItems,
    NSString *__strong *lastError) noexcept {
    if (storage == nullptr) {
        *lastError = @"Scene storage is unavailable.";
        return NO;
    }
    NSData *preparedTriangleVertexData = nil;
    NSArray<SceneOutlinerItem *> *preparedOutlinerItems = nil;
    if (!prepareSceneSnapshot(candidate, &preparedTriangleVertexData,
                              &preparedOutlinerItems, lastError)) {
        return NO;
    }
    commitPreparedScene(storage, std::move(candidate), preparedTriangleVertexData,
                        preparedOutlinerItems, cachedTriangleVertexData,
                        cachedOutlinerItems);
    return YES;
}

template <typename Mutator>
static BOOL mutateSceneCandidate(
    void *storage, NSData *__strong *cachedTriangleVertexData,
    NSArray<SceneOutlinerItem *> *__strong *cachedOutlinerItems,
    NSString *__strong *lastError, NSString *allocationError,
    NSString *unexpectedError, Mutator&& mutator) noexcept {
    if (storage == nullptr) {
        *lastError = @"Scene storage is unavailable.";
        return NO;
    }
    try {
        octopoly::Scene candidate = sceneFromStorage(storage);
        if (!mutator(candidate)) {
            return NO;
        }
        if (!prepareAndCommitScene(storage, std::move(candidate),
                                   cachedTriangleVertexData, cachedOutlinerItems,
                                   lastError)) {
            return NO;
        }
        *lastError = @"";
        return YES;
    } catch (const std::bad_alloc&) {
        *lastError = allocationError;
    } catch (...) {
        *lastError = unexpectedError;
    }
    return NO;
}

static const octopoly::SceneObject *selectedObjectOrStoreError(
    const octopoly::Scene& scene, NSString *__strong *lastError) noexcept {
    const octopoly::SceneObject *selected = scene.selectedObject();
    if (selected == nullptr) {
        *lastError = @"No scene object is selected.";
    }
    return selected;
}

template <typename Mutator>
static BOOL mutateSelectedTransform(
    void *storage, NSData *__strong *cachedTriangleVertexData,
    NSArray<SceneOutlinerItem *> *__strong *cachedOutlinerItems,
    NSString *__strong *lastError, Mutator&& mutator) noexcept {
    if (storage == nullptr) {
        *lastError = @"Scene storage is unavailable.";
        return NO;
    }
    return mutateSceneCandidate(
        storage, cachedTriangleVertexData, cachedOutlinerItems, lastError,
        @"Unable to allocate a transformed scene candidate.",
        @"Unexpected transform operation failure.",
        [&](octopoly::Scene& candidate) {
            const octopoly::SceneObject *selected =
                selectedObjectOrStoreError(candidate, lastError);
            if (selected == nullptr) {
                return false;
            }
            const octopoly::ObjectId selectedId = selected->id();
            octopoly::Transform transform = selected->localTransform();
            if (!mutator(transform)) {
                *lastError = @"Transform delta must be finite and produce a normalized, nonzero-scale TRS.";
                return false;
            }
            const octopoly::SceneResult result =
                candidate.setLocalTransform(selectedId, transform);
            storeSceneResult(lastError, result);
            return result.ok;
        });
}

}  // namespace

@implementation SceneOutlinerItem

@synthesize objectId = _objectId;
@synthesize name = _name;
@synthesize selected = _selected;

- (instancetype)initWithObjectId:(uint64_t)objectId
                            name:(NSString *)name
                        selected:(BOOL)selected {
    self = [super init];
    if (self != nil) {
        _objectId = objectId;
        _name = [name copy];
        _selected = selected;
    }
    return self;
}

@end

@implementation MeshBridge

- (instancetype)init {
    self = [super init];
    if (self != nil) {
        _sceneStorage = nullptr;
        _cachedTriangleVertexData = nil;
        _cachedOutlinerItems = nil;
        _lastError = @"";
        _glbDiagnostics = @"";
        _loadedLegacyProject = NO;
        try {
            std::unique_ptr<octopoly::Scene> scene(new octopoly::Scene());
            const octopoly::SceneResult created =
                scene->createPrimitive(octopoly::Primitive::cube, "Cube");
            if (!created.ok) {
                storeSceneResult(&_lastError, created);
                return nil;
            }
            NSData *preparedTriangleVertexData = nil;
            NSArray<SceneOutlinerItem *> *preparedOutlinerItems = nil;
            if (!prepareSceneSnapshot(*scene, &preparedTriangleVertexData,
                                      &preparedOutlinerItems, &_lastError)) {
                return nil;
            }
            _cachedTriangleVertexData = preparedTriangleVertexData;
            _cachedOutlinerItems = preparedOutlinerItems;
            _sceneStorage = scene.release();
        } catch (const std::bad_alloc&) {
            _lastError = @"Unable to allocate the initial Cube scene.";
            return nil;
        } catch (...) {
            _lastError = @"Unexpected initial scene failure.";
            return nil;
        }
    }
    return self;
}

- (void)dealloc {
    delete static_cast<octopoly::Scene *>(_sceneStorage);
}

- (uint64_t)sceneRevision {
    return _sceneStorage != nullptr ? sceneFromStorage(_sceneStorage).revision() : 0;
}

- (uint64_t)selectedObjectId {
    return _sceneStorage != nullptr ? sceneFromStorage(_sceneStorage).selectedObjectId() : 0;
}

- (NSString *)lastError {
    return _lastError;
}

- (NSString *)glbDiagnostics {
    return _glbDiagnostics;
}

- (BOOL)loadedLegacyProject {
    return _loadedLegacyProject;
}

- (NSArray<SceneOutlinerItem *> *)outlinerItems {
    return _cachedOutlinerItems;
}

- (NSData *)triangleVertexData {
    return _cachedTriangleVertexData;
}

- (NSData *)encodedProjectData {
    if (_sceneStorage == nullptr) {
        _lastError = @"Scene storage is unavailable.";
        return nil;
    }
    try {
        octopoly::project::EncodeResult result =
            octopoly::project::encodeSceneProject(sceneFromStorage(_sceneStorage));
        if (!result.ok) {
            storeCodecError(&_lastError, result.error.message);
            return nil;
        }
        _lastError = @"";
        if (result.bytes.empty()) {
            return [NSData data];
        }
        return [NSData dataWithBytes:result.bytes.data() length:result.bytes.size()];
    } catch (const std::bad_alloc&) {
        _lastError = @"Unable to allocate encoded project data.";
    } catch (...) {
        _lastError = @"Unexpected project save failure.";
    }
    return nil;
}

- (BOOL)loadProjectData:(NSData *)data {
    if (_sceneStorage == nullptr) {
        _lastError = @"Scene storage is unavailable.";
        return NO;
    }
    static constexpr std::array<char, 8> sceneMagic{'O', 'C', 'T', 'O', 'S', 'C', 'N', 'E'};
    static constexpr std::array<char, 8> legacyMagic{'O', 'C', 'T', 'O', 'P', 'O', 'L', 'Y'};
    try {
        const auto *dataBytes = static_cast<const std::uint8_t *>(data.bytes);
        const std::span<const std::uint8_t> bytes(
            dataBytes, static_cast<std::size_t>(data.length));
        if (dataHasMagic(data, sceneMagic)) {
            octopoly::project::SceneDecodeResult decoded =
                octopoly::project::decodeSceneProject(bytes);
            if (!decoded.ok) {
                storeCodecError(&_lastError, decoded.error.message);
                return NO;
            }
            octopoly::Scene candidate = std::move(decoded.scene);
            if (!prepareAndCommitScene(_sceneStorage, std::move(candidate),
                                       &_cachedTriangleVertexData,
                                       &_cachedOutlinerItems, &_lastError)) {
                return NO;
            }
            _loadedLegacyProject = NO;
            _glbDiagnostics = @"";
            _lastError = @"";
            return YES;
        }
        if (dataHasMagic(data, legacyMagic)) {
            octopoly::project::DecodeResult decoded =
                octopoly::project::decodeProject(bytes);
            if (!decoded.ok) {
                storeCodecError(&_lastError, decoded.error.message);
                return NO;
            }
            octopoly::Scene candidate;
            const octopoly::SceneResult created =
                candidate.createMeshObject(std::move(decoded.mesh), "Legacy Project");
            if (!created.ok) {
                storeSceneResult(&_lastError, created);
                return NO;
            }
            if (!prepareAndCommitScene(_sceneStorage, std::move(candidate),
                                       &_cachedTriangleVertexData,
                                       &_cachedOutlinerItems, &_lastError)) {
                return NO;
            }
            _loadedLegacyProject = YES;
            _glbDiagnostics = @"";
            _lastError = @"";
            return YES;
        }
        _lastError = @"Project is neither an OCTOSCNE scene nor a legacy OCTOPOLY mesh.";
    } catch (const std::bad_alloc&) {
        _lastError = @"Unable to allocate a detached project scene.";
    } catch (...) {
        _lastError = @"Unexpected project load failure.";
    }
    return NO;
}

- (NSData *)encodedGlbData {
    if (_sceneStorage == nullptr) {
        _lastError = @"Scene storage is unavailable.";
        return nil;
    }
    try {
        const octopoly::SceneObject *selected =
            sceneFromStorage(_sceneStorage).selectedObject();
        if (selected == nullptr) {
            _lastError = @"Select one object before exporting GLB.";
            return nil;
        }
        octopoly::glb::EncodeResult result = octopoly::glb::encodeGlb(selected->mesh());
        if (!result.ok) {
            storeCodecError(&_lastError, result.error.message);
            return nil;
        }
        _lastError = @"";
        if (result.bytes.empty()) {
            return [NSData data];
        }
        return [NSData dataWithBytes:result.bytes.data() length:result.bytes.size()];
    } catch (const std::bad_alloc&) {
        _lastError = @"Unable to allocate encoded GLB data.";
    } catch (...) {
        _lastError = @"Unexpected GLB export failure.";
    }
    return nil;
}

- (BOOL)loadGlbData:(NSData *)data {
    if (_sceneStorage == nullptr) {
        _lastError = @"Scene storage is unavailable.";
        return NO;
    }
    try {
        const auto *dataBytes = static_cast<const std::uint8_t *>(data.bytes);
        const std::span<const std::uint8_t> bytes(
            dataBytes, static_cast<std::size_t>(data.length));
        octopoly::glb::DecodeResult decoded = octopoly::glb::decodeGlb(bytes);
        if (!decoded.ok) {
            storeCodecError(&_lastError, decoded.error.message);
            return NO;
        }

        octopoly::Scene candidate = sceneFromStorage(_sceneStorage);
        const octopoly::SceneResult created = candidate.createMeshObject(
            std::move(decoded.mesh), "Imported GLB");
        if (!created.ok) {
            storeSceneResult(&_lastError, created);
            return NO;
        }
        NSString *preparedDiagnostics = nil;
        storeGlbDiagnostics(&preparedDiagnostics, decoded.diagnostics);
        if (preparedDiagnostics == nil) {
            _lastError = @"Unable to allocate GLB import diagnostics.";
            return NO;
        }
        if (!prepareAndCommitScene(_sceneStorage, std::move(candidate),
                                   &_cachedTriangleVertexData,
                                   &_cachedOutlinerItems, &_lastError)) {
            return NO;
        }
        _glbDiagnostics = preparedDiagnostics;
        _lastError = @"";
        return YES;
    } catch (const std::bad_alloc&) {
        _lastError = @"Unable to allocate a new imported GLB scene object.";
    } catch (...) {
        _lastError = @"Unexpected GLB scene import failure.";
    }
    return NO;
}

- (BOOL)resetSceneCube {
    if (_sceneStorage == nullptr) {
        _lastError = @"Scene storage is unavailable.";
        return NO;
    }
    try {
        octopoly::Scene candidate;
        const octopoly::SceneResult created =
            candidate.createPrimitive(octopoly::Primitive::cube, "Cube");
        if (!created.ok) {
            storeSceneResult(&_lastError, created);
            return NO;
        }
        if (!prepareAndCommitScene(_sceneStorage, std::move(candidate),
                                   &_cachedTriangleVertexData,
                                   &_cachedOutlinerItems, &_lastError)) {
            return NO;
        }
        _glbDiagnostics = @"";
        _loadedLegacyProject = NO;
        _lastError = @"";
        return YES;
    } catch (const std::bad_alloc&) {
        _lastError = @"Unable to allocate a reset Cube scene.";
    } catch (...) {
        _lastError = @"Unexpected scene reset failure.";
    }
    return NO;
}

- (BOOL)selectObject:(uint64_t)objectId {
    return mutateSceneCandidate(
        _sceneStorage, &_cachedTriangleVertexData, &_cachedOutlinerItems,
        &_lastError, @"Unable to allocate a selection scene candidate.",
        @"Unexpected object selection failure.",
        [&](octopoly::Scene& candidate) {
            const octopoly::SceneResult result = candidate.selectObject(objectId);
            storeSceneResult(&_lastError, result);
            return result.ok;
        });
}

- (BOOL)deleteObject:(uint64_t)objectId {
    return mutateSceneCandidate(
        _sceneStorage, &_cachedTriangleVertexData, &_cachedOutlinerItems,
        &_lastError, @"Unable to allocate a deletion scene candidate.",
        @"Unexpected object deletion failure.",
        [&](octopoly::Scene& candidate) {
            const octopoly::SceneResult result = candidate.deleteObject(objectId);
            storeSceneResult(&_lastError, result);
            return result.ok;
        });
}

- (BOOL)renameObject:(uint64_t)objectId name:(NSString *)name {
    return mutateSceneCandidate(
        _sceneStorage, &_cachedTriangleVertexData, &_cachedOutlinerItems,
        &_lastError, @"Unable to allocate a renamed scene candidate.",
        @"Unexpected object rename failure.",
        [&](octopoly::Scene& candidate) {
            std::string utf8Name;
            if (!copyUtf8String(name, utf8Name)) {
                _lastError = @"Object name cannot be encoded as strict UTF-8.";
                return false;
            }
            const octopoly::SceneResult result =
                candidate.renameObject(objectId, std::move(utf8Name));
            storeSceneResult(&_lastError, result);
            return result.ok;
        });
}

- (BOOL)addPrimitive:(MeshBridgePrimitive)primitive {
    octopoly::Primitive corePrimitive{};
    const char *name = nullptr;
    switch (primitive) {
    case MeshBridgePrimitiveCube:
        corePrimitive = octopoly::Primitive::cube;
        name = "Cube";
        break;
    case MeshBridgePrimitivePlane:
        corePrimitive = octopoly::Primitive::plane;
        name = "Plane";
        break;
    case MeshBridgePrimitiveTetrahedron:
        corePrimitive = octopoly::Primitive::tetrahedron;
        name = "Tetrahedron";
        break;
    case MeshBridgePrimitiveCylinder:
        corePrimitive = octopoly::Primitive::cylinder;
        name = "Cylinder";
        break;
    case MeshBridgePrimitiveCone:
        corePrimitive = octopoly::Primitive::cone;
        name = "Cone";
        break;
    case MeshBridgePrimitiveUVSphere:
        corePrimitive = octopoly::Primitive::uvSphere;
        name = "UV Sphere";
        break;
    default:
        _lastError = @"Unsupported primitive control value.";
        return NO;
    }
    return mutateSceneCandidate(
        _sceneStorage, &_cachedTriangleVertexData, &_cachedOutlinerItems,
        &_lastError, @"Unable to allocate a primitive scene candidate.",
        @"Unexpected primitive creation failure.",
        [&](octopoly::Scene& candidate) {
            const octopoly::SceneResult result =
                candidate.createPrimitive(corePrimitive, name);
            storeSceneResult(&_lastError, result);
            return result.ok;
        });
}

- (BOOL)translateSelectedByX:(double)x y:(double)y z:(double)z {
    return mutateSelectedTransform(_sceneStorage, &_cachedTriangleVertexData,
                                   &_cachedOutlinerItems, &_lastError,
        [=](octopoly::Transform& transform) {
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
                return false;
            }
            transform.translation.x += x;
            transform.translation.y += y;
            transform.translation.z += z;
            return std::isfinite(transform.translation.x) &&
                   std::isfinite(transform.translation.y) &&
                   std::isfinite(transform.translation.z);
        });
}

- (BOOL)rotateSelectedAroundAxisX:(double)x
                                y:(double)y
                                z:(double)z
                          radians:(double)radians {
    return mutateSelectedTransform(_sceneStorage, &_cachedTriangleVertexData,
                                   &_cachedOutlinerItems, &_lastError,
        [=](octopoly::Transform& transform) {
            const double axisLengthSquared = x * x + y * y + z * z;
            if (!std::isfinite(axisLengthSquared) || axisLengthSquared <= 0.0 ||
                !std::isfinite(radians)) {
                return false;
            }
            const double inverseAxisLength = 1.0 / std::sqrt(axisLengthSquared);
            const double halfAngle = radians * 0.5;
            const double sine = std::sin(halfAngle);
            octopoly::Quaternion delta{x * inverseAxisLength * sine,
                                       y * inverseAxisLength * sine,
                                       z * inverseAxisLength * sine,
                                       std::cos(halfAngle)};
            octopoly::Quaternion composed = multiplyQuaternion(delta, transform.rotation);
            return normalizedQuaternion(composed, transform.rotation);
        });
}

- (BOOL)scaleSelectedByX:(double)x y:(double)y z:(double)z {
    return mutateSelectedTransform(_sceneStorage, &_cachedTriangleVertexData,
                                   &_cachedOutlinerItems, &_lastError,
        [=](octopoly::Transform& transform) {
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
                x == 0.0 || y == 0.0 || z == 0.0) {
                return false;
            }
            transform.scale.x *= x;
            transform.scale.y *= y;
            transform.scale.z *= z;
            return std::isfinite(transform.scale.x) &&
                   std::isfinite(transform.scale.y) &&
                   std::isfinite(transform.scale.z) &&
                   transform.scale.x != 0.0 && transform.scale.y != 0.0 &&
                   transform.scale.z != 0.0;
        });
}

- (BOOL)loopCut {
    return mutateSceneCandidate(
        _sceneStorage, &_cachedTriangleVertexData, &_cachedOutlinerItems,
        &_lastError, @"Unable to allocate a Loop Cut scene candidate.",
        @"Unexpected Loop Cut failure.",
        [&](octopoly::Scene& candidate) {
            const octopoly::SceneObject *selected =
                selectedObjectOrStoreError(candidate, &_lastError);
            if (selected == nullptr || selected->mesh().faces().empty()) {
                if (selected != nullptr) {
                    _lastError = @"The selected object has no face available for Loop Cut.";
                }
                return false;
            }
            return storeMeshEditResult(
                &_lastError,
                candidate.selectedLoopCut(selected->mesh().faces().front().id)) == YES;
        });
}

- (BOOL)knifeCut {
    return mutateSceneCandidate(
        _sceneStorage, &_cachedTriangleVertexData, &_cachedOutlinerItems,
        &_lastError, @"Unable to allocate a Knife Cut scene candidate.",
        @"Unexpected Knife Cut failure.",
        [&](octopoly::Scene& candidate) {
            const octopoly::SceneObject *selected =
                selectedObjectOrStoreError(candidate, &_lastError);
            if (selected == nullptr || selected->mesh().faces().empty()) {
                if (selected != nullptr) {
                    _lastError = @"The selected object has no face available for Knife Cut.";
                }
                return false;
            }
            return storeMeshEditResult(
                &_lastError,
                candidate.selectedKnifeCut(selected->mesh().faces().front().id,
                                            0, 0.35, 2, 0.65)) == YES;
        });
}

- (BOOL)inset {
    return mutateSceneCandidate(
        _sceneStorage, &_cachedTriangleVertexData, &_cachedOutlinerItems,
        &_lastError, @"Unable to allocate an Inset scene candidate.",
        @"Unexpected Inset failure.",
        [&](octopoly::Scene& candidate) {
            const octopoly::SceneObject *selected =
                selectedObjectOrStoreError(candidate, &_lastError);
            if (selected == nullptr || selected->mesh().faces().empty()) {
                if (selected != nullptr) {
                    _lastError = @"The selected object has no face available for Inset.";
                }
                return false;
            }
            return storeMeshEditResult(
                &_lastError,
                candidate.selectedInsetFace(selected->mesh().faces().front().id,
                                             0.25)) == YES;
        });
}

- (BOOL)merge {
    return mutateSceneCandidate(
        _sceneStorage, &_cachedTriangleVertexData, &_cachedOutlinerItems,
        &_lastError, @"Unable to allocate a Merge scene candidate.",
        @"Unexpected Merge failure.",
        [&](octopoly::Scene& candidate) {
            const octopoly::SceneObject *selected =
                selectedObjectOrStoreError(candidate, &_lastError);
            if (selected == nullptr || selected->mesh().vertices().size() < 2) {
                if (selected != nullptr) {
                    _lastError = @"The selected object needs two vertices for Merge.";
                }
                return false;
            }
            return storeMeshEditResult(
                &_lastError,
                candidate.selectedMergeVertices(selected->mesh().vertices()[0].id,
                                                 selected->mesh().vertices()[1].id)) == YES;
        });
}

- (BOOL)extrude {
    return mutateSceneCandidate(
        _sceneStorage, &_cachedTriangleVertexData, &_cachedOutlinerItems,
        &_lastError, @"Unable to allocate an Extrude scene candidate.",
        @"Unexpected Extrude failure.",
        [&](octopoly::Scene& candidate) {
            const octopoly::SceneObject *selected =
                selectedObjectOrStoreError(candidate, &_lastError);
            if (selected == nullptr || selected->mesh().faces().empty()) {
                if (selected != nullptr) {
                    _lastError = @"The selected object has no face available for Extrude.";
                }
                return false;
            }
            return storeMeshEditResult(
                &_lastError,
                candidate.selectedExtrudeFace(selected->mesh().faces().front().id,
                                               {0.0, 0.0, -0.5})) == YES;
        });
}

@end
