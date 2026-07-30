#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define main SDL_main

#include "ChatPacket.h"
#include "NetUtil.h"
#include "UserPacket_generated.h"

#include <winsock2.h>
#include <Windows.h>
#include <iostream>
#include <process.h>
#include <conio.h>
#include <mutex>
#include "SDL.h"
#include <queue>

#pragma comment(lib, "ws2_32")
#pragma comment(lib, "NetCommon")


using namespace std;
using namespace flatbuffers;

char SendBuffer[1024] = { 0, };
char RecvBuffer[1024] = { 0, };

bool IsRecvThreadRunning = true;
bool IsSendThreadRunning = true;
bool IsRanderThreadRunning = true;
bool ToggleLogin = true;

//다른 클라 정보관리
SessionManager MySessionManager;

//내 소켓 아이디
SOCKET MyClientID;

void ProcessPacket(SOCKET ProcessSocket, const char* InBuffer); // Recv받은 패킷에 따라 역할수행
unsigned WINAPI RecvThread(void* Argument); 
unsigned WINAPI SendThread(void* Argument); 
unsigned WINAPI RanderThread(void* Argument);

void SendPacket(SOCKET ServerSocket, IPacket& Data, EPacketType Type);

void Render(); //콘솔용
void HideCursor(); //커서 숨기기 함수
void SignUp(SOCKET ServerSocket);
void LogIn(SOCKET ServerSocket);
void LogOut(SOCKET ServerSocket);

SDL_Window* InitWindow(); //윈도우Init
SDL_Renderer* CreateRender(SDL_Window* window); //랜더러 생성

//렌더러 및 이벤트 -> 귀찮으니 전역으로
SDL_Window* MyWindow;
SDL_Renderer* MyRender;
SDL_Event event;

//세션Lock
std::mutex sessionLock;

//유저 정보
string userId, userPwd;

