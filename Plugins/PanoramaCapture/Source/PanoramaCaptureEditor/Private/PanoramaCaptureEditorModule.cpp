#include "PanoramaCaptureEditorModule.h"

#include "Delegates/Delegate.h"
#include "LevelEditor.h"
#include "PanoramaCaptureController.h"
#include "PanoramaCaptureModule.h"
#include "PanoramaCaptureSettings.h"
#include "ToolMenus.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SWindow.h"
#include "Engine/Texture2D.h"
#include "Slate/SlateBrushAsset.h"

class SPanoramaPreviewWidget : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPanoramaPreviewWidget) {}
        SLATE_ARGUMENT(TFunction<UTexture2D*()>, TextureProvider)
        SLATE_ARGUMENT(TFunction<FText()>, StatusProvider)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        TextureProvider = InArgs._TextureProvider;
        StatusProvider = InArgs._StatusProvider;

        PreviewBrush = MakeShared<FSlateDynamicImageBrush>(FName(), FVector2D(512.f, 256.f));

        ChildSlot
        [
            SNew(SOverlay)
            + SOverlay::Slot()
            [
                SNew(SBorder)
                .BorderBackgroundColor(FLinearColor::Black)
                [
                    SNew(SBox)
                    .WidthOverride(512.f)
                    .HeightOverride(256.f)
                    [
                        SAssignNew(ImageWidget, SImage)
                        .Image(PreviewBrush.Get())
                    ]
                ]
            ]
            + SOverlay::Slot()
            .HAlign(HAlign_Left)
            .VAlign(VAlign_Bottom)
            .Padding(FMargin(8.f))
            [
                SAssignNew(StatusText, STextBlock)
                .Text(StatusProvider ? StatusProvider() : FText::GetEmpty())
                .ColorAndOpacity(FLinearColor::White)
                .ShadowColorAndOpacity(FLinearColor::Black)
                .ShadowOffset(FVector2D(1.f, 1.f))
            ]
        ];

        RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SPanoramaPreviewWidget::HandleActiveTimer));
    }

private:
    EActiveTimerReturnType HandleActiveTimer(double InCurrentTime, float InDeltaTime)
    {
        UpdateTexture();
        UpdateStatus();
        return EActiveTimerReturnType::Continue;
    }

    void UpdateTexture()
    {
        if (!TextureProvider)
        {
            return;
        }

        if (UTexture2D* Texture = TextureProvider())
        {
            if (PreviewBrush->GetResourceObject() != Texture)
            {
                PreviewBrush->SetResourceObject(Texture);
                PreviewBrush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
            }

            if (ImageWidget.IsValid())
            {
                ImageWidget->SetImage(PreviewBrush.Get());
            }
        }
    }

    void UpdateStatus()
    {
        if (StatusProvider && StatusText.IsValid())
        {
            StatusText->SetText(StatusProvider());
        }
    }

private:
    TFunction<UTexture2D*()> TextureProvider;
    TFunction<FText()> StatusProvider;
    TSharedPtr<FSlateDynamicImageBrush> PreviewBrush;
    TSharedPtr<SImage> ImageWidget;
    TSharedPtr<STextBlock> StatusText;
};

class FPanoramaCaptureEditorCommands : public TCommands<FPanoramaCaptureEditorCommands>
{
public:
    FPanoramaCaptureEditorCommands()
        : TCommands<FPanoramaCaptureEditorCommands>(TEXT("PanoramaCaptureEditor"), NSLOCTEXT("PanoramaCaptureEditor", "Commands", "Panorama Capture"), NAME_None, FAppStyle::GetAppStyleSetName())
    {
    }

    virtual void RegisterCommands() override
    {
        UI_COMMAND(ToggleCapture, "Panorama Capture", "Start or stop 360 capture.", EUserInterfaceActionType::ToggleButton, FInputChord());
        UI_COMMAND(TogglePreview, "Panorama Preview", "Open the live panorama preview window.", EUserInterfaceActionType::ToggleButton, FInputChord());
    }

    TSharedPtr<FUICommandInfo> ToggleCapture;
    TSharedPtr<FUICommandInfo> TogglePreview;
};

void FPanoramaCaptureEditorModule::StartupModule()
{
    FPanoramaCaptureEditorCommands::Register();

    CommandList = MakeShared<FUICommandList>();
    CommandList->MapAction(FPanoramaCaptureEditorCommands::Get().ToggleCapture,
        FExecuteAction::CreateRaw(this, &FPanoramaCaptureEditorModule::HandleToggleCapture),
        FCanExecuteAction::CreateLambda([] { return true; }),
        FIsActionChecked::CreateRaw(this, &FPanoramaCaptureEditorModule::IsAnyControllerCapturing));
    CommandList->MapAction(FPanoramaCaptureEditorCommands::Get().TogglePreview,
        FExecuteAction::CreateRaw(this, &FPanoramaCaptureEditorModule::HandleTogglePreviewWindow),
        FCanExecuteAction::CreateLambda([] { return true; }),
        FIsActionChecked::CreateRaw(this, &FPanoramaCaptureEditorModule::IsPreviewWindowOpen));

    StartupHandle = UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FPanoramaCaptureEditorModule::ExtendLevelEditorToolbar));
    UpdateStatusWidget();
}

