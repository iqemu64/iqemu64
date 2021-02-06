//
//  NSChattyString.m
//  HelloWorld
//
//  Created by 黄钟吕 on 2020/7/27.
//  Copyright © 2020 MoltenCore. All rights reserved.
//

#import "NSChattyString.h"

@interface NSChattyString ()

@property (nonatomic, strong) NSString *stringHolder;

@end

@implementation NSChattyString

-(instancetype)initWithCharactersNoCopy:(unichar *)characters length:(NSUInteger)length freeWhenDone:(BOOL)freeBuffer {
    self = [super init];
    if(self) {
        self.stringHolder = [[NSString alloc] initWithCharactersNoCopy:characters length:length freeWhenDone:freeBuffer];
    }
    return self;
}

- (NSUInteger)length {
    return self.stringHolder.length;
}

-(unichar)characterAtIndex:(NSUInteger)index {
    return [self.stringHolder characterAtIndex:index];
}

@end
