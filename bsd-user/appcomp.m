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
#include "fishhook/fishhook.h"

#import <Foundation/Foundation.h>
#import <objc/runtime.h>

typedef NSString *(*fnNSStringInitWithFormatLocaleArguments)(id, SEL, NSString *, id, va_list);
static fnNSStringInitWithFormatLocaleArguments oldNSStringInitWithFormatLocaleArguments = nil;

//
// Begin smoba
NSString *
myNSStringInitWithFormatLocaleArguments(id zelf, SEL sel, NSString *format, id locale, va_list arguments)
{
    NSString *ret = oldNSStringInitWithFormatLocaleArguments(zelf, sel, format, locale, arguments);
    if([ret isEqualToString:@"0"])
        return @"0";
    else if([ret isEqualToString:@"1"])
        return @"1";
    else if([ret isEqualToString:@"2"])
        return @"2";
    else if([ret isEqualToString:@"3"])
        return @"3";
    else if([ret isEqualToString:@"4"])
        return @"4";
    else if([ret isEqualToString:@"5"])
        return @"5";
    else if([ret isEqualToString:@"6"])
        return @"6";
    else if([ret isEqualToString:@"7"])
        return @"7";
    else if([ret isEqualToString:@"8"])
        return @"8";
    else if([ret isEqualToString:@"9"])
        return @"9";

    return ret;
}


static void
smoba_hack()
{
    Class cls = objc_getClass("NSPlaceholderString");
    Method m = class_getInstanceMethod(cls, sel_getUid("initWithFormat:locale:arguments:"));
    oldNSStringInitWithFormatLocaleArguments = (fnNSStringInitWithFormatLocaleArguments)
        method_setImplementation(m, (IMP)myNSStringInitWithFormatLocaleArguments);
}

//
// Begin azurlane
void
myEveryplaySurfaceresizeBuffer(id clazz, SEL imp, id arg1)
{
    return;
}

//
// Begin speedmobile
void*
my_malloc(size_t size)
{
    void *ret = malloc(size);
    if(size >= 0x20000 && ret)
        memset(ret, 0, size);
    return ret;
}


static BOOL is_smoba = NO;
struct SELType {
    SEL sel;
    const char *types;
};

static struct SELType *smoba_selTypes = nil;
static const int smoba_selTypes_count = 13;
static const char *smoba_famous_sels[] = {
    ".cxx_destruct",  "v16@0:8",
    "delegate",       "@16@0:8",
    "mutableCopy",    "@16@0:8",
    "copy",           "@16@0:8",
    "application:openURL:sourceApplication:annotation:",              "B48@0:8@16@24@32@40",
    "application:openURL:options:",                                   "B40@0:8@16@24@32",
    "application:didRegisterForRemoteNotificationsWithDeviceToken:",  "v32@0:8@16@24",
    "application:didFailToRegisterForRemoteNotificationsWithError:",  "v32@0:8@16@24",
    "application:didReceiveRemoteNotification:",                      "v32@0:8@16@24",
    "application:didReceiveRemoteNotification:fetchCompletionHandler:",   "v40@0:8@16@24@?32",
    "application:continueUserActivity:restorationHandler:",           "B40@0:8@16@24@?32",
    "userNotificationCenter:didReceiveNotificationResponse:withCompletionHandler:",   "v40@0:8@16@24@?32",
    "userNotificationCenter:willPresentNotification:withCompletionHandler:",         "v40@0:8@16@24@?32",
    nil
};
const char *
smoba_class_addMethod(const char *selector)
{
    if(is_smoba) {
        SEL sel = (SEL)selector;
        static dispatch_once_t onceToken;
        dispatch_once(&onceToken, ^{
            NSMutableData* data = [NSMutableData dataWithLength:sizeof(struct SELType) * smoba_selTypes_count];
            smoba_selTypes = [data mutableBytes];
            for(int i = 0; i < smoba_selTypes_count; i ++) {
                smoba_selTypes[i].sel = sel_getUid(smoba_famous_sels[i * 2]);
                smoba_selTypes[i].types = smoba_famous_sels[i * 2 + 1];
            }
        });
        
        for(int i = 0; i < smoba_selTypes_count; i ++) {
            if(sel == smoba_selTypes[i].sel) {
                return smoba_selTypes[i].types;
            }
        }
    }
    
    return nil;
}

//
// Main functions.
void
app_compatibility_level(const struct MACH_HEADER *header, intptr_t slide)
{
    const char *name = get_name_by_header(header);
    if(name) {
        size_t l = strlen(name);
        if(!strcmp(name + l - 12, "/speedmobile")) {
            // speed mobile needs to set all zeros for some mallocs...
             rebind_symbols_image((void *)header, slide, (struct rebinding[1]){{"malloc", my_malloc, NULL }}, 1);
        } else if(!strcmp(name + l - 9, "/azurlane")) {
            Method m;
            
            m = class_getInstanceMethod(objc_getClass("EveryplaySurface"), sel_getUid("resizeBuffer:"));    // for azurlane's problem, we don't support CVOpenGLESTextureCache yet.
            method_setImplementation(m, (IMP)myEveryplaySurfaceresizeBuffer);
        } else if(!strcmp(name + l - 6, "/smoba")) {
            is_smoba = YES;
            smoba_hack();
        }
    }
}