void FPanoramaCaptureEditorModule::ShutdownModule()
{
    if (StartupHandle.IsValid())
    {
        UToolMenus::UnregisterStartupCallback(StartupHandle);
        StartupHandle = FDelegateHandle();
    }
    UToolMenus::UnregisterOwner(this);
    if (PreviewWindow.IsValid())
    {
        PreviewWindow->RequestDestroyWindow();
        PreviewWindow.Reset();
    }
    PreviewWidget.Reset();
    FPanoramaCaptureEditorCommands::Unregister();
    CommandList.Reset();
    StatusWidget.Reset();
    StatusTextBlock.Reset();
    ControlMenuButton.Reset();
}

void FPanoramaCaptureEditorModule::ExtendLevelEditorToolbar()
{
    FToolMenuOwnerScoped OwnerScoped(this);
    UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
    if (!ToolbarMenu)
    {
        return;
    }

    FToolMenuSection& Section = ToolbarMenu->AddSection("PanoramaCapture", NSLOCTEXT("PanoramaCaptureEditor", "Section", "Panorama"), FToolMenuInsert(TEXT("Settings"), EToolMenuInsertType::After));

    FToolMenuEntry CaptureEntry = FToolMenuEntry::InitToolBarButton(FPanoramaCaptureEditorCommands::Get().ToggleCapture);
    CaptureEntry.SetCommandList(CommandList);
    Section.AddEntry(CaptureEntry);

    FToolMenuEntry PreviewEntry = FToolMenuEntry::InitToolBarButton(FPanoramaCaptureEditorCommands::Get().TogglePreview);
    PreviewEntry.SetCommandList(CommandList);
    Section.AddEntry(PreviewEntry);

    const TSharedRef<STextBlock> StatusText = SNew(STextBlock).Text(BuildStatusText());
    StatusTextBlock = StatusText;

    FToolMenuEntry StatusEntry = FToolMenuEntry::InitWidget(TEXT("PanoramaCaptureStatus"), StatusText, FText(), false, false);
    Section.AddEntry(StatusEntry);

    StatusWidget = StatusText;

    FToolMenuEntry ControlEntry = FToolMenuEntry::InitWidget(TEXT("PanoramaCaptureControls"),
        SAssignNew(ControlMenuButton, SComboButton)
            .OnGetMenuContent(this, &FPanoramaCaptureEditorModule::GenerateControlMenu)
            .ButtonContent()
            [
                SNew(STextBlock)
                .Text(NSLOCTEXT("PanoramaCaptureEditor", "SettingsButton", "Panorama Settings"))
            ],
        NSLOCTEXT("PanoramaCaptureEditor", "SettingsTooltip", "Adjust panorama capture settings"),
        false);
    Section.AddEntry(ControlEntry);
}

void FPanoramaCaptureEditorModule::FillToolbar(FToolBarBuilder& Builder)
{
    Builder.AddToolBarButton(FPanoramaCaptureEditorCommands::Get().ToggleCapture);
}

void FPanoramaCaptureEditorModule::HandleToggleCapture()
{
    const bool bCapturing = IsAnyControllerCapturing();
    ForEachController([bCapturing](UPanoramaCaptureController* Controller)
    {
        if (!Controller)
        {
            return;
        }

        if (bCapturing)
        {
            Controller->StopCapture();
        }
        else
        {
            Controller->StartCapture();
        }
    });

    UpdateStatusWidget();
}

void FPanoramaCaptureEditorModule::UpdateStatusWidget()
{
    if (TSharedPtr<STextBlock> StatusText = StatusTextBlock.Pin())
    {
        StatusText->SetText(BuildStatusText());
    }
}

