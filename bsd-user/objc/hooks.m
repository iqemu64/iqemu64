/*
 * Copyright (c) 2020 上海芯竹科技有限公司
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu.h"
#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>

CVReturn
myCVPixelBufferCreate(CFAllocatorRef CV_NULLABLE allocator,
                      size_t width,
                      size_t height,
                      OSType pixelFormatType,
                      CFDictionaryRef CV_NULLABLE pixelBufferAttributes,
                      CV_RETURNS_RETAINED_PARAMETER CVPixelBufferRef CV_NULLABLE * CV_NONNULL pixelBufferOut)
{
    if(pixelBufferAttributes) {
        NSDictionary *attr = (__bridge NSDictionary *)pixelBufferAttributes;
        if([attr objectForKey:(__bridge id)kCVPixelBufferIOSurfacePropertiesKey] != nil) {
            NSMutableDictionary *mp = [attr mutableCopy];
            [mp removeObjectForKey:(__bridge id)kCVPixelBufferIOSurfacePropertiesKey];
            return CVPixelBufferCreate(allocator, width, height, pixelFormatType, (__bridge CFDictionaryRef _Nullable)(mp), pixelBufferOut);
        }
    }
    
    return CVPixelBufferCreate(allocator, width, height, pixelFormatType, pixelBufferAttributes, pixelBufferOut);
}
DYLD_INTERPOSE(myCVPixelBufferCreate, CVPixelBufferCreate)
