#include "CaptureFrameQueue.h"

#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"

FCaptureFrameRingBuffer::FCaptureFrameRingBuffer()
    : Head(0)
    , Tail(0)
    , CurrentSize(0)
    , MaxCapacity(0)
    , DroppedFrames(0)
    , BlockedEnqueues(0)
    , OverflowPolicy(ERingBufferOverflowPolicy::DropOldest)
    , NotEmptyEvent(nullptr)
    , NotFullEvent(nullptr)
{
}

FCaptureFrameRingBuffer::~FCaptureFrameRingBuffer()
{
    Clear();
}

void FCaptureFrameRingBuffer::Initialize(int32 InCapacity, ERingBufferOverflowPolicy InPolicy)
{
    FScopeLock Lock(&CriticalSection);
    MaxCapacity = FMath::Max(1, InCapacity);
    Frames.SetNum(MaxCapacity);
    Head = 0;
    Tail = 0;
    CurrentSize = 0;
    DroppedFrames = 0;
    BlockedEnqueues = 0;
    OverflowPolicy = InPolicy;

    if (!NotEmptyEvent)
    {
        NotEmptyEvent = FPlatformProcess::GetSynchEventFromPool(false);
    }
    if (!NotFullEvent)
    {
        NotFullEvent = FPlatformProcess::GetSynchEventFromPool(false);
    }

    if (NotFullEvent)
    {
        NotFullEvent->Trigger();
    }
}

void FCaptureFrameRingBuffer::Clear()
{
    FScopeLock Lock(&CriticalSection);
    Frames.Empty();
    Head = 0;
    Tail = 0;
    CurrentSize = 0;
    DroppedFrames = 0;
    BlockedEnqueues = 0;
    MaxCapacity = 0;
    OverflowPolicy = ERingBufferOverflowPolicy::DropOldest;

    if (NotEmptyEvent)
    {
        FPlatformProcess::ReturnSynchEventToPool(NotEmptyEvent);
        NotEmptyEvent = nullptr;
    }
    if (NotFullEvent)
    {
        FPlatformProcess::ReturnSynchEventToPool(NotFullEvent);
        NotFullEvent = nullptr;
    }
}

bool FCaptureFrameRingBuffer::Enqueue(FPanoramaCaptureFrame&& Frame)
{
    while (true)
    {
        CriticalSection.Lock();

        if (MaxCapacity <= 0)
        {
            ++DroppedFrames;
            CriticalSection.Unlock();
            return false;
        }

        if (Frames.Num() != MaxCapacity)
        {
            Frames.SetNum(MaxCapacity);
        }

        if (CurrentSize < MaxCapacity)
        {
            Frames[Tail] = MoveTemp(Frame);
            Tail = (Tail + 1) % MaxCapacity;
            ++CurrentSize;
            CriticalSection.Unlock();

            if (NotEmptyEvent)
            {
                NotEmptyEvent->Trigger();
            }

            return true;
        }

        // Buffer full
        if (OverflowPolicy == ERingBufferOverflowPolicy::DropOldest)
        {
            Head = (Head + 1) % MaxCapacity;
            --CurrentSize;
            ++DroppedFrames;
            Frames[Tail] = MoveTemp(Frame);
            Tail = (Tail + 1) % MaxCapacity;
            ++CurrentSize;
            CriticalSection.Unlock();

            if (NotEmptyEvent)
            {
                NotEmptyEvent->Trigger();
            }

            return true;
        }

        if (OverflowPolicy == ERingBufferOverflowPolicy::DropNewest)
        {
            ++DroppedFrames;
            CriticalSection.Unlock();
            return false;
        }

        // Block until consumer frees space
        ++BlockedEnqueues;
        if (NotFullEvent)
        {
            NotFullEvent->Reset();
        }
        CriticalSection.Unlock();

        if (NotFullEvent)
        {
            NotFullEvent->Wait();
        }
        else
        {
            FPlatformProcess::Sleep(0.001f);
        }
    }
}

bool FCaptureFrameRingBuffer::Dequeue(FPanoramaCaptureFrame& OutFrame)
{
    CriticalSection.Lock();

    if (CurrentSize == 0)
    {
        CriticalSection.Unlock();
        return false;
    }

    OutFrame = MoveTemp(Frames[Head]);
    Head = (Head + 1) % MaxCapacity;
    --CurrentSize;

    if (NotFullEvent)
    {
        NotFullEvent->Trigger();
    }

    CriticalSection.Unlock();
    return true;
}

int32 FCaptureFrameRingBuffer::Num() const
{
    FScopeLock Lock(&CriticalSection);
    return CurrentSize;
}

int32 FCaptureFrameRingBuffer::Capacity() const
{
    FScopeLock Lock(&CriticalSection);
    return MaxCapacity;
}

int32 FCaptureFrameRingBuffer::GetDroppedFrames() const
{
    FScopeLock Lock(&CriticalSection);
    return DroppedFrames;
}

int32 FCaptureFrameRingBuffer::GetBlockedFrames() const
{
    FScopeLock Lock(&CriticalSection);
    return BlockedEnqueues;
}
