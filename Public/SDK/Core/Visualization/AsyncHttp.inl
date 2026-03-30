/*--------------------------------------------------------------------------------------+
|
|     $Source: AsyncHttp.inl $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Core/Network/http.h>
#include <Core/Visualization/AsyncHelpers.h>

namespace AdvViz::SDK
{
	/// Utility templates for asynchronous requests, to deal with inter-dependency cases (typically when we
	/// need to create something on the server before starting populating it though other requests - the
	/// scene or decoration, for example).

	using HttpPtr = std::shared_ptr<Http>;
	using RequestGroupCallbackPtr = std::shared_ptr<AsyncRequestGroupCallback>;


	template <typename TypeOutput, typename TFunctor>
	inline void AsyncPostJson(HttpPtr const& http,
		RequestGroupCallbackPtr const& callbackPtr,
		TFunctor&& inFct,
		std::string const& url,
		Http::BodyParams const& bodyParams,
		Http::Headers const& h = {})
	{
		auto dataOut = Tools::MakeSharedLockableData<TypeOutput>();

		callbackPtr->AddRequestToWait(); // register request to group
		http->AsyncPostJson<TypeOutput>(dataOut,
			[callbackPtr,
			fct = std::move(inFct)](long httpCode, Tools::TSharedLockableData<TypeOutput> const& joutPtr)
		{
			if (!callbackPtr->IsValid())
				return;
			const bool bSuccess = fct(httpCode, joutPtr);
			callbackPtr->OnRequestDone(bSuccess); // notify termination to request group
		},
			url, bodyParams, h, Http::EAsyncCallbackExecutionMode::MainThread);
	}

	template <typename TypeOutput, typename TFunctor, typename TypeBody>
	inline void AsyncPostJsonJBody(HttpPtr const& http,
								   RequestGroupCallbackPtr const& callbackPtr,
								   TFunctor&& inFct,
								   std::string const& url, 
								   TypeBody const& body,
								   Http::Headers const& h = {})
	{
		AsyncPostJson<TypeOutput>(http, callbackPtr, std::move(inFct),
			url, Json::ToString(body), h);
	}

	template <typename TFunctor>
	inline void AsyncPostFile(HttpPtr const& http,
							  RequestGroupCallbackPtr const& callbackPtr,
							  TFunctor&& inFct,
							  std::string const& url,
							  std::string const& fileParamName,
							  std::string const& filePath,
							  Http::KeyValueVector const& extraParams = {},
							  Http::Headers const& h = {})
	{
		callbackPtr->AddRequestToWait();
		http->AsyncPostFile(
			[callbackPtr, fct = std::move(inFct)](const Http::Response& r)
		{
			if (!callbackPtr->IsValid())
				return;
			const bool bSuccess = fct(r);
			callbackPtr->OnRequestDone(bSuccess); // notify termination to request group
		}, url
		 , fileParamName
		 , filePath
		 , extraParams
		 , h
		 , Http::EAsyncCallbackExecutionMode::MainThread);
	}


	template <typename TypeOutput, typename TFunctor, typename TypeBody>
	inline void AsyncPutJsonJBody(HttpPtr const& http,
		RequestGroupCallbackPtr const& callbackPtr,
		TFunctor&& inFct,
		std::string const& url,
		TypeBody const& body,
		Http::Headers const& h = {})
	{
		Http::BodyParams const bodyParams(Json::ToString(body));
		auto dataOut = Tools::MakeSharedLockableData<TypeOutput>();

		callbackPtr->AddRequestToWait(); // register request to group
		http->AsyncPutJson<TypeOutput>(dataOut,
			[callbackPtr,
			fct = std::move(inFct)](long httpCode, Tools::TSharedLockableData<TypeOutput> const& joutPtr)
		{
			if (!callbackPtr->IsValid())
				return;
			const bool bSuccess = fct(httpCode, joutPtr);
			callbackPtr->OnRequestDone(bSuccess); // notify termination to request group
		},
			url, bodyParams, h, Http::EAsyncCallbackExecutionMode::MainThread);
	}


	template <typename TypeOutput, typename TFunctor>
	inline void AsyncPatchJson(HttpPtr const& http,
		RequestGroupCallbackPtr const& callbackPtr,
		TFunctor&& inFct,
		std::string const& url,
		Http::BodyParams const& bodyParams,
		Http::Headers const& h = {})
	{
		auto dataOut = Tools::MakeSharedLockableData<TypeOutput>();

		callbackPtr->AddRequestToWait(); // register request to group
		http->AsyncPatchJson<TypeOutput>(dataOut,
			[callbackPtr,
			fct = std::move(inFct)](long httpCode, Tools::TSharedLockableData<TypeOutput> const& joutPtr)
		{
			if (!callbackPtr->IsValid())
				return;
			const bool bSuccess = fct(httpCode, joutPtr);
			callbackPtr->OnRequestDone(bSuccess); // notify termination to request group
		},
			url, bodyParams, h, Http::EAsyncCallbackExecutionMode::MainThread);
	}

	template <typename TypeOutput, typename TFunctor, typename TypeBody>
	inline void AsyncPatchJsonJBody(HttpPtr const& http,
		RequestGroupCallbackPtr const& callbackPtr,
		TFunctor&& inFct,
		std::string const& url,
		TypeBody const& body,
		Http::Headers const& h = {})
	{
		AsyncPatchJson<TypeOutput>(http, callbackPtr, std::move(inFct),
			url, Json::ToString(body), h);
	}


	// bIsExpectingOutput can be set to false if TypeOutput is an "empty" type, to avoid logging an error
	// (our Json library makes an error when trying to parse an empty string as an empty struct...)
	template <typename TypeOutput, typename TFunctor, typename TypeBody>
	inline void AsyncDeleteJsonJBody(HttpPtr const& http,
		RequestGroupCallbackPtr const& callbackPtr,
		TFunctor&& inFct,
		std::string const& url,
		TypeBody const& body,
		Http::Headers const& h = {},
		bool bIsExpectingOutput = true)
	{
		Http::BodyParams const bodyParams(Json::ToString(body));
		auto dataOut = Tools::MakeSharedLockableData<TypeOutput>();

		callbackPtr->AddRequestToWait(); // register request to group
		http->AsyncDeleteJson<TypeOutput>(dataOut,
			[callbackPtr,
			fct = std::move(inFct)](long httpCode, Tools::TSharedLockableData<TypeOutput> const& joutPtr)
		{
			if (!callbackPtr->IsValid())
				return;
			const bool bSuccess = fct(httpCode, joutPtr);
			callbackPtr->OnRequestDone(bSuccess); // notify termination to request group
		},
			url, bodyParams, h, Http::EAsyncCallbackExecutionMode::MainThread, bIsExpectingOutput);
	}

	// Use this version if your request expects to response upon deletion.
	template <typename TFunctor, typename TypeBody>
	inline void AsyncDeleteJsonNoOutput(HttpPtr const& http,
		RequestGroupCallbackPtr const& callbackPtr,
		TFunctor&& inFct,
		std::string const& url,
		TypeBody const& body,
		Http::Headers const& h = {})
	{
		struct SJsonEmpty {};
		AsyncDeleteJsonJBody<SJsonEmpty>(http, callbackPtr,
			[fct = std::move(inFct)](long httpCode, Tools::TSharedLockableData<SJsonEmpty> const& /*joutPtr*/)
		{
			return fct(httpCode);
		},
			url, body, h, false);
	}

}
