# 게임서버 IP 등록 / 자동 접속 작업 기록

**설계서:** [docs/서버IP등록-접속.md](서버IP등록-접속.md)
**선행 작업:** [docs/작업기록.md](작업기록.md) — 로그인/회원가입
**작업일:** 2026-09-20

---

## 1. 무엇이 바뀌었나

참가자가 호스트의 IP를 직접 타이핑하던 것을, **호스트가 웹서버에 자기 주소를 등록하고 참가자는 로그인 응답으로 받는** 구조로 바꿨다.

| | 전 | 후 |
|---|---|---|
| 호스트 IP 전달 | 사람이 말로 전달 → 참가자가 타이핑 | `POST /server/register` → `POST /login` 응답 |
| `ServerIP` 입력칸 | 웹서버 주소 **겸** 게임서버 주소 | **웹서버 주소 전용** |
| 접속 주소 | 입력칸 텍스트 | `Data->GameServerIP:GameServerPort` |
| ConnectServer 활성 조건 | 로그인 성공 | 로그인 성공 **AND** 등록된 서버 존재 |

---

## 2. 변경 파일

### 서버

| 파일 | 내용 |
|---|---|
| `Server/schema.sql` | **신규.** `game_server` 테이블 DDL |
| `Server/main.py` | `/server/register`, `/server/heartbeat`, `/server/unregister` 추가. `AuthResponse`에 `server_ip`·`server_port` 추가. `/login`이 살아있는 서버 1건을 함께 조회 |

`Server/db.py`는 건드리지 않았다.

### 언리얼

| 파일 | 내용 |
|---|---|
| `DataGameInstanceSubsystem.h` | `GameServerIP`, `GameServerPort`, `ServerIdx` 추가. 기존 `ServerIP`에 "웹서버 주소"라는 주석 |
| `Web/WebApiSubsystem.h/.cpp` | `RequestRegisterServer`, `RequestUnregisterServer`, `SendHeartbeat`, `Deinitialize` 추가. 요청 전송과 응답 파싱을 공통 함수로 추출 |
| `Lobby/LobbyGM.cpp` | `BeginPlay`에서 등록 요청 1회 |
| `Title/TitleWidgetBase.cpp` | `ConnectServer`가 입력칸 대신 `GameServerIP`를 쓴다. 버튼 활성 조건, 상태 초기화, 안내 문구 |

`.Build.cs`는 변경 없다 — 이번 작업에 새 모듈 의존성이 생기지 않았다 (설계 결정 A의 이득).

---

## 3. 구현 중 내린 판정

설계서에 없던, 코드를 쓰면서 드러난 결정들이다.

| | 판정 | 근거 |
|---|---|---|
| J | `ON DUPLICATE KEY UPDATE`에 **`updated_at = CURRENT_TIMESTAMP`를 명시**한다 | `ON UPDATE CURRENT_TIMESTAMP` 컬럼은 **행의 값이 실제로 바뀔 때만** 발동한다. 같은 호스트가 같은 이름으로 재등록하면 값이 전부 동일해 MySQL이 행을 건드리지 않고, 그러면 `updated_at`이 과거에 머물러 **재등록했는데도 TTL 만료 상태**가 된다. 명시 대입이 이 구멍을 막는다. |
| K | 등록 후 `cur.lastrowid` 대신 **`SELECT idx`로 다시 읽는다** | `ON DUPLICATE KEY UPDATE`가 탄 경우 `lastrowid`가 0이거나 엉뚱한 값이 된다. 클라이언트는 이 값을 하트비트 키로 쓰므로 틀리면 조용히 남의 행을 갱신한다. |
| L | 하트비트는 `UPDATE`의 affected rows로 판정하지 않고 **`SELECT`를 먼저 한다** | MySQL의 affected rows는 "행이 없음"과 "값이 그대로임"을 같은 0으로 돌려준다. 이걸로 판정하면 정상 상황에서 "등록되지 않은 서버입니다"가 나가고 클라이언트가 재등록 루프에 빠질 수 있다. 쿼리 하나를 더 쓰는 대신 의미가 분명해진다. |
| M | `SendJsonRequest` / `ParseResponse`를 **추출하고 기존 인증 경로를 그 위에 다시 얹었다** | 엔드포인트가 2개에서 5개로 늘었다. 요청 생성·`TWeakObjectPtr` 가드·응답 검사를 네 번 복사하면 그중 하나에서 가드를 빠뜨린다. 비동기 콜백 수명이 걸린 코드라 복사가 특히 위험하다. 기존 `SendAuthRequest`/`HandleAuthResponse`의 **동작은 그대로**고 내부만 공통 함수를 부른다. |
| N | 하트비트 타이머는 **등록 성공 콜백에서** 건다 | `RequestRegisterServer` 직후에 걸면 등록이 실패해도 타이머가 돈다. 성공 응답에서 `ServerIdx`를 받은 뒤에 시작해야 하트비트가 유효한 키를 갖는다. |
| O | 하트비트 실패 시 **연결 실패와 `result:false`를 구분하지 않고 재등록**한다 | 둘 다 "웹서버가 내 행을 모른다"로 수렴한다. 연결 실패 상황에서 재등록도 실패하지만 해로울 게 없고, 10초 뒤 다시 시도한다. 구분하면 분기만 늘고 동작은 같다. |

