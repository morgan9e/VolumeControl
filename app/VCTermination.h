// This file is part of Background Music.
//
// Background Music is free software: you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation, either version 2 of the
// License, or (at your option) any later version.

#ifndef VCApp__VCTermination
#define VCApp__VCTermination

#import "VCAudioDeviceManager.h"

#pragma clang assume_nonnull begin

class VCTermination {
public:
    // Sets up signal handlers (SIGINT, SIGTERM, SIGQUIT) and std::terminate handler
    // to restore the default audio device before exiting.
    static void SetUpTerminationCleanUp(VCAudioDeviceManager* inAudioDevices);

private:
    static void CleanUpAudioDevices();
    static void* __nullable ExitSignalsProc(void* __nullable ignored);

    static sigset_t sExitSignals;
    static pthread_t sExitSignalsThread;
    static std::terminate_handler sOriginalTerminateHandler;
    static VCAudioDeviceManager* __nullable sAudioDevices;
};

#pragma clang assume_nonnull end

#endif /* VCApp__VCTermination */
