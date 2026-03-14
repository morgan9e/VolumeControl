// This file is part of Background Music.
//
// Background Music is free software: you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation, either version 2 of the
// License, or (at your option) any later version.

#import "VCAudioDeviceManager.h"

#import "VC_Types.h"
#import "VC_Utils.h"
#import "VCAudioDevice.h"
#import "VCPlayThrough.h"

#import "CAAtomic.h"
#import "CAAutoDisposer.h"
#import "CAHALAudioSystemObject.h"
#import "CAPropertyAddress.h"

static NSString* const kVCVolumeKey = @"VCVolume";


#pragma clang assume_nonnull begin

@implementation VCAudioDeviceManager {
    VCVirtualDevice* vcDevice;
    VCAudioDevice outputDevice;
    VCPlayThrough playThrough;
    NSRecursiveLock* stateLock;
    BOOL virtualDeviceActive;
    AudioObjectPropertyListenerBlock deviceListListener;
    AudioObjectPropertyListenerBlock defaultDeviceListener;
    AudioObjectPropertyListenerBlock volumeListenerBlock;
    AudioObjectID listenedDeviceID;
    dispatch_source_t debounceTimer;
}

#pragma mark Init / Dealloc

- (instancetype) init {
    if ((self = [super init])) {
        stateLock = [NSRecursiveLock new];
        outputDevice = kAudioObjectUnknown;
        listenedDeviceID = kAudioObjectUnknown;
        virtualDeviceActive = NO;

        try {
            vcDevice = new VCVirtualDevice;
        } catch (const CAException& e) {
            LogError("VCAudioDeviceManager::init: VCDevice not found. (%d)", e.GetError());
            self = nil;
            return self;
        }

        [self listenForDeviceChanges];
    }

    return self;
}

- (void) dealloc {
    @try {
        [stateLock lock];

        [self stopVolumeListener];

        if (deviceListListener) {
            VCLogAndSwallowExceptions("VCAudioDeviceManager::dealloc[devices]", [&] {
                CAHALAudioSystemObject().RemovePropertyListenerBlock(
                    CAPropertyAddress(kAudioHardwarePropertyDevices),
                    dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0),
                    deviceListListener);
            });
        }

        if (defaultDeviceListener) {
            VCLogAndSwallowExceptions("VCAudioDeviceManager::dealloc[defaultDevice]", [&] {
                CAHALAudioSystemObject().RemovePropertyListenerBlock(
                    CAPropertyAddress(kAudioHardwarePropertyDefaultOutputDevice),
                    dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0),
                    defaultDeviceListener);
            });
        }

        if (vcDevice) {
            delete vcDevice;
            vcDevice = nullptr;
        }
    } @finally {
        [stateLock unlock];
    }
}

#pragma mark Default Device

- (NSError* __nullable) setVCDeviceAsOSDefault {
    try {
        CAMemoryBarrier();
        vcDevice->SetAsOSDefault();
    } catch (const CAException& e) {
        VCLogExceptionIn("VCAudioDeviceManager::setVCDeviceAsOSDefault", e);
        return [NSError errorWithDomain:@kVCAppBundleID code:e.GetError() userInfo:nil];
    }

    return nil;
}

- (NSError* __nullable) unsetVCDeviceAsOSDefault {
    @try {
        [stateLock lock];

        AudioDeviceID outputDeviceID = outputDevice.GetObjectID();

        if (outputDeviceID == kAudioObjectUnknown) {
            return nil;
        }

        try {
            vcDevice->UnsetAsOSDefault(outputDeviceID);
        } catch (const CAException& e) {
            VCLogExceptionIn("VCAudioDeviceManager::unsetVCDeviceAsOSDefault", e);
            return [NSError errorWithDomain:@kVCAppBundleID code:e.GetError() userInfo:nil];
        }
    } @finally {
        [stateLock unlock];
    }

    return nil;
}

#pragma mark Accessors

- (VCVirtualDevice) vcDevice { return *vcDevice; }
- (CAHALAudioDevice) outputDevice { return outputDevice; }

