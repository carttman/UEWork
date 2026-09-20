from fastapi import FastAPI, Request
from pydantic import BaseModel, Field
import pymysql

from db import get_connection

app = FastAPI(title="L20260713_Day03 Auth Server")

# 이 시간 안에 하트비트가 없으면 죽은 서버로 본다.
SERVER_TTL_SECONDS = 30


class AuthRequest(BaseModel):
    user_id: str = Field(min_length=1)
    passwd: str = Field(min_length=1)


class AuthResponse(BaseModel):
    result: bool
    message: str = ""
    idx: int = 0
    nickname: str = ""
    level: int = 0
    server_ip: str = ""
    server_port: int = 0


class ServerRegisterRequest(BaseModel):
    owner_idx: int = Field(gt=0)
    port: int = Field(default=7777, gt=0, lt=65536)
    name: str = ""


class ServerRegisterResponse(BaseModel):
    result: bool
    message: str = ""
    server_idx: int = 0


class HeartbeatRequest(BaseModel):
    server_idx: int = Field(gt=0)
    cur_players: int = 0


class UnregisterRequest(BaseModel):
    server_idx: int = Field(gt=0)


class ServerResponse(BaseModel):
    result: bool
    message: str = ""


@app.post("/signup", response_model=AuthResponse)
def signup(req: AuthRequest):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            try:
                cur.execute(
                    "INSERT INTO member (user_id, passwd, nickname, level)"
                    " VALUES (%s, %s, %s, 1)",
                    (req.user_id, req.passwd, req.user_id),
                )
            except pymysql.err.IntegrityError:
                return AuthResponse(result=False, message="이미 존재하는 아이디입니다")

            new_idx = cur.lastrowid

        conn.commit()
    finally:
        conn.close()

    return AuthResponse(
        result=True, idx=new_idx, nickname=req.user_id, level=1
    )


@app.post("/login", response_model=AuthResponse)
def login(req: AuthRequest):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute(
                "SELECT idx, nickname, level FROM member"
                " WHERE user_id = %s AND passwd = %s",
                (req.user_id, req.passwd),
            )
            row = cur.fetchone()

            server = None
            if row is not None:
                cur.execute(
                    "SELECT ip, port FROM game_server"
                    " WHERE updated_at > NOW() - INTERVAL %s SECOND"
                    " ORDER BY updated_at DESC LIMIT 1",
                    (SERVER_TTL_SECONDS,),
                )
                server = cur.fetchone()
    finally:
        conn.close()

    if row is None:
        return AuthResponse(
            result=False, message="아이디 또는 비밀번호가 올바르지 않습니다"
        )

    # 살아있는 서버가 없으면 빈 값으로 둔다. 로그인 자체는 성공이다.
    return AuthResponse(
        result=True,
        idx=row["idx"],
        nickname=row["nickname"],
        level=row["level"],
        server_ip=server["ip"] if server else "",
        server_port=server["port"] if server else 0,
    )


@app.post("/server/register", response_model=ServerRegisterResponse)
def register_server(req: ServerRegisterRequest, request: Request):
    # 호스트가 자기 IP를 보내지 않는다. 요청이 실제로 도달한 출발지 주소를 쓴다.
    ip = request.client.host if request.client else ""
    if not ip:
        return ServerRegisterResponse(
            result=False, message="서버 주소를 확인할 수 없습니다"
        )

    conn = get_connection()
    try:
        with conn.cursor() as cur:
            # 호스트가 크래시 후 재시작하면 같은 (ip, port)로 다시 들어온다.
            # updated_at을 명시로 갱신해야 한다. 값이 모두 같으면 MySQL이
            # 행을 건드리지 않아 ON UPDATE CURRENT_TIMESTAMP가 발동하지 않는다.
            cur.execute(
                "INSERT INTO game_server (owner_idx, ip, port, name)"
                " VALUES (%s, %s, %s, %s)"
                " ON DUPLICATE KEY UPDATE"
                " owner_idx = VALUES(owner_idx), name = VALUES(name),"
                " cur_players = 0, updated_at = CURRENT_TIMESTAMP",
                (req.owner_idx, ip, req.port, req.name),
            )

            # ON DUPLICATE KEY UPDATE가 탄 경우 lastrowid를 믿을 수 없다.
            cur.execute(
                "SELECT idx FROM game_server WHERE ip = %s AND port = %s",
                (ip, req.port),
            )
            row = cur.fetchone()

        conn.commit()
    finally:
        conn.close()

    return ServerRegisterResponse(result=True, server_idx=row["idx"])


@app.post("/server/heartbeat", response_model=ServerResponse)
def heartbeat(req: HeartbeatRequest):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            # UPDATE의 affected rows로는 "행이 없다"와 "값이 그대로다"를
            # 구분할 수 없어서 존재 여부를 먼저 확인한다.
            cur.execute(
                "SELECT idx FROM game_server WHERE idx = %s", (req.server_idx,)
            )
            if cur.fetchone() is None:
                return ServerResponse(
                    result=False, message="등록되지 않은 서버입니다"
                )

            cur.execute(
                "UPDATE game_server SET cur_players = %s,"
                " updated_at = CURRENT_TIMESTAMP WHERE idx = %s",
                (req.cur_players, req.server_idx),
            )

        conn.commit()
    finally:
        conn.close()

    return ServerResponse(result=True)


@app.post("/server/unregister", response_model=ServerResponse)
def unregister_server(req: UnregisterRequest):
    conn = get_connection()
    try:
        with conn.cursor() as cur:
            cur.execute(
                "DELETE FROM game_server WHERE idx = %s", (req.server_idx,)
            )
        conn.commit()
    finally:
        conn.close()

    # 이미 없어도 성공이다. 지우려던 결과는 달성됐다.
    return ServerResponse(result=True)
