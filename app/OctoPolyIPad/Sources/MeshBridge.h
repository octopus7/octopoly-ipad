#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface MeshBridge : NSObject {
@private
    void *_meshStorage;
    NSString *_lastError;
}

@property(nonatomic, readonly) NSUInteger revision;
@property(nonatomic, copy, readonly) NSString *lastError;
@property(nonatomic, readonly) NSData *triangleVertexData;

- (void)resetCube;
- (BOOL)loopCut;
- (BOOL)knifeCut;
- (BOOL)inset;
- (BOOL)merge;
- (BOOL)extrude;

@end

NS_ASSUME_NONNULL_END
