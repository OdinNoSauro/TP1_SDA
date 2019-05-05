// Application.cpp : Este arquivo contém a função 'main'. A execução do programa começa e termina ali.

#undef UNICODE

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string.h>
#include <stdio.h>
#include <process.h>	// _beginthreadex() e _endthreadex() 
#include "../Include/checkforerror.h"


// Casting para terceiro e sexto parâmetros da função _beginthreadex
typedef unsigned (WINAPI *CAST_FUNCTION)(LPVOID);
typedef unsigned *CAST_LPDWORD;

#define	LISTENPORT "3980"
#define TAMBUF  20
#define ACKCODE 33
#define DATAREQUEST 55
#define DATAMSG 11
#define PARAMETERSMSG 00
#define BUFFERLEN 30

#define ERROR -1
#define EXITSUCESS 0 

#pragma comment(lib, "Ws2_32.lib")

HANDLE hOPCClientThread,
	   hSockerServerThread,
	   hMutex;

unsigned int sequenceNumber, 
			 pressureSetPoint,
	         gasVolume, 
			 tubePressure,
		     tubeTemperature;

float temperatureSetPoint,
	  reservatoryPressure,
	  reservatoryLevel;
			

DWORD WINAPI OPCClient();	// declaração da função
DWORD WINAPI SocketServer();	// declaração da função

int main(int argc, char **argv) {
	system("chcp 1252"); // Comando para apresentar caracteres especiais no console
	DWORD dwReturn,
		dwExitCode,
		dwThreadId;
	hMutex = CreateMutex(NULL, FALSE, (LPCSTR)"DataMutex");

	hOPCClientThread = (HANDLE)_beginthreadex(
		NULL,
		0,
		(CAST_FUNCTION)OPCClient,	// casting necessário
		NULL,
		0,
		(CAST_LPDWORD)&dwThreadId	// casting necessário
	);

	hSockerServerThread = (HANDLE)_beginthreadex(
		NULL,
		0,
		(CAST_FUNCTION)SocketServer,	// casting necessário
		NULL,
		0,
		(CAST_LPDWORD)&dwThreadId	// casting necessário
	);

	

	const HANDLE pointerHandles[2] = { hOPCClientThread, hSockerServerThread};
	dwReturn = WaitForMultipleObjects(2, pointerHandles, true, INFINITE);
//	CheckForError(dwReturn);

	CloseHandle(hMutex);

	dwReturn = GetExitCodeThread(hOPCClientThread, &dwExitCode);
	//CheckForError(dwReturn);
	CloseHandle(hOPCClientThread);	// apaga referência ao objeto
	dwReturn = GetExitCodeThread(hSockerServerThread, &dwExitCode);
	//CheckForError(dwReturn);
	CloseHandle(hSockerServerThread);	
	
	return 0;
}



DWORD WINAPI OPCClient() {
	
	return 0;
}
DWORD WINAPI SocketServer() {
	WSADATA     wsaData;
	SOCKET serverSocket;
	SOCKET clientSocket;
	struct addrinfo *result = NULL;
	struct addrinfo hints;
	int status;
	char buffer[BUFFERLEN];
	
	status = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (status < 0) {
		printf("Erro na inicizalização do ambientw WSA: %d\n",status);
		return ERROR;
	
	}

	//Inicializando Estrutura do socket
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	status = GetAddrInfo(NULL, LISTENPORT, &hints, &result);

	printf("Criando socket TCP\n");
	serverSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (serverSocket == INVALID_SOCKET) {
		status = WSAGetLastError();
		printf("Erro na criação do socket: %d\n",status);
		WSACleanup();
		return ERROR;
	}

	printf("Vinculando o socket a porta\n");
	status = bind(serverSocket, result->ai_addr, (int)result->ai_addr);
	if (status == SOCKET_ERROR) {
		status = WSAGetLastError();
		printf("Erro na vinculação do socket: %d\n", status);
		WSACleanup();
		return ERROR;
	}

	freeaddrinfo(result);

	printf("Socket comeca a escutar\n");
	status = listen(serverSocket, SOMAXCONN);
	if (status == SOCKET_ERROR) {
		status = WSAGetLastError();
		printf("Erro no listening: %d\n", status);
		closesocket(serverSocket);
		WSACleanup();
		return ERROR;
	}

	printf("Servidor Esperando conexao\n");
	clientSocket = accept(serverSocket,NULL,NULL);
	if (clientSocket == INVALID_SOCKET) {
		status = WSAGetLastError();
		printf("Erro na função accept do socket: %d\n", status);
		closesocket(serverSocket);
		WSACleanup();
		return ERROR;
	}

	//serverSocket não é mais necessário
	closesocket(serverSocket);

	do {
		status = recv(clientSocket, buffer, BUFFERLEN, 0);
		if (status > 0) { // recebeu alguma mensagem
			printf("Mensagem recebida: %s\n", buffer);
		}
		else if (status == 0) {
			printf("Encerrando a conexao\n");
		}
		else {
			printf("Error no recebimento dos dados: %d\n", WSAGetLastError());
			closesocket(clientSocket);
			WSACleanup();
			return ERROR;
		}
	} while (status>0);

	// Desailtando o Socket
	status = shutdown(clientSocket, SD_SEND);
	if (status == SOCKET_ERROR) {
		printf("Shutdown falhou: %d\n", WSAGetLastError());
		closesocket(clientSocket);
		WSACleanup();
		return ERROR;
	}

	// cleanup
	closesocket(clientSocket);
	WSACleanup();

	return EXITSUCESS;
}

