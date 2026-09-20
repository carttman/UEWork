// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DataGameInstanceSubsystem.generated.h"

/**
 * 
 */
UCLASS()
class L20260713_DAY03_API UDataGameInstanceSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
	FString UserID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
	FString Password;

	// 웹서버 주소 (Title 화면 입력칸). 게임서버 주소가 아니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
	FString ServerIP;

	// 로그인 응답으로 받은 게임서버 주소. 비어 있으면 접속 가능한 서버가 없다.
	UPROPERTY(BlueprintReadOnly, Category = "Data")
	FString GameServerIP;

	UPROPERTY(BlueprintReadOnly, Category = "Data")
	int32 GameServerPort = 0;

	// 호스트일 때 웹서버가 발급한 등록 번호. 하트비트/해제에 쓴다.
	UPROPERTY(BlueprintReadOnly, Category = "Data")
	int32 ServerIdx = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Data")
	bool bLoggedIn = false;

	UPROPERTY(BlueprintReadOnly, Category = "Data")
	int32 Idx = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Data")
	FString Nickname;

	UPROPERTY(BlueprintReadOnly, Category = "Data")
	int32 Level = 0;

};
