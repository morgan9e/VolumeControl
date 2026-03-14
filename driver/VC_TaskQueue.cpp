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
//  VC_TaskQueue.cpp
//  VCDriver
//
//  Copyright © 2016 Kyle Neideck
//

// Self Include
#include "VC_TaskQueue.h"

// Local Includes
#include "VC_Types.h"
#include "VC_Utils.h"
#include "VC_PlugIn.h"

// PublicUtility Includes
#include "CAException.h"
#include "CADebugMacros.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#include "CAAtomic.h"
#pragma clang diagnostic pop

// System Includes
#include <mach/mach_init.h>
#include <mach/mach_time.h>
#include <mach/task.h>


#pragma clang assume_nonnull begin

#pragma mark Construction/destruction

VC_TaskQueue::VC_TaskQueue()
:
    // The inline documentation for thread_time_constraint_policy.period says "A value of 0 indicates that there is no
    // inherent periodicity in the computation". So I figure setting the period to 0 means the scheduler will take as long
    // as it wants to wake our real-time thread, which is fine for us, but once it has only other real-time threads can
    // preempt us. (And that's only if they won't make our computation take longer than kRealTimeThreadMaximumComputationNs).
    mRealTimeThread(&VC_TaskQueue::RealTimeThreadProc,
                    this,
                    /* inPeriod = */ 0,
                    NanosToAbsoluteTime(kRealTimeThreadNominalComputationNs),
                    NanosToAbsoluteTime(kRealTimeThreadMaximumComputationNs),
                    /* inIsPreemptible = */ true),
    mNonRealTimeThread(&VC_TaskQueue::NonRealTimeThreadProc, this)
{
    // Init the semaphores
    auto createSemaphore = [] () {
        semaphore_t theSemaphore;
        kern_return_t theError = semaphore_create(mach_task_self(), &theSemaphore, SYNC_POLICY_FIFO, 0);
        
        VC_Utils::ThrowIfMachError("VC_TaskQueue::VC_TaskQueue", "semaphore_create", theError);
        
        ThrowIf(theSemaphore == SEMAPHORE_NULL,
                CAException(kAudioHardwareUnspecifiedError),
                "VC_TaskQueue::VC_TaskQueue: Could not create semaphore");
        
        return theSemaphore;
    };
    
    mRealTimeThreadWorkQueuedSemaphore = createSemaphore();
    mNonRealTimeThreadWorkQueuedSemaphore = createSemaphore();
    mRealTimeThreadSyncTaskCompletedSemaphore = createSemaphore();
    mNonRealTimeThreadSyncTaskCompletedSemaphore = createSemaphore();
    
    // Pre-allocate enough tasks in mNonRealTimeThreadTasksFreeList that the real-time threads should never have to
    // allocate memory when adding a task to the non-realtime queue.
    for(UInt32 i = 0; i < kNonRealTimeThreadTaskBufferSize; i++)
    {
        VC_Task* theTask = new VC_Task;
        mNonRealTimeThreadTasksFreeList.push_NA(theTask);
    }
    
    // Start the worker threads
    mRealTimeThread.Start();
    mNonRealTimeThread.Start();
}

VC_TaskQueue::~VC_TaskQueue()
{
    // Join the worker threads
    VCLogAndSwallowExceptionsMsg("VC_TaskQueue::~VC_TaskQueue", "QueueSync", ([&] {
        QueueSync(kVCTaskStopWorkerThread, /* inRunOnRealtimeThread = */ true);
        QueueSync(kVCTaskStopWorkerThread, /* inRunOnRealtimeThread = */ false);
    }));

    // Destroy the semaphores
    auto destroySemaphore = [] (semaphore_t inSemaphore) {
        kern_return_t theError = semaphore_destroy(mach_task_self(), inSemaphore);
        
        VC_Utils::LogIfMachError("VC_TaskQueue::~VC_TaskQueue", "semaphore_destroy", theError);
    };
    
    destroySemaphore(mRealTimeThreadWorkQueuedSemaphore);
    destroySemaphore(mNonRealTimeThreadWorkQueuedSemaphore);
    destroySemaphore(mRealTimeThreadSyncTaskCompletedSemaphore);
    destroySemaphore(mNonRealTimeThreadSyncTaskCompletedSemaphore);
    
    VC_Task* theTask;
    
    // Delete the tasks in the non-realtime tasks free list
    while((theTask = mNonRealTimeThreadTasksFreeList.pop_atomic()) != NULL)
    {
        delete theTask;
    }
    
    // Delete any tasks left on the non-realtime queue that need to be
    while((theTask = mNonRealTimeThreadTasks.pop_atomic()) != NULL)
    {
        if(!theTask->IsSync())
        {
            delete theTask;
        }
    }
}

