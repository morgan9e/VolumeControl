// This file is part of Background Music.
//
// Background Music is free software: you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation, either version 2 of the
// License, or (at your option) any later version.
//
// Background Music is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Background Music. If not, see <http://www.gnu.org/licenses/>.

//  VCAudioDevice.h
//  VCApp
//
//  Copyright © 2017, 2020 Kyle Neideck
//
//  A HAL audio device. Note that this class's only state is the AudioObjectID of the device.
//

#ifndef VCApp__VCAudioDevice
#define VCApp__VCAudioDevice

// PublicUtility Includes
#include "CAHALAudioDevice.h"


class VCAudioDevice
:
    public CAHALAudioDevice
{

#pragma mark Construction/Destruction

public:
                       VCAudioDevice(AudioObjectID inAudioDevice);
    /*!
     Creates a VCAudioDevice with the Audio Object ID of the device whose UID is inUID or, if no
     such device is found, kAudioObjectUnknown.

     @throws CAException If the HAL returns an error when queried for the device's ID.
     @see kAudioPlugInPropertyTranslateUIDToDevice in AudioHardwareBase.h.
     */
                       VCAudioDevice(CFStringRef inUID);
                       VCAudioDevice(const CAHALAudioDevice& inDevice);
    virtual            ~VCAudioDevice();

#if defined(__OBJC__)

    // Hack/workaround for Objective-C classes so we don't have to use pointers for instance
    // variables.
                       VCAudioDevice() : VCAudioDevice(kAudioObjectUnknown) { }

#endif /* defined(__OBJC__) */

                       operator AudioObjectID() const { return GetObjectID(); }

    /*!
     @return True if this device is VCDevice. (Specifically, the main instance of VCDevice, not
             the instance used for UI sounds.)
     @throws CAException If the HAL returns an error when queried.
     */
    bool               IsVCDevice() const { return IsVCDevice(false); };
    /*!
     @return True if this device is either the main instance of VCDevice (the device named
             "Background Music") or the instance used for UI sounds (the device named "Background
             Music (UI Sounds)").
     @throws CAException If the HAL returns an error when queried.
     */
    bool               IsVCDeviceInstance() const { return IsVCDevice(true); };

    /*!
     @return True if this device can be set as the output device in VCApp.
     @throws CAException If the HAL returns an error when queried.
     */
    bool               CanBeOutputDeviceInVCApp() const;

#pragma mark Available Controls

    bool               HasSettableMasterVolume(AudioObjectPropertyScope inScope) const;
    bool               HasSettableVirtualMasterVolume(AudioObjectPropertyScope inScope) const;
    bool               HasSettableMasterMute(AudioObjectPropertyScope inScope) const;

#pragma mark Control Values Accessors

    void               CopyMuteFrom(const VCAudioDevice inDevice,
                                    AudioObjectPropertyScope inScope);
    void               CopyVolumeFrom(const VCAudioDevice inDevice,
                                      AudioObjectPropertyScope inScope);

    bool               SetMasterVolumeScalar(AudioObjectPropertyScope inScope, Float32 inVolume);
    
    bool               GetVirtualMasterVolumeScalar(AudioObjectPropertyScope inScope,
                                                    Float32& outVirtualMasterVolume) const;
    bool               SetVirtualMasterVolumeScalar(AudioObjectPropertyScope inScope,
                                                    Float32 inVolume);

    bool               GetVirtualMasterBalance(AudioObjectPropertyScope inScope,
                                               Float32& outVirtualMasterBalance) const;

#pragma mark Implementation

private:
    bool               IsVCDevice(bool inIncludingUISoundsInstance) const;

    static OSStatus    AHSGetPropertyData(AudioObjectID inObjectID,
                                          const AudioObjectPropertyAddress* inAddress,
                                          UInt32* ioDataSize,
                                          void* outData);
    static OSStatus    AHSSetPropertyData(AudioObjectID inObjectID,
                                          const AudioObjectPropertyAddress* inAddress,
                                          UInt32 inDataSize,
                                          const void* inData);

};

#endif /* VCApp__VCAudioDevice */

