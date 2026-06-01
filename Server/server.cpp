#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "NetUtil.h"
#include "UserPacket_generated.h"
#include <winsock2.h>
#include <iostream>

#pragma comment(lib, "ws2_32")
#pragma comment(lib, "NetCommon")

//mysql cpp connect 라이브러리
#include "jdbc/mysql_connection.h"
#include "jdbc/cppconn/driver.h"
#include "jdbc/cppconn/exception.h"
#include "jdbc/cppconn/resultset.h"
#include "jdbc/cppconn/statement.h"
#include "jdbc/cppconn/prepared_statement.h"

#pragma comment(lib,"debug/mysqlcppconn")

using namespace std;
using namespace sql;

char Buffer[1024] = { 0, };

SessionManager MySessionManager; //세션 매니저

void ProcessPacket(SOCKET ProcessSocket, const char* InBuffer);
void DisconnectSocket(SOCKET DisconnectedSocket, fd_set* Sockets);

//DB관련
Driver* MyDiver;
Connection* MyConnection;
ResultSet* MyResultSet;
PreparedStatement* MyPreparedStatement;

//blocking, synchrous, multiplexing(polling)
int main()
{
	try
	{
		//DB연동 -> 나중에 함수로
		MyDiver = get_driver_instance(); 
		MyConnection = MyDiver->connect("tcp://127.0.0.1", "Origin", "bit05");		
		Statement* stmt = MyConnection->createStatement();
		stmt->execute("SET NAMES euckr;");
		delete stmt; // 사용 후 바로 해제
		MyConnection->setSchema("testdb");
		cout << "DB 연동 완료" << endl;

		cout << "server start" << endl;

		WSAData wsaData;

		WSAStartup(MAKEWORD(2, 2), &wsaData);

		SOCKET ListenSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

		SOCKADDR_IN ListenSockAddr;
		memset(&ListenSockAddr, 0, sizeof(ListenSockAddr));
		ListenSockAddr.sin_family = AF_INET;
		ListenSockAddr.sin_addr.s_addr = INADDR_ANY;
		ListenSockAddr.sin_port = htons(35000);

		//already use port 이미 포트 사용중
		::bind(ListenSocket, (SOCKADDR*)&ListenSockAddr, sizeof(ListenSockAddr));

		listen(ListenSocket, SOMAXCONN);

		//blocking, synchronous(TimeOut)
		TIMEVAL TimeOut;
		TimeOut.tv_sec = 0;
		TimeOut.tv_usec = 500000;

		fd_set ReadSockets;
		fd_set CopyReadSockets;

		FD_ZERO(&ReadSockets);
		FD_SET(ListenSocket, &ReadSockets);

		while (true)
		{
			CopyReadSockets = ReadSockets;

			//0.5초씩 blocking
			int ChangeCount = select(0, &CopyReadSockets, 0, 0, &TimeOut);

			if (ChangeCount <= 0)
			{
				//Server Work
				//0.5초한번 서버 작업을 하는거
				continue;
			}

			//몬가 자료 있다.
			for (int i = 0; i < (int)ReadSockets.fd_count; ++i)
			{
				if (FD_ISSET(ReadSockets.fd_array[i], &CopyReadSockets))
				{
					if (ReadSockets.fd_array[i] == ListenSocket)
					{
						//connect process
						SOCKADDR_IN ClientSockAddr;
						memset(&ClientSockAddr, 0, sizeof(ClientSockAddr));
						int ClientSockSockLength = sizeof(ClientSockAddr);

						//blocking, synchronous
						SOCKET ClientSocket = accept(ListenSocket, (SOCKADDR*)&ClientSockAddr, &ClientSockSockLength);

						cout << "connect client " << inet_ntoa(ClientSockAddr.sin_addr) << endl;

						FD_SET(ClientSocket, &ReadSockets);
					}
					else
					{
						//Data Receive
						int RecvBytes = RecvAll(ReadSockets.fd_array[i], Buffer);
						if (RecvBytes <= 0)
						{
							cout << "data recv fail " << endl;
							DisconnectSocket(ReadSockets.fd_array[i], &ReadSockets);
							continue;
						}
						else
						{
							ProcessPacket(ReadSockets.fd_array[i], Buffer);
						}
					}
				}
			}
		}

		closesocket(ListenSocket);
		WSACleanup();

		//DB연동 메모리 해제
		if (MyResultSet) delete MyResultSet;
		if (MyPreparedStatement) delete MyPreparedStatement;
		if (MyConnection) delete MyConnection;
	}
	catch (SQLException Exception)
	{
		cout << Exception.what() << endl;
		cout << Exception.getSQLState() << endl;
	}
	return 0;
}

