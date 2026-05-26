#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define main SDL_main

#include "ChatPacket.h"
#include "NetUtil.h"

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

char SendBuffer[1024] = { 0, };
char RecvBuffer[1024] = { 0, };

bool IsRecvThreadRunning = true;
bool IsSendThreadRunning = true;
bool IsRanderThreadRunning = true;

//다른 클라 정보관리
SessionManager MySessionManager;

//내 소켓 아이디
SOCKET MyClientID;

void ProcessPacket(SOCKET ProcessSocket, const char* InBuffer, const Header& InHeader); // Recv받은 패킷에 따라 역할수행
unsigned WINAPI RecvThread(void* Argument); 
unsigned WINAPI SendThread(void* Argument); 
unsigned WINAPI RanderThread(void* Argument);

void SendPacket(SOCKET ServerSocket, IPacket& Data, EPacketType Type);

void Render(SDL_Renderer* MyRender); //랜더 함수
void HideCursor(); //커서 숨기기 함수

SDL_Window* InitWindow(); //윈도우Init
SDL_Renderer* CreateRender(SDL_Window* window); //랜더러 생성

//렌더러 및 이벤트 -> 귀찮으니 전역으로
SDL_Window* MyWindow;
SDL_Renderer* MyRender;
SDL_Event event;

//세션Lock
std::mutex sessionLock;

