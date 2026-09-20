// Fill out your copyright notice in the Description page of Project Settings.


#include "WebApiSubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "TimerManager.h"
#include "../DataGameInstanceSubsystem.h"

namespace
{
	constexpr int32 WebServerPort = 8080;

	// UE 리슨 서버 기본 포트.
	constexpr int32 GameServerPort = 7777;

	// 웹서버 TTL 30초의 1/3. 한두 번 놓쳐도 만료되지 않는다.
	constexpr float HeartbeatInterval = 10.0f;
}

void UWebApiSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		GI->GetTimerManager().ClearTimer(HeartbeatHandle);
	}

	RequestUnregisterServer();

	Super::Deinitialize();
}

void UWebApiSubsystem::RequestLogin(const FString& InServerIP, const FString& InUserID, const FString& InPassword)
{
	SendAuthRequest(InServerIP, TEXT("/login"), InUserID, InPassword, OnLoginResult, true);
}

void UWebApiSubsystem::RequestSignUp(const FString& InServerIP, const FString& InUserID, const FString& InPassword)
{
	SendAuthRequest(InServerIP, TEXT("/signup"), InUserID, InPassword, OnSignUpResult, false);
}

void UWebApiSubsystem::RequestRegisterServer()
{
	UDataGameInstanceSubsystem* Data = GetData();
	if (!Data || Data->ServerIP.IsEmpty())
	{
		return;
	}

	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetNumberField(TEXT("owner_idx"), Data->Idx);
	JsonObject->SetNumberField(TEXT("port"), GameServerPort);
	JsonObject->SetStringField(TEXT("name"), Data->Nickname);

	SendJsonRequest(Data->ServerIP, TEXT("/server/register"), JsonObject,
		[this](FHttpResponsePtr InResponse, bool bInConnectedSuccessfully)
		{
			HandleRegisterServerResponse(InResponse, bInConnectedSuccessfully);
		});
}

void UWebApiSubsystem::RequestUnregisterServer()
{
	UDataGameInstanceSubsystem* Data = GetData();
	if (!Data || Data->ServerIdx <= 0 || Data->ServerIP.IsEmpty())
	{
		return;
	}

	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetNumberField(TEXT("server_idx"), Data->ServerIdx);

	SendJsonRequest(Data->ServerIP, TEXT("/server/unregister"), JsonObject,
		[](FHttpResponsePtr, bool) {});

	Data->ServerIdx = 0;
}

void UWebApiSubsystem::SendHeartbeat()
{
	UDataGameInstanceSubsystem* Data = GetData();
	if (!Data || Data->ServerIdx <= 0)
	{
		return;
	}

	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetNumberField(TEXT("server_idx"), Data->ServerIdx);
	JsonObject->SetNumberField(TEXT("cur_players"), GetCurrentPlayerCount());

	SendJsonRequest(Data->ServerIP, TEXT("/server/heartbeat"), JsonObject,
		[this](FHttpResponsePtr InResponse, bool bInConnectedSuccessfully)
		{
			TSharedPtr<FJsonObject> Json;
			FString Error;
			if (!ParseResponse(InResponse, bInConnectedSuccessfully, Json, Error))
			{
				// 웹서버가 재시작해 행을 잃었을 수 있으므로 다시 등록한다.
				UE_LOG(LogTemp, Warning, TEXT("Heartbeat failed: %s"), *Error);
				RequestRegisterServer();
			}
		});
}

void UWebApiSubsystem::SendAuthRequest(const FString& InServerIP, const FString& InPath,
	const FString& InUserID, const FString& InPassword,
	FWebApiResultSignature& InDelegate, const bool bInIsLogin)
{
	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetStringField(TEXT("user_id"), InUserID);
	JsonObject->SetStringField(TEXT("passwd"), InPassword);

	FWebApiResultSignature* DelegatePtr = &InDelegate;

	SendJsonRequest(InServerIP, InPath, JsonObject,
		[this, DelegatePtr, bInIsLogin](FHttpResponsePtr InResponse, bool bInConnectedSuccessfully)
		{
			HandleAuthResponse(InResponse, bInConnectedSuccessfully, *DelegatePtr, bInIsLogin);
		});
}

