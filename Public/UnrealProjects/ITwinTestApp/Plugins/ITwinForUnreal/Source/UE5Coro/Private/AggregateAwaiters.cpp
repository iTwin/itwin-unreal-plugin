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

#include "UE5Coro/AggregateAwaiter.h"

using namespace UE5Coro;
using namespace UE5Coro::Private;

int FAggregateAwaiter::GetResumerIndex() const
{
	checkf(Data->Count <= 0, TEXT("Internal error: resuming too early"));
	checkf(Data->Count == 0 || Data->Index != -1,
	       TEXT("Internal error: resuming with no result"));
	return Data->Index;
}

FAggregateAwaiter::FAggregateAwaiter(auto All,
                                     const TArray<TCoroutine<>>& Coroutines)
	: Data(std::make_shared<FData>(All.value ? Coroutines.Num()
	                                         : Coroutines.Num() ? 1 : 0))
{
	for (int i = 0; i < Coroutines.Num(); ++i)
		Consume(Data, i, Coroutines[i]);
}
template UE5CORO_API FAggregateAwaiter::FAggregateAwaiter(
	std::false_type, const TArray<TCoroutine<>>&);
template UE5CORO_API FAggregateAwaiter::FAggregateAwaiter(
	std::true_type, const TArray<TCoroutine<>>&);

bool FAggregateAwaiter::await_ready()
{
	checkf(Data, TEXT("Attempting to await moved-from aggregate awaiter"));
	Data->Lock.lock();
	checkf(!Data->Promise, TEXT("Attempting to reuse aggregate awaiter"));

	// Unlock if ready and resume immediately by returning true,
	// otherwise carry the lock to await_suspend/Suspend
	bool bReady = Data->Count <= 0;
	if (bReady)
		Data->Lock.unlock();
	return bReady;
}

void FAggregateAwaiter::Suspend(FPromise& Promise)
{
	checkf(!Data->Lock.try_lock(), TEXT("Internal error: lock was not taken"));
	checkf(!Data->Promise, TEXT("Attempting to reuse aggregate awaiter"));

	Data->Promise = &Promise;
	Data->Lock.unlock();
}

FAnyAwaiter UE5Coro::WhenAny(const TArray<TCoroutine<>>& Coroutines)
{
	return FAnyAwaiter(std::false_type(), Coroutines);
}

FRaceAwaiter UE5Coro::Race(TArray<TCoroutine<>> Array)
{
	return FRaceAwaiter(std::move(Array));
}

FAllAwaiter UE5Coro::WhenAll(const TArray<TCoroutine<>>& Coroutines)
{
	return FAllAwaiter(std::true_type(), Coroutines);
}

FRaceAwaiter::FRaceAwaiter(TArray<TCoroutine<>>&& Array)
	: Data(std::make_shared<FData>(std::move(Array)))
{
	// Add a continuation to every coroutine, but any one of them might
	// invalidate the array
	for (int i = 0; i < Data->Handles.Num(); ++i)
	{
		TCoroutine<>* Coro;
		{
			// Must be limited in scope because ContinueWith may be synchronous
			std::scoped_lock _(Data->Lock);
			if (Data->Index != -1) // Did a coroutine finish during this loop?
				return; // Don't bother asking the others, they've all canceled
			Coro = &Data->Handles[i];
		}

		Coro->ContinueWith([Data = Data, i]
		{
			std::unique_lock _(Data->Lock);

			// Nothing to do if this wasn't the first one
			if (Data->Index != -1)
				return;
			Data->Index = i;

			for (int j = 0; j < Data->Handles.Num(); ++j)
				if (j != i) // Cancel the others
					Data->Handles[j].Cancel();

			if (auto* Promise = Data->Promise)
			{
				_.unlock();
				Promise->Resume();
			}
		});
	}
}

bool FRaceAwaiter::await_ready()
{
	Data->Lock.lock();
	if (Data->Handles.Num() == 0 || Data->Index != -1)
	{
		Data->Lock.unlock();
		return true;
	}
	else
		return false; // Passing the lock to Suspend
}

void FRaceAwaiter::Suspend(FPromise& Promise)
{
	// Expecting a lock from await_ready
	checkf(!Data->Lock.try_lock(), TEXT("Internal error: lock not held"));
	checkf(!Data->Promise, TEXT("Unexpected double race await"));
	Data->Promise = &Promise;
	Data->Lock.unlock();
}

int FRaceAwaiter::await_resume() noexcept
{
	// This will be read on the same thread that wrote Index, or after
	// await_ready determined its value; no lock needed
	checkf(Data->Handles.Num() == 0 || Data->Index != -1,
	       TEXT("Internal error: resuming with unknown result"));
	return Data->Index;
}
