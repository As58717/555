#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class FUICommandList;

class FPanoramaCaptureEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void ExtendLevelEditorToolbar();
    void HandleToggleCapture();
    void UpdateStatusWidget();
    TSharedRef<class SWidget> GenerateControlMenu();
    FReply HandleStartStopButton();
    void ApplySettingsToControllers();
    void HandleTogglePreviewWindow();
    bool IsPreviewWindowOpen() const;
    class UTexture2D* ResolvePreviewTexture() const;
    FText BuildPreviewStatusText() const;
    static void ForEachController(TFunctionRef<void(class UPanoramaCaptureController*)> InFunc);
    bool IsAnyControllerCapturing() const;
    FText BuildStatusText() const;

    TSharedPtr<FUICommandList> CommandList;
    TWeakPtr<class SWidget> StatusWidget;
    TWeakPtr<class STextBlock> StatusTextBlock;
    TWeakPtr<class SComboButton> ControlMenuButton;
    TSharedPtr<class SWindow> PreviewWindow;
    TWeakPtr<class SPanoramaPreviewWidget> PreviewWidget;
    FDelegateHandle StartupHandle;
};
