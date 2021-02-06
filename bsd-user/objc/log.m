/*
 * Copyright (c) 2020 上海芯竹科技有限公司
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

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

