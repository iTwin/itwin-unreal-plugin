/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinServiceActor.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <ITwinServiceActor.h>
#include <ITwinServerConnection.h>
#include <ITwinWebServices/ITwinAuthorizationManager.h>
#include <ITwinWebServices/ITwinWebServices.h>
#include <Engine/World.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Tools/Log.h>
#include <Compil/AfterNonUnrealIncludes.h>


ITWINRUNTIME_API DEFINE_LOG_CATEGORY(LogITwin);

AITwinServiceActor::AITwinServiceActor()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		// Use the default environment.
		Environment = UITwinWebServices::GetDefaultEnvironment(GetWorld(), EITwinEnvironment::Invalid);

		if (Environment == EITwinEnvironment::Invalid)
		{
			// If the default environment is invalid, use the active environment.
			// (useful for applications such as iTwin Engage, where only one active environment at a time).
			Environment = UITwinWebServices::GetActiveEnvironment();
		}

		// If no automatic environment could be determined, use the production environment.
		if (Environment == EITwinEnvironment::Invalid)
		{
			Environment = EITwinEnvironment::Prod;
		}
	}
}


void AITwinServiceActor::PostLoad()
{
	Super::PostLoad();
	// For compatibility for old scenes, if we have a web service with a non-default environment,
	// keep that one instead of overwriting it with the default environment.
	if (WebServices
		&& WebServices->IsValidLowLevel()
		&& WebServices->GetEnvironment() != EITwinEnvironment::Invalid
		&& WebServices->GetEnvironment() != GetEnvironment())
	{
		Environment = WebServices->GetEnvironment();
		EnvironmentSelectionMethod = EEnvironmentSelectionMethod::ESM_Manual;
	}
}

void AITwinServiceActor::BeginDestroy()
{
	if (WebServices)
	{
		// Make sure we won't execute any Http request callback while the object is half-destroyed, as it can
		// make Unreal crash on exit, *included* during the packaging (and thus block it randomly...)
		WebServices->SetObserver(nullptr);
	}
	Super::BeginDestroy();
}

void AITwinServiceActor::SetEnvironment(EITwinEnvironment NewEnvironment)
{
	bool const bUpdateWebServices = (WebServices && NewEnvironment != this->Environment);
	this->Environment = NewEnvironment;
	if (bUpdateWebServices)
	{
		UpdateWebServices();
	}
}

void AITwinServiceActor::CopyEnvironmentFrom(AITwinServiceActor const& Other)
{
	this->SetEnvironment(Other.GetEnvironment());
	this->EnvironmentSelectionMethod = Other.EnvironmentSelectionMethod;
}

void AITwinServiceActor::UpdateWebServices()
{
	// Update the environment if the selection method is automatic and the active environment has changed.
	EITwinEnvironment const ActiveEnv = UITwinWebServices::GetActiveEnvironment();
	if (EnvironmentSelectionMethod == EEnvironmentSelectionMethod::ESM_Automatic
		&& ActiveEnv != EITwinEnvironment::Invalid
		&& ActiveEnv != this->Environment)
	{
		this->Environment = ActiveEnv;
	}

	const bool bHasValidWebServices = WebServices && WebServices->IsValidLowLevel();
	const bool bHasChangedEnvironment =	bHasValidWebServices
		&& this->Environment != EITwinEnvironment::Invalid
		&& this->Environment != WebServices->GetEnvironment();
	const bool bHasObserver =
		bHasValidWebServices && WebServices->HasObserver(this);
	if (!bHasValidWebServices || bHasChangedEnvironment || !bHasObserver)
	{
		WebServices = NewObject<UITwinWebServices>(this);
		if (this->Environment != EITwinEnvironment::Invalid)
		{
			WebServices->SetEnvironment(this->Environment);
		}
		WebServices->SetObserver(this);
	}
}

