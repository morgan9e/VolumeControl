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
//  VC_Types.h
//  SharedSource
//
//  Copyright © 2016, 2017, 2019, 2024 Kyle Neideck
//

#ifndef SharedSource__VC_Types
#define SharedSource__VC_Types

// STL Includes
#if defined(__cplusplus)
#include <stdexcept>
#endif

// System Includes
#include <CoreAudio/AudioServerPlugIn.h>


#pragma mark IDs

#define kVCDriverBundleID           "com.volumecontrol.Driver"
#define kVCAppBundleID              "com.volumecontrol.App"

#define kVCDeviceUID                "VCDevice"
#define kVCDeviceModelUID           "VCDeviceModelUID"

// The object IDs for the audio objects this driver implements.
enum
{
	kObjectID_PlugIn                            = kAudioObjectPlugInObject,
	kObjectID_Device                            = 2,   // Belongs to kObjectID_PlugIn
	kObjectID_Stream_Input                      = 3,   // Belongs to kObjectID_Device
	kObjectID_Stream_Output                     = 4,   // Belongs to kObjectID_Device
	kObjectID_Volume_Output_Master              = 5,   // Belongs to kObjectID_Device
	kObjectID_Mute_Output_Master                = 6,   // Belongs to kObjectID_Device
};

// AudioObjectPropertyElement docs: "Elements are numbered sequentially where 0 represents the
// master element."
static const AudioObjectPropertyElement kMasterChannel = kAudioObjectPropertyElementMaster;

#pragma mark VCDevice Custom Properties

enum
{
    // A CFBoolean similar to kAudioDevicePropertyDeviceIsRunning except it ignores whether IO is running for
    // VCApp. This is so VCApp knows when it can stop doing IO to save CPU.
    kAudioDeviceCustomPropertyDeviceIsRunningSomewhereOtherThanVCApp = 'runo',

    // A UInt32 (0 or 1). The app sets this to hide/show the virtual device.
    kAudioDeviceCustomPropertyVCHidden = 'vchd',
};

#pragma mark VCDevice Custom Property Addresses

// For convenience.

static const AudioObjectPropertyAddress kVCRunningSomewhereOtherThanVCAppAddress = {
    kAudioDeviceCustomPropertyDeviceIsRunningSomewhereOtherThanVCApp,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMaster
};

#pragma mark Exceptions

#if defined(__cplusplus)

class VC_InvalidClientException : public std::runtime_error {
public:
    VC_InvalidClientException() : std::runtime_error("InvalidClient") { }
};

class VC_InvalidClientPIDException : public std::runtime_error {
public:
    VC_InvalidClientPIDException() : std::runtime_error("InvalidClientPID") { }
};

class VC_DeviceNotSetException : public std::runtime_error {
public:
    VC_DeviceNotSetException() : std::runtime_error("DeviceNotSet") { }
};

#endif

// Assume we've failed to start the output device if it isn't running IO after this timeout expires.
//
// Currently set to 30s because some devices, e.g. AirPlay, can legitimately take that long to start.
static const UInt64 kStartIOTimeoutNsec = 30 * NSEC_PER_SEC;

#endif /* SharedSource__VC_Types */