int main(int argc, char* argv[])
{
	srand((unsigned short)time(nullptr));
	HideCursor();
	SDL_Init(SDL_INIT_EVERYTHING);
	MyWindow = InitWindow();
	MyRender = CreateRender(MyWindow);

	WSAData wsaData;

	WSAStartup(MAKEWORD(2, 2), &wsaData);

	SOCKET ServerSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

	SOCKADDR_IN ServerSockAddr;
	memset(&ServerSockAddr, 0, sizeof(ServerSockAddr));
	ServerSockAddr.sin_family = AF_INET;
	ServerSockAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	//ServerSockAddr.sin_addr.s_addr = inet_addr("192.168.0.95");
	ServerSockAddr.sin_port = htons(35000);

	connect(ServerSocket, (SOCKADDR*)&ServerSockAddr, sizeof(ServerSockAddr));
	cout << "client connect" << endl;

	HANDLE ThreadHandles[3] = { 0, };

	//nonblocking, asynchrous
	ThreadHandles[0] = (HANDLE)_beginthreadex(0, 0, RecvThread, &ServerSocket, /*CREATE_SUSPENDED*/0, 0);
	//ThreadHandles[1] = (HANDLE)_beginthreadex(0, 0, SendThread, &ServerSocket, /*CREATE_SUSPENDED*/0, 0);
	ThreadHandles[2] = (HANDLE)_beginthreadex(0, 0, RanderThread, MyRender, /*CREATE_SUSPENDED*/0, 0);

	Work_LOOPS:
	cout << "원하시는 작업의 숫자를 입력해주세요(ex 1)" << endl;
	cout << "1.SignUp" << endl;
	cout << "2.LogIn" << endl;
	cout << "3.LogOut" << endl;
	//작업 테스트
	while(ToggleLogin)
	{
		if (_kbhit())
		{
			int KeyCode = _getch();
			switch (KeyCode)
			{
			case '1':
			{
				SignUp(ServerSocket);
			}
			break;
			case '2':
			{
				LogIn(ServerSocket);
			}
			break;
			case '3':
			{
				LogOut(ServerSocket);
			}
			break;
			default:
				cout << "다시 입력해주세요" << endl;
				break;
			};
		}
	}

	SDL_ShowWindow(MyWindow);
	cout << "SDL 이벤트 시작" << endl;
	//SDL 이벤트
	while (IsRanderThreadRunning) {
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) {
				IsRanderThreadRunning = false; 
			}
			else if (event.type == SDL_KEYDOWN)
			{
				SDL_Keycode pressedKey = event.key.keysym.sym;

				if (pressedKey == 'c')
				{
					flatbuffers::FlatBufferBuilder SendBuilder;
					auto C2S_ChangeColorData = UserPacket::CreateC2S_ChangeColor(
						SendBuilder,
						(uint16_t)MyClientID);
					auto UserPacketData = UserPacket::CreatePacketData(
						SendBuilder,
						UserPacket::PacketType_C2S_ChangeColor,
						C2S_ChangeColorData.Union()
					);
					SendBuilder.Finish(UserPacketData);
					SendAll(ServerSocket, SendBuilder);
				}
				else if (pressedKey == 'w' ||
					pressedKey == 'a' ||
					pressedKey == 's' ||
					pressedKey == 'd')
				{
					flatbuffers::FlatBufferBuilder SendBuilder;
					flatbuffers::Offset<UserPacket::C2S_Move> C2S_MoveData;
					C2S_MoveData = UserPacket::CreateC2S_Move(
						SendBuilder,
						(uint16_t)MyClientID,
						(int8_t)pressedKey
					);
					
					auto UserPacketData = UserPacket::CreatePacketData(
						SendBuilder,
						UserPacket::PacketType_C2S_Move,
						C2S_MoveData.Union()
					);

					SendBuilder.Finish(UserPacketData);

					SendAll(ServerSocket, SendBuilder);
				}
			}
		}
		
		SDL_Delay(10);
	}

	//blocking
	WaitForMultipleObjects(3, ThreadHandles, FALSE, INFINITE);

	closesocket(ServerSocket);

	IsSendThreadRunning = false;
	IsRecvThreadRunning = false;
	IsRanderThreadRunning = false;

	CloseHandle(ThreadHandles[0]);
	CloseHandle(ThreadHandles[1]);
	CloseHandle(ThreadHandles[2]);

	WSACleanup();


	SDL_DestroyRenderer(MyRender);
	SDL_DestroyWindow(MyWindow);
	SDL_Quit();

	return 0;
}

