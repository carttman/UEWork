# 게임서버 IP 등록 / 자동 접속 설계

**상태:** 2026-09-20 구현됨 — 실행 검증은 일부만. [작업기록](서버IP등록-접속-작업기록.md) 참고
**전제 문서:** [docs/작업기록.md](작업기록.md) — 로그인/회원가입이 끝난 지점에서 이어진다
**관련 코드:** `Server/main.py`, `Source/L20260713_Day03/Web/WebApiSubsystem.*`, `Title/TitleWidgetBase.*`, `Lobby/LobbyGM.*`, `DataGameInstanceSubsystem.h`

---

## 1. 무엇을 만드는가

언리얼 리슨 서버(호스트)가 **자기 주소를 웹서버에 등록**하고, 다른 플레이어는 **로그인 응답으로 그 주소를 받아** 서버 IP를 몰라도 접속하게 만든다.

### 지금 (as-is)

| 단계 | 현재 동작 | 코드 |
|---|---|---|
| 호스트 | StartServer → `OpenLevel("Lobby", Listen)` | `TitleWidgetBase.cpp:47` |
| 호스트 | 자기 IP를 아무데도 알리지 않는다 | — |
| 참가자 | **입력칸에 친 IP**로 `OpenLevel(ServerIP)` | `TitleWidgetBase.cpp:64` |

참가자가 호스트의 IP를 **말이나 메신저로 전달받아 직접 타이핑**해야 한다. `ServerIP` 입력칸 하나가 웹서버 주소(`http://{IP}:8080/login`)와 게임서버 주소 두 역할을 동시에 하고 있어, 웹서버와 호스트가 다른 PC면 애초에 동작하지 않는다.

### 바꾼 뒤 (to-be)

```
[호스트 PC]                      [웹서버 PC]                 [참가자 PC]
StartServer 클릭
  → OpenLevel(Lobby, Listen)
  → LobbyGM::BeginPlay
      POST /server/register  ──►  game_server 테이블
                                  ip = 요청 소켓의 출발지 주소
                             ◄──  { result, server_idx }
  ... 10초마다
      POST /server/heartbeat ──►  updated_at 갱신
                                                              로그인 클릭
                                  POST /login           ◄──
                                  살아있는 서버 1건 조회
                                  { ..., server_ip,     ──►  ConnectServer 활성화
                                    server_port }            OpenLevel("192.168.0.10:7777")
  종료 시
      POST /server/unregister ─►  행 삭제 (실패해도 TTL이 정리)
```

**핵심:** 입력칸의 `ServerIP`는 **웹서버 주소 전용**이 되고, 게임서버 주소는 로그인 응답에서만 온다.

---

## 2. 설계 결정과 근거

| | 결정 | 근거 |
|---|---|---|
| A | **IP는 언리얼이 보내지 않고 웹서버가 `request.client.host`로 판정한다** | 호스트가 자기 주소를 알아낼 필요가 없다. `ISocketSubsystem::GetLocalHostAddr`를 쓰면 NIC가 여러 개인 PC(VMware·WSL·Hyper-V 가상 어댑터)에서 엉뚱한 주소를 고른다. 소켓 출발지 주소는 실제로 웹서버까지 도달한 경로의 주소라 틀릴 수가 없다. `Sockets`·`Networking` 모듈 의존성도 안 생긴다. |
| B | **등록 시점은 `ALobbyGM::BeginPlay()`** | 이 함수가 도는 시점은 리슨 서버가 실제로 떠 있다는 증거다. `StartServer()` 클릭 시점에 등록하면 레벨 로드가 실패해도 행이 남는다. `LobbyGM`은 서버에만 존재하므로 클라이언트가 실수로 등록할 일도 없다. |
| C | **하트비트 타이머는 `UWebApiSubsystem`(GameInstance 스코프)에 둔다** | `LobbyGM`은 `ServerTravel(Lvl_ThirdPerson)`에서 파괴된다 (`LobbyGM.cpp:133`). GameMode에 타이머를 두면 게임 시작과 동시에 하트비트가 끊겨 서버가 목록에서 사라진다. GameInstance는 레벨 전환을 넘어 살아남는다. |
| D | **`UNIQUE(ip, port)` + `ON DUPLICATE KEY UPDATE`** | 호스트가 크래시 후 재시작하면 같은 주소로 다시 등록한다. 행이 쌓이지 않고 갱신된다. 등록은 몇 번을 해도 결과가 같다. |
| E | **TTL 30초. 조회는 `updated_at`이 30초 이내인 행만** | 언리얼이 강제 종료되면 `/server/unregister`가 나갈 수 없다. 해제 요청은 **최선 노력**일 뿐이고, 죽은 서버를 실제로 치우는 건 TTL이다. 해제 API만 믿으면 유령 서버에 접속을 시도하게 된다. |
| F | **로그인 응답에 `server_ip`를 얹는다. 별도 `/server/list`를 만들지 않는다** | 요구사항이 "로그인하면 접속한다"이다. 서버 목록 UI가 없는데 목록 API를 먼저 만들 이유가 없다. 여러 서버를 고르게 할 거면 그때 `/server/list`를 추가하고, 그 전까지는 `ORDER BY updated_at DESC LIMIT 1`로 최근 서버 1건만 준다. |
| G | **등록된 서버가 없으면 `server_ip = ""`** | 로그인 자체는 성공이다. `result: false`로 만들면 "로그인 실패"와 "서버 없음"이 섞인다. 빈 문자열이면 클라이언트가 ConnectServer 버튼만 잠근다. |
| H | **인증 실패도 HTTP 200 + `result` 플래그** (기존 계약 유지) | 새 엔드포인트도 같은 규칙을 따른다. 클라이언트의 `HandleAuthResponse` 분기 구조를 그대로 재사용하기 위해서다. |