//static
UInt32  VC_TaskQueue::NanosToAbsoluteTime(UInt32 inNanos)
{
    // Converts a duration from nanoseconds to absolute time (i.e. number of bus cycles). Used for calculating
    // the real-time thread's time constraint policy.
    
    mach_timebase_info_data_t theTimebaseInfo;
    mach_timebase_info(&theTimebaseInfo);
    
    Float64 theTicksPerNs = static_cast<Float64>(theTimebaseInfo.denom) / theTimebaseInfo.numer;
    return static_cast<UInt32>(inNanos * theTicksPerNs);
}

#pragma mark Task queueing

void    VC_TaskQueue::QueueAsync_SendPropertyNotification(AudioObjectPropertySelector inProperty, AudioObjectID inDeviceID)
{
    DebugMsg("VC_TaskQueue::QueueAsync_SendPropertyNotification: Queueing property notification. inProperty=%u inDeviceID=%u",
             inProperty,
             inDeviceID);
    VC_Task theTask(kVCTaskSendPropertyNotification, /* inIsSync = */ false, inProperty, inDeviceID);
    QueueOnNonRealtimeThread(theTask);
}

UInt64    VC_TaskQueue::QueueSync(VC_TaskID inTaskID, bool inRunOnRealtimeThread, UInt64 inTaskArg1, UInt64 inTaskArg2)
{
    DebugMsg("VC_TaskQueue::QueueSync: Queueing task synchronously to be processed on the %s thread. inTaskID=%d inTaskArg1=%llu inTaskArg2=%llu",
             (inRunOnRealtimeThread ? "realtime" : "non-realtime"),
             inTaskID,
             inTaskArg1,
             inTaskArg2);
    
    // Create the task
    VC_Task theTask(inTaskID, /* inIsSync = */ true, inTaskArg1, inTaskArg2);
    
    // Add the task to the queue
    TAtomicStack<VC_Task>& theTasks = (inRunOnRealtimeThread ? mRealTimeThreadTasks : mNonRealTimeThreadTasks);
    theTasks.push_atomic(&theTask);
    
    // Wake the worker thread so it'll process the task. (Note that semaphore_signal has an implicit barrier.)
    kern_return_t theError = semaphore_signal(inRunOnRealtimeThread ? mRealTimeThreadWorkQueuedSemaphore : mNonRealTimeThreadWorkQueuedSemaphore);
    VC_Utils::ThrowIfMachError("VC_TaskQueue::QueueSync", "semaphore_signal", theError);
    
    // Wait until the task has been processed.
    //
    // The worker thread signals all threads waiting on this semaphore when it finishes a task. The comments in WorkerThreadProc
    // explain why we have to check the condition in a loop here.
    bool didLogTimeoutMessage = false;
    while(!theTask.IsComplete())
    {
        semaphore_t theTaskCompletedSemaphore =
            inRunOnRealtimeThread ? mRealTimeThreadSyncTaskCompletedSemaphore : mNonRealTimeThreadSyncTaskCompletedSemaphore;
        // TODO: Because the worker threads use semaphore_signal_all instead of semaphore_signal, a thread can miss the signal if
        //       it isn't waiting at the right time. Using a timeout for now as a temporary fix so threads don't get stuck here.
        theError = semaphore_timedwait(theTaskCompletedSemaphore,
                                       (mach_timespec_t){ 0, kRealTimeThreadMaximumComputationNs * 4 });
        
        if(theError == KERN_OPERATION_TIMED_OUT)
        {
            if(!didLogTimeoutMessage && inRunOnRealtimeThread)
            {
                DebugMsg("VC_TaskQueue::QueueSync: Task %d taking longer than expected.", theTask.GetTaskID());
                didLogTimeoutMessage = true;
            }
        }
        else
        {
            VC_Utils::ThrowIfMachError("VC_TaskQueue::QueueSync", "semaphore_timedwait", theError);
        }
        
        CAMemoryBarrier();
    }
    
    if(didLogTimeoutMessage)
    {
        DebugMsg("VC_TaskQueue::QueueSync: Late task %d finished.", theTask.GetTaskID());
    }
    
    if(theTask.GetReturnValue() != INT64_MAX)
    {
        DebugMsg("VC_TaskQueue::QueueSync: Task %d returned %llu.", theTask.GetTaskID(), theTask.GetReturnValue());
    }
    
    return theTask.GetReturnValue();
}