- (BOOL) isVirtualDeviceActive {
    @try {
        [stateLock lock];
        return virtualDeviceActive;
    } @finally {
        [stateLock unlock];
    }
}

#pragma mark Device Evaluation

- (BOOL) deviceHasHardwareVolume:(const VCAudioDevice&)device {
    AudioObjectPropertyScope scope = kAudioDevicePropertyScopeOutput;

    try {
        if (device.HasSettableMasterVolume(scope) ||
            device.HasSettableVirtualMasterVolume(scope)) {
            return YES;
        }
    } catch (const CAException& e) {
        VCLogException(e);
    }

    return NO;
}

- (void) listenForDeviceChanges {
    VCAudioDeviceManager* __weak weakSelf = self;

    deviceListListener = ^(UInt32 inNumberAddresses,
                            const AudioObjectPropertyAddress* inAddresses) {
        #pragma unused (inNumberAddresses, inAddresses)
        // Debounce: coalesce rapid hotplug events into one evaluation.
        VCAudioDeviceManager* strongSelf = weakSelf;
        if (!strongSelf) return;

        @synchronized (strongSelf) {
            if (strongSelf->debounceTimer) {
                dispatch_source_cancel(strongSelf->debounceTimer);
            }
            strongSelf->debounceTimer = dispatch_source_create(
                DISPATCH_SOURCE_TYPE_TIMER, 0, 0,
                dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0));
            dispatch_source_set_timer(strongSelf->debounceTimer,
                                       dispatch_time(DISPATCH_TIME_NOW, 200 * NSEC_PER_MSEC),
                                       DISPATCH_TIME_FOREVER, 50 * NSEC_PER_MSEC);
            dispatch_source_set_event_handler(strongSelf->debounceTimer, ^{
                [weakSelf evaluateAndActivate];
            });
            dispatch_resume(strongSelf->debounceTimer);
        }
    };

    VCLogAndSwallowExceptions("VCAudioDeviceManager::listenForDeviceChanges", [&] {
        CAHALAudioSystemObject().AddPropertyListenerBlock(
            CAPropertyAddress(kAudioHardwarePropertyDevices),
            dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0),
            deviceListListener);
    });

    // When the default output device changes, re-attach volume listener to the new device.
    defaultDeviceListener = ^(UInt32 inNumberAddresses,
                               const AudioObjectPropertyAddress* inAddresses) {
        #pragma unused (inNumberAddresses, inAddresses)
        VCAudioDeviceManager* strongSelf = weakSelf;
        if (!strongSelf) return;

        AudioObjectID newDefault = [strongSelf currentDefaultOutputDevice];
        if (newDefault != kAudioObjectUnknown && newDefault != strongSelf->listenedDeviceID) {
            [strongSelf startVolumeListenerOnDevice:newDefault];
        }

        if (strongSelf.onVolumeChanged) {
            dispatch_async(dispatch_get_main_queue(), ^{
                strongSelf.onVolumeChanged();
            });
        }
    };

    VCLogAndSwallowExceptions("VCAudioDeviceManager::listenForDefaultDeviceChanges", [&] {
        CAHALAudioSystemObject().AddPropertyListenerBlock(
            CAPropertyAddress(kAudioHardwarePropertyDefaultOutputDevice),
            dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0),
            defaultDeviceListener);
    });
}

