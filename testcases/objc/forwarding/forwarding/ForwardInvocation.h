//
//  ForwardInvocation.h
//  forwarding
//
//  Created by 黄钟吕 on 2020/7/25.
//  Copyright © 2020 MoltenCore. All rights reserved.
//

#ifndef ForwardInvocation_h
#define ForwardInvocation_h

#import <Foundation/Foundation.h>

@interface ForwardInvocation : NSObject

- (void)FakeOne:(const char *)str0
           Str1:(const char *)str1
           Str2:(const char *)str2
           Str3:(const char *)str3
           Str4:(const char *)str4;

- (void)forwardInvocation:(NSInvocation *)anInvocation;
- (NSMethodSignature *)methodSignatureForSelector:(SEL)aSelector;

@end

#endif /* ForwardInvocation_h */