### 의도적으로 하지 않는 것

- **등록 요청에 인증을 걸지 않는다.** 아무나 `POST /server/register`를 날려 가짜 서버를 등록할 수 있다. 실습 프로젝트 전제로 받아들인 결정이다. 막으려면 로그인 시 토큰을 발급해 등록 요청 헤더에 싣는 구조가 필요한데, 현재 서버에는 세션·토큰 개념 자체가 없다.
- **포트 포워딩·NAT 통과를 다루지 않는다.** 같은 LAN 안에서만 동작한다. 공인 IP 환경은 별도 작업이다.
- **동시 서버 여러 개를 UI로 고르게 하지 않는다.** 결정 F 참고.

---

## 3. DB 스키마

`seul` 데이터베이스에 테이블 하나를 **추가**한다. 기존 `member` 테이블은 건드리지 않는다.

```sql
CREATE TABLE game_server (
    idx         INT AUTO_INCREMENT PRIMARY KEY,
    owner_idx   INT         NOT NULL,                  -- member.idx (호스트)
    ip          VARCHAR(45) NOT NULL,                  -- IPv6까지 담기는 길이
    port        INT         NOT NULL DEFAULT 7777,
    name        VARCHAR(64) NOT NULL DEFAULT '',
    cur_players INT         NOT NULL DEFAULT 0,
    updated_at  DATETIME    NOT NULL DEFAULT CURRENT_TIMESTAMP
                            ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY uk_ip_port (ip, port)
) CHARSET = utf8mb4;
```

`updated_at`의 `ON UPDATE CURRENT_TIMESTAMP` 덕분에 하트비트 UPDATE가 시각을 자동으로 밀어준다. 애플리케이션이 `NOW()`를 따로 넣지 않는다.

`owner_idx`에 FK를 걸지 않는다 — 회원이 지워져도 등록 행은 TTL이 어차피 무효화하고, FK 때문에 삭제가 막히는 쪽이 더 귀찮다.

---

## 4. 서버 API 계약

포트는 기존과 동일하게 **8080** (`Server/run.bat`).

### 4.1 `POST /server/register`

요청:

```json
{ "owner_idx": 1, "port": 7777, "name": "junios room" }
```

`ip`는 **보내지 않는다.** 서버가 `request.client.host`로 채운다 (결정 A).

응답:

```json
{ "result": true, "message": "", "server_idx": 3 }
```

| 상황 | HTTP | result | message |
|---|---|---|---|
| 등록/갱신 성공 | 200 | `true` | `""` |
| `owner_idx`가 0 이하 | 422 | — | Pydantic이 거부 |
| 출발지 주소를 못 읽음 | 200 | `false` | `서버 주소를 확인할 수 없습니다` |

### 4.2 `POST /server/heartbeat`

```json
{ "server_idx": 3, "cur_players": 2 }
```

```json
{ "result": true, "message": "" }
```

행이 이미 없으면 `result: false`, `message: "등록되지 않은 서버입니다"`. 클라이언트는 이걸 받으면 **재등록**한다 (웹서버 재시작 시 복구 경로).

