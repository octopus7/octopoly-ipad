#import "MeshBridge.h"

#include "octopoly/mesh.hpp"
#include "octopoly/project_codec.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace {

struct RenderVertex {
    float x;
    float y;
    float z;
};

static octopoly::Mesh& meshFromStorage(void *storage) {
    return *static_cast<octopoly::Mesh *>(storage);
}

static BOOL storeResult(NSString *__strong *lastError,
                        const octopoly::OperationResult& result) {
    if (result.ok) {
        *lastError = @"";
        return YES;
    }
    *lastError = [NSString stringWithUTF8String:result.error.c_str()];
    return NO;
}

static void storeCodecError(NSString *__strong *lastError, std::string_view message) {
    NSString *text = [[NSString alloc] initWithBytes:message.data()
                                              length:message.size()
                                            encoding:NSUTF8StringEncoding];
    *lastError = text != nil ? text : @"Project codec failed.";
}

static bool checkedMultiplySize(std::size_t first, std::size_t second,
                                std::size_t& result) noexcept {
    if (first != 0 && second > std::numeric_limits<std::size_t>::max() / first) {
        return false;
    }
    result = first * second;
    return true;
}

}  // namespace

@implementation MeshBridge

- (instancetype)init {
    self = [super init];
    if (self != nil) {
        _meshStorage = new octopoly::Mesh(octopoly::Mesh::makeDefaultCube());
        _lastError = @"";
    }
    return self;
}

- (void)dealloc {
    delete static_cast<octopoly::Mesh *>(_meshStorage);
}

- (NSUInteger)revision {
    return static_cast<NSUInteger>(meshFromStorage(_meshStorage).revision());
}

- (NSString *)lastError {
    return _lastError;
}

- (NSData *)triangleVertexData {
    const octopoly::Mesh& mesh = meshFromStorage(_meshStorage);
    std::size_t triangleCount = 0;
    bool triangleCountFits = true;
    mesh.visitTriangles([&](const octopoly::Triangle&) {
        if (triangleCount == std::numeric_limits<std::size_t>::max()) {
            triangleCountFits = false;
            return;
        }
        ++triangleCount;
    });

    std::size_t renderVertexCount = 0;
    std::size_t byteCapacity = 0;
    if (!triangleCountFits ||
        !checkedMultiplySize(triangleCount, 3, renderVertexCount) ||
        !checkedMultiplySize(renderVertexCount, sizeof(RenderVertex), byteCapacity) ||
        byteCapacity > std::numeric_limits<NSUInteger>::max()) {
        _lastError = @"Rendered triangle data exceeds this platform's capacity.";
        return [NSData data];
    }

    NSMutableData *data =
        [NSMutableData dataWithCapacity:static_cast<NSUInteger>(byteCapacity)];
    if (data == nil) {
        _lastError = @"Unable to allocate rendered triangle data.";
        return [NSData data];
    }

    bool referencesValid = true;
    mesh.visitTriangles([&](const octopoly::Triangle& triangle) {
        if (!referencesValid) {
            return;
        }
        for (const octopoly::VertexId vertexId : triangle.vertices) {
            const octopoly::Vertex *vertex = mesh.vertex(vertexId);
            if (vertex == nullptr) {
                referencesValid = false;
                return;
            }
            const RenderVertex packed{
                static_cast<float>(vertex->position.x),
                static_cast<float>(vertex->position.y),
                static_cast<float>(vertex->position.z),
            };
            [data appendBytes:&packed length:sizeof(packed)];
        }
    });
    if (!referencesValid) {
        _lastError = @"Mesh topology references a missing render vertex.";
        return [NSData data];
    }
    return data;
}

- (NSData *)encodedProjectData {
    const octopoly::Mesh& mesh = meshFromStorage(_meshStorage);
    octopoly::project::EncodeResult result = octopoly::project::encodeProject(mesh);
    if (!result.ok) {
        storeCodecError(&_lastError, result.error.message);
        return nil;
    }
    _lastError = @"";
    return [NSData dataWithBytes:result.bytes.data() length:result.bytes.size()];
}

- (BOOL)loadProjectData:(NSData *)data {
    octopoly::Mesh& mesh = meshFromStorage(_meshStorage);
    const auto *dataBytes = static_cast<const std::uint8_t *>(data.bytes);
    const std::span<const std::uint8_t> bytes(dataBytes, static_cast<std::size_t>(data.length));
    const octopoly::project::InstallResult result = octopoly::project::installProject(mesh, bytes);
    if (!result.ok) {
        storeCodecError(&_lastError, result.error.message);
        return NO;
    }
    _lastError = @"";
    return YES;
}

- (void)resetCube {
    meshFromStorage(_meshStorage) = octopoly::Mesh::makeDefaultCube();
    _lastError = @"";
}

- (BOOL)loopCut {
    octopoly::Mesh& mesh = meshFromStorage(_meshStorage);
    if (mesh.faces().empty()) {
        _lastError = @"No face is available.";
        return NO;
    }
    return storeResult(&_lastError, mesh.loopCut(mesh.faces().front().id));
}

- (BOOL)knifeCut {
    octopoly::Mesh& mesh = meshFromStorage(_meshStorage);
    if (mesh.faces().empty()) {
        _lastError = @"No face is available.";
        return NO;
    }
    return storeResult(&_lastError,
                       mesh.knifeCut(mesh.faces().front().id, 0, 0.35, 2, 0.65));
}

- (BOOL)inset {
    octopoly::Mesh& mesh = meshFromStorage(_meshStorage);
    if (mesh.faces().empty()) {
        _lastError = @"No face is available.";
        return NO;
    }
    return storeResult(&_lastError, mesh.insetFace(mesh.faces().front().id, 0.25));
}

- (BOOL)merge {
    octopoly::Mesh& mesh = meshFromStorage(_meshStorage);
    if (mesh.vertices().size() < 2) {
        _lastError = @"Two vertices are required.";
        return NO;
    }
    return storeResult(&_lastError,
                       mesh.mergeVertices(mesh.vertices()[0].id, mesh.vertices()[1].id));
}

- (BOOL)extrude {
    octopoly::Mesh& mesh = meshFromStorage(_meshStorage);
    if (mesh.faces().empty()) {
        _lastError = @"No face is available.";
        return NO;
    }
    return storeResult(&_lastError,
                       mesh.extrudeFace(mesh.faces().front().id, {0.0, 0.0, -0.5}));
}

@end
