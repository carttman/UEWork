// Fill out your copyright notice in the Description page of Project Settings.


#include "LobbyGM.h"
#include "Kismet/KismetSystemLibrary.h"
#include "LobbyGS.h"
#include "LobbyPC.h"
#include "Engine/GameInstance.h"
#include "../Web/WebApiSubsystem.h"

void ALobbyGM::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	//UKismetSystemLibrary::PrintString(GetWorld(), TEXT("ALobbyGM::PreLogin Begin"));

	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);

	//UKismetSystemLibrary::PrintString(GetWorld(), TEXT("ALobbyGM::PreLogin End"));
}

APlayerController* ALobbyGM::Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal, const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	//UKismetSystemLibrary::PrintString(GetWorld(), TEXT("ALobbyGM::Login Begin"));

	APlayerController* PC =  Super::Login(NewPlayer, InRemoteRole, Portal, Options, UniqueId, ErrorMessage);

	//UKismetSystemLibrary::PrintString(GetWorld(), TEXT("ALobbyGM::Login End"));

	return PC;
}

void ALobbyGM::PostLogin(APlayerController* NewPlayer)
{
	//UKismetSystemLibrary::PrintString(GetWorld(), TEXT("ALobbyGM::PostLogin Begin"));

	Super::PostLogin(NewPlayer);

	CountConnection();
	//UKismetSystemLibrary::PrintString(GetWorld(), TEXT("ALobbyGM::PostLogin End"));

}

void ALobbyGM::Logout(AController* Exiting)
{
	//UKismetSystemLibrary::PrintString(GetWorld(), TEXT("ALobbyGM::Logout Begin"));
	
	Super::Logout(Exiting);

	CountConnection();

	//UKismetSystemLibrary::PrintString(GetWorld(), TEXT("ALobbyGM::Logout End"));
}

void ALobbyGM::StartPlay()
{
	//UKismetSystemLibrary::PrintString(GetWorld(), TEXT("ALobbyGM::StartPlay Begin"));

	Super::StartPlay();

	//UKismetSystemLibrary::PrintString(GetWorld(), TEXT("ALobbyGM::StartPlay End"));
}

void ALobbyGM::BeginPlay()
{
	Super::BeginPlay();

	// 이 함수가 도는 시점은 리슨 서버가 실제로 떠 있다는 뜻이다.
	// LobbyGM은 서버에만 스폰되므로 권한 검사가 따로 필요 없다.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UWebApiSubsystem* WebApi = GI->GetSubsystem<UWebApiSubsystem>())
		{
			WebApi->RequestRegisterServer();
		}
	}


	GetWorld()->GetTimerManager().SetTimer(
		LeftTimeHandle,
		FTimerDelegate::CreateLambda([this]() {
			CountDownLeftTime();
		}),
		1.0f,
		true,
		0.0f
	);
}

void ALobbyGM::CountConnection()
{
	int Count = GetNumPlayers();
	//for (auto Iter = GetWorld()->GetPlayerControllerIterator(); Iter; ++Iter)
	//{
	//	Count++;
	//}

	ALobbyGS* GS = GetGameState<ALobbyGS>();
	if (GS)
	{
		GS->ConnectionCount = Count;

		//ReplicatedUsing������ C++������ ȣ���� �ȵ�.
		GS->OnRep_ConnectionCount();
	}
}

void ALobbyGM::CountDownLeftTime()
{

	ALobbyGS* GS = GetGameState<ALobbyGS>();
	if (GS)
	{
		GS->LeftTime--;
		GS->LeftTime = FMath::Clamp(GS->LeftTime, 0, 60);

		//ReplicatedUsing������ C++������ ȣ���� �ȵ�.
		GS->OnRep_LeftTime();

		if (GS->LeftTime <= 0)
		{
			StartGame();
		}
	}
}

void ALobbyGM::StopTimer()
{
	GetWorldTimerManager().ClearTimer(
		LeftTimeHandle
	);
}

void ALobbyGM::StartGame()
{
	StopTimer();
	for (auto Iter = GetWorld()->GetPlayerControllerIterator(); Iter; ++Iter)
	{
		ALobbyPC* PC = Cast<ALobbyPC>(*Iter);
		if (PC)
		{
			PC->S2C_ShowLoadingScreen();
		}
	}


	GetWorld()->ServerTravel(TEXT("Lvl_ThirdPerson"));


}
