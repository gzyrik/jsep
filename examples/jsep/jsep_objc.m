#include <TargetConditionals.h>
#if TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#endif
#include "jsep.h"
void objcRTCSessionCallback(struct RTCSessionObserver* userdata, enum RTCSessionEvent event, const char* json, int length)
{
    NSData* data = [[NSData alloc] initWithBytes:json length:length];
    dispatch_async(dispatch_get_main_queue(), ^{
        NSError *error = nil;
        NSDictionary *dictionary= [NSJSONSerialization JSONObjectWithData: data options: 0 error: &error];
#if !__has_feature(objc_arc)
        [data release];
#endif
        [[NSNotificationCenter defaultCenter] postNotificationName:@RTCSessionNotification object:nil userInfo:dictionary];
    });
}

void objcRTCSocketCallback(struct RTCSocketObserver* userdata, RTCSocket* socket, const char* message, int length, enum RTCSocketEvent event)
{
    NSData* data = [[NSData alloc] initWithBytes:message length:length];
    dispatch_async(dispatch_get_main_queue(), ^{
        NSDictionary *dictionary= @{ @JsepMessage:data,
            @JespEvent:[NSNumber numberWithInt:event], @"JsepSocket":[NSValue valueWithPointer:socket] };
#if !__has_feature(objc_arc)
        [data release];
#endif
        [[NSNotificationCenter defaultCenter] postNotificationName:@RTCSocketNotification object:nil userInfo:dictionary];
    });
}
