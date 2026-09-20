// Fill out your copyright notice in the Description page of Project Settings.


#include "TitleWidgetBase.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Kismet/GameplayStatics.h"
#include "../DataGameInstanceSubsystem.h"
#include "../Web/WebApiSubsystem.h"

void UTitleWidgetBase::NativeConstruct()
{
	Super::NativeConstruct();


	StartServerButton = Cast<UButton>(GetWidgetFromName(TEXT("StartServerButton")));
	if (StartServerButton)
	{
		StartServerButton->OnClicked.AddDynamic(this, &UTitleWidgetBase::StartServer);
		StartServerButton->SetIsEnabled(false);
	}

	if (ConnectServerButton)
	{
		ConnectServerButton->OnClicked.AddDynamic(this, &UTitleWidgetBase::ConnectServer);
		ConnectServerButton->SetIsEnabled(false);
	}

	if (LoginButton)
	{
		LoginButton->OnClicked.AddDynamic(this, &UTitleWidgetBase::Login);
	}

	if (SignUpButton)
	{
		SignUpButton->OnClicked.AddDynamic(this, &UTitleWidgetBase::SignUp);
	}

	if (UWebApiSubsystem* WebApi = GetWebApi())
	{
		WebApi->OnLoginResult.AddUniqueDynamic(this, &UTitleWidgetBase::ProcessLoginResult);
		WebApi->OnSignUpResult.AddUniqueDynamic(this, &UTitleWidgetBase::ProcessSignUpResult);
	}
}

void UTitleWidgetBase::StartServer()
{
	if (!IsLoggedIn())
	{
		SetInfoText(TEXT("먼저 로그인해 주세요"));
		return;
	}

	SaveData();

	UGameplayStatics::OpenLevel(GetWorld(),
		TEXT("Lobby"),
		true,
		TEXT("Listen")
	);
}

void UTitleWidgetBase::ConnectServer()
{
	if (!IsLoggedIn())
	{
		SetInfoText(TEXT("먼저 로그인해 주세요"));
		return;
	}

	UGameInstance* GI = GetGameInstance();
	UDataGameInstanceSubsystem* Data = GI ? GI->GetSubsystem<UDataGameInstanceSubsystem>() : nullptr;
	if (!Data || Data->GameServerIP.IsEmpty())
	{
		SetInfoText(TEXT("접속 가능한 서버가 없습니다"));
		return;
	}

	SaveData();

	// 입력칸은 웹서버 주소다. 게임서버 주소는 로그인 응답에서만 온다.
	const FString Address = FString::Printf(TEXT("%s:%d"),
		*Data->GameServerIP, Data->GameServerPort);

	UGameplayStatics::OpenLevel(GetWorld(),
		FName(Address),
		true,
		TEXT("Key=100")
	);
}

void UTitleWidgetBase::SaveData()
{
	UGameInstance* GI = UGameplayStatics::GetGameInstance(GetWorld());
	if (GI)
	{
		UDataGameInstanceSubsystem* MySubSystem = GI->GetSubsystem<UDataGameInstanceSubsystem>();
		if (MySubSystem)
		{
			MySubSystem->UserID = UserID->GetText().ToString();
			MySubSystem->Password = Password->GetText().ToString();
			MySubSystem->ServerIP = ServerIP->GetText().ToString();
		}
	}
}

void UTitleWidgetBase::Login()
{
	if (!ValidateInput())
	{
		return;
	}

	if (bRequestInFlight)
	{
		return;
	}

	ClearLoginState();

	SaveData();

	if (UWebApiSubsystem* WebApi = GetWebApi())
	{
		SetInfoText(TEXT("로그인 중..."));
		WebApi->RequestLogin(ServerIP->GetText().ToString(),
			UserID->GetText().ToString(),
			Password->GetText().ToString());
		bRequestInFlight = true;
	}
}