### 4.3 `POST /server/unregister`

```json
{ "server_idx": 3 }
```

행을 삭제한다. 없으면 `result: true` — 지우려던 결과는 이미 달성됐다.

### 4.4 `POST /login` — 응답 확장

요청 본문은 그대로다. 응답에 두 필드가 **추가**된다.

```json
{
  "result": true, "message": "", "idx": 1, "nickname": "junios", "level": 1,
  "server_ip": "192.168.0.10",
  "server_port": 7777
}
```

조회 쿼리:

```sql
SELECT ip, port FROM game_server
 WHERE updated_at > NOW() - INTERVAL 30 SECOND
 ORDER BY updated_at DESC
 LIMIT 1
```

살아있는 서버가 없으면 `server_ip: ""`, `server_port: 0` (결정 G).

> **기존 클라이언트 호환:** 필드를 **추가**만 하므로 이 필드를 안 읽는 코드는 그대로 동작한다. `AuthResponse`에 `server_ip: str = ""`, `server_port: int = 0` 기본값을 준다.

---

## 5. 언리얼 쪽 변경

### 5.1 파일별 책임

| 파일 | 변경 |
|---|---|
| `Web/WebApiSubsystem.h/.cpp` | 등록·하트비트·해제 호출, 하트비트 타이머 소유. HTTP를 아는 유일한 지점이라는 기존 원칙 유지 |
| `DataGameInstanceSubsystem.h` | `GameServerIP`, `GameServerPort`, `ServerIdx` 보관 필드 추가 |
| `Lobby/LobbyGM.cpp` | `BeginPlay`에서 등록 요청 1회 |
| `Title/TitleWidgetBase.cpp` | `ConnectServer`가 입력칸 대신 보관된 게임서버 주소를 쓴다 |

`WebApiSubsystem`=통신 / `DataGameInstanceSubsystem`=보관 / 위젯=화면 이라는 기존 분리를 그대로 지킨다.

### 5.2 `DataGameInstanceSubsystem.h`

```cpp
    // 웹서버 주소 (Title 화면 입력칸). 게임서버 주소가 아니다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
    FString ServerIP;

    // 로그인 응답으로 받은 게임서버 주소
    UPROPERTY(BlueprintReadOnly, Category = "Data")
    FString GameServerIP;

    UPROPERTY(BlueprintReadOnly, Category = "Data")
    int32 GameServerPort = 0;

    // 호스트일 때 웹서버가 발급한 등록 번호. 하트비트/해제에 쓴다.
    UPROPERTY(BlueprintReadOnly, Category = "Data")
    int32 ServerIdx = 0;
```

기존 `ServerIP`의 의미가 "웹서버 주소"로 **좁아진다.** 주석으로 명시한다 — 이 설계에서 가장 혼동하기 쉬운 지점이다.

### 5.3 `WebApiSubsystem` 확장

```cpp
namespace
{
    constexpr int32 WebServerPort     = 8080;
    constexpr int32 GameServerPort    = 7777;  // UE 리슨 서버 기본 포트
    constexpr float HeartbeatInterval = 10.0f; // TTL 30초의 1/3
}

public:
    UPROPERTY(BlueprintAssignable, Category = "WebApi")
    FWebApiResultSignature OnRegisterServerResult;

    void RequestRegisterServer();
    void RequestUnregisterServer();

private:
    void SendHeartbeat();
    FTimerHandle HeartbeatHandle;
```

호출 URL은 `http://{Data->ServerIP}:8080{Path}`로 기존과 동일하게 조립한다. `SendAuthRequest`의 요청 생성·약참조 콜백 부분(`WebApiSubsystem.cpp:29` 이하)을 `SendJsonRequest(Path, JsonObject, Handler)`로 뽑아 네 엔드포인트가 함께 쓴다. **비동기 응답이 도착하기 전에 GameInstance가 정리될 수 있으므로 `TWeakObjectPtr`로 잡는 기존 패턴을 반드시 유지한다.**

타이머는 `GetGameInstance()->GetTimerManager()`에 건다. `GetWorld()->GetTimerManager()`는 레벨 전환에서 타이머가 날아간다 (결정 C).

