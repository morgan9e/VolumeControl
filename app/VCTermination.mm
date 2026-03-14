// This file is part of Background Music.
//
// Background Music is free software: you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation, either version 2 of the
// License, or (at your option) any later version.

#import "VCTermination.h"
#import "VC_Utils.h"

#import <signal.h>
#import <pthread.h>
#import <exception>


#pragma clang assume_nonnull begin

std::terminate_handler VCTermination::sOriginalTerminateHandler = std::get_terminate();
sigset_t               VCTermination::sExitSignals;
pthread_t              VCTermination::sExitSignalsThread;
VCAudioDeviceManager* __nullable VCTermination::sAudioDevices = nullptr;

void VCTermination::SetUpTerminationCleanUp(VCAudioDeviceManager* inAudioDevices) {
    sAudioDevices = inAudioDevices;

    // Block SIGINT/SIGTERM/SIGQUIT so our dedicated thread can handle them.
    sigemptyset(&sExitSignals);
    sigaddset(&sExitSignals, SIGINT);
    sigaddset(&sExitSignals, SIGTERM);
    sigaddset(&sExitSignals, SIGQUIT);
    pthread_sigmask(SIG_BLOCK, &sExitSignals, nullptr);

    pthread_create(&sExitSignalsThread, nullptr, ExitSignalsProc, nullptr);

    // Wrap std::terminate to clean up before crashing.
    sOriginalTerminateHandler = std::get_terminate();
    std::set_terminate([] {
        CleanUpAudioDevices();
        sOriginalTerminateHandler();
    });
}

void VCTermination::CleanUpAudioDevices() {
    if (sAudioDevices && [sAudioDevices isVirtualDeviceActive]) {
        [sAudioDevices unsetVCDeviceAsOSDefault];
    }
}

void* __nullable VCTermination::ExitSignalsProc(void* __nullable ignored) {
    #pragma unused (ignored)

    int signal = -1;

    while (signal != SIGINT && signal != SIGTERM && signal != SIGQUIT) {
        if (sigwait(&sExitSignals, &signal) != 0) {
            return nullptr;
        }
    }

    NSLog(@"VolumeControl: Received signal %d, cleaning up...", signal);
    CleanUpAudioDevices();

    // Re-raise with default handler to exit.
    pthread_sigmask(SIG_UNBLOCK, &sExitSignals, nullptr);
    raise(signal);
    return nullptr;
}

#pragma clang assume_nonnull end
