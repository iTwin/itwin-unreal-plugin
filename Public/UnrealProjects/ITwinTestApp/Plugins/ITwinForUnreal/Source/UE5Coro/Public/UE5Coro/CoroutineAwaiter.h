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

#pragma once

#include "CoreMinimal.h"
#include "UE5Coro/Definition.h"
#include "Async/TaskGraphInterfaces.h"
#include "UE5Coro/AsyncAwaiter.h"
#include "UE5Coro/Coroutine.h"
#include "UE5Coro/LatentAwaiter.h"

#pragma region Private
namespace UE5Coro::Private
{
template<typename T, bool bMove>
class TAsyncCoroutineAwaiter final
	: public TAwaiter<TAsyncCoroutineAwaiter<T, bMove>>
{
	TCoroutine<T> Antecedent;

public:
	explicit TAsyncCoroutineAwaiter(TCoroutine<T> Antecedent)
		: Antecedent(std::move(Antecedent)) { }

	[[nodiscard]] bool await_ready() { return Antecedent.IsDone(); }

	void Suspend(FPromise& Promise)
	{
		Antecedent.ContinueWith([&Promise] { Promise.Resume(); });
	}

	T await_resume()
	{
		checkf(Antecedent.IsDone(), TEXT("Internal error: resuming too early"));
		if constexpr (!std::is_void_v<T>)
		{
			if constexpr (bMove) // This cannot be a ternary due to RVO rules
				return Antecedent.MoveResult();
			else
				return Antecedent.GetResult();
		}
	}
};

template<typename T>
bool ShouldResumeLatentCoroutine(void* State, bool bCleanup)
{
	auto* This = static_cast<TCoroutine<T>*>(State);
	if (bCleanup) [[unlikely]]
	{
		delete This;
		return false;
	}
	return This->IsDone();
}

template<typename T, bool bMove>
struct TLatentCoroutineAwaiter final : FLatentAwaiter
{
	explicit TLatentCoroutineAwaiter(TCoroutine<T> Antecedent)
		: FLatentAwaiter(new TCoroutine<T>(std::move(Antecedent)),
		                 &ShouldResumeLatentCoroutine<T>) { }

	T await_resume()
	{
		auto* Coro = static_cast<TCoroutine<T>*>(State);
		checkf(Coro->IsDone(), TEXT("Internal error: resuming too early"));
		if constexpr (!std::is_void_v<T>)
		{
			if constexpr (bMove) // This cannot be a ternary due to RVO rules
				return Coro->MoveResult();
			else
				return Coro->GetResult();
		}
	}
};

template<typename T>
auto TAwaitTransform<FAsyncPromise, TCoroutine<T>>::operator()(
	const TCoroutine<T>& Coro) -> TAsyncCoroutineAwaiter<T, false>
{
	return TAsyncCoroutineAwaiter<T, false>(Coro);
}

template<typename T>
auto TAwaitTransform<FAsyncPromise, TCoroutine<T>>::operator()(
	TCoroutine<T>&& Coro) -> TAsyncCoroutineAwaiter<T, true>
{
	return TAsyncCoroutineAwaiter<T, true>(std::move(Coro));
}

template<typename T>
auto TAwaitTransform<FLatentPromise, TCoroutine<T>>::operator()(
	const TCoroutine<T>& Coro) -> TLatentCoroutineAwaiter<T, false>
{
	return TLatentCoroutineAwaiter<T, false>(Coro);
}

template<typename T>
auto TAwaitTransform<FLatentPromise, TCoroutine<T>>::operator()(
	TCoroutine<T>&& Coro) -> TLatentCoroutineAwaiter<T, true>
{
	return TLatentCoroutineAwaiter<T, true>(std::move(Coro));
}
}
#pragma endregion