void   VC_TaskQueue::QueueOnNonRealtimeThread(VC_Task inTask)
{
    // Add the task to our task list
    VC_Task* freeTask = mNonRealTimeThreadTasksFreeList.pop_atomic();
    
    if(freeTask == NULL)
    {
        LogWarning("VC_TaskQueue::QueueOnNonRealtimeThread: No pre-allocated tasks left in the free list. Allocating new task.");
        freeTask = new VC_Task;
    }
    
    *freeTask = inTask;
    
    mNonRealTimeThreadTasks.push_atomic(freeTask);
    
    // Signal the worker thread to process the task. (Note that semaphore_signal has an implicit barrier.)
    kern_return_t theError = semaphore_signal(mNonRealTimeThreadWorkQueuedSemaphore);
    VC_Utils::ThrowIfMachError("VC_TaskQueue::QueueOnNonRealtimeThread", "semaphore_signal", theError);
}

#pragma mark Worker threads

void    VC_TaskQueue::AssertCurrentThreadIsRTWorkerThread(const char* inCallerMethodName)
{
#if DEBUG  // This Assert macro always checks the condition, even in release builds if the compiler doesn't optimise it away
    if(!mRealTimeThread.IsCurrentThread())
    {
        DebugMsg("%s should only be called on the realtime worker thread.", inCallerMethodName);
        __ASSERT_STOP;  // TODO: Figure out a better way to assert with a formatted message
    }
    
    Assert(mRealTimeThread.IsTimeConstraintThread(), "mRealTimeThread should be in a time-constraint priority band.");
#else
    #pragma unused (inCallerMethodName)
#endif
}

//static
void* __nullable    VC_TaskQueue::RealTimeThreadProc(void* inRefCon)
{
    DebugMsg("VC_TaskQueue::RealTimeThreadProc: The realtime worker thread has started");
    
    VC_TaskQueue* refCon = static_cast<VC_TaskQueue*>(inRefCon);
    refCon->WorkerThreadProc(refCon->mRealTimeThreadWorkQueuedSemaphore,
                             refCon->mRealTimeThreadSyncTaskCompletedSemaphore,
                             &refCon->mRealTimeThreadTasks,
                             NULL,
                             [&] (VC_Task* inTask) { return refCon->ProcessRealTimeThreadTask(inTask); });
    
    return NULL;
}

//static
void* __nullable    VC_TaskQueue::NonRealTimeThreadProc(void* inRefCon)
{
    DebugMsg("VC_TaskQueue::NonRealTimeThreadProc: The non-realtime worker thread has started");
    
    VC_TaskQueue* refCon = static_cast<VC_TaskQueue*>(inRefCon);
    refCon->WorkerThreadProc(refCon->mNonRealTimeThreadWorkQueuedSemaphore,
                             refCon->mNonRealTimeThreadSyncTaskCompletedSemaphore,
                             &refCon->mNonRealTimeThreadTasks,
                             &refCon->mNonRealTimeThreadTasksFreeList,
                             [&] (VC_Task* inTask) { return refCon->ProcessNonRealTimeThreadTask(inTask); });
    
    return NULL;
}

