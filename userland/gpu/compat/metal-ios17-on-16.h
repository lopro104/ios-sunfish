/*
 * The iOS 17 Metal names that UniXNU's AdrenoMetal frontend (asteroids / feat/ipados27-gpu-parity) uses, for building it
 * against the iOS 16.5 SDK (theos): plain enum values, the same numbers as Apple's iOS 17 headers.
 */
#pragma once
#import <Metal/Metal.h>
#define MTLLanguageVersion3_1 ((MTLLanguageVersion)((3 << 16) + 1))
typedef NS_ENUM(NSUInteger, MTLBindingAccess) {
	MTLBindingAccessReadOnly = 0,
	MTLBindingAccessReadWrite = 1,
	MTLBindingAccessWriteOnly = 2,
};