---

## 4. 검증 결과

### 언리얼 — 실제 컴파일로 검증됨

`Build.bat`(UE 5.8) CLI. 에디터는 미실행 상태였다.

```
[27/31] Compile [x64] WebApiSubsystem.cpp
[30/31] Link   [x64] UnrealEditor-L20260713_Day03.dll
Result: Succeeded
Total execution time: 66.87 seconds
```

- 에러 0, 경고 0
- `WebApiSubsystem.cpp`, `LobbyGM.cpp`, `TitleWidgetBase.cpp`, `DataGameInstanceSubsystem.cpp` 모두 개별 컴파일 후 링크까지 통과
- 어댑티브 유니티 빌드라 변경 파일이 각각 따로 컴파일됐다 — 헤더만 통과하고 넘어간 게 아니다

### 서버 — 문법만 검증됨

**이 PC에는 Python도 MySQL도 설치되어 있지 않다.** (`python` → Windows Store 스텁, `exit 9009`. MySQL 서비스·설치 디렉터리 모두 없음)

엔진에 번들된 Python 3.11.8로 문법 검사만 했다.

```
python -m py_compile Server/main.py Server/db.py   → exit 0
```

**검증되지 않은 것:**

- FastAPI 라우트가 실제로 뜨는지
- SQL이 MySQL에서 실행되는지 (`INTERVAL %s SECOND` 파라미터 바인딩 포함)
- `request.client.host`가 기대한 주소를 주는지
- 클라이언트↔서버 실제 왕복

즉 **서버 코드는 읽어서 맞다고 판단한 수준이고, 돌려본 적이 없다.** 6절 절차를 사람이 직접 밟아야 한다.

---

## 5. 흐름 정리

```
호스트                          웹서버                        참가자
─────                          ─────                        ─────
로그인                          /login
StartServer
  OpenLevel(Lobby, Listen)
  LobbyGM::BeginPlay
    RequestRegisterServer   →  /server/register
                               ip = request.client.host
                               INSERT ... ON DUPLICATE KEY UPDATE
                            ←  { server_idx }
    Data->ServerIdx 저장
    GameInstance 타이머 시작
  10초마다
    SendHeartbeat           →  /server/heartbeat
                               SELECT 존재 확인 → UPDATE updated_at
  (ServerTravel 후에도 계속 — 타이머가 GameInstance에 있다)

                                                            로그인
                               /login                    ←
                               SELECT ... updated_at > NOW()-30s
                               ORDER BY updated_at DESC LIMIT 1
                               { ..., server_ip, server_port }  →
                                                            ConnectServer 활성
                                                            OpenLevel("IP:7777")
종료
  Deinitialize
    ClearTimer
    RequestUnregisterServer →  DELETE   (못 나가도 TTL이 정리)
```

---

## 6. 사람이 직접 할 일

### 6.1 환경 준비 (이 PC 기준 전부 미설치)

1. **Python 3.12 설치** — Store 스텁이 아닌 python.org 설치본
2. **MySQL 8 설치**, `seul` 데이터베이스와 `member` 테이블 준비
3. `Server/db.py`의 접속 정보를 이 PC 값으로 맞춘다

### 6.2 테이블 생성

