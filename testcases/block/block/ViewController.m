//
//  ViewController.m
//  block
//
//  Created by 黄钟吕 on 2020/7/28.
//  Copyright © 2020 MoltenCore. All rights reserved.
//

#import "ViewController.h"
#import "NSFoo.h"
#import <objc/runtime.h>

@interface ViewController ()

@end

typedef void (*fn_t)(const char *bigtime);
typedef void (^block_t)(const char *bigtime);

void (^global_Block)(void) = ^{
    NSLog(@"This is a global block");
};

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];

    __block int test_arg = 42;
    __block NSFoo *foo = [[NSFoo alloc] init];
    __block block_t block_in_block = ^(const char *bigtime){
        NSLog(@"Block in block: %s", bigtime);
    };
    
    dispatch_async(dispatch_get_main_queue(), ^{
        NSLog(@"test_arg is %d", test_arg);
        block_in_block("bigtime");
    });
    
    dispatch_async(dispatch_get_main_queue(), ^{
        NSLog(@"test_arg is %d, foo is %@", test_arg, foo);
    });
    
    dispatch_async(dispatch_get_main_queue(), global_Block);
    
    test_arg = 84;
    
    NSDictionary *dic = [[NSDictionary alloc] initWithObjectsAndKeys:self, @"self", nil];
    [dic enumerateKeysAndObjectsUsingBlock:^(id  _Nonnull key, id  _Nonnull obj, BOOL * _Nonnull stop) {
        NSLog(@"key is %@, obj is %@", key, obj);
    }];
    
    Method m = class_getInstanceMethod([NSDictionary class], @selector(enumerateKeysAndObjectsUsingBlock:));
    const char *type = method_getTypeEncoding(m);
    NSLog(@"type encoding of keysOfEntriesPassingTest: %s, function is %s, block is %s, NSDictionary is %s", type, @encode(fn_t), @encode(block_t), @encode(NSDictionary *));
}


@end
