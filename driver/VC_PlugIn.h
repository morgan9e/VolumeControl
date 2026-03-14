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
//  VC_PlugIn.h
//  VCDriver
//
//  Copyright © 2016 Kyle Neideck
//  Copyright (C) 2013 Apple Inc. All Rights Reserved.
//
//  Based largely on SA_PlugIn.h from Apple's SimpleAudioDriver Plug-In sample code.
//  https://developer.apple.com/library/mac/samplecode/AudioDriverExamples
//

#ifndef __VCDriver__VC_PlugIn__
#define __VCDriver__VC_PlugIn__

// SuperClass Includes
#include "VC_Object.h"

// Local Includes
#include "VC_Types.h"

// PublicUtility Includes
#include "CAMutex.h"


class VC_PlugIn
:
	public VC_Object
{

#pragma mark Construction/Destruction
    
public:
    static VC_PlugIn&				GetInstance();

protected:
                                    VC_PlugIn();
	virtual							~VC_PlugIn();
    virtual void					Deactivate();
    
private:
    static void						StaticInitializer();
    
#pragma mark Host Access
    
public:
	static void						SetHost(AudioServerPlugInHostRef inHost)	{ sHost = inHost; }
	
	static void						Host_PropertiesChanged(AudioObjectID inObjectID, UInt32 inNumberAddresses, const AudioObjectPropertyAddress inAddresses[])	{ if(sHost != NULL) { sHost->PropertiesChanged(sHost, inObjectID, inNumberAddresses, inAddresses); } }
	static void						Host_RequestDeviceConfigurationChange(AudioObjectID inDeviceObjectID, UInt64 inChangeAction, void* inChangeInfo)			{ if(sHost != NULL) { sHost->RequestDeviceConfigurationChange(sHost, inDeviceObjectID, inChangeAction, inChangeInfo); } }

#pragma mark Property Operations
    
public:
	virtual bool					HasProperty(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const;
	virtual bool					IsPropertySettable(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const;
	virtual UInt32					GetPropertyDataSize(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData) const;
	virtual void					GetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32& outDataSize, void* outData) const;
	virtual void					SetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData);

#pragma mark Implementation
    
public:
    const CFStringRef               GetBundleID() const { return CFSTR(kVCDriverBundleID); }
    
private:
    CAMutex							mMutex;
    
    static pthread_once_t			sStaticInitializer;
    static VC_PlugIn*				sInstance;
	static AudioServerPlugInHostRef	sHost;

};

#endif /* __VCDriver__VC_PlugIn__ */