- (void) evaluateAndActivate {
    @try {
        [stateLock lock];

        CAHALAudioSystemObject audioSystem;
        UInt32 numDevices = audioSystem.GetNumberAudioDevices();

        if (numDevices == 0) {
            return;
        }

        CAAutoArrayDelete<AudioObjectID> devices(numDevices);
        audioSystem.GetAudioDevices(numDevices, devices);

        // Find a device needing software volume, and a fallback with hardware volume.
        AudioObjectID deviceNeedingSoftwareVolume = kAudioObjectUnknown;
        AudioObjectID fallbackDevice = kAudioObjectUnknown;

        for (UInt32 i = 0; i < numDevices; i++) {
            VCAudioDevice device(devices[i]);

            VCLogAndSwallowExceptions("evaluateAndActivate", [&] {
                if (!device.CanBeOutputDeviceInVCApp()) {
                    return;
                }

                if (![self deviceHasHardwareVolume:device]) {
                    deviceNeedingSoftwareVolume = devices[i];
                } else if (fallbackDevice == kAudioObjectUnknown) {
                    fallbackDevice = devices[i];
                }
            });
        }

        AudioObjectID targetDevice;
        BOOL needsVirtualDevice;

        if (deviceNeedingSoftwareVolume != kAudioObjectUnknown) {
            targetDevice = deviceNeedingSoftwareVolume;
            needsVirtualDevice = YES;
        } else if (fallbackDevice != kAudioObjectUnknown) {
            targetDevice = fallbackDevice;
            needsVirtualDevice = NO;
        } else {
            return;
        }

        // No change needed?
        if (targetDevice == outputDevice.GetObjectID() &&
            needsVirtualDevice == virtualDeviceActive) {
            return;
        }

        VCAudioDevice newOutputDevice(targetDevice);

        if (needsVirtualDevice) {
            NSLog(@"VolumeControl: Activating for device %u (no hardware volume)", targetDevice);

            try {
                vcDevice->SetHidden(false);

                playThrough.Deactivate();
                playThrough.SetDevices(vcDevice, &newOutputDevice);
                playThrough.Activate();

                outputDevice = newOutputDevice;
                virtualDeviceActive = YES;

                [self setVCDeviceAsOSDefault];
                [self restoreVolume];
                [self startVolumeListenerOnDevice:vcDevice->GetObjectID()];

                playThrough.Start();
                playThrough.StopIfIdle();
            } catch (const CAException& e) {
                NSLog(@"VolumeControl: Failed to start playthrough for device %u (error %d)",
                      targetDevice, e.GetError());
            }
        } else {
            NSLog(@"VolumeControl: Bypassing for device %u (has hardware volume)", targetDevice);

            [self saveVolume];
            playThrough.Deactivate();
            virtualDeviceActive = NO;
            outputDevice = newOutputDevice;

            [self unsetVCDeviceAsOSDefault];
            vcDevice->SetHidden(true);
            [self startVolumeListenerOnDevice:targetDevice];
        }
    } @finally {
        [stateLock unlock];
    }
}

#pragma mark Universal Volume

// Returns the current system default output device.
- (AudioObjectID) currentDefaultOutputDevice {
    AudioObjectID defaultDevice = kAudioObjectUnknown;
    VCLogAndSwallowExceptions("currentDefaultOutputDevice", [&] {
        CAHALAudioSystemObject audioSystem;
        defaultDevice = audioSystem.GetDefaultAudioDevice(false, false);
    });
    return defaultDevice;
}

- (float) volume {
    float vol = 0.5f;
    VCLogAndSwallowExceptions("volume", [&] {
        AudioObjectID devID = [self currentDefaultOutputDevice];
        if (devID == kAudioObjectUnknown) return;
        CAHALAudioDevice device(devID);
        AudioObjectPropertyScope scope = kAudioDevicePropertyScopeOutput;
        if (device.HasVolumeControl(scope, kMasterChannel)) {
            vol = device.GetVolumeControlScalarValue(scope, kMasterChannel);
        }
    });
    return vol;
}

- (void) setVolume:(float)vol {
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;

    VCLogAndSwallowExceptions("setVolume", [&] {
        AudioObjectID devID = [self currentDefaultOutputDevice];
        if (devID == kAudioObjectUnknown) return;
        CAHALAudioDevice device(devID);
        AudioObjectPropertyScope scope = kAudioDevicePropertyScopeOutput;
        if (device.HasVolumeControl(scope, kMasterChannel)) {
            device.SetVolumeControlScalarValue(scope, kMasterChannel, vol);
        }
    });
}

