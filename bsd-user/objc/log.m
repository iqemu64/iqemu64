#include "qemu/osdep.h"
#include "qemu.h"
#import <Foundation/Foundation.h>

int
RedirectOutputToView(void *inFD, const char *buffer, int size)
{
    char *localbuf = (char *)malloc(size + 1);
    memcpy(localbuf, buffer, size);
    localbuf[size] = '\0';
    
    NSString *nsbuffer = [NSString stringWithUTF8String:localbuf];
    NSLog(@"%@", nsbuffer);
    
    free(localbuf);
    return size;
}

