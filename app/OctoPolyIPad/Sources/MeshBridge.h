#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, MeshBridgePrimitive) {
    MeshBridgePrimitiveCube = 0,
    MeshBridgePrimitivePlane,
    MeshBridgePrimitiveTetrahedron,
    MeshBridgePrimitiveCylinder,
    MeshBridgePrimitiveCone,
    MeshBridgePrimitiveUVSphere,
};

@interface SceneOutlinerItem : NSObject {
@private
    uint64_t _objectId;
    NSString *_name;
    BOOL _selected;
}

@property(nonatomic, readonly) uint64_t objectId;
@property(nonatomic, copy, readonly) NSString *name;
@property(nonatomic, readonly, getter=isSelected) BOOL selected;

- (instancetype)initWithObjectId:(uint64_t)objectId
                            name:(NSString *)name
                        selected:(BOOL)selected NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

@end

@interface MeshBridge : NSObject {
@private
    void *_sceneStorage;
    NSData *_cachedTriangleVertexData;
    NSArray<SceneOutlinerItem *> *_cachedOutlinerItems;
    NSString *_lastError;
    NSString *_glbDiagnostics;
    BOOL _loadedLegacyProject;
}

- (nullable instancetype)init NS_DESIGNATED_INITIALIZER;

@property(nonatomic, readonly) uint64_t sceneRevision;
@property(nonatomic, readonly) uint64_t selectedObjectId;
@property(nonatomic, copy, readonly) NSString *lastError;
@property(nonatomic, copy, readonly) NSArray<SceneOutlinerItem *> *outlinerItems;
@property(nonatomic, readonly) NSData *triangleVertexData;
@property(nonatomic, readonly, nullable) NSData *encodedProjectData;
@property(nonatomic, readonly, nullable) NSData *encodedGlbData;
@property(nonatomic, copy, readonly) NSString *glbDiagnostics;
@property(nonatomic, readonly) BOOL loadedLegacyProject;

- (BOOL)resetSceneCube;
- (BOOL)loadProjectData:(NSData *)data;
- (BOOL)loadGlbData:(NSData *)data;

- (BOOL)selectObject:(uint64_t)objectId;
- (BOOL)deleteObject:(uint64_t)objectId;
- (BOOL)renameObject:(uint64_t)objectId name:(NSString *)name;
- (BOOL)addPrimitive:(MeshBridgePrimitive)primitive;

- (BOOL)translateSelectedByX:(double)x y:(double)y z:(double)z;
- (BOOL)rotateSelectedAroundAxisX:(double)x
                                y:(double)y
                                z:(double)z
                          radians:(double)radians;
- (BOOL)scaleSelectedByX:(double)x y:(double)y z:(double)z;

- (BOOL)loopCut;
- (BOOL)knifeCut;
- (BOOL)inset;
- (BOOL)merge;
- (BOOL)extrude;

@end

NS_ASSUME_NONNULL_END
