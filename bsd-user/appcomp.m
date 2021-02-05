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
             rebind_symbols_image((const struct mach_header *)header, slide, (struct rebinding[1]){{"malloc", my_malloc, NULL }}, 1);
        } else if(!strcmp(name + l - 9, "/azurlane")) {
            Method m;
            
            m = class_getInstanceMethod(objc_getClass("EveryplaySurface"), sel_getUid("resizeBuffer:"));    // for azurlane's problem, we don't support CVOpenGLESTextureCache yet.
            method_setImplementation(m, (IMP)myEveryplaySurfaceresizeBuffer);
        } else if(!strcmp(name + l - 6, "/smoba")) {
            smoba_hack();
        }
    }
}