void UTitleWidgetBase::SignUp()
{
	if (!ValidateInput())
	{
		return;
	}

	if (bRequestInFlight)
	{
		return;
	}

	ClearLoginState();

	SaveData();

	if (UWebApiSubsystem* WebApi = GetWebApi())
	{
		SetInfoText(TEXT("가입 중..."));
		WebApi->RequestSignUp(ServerIP->GetText().ToString(),
			UserID->GetText().ToString(),
			Password->GetText().ToString());
		bRequestInFlight = true;
	}
}

void UTitleWidgetBase::ProcessLoginResult(const bool bInSuccess, const FString& InMessage)
{
	bRequestInFlight = false;

	if (!bInSuccess)
	{
		SetInfoText(InMessage);
		return;
	}

	UGameInstance* GI = GetGameInstance();
	UDataGameInstanceSubsystem* Data = GI ? GI->GetSubsystem<UDataGameInstanceSubsystem>() : nullptr;
	if (Data)
	{
		SetInfoText(FString::Printf(TEXT("%s (Lv.%d)"), *Data->Nickname, Data->Level));
	}

	if (StartServerButton)
	{
		StartServerButton->SetIsEnabled(true);
	}

	// 등록된 서버가 있을 때만 접속 버튼을 연다.
	if (ConnectServerButton)
	{
		ConnectServerButton->SetIsEnabled(Data && !Data->GameServerIP.IsEmpty());
	}
}

void UTitleWidgetBase::ProcessSignUpResult(const bool bInSuccess, const FString& InMessage)
{
	bRequestInFlight = false;

	SetInfoText(bInSuccess ? TEXT("가입이 완료되었습니다. 로그인해 주세요.") : InMessage);
}

UWebApiSubsystem* UTitleWidgetBase::GetWebApi() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UWebApiSubsystem>() : nullptr;
}

bool UTitleWidgetBase::IsLoggedIn() const
{
	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return false;
	}

	UDataGameInstanceSubsystem* Data = GI->GetSubsystem<UDataGameInstanceSubsystem>();
	return Data && Data->bLoggedIn;
}

void UTitleWidgetBase::ClearLoginState()
{
	UGameInstance* GI = GetGameInstance();
	UDataGameInstanceSubsystem* Data = GI ? GI->GetSubsystem<UDataGameInstanceSubsystem>() : nullptr;
	if (Data)
	{
		Data->bLoggedIn = false;
		Data->Idx = 0;
		Data->Nickname.Empty();
		Data->Level = 0;

		// 남겨두면 죽은 서버로 접속을 시도한다.
		Data->GameServerIP.Empty();
		Data->GameServerPort = 0;
	}

	if (StartServerButton)
	{
		StartServerButton->SetIsEnabled(false);
	}

	if (ConnectServerButton)
	{
		ConnectServerButton->SetIsEnabled(false);
	}
}

void UTitleWidgetBase::SetInfoText(const FString& InMessage)
{
	if (InfoText)
	{
		InfoText->SetText(FText::FromString(InMessage));
	}
}

bool UTitleWidgetBase::ValidateInput()
{
	if (!UserID || !Password || !ServerIP)
	{
		return false;
	}

	if (UserID->GetText().IsEmptyOrWhitespace() || Password->GetText().IsEmptyOrWhitespace())
	{
		SetInfoText(TEXT("아이디와 비밀번호를 입력해 주세요"));
		return false;
	}

	if (ServerIP->GetText().IsEmptyOrWhitespace())
	{
		SetInfoText(TEXT("웹서버 주소를 입력해 주세요"));
		return false;
	}

	UserID->SetText(UserID->GetText().TrimPrecedingAndTrailing(UserID->GetText()));
	Password->SetText(Password->GetText().TrimPrecedingAndTrailing(Password->GetText()));
	ServerIP->SetText(ServerIP->GetText().TrimPrecedingAndTrailing(ServerIP->GetText()));

	return true;
}