```cpp
void UWebApiSubsystem::RequestRegisterServer()
{
    // owner_idx = Data->Idx, port = GameServerPort, name = Data->Nickname
    // 성공 콜백에서:
    //   Data->ServerIdx = server_idx;
    //   GetGameInstance()->GetTimerManager().SetTimer(
    //       HeartbeatHandle, this, &UWebApiSubsystem::SendHeartbeat,
    //       HeartbeatInterval, /*bLoop=*/true);
}

void UWebApiSubsystem::Deinitialize()
{
    GetGameInstance()->GetTimerManager().ClearTimer(HeartbeatHandle);
    RequestUnregisterServer();   // 최선 노력. 못 나가도 TTL이 정리한다 (결정 E)
    Super::Deinitialize();
}
```

### 5.4 `LobbyGM::BeginPlay`

```cpp
void ALobbyGM::BeginPlay()
{
    Super::BeginPlay();

    if (UWebApiSubsystem* WebApi = GetGameInstance()->GetSubsystem<UWebApiSubsystem>())
    {
        WebApi->RequestRegisterServer();
    }

    // ... 기존 LeftTime 타이머
}
```

`LobbyGM`은 서버에만 스폰되므로 `HasAuthority()` 검사가 따로 필요 없다. `ServerTravel` 뒤 `Lvl_ThirdPerson`의 GameMode(`AMyGM`)에는 등록 코드를 **넣지 않는다** — 하트비트가 GameInstance에서 계속 돌기 때문에 행은 이미 살아있다.

### 5.5 `TitleWidgetBase`

`ConnectServer()`가 입력칸을 읽던 부분(`TitleWidgetBase.cpp:75`)을 바꾼다.

```cpp
void UTitleWidgetBase::ConnectServer()
{
    if (!IsLoggedIn()) { SetInfoText(TEXT("먼저 로그인해 주세요")); return; }

    UDataGameInstanceSubsystem* Data = /* ... */;
    if (!Data || Data->GameServerIP.IsEmpty())
    {
        SetInfoText(TEXT("접속 가능한 서버가 없습니다"));
        return;
    }

    SaveData();

    const FString Address = FString::Printf(TEXT("%s:%d"),
        *Data->GameServerIP, Data->GameServerPort);

    UGameplayStatics::OpenLevel(GetWorld(), FName(Address), true, TEXT("Key=100"));
}
```

`ProcessLoginResult`에서 버튼을 켤 때도 조건이 갈린다.

| 버튼 | 활성 조건 |
|---|---|
| `StartServerButton` | 로그인 성공 |
| `ConnectServerButton` | 로그인 성공 **그리고** `GameServerIP`가 비어있지 않음 |

`HandleAuthResponse`의 로그인 성공 블록(`WebApiSubsystem.cpp:100` 이하)에서 `server_ip`/`server_port`를 `Data`에 함께 채운다. `ClearLoginState()`에서도 두 필드를 비운다 — 이전 로그인의 서버 주소가 남으면 죽은 서버로 접속을 시도한다.

입력칸 라벨은 `서버 주소` → **`웹서버 주소`** 로 바꾼다 (WBP_Title, 사람이 직접).

---

## 6. 엣지 케이스

| 상황 | 동작 |
|---|---|
| 등록된 서버 없이 로그인 | `server_ip: ""` → ConnectServer 비활성, "접속 가능한 서버가 없습니다" |
| 호스트가 강제 종료 | 해제 요청이 못 나감. 30초 뒤 TTL로 조회에서 빠진다. **그 사이 로그인한 사람은 죽은 서버로 접속을 시도**하고 언리얼 기본 타임아웃 화면을 본다 |
| 웹서버 재시작 | 하트비트가 "등록되지 않은 서버입니다"를 받으면 재등록 |
| 호스트 크래시 후 재시작 | 같은 `(ip, port)`로 `ON DUPLICATE KEY UPDATE` → 행 갱신, 중복 없음 |
| **호스트와 웹서버가 같은 PC** | `request.client.host`가 `127.0.0.1`로 잡힌다. 다른 PC의 참가자는 이 주소로 접속할 수 없다 → 아래 참고 |
| 서버 두 대 동시 등록 | 최근에 하트비트한 쪽 1건만 응답에 실린다. 다른 서버는 UI에서 고를 방법이 없다 (결정 F) |

### 127.0.0.1 문제

한 PC에서 웹서버·호스트·클라이언트를 모두 띄우면 등록 IP가 `127.0.0.1`이 되고, 이건 **같은 PC 안에서만** 유효하다. 세 가지 중 하나를 택한다.

