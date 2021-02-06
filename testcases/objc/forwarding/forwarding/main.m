//
//  main.m
//  forwarding
//
//  Created by 黄钟吕 on 2020/7/25.
//  Copyright © 2020 MoltenCore. All rights reserved.
//

#import <UIKit/UIKit.h>
#import "AppDelegate.h"
#import "ForwardTarget.h"
#import "ForwardInvocation.h"

int main(int argc, char * argv[]) {
    NSString * appDelegateClassName;
    @autoreleasepool {
        
        ForwardTarget *ft = [[ForwardTarget alloc] init];
        [ft RealOne:"str0" Str1:"str1" Str2:"str2" Str3:"str3" Str4:"str4"];
        ForwardInvocation *fi = [[ForwardInvocation alloc] init];
        [fi FakeOne:"str0" Str1:"str1" Str2:"str2" Str3:"str3" Str4:"str4"];
        
        // Setup code that might create autoreleased objects goes here.
        appDelegateClassName = NSStringFromClass([AppDelegate class]);
    }
    return UIApplicationMain(argc, argv, nil, appDelegateClassName);
}