#if WITH_TESTS
void AITwinServiceActor::SetTestMode(FString const& ServerUrl)
{
	// Set a fake access token, to prevent the AuthorizationManager from trying to retrieve a real token.
	auto& AuthMngr = FITwinAuthorizationManager::GetInstance(AdvViz::SDK::EITwinEnvironment::Prod);
	if (ensure(AuthMngr))
	{
		AuthMngr->SetOverrideAccessToken("TestToken", AdvViz::SDK::EITwinAuthOverrideMode::Testing);
	}
	// Create WebServices pointing to the mock server.
	WebServices = NewObject<UITwinWebServices>(this);
	this->Environment = EITwinEnvironment::Prod;
	WebServices->SetEnvironment(this->Environment);
	WebServices->SetTestServerURL(ServerUrl);
	WebServices->SetObserver(this);
}

bool AITwinServiceActor::IsInTestMode(FString& OutServerUrl) const
{
	if (WebServices)
	{
		OutServerUrl = WebServices->GetServerURL();
		return OutServerUrl.StartsWith(TEXT("http://localhost:"));
	}
	return false;
}

#endif // WITH_TESTS

const UITwinWebServices* AITwinServiceActor::GetWebServices() const
{
	return WebServices.Get();
}

UITwinWebServices* AITwinServiceActor::GetMutableWebServices()
{
	return WebServices.Get();
}

const TCHAR* AITwinServiceActor::GetObserverName() const
{
	// NB: ideally, I would not have overridden it (but GENERATED_BODY() macro seems to impose that the class
	// can be instantiated...)
	checkNoEntry();
	return TEXT("<unknown>");
}

AdvViz::SDK::EITwinAuthStatus AITwinServiceActor::CheckAuthorizationStatus(bool bRequestAuthorisationIfNeeded /*= true*/)
{
	UpdateWebServices();
	if (WebServices && WebServices->HasAccessToken())
	{
		return AdvViz::SDK::EITwinAuthStatus::Success;
	}
	if (ensureMsgf(WebServices, TEXT("WebServices was not yet created")))
	{
		if (WebServices->IsAuthorizationInProgress())
		{
			return AdvViz::SDK::EITwinAuthStatus::InProgress;
		}
		else if (bRequestAuthorisationIfNeeded)
		{
			return WebServices->CheckAuthorizationStatus();
		}
	}
	return AdvViz::SDK::EITwinAuthStatus::None;
}

void AITwinServiceActor::UpdateOnSuccessfulAuthorization()
{

}

void AITwinServiceActor::OnAuthorizationDone(bool bSuccess, std::string const& AuthError)
{
	if (bSuccess)
	{
		UpdateWebServices();

		if (WebServices && WebServices->HasAccessToken())
		{
			UpdateOnSuccessfulAuthorization();
		}
	}
	else
	{
		BE_LOGE("ITwinAPI", "[" << TCHAR_TO_UTF8(GetObserverName()) << "] Authorization failure: " << AuthError);
	}
}

bool AITwinServiceActor::HasAccessToken() const
{
	auto const Env = GetEnvironment();
	if (Env != EITwinEnvironment::Invalid)
	{
		auto const& AuthMngr = FITwinAuthorizationManager::GetInstance(
			static_cast<AdvViz::SDK::EITwinEnvironment>(Env));
		return AuthMngr && AuthMngr->HasAccessToken();
	}
	return false;
}

FString AITwinServiceActor::GetAccessToken() const
{
	auto const Env = GetEnvironment();
	if (Env != EITwinEnvironment::Invalid)
	{
		auto token = AITwinServerConnection::GetAccessTokenStringForEnv(Env);
		if (!token.IsEmpty())
			return token;
	}
	BE_LOGE("ITwinAPI", "[" << TCHAR_TO_UTF8(GetObserverName()) << "] No access token");
	return {};
}

#if WITH_EDITOR
void AITwinServiceActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FName const PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AITwinServiceActor, Environment))
	{
		UpdateWebServices();
	}
}
#endif // WITH_EDITOR