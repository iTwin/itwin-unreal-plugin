// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#pragma once

#include "Blueprint/UserWidget.h"
#include "Components/RichTextBlockDecorator.h"
#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include <memory>
#include <string>
#include "ITwinScreenCreditsWidget.generated.h"

DECLARE_DELEGATE(FOnPopupClicked)

UCLASS()
class UITwinScreenCreditsWidget : public UUserWidget {
  GENERATED_BODY()
public:
  /**
   * Attempts to load an image from the given URL and returns the name of the
   * image to be referenced in RTF.
   */
  std::string LoadImage(const std::string& url);

  void SetCredits(const FString& InCredits, const FString& InOnScreenCredits);

private:
  UITwinScreenCreditsWidget(const FObjectInitializer& ObjectInitializer);
  ~UITwinScreenCreditsWidget();
  virtual void NativeConstruct() override;

  void OnPopupClicked();

  void HandleImageRequest(
      FHttpRequestPtr HttpRequest,
      FHttpResponsePtr HttpResponse,
      bool bSucceeded,
      int32 id);

  UPROPERTY(meta = (BindWidget))
  class URichTextBlock* RichTextOnScreen;

  UPROPERTY(meta = (BindWidget))
  class URichTextBlock* RichTextPopup;

  UPROPERTY(meta = (BindWidget))
  class UBackgroundBlur* BackgroundBlur;

  UPROPERTY()
  TArray<UTexture2D*> _textures;

  FString _credits = "";
  FString _onScreenCredits = "";
  bool _showPopup = false;
  class UITwinCreditsDecorator* _decoratorOnScreen;
  class UITwinCreditsDecorator* _decoratorPopup;
  int32 _numImagesLoading;
  FSlateFontInfo _font;
  TArray<FSlateBrush*> _creditImages;
  friend class UITwinCreditsDecorator;
};

UCLASS()
class UITwinCreditsDecorator : public URichTextBlockDecorator {
  GENERATED_BODY()

public:
  UITwinCreditsDecorator(const FObjectInitializer& ObjectInitializer);

  virtual TSharedPtr<ITextDecorator>
  CreateDecorator(URichTextBlock* InOwner) override;

  virtual const FSlateBrush* FindImageBrush(int32 id);

  UITwinScreenCreditsWidget* CreditsWidget;
  FOnPopupClicked PopupClicked;
};