void ProcessPacket(SOCKET ProcessSocket, const char* InBuffer)
{
	auto UserPacketData = UserPacket::GetPacketData(InBuffer);

	switch (UserPacketData->data_type())
	{
	case UserPacket::PacketType_S2C_Login:
	{
		MyClientID = UserPacketData->data_as_S2C_Login()->client_socket_id();
		if (UserPacketData->data_as_S2C_Login()->is_success())
		{
			ToggleLogin = false;
		}
	}
	break;
	case UserPacket::PacketType_S2C_Spawn:
	{
		auto SpawnData = UserPacketData->data_as_S2C_Spawn();

		//다른 클라이언트 정보 가지고 있는 세션매니저에 자기정보 저장
		//원래는 세션이 아니라 엑터 매니저
		Session InSession;
		InSession.ClientSocket = SpawnData->client_socket_id();
		InSession.Shape = SpawnData->shape();
		InSession.X = SpawnData->position()->x();
		InSession.Y = SpawnData->position()->y();
		InSession.R = SpawnData->color()->r();
		InSession.G = SpawnData->color()->g();
		InSession.B = SpawnData->color()->b();
		sessionLock.lock();
		MySessionManager.Add(InSession);
		sessionLock.unlock();
	}
	break;
	case UserPacket::PacketType_S2C_Move:
	{
		auto MoveData = UserPacketData->data_as_S2C_Move();

		Session* FindSession = MySessionManager.GetSession((SOCKET)MoveData->client_socket_id());
		sessionLock.lock();
		FindSession->X = MoveData->position()->x();
		FindSession->Y = MoveData->position()->y();
		sessionLock.unlock();
	}
	break;
	case UserPacket::PacketType_S2C_Destroy:
	{
		auto DestroyData = UserPacketData->data_as_S2C_Destroy();
		Session* FindSession = MySessionManager.GetSession((SOCKET)DestroyData->client_socket_id());
		sessionLock.lock(); //Rander쓰레드에 사용할 수 있으므로 Lock
		MySessionManager.Delete(*FindSession);
		sessionLock.unlock();
	}
	break;
	case UserPacket::PacketType_S2C_ChangeColor:
	{
		auto ChangeColorData = UserPacketData->data_as_S2C_ChangeColor();
		Session* FindSession = MySessionManager.GetSession((SOCKET)ChangeColorData -> client_socket_id());
		sessionLock.lock();
		FindSession->R = ChangeColorData->color()->r();
		FindSession->G = ChangeColorData->color()->g();
		FindSession->B = ChangeColorData->color()->b();
		sessionLock.unlock();
	}
	break;
	case UserPacket::PacketType_S2C_LogOut:
	{
		auto LogOutData = UserPacketData->data_as_S2C_LogOut();
		cout << LogOutData->user_id() << " 로그아웃 성공 유무: " << LogOutData->is_success() << endl;
	}
	break;
	case UserPacket::PacketType_S2C_SignUp:
	{
		auto SignUpData = UserPacketData->data_as_S2C_SignUp();
		cout << SignUpData->user_id()->c_str() << " 회원가입 성공 유무: " << SignUpData->is_success() << endl;
		if (SignUpData->is_success())
		{
			cout << "원하시는 작업의 숫자를 입력해주세요(ex 1)" << endl;
			cout << "1.SignUp" << endl;
			cout << "2.LogIn" << endl;
		}
	}
	break;
	}
}

unsigned WINAPI RecvThread(void* Argument)
{
	SOCKET ServerSocket = *(SOCKET*)Argument;

	while (IsRecvThreadRunning)
	{
		int RecvBytes = RecvAll(ServerSocket, RecvBuffer);
		if (RecvBytes <= 0)
		{
			std::cout << "recv fail " << endl;
			break;
		}

		ProcessPacket(ServerSocket, RecvBuffer);
	
	}
	return 0;
}

void SignUp(SOCKET ServerSocket)
{
	string userName;
	cout << "아이디 입력: ";
	cin >> userId;
	cout << "비밀번호 입력: ";
	cin >> userPwd;
	cout << "이름 입력: ";
	cin >> userName;

	flatbuffers::FlatBufferBuilder SendBuilder;
	auto C2S_SignUp_Data = UserPacket::CreateC2S_SignUp(
		SendBuilder,
		SendBuilder.CreateString(userId.c_str()),
		SendBuilder.CreateString(userPwd.c_str()),
		SendBuilder.CreateString(userName.c_str())
	);
	auto UserPacketData = UserPacket::CreatePacketData(
		SendBuilder,
		UserPacket::PacketType_C2S_SignUp,
		C2S_SignUp_Data.Union()
	);
	SendBuilder.Finish(UserPacketData);
	SendAll(ServerSocket, SendBuilder);
};

void LogIn(SOCKET ServerSocket)
{
	cout << "아이디 입력: ";
	cin >> userId;
	cout << "비밀번호 입력: ";
	cin >> userPwd;
	flatbuffers::FlatBufferBuilder SendBuilder;
	auto C2S_Login_Data = UserPacket::CreateC2S_Login(
		SendBuilder,
		SendBuilder.CreateString(userId.c_str()),
		SendBuilder.CreateString(userPwd.c_str()),
		SendBuilder.CreateString("base64abcdefg")
	);
	auto UserPacketData = UserPacket::CreatePacketData(
		SendBuilder,
		UserPacket::PacketType_C2S_Login,
		C2S_Login_Data.Union()
	);
	SendBuilder.Finish(UserPacketData);
	SendAll(ServerSocket, SendBuilder);
};

void LogOut(SOCKET ServerSocket)
{
};