void UWebApiSubsystem::SendJsonRequest(const FString& InServerIP, const FString& InPath,
	const TSharedRef<FJsonObject>& InBody, TFunction<void(FHttpResponsePtr, bool)> InHandler)
{
	FString Body;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(InBody, Writer);

	const FString Url = FString::Printf(TEXT("http://%s:%d%s"), *InServerIP, WebServerPort, *InPath);
	UE_LOG(LogTemp, Warning, TEXT("%s"), *Url);

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(Body);

	// 응답이 도착하기 전에 GameInstance가 정리될 수 있으므로 약참조로 잡는다.
	// 핸들러가 this를 캡처해도 여기서 막히므로 안전하다.
	TWeakObjectPtr<UWebApiSubsystem> WeakThis(this);

	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, InHandler](FHttpRequestPtr, FHttpResponsePtr InResponse, bool bInConnectedSuccessfully)
		{
			if (!WeakThis.IsValid())
			{
				return;
			}

			InHandler(InResponse, bInConnectedSuccessfully);
		});

	Request->ProcessRequest();
}

bool UWebApiSubsystem::ParseResponse(FHttpResponsePtr InResponse, const bool bInConnectedSuccessfully,
	TSharedPtr<FJsonObject>& OutJson, FString& OutError) const
{
	if (!bInConnectedSuccessfully || !InResponse.IsValid())
	{
		OutError = TEXT("서버에 연결할 수 없습니다");
		return false;
	}

	const int32 ResponseCode = InResponse->GetResponseCode();
	if (ResponseCode != 200)
	{
		OutError = FString::Printf(TEXT("요청을 처리할 수 없습니다 (코드 %d)"), ResponseCode);
		return false;
	}

	const FString ResponseBody = InResponse->GetContentAsString();

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, OutJson) || !OutJson.IsValid())
	{
		OutError = TEXT("응답을 해석할 수 없습니다");
		return false;
	}

	if (!OutJson->GetBoolField(TEXT("result")))
	{
		OutError = OutJson->GetStringField(TEXT("message"));
		return false;
	}

	return true;
}

void UWebApiSubsystem::HandleAuthResponse(FHttpResponsePtr InResponse, const bool bInConnectedSuccessfully,
	FWebApiResultSignature& InDelegate, const bool bInIsLogin)
{
	TSharedPtr<FJsonObject> JsonObject;
	FString Error;
	if (!ParseResponse(InResponse, bInConnectedSuccessfully, JsonObject, Error))
	{
		InDelegate.Broadcast(false, Error);
		return;
	}

	if (bInIsLogin)
	{
		UDataGameInstanceSubsystem* Data = GetData();
		if (Data)
		{
			Data->Idx = JsonObject->GetIntegerField(TEXT("idx"));
			Data->Nickname = JsonObject->GetStringField(TEXT("nickname"));
			Data->Level = JsonObject->GetIntegerField(TEXT("level"));
			Data->bLoggedIn = true;

			// 빈 문자열이면 접속 가능한 서버가 없다는 뜻이다.
			Data->GameServerIP = JsonObject->GetStringField(TEXT("server_ip"));
			Data->GameServerPort = JsonObject->GetIntegerField(TEXT("server_port"));
		}
	}

	InDelegate.Broadcast(true, TEXT(""));
}

void UWebApiSubsystem::HandleRegisterServerResponse(FHttpResponsePtr InResponse, const bool bInConnectedSuccessfully)
{
	TSharedPtr<FJsonObject> JsonObject;
	FString Error;
	if (!ParseResponse(InResponse, bInConnectedSuccessfully, JsonObject, Error))
	{
		OnRegisterServerResult.Broadcast(false, Error);
		return;
	}

	UDataGameInstanceSubsystem* Data = GetData();
	if (Data)
	{
		Data->ServerIdx = JsonObject->GetIntegerField(TEXT("server_idx"));
	}

	// ServerTravel에서 GameMode가 파괴되어도 살아남도록 GameInstance 타이머를 쓴다.
	if (UGameInstance* GI = GetGameInstance())
	{
		GI->GetTimerManager().SetTimer(HeartbeatHandle, this,
			&UWebApiSubsystem::SendHeartbeat, HeartbeatInterval, true);
	}

	OnRegisterServerResult.Broadcast(true, TEXT(""));
}

UDataGameInstanceSubsystem* UWebApiSubsystem::GetData() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UDataGameInstanceSubsystem>() : nullptr;
}

int32 UWebApiSubsystem::GetCurrentPlayerCount() const
{
	UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	AGameModeBase* GameMode = World ? World->GetAuthGameMode() : nullptr;

	return GameMode ? GameMode->GetNumPlayers() : 0;
}
