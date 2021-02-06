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

#import <Foundation/Foundation.h>
#import <CoreGraphics/CGColorConversionInfo.h>
#import <AudioToolbox/AUComponent.h>
#include <objc/runtime.h>
#import <Metal/Metal.h>

const char *nsstr_2_cstr(void *ns) {
    NSString *nss = (__bridge NSString *)ns;
    return [nss UTF8String];
}

typedef void *(* MallocFn)(size_t);
typedef void (* FreeFn)(void *);

const char *cfstring_dup_cstr(void *cf, MallocFn mymalloc, FreeFn myfree) {
    const CFStringEncoding encoding = kCFStringEncodingUTF8;
    CFStringRef src = (CFStringRef)cf;
    CFIndex length = CFStringGetLength(src);
    CFIndex maxSize = 1 + CFStringGetMaximumSizeForEncoding(length, encoding);
    char *buf = (char *)mymalloc(maxSize);
    if (CFStringGetCString(src, buf, maxSize, encoding)) {
        return buf;
    }
    myfree(buf);
    return NULL;
}

void *NSLog_ptr(void) {
    return NSLog;
}

void *NSLogv_ptr(void) {
    return NSLogv;
}

void *CFStringCreateWithFormat_ptr(void) {
    return CFStringCreateWithFormat;
}

void *CFStringCreateWithFormatAndArguments_ptr(void) {
    return CFStringCreateWithFormatAndArguments;
}

void *CFStringAppendFormat_ptr(void) {
    return CFStringAppendFormat;
}

void *CFStringAppendFormatAndArguments_ptr(void) {
    return CFStringAppendFormatAndArguments;
}

void *CGColorConversionInfoCreateFromList_ptr(void) {
    return CGColorConversionInfoCreateFromList;
}

void *CGColorConversionInfoCreateFromListWithArguments_ptr(void) {
    return CGColorConversionInfoCreateFromListWithArguments;
}

void *AudioUnitSetProperty_ptr(void) {
    return AudioUnitSetProperty;
}

void *_Block_object_assign_ptr(void) {
    return _Block_object_assign;
}

void *class_replaceMethod_ptr(void) {
    return class_replaceMethod;
}

void *method_setImplementation_ptr(void) {
    return method_setImplementation;
}

void *MTLCreateSystemDefaultDevice_ptr(void) {
    return MTLCreateSystemDefaultDevice;
}
