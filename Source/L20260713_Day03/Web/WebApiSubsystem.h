// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/IHttpRequest.h"
#include "WebApiSubsystem.generated.h"

class FJsonObject;
class UDataGameInstanceSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FWebApiResultSignature, const bool, bInSuccess, const FString&, InMessage);

/**
 * 웹서버와의 HTTP 통신을 전담한다. 결과는 델리게이트로만 알린다.
 */
UCLASS()
class L20260713_DAY03_API UWebApiSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:

	virtual void Deinitialize() override;

	UPROPERTY(BlueprintAssignable, Category = "WebApi")
	FWebApiResultSignature OnLoginResult;

	UPROPERTY(BlueprintAssignable, Category = "WebApi")
	FWebApiResultSignature OnSignUpResult;

	UPROPERTY(BlueprintAssignable, Category = "WebApi")
	FWebApiResultSignature OnRegisterServerResult;

	void RequestLogin(const FString& InServerIP, const FString& InUserID, const FString& InPassword);

	void RequestSignUp(const FString& InServerIP, const FString& InUserID, const FString& InPassword);

	/** 리슨 서버가 자기 주소를 웹서버에 등록한다. IP는 웹서버가 판정하므로 보내지 않는다. */
	void RequestRegisterServer();

	/** 등록을 해제한다. 최선 노력일 뿐이고, 실제 정리는 웹서버의 TTL이 한다. */
	void RequestUnregisterServer();

private:

	void SendAuthRequest(const FString& InServerIP, const FString& InPath,
		const FString& InUserID, const FString& InPassword,
		FWebApiResultSignature& InDelegate, const bool bInIsLogin);

	void HandleAuthResponse(FHttpResponsePtr InResponse, const bool bInConnectedSuccessfully,
		FWebApiResultSignature& InDelegate, const bool bInIsLogin);

	void HandleRegisterServerResponse(FHttpResponsePtr InResponse, const bool bInConnectedSuccessfully);

	void SendHeartbeat();

	void SendJsonRequest(const FString& InServerIP, const FString& InPath,
		const TSharedRef<FJsonObject>& InBody,
		TFunction<void(FHttpResponsePtr, bool)> InHandler);

	/** 응답을 검사하고 성공이면 JSON을 돌려준다. 실패면 표시할 문구를 OutError에 담는다. */
	bool ParseResponse(FHttpResponsePtr InResponse, const bool bInConnectedSuccessfully,
		TSharedPtr<FJsonObject>& OutJson, FString& OutError) const;

	UDataGameInstanceSubsystem* GetData() const;

	int32 GetCurrentPlayerCount() const;

	FTimerHandle HeartbeatHandle;
};