- (BOOL) isMuted {
    BOOL muted = NO;
    VCLogAndSwallowExceptions("isMuted", [&] {
        AudioObjectID devID = [self currentDefaultOutputDevice];
        if (devID == kAudioObjectUnknown) return;
        CAHALAudioDevice device(devID);
        AudioObjectPropertyScope scope = kAudioDevicePropertyScopeOutput;
        if (device.HasMuteControl(scope, kMasterChannel)) {
            muted = device.GetMuteControlValue(scope, kMasterChannel);
        }
    });
    return muted;
}

- (void) setMuted:(BOOL)muted {
    VCLogAndSwallowExceptions("setMuted", [&] {
        AudioObjectID devID = [self currentDefaultOutputDevice];
        if (devID == kAudioObjectUnknown) return;
        CAHALAudioDevice device(devID);
        AudioObjectPropertyScope scope = kAudioDevicePropertyScopeOutput;
        if (device.HasMuteControl(scope, kMasterChannel)) {
            device.SetMuteControlValue(scope, kMasterChannel, muted);
        }
    });
}

#pragma mark Volume Persistence

- (void) restoreVolume {
    Float32 saved = [[NSUserDefaults standardUserDefaults] floatForKey:kVCVolumeKey];
    if (saved <= 0.0f) {
        saved = 0.5f;  // Default if never saved.
    }

    VCLogAndSwallowExceptions("restoreVolume", [&] {
        AudioObjectPropertyScope scope = kAudioDevicePropertyScopeOutput;
        if (vcDevice->HasVolumeControl(scope, kMasterChannel)) {
            vcDevice->SetVolumeControlScalarValue(scope, kMasterChannel, saved);
        }
    });
}

- (void) saveVolume {
    VCLogAndSwallowExceptions("saveVolume", [&] {
        AudioObjectPropertyScope scope = kAudioDevicePropertyScopeOutput;
        if (vcDevice->HasVolumeControl(scope, kMasterChannel)) {
            Float32 vol = vcDevice->GetVolumeControlScalarValue(scope, kMasterChannel);
            [[NSUserDefaults standardUserDefaults] setFloat:vol forKey:kVCVolumeKey];
        }
    });
}

- (void) startVolumeListenerOnDevice:(AudioObjectID)deviceID {
    [self stopVolumeListener];

    VCAudioDeviceManager* __weak weakSelf = self;
    volumeListenerBlock = ^(UInt32 inNumberAddresses,
                             const AudioObjectPropertyAddress* inAddresses) {
        #pragma unused (inNumberAddresses, inAddresses)
        VCAudioDeviceManager* strongSelf = weakSelf;
        if (!strongSelf) return;

        if (strongSelf->virtualDeviceActive) {
            [strongSelf saveVolume];
        }

        if (strongSelf.onVolumeChanged) {
            dispatch_async(dispatch_get_main_queue(), ^{
                strongSelf.onVolumeChanged();
            });
        }
    };

    listenedDeviceID = deviceID;

    VCLogAndSwallowExceptions("startVolumeListener", [&] {
        CAHALAudioDevice device(deviceID);
        device.AddPropertyListenerBlock(
            CAPropertyAddress(kAudioDevicePropertyVolumeScalar,
                              kAudioDevicePropertyScopeOutput,
                              kMasterChannel),
            dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),
            volumeListenerBlock);
    });
}

- (void) stopVolumeListener {
    if (!volumeListenerBlock) return;

    VCLogAndSwallowExceptions("stopVolumeListener", [&] {
        if (CAHALAudioObject::ObjectExists(listenedDeviceID)) {
            CAHALAudioDevice device(listenedDeviceID);
            device.RemovePropertyListenerBlock(
                CAPropertyAddress(kAudioDevicePropertyVolumeScalar,
                                  kAudioDevicePropertyScopeOutput,
                                  kMasterChannel),
                dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),
                volumeListenerBlock);
        }
    });
    volumeListenerBlock = nil;
    listenedDeviceID = kAudioObjectUnknown;
}

@end

#pragma clang assume_nonnull end
