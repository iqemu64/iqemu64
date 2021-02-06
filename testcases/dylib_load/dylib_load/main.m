//
//  main.m
//  dylib_load
//
//  Created by 黄钟吕 on 2020/8/19.
//  Copyright © 2020 MoltenCore. All rights reserved.
//

#import <UIKit/UIKit.h>
#import "AppDelegate.h"

#import <dlfcn.h>

int main(int argc, char * argv[]) {
    
    void *p = dlopen("libsimdylib.dylib", RTLD_LAZY);
    if(p == NULL) {
        NSLog(@"Cannot load sim dylib.");
    } else {
        NSLog(@"Loading sim dylib success.");
    }
    
    NSString * appDelegateClassName;
    @autoreleasepool {
        // Setup code that might create autoreleased objects goes here.
        appDelegateClassName = NSStringFromClass([AppDelegate class]);
    }
    return UIApplicationMain(argc, argv, nil, appDelegateClassName);
}