void ProcessPacket(SOCKET ProcessSocket, const char* InBuffer)
{
	auto UserPacketData = UserPacket::GetPacketData(InBuffer);
	switch (UserPacketData->data_type())
	{
	case UserPacket::PacketType_C2S_Login:
	{
		auto LoginPacket = UserPacketData->data_as_C2S_Login();
		cout << LoginPacket->user_id()->c_str() << " 아이디로 로그인 시도" << endl;
		//select로 DB에 유저 있는지 확인 -> 로그인 되어있으면 다른 기기에서 중복 로그인 불가
		SQLString Query = "SELECT user_id FROM testdb.user_info WHERE user_id=? and is_login =0 and user_pwd =sha2(?,512);";
		MyPreparedStatement = MyConnection->prepareStatement(Query);
		MyPreparedStatement->setString(1, LoginPacket->user_id()->c_str());
		MyPreparedStatement->setString(2, LoginPacket->user_pwd()->c_str());
		MyResultSet = MyPreparedStatement->executeQuery();
		bool is_success = true;
		if (MyResultSet->next()==false)
		{
			cout << "C2S_Login SELECT Query fail." << endl;
			is_success = false;
		}
		//is_login값 0에서 1로 변경
		if (is_success)
		{
			SQLString Query = "UPDATE testdb.user_info SET is_login = 1 WHERE user_id=?;";
			MyPreparedStatement = MyConnection->prepareStatement(Query);
			MyPreparedStatement->setString(1, (SQLString)LoginPacket->user_id()->c_str());
			int Affected_rows = MyPreparedStatement->executeUpdate();
			if (Affected_rows <= 0)
			{
				cout << "C2S_Login UPDATE Query fail." << endl;
				is_success = false;
			}
		}

		//실패시 클라에게 메시지 보내기
		if (!is_success)
		{
			cout << LoginPacket->user_id()->c_str() <<" Login fail." << endl;
			flatbuffers::FlatBufferBuilder SendBuilder;
			auto S2C_Login_Data = UserPacket::CreateS2C_Login(
				SendBuilder,
				(uint16_t)ProcessSocket,
				SendBuilder.CreateString("로그인 실패"),
				is_success
			);

			auto UserPacketData = UserPacket::CreatePacketData(
				SendBuilder,
				UserPacket::PacketType_S2C_Login,
				S2C_Login_Data.Union()
			);

			SendBuilder.Finish(UserPacketData);
			int SentBytes = SendAll(ProcessSocket, SendBuilder);
			if (SentBytes <= 0)
			{
				cout << "S2C_Login send fail." << endl;
				return;
			}
			return;
		}

		cout << LoginPacket->user_id()->c_str() << " 로그인 성공" << endl;

		//접속 한 유저 정보 업데이트(Session)
		Session InSession;
		InSession.ClientSocket = ProcessSocket;
		InSession.UserID = LoginPacket->user_id()->c_str();
		InSession.X = rand() % 250 + 1;
		InSession.Y = rand() % 250 + 1;
		InSession.Shape = 65 + (rand() % 26);
		InSession.R = 255;
		InSession.G = rand() % 255;
		InSession.B = rand() % 255;
		MySessionManager.Add(InSession);

		//접속 한 아이한테 확인 패킷(S2C_Login)
		flatbuffers::FlatBufferBuilder SendBuilder;
		auto S2C_Login_Data = UserPacket::CreateS2C_Login(
			SendBuilder,
			(uint16_t)ProcessSocket,
			SendBuilder.CreateString("로그인 성공"),
			is_success
		);

		auto UserPacketData = UserPacket::CreatePacketData(
			SendBuilder,
			UserPacket::PacketType_S2C_Login,
			S2C_Login_Data.Union()
		);

		SendBuilder.Finish(UserPacketData);
		int SentBytes = SendAll(ProcessSocket, SendBuilder);
		if (SentBytes <= 0)
		{
			cout << "S2C_Login send fail." << endl;
			return;
		}
		//접속한 모든 유저한테 현재 모든 유저의 정보를 보내줌
		for (auto Item : MySessionManager.SessionList)
		{
			flatbuffers::FlatBufferBuilder LoginSendBuilder;

			UserPacket::FVector2D Position(Item.X, Item.Y);
			UserPacket::FColor Color(Item.R, Item.G, Item.B);
			auto SpawnData = UserPacket::CreateS2C_Spawn(
				LoginSendBuilder,
				(uint16_t)Item.ClientSocket,
				&Position,
				&Color,
				Item.Shape
			);
			auto UserSpawnPacketData = UserPacket::CreatePacketData(
				LoginSendBuilder,
				UserPacket::PacketType_S2C_Spawn,
				SpawnData.Union()
			);
			LoginSendBuilder.Finish(UserSpawnPacketData);

			for (auto Receiver : MySessionManager.SessionList)
			{
				int SentBytes = SendAll(Receiver.ClientSocket, LoginSendBuilder);
				if (SentBytes <= 0)
				{
					cout << "S2C_Login LoginSendBuilder send fail." << endl;
					return;
				}
			}
		}
	}
	break;
	case UserPacket::PacketType_C2S_Move:
	{
		flatbuffers::FlatBufferBuilder SendBuilder;
		auto MovePacket = UserPacketData->data_as_C2S_Move();
		Session* FindSession = MySessionManager.GetSession((SOCKET)MovePacket->client_socket_id());
		//cout << MovePacket->direction() << endl;
		switch (MovePacket->direction())
		{
		case 'w':
			FindSession->Y--;
			break;
		case 's':
			FindSession->Y++;
			break;
		case 'a':
			FindSession->X--;
			break;
		case 'd':
			FindSession->X++;
			break;
		}
		//모든 유저한테 이동 패킷 보내기
		UserPacket::FVector2D Position(FindSession->X, FindSession->Y);
		auto S2C_MoveData = UserPacket::CreateS2C_Move(
			SendBuilder,
			(uint16_t)FindSession->ClientSocket,
			&Position
		);
		auto MoveData = UserPacket::CreatePacketData(
			SendBuilder,
			UserPacket::PacketType_S2C_Move,
			S2C_MoveData.Union()
		);
		SendBuilder.Finish(MoveData);

		for (auto Receiver : MySessionManager.SessionList)
		{
			int SentBytes = SendAll(Receiver.ClientSocket, SendBuilder);
			if (SentBytes <= 0)
			{
				cout << "C2S_Move send fail." << endl;
				return;
			}
		}
	}
	break;
	case UserPacket::PacketType_C2S_ChangeColor:
	{
		flatbuffers::FlatBufferBuilder SendBuilder;

		auto ChangeColorPacket = UserPacketData->data_as_C2S_ChangeColor();
		Session* ChangeSession = MySessionManager.GetSession((SOCKET)ChangeColorPacket->client_socket_id());
		ChangeSession->R = rand() % 255;
		ChangeSession->G = rand() % 255;
		ChangeSession->B = rand() % 255;
		UserPacket::FColor Color(ChangeSession->R, ChangeSession->G, ChangeSession->B);
		auto S2C_ColorData = UserPacket::CreateS2C_ChangeColor(
			SendBuilder,
			ChangeColorPacket->client_socket_id(),
			&Color
		);
		auto UserPacketData = UserPacket::CreatePacketData(
			SendBuilder,
			UserPacket::PacketType_S2C_ChangeColor,
			S2C_ColorData.Union()
		);
		SendBuilder.Finish(UserPacketData);

		//모든 유저한테 이동 패킷 보내줌
		for (auto Receiver : MySessionManager.SessionList)
		{
			int SentBytes = SendAll(Receiver.ClientSocket, SendBuilder);
			if (SentBytes <= 0)
			{
				std::cout << "C2S_ChangeColor send fail." << endl;
			}
		}
	}
	break;
	case UserPacket::PacketType_C2S_LogOut:
	{
		auto LogOutPacket = UserPacketData->data_as_C2S_LogOut();
		cout << LogOutPacket->user_id()->c_str() << " 로그아웃 시도" << endl;
		//select 후 있으면 로그아웃, 없으면 불가 반환
		MyPreparedStatement = MyConnection->prepareStatement("CALL check_and_logout_user(?, ?, @result)");
		//인자 바인딩
		MyPreparedStatement->setString(1, LogOutPacket->user_id()->c_str());
		MyPreparedStatement->setInt(2, 1);
		//프로시저 실행
		MyPreparedStatement->execute();
		//세션 변수(@result)에 담긴 OUT 파라미터 값 조회
		MyPreparedStatement = MyConnection->prepareStatement("SELECT @result AS is_success");
		MyResultSet = MyPreparedStatement->executeQuery();
		//결과 확인
		bool isSuccess = MyResultSet->getBoolean("is_success");
		if (isSuccess == 1) {
			cout << "성공: 로그인 상태가 정상적으로 검증되었고 로그아웃 되었습니다. (TRUE)" << endl;
		}
		else {
			cout << "실패: 조건이 맞지 않거나 이미 로그아웃된 사용자입니다. (FALSE)" << endl;
		}

		//성공 여부 클라에게 보내기
		flatbuffers::FlatBufferBuilder SendBuilder;
		auto S2C_LogOut_Data = UserPacket::CreateS2C_LogOut(
			SendBuilder,
			SendBuilder.CreateString(LogOutPacket->user_id()->c_str()),
			isSuccess
		);
		auto UserPacketData = UserPacket::CreatePacketData(
			SendBuilder,
			UserPacket::PacketType_S2C_LogOut,
			S2C_LogOut_Data.Union()
		);
		SendBuilder.Finish(UserPacketData);
		int SentBytes = SendAll(ProcessSocket, SendBuilder);
		if (SentBytes <= 0)
		{
			cout << "S2C_LogOut send fail." << endl;
			return;
		}
	}
	break;
	case UserPacket::PacketType_C2S_SignUp:
	{
		auto SignUpPacket = UserPacketData->data_as_C2S_SignUp();
		
		//예외처리 생략
		SQLString UserID = SignUpPacket->user_id()->c_str();
		SQLString Password = SignUpPacket->user_pwd()->c_str();
		SQLString UserName = SignUpPacket->user_name()->c_str();

		//DB에 유저 정보 저장
		SQLString Query = "INSERT INTO testdb.user_info(user_id, user_pwd, user_name) VALUES(?, sha2(?,512), ?);";
		MyPreparedStatement = MyConnection->prepareStatement(Query);
		MyPreparedStatement->setString(1, UserID);
		MyPreparedStatement->setString(2, Password);
		MyPreparedStatement->setString(3, UserName);
		int Affected_rows = MyPreparedStatement->executeUpdate();
		//DB 저장 여부
		bool is_success = true;
		if (Affected_rows <= 0)
		{
			cout << "C2S_SignUp INSERT Error" << endl;
			is_success = false;
		}
		
		//성공 여부 클라에게 보내기
		flatbuffers::FlatBufferBuilder SendBuilder;		
		auto S2C_SignUp_Data = UserPacket::CreateS2C_SignUp(
			SendBuilder,
			SendBuilder.CreateString(SignUpPacket->user_id()->c_str()),
			is_success
		);
		auto UserPacketData = UserPacket::CreatePacketData(
			SendBuilder,
			UserPacket::PacketType_S2C_SignUp,
			S2C_SignUp_Data.Union()
		);
		SendBuilder.Finish(UserPacketData);
		int SentBytes = SendAll(ProcessSocket, SendBuilder);
		if (SentBytes <= 0)
		{
			cout << "S2C_SignUp send fail." << endl;
			return;
		}
	}
	break;
	}
	
}


