/*--------------------------------------------------------------------------------------+
|
|     $Source: GenericHelpers.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Engine/World.h>
#include <Misc/AutomationTest.h>
#include <TimerManager.h>

// Copied from CesiumRuntime/Private/Tests/CesiumTestHelpers.h
template <typename T>
void WaitForImpl(
    const FDoneDelegate& done,
    UWorld* pWorld,
    T&& condition,
    FTimerHandle& timerHandle) {
  if (condition()) {
    pWorld->GetTimerManager().ClearTimer(timerHandle);
    done.Execute();
  } else if (pWorld->GetTimerManager().GetTimerRemaining(timerHandle) <= 0.0f) {
    // Timeout
    pWorld->GetTimerManager().ClearTimer(timerHandle);
    done.Execute();
  } else {
    pWorld->GetTimerManager().SetTimerForNextTick(
        [done, pWorld, condition, timerHandle]() mutable {
          WaitForImpl<T>(done, pWorld, std::move(condition), timerHandle);
        });
  }
}

// Copied from CesiumRuntime/Private/Tests/CesiumTestHelpers.h
/// <summary>
/// Waits for a provided lambda function to become true, ticking through render
/// frames in the meantime. If the timeout elapses before the condition becomes
/// true, an error is logged (which will cause a test failure) and the done
/// delegate is invoked anyway.
/// </summary>
/// <typeparam name="T"></typeparam>
/// <param name="done">The done delegate provided by a LatentIt or
/// LatentBeforeEach. It will be invoked when the condition is true or when the
/// timeout elapses.</param>
/// <param name="pWorld">The world in which to check the condition.</param>
/// <param name="timeoutSeconds">The maximum time to wait for the condition to
/// become true.</param>
/// <param name="condition">A lambda that is invoked each
/// frame. If this function returns false, waiting continues.</param>
template <typename T>
void WaitFor(
    const FDoneDelegate& done,
    UWorld* pWorld,
    float timeoutSeconds,
    T&& condition) {
  FTimerHandle timerHandle;
  pWorld->GetTimerManager().SetTimer(timerHandle, timeoutSeconds, false);
  WaitForImpl<T>(done, pWorld, std::forward<T>(condition), timerHandle);
}
