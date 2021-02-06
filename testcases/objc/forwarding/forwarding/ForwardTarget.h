//
//  ForwardTarget.h
//  forwarding
//
//  Created by 黄钟吕 on 2020/7/25.
//  Copyright © 2020 MoltenCore. All rights reserved.
//

#ifndef ForwardTarget_h
#define ForwardTarget_h

#import <Foundation/Foundation.h>

@interface ForwardTarget : NSObject

- (void)RealOne:(const char *)str0
           Str1:(const char *)str1
           Str2:(const char *)str2
           Str3:(const char *)str3
           Str4:(const char *)str4;

- (id)forwardingTargetForSelector:(SEL)aSelector;

@end

#endif /* ForwardTarget_h */
