//
//  ForwardInvocation.m
//  forwarding
//
//  Created by 黄钟吕 on 2020/7/25.
//  Copyright © 2020 MoltenCore. All rights reserved.
//

#import "ForwardInvocation.h"

@implementation ForwardInvocation

- (void)RealOne:(const char *)str0
           Str1:(const char *)str1
           Str2:(const char *)str2
           Str3:(const char *)str3
           Str4:(const char *)str4
{
    NSLog(@"RealOne gets called %s.", str4);
}

- (void)forwardInvocation:(NSInvocation *)anInvocation {
    SEL sel = anInvocation.selector;
    if(sel == @selector(FakeOne:Str1:Str2:Str3:Str4:)) {
        anInvocation.selector = @selector(RealOne:Str1:Str2:Str3:Str4:);
        [anInvocation invoke];
    }
}
- (NSMethodSignature *)methodSignatureForSelector:(SEL)aSelector {
    if(aSelector == @selector(FakeOne:Str1:Str2:Str3:Str4:)) {
        return [NSMethodSignature signatureWithObjCTypes:"v@:*****"];
    }
    return nil;
}

@end
