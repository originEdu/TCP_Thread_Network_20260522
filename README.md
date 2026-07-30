# ThreadStudy_20260522

<img width="400" height="430" alt="CPP TCP 통신" src="https://github.com/user-attachments/assets/4fe0f35f-9a85-4fa6-800c-adb9574190eb" />

## [프로젝트 요약]

Winsock TCP 기반 멀티플레이 학습 프로젝트. 
여러 클라이언트가 서버에 접속해 로그인하고, 
각자 사각형 하나를 조작하면 그 위치가 접속 중인 모든 클라이언트 화면에 동기화.

- 서버: 싱글 스레드 `select()` 멀티플렉싱 + MySQL 계정 인증
- 클라이언트: 멀티 스레드(수신 / 렌더) + SDL2 2D 렌더링
- 직렬화: FlatBuffers (`UserPacket.fbs`)

---

## 구성

| 프로젝트 | 종류 | 설명 |
|---|---|---|
| `Server` | Application | TCP 리슨, `select` 폴링, 패킷 처리, 세션 관리, MySQL 연동 |
| `Client` | Application | 서버 접속, 콘솔 로그인 UI, SDL2 창에 사각형 렌더 |
| `NetCommon` | StaticLibrary | 패킷 스키마/생성 코드, 송수신 유틸, 세션 매니저 |

---

## 동작 흐름

```
Client                          Server
  |  connect (TCP 127.0.0.1:35000)  |
  |-------------------------------->|  accept, fd_set 등록
  |                                 |
  |  C2S_SignUp (id/pwd/name)       |
  |-------------------------------->|  INSERT user_info (pwd = SHA2(pwd,512))
  |<--------------------------------|  S2C_SignUp (is_success)
  |                                 |
  |  C2S_Login (id/pwd)             |
  |-------------------------------->|  SELECT ... is_login=0 AND pwd=SHA2(?,512)
  |                                 |  UPDATE is_login = 1   (중복 로그인 차단)
  |<--------------------------------|  S2C_Login (client_socket_id, is_success)
  |<--------------------------------|  S2C_Spawn × 접속자 수 (전체 브로드캐스트)
  |                                 |
  |  C2S_Move (w/a/s/d)             |
  |-------------------------------->|  세션 좌표 갱신
  |<--------------------------------|  S2C_Move (전체 브로드캐스트)
  |                                 |
  |  C2S_ChangeColor (c)            |
  |-------------------------------->|  랜덤 RGB 재설정
  |<--------------------------------|  S2C_ChangeColor (전체 브로드캐스트)
  |                                 |
  |  disconnect                     |
  |-------------------------------->|  세션 삭제 + S2C_Destroy 브로드캐스트
```

### 서버 (`Server/server.cpp`)
- 블로킹 소켓 + `select()` 타임아웃 0.5초 폴링 루프. 스레드 없이 단일 루프에서 모든 클라이언트 처리.
- `fd_set` 순회 중 리슨 소켓이면 `accept`, 아니면 `RecvAll` 후 `ProcessPacket`.
- 접속자 상태는 `SessionManager`의 `Session` 벡터(소켓, UserID, X/Y, RGB, Shape)로 보관.
- 좌표 이동/색 변경은 서버가 권위(authoritative)를 가지고, 결과만 전체에 브로드캐스트.

### 클라이언트 (`Client/client.cpp`)
- `RecvThread`: 블로킹 `recv` 루프, 수신 패킷을 세션 매니저에 반영.
- `RanderThread`: 16ms 주기로 세션 목록을 순회하며 `SDL_RenderFillRect`로 50×50 사각형 렌더.
- 메인 스레드: 로그인 전에는 콘솔 메뉴(`_kbhit`/`_getch`), 로그인 성공 후 `SDL_PollEvent` 루프로 전환해 WASD/`c` 입력을 서버로 전송.
- 두 스레드가 `SessionManager`를 공유하므로 `std::mutex sessionLock`으로 보호.

### 패킷 (`NetCommon/UserPacket.fbs`)
길이 프리픽스(2바이트, 네트워크 바이트 오더) + FlatBuffers 본문. `RecvAll`/`SendAll`이 `MSG_WAITALL`과 반복 `send`로 부분 송수신을 처리한다.

| 방향 | 패킷 |
|---|---|
| C2S | `C2S_SignUp`, `C2S_Login`, `C2S_LogOut`, `C2S_Move`, `C2S_ChangeColor`, `C2S_Chat`(미사용) |
| S2C | `S2C_SignUp`, `S2C_Login`, `S2C_LogOut`, `S2C_Spawn`, `S2C_Move`, `S2C_Destroy`, `S2C_ChangeColor`, `S2C_Chat`(미사용) |

---

### 조작
| 키 | 동작 |
|---|---|
| `W` `A` `S` `D` | 사각형 이동  |
| `C` | 내 사각형 색상 랜덤 변경 |
| 창 닫기 | 종료 |

---