TSharedRef<SWidget> FPanoramaCaptureEditorModule::GenerateControlMenu()
{
    UPanoramaCaptureSettings* Settings = GetMutableDefault<UPanoramaCaptureSettings>();
    FCaptureOutputSettings& Defaults = Settings->DefaultOutput;

    const bool bCapturing = IsAnyControllerCapturing();

    TSharedRef<SVerticalBox> Root = SNew(SVerticalBox);

    Root->AddSlot()
        .AutoHeight()
        .Padding(4.0f)
        [
            SNew(SButton)
            .Text(bCapturing ? NSLOCTEXT("PanoramaCaptureEditor", "StopCapture", "Stop Capture") : NSLOCTEXT("PanoramaCaptureEditor", "StartCapture", "Start Capture"))
            .OnClicked_Raw(this, &FPanoramaCaptureEditorModule::HandleStartStopButton)
        ];

    Root->AddSlot()
        .AutoHeight()
        .Padding(4.0f)
        [
            SNew(SButton)
            .Text_Lambda([this]()
            {
                return IsPreviewWindowOpen()
                    ? NSLOCTEXT("PanoramaCaptureEditor", "ClosePreview", "Close Preview")
                    : NSLOCTEXT("PanoramaCaptureEditor", "OpenPreview", "Open Preview");
            })
            .OnClicked_Lambda([this]()
            {
                HandleTogglePreviewWindow();
                return FReply::Handled();
            })
        ];

    Root->AddSlot()
        .AutoHeight()
        .Padding(4.0f)
        [
            SNew(STextBlock)
            .Text(BuildStatusText())
        ];

    Root->AddSlot()
        .AutoHeight()
        .Padding(2.0f)
        [
            MakeLabeledFloat(NSLOCTEXT("PanoramaCaptureEditor", "StereoIPD", "IPD (cm)"), Defaults.InterpupillaryDistanceCm, 0.0f, 12.0f, [this, Settings](float NewValue)
            {
                Settings->DefaultOutput.InterpupillaryDistanceCm = NewValue;
                Settings->SaveConfig();
                ApplySettingsToControllers();
            })
        ];

    Root->AddSlot()
        .AutoHeight()
        .Padding(2.0f)
        [
            SNew(SCheckBox)
            .IsEnabled(Defaults.StereoMode != EPanoramaStereoMode::Mono)
            .IsChecked(Defaults.bUseStereoToeIn ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
            .OnCheckStateChanged_Lambda([this, Settings](ECheckBoxState NewState)
            {
                Settings->DefaultOutput.bUseStereoToeIn = (NewState == ECheckBoxState::Checked);
                Settings->SaveConfig();
                ApplySettingsToControllers();
            })
            .Content()[SNew(STextBlock).Text(NSLOCTEXT("PanoramaCaptureEditor", "StereoToeIn", "Toe-In Cameras"))]
        ];

    Root->AddSlot()
        .AutoHeight()
        .Padding(2.0f)
        [
            MakeLabeledFloat(NSLOCTEXT("PanoramaCaptureEditor", "ToeInAngle", "Toe-In Angle"), Defaults.ToeInAngleDegrees, 0.0f, 10.0f, [this, Settings](float NewValue)
            {
                Settings->DefaultOutput.ToeInAngleDegrees = NewValue;
                Settings->SaveConfig();
                ApplySettingsToControllers();
            })
        ];

    Root->AddSlot()
        .AutoHeight()
        .Padding(2.0f)
        [
            SNew(SCheckBox)
            .IsChecked(Defaults.StereoMode != EPanoramaStereoMode::Mono ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
            .OnCheckStateChanged_Lambda([this, Settings](ECheckBoxState NewState)
            {
                FCaptureOutputSettings& SettingsDefaults = Settings->DefaultOutput;
                SettingsDefaults.StereoMode = (NewState == ECheckBoxState::Checked) ? EPanoramaStereoMode::StereoOverUnder : EPanoramaStereoMode::Mono;
                Settings->SaveConfig();
                ApplySettingsToControllers();
                UpdateStatusWidget();
            })
            .Content()[SNew(STextBlock).Text(NSLOCTEXT("PanoramaCaptureEditor", "StereoToggle", "Enable Stereo"))]
        ];

    Root->AddSlot()
        .AutoHeight()
        .Padding(2.0f)
        [
            SNew(SCheckBox)
            .IsEnabled(Defaults.StereoMode != EPanoramaStereoMode::Mono)
            .IsChecked(Defaults.StereoMode == EPanoramaStereoMode::StereoSideBySide ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
            .OnCheckStateChanged_Lambda([this, Settings](ECheckBoxState NewState)
            {
                FCaptureOutputSettings& SettingsDefaults = Settings->DefaultOutput;
                if (SettingsDefaults.StereoMode == EPanoramaStereoMode::Mono)
                {
                    return;
                }
                SettingsDefaults.StereoMode = (NewState == ECheckBoxState::Checked) ? EPanoramaStereoMode::StereoSideBySide : EPanoramaStereoMode::StereoOverUnder;
                Settings->SaveConfig();
                ApplySettingsToControllers();
            })
            .Content()[SNew(STextBlock).Text(NSLOCTEXT("PanoramaCaptureEditor", "StereoLayout", "Side-by-Side Layout"))]
        ];

    Root->AddSlot()
        .AutoHeight()
        .Padding(2.0f)
        [
            SNew(SCheckBox)
            .IsChecked(Defaults.NVENC.Codec == ENVENCCodec::HEVC ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
            .OnCheckStateChanged_Lambda([this, Settings](ECheckBoxState NewState)
            {
                Settings->DefaultOutput.NVENC.Codec = (NewState == ECheckBoxState::Checked) ? ENVENCCodec::HEVC : ENVENCCodec::H264;
                Settings->SaveConfig();
                ApplySettingsToControllers();
            })
            .Content()[SNew(STextBlock).Text(NSLOCTEXT("PanoramaCaptureEditor", "HEVCToggle", "Use HEVC"))]
        ];

    Root->AddSlot()
        .AutoHeight()
        .Padding(2.0f)
        [
            SNew(SCheckBox)
            .IsChecked(Defaults.bEnablePreview ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
            .OnCheckStateChanged_Lambda([this, Settings](ECheckBoxState NewState)
            {
                Settings->DefaultOutput.bEnablePreview = (NewState == ECheckBoxState::Checked);
                Settings->SaveConfig();
                ApplySettingsToControllers();
                UpdateStatusWidget();
            })
            .Content()[SNew(STextBlock).Text(NSLOCTEXT("PanoramaCaptureEditor", "PreviewToggle", "Realtime Preview"))]
        ];

    auto MakeLabeledFloat = [&](const FText& Label, float Value, float MinValue, float MaxValue, TFunction<void(float)> OnCommit)
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SNew(STextBlock).Text(Label)
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f)
            [
                SNew(SSpinBox<float>)
                .MinValue(MinValue)
                .MaxValue(MaxValue)
                .Value(Value)
                .OnValueCommitted_Lambda([OnCommit](float NewValue, ETextCommit::Type)
                {
                    OnCommit(NewValue);
                })
            ];
    };

    Root->AddSlot().AutoHeight().Padding(2.0f)
        [MakeLabeledFloat(NSLOCTEXT("PanoramaCaptureEditor", "BitrateLabel", "Bitrate (Mbps)"), Defaults.NVENC.BitrateMbps, 1.0f, 2000.0f, [this, Settings](float NewValue)
        {
            Settings->DefaultOutput.NVENC.BitrateMbps = NewValue;
            Settings->SaveConfig();
            ApplySettingsToControllers();
        })];

    Root->AddSlot().AutoHeight().Padding(2.0f)
        [MakeLabeledFloat(NSLOCTEXT("PanoramaCaptureEditor", "MaxBitrateLabel", "Max Bitrate (Mbps)"), Defaults.NVENC.MaxBitrateMbps, 1.0f, 4000.0f, [this, Settings](float NewValue)
        {
            Settings->DefaultOutput.NVENC.MaxBitrateMbps = NewValue;
            Settings->SaveConfig();
            ApplySettingsToControllers();
        })];

    auto MakeLabeledInt = [&](const FText& Label, int32 Value, int32 MinValue, int32 MaxValue, TFunction<void(int32)> OnCommit)
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SNew(STextBlock).Text(Label)
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f)
            [
                SNew(SSpinBox<int32>)
                .MinValue(MinValue)
                .MaxValue(MaxValue)
                .Value(Value)
                .OnValueCommitted_Lambda([OnCommit](int32 NewValue, ETextCommit::Type)
                {
                    OnCommit(NewValue);
                })
            ];
    };

    Root->AddSlot().AutoHeight().Padding(2.0f)
        [MakeLabeledInt(NSLOCTEXT("PanoramaCaptureEditor", "GOPLabel", "GOP Length"), Defaults.NVENC.GOPLength, 1, 600, [this, Settings](int32 NewValue)
        {
            Settings->DefaultOutput.NVENC.GOPLength = NewValue;
            Settings->SaveConfig();
            ApplySettingsToControllers();
        })];

    Root->AddSlot().AutoHeight().Padding(2.0f)
        [MakeLabeledInt(NSLOCTEXT("PanoramaCaptureEditor", "BFrameLabel", "B-Frames"), Defaults.NVENC.BFrameCount, 0, 6, [this, Settings](int32 NewValue)
        {
            Settings->DefaultOutput.NVENC.BFrameCount = NewValue;
            Settings->DefaultOutput.NVENC.bEnableBFrames = (NewValue > 0);
            Settings->SaveConfig();
            ApplySettingsToControllers();
        })];

    Root->AddSlot().AutoHeight().Padding(2.0f)
        [MakeLabeledFloat(NSLOCTEXT("PanoramaCaptureEditor", "RingBufferSeconds", "Ring Buffer Seconds"), Defaults.RingBufferDurationSeconds, 0.1f, 30.0f, [this, Settings](float NewValue)
        {
            Settings->DefaultOutput.RingBufferDurationSeconds = NewValue;
            Settings->SaveConfig();
            ApplySettingsToControllers();
        })];

    Root->AddSlot().AutoHeight().Padding(2.0f)
        [MakeLabeledInt(NSLOCTEXT("PanoramaCaptureEditor", "RingBufferCapacity", "Ring Buffer Capacity"), Defaults.RingBufferCapacityOverride, 0, 2048, [this, Settings](int32 NewValue)
        {
            Settings->DefaultOutput.RingBufferCapacityOverride = NewValue;
            Settings->SaveConfig();
            ApplySettingsToControllers();
        })];

    Root->AddSlot().AutoHeight().Padding(2.0f)
        [
            SNew(SCheckBox)
            .IsChecked(Defaults.bEnablePreflight ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
            .OnCheckStateChanged_Lambda([this, Settings](ECheckBoxState NewState)
            {
                Settings->DefaultOutput.bEnablePreflight = (NewState == ECheckBoxState::Checked);
                Settings->SaveConfig();
                ApplySettingsToControllers();
            })
            .Content()[SNew(STextBlock).Text(NSLOCTEXT("PanoramaCaptureEditor", "EnablePreflight", "Enable Preflight"))]
        ];

    Root->AddSlot().AutoHeight().Padding(2.0f)
        [MakeLabeledFloat(NSLOCTEXT("PanoramaCaptureEditor", "MinDisk", "Min Disk (GB)"), Defaults.MinFreeDiskGB, 0.0f, 500.0f, [this, Settings](float NewValue)
        {
            Settings->DefaultOutput.MinFreeDiskGB = NewValue;
            Settings->SaveConfig();
            ApplySettingsToControllers();
        })];

    Root->AddSlot().AutoHeight().Padding(2.0f)
        [MakeLabeledFloat(NSLOCTEXT("PanoramaCaptureEditor", "MinMemory", "Min Memory (GB)"), Defaults.MinFreeMemoryGB, 0.0f, 128.0f, [this, Settings](float NewValue)
        {
            Settings->DefaultOutput.MinFreeMemoryGB = NewValue;
            Settings->SaveConfig();
            ApplySettingsToControllers();
        })];

    Root->AddSlot().AutoHeight().Padding(2.0f)
        [MakeLabeledFloat(NSLOCTEXT("PanoramaCaptureEditor", "PreviewScale", "Preview Scale"), Defaults.PreviewScale, 0.1f, 1.0f, [this, Settings](float NewValue)
        {
            Settings->DefaultOutput.PreviewScale = NewValue;
            Settings->SaveConfig();
            ApplySettingsToControllers();
        })];

    Root->AddSlot().AutoHeight().Padding(2.0f)
        [MakeLabeledFloat(NSLOCTEXT("PanoramaCaptureEditor", "PreviewMaxFPS", "Preview Max FPS"), Defaults.PreviewMaxFPS, 0.0f, 120.0f, [this, Settings](float NewValue)
        {
            Settings->DefaultOutput.PreviewMaxFPS = NewValue;
            Settings->SaveConfig();
            ApplySettingsToControllers();
        })];

    Root->AddSlot().AutoHeight().Padding(2.0f)
        [
            SNew(SCheckBox)
            .IsChecked(Defaults.GammaSpace == EPanoramaGammaSpace::Linear ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
            .OnCheckStateChanged_Lambda([this, Settings](ECheckBoxState NewState)
            {
                Settings->DefaultOutput.GammaSpace = (NewState == ECheckBoxState::Checked) ? EPanoramaGammaSpace::Linear : EPanoramaGammaSpace::sRGB;
                Settings->SaveConfig();
                ApplySettingsToControllers();
            })
            .Content()[SNew(STextBlock).Text(NSLOCTEXT("PanoramaCaptureEditor", "LinearGamma", "Linear Gamma"))]
        ];

    Root->AddSlot().AutoHeight().Padding(2.0f)
        [MakeLabeledFloat(NSLOCTEXT("PanoramaCaptureEditor", "SeamFeather", "Seam Feather (deg)"), Defaults.SeamBlendAngleDegrees, 0.0f, 5.0f, [this, Settings](float NewValue)
        {
            Settings->DefaultOutput.SeamBlendAngleDegrees = NewValue;
            Settings->SaveConfig();
            ApplySettingsToControllers();
        })];

    TSharedRef<TArray<TSharedPtr<FString>>> PrimariesOptions = MakeShared<TArray<TSharedPtr<FString>>>();
    PrimariesOptions->Add(MakeShared<FString>(TEXT("Rec.709")));
    PrimariesOptions->Add(MakeShared<FString>(TEXT("Rec.2020")));

    auto ResolvePrimariesLabel = [PrimariesOptions, Settings]() -> TSharedPtr<FString>
    {
        return (Settings->DefaultOutput.ColorPrimaries == EPanoramaColorPrimaries::Rec2020) ? (*PrimariesOptions)[1] : (*PrimariesOptions)[0];
    };

    Root->AddSlot().AutoHeight().Padding(2.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SNew(STextBlock).Text(NSLOCTEXT("PanoramaCaptureEditor", "ColorPrimaries", "Color Primaries"))
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f)
            [
                SNew(SComboBox<TSharedPtr<FString>>)
                .OptionsSource(&PrimariesOptions.Get())
                .InitiallySelectedItem(ResolvePrimariesLabel())
                .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
                {
                    return SNew(STextBlock).Text(FText::FromString(*Item));
                })
                .OnSelectionChanged_Lambda([this, Settings, PrimariesOptions](TSharedPtr<FString> Item, ESelectInfo::Type)
                {
                    if (!Item.IsValid())
                    {
                        return;
                    }

                    Settings->DefaultOutput.ColorPrimaries = (*Item == *(*PrimariesOptions)[1]) ? EPanoramaColorPrimaries::Rec2020 : EPanoramaColorPrimaries::Rec709;
                    Settings->SaveConfig();
                    ApplySettingsToControllers();
                })
                [
                    SNew(STextBlock).Text_Lambda([ResolvePrimariesLabel]()
                    {
                        return FText::FromString(*ResolvePrimariesLabel());
                    })
                ]
            ]
        ];

    TSharedRef<TArray<TSharedPtr<FString>>> TransferOptions = MakeShared<TArray<TSharedPtr<FString>>>();
    TransferOptions->Add(MakeShared<FString>(TEXT("BT.1886")));
    TransferOptions->Add(MakeShared<FString>(TEXT("sRGB")));
    TransferOptions->Add(MakeShared<FString>(TEXT("PQ")));
    TransferOptions->Add(MakeShared<FString>(TEXT("HLG")));

    auto ResolveTransferLabel = [TransferOptions, Settings]() -> TSharedPtr<FString>
    {
        switch (Settings->DefaultOutput.TransferFunction)
        {
        case EPanoramaTransferFunction::sRGB: return (*TransferOptions)[1];
        case EPanoramaTransferFunction::PQ: return (*TransferOptions)[2];
        case EPanoramaTransferFunction::HLG: return (*TransferOptions)[3];
        default: return (*TransferOptions)[0];
        }
    };

    Root->AddSlot().AutoHeight().Padding(2.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SNew(STextBlock).Text(NSLOCTEXT("PanoramaCaptureEditor", "TransferFunction", "Transfer Function"))
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f)
            [
                SNew(SComboBox<TSharedPtr<FString>>)
                .OptionsSource(&TransferOptions.Get())
                .InitiallySelectedItem(ResolveTransferLabel())
                .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
                {
                    return SNew(STextBlock).Text(FText::FromString(*Item));
                })
                .OnSelectionChanged_Lambda([this, Settings, TransferOptions](TSharedPtr<FString> Item, ESelectInfo::Type)
                {
                    if (!Item.IsValid())
                    {
                        return;
                    }

                    if (*Item == *(*TransferOptions)[1])
                    {
                        Settings->DefaultOutput.TransferFunction = EPanoramaTransferFunction::sRGB;
                    }
                    else if (*Item == *(*TransferOptions)[2])
                    {
                        Settings->DefaultOutput.TransferFunction = EPanoramaTransferFunction::PQ;
                    }
                    else if (*Item == *(*TransferOptions)[3])
                    {
                        Settings->DefaultOutput.TransferFunction = EPanoramaTransferFunction::HLG;
                    }
                    else
                    {
                        Settings->DefaultOutput.TransferFunction = EPanoramaTransferFunction::BT1886;
                    }
                    Settings->SaveConfig();
                    ApplySettingsToControllers();
                })
                [
                    SNew(STextBlock).Text_Lambda([ResolveTransferLabel]()
                    {
                        return FText::FromString(*ResolveTransferLabel());
                    })
                ]
            ]
        ];

    TSharedRef<TArray<TSharedPtr<FString>>> MatrixOptions = MakeShared<TArray<TSharedPtr<FString>>>();
    MatrixOptions->Add(MakeShared<FString>(TEXT("BT.709")));
    MatrixOptions->Add(MakeShared<FString>(TEXT("BT.2020")));

    auto ResolveMatrixLabel = [MatrixOptions, Settings]() -> TSharedPtr<FString>
    {
        return (Settings->DefaultOutput.MatrixCoefficients == EPanoramaMatrixCoefficients::BT2020NCL) ? (*MatrixOptions)[1] : (*MatrixOptions)[0];
    };

    Root->AddSlot().AutoHeight().Padding(2.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SNew(STextBlock).Text(NSLOCTEXT("PanoramaCaptureEditor", "MatrixCoefficients", "Matrix Coefficients"))
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f)
            [
                SNew(SComboBox<TSharedPtr<FString>>)
                .OptionsSource(&MatrixOptions.Get())
                .InitiallySelectedItem(ResolveMatrixLabel())
                .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
                {
                    return SNew(STextBlock).Text(FText::FromString(*Item));
                })
                .OnSelectionChanged_Lambda([this, Settings, MatrixOptions](TSharedPtr<FString> Item, ESelectInfo::Type)
                {
                    if (!Item.IsValid())
                    {
                        return;
                    }

                    Settings->DefaultOutput.MatrixCoefficients = (*Item == *(*MatrixOptions)[1]) ? EPanoramaMatrixCoefficients::BT2020NCL : EPanoramaMatrixCoefficients::BT709;
                    Settings->SaveConfig();
                    ApplySettingsToControllers();
                })
                [
                    SNew(STextBlock).Text_Lambda([ResolveMatrixLabel]()
                    {
                        return FText::FromString(*ResolveMatrixLabel());
                    })
                ]
            ]
        ];

    Root->AddSlot().AutoHeight().Padding(2.0f)
        [
            SNew(SCheckBox)
            .IsChecked(Defaults.bEnableSphericalMetadata ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
            .OnCheckStateChanged_Lambda([this, Settings](ECheckBoxState NewState)
            {
                Settings->DefaultOutput.bEnableSphericalMetadata = (NewState == ECheckBoxState::Checked);
                Settings->SaveConfig();
                ApplySettingsToControllers();
            })
            .Content()[SNew(STextBlock).Text(NSLOCTEXT("PanoramaCaptureEditor", "SphericalMetadata", "Write 360° Metadata"))]
        ];

    Root->AddSlot().AutoHeight().Padding(2.0f)
        [
            SNew(SCheckBox)
            .IsChecked(Defaults.bEnableFastStart ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
            .OnCheckStateChanged_Lambda([this, Settings](ECheckBoxState NewState)
            {
                Settings->DefaultOutput.bEnableFastStart = (NewState == ECheckBoxState::Checked);
                Settings->SaveConfig();
                ApplySettingsToControllers();
            })
            .Content()[SNew(STextBlock).Text(NSLOCTEXT("PanoramaCaptureEditor", "FastStart", "Enable Fast Start"))]
        ];

    Root->AddSlot().AutoHeight().Padding(2.0f)
        [
            SNew(SCheckBox)
            .IsChecked(Defaults.bTagHVC1 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
            .OnCheckStateChanged_Lambda([this, Settings](ECheckBoxState NewState)
            {
                Settings->DefaultOutput.bTagHVC1 = (NewState == ECheckBoxState::Checked);
                Settings->SaveConfig();
                ApplySettingsToControllers();
            })
            .Content()[SNew(STextBlock).Text(NSLOCTEXT("PanoramaCaptureEditor", "TagHVC1", "Tag HEVC as hvc1"))]
        ];

    TArray<TSharedPtr<FString>> PolicyOptions;
    PolicyOptions.Add(MakeShared<FString>(TEXT("Drop Oldest")));
    PolicyOptions.Add(MakeShared<FString>(TEXT("Drop Newest")));
    PolicyOptions.Add(MakeShared<FString>(TEXT("Block")));

    auto ResolvePolicyLabel = [&]() -> TSharedPtr<FString>
    {
        switch (Defaults.RingBufferPolicy)
        {
        case ERingBufferOverflowPolicy::DropNewest: return PolicyOptions[1];
        case ERingBufferOverflowPolicy::BlockUntilAvailable: return PolicyOptions[2];
        default: return PolicyOptions[0];
        }
    };

    Root->AddSlot().AutoHeight().Padding(2.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SNew(STextBlock).Text(NSLOCTEXT("PanoramaCaptureEditor", "RingPolicy", "Ring Buffer Policy"))
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f)
            [
                SNew(SComboBox<TSharedPtr<FString>>)
                .OptionsSource(&PolicyOptions)
                .InitiallySelectedItem(ResolvePolicyLabel())
                .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
                {
                    return SNew(STextBlock).Text(FText::FromString(*Item));
                })
                .OnSelectionChanged_Lambda([this, Settings, PolicyOptions](TSharedPtr<FString> Item, ESelectInfo::Type)
                {
                    if (!Item.IsValid())
                    {
                        return;
                    }
                    if (*Item == *PolicyOptions[1])
                    {
                        Settings->DefaultOutput.RingBufferPolicy = ERingBufferOverflowPolicy::DropNewest;
                    }
                    else if (*Item == *PolicyOptions[2])
                    {
                        Settings->DefaultOutput.RingBufferPolicy = ERingBufferOverflowPolicy::BlockUntilAvailable;
                    }
                    else
                    {
                        Settings->DefaultOutput.RingBufferPolicy = ERingBufferOverflowPolicy::DropOldest;
                    }
                    Settings->SaveConfig();
                    ApplySettingsToControllers();
                })
                .Content()
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(*ResolvePolicyLabel()))
                ]
            ]
        ];

    Root->AddSlot().AutoHeight().Padding(2.0f)
        [
            SNew(SCheckBox)
            .IsChecked(Defaults.bEnableAudioDriftCorrection ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
            .OnCheckStateChanged_Lambda([this, Settings](ECheckBoxState NewState)
            {
                Settings->DefaultOutput.bEnableAudioDriftCorrection = (NewState == ECheckBoxState::Checked);
                Settings->SaveConfig();
                ApplySettingsToControllers();
            })
            .Content()[SNew(STextBlock).Text(NSLOCTEXT("PanoramaCaptureEditor", "AudioDriftCorrection", "Audio Drift Correction"))]
        ];

    Root->AddSlot().AutoHeight().Padding(2.0f)
        [MakeLabeledFloat(NSLOCTEXT("PanoramaCaptureEditor", "AudioDriftTolerance", "Drift Tolerance (ms)"), Defaults.AudioDriftToleranceMs, 0.0f, 200.0f, [this, Settings](float NewValue)
        {
            Settings->DefaultOutput.AudioDriftToleranceMs = NewValue;
            Settings->SaveConfig();
            ApplySettingsToControllers();
        })];

    Root->AddSlot().AutoHeight().Padding(2.0f)
        [
            SNew(SCheckBox)
            .IsChecked(Defaults.bAutoMuxNVENC ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
            .OnCheckStateChanged_Lambda([this, Settings](ECheckBoxState NewState)
            {
                Settings->DefaultOutput.bAutoMuxNVENC = (NewState == ECheckBoxState::Checked);
                Settings->SaveConfig();
            })
            .Content()[SNew(STextBlock).Text(NSLOCTEXT("PanoramaCaptureEditor", "MuxToggle", "Auto-mux NVENC output"))]
        ];

    return Root;
}

FReply FPanoramaCaptureEditorModule::HandleStartStopButton()
{
    HandleToggleCapture();
    if (TSharedPtr<SComboButton> Combo = ControlMenuButton.Pin())
    {
        Combo->SetIsOpen(false);
    }
    return FReply::Handled();
}

void FPanoramaCaptureEditorModule::HandleTogglePreviewWindow()
{
    if (PreviewWindow.IsValid())
    {
        PreviewWindow->RequestDestroyWindow();
        PreviewWindow.Reset();
        PreviewWidget.Reset();
        return;
    }

    TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(NSLOCTEXT("PanoramaCaptureEditor", "PreviewWindowTitle", "Panorama Preview"))
        .SupportsMaximize(false)
        .SupportsMinimize(true)
        .SizingRule(ESizingRule::Autosized);

    Window->SetContent(
        SAssignNew(PreviewWidget, SPanoramaPreviewWidget)
        .TextureProvider([this]() { return ResolvePreviewTexture(); })
        .StatusProvider([this]() { return BuildPreviewStatusText(); })
    );

    PreviewWindow = Window;
    Window->SetOnWindowClosed(FOnWindowClosed::CreateLambda([this](const TSharedRef<SWindow>&)
    {
        PreviewWindow.Reset();
        PreviewWidget.Reset();
    }));
    FSlateApplication::Get().AddWindow(Window);
}

bool FPanoramaCaptureEditorModule::IsPreviewWindowOpen() const
{
    return PreviewWindow.IsValid();
}

UTexture2D* FPanoramaCaptureEditorModule::ResolvePreviewTexture() const
{
    UTexture2D* Result = nullptr;
    ForEachController([&Result](UPanoramaCaptureController* Controller)
    {
        if (Result || !Controller)
        {
            return;
        }

        if (UTexture2D* Preview = Controller->GetPreviewTexture())
        {
            Result = Preview;
        }
    });
    return Result;
}

FText FPanoramaCaptureEditorModule::BuildPreviewStatusText() const
{
    return BuildStatusText();
}

void FPanoramaCaptureEditorModule::ApplySettingsToControllers()
{
    UPanoramaCaptureSettings* Settings = GetMutableDefault<UPanoramaCaptureSettings>();
    FCaptureOutputSettings& Defaults = Settings->DefaultOutput;
    ForEachController([&Defaults](UPanoramaCaptureController* Controller)
    {
        if (Controller && !Controller->IsCapturing())
        {
            Controller->OutputSettings = Defaults;
        }
    });
}

void FPanoramaCaptureEditorModule::ForEachController(TFunctionRef<void(UPanoramaCaptureController*)> InFunc)
{
    for (TObjectIterator<UPanoramaCaptureController> It; It; ++It)
    {
        if (It->IsTemplate())
        {
            continue;
        }

        if (UPanoramaCaptureController* Controller = *It)
        {
            if (Controller->GetWorld() && !Controller->GetWorld()->IsPreviewWorld())
            {
                InFunc(Controller);
            }
        }
    }
}

bool FPanoramaCaptureEditorModule::IsAnyControllerCapturing() const
{
    bool bCapturing = false;
    ForEachController([&bCapturing](UPanoramaCaptureController* Controller)
    {
        if (Controller && Controller->IsCapturing())
        {
            bCapturing = true;
        }
    });
    return bCapturing;
}

FText FPanoramaCaptureEditorModule::BuildStatusText() const
{
    int32 ControllerCount = 0;
    int32 CapturingCount = 0;
    int32 BufferedFrames = 0;
    int32 DroppedFrames = 0;
    int32 BlockedFrames = 0;
    bool bHasWarning = false;
    bool bHasPreflightIssue = false;
    FString FirstWarningMessage;

    ForEachController([&](UPanoramaCaptureController* Controller)
    {
        if (!Controller)
        {
            return;
        }

        ++ControllerCount;
        if (Controller->IsCapturing())
        {
            ++CapturingCount;
        }

        BufferedFrames += Controller->GetBufferedFrameCount();
        DroppedFrames += Controller->GetDroppedFrameCount();
        BlockedFrames += Controller->GetBlockedFrameCount();

        if (!bHasWarning)
        {
            const FString Warning = Controller->GetLastWarning();
            if (!Warning.IsEmpty())
            {
                bHasWarning = true;
                FirstWarningMessage = Warning;
            }
        }

        if (!Controller->WasLastPreflightSuccessful())
        {
            bHasPreflightIssue = true;
            if (FirstWarningMessage.IsEmpty())
            {
                const TArray<FString> Messages = Controller->GetPreflightMessages();
                if (Messages.Num() > 0)
                {
                    FirstWarningMessage = Messages[0];
                }
            }
        }
    });

    FString Label;
    if (CapturingCount > 0)
    {
        Label = FString::Printf(TEXT("Recording (%d/%d)"), CapturingCount, FMath::Max(1, ControllerCount));
    }
    else
    {
        Label = TEXT("Idle");
    }

    Label += FString::Printf(TEXT(" | Buffer:%d | Dropped:%d"), BufferedFrames, DroppedFrames);
    if (BlockedFrames > 0)
    {
        Label += FString::Printf(TEXT(" | Blocked:%d"), BlockedFrames);
    }

    if (bHasPreflightIssue)
    {
        Label += TEXT(" | Preflight");
    }

    if (bHasWarning)
    {
        Label += TEXT(" | Warn");
    }

    if (const UPanoramaCaptureSettings* Settings = GetDefault<UPanoramaCaptureSettings>())
    {
        if (!Settings->DefaultOutput.bEnablePreview)
        {
            Label += TEXT(" | Preview Off");
        }
        if (Settings->DefaultOutput.OutputPath == ECaptureOutputPath::NVENCVideo && Settings->DefaultOutput.NVENC.Codec == ENVENCCodec::HEVC)
        {
            Label += TEXT(" | HEVC");
        }
    }

    if (!FirstWarningMessage.IsEmpty())
    {
        Label += FString::Printf(TEXT(" | %s"), *FirstWarningMessage.Left(48));
    }

    return FText::FromString(Label);
}

IMPLEMENT_MODULE(FPanoramaCaptureEditorModule, PanoramaCaptureEditor)
