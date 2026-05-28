#include "pch.h"

#include "NetUtil.h"

#include <iostream>

void SendAll(SOCKET ReceiverSocket, const flatbuffers::FlatBufferBuilder& Builder)
{
	int PacketSize = Builder.GetSize();
	PacketSize = htons(PacketSize);
	//Header
	SendAll(ReceiverSocket, (char*)&PacketSize, 2);
	//Data
	SendAll(ReceiverSocket, (char*)Builder.GetBufferPointer(), Builder.GetSize());

}

int SendAll(SOCKET ReceiverSocket, const char* Data, int Size)
{
	int TotalSendDataSize = 0;
	int WantSendDataSize = Size;
	int SentBytes = 0;
	int Count = 0;
	do
	{
		SentBytes = send(ReceiverSocket, Data + TotalSendDataSize, WantSendDataSize - TotalSendDataSize, 0);
		TotalSendDataSize += SentBytes;
		if (SentBytes <= 0)
		{
			return SentBytes;
		}
	} while (TotalSendDataSize < WantSendDataSize);

	return WantSendDataSize;
}

int RecvAll(SOCKET ReceiverSocket, char* OutData)
{

	int PacketSize = 0;
	int RecvBytes = ::recv(ReceiverSocket, (char*)&PacketSize, 2, MSG_WAITALL);
	if(RecvBytes<=0)
	{
		return RecvBytes;
	};
	PacketSize = ntohs(PacketSize);

	RecvBytes = ::recv(ReceiverSocket, OutData, PacketSize, MSG_WAITALL);
	if (RecvBytes <= 0)
	{
		return RecvBytes;
	};

	return RecvBytes;
}

int RecvAll(SOCKET ReceiverSocket, char* OutData, int Size)
{
	int RecvBytes = recv(ReceiverSocket, OutData, Size, MSG_WAITALL);

	return RecvBytes;
}

const char* GetPacketTypeString(EPacketType Type) {
	switch (Type) {
	case EPacketType::C2S_Login:
		return "C2S_Login";
	case EPacketType::S2C_Login:
		return "S2C_Login";
	case EPacketType::S2C_Spawn:
		return "S2C_Spawn";
	case EPacketType::S2C_Destroy:
		return "S2C_Destroy";
	case EPacketType::C2S_Move:
		return "C2S_Move";
	case EPacketType::S2C_Move:
		return "S2C_Move";
	case EPacketType::ChatPacket:
		return "ChatPacket";
	}
	return "No EPacketType Macting Error";
}
