//
//  ViewController.m
//  HelloWorld
//
//  Created by 黄钟吕 on 2020/7/27.
//  Copyright © 2020 MoltenCore. All rights reserved.
//

#import "ViewController.h"
#import "NSChattyString.h"

@interface ViewController ()

@end

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    // Do any additional setup after loading the view.
    [self showLabel];
}

- (IBAction)showLabel {
    NSChattyString *s = [[NSChattyString alloc] initWithString:@"Hello World!!!"];
    label.text = s;
    //label.text = nil;
}

@end
