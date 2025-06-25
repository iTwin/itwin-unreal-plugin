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

#include "UE5Coro/LatentCallback.h"
#include "LatentExitReason.h"

using namespace UE5Coro::Latent;
using namespace UE5Coro::Private;

ELatentExitReason UE5Coro::Private::GLatentExitReason = ELatentExitReason::Normal;

namespace
{
template<ELatentExitReason Reason>
void CleanupIf(std::function<void()>& Fn) // lvalue ref to work around TScopeGuard
{
	// This race condition is explicitly allowed due to the IsInGameThread check
	if (static_cast<uint8>(GLatentExitReason) & static_cast<uint8>(Reason) &&
	    IsInGameThread())
		Fn();
}
}

FOnAbnormalExit::FOnAbnormalExit(std::function<void()> Fn)
	: TScopeGuard(std::bind_front(&CleanupIf<ELatentExitReason::AnyAbnormal>,
	                              std::move(Fn)))
{
}

FOnActionAborted::FOnActionAborted(std::function<void()> Fn)
	: TScopeGuard(std::bind_front(&CleanupIf<ELatentExitReason::ActionAborted>,
	                              std::move(Fn)))
{
}

FOnObjectDestroyed::FOnObjectDestroyed(std::function<void()> Fn)
	: TScopeGuard(std::bind_front(&CleanupIf<ELatentExitReason::ObjectDestroyed>,
	                              std::move(Fn)))
{
}