```bash
mysql -u root -p seul < Server/schema.sql
```

`CREATE TABLE IF NOT EXISTS`라 여러 번 실행해도 안전하다. 확인:

```sql
SHOW INDEX FROM game_server WHERE Key_name = 'uk_ip_port';
```

`Non_unique`가 `0`이어야 한다. 이 인덱스가 없으면 `ON DUPLICATE KEY UPDATE`가 동작하지 않고 재등록마다 행이 쌓인다.

### 6.3 가상환경과 서버 실행

```bash
python -m venv Server/.venv
Server/.venv/Scripts/python.exe -m pip install -r Server/requirements.txt
Server/run.bat
```

`http://127.0.0.1:8080/docs`에 `/server/register`, `/server/heartbeat`, `/server/unregister` 세 개가 보여야 한다. **4절에서 검증하지 못한 부분이 여기서 처음 드러난다.**

### 6.4 서버 단독 확인 (언리얼 없이)

```bash
curl -X POST http://127.0.0.1:8080/server/register ^
  -H "Content-Type: application/json" ^
  -d "{\"owner_idx\":1,\"port\":7777,\"name\":\"test\"}"
```

| 확인 | 기대 |
|---|---|
| 응답 | `{"result":true,"message":"","server_idx":N}` |
| `SELECT * FROM game_server` | 행 1개, `ip`가 `127.0.0.1` |
| 같은 요청 재전송 | **행 수 1 유지**, `server_idx` 동일, `updated_at` 갱신 (판정 J·K) |
| `/login` | `server_ip`가 채워져서 온다 |
| 31초 뒤 `/login` | `server_ip`가 `""` (TTL) |
| `/server/unregister` | 행 0 |

### 6.5 위젯 (WBP_Title)

이번 작업으로 **새로 배치할 위젯은 없다.** 기존 위젯 그대로 동작한다.

다만 `ServerIP` 입력칸의 **라벨 텍스트**를 `서버 주소` → `웹서버 주소`로 바꾸는 게 좋다. 코드의 안내 문구는 이미 "웹서버 주소를 입력해 주세요"로 바꿨다.

### 6.6 통합 검증

설계서 [8절 체크리스트](서버IP등록-접속.md#8-수동-검증-체크리스트) 9개 항목. **PC 두 대가 필요하다** — 한 대에서 다 돌리면 등록 IP가 `127.0.0.1`이 되어 6번 항목(다른 PC에서 접속)이 원리상 실패한다.

특히 7번(`ServerTravel` 후에도 `updated_at` 갱신)은 판정 N과 설계 결정 C가 실제로 맞았는지 확인하는 항목이다. 여기서 갱신이 멈추면 타이머가 GameMode에 묶인 것이다.

---

## 7. 미결 사항

- **서버 코드 실행 검증 미완** — 4절. 6.3~6.4가 사실상 나머지 절반의 검증이다.
- **`.gitignore`에 `Server/.venv/`와 `__pycache__/`가 없다.** 이 저장소의 `.gitignore`는 언리얼 기본 템플릿 그대로다. 가상환경을 만들면 수천 개 파일이 추적 대상이 된다. 이번 변경이 만든 문제가 아니라 손대지 않았지만, 6.3 전에 두 줄을 추가하는 게 좋다.
- **게임 포트 7777 하드코딩** (`WebApiSubsystem.cpp`의 `GameServerPort`) — 한 PC에서 리슨 서버를 둘 띄우면 충돌한다.
- **등록 API에 인증이 없다** — 설계서 2절. 아무나 가짜 서버를 등록할 수 있다.
- **죽은 행이 DB에 쌓인다** — 조회에서 TTL로 거를 뿐 삭제하지 않는다.
- **`Deinitialize`의 해제 요청은 나가지 못할 가능성이 크다.** 종료 중에 HTTP 모듈이 요청을 플러시할 보장이 없다. 설계 단계부터 최선 노력으로 잡았고, 실제 정리는 TTL이 한다. 6.6에서 8번 항목(정상 종료 시 행 삭제)이 실패해도 9번이 통과하면 설계대로다.
- **`LobbyWidgetBase.h`의 `meta = (WidgetBind)` 오타 6개** — 이번 범위 밖 (선행 작업기록 7절에서 이어짐).