//queue
std::queue<int> KeyBuffer;

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

	C2S_Login LoginData;
	LoginData.UserID = "Origin";
	LoginData.HashKey = "base64incoding";

	Header LoginHeader;
	LoginHeader.MakeHeader(static_cast<unsigned short>(LoginData.ToString().length()), EPacketType::C2S_Login);

	//Login 요청
	SendAll(ServerSocket, (char*)&LoginHeader, HeaderSize);
	SendAll(ServerSocket, LoginData.ToString().c_str(), (int)LoginData.ToString().length());
	cout << "Login 요청함" << endl;

	HANDLE ThreadHandles[3] = { 0, };

	//nonblocking, asynchrous
	ThreadHandles[0] = (HANDLE)_beginthreadex(0, 0, RecvThread, &ServerSocket, /*CREATE_SUSPENDED*/0, 0);
	ThreadHandles[1] = (HANDLE)_beginthreadex(0, 0, SendThread, &ServerSocket, /*CREATE_SUSPENDED*/0, 0);
	ThreadHandles[2] = (HANDLE)_beginthreadex(0, 0, RanderThread, MyRender, /*CREATE_SUSPENDED*/0, 0);

	//SDL 이벤트
	while (IsRanderThreadRunning) {
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) {
				IsRanderThreadRunning = false; 
			}
			else if (event.type == SDL_KEYDOWN)
			{
				SDL_Keycode pressedKey = event.key.keysym.sym;

				const char* KeyCode = SDL_GetKeyName(pressedKey);
				if (pressedKey == 'w' ||
					pressedKey == 'a' ||
					pressedKey == 's' ||
					pressedKey == 'd')
				{
					C2S_Move Data;
					Data.ClientSocket = MyClientID;
					//0번 유저 강제로 움직이게하기
					//Session* FindSession = MySessionManager.GetSession(0);
					//Data.ClientSocket = FindSession->ClientSocket;
					
					Data.Direction = *KeyCode;
					SendPacket(ServerSocket, Data, EPacketType::C2S_Move);
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

	WSACleanup();


	SDL_DestroyRenderer(MyRender);
	SDL_DestroyWindow(MyWindow);
	SDL_Quit();

	return 0;
}

void ProcessPacket(SOCKET ProcessSocket, const char* InBuffer, const Header& InHeader)
{
	switch ((EPacketType)InHeader.PacketType)
	{
	case EPacketType::S2C_Login:
	{
		S2C_Login LoginPacket;
		LoginPacket.Parse(InBuffer);
		MyClientID = LoginPacket.ClientSocketID;
		std::cout << LoginPacket.ClientSocketID << LoginPacket.Message << std::endl;
	}
	break;
	case EPacketType::S2C_Spawn:
	{
		S2C_Spawn SpawnData;
		SpawnData.Parse(InBuffer);
		std::cout << SpawnData.ToString().c_str() << std::endl;

		//다른 클라이언트 정보 가지고 있는 세션매니저에 자기정보 저장
		//원래는 세션이 아니라 엑터 매니저
		Session Insession;
		Insession.ClientSocket = SpawnData.ClientSocket;
		Insession.Shape = SpawnData.Shape;
		Insession.X = SpawnData.X;
		Insession.Y = SpawnData.Y;
		Insession.R = SpawnData.R;
		Insession.G = SpawnData.G;
		Insession.B = SpawnData.B;
		sessionLock.lock();
		MySessionManager.Add(Insession);
		sessionLock.unlock();
		//Render(MyRender);
	}
	break;
	case EPacketType::S2C_Move:
	{
		S2C_Move MoveData;
		MoveData.Parse(InBuffer);
		Session* FindSession = MySessionManager.GetSession(MoveData.ClientSocket);
		FindSession->X = MoveData.X;
		FindSession->Y = MoveData.Y;

		//std::cout << MoveData.ToString() << std::endl;
		//Render(MyRender);
	}
	break;
	case EPacketType::S2C_Destroy:
	{
		S2C_Destroy DestroyPacket;
		DestroyPacket.Parse(InBuffer);
		std::cout << DestroyPacket.ClientSocket << std::endl;
		Session* FindSession = MySessionManager.GetSession(DestroyPacket.ClientSocket);
		sessionLock.lock(); //Rander쓰레드에 사용할 수 있으므로 Lock
		MySessionManager.Delete(*FindSession);
		sessionLock.unlock();
		//Render(MyRender);
	}
	break;
	}
}

unsigned WINAPI RecvThread(void* Argument)
{
	SOCKET ServerSocket = *(SOCKET*)Argument;

	while (IsRecvThreadRunning)
	{
		Header HeaderData;
		//header
		int RecvBytes = RecvAll(ServerSocket, (char*)&HeaderData, sizeof(HeaderData));
		if (RecvBytes <= 0)
		{
			cout << "recv fail " << endl;
			break;
		}
		HeaderData.NetworkToHost();

		memset(RecvBuffer, 0, sizeof(RecvBuffer));
		//data JSON
		RecvBytes = RecvAll(ServerSocket, RecvBuffer, HeaderData.PacketSize);
		if (RecvBytes <= 0)
		{
			cout << "recv fail " << endl;
			break;
		}

		ProcessPacket(ServerSocket, RecvBuffer, HeaderData);
	
	}
	return 0;
}

unsigned WINAPI SendThread(void* Argument)
{
	//책임은 사용하는 놈이 진다.
	SOCKET ServerSocket = *(SOCKET*)Argument;

	while (IsSendThreadRunning)
	{
		int KeyCode = _getch();
		KeyCode = toupper(KeyCode);
		if (KeyCode == 'W' ||
			KeyCode == 'A' ||
			KeyCode == 'S' ||
			KeyCode == 'D')
		{
			C2S_Move Data;
			Data.ClientSocket = MyClientID;
			Data.Direction = (char)KeyCode;
			SendPacket(ServerSocket, Data,EPacketType::C2S_Move);
		}
		else if (KeyCode == 13)
		{
			cin.getline(SendBuffer, sizeof(SendBuffer));
			ChatPacket Data;
			Data.UserID = "Test";
			Data.Message = SendBuffer;
			Data.Gold = 0;
			std::string JSONString = Data.ToString();
			SendPacket(ServerSocket, Data, EPacketType::ChatPacket);
		}

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


void Render(SDL_Renderer* MyRender)
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
		SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE              // 창을 바로 표시
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