void DisconnectSocket(SOCKET DisconnectedSocket, fd_set* Sockets)
{
	SOCKADDR_IN ClosedSockAddr;
	memset(&ClosedSockAddr, 0, sizeof(ClosedSockAddr));
	int ClosedSockAddrLength = sizeof(ClosedSockAddr);

	SOCKET ClosedSocket = DisconnectedSocket;
	getpeername(ClosedSocket, (SOCKADDR*)&ClosedSockAddr, &ClosedSockAddrLength);
	FD_CLR(DisconnectedSocket, Sockets);
	closesocket(ClosedSocket);

	//세션에서도 지워야함
	flatbuffers::FlatBufferBuilder SendBuilder;
	auto DestroyData = UserPacket::CreateS2C_Destroy(
		SendBuilder,
		(uint16_t)ClosedSocket
	);

	auto UserPacketData = UserPacket::CreatePacketData(
		SendBuilder,
		UserPacket::PacketType_S2C_Destroy,
		DestroyData.Union()
	);

	SendBuilder.Finish(UserPacketData);
	Session* FindSession = MySessionManager.GetSession(ClosedSocket);
	MySessionManager.Delete(*FindSession);
	FindSession = nullptr;
	//모든 유저한테 보내기

	for (auto Receiver : MySessionManager.SessionList)
	{
		SendAll(Receiver.ClientSocket, SendBuilder);
	}

}

