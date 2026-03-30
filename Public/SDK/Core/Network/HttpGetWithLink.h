/*--------------------------------------------------------------------------------------+
|
|     $Source: HttpGetWithLink.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once
#include "Network.h"
#include <fmt/format.h>

namespace AdvViz::SDK
{
	using namespace Tools;

	template<typename T, typename VecTFct>
	inline AdvViz::expected<void, std::string> HttpGetWithLink_ByBatch(const std::shared_ptr<Http>& http,
		const std::string& url, const Http::Headers& headers,
		const VecTFct& fct)
	{
		struct SJsonLink
		{
			std::optional<std::string> prev;
			std::optional<std::string> self;
			std::optional<std::string> next;
		};

		struct SJsonOut
		{
			int total_rows = 0;
			std::optional<std::vector<T>> rows;
			SJsonLink _links;
		};
		SJsonOut jOut;

		long status = http->GetJson(jOut, url, headers);
		bool continueLoading = true;
		if (jOut.total_rows > 0 && !jOut.rows.has_value() && (status == 200 || status == 201))
		{
			BE_ISSUE("unexpected Json parsed value");
		}
		while (continueLoading)
		{
			if (status != 200 && status != 201)
			{
				continueLoading = false;
				return AdvViz::make_unexpected(fmt::format("{} failed with status: {}", url, status));
			}
			if (jOut.rows)
			{
				auto ret = fct(*jOut.rows);
					if (!ret)
						return ret;

				jOut.rows->clear();
			}
			if (jOut._links.next.has_value() && !jOut._links.next.value().empty())
			{
				
				// Quick workaround for a bug in Decoration Service sometimes providing bad URLs with http
				// instead of https protocol! (issue found for instances, which was causing bug #1609088).
				if (!jOut._links.next.value().starts_with("http://localhost") &&
					!jOut._links.next.value().starts_with("http://127.0.0.1")
					) // don't change http when using localhost
				{
					std::string const nextUrl = rfl::internal::strings::replace_all(
						jOut._links.next.value(),
						"http://", "https://");
					status = http->GetJson(jOut, nextUrl, headers, true);
				}
				else
				{
					status = http->GetJson(jOut, jOut._links.next.value(), headers, true);
				}
			}
			else
			{
				continueLoading = false;
			}
		}
		return {};
	}

	template<typename T, typename VecTFct, typename OnFinishTFct>
	inline void AsyncHttpGetWithLink_ByBatch(const std::shared_ptr<Http>& http,
		const std::string& url, const Http::Headers& headers,
		const VecTFct& fct,
		const OnFinishTFct& onFinish)
	{
		struct SJsonLink
		{
			std::optional<std::string> prev;
			std::optional<std::string> self;
			std::optional<std::string> next;
		};

		struct SJsonOut
		{
			int total_rows = 0;
			std::optional<std::vector<T>> rows;
			SJsonLink _links;
		};

		auto finishMonitorPtr = std::make_shared<Tools::TaskFinishMonitor<expected<void, std::string>>>(onFinish);

		// Use shared_ptr to std::function to allow recursive lambda capture
		using CallbackType = std::function<void(
			const Http::Response&, const expected<TSharedLockableDataPtr<SJsonOut>, std::string>&)>;
		auto resultCallBackPtr = std::make_shared<CallbackType>();

		*resultCallBackPtr = [fct, onFinish, http, headers, resultCallBackPtr, finishMonitorPtr](
			const Http::Response& r, const expected<TSharedLockableDataPtr<SJsonOut>, std::string>& jOutInner)
		{
			if (!Http::IsSuccessful(r))
			{
				onFinish(make_unexpected(fmt::format("AsyncHttpGetWithLink_ByBatch failed with status: {}", r.first)));
				return;
			}
			if (!jOutInner)
			{
				onFinish(make_unexpected(fmt::format("AsyncHttpGetWithLink_ByBatch: json parse error: {}", jOutInner.error())));
				return;
			}

			auto jOut((*jOutInner)->GetAutoLock());

			if (jOut->total_rows > 0 && !jOut->rows.has_value())
			{
				BE_ISSUE("unexpected Json parsed value");
				onFinish(make_unexpected("AsyncHttpGetWithLink_ByBatch: unexpected Json parsed value"));
				return;
			}

			bool hasNext = false;
			if (jOut->_links.next.has_value() && !jOut->_links.next.value().empty())
			{
				hasNext = true;
				finishMonitorPtr->AddTask();
				// Quick workaround for a bug in Decoration Service sometimes providing bad URLs with http
				// instead of https protocol! (issue found for instances, which was causing bug #1609088).
				if (!jOut->_links.next.value().starts_with("http://localhost") &&
					!jOut->_links.next.value().starts_with("http://127.0.0.1")
					) // don't change http when using localhost
				{
					TSharedLockableDataPtr<SJsonOut> jOut2 = MakeSharedLockableDataPtr<SJsonOut>(new SJsonOut());
					std::string const nextUrl = rfl::internal::strings::replace_all(
						jOut->_links.next.value(),
						"http://", "https://");
					http->AsyncGetJson(jOut2, *resultCallBackPtr, nextUrl, headers, true);
				}
				else
				{
					TSharedLockableDataPtr<SJsonOut> jOut2 = MakeSharedLockableDataPtr<SJsonOut>(new SJsonOut());
					http->AsyncGetJson(jOut2, *resultCallBackPtr, jOut->_links.next.value(), headers, true);
				}
			}

			if (jOut->rows)
			{
				auto ret = fct(*jOut->rows);
				if (!ret)
				{
					onFinish(ret);
				}
			}

			finishMonitorPtr->TaskFinished({});
		};

		finishMonitorPtr->AddTask();
		TSharedLockableDataPtr<SJsonOut> jOut = MakeSharedLockableDataPtr<SJsonOut>(new SJsonOut());
		http->AsyncGetJson(jOut, *resultCallBackPtr, url, headers);
	}

	template<typename T, typename TFct>
	inline AdvViz::expected<void, std::string> HttpGetWithLink(const std::shared_ptr<Http>& http,
		const std::string& url, const Http::Headers& headers,
		const TFct& fct)
	{
		return HttpGetWithLink_ByBatch<T>(http, url, headers,
			[&fct](std::vector<T>& rows) -> expected<void, std::string>
		{
			for (auto& row : rows)
			{
				auto ret = fct(row);
				if (!ret)
					return ret;
			}
			return {};
		});
	}

	template<typename T, typename TFct, typename TOnFinishTFct>
	inline void AsyncHttpGetWithLink(const std::shared_ptr<Http>& http,
		const std::string& url, const Http::Headers& headers,
		const TFct& fct,
		const TOnFinishTFct& onFinish)
	{
		AsyncHttpGetWithLink_ByBatch<T>(http, url, headers,
			[fct](std::vector<T>& rows) -> expected<void, std::string>
			{
				for (auto& row : rows)
				{
					auto ret = fct(row);
					if (!ret)
						return ret;
				}
				return {};
			},
			onFinish);
	}


	/// Variant of the same function which should be used for requests made to other iTwin services (the only
	/// difference with the Decoration Service being that data is not written with a common pattern
	// "total_rows" + "rows", but instead, with name depending on the data type ("scenes" for SceneAPI...)
	namespace ITwinAPI
	{
		struct SUrl
		{
			std::string href;
		};
		struct SLinks
		{
			std::optional<SUrl> prev;
			std::optional<SUrl> self;
			std::optional<SUrl> next;
		};

		template <rfl::internal::StringLiteral TDataVecName, typename T, typename VecTFct>
		inline AdvViz::expected<void, HttpError> GetPagedDataVector(
			const std::shared_ptr<Http>& http,
			const std::string& url,
			const Http::Headers& headers,
			const VecTFct& fct)
		{
			struct SJsonOut
			{
				rfl::Rename<TDataVecName, std::optional<std::vector<T>> > dataVec_;
				SLinks _links;
			};
			SJsonOut jOut;

			long status = http->GetJson(jOut, url, headers);
			bool continueLoading = true;
			if (!jOut.dataVec_.value().has_value() && (status == 200 || status == 201))
			{
				BE_ISSUE("unexpected Json parsed value");
			}
			while (continueLoading)
			{
				if (status != 200 && status != 201)
				{
					continueLoading = false;
					return AdvViz::make_unexpected(HttpError{
						.message = fmt::format("{} failed with status: {}", url, status),
						.httpCode = status
						});
				}
				if (jOut.dataVec_.value().has_value())
				{
					auto ret = fct(jOut.dataVec_.value().value());
					if (!ret)
						return ret;

					jOut.dataVec_.value()->clear();
				}
				if (jOut._links.next && !jOut._links.next->href.empty())
				{
					status = http->GetJson(jOut, jOut._links.next->href, headers, true /*isFullURL*/);
				}
				else
				{
					continueLoading = false;
				}
			}
			return {};
		}

		template <rfl::internal::StringLiteral TDataVecName, typename T, typename TFct>
		inline AdvViz::expected<void, HttpError> GetPagedData(const std::shared_ptr<Http>& http,
			const std::string& url, const Http::Headers& headers,
			const TFct& fct)
		{
			return GetPagedDataVector<TDataVecName, T>(http, url, headers,
				[&fct](std::vector<T> const& rows) -> expected<void, HttpError>
			{
				for (auto const& row : rows)
				{
					auto ret = fct(row);
					if (!ret)
						return ret;
				}
				return {};
			});
		}



		template <rfl::internal::StringLiteral TDataVecName, typename T, typename VecTFct, typename OnFinishTFct>
		inline void AsyncGetPagedDataVector(
			const std::shared_ptr<Http>& http,
			const std::string& url,
			const Http::Headers& headers,
			const VecTFct& fct,
			const OnFinishTFct& onFinish,
			Http::EAsyncCallbackExecutionMode asyncCBExecMode = Http::EAsyncCallbackExecutionMode::Default)
		{
			struct SJsonOut
			{
				rfl::Rename<TDataVecName, std::optional<std::vector<T>> > dataVec_;
				SLinks _links;
			};

			auto finishMonitorPtr = std::make_shared<Tools::TaskFinishMonitor<expected<void, HttpError>>>(onFinish);

			// Use shared_ptr to std::function to allow recursive lambda capture
			using CallbackType = std::function<void(
				const Http::Response&, const expected<TSharedLockableDataPtr<SJsonOut>, std::string>&)>;
			auto resultCallBackPtr = std::make_shared<CallbackType>();

			*resultCallBackPtr = [fct, onFinish, http, url, headers, asyncCBExecMode, resultCallBackPtr, finishMonitorPtr](
				const Http::Response& r, const expected<TSharedLockableDataPtr<SJsonOut>, std::string>& jOutInner)
			{
				if (!Http::IsSuccessful(r))
				{
					onFinish(make_unexpected(HttpError{
						.message = fmt::format("AsyncGetPagedDataVector for {} failed with status: {}", url, r.first),
						.httpCode = r.first
						})
					);
					return;
				}
				if (!jOutInner)
				{
					onFinish(make_unexpected(HttpError{
						.message = fmt::format("AsyncGetPagedDataVector for {}: json parse error:{}", url, jOutInner.error()),
						.httpCode = -1
						})
					);
					return;
				}

				auto jOut((*jOutInner)->GetAutoLock());

				bool hasNext = false;
				if (jOut->_links.next.has_value() && !jOut->_links.next->href.empty())
				{
					hasNext = true;
					finishMonitorPtr->AddTask();
					TSharedLockableDataPtr<SJsonOut> jOut2 = MakeSharedLockableDataPtr<SJsonOut>(new SJsonOut());
					http->AsyncGetJson(jOut2, *resultCallBackPtr, jOut->_links.next->href, headers, true /*isFullURL*/, asyncCBExecMode);
				}

				if (jOut->dataVec_.value().has_value())
				{
					auto ret = fct(jOut->dataVec_.value().value());
					if (!ret)
					{
						onFinish(ret);
					}
				}

				finishMonitorPtr->TaskFinished({});
			};

			finishMonitorPtr->AddTask();
			TSharedLockableDataPtr<SJsonOut> jOut = MakeSharedLockableDataPtr<SJsonOut>(new SJsonOut());
			http->AsyncGetJson(jOut, *resultCallBackPtr, url, headers, false /*isFullURL*/, asyncCBExecMode);
		}

		template <rfl::internal::StringLiteral TDataVecName, typename T, typename TFct, typename TOnFinishTFct>
		inline void AsyncGetPagedData(const std::shared_ptr<Http>& http,
			const std::string& url, const Http::Headers& headers,
			const TFct& fct,
			const TOnFinishTFct& onFinish,
			Http::EAsyncCallbackExecutionMode asyncCBExecMode = Http::EAsyncCallbackExecutionMode::Default)
		{
			AsyncGetPagedDataVector<TDataVecName, T>(http, url, headers,
				[fct](std::vector<T> const& rows) -> expected<void, HttpError>
			{
				for (auto& row : rows)
				{
					auto ret = fct(row);
					if (!ret)
						return ret;
				}
				return {};
			},
				onFinish, asyncCBExecMode);
		}
	}
}
