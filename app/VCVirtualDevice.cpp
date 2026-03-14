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

//
//  VCVirtualDevice.cpp
//  VCApp
//
//  Copyright © 2016-2019 Kyle Neideck
//  Copyright © 2017 Andrew Tonner
//

// Self Include
#include "VCVirtualDevice.h"

// Local Includes
#include "VC_Types.h"
#include "VC_Utils.h"

// PublicUtility Includes
#include "CADebugMacros.h"
#include "CAHALAudioSystemObject.h"


#pragma clang assume_nonnull begin

#pragma mark Construction/Destruction

VCVirtualDevice::VCVirtualDevice()
:
    VCAudioDevice(CFSTR(kVCDeviceUID))
{
    if(GetObjectID() == kAudioObjectUnknown)
    {
        LogError("VCVirtualDevice::VCVirtualDevice: Error getting VCDevice ID");
        Throw(CAException(kAudioHardwareIllegalOperationError));
    }
};

VCVirtualDevice::~VCVirtualDevice()
{
}

#pragma mark Systemwide Default Device

void VCVirtualDevice::SetAsOSDefault()
{
    DebugMsg("VCVirtualDevice::SetAsOSDefault: Setting the system's default audio device "
             "to VCDevice");

    CAHALAudioSystemObject audioSystem;

    AudioDeviceID defaultDevice = audioSystem.GetDefaultAudioDevice(false, false);
    AudioDeviceID systemDefaultDevice = audioSystem.GetDefaultAudioDevice(false, true);

    if(systemDefaultDevice == defaultDevice)
    {
        // The default system device is the same as the default device, so change both of them.
        audioSystem.SetDefaultAudioDevice(false, true, GetObjectID());
    }

    audioSystem.SetDefaultAudioDevice(false, false, GetObjectID());
}

void VCVirtualDevice::UnsetAsOSDefault(AudioDeviceID inOutputDeviceID)
{
    CAHALAudioSystemObject audioSystem;

    bool vcDeviceIsDefault =
            (audioSystem.GetDefaultAudioDevice(false, false) == GetObjectID());

    if(vcDeviceIsDefault)
    {
        DebugMsg("VCVirtualDevice::UnsetAsOSDefault: Setting the system's default output "
                 "device back to device %d", inOutputDeviceID);

        audioSystem.SetDefaultAudioDevice(false, false, inOutputDeviceID);
    }

    bool vcDeviceIsSystemDefault =
            (audioSystem.GetDefaultAudioDevice(false, true) == GetObjectID());

    if(vcDeviceIsSystemDefault)
    {
        DebugMsg("VCVirtualDevice::UnsetAsOSDefault: Setting the system's default system "
                 "output device back to device %d", inOutputDeviceID);

        audioSystem.SetDefaultAudioDevice(false, true, inOutputDeviceID);
    }
}

void VCVirtualDevice::SetHidden(bool inHidden)
{
    AudioObjectPropertyAddress addr = {
        kAudioDeviceCustomPropertyVCHidden,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMaster
    };

    CFBooleanRef value = inHidden ? kCFBooleanTrue : kCFBooleanFalse;
    OSStatus err = AudioObjectSetPropertyData(GetObjectID(), &addr, 0, nullptr, sizeof(CFBooleanRef), &value);

    if (err != kAudioHardwareNoError) {
        LogError("VCVirtualDevice::SetHidden: Failed (%d)", err);
    }
}

#pragma clang assume_nonnull end