//콘솔용 인풋
//SDL사용하면서 사용안함
unsigned WINAPI SendThread(void* Argument)
{
	//책임은 사용하는 놈이 진다.
	SOCKET ServerSocket = *(SOCKET*)Argument;

	while (IsSendThreadRunning)
	{
		//WASD로 움직이기 및 에코 채팅
		//KeyCode = toupper(KeyCode);
		//if (KeyCode == 'W' ||
		//	KeyCode == 'A' ||
		//	KeyCode == 'S' ||
		//	KeyCode == 'D')
		//{
		//	C2S_Move Data;
		//	Data.ClientSocket = MyClientID;
		//	Data.Direction = (char)KeyCode;
		//	SendPacket(ServerSocket, Data,EPacketType::C2S_Move);
		//}
		//else if (KeyCode == 13)
		//{
		//	cin.getline(SendBuffer, sizeof(SendBuffer));
		//	ChatPacket Data;
		//	Data.UserID = "Test";
		//	Data.Message = SendBuffer;
		//	Data.Gold = 0;
		//	std::string JSONString = Data.ToString();
		//	SendPacket(ServerSocket, Data, EPacketType::ChatPacket);
		//}
		//Rend();
	}

	return 0;
}

void SendPacket(SOCKET ServerSocket,IPacket& Data, EPacketType Type)
{
	Header DataHeader;
	DataHeader.MakeHeader((int)(Data.ToString().length()), Type);
	//header
	int SentBytes = SendAll(ServerSocket, (char*)&DataHeader, HeaderSize);
	if (SentBytes <= 0)
	{
		cout << GetPacketTypeString(Type) <<" Header send fail." << endl;
	}
	//Data
	SentBytes = SendAll(ServerSocket, Data.ToString().c_str(), (int)(Data.ToString().length()));
	if (SentBytes <= 0)
	{
		cout <<GetPacketTypeString(Type) << " Data send fail." << endl;
	}
}

unsigned WINAPI RanderThread(void* Argument)
{
	SDL_Renderer* Rander = (SDL_Renderer*)Argument;

	while (IsRanderThreadRunning) {
		SDL_SetRenderDrawColor(MyRender, 0, 0, 0, 255); // 배경을 검은색으로
		SDL_RenderClear(MyRender);
		sessionLock.lock();
		for (auto Player : MySessionManager.SessionList)
		{
			SDL_Rect MyRect{ Player.X, Player.Y, 50, 50 };
			SDL_SetRenderDrawColor(MyRender, (Uint8)Player.R, (Uint8)Player.G, (Uint8)Player.B, 255);
			SDL_RenderFillRect(MyRender, &MyRect);
		}
		sessionLock.unlock();
		SDL_RenderPresent(MyRender);
		SDL_Delay(16);
	}
	return 0;
}


void Render()
{
	/*system("cls");
	for (auto Player : MySessionManager.SessionList)
	{
		COORD Where;
		Where.X = Player.X; 
		Where.Y = Player.Y;
		SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), Where);
		std::cout << (char)Player.Shape;
	}*/
}

void HideCursor()
{
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_CURSOR_INFO cursorInfo;

	GetConsoleCursorInfo(hConsole, &cursorInfo);
	cursorInfo.bVisible = FALSE; //커서를 보이지 않게 설정
	SetConsoleCursorInfo(hConsole, &cursorInfo);
}

SDL_Window* InitWindow()
{
	SDL_Window* window = SDL_CreateWindow(
		"MyWindow",                       // 창 제목
		SDL_WINDOWPOS_CENTERED,           // 창 시작 X 위치 (화면 중앙)
		SDL_WINDOWPOS_CENTERED,           // 창 시작 Y 위치 (화면 중앙)
		1000, 800,                       // 창 가로, 세로 크기
		SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE              // 창을 바로 표시
	);
	return window;
}

SDL_Renderer* CreateRender(SDL_Window* window)
{
	SDL_Renderer* renderer = SDL_CreateRenderer(
		window,
		-1,
		SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC // 하드웨어 가속 및 수직동기화 사용
	);
	return renderer;
}