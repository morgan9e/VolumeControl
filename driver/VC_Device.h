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
//  VC_Device.h
//  VCDriver
//
//  Copyright © 2016, 2017, 2019 Kyle Neideck
//  Copyright © 2019 Gordon Childs
//  Copyright (C) 2013 Apple Inc. All Rights Reserved.
//
//  Based largely on SA_Device.h from Apple's SimpleAudioDriver Plug-In sample code.
//  https://developer.apple.com/library/mac/samplecode/AudioDriverExamples
//

#ifndef VCDriver__VC_Device
#define VCDriver__VC_Device

// SuperClass Includes
#include "VC_AbstractDevice.h"

// Local Includes
#include "VC_Types.h"
#include "VC_Stream.h"
#include "VC_VolumeControl.h"
#include "VC_MuteControl.h"

// PublicUtility Includes
#include "CAMutex.h"
#include "CAVolumeCurve.h"
#include "CARingBuffer.h"

// System Includes
#include <CoreFoundation/CoreFoundation.h>
#include <pthread.h>
#include <atomic>


class VC_Device
:
	public VC_AbstractDevice
{

#pragma mark Construction/Destruction

public:
    static VC_Device&			GetInstance();

private:
    static void					StaticInitializer();

protected:
                                VC_Device(AudioObjectID inObjectID,
										   const CFStringRef __nonnull inDeviceName,
                                           const CFStringRef __nonnull inDeviceUID,
										   const CFStringRef __nonnull inDeviceModelUID,
                                           AudioObjectID inInputStreamID,
                                           AudioObjectID inOutputStreamID,
                                           AudioObjectID inOutputVolumeControlID,
										   AudioObjectID inOutputMuteControlID);
    virtual						~VC_Device();

    virtual void				Activate();
    virtual void				Deactivate();

private:
    void                        InitLoopback();

#pragma mark Property Operations

public:
	virtual bool				HasProperty(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const;
	virtual bool				IsPropertySettable(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const;
	virtual UInt32				GetPropertyDataSize(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* __nullable inQualifierData) const;
	virtual void				GetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* __nullable inQualifierData, UInt32 inDataSize, UInt32& outDataSize, void* __nonnull outData) const;
	virtual void				SetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* __nullable inQualifierData, UInt32 inDataSize, const void* __nonnull inData);

#pragma mark Device Property Operations

private:
	bool						Device_HasProperty(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const;
	bool						Device_IsPropertySettable(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const;
	UInt32						Device_GetPropertyDataSize(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* __nullable inQualifierData) const;
	void						Device_GetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* __nullable inQualifierData, UInt32 inDataSize, UInt32& outDataSize, void* __nonnull outData) const;
	void						Device_SetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* __nullable inQualifierData, UInt32 inDataSize, const void* __nonnull inData);

#pragma mark IO Operations

public:
	void						StartIO(UInt32 inClientID);
	void						StopIO(UInt32 inClientID);

	void						GetZeroTimeStamp(Float64& outSampleTime, UInt64& outHostTime, UInt64& outSeed);

	void						WillDoIOOperation(UInt32 inOperationID, bool& outWillDo, bool& outWillDoInPlace) const;
	void						BeginIOOperation(UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo& inIOCycleInfo, UInt32 inClientID);
	void						DoIOOperation(AudioObjectID inStreamObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo& inIOCycleInfo, void* __nonnull ioMainBuffer, void* __nullable ioSecondaryBuffer);
	void						EndIOOperation(UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo& inIOCycleInfo, UInt32 inClientID);

private:
	void						ReadInputData(UInt32 inIOBufferFrameSize, Float64 inSampleTime, void* __nonnull outBuffer);
    void						WriteOutputData(UInt32 inIOBufferFrameSize, Float64 inSampleTime, const void* __nonnull inBuffer);

#pragma mark Accessors

public:
    Float64						GetSampleRate() const;
    void                        RequestSampleRate(Float64 inRequestedSampleRate);

private:
	/*!
     @return The Audio Object that has the ID inObjectID and belongs to this device.
     @throws CAException if there is no such Audio Object.
     */
	const VC_Object&		    GetOwnedObjectByID(AudioObjectID inObjectID) const;
	VC_Object&                 GetOwnedObjectByID(AudioObjectID inObjectID);

	/*! @return The number of Audio Objects belonging to this device, e.g. streams and controls. */
	UInt32 						GetNumberOfSubObjects() const;
	/*! @return The number of Audio Objects with output scope belonging to this device. */
    UInt32 						GetNumberOfOutputSubObjects() const;
	/*!
	 @return The number of control Audio Objects with output scope belonging to this device, e.g.
	         output volume and mute controls.
	 */
    UInt32 						GetNumberOfOutputControls() const;
    /*!
     Set the device's sample rate.

     Private because (after initialisation) this can only be called after asking the host to stop IO
     for the device. See VC_Device::PerformConfigChange and
     RequestDeviceConfigurationChange in AudioServerPlugIn.h.

     @param inNewSampleRate The sample rate.
     @param force If true, set the sample rate on the device even if it's currently set to
                  inNewSampleRate.
     @throws CAException if inNewSampleRate < 1 or if applying the sample rate to one of the streams
             fails.
     */
    void                        SetSampleRate(Float64 inNewSampleRate, bool force = false);

    /*! @return True if inObjectID is the ID of one of this device's streams. */
    inline bool                 IsStreamID(AudioObjectID inObjectID) const noexcept;

#pragma mark Hardware Accessors

private:
	void						_HW_Open();
	void						_HW_Close();
	kern_return_t				_HW_StartIO();
	void						_HW_StopIO();
	Float64						_HW_GetSampleRate() const;
	kern_return_t				_HW_SetSampleRate(Float64 inNewSampleRate);
	UInt32						_HW_GetRingBufferFrameSize() const;

#pragma mark Implementation

public:
    CFStringRef __nonnull		CopyDeviceUID() const { return mDeviceUID; }
    void                        AddClient(const AudioServerPlugInClientInfo* __nonnull inClientInfo);
    void                        RemoveClient(const AudioServerPlugInClientInfo* __nonnull inClientInfo);
    /*!
     Apply a change requested with VC_PlugIn::Host_RequestDeviceConfigurationChange. See
     PerformDeviceConfigurationChange in AudioServerPlugIn.h.
     */
	void						PerformConfigChange(UInt64 inChangeAction, void* __nullable inChangeInfo);
    /*! Cancel a change requested with VC_PlugIn::Host_RequestDeviceConfigurationChange. */
	void						AbortConfigChange(UInt64 inChangeAction, void* __nullable inChangeInfo);

private:
    static pthread_once_t		sStaticInitializer;
    static VC_Device* __nonnull    sInstance;

    #define kDeviceName                 "Volume Control"
    #define kDeviceManufacturerName     "VolumeControl"

	const CFStringRef __nonnull	mDeviceName;
	const CFStringRef __nonnull mDeviceUID;
	const CFStringRef __nonnull mDeviceModelUID;

    bool                        mIsHidden;

	enum
	{
		// The number of global/output sub-objects varies because the controls can be disabled.
								kNumberOfInputSubObjects			= 1,

								kNumberOfStreams					= 2,
								kNumberOfInputStreams				= 1,
								kNumberOfOutputStreams				= 1
	};

    CAMutex                     mStateMutex;
    CAMutex						mIOMutex;

    const Float64               kSampleRateDefault = 44100.0;
    // Before we can change sample rate, the host has to stop the device. The new sample rate is
    // stored here while it does.
    Float64                     mPendingSampleRate = kSampleRateDefault;

    #define kLoopbackRingBufferFrameSize    16384
    Float64                     mLoopbackSampleRate;
    CARingBuffer                mLoopbackRingBuffer;

    // TODO: a comment explaining why we need a clock for loopback-only mode
    struct {
        Float64					hostTicksPerFrame = 0.0;
        UInt64					numberTimeStamps  = 0;
        UInt64					anchorHostTime    = 0;
    }                           mLoopbackTime;

    VC_Stream                  mInputStream;
    VC_Stream                  mOutputStream;

    enum class ChangeAction : UInt64
    {
        SetSampleRate
    };

    VC_VolumeControl			mVolumeControl;
	VC_MuteControl				mMuteControl;

    // Simple IO client counter to replace mClients-based tracking.
    std::atomic<UInt32>         mIOClientCount{0};

};

#endif /* VCDriver__VC_Device */