1. **호스트를 웹서버와 다른 PC에서 실행한다** (권장, LAN 실습의 정상 구성)
2. 등록 요청 본문에 `ip` 필드를 선택적으로 허용해 호스트가 덮어쓰게 한다 (결정 A의 예외 경로)
3. 웹서버가 `127.0.0.1`을 받으면 자기 LAN IP로 치환한다 — 웹서버와 호스트가 같은 PC일 때만 맞는 가정이라 권장하지 않는다

이 설계는 **1번을 전제**한다. 2번이 필요해지면 그때 필드를 추가한다.

---

## 7. 구현 순서

각 단계는 그 자리에서 확인 가능한 결과물을 남긴다.

```
1. game_server 테이블 생성
   → 검증: DESC game_server, uk_ip_port 인덱스 확인

2. /server/register + /heartbeat + /unregister 추가
   → 검증: curl로 등록 → SELECT로 ip가 출발지 주소인지 확인
           → 같은 요청 재전송 → 행 수 1 유지, updated_at만 갱신
           → 해제 → 행 0

3. /login 응답에 server_ip, server_port 추가
   → 검증: 등록된 서버 없을 때 "" / 있을 때 주소
           → 31초 기다린 뒤 다시 로그인 → ""로 돌아오는지 (TTL)

4. DataGameInstanceSubsystem 필드 추가 + WebApiSubsystem 응답 파싱
   → 검증: Build.bat 컴파일 성공, PIE 로그인 후 GameServerIP 로그 출력

5. WebApiSubsystem 등록/하트비트/해제 + LobbyGM::BeginPlay 호출
   → 검증: StartServer 후 DB에 행 생성, 10초마다 updated_at 갱신
           → ServerTravel(게임 시작) 후에도 갱신이 계속되는지 (결정 C의 핵심)

6. TitleWidgetBase 접속 경로 교체
   → 검증: 8절 체크리스트
```

> 언리얼 빌드는 `Build.bat` CLI로 확인한다. **에디터가 켜져 있으면 DLL이 잠겨 빌드가 실패**하므로 먼저 닫는다 (작업기록 판정 B·H).

---

## 8. 수동 검증 체크리스트

**구성:** PC-A = 웹서버 + MySQL + 호스트, PC-B = 참가자. (한 PC로 하면 6절의 127.0.0.1 문제에 걸린다)

| # | 조작 | 기대 결과 |
|---|---|---|
| 1 | PC-A에서 `Server/run.bat` | 8080 포트 리슨 |
| 2 | PC-B에서 PC-A의 IP로 로그인 | 닉네임 표시, **ConnectServer는 비활성** (서버 미등록) |
| 3 | PC-A에서 로그인 후 StartServer | Lobby 진입. `SELECT * FROM game_server` → 행 1개, `ip` = PC-A의 LAN IP |
| 4 | 10초 뒤 같은 SELECT | `updated_at`이 갱신됨 |
| 5 | PC-B에서 다시 로그인 | **ConnectServer 활성화** |
| 6 | PC-B에서 ConnectServer 클릭 | PC-A의 Lobby에 합류, 접속 인원 2 |
| 7 | 로비 타이머 만료 → 게임 시작 | `ServerTravel` 후에도 `updated_at`이 계속 갱신 |
| 8 | PC-A 언리얼 정상 종료 | 행 삭제 |
| 9 | PC-A 강제 종료(작업관리자) 후 31초 | 행은 남아있지만 로그인 응답의 `server_ip`는 `""` |

---

## 9. 미결 사항

- **등록 API에 인증이 없다.** 2절 "의도적으로 하지 않는 것" 참고. 실습 범위를 넘어가면 토큰 발급이 선행되어야 한다.
- **게임 포트 7777 하드코딩.** 한 PC에서 리슨 서버를 둘 띄우면 충돌한다. 실제 포트를 알아내려면 `GetWorld()->URL.Port`를 읽는 경로가 있지만, 리슨 서버 시작 직후에 신뢰할 수 있는 값인지 확인이 필요하다.
- **죽은 행이 DB에 계속 쌓인다.** 조회에서 TTL로 걸러낼 뿐 삭제하지 않는다. 실습 규모에서는 문제가 없고, 필요해지면 정리 쿼리를 `/login`에 곁들이거나 별도 작업으로 뺀다.
- **`LobbyWidgetBase.h`의 `meta = (WidgetBind)` 오타 6개** — 이번 범위 밖이다 (작업기록 7절에서 이어지는 미결 사항).