void    VC_TaskQueue::WorkerThreadProc(semaphore_t inWorkQueuedSemaphore, semaphore_t inSyncTaskCompletedSemaphore, TAtomicStack<VC_Task>* inTasks, TAtomicStack2<VC_Task>* __nullable inFreeList, std::function<bool(VC_Task*)> inProcessTask)
{
    bool theThreadShouldStop = false;
    
    while(!theThreadShouldStop)
    {
        // Wait until a thread signals that it's added tasks to the queue.
        //
        // Note that we don't have to hold any lock before waiting. If the semaphore is signalled before we begin waiting we'll
        // still get the signal after we do.
        kern_return_t theError = semaphore_wait(inWorkQueuedSemaphore);
        VC_Utils::ThrowIfMachError("VC_TaskQueue::WorkerThreadProc", "semaphore_wait", theError);
        
        // Fetch the tasks from the queue.
        //
        // The tasks need to be processed in the order they were added to the queue. Since pop_all_reversed is atomic, other threads
        // can't add new tasks while we're reading, which would mix up the order.
        VC_Task* theTask = inTasks->pop_all_reversed();
        
        while(theTask != NULL &&
              !theThreadShouldStop)  // Stop processing tasks if we're shutting down
        {
            VC_Task* theNextTask = theTask->mNext;
            
            VCAssert(!theTask->IsComplete(),
                      "VC_TaskQueue::WorkerThreadProc: Cannot process already completed task (ID %d)",
                      theTask->GetTaskID());
            
            VCAssert(theTask != theNextTask,
                      "VC_TaskQueue::WorkerThreadProc: VC_Task %p (ID %d) was added to %s multiple times. arg1=%llu arg2=%llu",
                      theTask,
                      theTask->GetTaskID(),
                      (inTasks == &mRealTimeThreadTasks ? "mRealTimeThreadTasks" : "mNonRealTimeThreadTasks"),
                      theTask->GetArg1(),
                      theTask->GetArg2());
            
            // Process the task
            theThreadShouldStop = inProcessTask(theTask);
            
            // If the task was queued synchronously, let the thread that queued it know we're finished
            if(theTask->IsSync())
            {
                // Marking the task as completed allows QueueSync to return, which means it's possible for theTask to point to
                // invalid memory after this point.
                CAMemoryBarrier();
                theTask->MarkCompleted();
                
                // Signal any threads waiting for their task to be processed.
                //
                // We use semaphore_signal_all instead of semaphore_signal to avoid a race condition in QueueSync. It's possible
                // for threads calling QueueSync to wait on the semaphore in an order different to the order of the tasks they just
                // added to the queue. So after each task is completed we have every waiting thread check if it was theirs.
                //
                // Note that semaphore_signal_all has an implicit barrier.
                theError = semaphore_signal_all(inSyncTaskCompletedSemaphore);
                VC_Utils::ThrowIfMachError("VC_TaskQueue::WorkerThreadProc", "semaphore_signal_all", theError);
            }
            else if(inFreeList != NULL)
            {
                // After completing an async task, move it to the free list so the memory can be reused
                inFreeList->push_atomic(theTask);
            }
            
            theTask = theNextTask;
        }
    }
}

bool    VC_TaskQueue::ProcessRealTimeThreadTask(VC_Task* inTask)
{
    AssertCurrentThreadIsRTWorkerThread("VC_TaskQueue::ProcessRealTimeThreadTask");
    
    switch(inTask->GetTaskID())
    {
        case kVCTaskStopWorkerThread:
            DebugMsg("VC_TaskQueue::ProcessRealTimeThreadTask: Stopping");
            // Return that the thread should stop itself
            return true;

        default:
            Assert(false, "VC_TaskQueue::ProcessRealTimeThreadTask: Unexpected task ID");
            break;
    }
    
    return false;
}

bool    VC_TaskQueue::ProcessNonRealTimeThreadTask(VC_Task* inTask)
{
#if DEBUG  // This Assert macro always checks the condition, if for some reason the compiler doesn't optimise it away, even in release builds
    Assert(mNonRealTimeThread.IsCurrentThread(), "ProcessNonRealTimeThreadTask should only be called on the non-realtime worker thread.");
    Assert(mNonRealTimeThread.IsTimeShareThread(), "mNonRealTimeThread should not be in a time-constraint priority band.");
#endif
    
    switch(inTask->GetTaskID())
    {
        case kVCTaskStopWorkerThread:
            DebugMsg("VC_TaskQueue::ProcessNonRealTimeThreadTask: Stopping");
            // Return that the thread should stop itself
            return true;
            
        case kVCTaskSendPropertyNotification:
            DebugMsg("VC_TaskQueue::ProcessNonRealTimeThreadTask: Processing kVCTaskSendPropertyNotification");
            {
                AudioObjectPropertyAddress thePropertyAddress[] = {
                    { static_cast<UInt32>(inTask->GetArg1()), kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster } };
                VC_PlugIn::Host_PropertiesChanged(static_cast<AudioObjectID>(inTask->GetArg2()), 1, thePropertyAddress);
            }
            break;
            
        default:
            Assert(false, "VC_TaskQueue::ProcessNonRealTimeThreadTask: Unexpected task ID");
            break;
    }
    
    return false;
}

#pragma clang assume_nonnull end

