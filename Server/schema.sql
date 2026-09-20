-- 게임서버 등록 테이블. seul 데이터베이스에 한 번만 실행한다.
-- 기존 member 테이블은 건드리지 않는다.

CREATE TABLE IF NOT EXISTS game_server (
    idx         INT AUTO_INCREMENT PRIMARY KEY,
    owner_idx   INT         NOT NULL,
    ip          VARCHAR(45) NOT NULL,
    port        INT         NOT NULL DEFAULT 7777,
    name        VARCHAR(64) NOT NULL DEFAULT '',
    cur_players INT         NOT NULL DEFAULT 0,
    updated_at  DATETIME    NOT NULL DEFAULT CURRENT_TIMESTAMP
                            ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY uk_ip_port (ip, port)
) CHARSET = utf8mb4;
