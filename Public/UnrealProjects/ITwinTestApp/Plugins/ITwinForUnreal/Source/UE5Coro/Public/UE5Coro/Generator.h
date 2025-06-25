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
#include <coroutine>
#include "UE5Coro/Private.h"

namespace UE5Coro
{
template<typename> class TGeneratorIterator;

/** Generator coroutine handle.
 *  Make a function return TGenerator<T> instead of T, and it will be able to
 *  co_yield multiple values throughout its execution.
 *  Callers can either manually fetch values or use the provided iterator
 *  wrappers to treat the returned values as a virtual container.
 *  This object represents ownership of the coroutine, its destruction will
 *  cancel the coroutine. */
template<typename T>
struct [[nodiscard]] TGenerator
{
	using promise_type = Private::TGeneratorPromise<T>;
	using iterator = TGeneratorIterator<T>;
	friend promise_type;

private:
	std::coroutine_handle<promise_type> Handle;

	explicit TGenerator(std::coroutine_handle<promise_type> Handle) noexcept
		: Handle(Handle) { }

public:
	TGenerator(const TGenerator&) = delete;
	TGenerator(TGenerator&& Other) noexcept
		: Handle(std::exchange(Other.Handle, nullptr)) { }
	~TGenerator() { if (Handle) Handle.destroy(); }

	/** Replaces the underlying coroutine of this object with another.
	 *  The coroutine that this object used to own is canceled. */
	TGenerator& operator=(TGenerator&& Other) noexcept
	{
		if (Handle)
			Handle.destroy();
		Handle = std::exchange(Other.Handle, nullptr);
		return *this;
	}

	/** Returns true if Current() is valid. */
	explicit operator bool() const noexcept { return Handle && !Handle.done(); }

	/**	Resumes the generator. Returns true if Current() is valid. */
	bool Resume()
	{
		if (operator bool()) [[likely]]
			Handle.resume();
		return operator bool();
	}

	/** Retrieves the value that was last co_yielded. */
	T& Current() const
	{
		checkf(Handle && Handle.promise().Current,
		       TEXT("Attempting to read from invalid generator"));
		return *static_cast<T*>(Handle.promise().Current);
	}

	iterator CreateIterator() noexcept { return iterator(*this); }
	iterator begin() noexcept { return iterator(*this); }
	iterator end() const noexcept { return iterator(nullptr); }
};

/** Provides an iterator-like interface over TGenerator: operator++ advances
 *  the generator, operator* and operator-> read the current value, etc. */
template<typename T>
class TGeneratorIterator
{
	TGenerator<T>* Generator; // nullptr == end()

public:
	/** Constructs an iterator wrapper over a generator coroutine. */
	explicit TGeneratorIterator(TGenerator<T>& Generator) noexcept
		: Generator(Generator ? &Generator : nullptr) { }

	/** The end() iterator for every generator coroutine. */
	explicit TGeneratorIterator(std::nullptr_t) noexcept
		: Generator(nullptr) { }

	/** Returns true if the iterator is not equal to end().
	 *  Provided for compatibility with code expecting UE-style iterators. */
	explicit operator bool() const noexcept { return Generator != nullptr; }

	/** Compares this iterator with another. Provided for STL compatibility. */
	bool operator==(const TGeneratorIterator& Other) const noexcept
	{
		return Generator == Other.Generator;
	}

	/** Advances the generator. */
	TGeneratorIterator& operator++()
	{
		checkf(Generator, TEXT("Attempted to move iterator past end()"));
		if (!Generator->Resume()) [[unlikely]] // Did the coroutine finish?
			Generator = nullptr; // Become end() if it did
		return *this;
	}

	/** Advances the generator. Returns void to break code that expects a
	 *  meaningful value before incrementing, because the generator's previous
	 *  state cannot be copied and is always lost. */
	void operator++(int) { operator++(); }

	/** Returns the generator's Current() value. */
	T& operator*() const
	{
		checkf(Generator, TEXT("Attempted to dereference invalid iterator"));
		return Generator->Current();
	}

	/** Returns a pointer to the generator's Current() value. */
	T* operator->() const { return std::addressof(operator*()); }
};
}

#pragma region Private
namespace UE5Coro::Private
{
class [[nodiscard]] UE5CORO_API FGeneratorPromise
{
protected:
	/** Points to the current co_yield value, if valid. */
	void* Current = nullptr;

public:
	FGeneratorPromise() = default;
	UE_NONCOPYABLE(FGeneratorPromise);
	~FGeneratorPromise() = default;

	std::suspend_never initial_suspend() noexcept { return {}; }
	std::suspend_always final_suspend() noexcept { return {}; }
	void return_void() noexcept { Current = nullptr; }
	void unhandled_exception();

	// co_await is not allowed in generators
	std::suspend_never await_transform(auto&&) = delete;
};

template<typename T>
class [[nodiscard]] TGeneratorPromise : public FGeneratorPromise
{
	friend TGenerator<T>;
	using handle_type = std::coroutine_handle<TGeneratorPromise>;

public:
	TGenerator<T> get_return_object() noexcept
	{
		return TGenerator<T>(handle_type::from_promise(*this));
	}

	std::suspend_always yield_value(std::remove_reference_t<T>& Value) noexcept
	{
		Current = std::addressof(Value);
		return {};
	}

	std::suspend_always yield_value(std::remove_reference_t<T>&& Value) noexcept
	{
		Current = std::addressof(Value);
		return {};
	}
};
}
#pragma endregion
