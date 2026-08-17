#import "MeshBridge.h"

#include "octopoly/mesh.hpp"

#include <cstddef>
#include <string>

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
    const std::vector<octopoly::Triangle> triangles = mesh.triangulate();
    NSMutableData *data = [NSMutableData dataWithCapacity:
        triangles.size() * 3 * sizeof(RenderVertex)];
    for (const octopoly::Triangle& triangle : triangles) {
        for (const octopoly::VertexId vertexId : triangle.vertices) {
            const octopoly::Vertex *vertex = mesh.vertex(vertexId);
            if (vertex == nullptr) {
                continue;
            }
            const RenderVertex packed{
                static_cast<float>(vertex->position.x),
                static_cast<float>(vertex->position.y),
                static_cast<float>(vertex->position.z),
            };
            [data appendBytes:&packed length:sizeof(packed)];
        }
    }
    return data;
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
