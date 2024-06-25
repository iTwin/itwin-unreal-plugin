// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#include "ITwinCesiumIonServer.h"
#include "CesiumAsync/AsyncSystem.h"
#include "CesiumIonClient/Connection.h"
#include "ITwinCesiumRuntime.h"
#include "ITwinCesiumRuntimeSettings.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "CesiumIonClient/Connection.h"
#include "Factories/DataAssetFactory.h"
#include "FileHelpers.h"
#endif

/*static*/ UITwinCesiumIonServer* UITwinCesiumIonServer::_pDefaultForNewObjects = nullptr;

/*static*/ UITwinCesiumIonServer* UITwinCesiumIonServer::GetDefaultServer() {
  UPackage* Package = CreatePackage(
      TEXT("/Game/CesiumSettings/CesiumIonServers/ITwinCesiumIonSaaS"));
  Package->FullyLoad();
  UITwinCesiumIonServer* Server =
      Cast<UITwinCesiumIonServer>(Package->FindAssetInPackage());

#if WITH_EDITOR
  if (!IsValid(Server)) {
    UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
    Server = Cast<UITwinCesiumIonServer>(Factory->FactoryCreateNew(
        UITwinCesiumIonServer::StaticClass(),
        Package,
        "ITwinCesiumIonSaaS",
        RF_Public | RF_Standalone | RF_Transactional,
        nullptr,
        GWarn));

    Server->DisplayName = TEXT("ion.cesium.com");
    Server->ServerUrl = TEXT("https://ion.cesium.com");
    Server->ApiUrl = TEXT("https://api.cesium.com");
    Server->OAuth2ApplicationID = 190;

    FAssetRegistryModule::AssetCreated(Server);

    Package->FullyLoad();
    Package->SetDirtyFlag(true);
    UEditorLoadingAndSavingUtils::SavePackages({Package}, true);
  }
#endif

  return Server;
}

/*static*/ UITwinCesiumIonServer* UITwinCesiumIonServer::GetServerForNewObjects() {
  if (IsValid(_pDefaultForNewObjects)) {
    return _pDefaultForNewObjects;
  } else {
    return GetDefaultServer();
  }
}

/*static*/ void
UITwinCesiumIonServer::SetServerForNewObjects(UITwinCesiumIonServer* Server) {
  _pDefaultForNewObjects = Server;
}

#if WITH_EDITOR
UITwinCesiumIonServer*
UITwinCesiumIonServer::GetBackwardCompatibleServer(const FString& apiUrl) {
  // Return the default server if the API URL is unspecified or if it's the
  // standard SaaS API URL.
  if (apiUrl.IsEmpty() ||
      apiUrl.StartsWith(TEXT("https://api.ion.cesium.com")) ||
      apiUrl.StartsWith(TEXT("https://api.cesium.com"))) {
    return UITwinCesiumIonServer::GetDefaultServer();
  }

  // Find a server with this API URL.
  TArray<FAssetData> CesiumIonServers;
  FAssetRegistryModule& AssetRegistryModule =
      FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
  AssetRegistryModule.Get().GetAssetsByClass(
      UITwinCesiumIonServer::StaticClass()->GetClassPathName(),
      CesiumIonServers);

  FAssetData* pFound =
      CesiumIonServers.FindByPredicate([&apiUrl](const FAssetData& asset) {
        UITwinCesiumIonServer* pServer = Cast<UITwinCesiumIonServer>(asset.GetAsset());
        return pServer && pServer->ApiUrl == apiUrl;
      });

  if (pFound) {
    return Cast<UITwinCesiumIonServer>(pFound->GetAsset());
  }

  // Not found - create a new server asset.
  UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();

  UPackage* Package = nullptr;

  FString PackageBasePath = TEXT("/Game/CesiumSettings/CesiumIonServers/");
  FString PackageName;
  FString PackagePath;

  const int ArbitraryPackageIndexLimit = 10000;

  for (int i = 0; i < ArbitraryPackageIndexLimit; ++i) {
    PackageName = TEXT("FromApiUrl") + FString::FromInt(i);
    PackagePath = PackageBasePath + PackageName;
    Package = FindPackage(nullptr, *PackagePath);
    if (Package == nullptr) {
      Package = CreatePackage(*PackagePath);
      break;
    }
  }

  if (Package == nullptr)
    return nullptr;

  Package->FullyLoad();

  UITwinCesiumIonServer* Server = Cast<UITwinCesiumIonServer>(Factory->FactoryCreateNew(
      UITwinCesiumIonServer::StaticClass(),
      Package,
      FName(PackageName),
      RF_Public | RF_Standalone | RF_Transactional,
      nullptr,
      GWarn));

  Server->DisplayName = apiUrl;
  Server->ServerUrl = apiUrl;
  Server->ApiUrl = apiUrl;
  Server->OAuth2ApplicationID = 190;

  // Adopt the token from the default server, consistent with the behavior in
  // old versions of Cesium for Unreal.
  UITwinCesiumIonServer* pDefault = UITwinCesiumIonServer::GetDefaultServer();
  Server->DefaultIonAccessTokenId = pDefault->DefaultIonAccessTokenId;
  Server->DefaultIonAccessToken = pDefault->DefaultIonAccessToken;

  FAssetRegistryModule::AssetCreated(Server);

  Package->FullyLoad();
  Package->SetDirtyFlag(true);
  UEditorLoadingAndSavingUtils::SavePackages({Package}, true);

  return Server;
}

CesiumAsync::Future<void> UITwinCesiumIonServer::ResolveApiUrl() {
  if (!this->ApiUrl.IsEmpty())
    return ITwinCesium::getAsyncSystem().createResolvedFuture();

  if (this->ServerUrl.IsEmpty()) {
    // We don't even have a server URL, so use the SaaS defaults.
    this->ServerUrl = TEXT("https://ion.cesium.com/");
    this->ApiUrl = TEXT("https://api.cesium.com/");
    this->Modify();
    UEditorLoadingAndSavingUtils::SavePackages({this->GetPackage()}, true);
    return ITwinCesium::getAsyncSystem().createResolvedFuture();
  }

  TObjectPtr<UITwinCesiumIonServer> pServer = this;

  return CesiumIonClient::Connection::getApiUrl(
             ITwinCesium::getAsyncSystem(),
             ITwinCesium::getAssetAccessor(),
             TCHAR_TO_UTF8(*this->ServerUrl))
      .thenInMainThread([pServer](std::optional<std::string>&& apiUrl) {
        if (pServer && pServer->ApiUrl.IsEmpty()) {
          pServer->ApiUrl = UTF8_TO_TCHAR(apiUrl->c_str());
          pServer->Modify();
          UEditorLoadingAndSavingUtils::SavePackages(
              {pServer->GetPackage()},
              true);
        }
      });
}
#endif
