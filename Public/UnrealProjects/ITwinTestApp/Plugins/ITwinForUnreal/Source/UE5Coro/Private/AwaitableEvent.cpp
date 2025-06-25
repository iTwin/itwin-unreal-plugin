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

FAwaitableEvent::FAwaitableEvent(EEventMode Mode, bool bInitialState)
	: Mode(Mode), bActive(bInitialState)
{
	checkf(Mode == EEventMode::AutoReset || Mode == EEventMode::ManualReset,
	       TEXT("Invalid event mode"));
}

#if UE5CORO_DEBUG
FAwaitableEvent::~FAwaitableEvent()
{
	ensureMsgf(!Awaiters,
	           TEXT("Destroyed early, remaining awaiters will never resume!"));
}
#endif

void FAwaitableEvent::Trigger()
{
	Lock.lock();
	if (Mode == EEventMode::ManualReset)
	{
		bActive = true;
		TryResumeAll();
	}
	else if (Awaiters)
		ResumeOne(); // AutoReset: don't set bActive
	else
	{
		bActive = true;
		Lock.unlock();
	}
}

void FAwaitableEvent::Reset()
{
	std::scoped_lock _(Lock);
	bActive = false;
}

bool FAwaitableEvent::IsManualReset() const noexcept
{
	return Mode == EEventMode::ManualReset;
}

FEventAwaiter FAwaitableEvent::operator co_await()
{
	return FEventAwaiter(*this);
}

void FAwaitableEvent::ResumeOne()
{
	checkf(!Lock.try_lock(), TEXT("Internal error: resuming without lock"));
	checkf(Awaiters, TEXT("Internal error: attempting to resume nothing"));
	auto* Node = static_cast<FAwaitingPromise*>(std::exchange(Awaiters,
		static_cast<FAwaitingPromise*>(Awaiters)->Next));
	Lock.unlock(); // The coroutine might want the lock

	auto* Promise = Node->Promise;
	delete Node; // Do this first to help tail calls
	Promise->Resume();
}

void FAwaitableEvent::TryResumeAll()
{
	checkf(!Lock.try_lock(), TEXT("Internal error: resuming without lock"));

	// Start a new awaiter list to make sure everything active at this point
	// gets resumed eventually, even if the event is reset
	auto* Node = static_cast<FAwaitingPromise*>(std::exchange(Awaiters, nullptr));
	Lock.unlock();

	while (Node)
	{
		Node->Promise->Resume();
		delete std::exchange(Node, Node->Next);
	}
}

bool FEventAwaiter::await_ready() noexcept
{
	Event.Lock.lock();
	bool bValue = Event.bActive;
	if (Event.Mode == EEventMode::AutoReset)
		Event.bActive = false;
	if (bValue)
	{
		Event.Lock.unlock();
		return true;
	}
	else // Leave it locked
		return false;
}

void FEventAwaiter::Suspend(FPromise& Promise)
{
	checkf(!Event.Lock.try_lock(),
	       TEXT("Internal error: suspension without lock"));
	checkf(!Event.bActive, TEXT("Internal error: suspending with active event"));
	Event.Awaiters = new FAwaitingPromise{
		&Promise, static_cast<FAwaitingPromise*>(Event.Awaiters)};
	Event.Lock.unlock();
}
