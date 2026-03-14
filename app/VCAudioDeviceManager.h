// This file is part of Background Music.
//
// Background Music is free software: you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation, either version 2 of the
// License, or (at your option) any later version.

#if defined(__cplusplus)

// Local Includes
#import "VCVirtualDevice.h"

// PublicUtility Includes
#import "CAHALAudioDevice.h"

#endif /* defined(__cplusplus) */

// System Includes
#import <Foundation/Foundation.h>
#import <CoreAudio/AudioHardwareBase.h>


#pragma clang assume_nonnull begin

@interface VCAudioDeviceManager : NSObject

// Called on any volume change (scroll, keyboard, system slider).
@property (nonatomic, copy) void (^onVolumeChanged)(void);

// Returns nil if the virtual device driver isn't installed.
- (instancetype) init;

- (NSError* __nullable) setVCDeviceAsOSDefault;
- (NSError* __nullable) unsetVCDeviceAsOSDefault;

#ifdef __cplusplus
- (VCVirtualDevice) vcDevice;
- (CAHALAudioDevice) outputDevice;
#endif

- (BOOL) isVirtualDeviceActive;

// Scans connected devices. If any lack hardware volume, activates the virtual device and starts
// playthrough. Otherwise bypasses. Called at launch and on device hotplug.
- (void) evaluateAndActivate;

// Universal volume control — works on both virtual device and native hardware.
- (float) volume;
- (void) setVolume:(float)vol;
- (BOOL) isMuted;
- (void) setMuted:(BOOL)muted;

@end

#pragma clang assume_nonnull end
