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
//  VCVirtualDevice.h
//  VCApp
//
//  Copyright © 2017 Kyle Neideck
//
//  The interface to VCDevice, the main virtual device published by VCDriver.
//
//  VCDevice is the device that appears as "Background Music" in programs that list the output
//  devices, e.g. System Preferences. It receives the system's audio, processes it and sends it to
//  VCApp by publishing an input stream. VCApp then plays the audio on the user's real output
//  device.
//
//  See VCDriver/VCDriver/VC_Device.h.
//

#ifndef VCApp__VCVirtualDevice
#define VCApp__VCVirtualDevice

// Superclass Includes
#include "VCAudioDevice.h"

// Local Includes
#include "VC_Types.h"

// PublicUtility Includes
#include "CACFString.h"


#pragma clang assume_nonnull begin

class VCVirtualDevice
:
    public VCAudioDevice
{

#pragma mark Construction/Destruction

public:
    /*!
     @throws CAException If VCDevice is not found or the HAL returns an error when queried for
                         VCDevice's current Audio Object ID.
     */
                        VCVirtualDevice();
    virtual            ~VCVirtualDevice();

#pragma mark Systemwide Default Device

public:
    /*!
     Set VCDevice as the default audio device for all processes.

     @throws CAException If the HAL responds with an error.
     */
    void                SetAsOSDefault();
    /*!
     Replace VCDevice as the default device with the output device.

     @throws CAException If the HAL responds with an error.
     */
    void                UnsetAsOSDefault(AudioDeviceID inOutputDeviceID);

    // Show or hide the virtual device in system device lists.
    void                SetHidden(bool inHidden);

};

#pragma clang assume_nonnull end

#endif /* VCApp__VCVirtualDevice */
