// What the iOS app needs from platform_ios.mm besides the core's C interface (capi.h): the Photos
// identifier behind a picture path that the core returns (choosen_item_path).
#pragma once
#import <Foundation/Foundation.h>

#ifdef __cplusplus
#include <string>
std::string choosenPhotosAssetId(const std::string& path);  // "" if the path is not under /photos
std::string choosenPhotosRoot();                            // "/photos"
#endif

NS_ASSUME_NONNULL_BEGIN

@interface ChoosenPhotos : NSObject
/// The Photos local identifier of a picture path from choosen_item_path, or nil.
+ (nullable NSString*)assetIdForPath:(NSString*)path;
/// The photo folder the core reads ("/photos").
+ (NSString*)root;
@end

NS_ASSUME_NONNULL_END
