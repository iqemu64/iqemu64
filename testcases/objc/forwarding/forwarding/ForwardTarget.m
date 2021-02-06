//
//  ForwardTarget.m
//  forwarding
//
//  Created by 黄钟吕 on 2020/7/25.
//  Copyright © 2020 MoltenCore. All rights reserved.
//

#import "ForwardTarget.h"

@interface ForwardTargetObj : NSObject
- (void)RealOne:(const char *)str0
           Str1:(const char *)str1
           Str2:(const char *)str2
           Str3:(const char *)str3
           Str4:(const char *)str4;
@end

@implementation ForwardTargetObj

- (void)RealOne:(const char *)str0
           Str1:(const char *)str1
           Str2:(const char *)str2
           Str3:(const char *)str3
           Str4:(const char *)str4
{
    NSLog(@"RealOne gets called %s.", str4);
}

@end

@implementation ForwardTarget

- (id)forwardingTargetForSelector:(SEL)aSelector {
    if(aSelector == @selector(RealOne:Str1:Str2:Str3:Str4:)) {
        return [[ForwardTargetObj alloc] init];
    }
    return nil;
}

@end
