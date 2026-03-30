/*--------------------------------------------------------------------------------------+
|
|     $Source: UEHttp.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "CoreMinimal.h"


#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <Core/Network/Http.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

class FUEHttp : public AdvViz::SDK::Http, AdvViz::SDK::Tools::TypeId<FUEHttp>
{
public:
	ITWINRUNTIME_API static void Init();

	FUEHttp();

	virtual bool SupportsExecuteAsyncCallbackInMainThread() const override { return true; }


protected:
	virtual void SetBasicAuth(const char* login, const char* passwd) override {
		BE_ISSUE("Should not be used.");
	}
	virtual bool DecodeBase64(const std::string& src, RawData& buffer) const override;

	virtual Response DoGet(const std::string& url, const Headers& h = {}, bool isFullUrl = false) override
	{
		return Do(TEXT("GET"), url, {}, h, isFullUrl);
	}

	virtual void DoAsyncGet(std::function<void(const Response&)> callback, const std::string& url,
		const Headers& headers = {},
		bool isFullUrl = false,
		EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default) override
	{
		Do(TEXT("GET"), url, {}, headers, isFullUrl, callback, asyncCBExecMode);
	}

	virtual Response DoPatch(const std::string& url, const BodyParams& body, const Headers& h = {}) override
	{
		return Do(TEXT("PATCH"), url, body, h);
	}

	virtual void DoAsyncPatch(std::function<void(const Response&)> callback, const std::string& url,
		const BodyParams& body,
		const Headers& headers,
		EAsyncCallbackExecutionMode asyncCBExecMode) override
	{
		Do(TEXT("PATCH"), url, body, headers, false, callback, asyncCBExecMode);
	}

	virtual Response DoPost(const std::string& url, const BodyParams& body, const Headers& h = {}) override
	{
		return Do(TEXT("POST"), url, body, h);
	}

	virtual void DoAsyncPost(std::function<void(const Response&)> callback, const std::string& url,
		const BodyParams& body,
		const Headers& headers,
		EAsyncCallbackExecutionMode asyncCBExecMode) override
	{
		Do(TEXT("POST"), url, body, headers, false, callback, asyncCBExecMode);
	}

	virtual Response DoPostFile(const std::string& url, const std::string& fileParamName, const std::string& filePath,
		const KeyValueVector& extraParams = {}, const Headers& h = {}) override
	{
		return DoFile(TEXT("POST"), url, fileParamName, filePath, extraParams, h);
	}

	virtual void DoAsyncPostFile(std::function<void(const Response&)> callback, const std::string& url,
		const std::string& fileParamName, const std::string& filePath,
		const KeyValueVector& extraParams = {}, const Headers& h = {},
		EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default) override
	{
		DoFile(TEXT("POST"), url, fileParamName, filePath, extraParams, h, callback, asyncCBExecMode);
	}

	virtual Response DoPut(const std::string& url, const BodyParams& body, const Headers& h = {})  override
	{
		return Do(TEXT("PUT"), url, body, h);
	}

	virtual void DoAsyncPut(std::function<void(const Response&)> callback, const std::string& url,
		const BodyParams& body, const Headers& headers,
		EAsyncCallbackExecutionMode asyncCBExecMode)
	{
		Do(TEXT("PUT"), url, body, headers, false, callback, asyncCBExecMode);
	}

	virtual Response DoPutBinaryFile(const std::string& url, const std::string& filePath, const Headers& headers = {})  override
	{
		BE_ISSUE("Not implemented yet");
		return Response(0, std::string(""));
	}

	virtual Response DoDelete(const std::string& url, const BodyParams& body, const Headers& h = {}) override
	{
		return Do(TEXT("DELETE"), url, body, h);
	}

	virtual void DoAsyncDelete(std::function<void(const Response&)> callback, const std::string& url,
		const BodyParams& body = {}, const Headers& headers = {},
		EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default) override
	{
		Do(TEXT("DELETE"), url, body, headers, false, callback, asyncCBExecMode);
	}

	Response Do(FString verb, const std::string& url, const BodyParams& body, const Headers& headers = {}, bool isFullUrl = false,
		std::function<void(const Response&)> callbackFct = {},
		EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default);
	Response DoFile(FString verb, const std::string& url,
		const std::string& fileParamName, const std::string& filePath, const KeyValueVector& extraParams = {},
		const Headers& headers = {},
		std::function<void(const Response&)> callbackFct = {},
		EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default);

	using AdvViz::SDK::Tools::TypeId<FUEHttp>::GetTypeId;
	std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
	bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || AdvViz::SDK::Http::IsTypeOf(i); }
	
	std::string EncodeForUrl(const std::string& str) const override;
};
