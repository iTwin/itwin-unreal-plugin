/*--------------------------------------------------------------------------------------+
|
|     $Source: AwaitableSemaphore.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

// Copyright © Laura Andelare
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted (subject to the limitations in the disclaimer
// below) provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY
// THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
// CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT
// NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
// PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "UE5Coro/Threading.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;

namespace
{
struct FAwaitingPromise
{
	FPromise* Promise;
	FAwaitingPromise* Next;
};
}

FAwaitableSemaphore::FAwaitableSemaphore(int Capacity, int InitialCount)
	: Capacity(Capacity), Count(InitialCount)
{
	checkf(Capacity > 0 && InitialCount >= 0 && InitialCount <= Capacity,
	       TEXT("Initial semaphore values out of range"));
}

#if UE5CORO_DEBUG
FAwaitableSemaphore::~FAwaitableSemaphore()
{
	ensureMsgf(!Awaiters,
	           TEXT("Destroyed early, remaining awaiters will never resume!"));
}
#endif

void FAwaitableSemaphore::Unlock(int InCount)
{
	checkf(InCount > 0, TEXT("Invalid count"));
	Lock.lock();
	verifyf((Count += InCount) <= Capacity,
	        TEXT("Semaphore unlocked above maximum"));
	TryResumeAll();
}

FSemaphoreAwaiter FAwaitableSemaphore::operator co_await()
{
	return FSemaphoreAwaiter(*this);
}

void FAwaitableSemaphore::TryResumeAll()
{
	checkf(!Lock.try_lock(), TEXT("Internal error: resuming without lock held"));
	while (Awaiters && Count > 0)
	{
		auto* Node = static_cast<FAwaitingPromise*>(std::exchange(
			Awaiters, static_cast<FAwaitingPromise*>(Awaiters)->Next));
		verifyf(--Count >= 0, TEXT("Internal error: semaphore went negative"));
		Lock.unlock();
		Node->Promise->Resume();
		delete Node;
		Lock.lock();
	}
	Lock.unlock();
}

bool FSemaphoreAwaiter::await_ready()
{
	Semaphore.Lock.lock();
	if (Semaphore.Count > 0)
	{
		verifyf(--Semaphore.Count >= 0,
		        TEXT("Internal error: semaphore went negative"));
		Semaphore.Lock.unlock();
		return true;
	}
	else // Leave it locked
		return false;
}

void FSemaphoreAwaiter::Suspend(FPromise& Promise)
{
	checkf(!Semaphore.Lock.try_lock(),
	       TEXT("Internal error: suspension without lock"));
	Semaphore.Awaiters = new FAwaitingPromise{
		&Promise, static_cast<FAwaitingPromise*>(Semaphore.Awaiters)};
	Semaphore.Lock.unlock();
}
