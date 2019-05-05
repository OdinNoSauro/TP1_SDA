// Application.cpp : Este arquivo contém a função 'main'. A execução do programa começa e termina ali.

#undef UNICODE

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <stdio.h>
#include <iostream>     // std::cout, std::ostream, std::ios
#include <sstream>
#include <process.h>	// _beginthreadex() e _endthreadex() 
#include <iomanip> // needed to use manipulators with parameters (precision, width)
#include "../Include/checkforerror.h"


// Casting para terceiro e sexto parâmetros da função _beginthreadex
typedef unsigned (WINAPI *CAST_FUNCTION)(LPVOID);
typedef unsigned *CAST_LPDWORD;

#define	LISTENPORT "3980"
#define TAMBUF  "20"
#define ACKCODE "33"
#define DATAREQUEST "55"
#define DATAMSG "11"
#define PARAMETERSMSG "00"
#define BUFFERLEN 45


#define EXITERROR -1
#define EXITSUCESS 0 

#pragma comment(lib, "Ws2_32.lib")

HANDLE hOPCClientThread,
	   hSockerServerThread,
	   hMutex;

unsigned int sequenceNumber, 
			 pressureSetPoint,
	         gasVolume, 
			 tubePressure = 1000,
		     tubeTemperature = 1000;

float temperatureSetPoint,
	  reservatoryPressure= 5.22,
	  reservatoryLevel = 3.25;
			

DWORD WINAPI OPCClient();	// declaração da função
DWORD WINAPI SocketServer();	// declaração da função
int increaseSequenceNumber(int previousSequenceNumber);

int main(int argc, char **argv) {
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
	std::ostringstream ss;

	struct addrinfo hints;
	int status, response;
	char code[3], oldSeqNumber[7], resPressure[10], resLevel[10];
	char buffer[BUFFERLEN];
	
	status = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (status < 0) {
		printf("Erro na inicizalização do ambiente WSA: %d\n",status);
		return EXITERROR;
	
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
		return EXITERROR;
	}

	printf("Vinculando o socket a porta\n");
	status = bind(serverSocket, result->ai_addr, (int)result->ai_addr);
	if (status == SOCKET_ERROR) {
		status = WSAGetLastError();
		printf("Erro na vinculação do socket: %d\n", status);
		WSACleanup();
		return EXITERROR;
	}

	freeaddrinfo(result);

	printf("Socket comeca a escutar\n");
	status = listen(serverSocket, SOMAXCONN);
	if (status == SOCKET_ERROR) {
		status = WSAGetLastError();
		printf("Erro no listening: %d\n", status);
		closesocket(serverSocket);
		WSACleanup();
		return EXITERROR;
	}

	printf("Servidor Esperando conexao\n");
	clientSocket = accept(serverSocket,NULL,NULL);
	if (clientSocket == INVALID_SOCKET) {
		status = WSAGetLastError();
		printf("Erro na função accept do socket: %d\n", status);
		closesocket(serverSocket);
		WSACleanup();
		return EXITERROR;
	}
	

	do {
		response = recv(clientSocket, buffer, BUFFERLEN, 0);

		if (response > 0) { // recebeu alguma mensagem
			if (response < BUFFERLEN)
				buffer[response] = '\0';
			strncpy_s(code, buffer, 2);
			code[2] = '\0';
			if (strcmp(code, DATAREQUEST) == 0) {

				printf("Mensagem de requisicao de dados recebida: %s\n", buffer);
				strncpy_s(oldSeqNumber, &buffer[3], 6);
				WaitForSingleObject(hMutex, INFINITE);
				sequenceNumber = increaseSequenceNumber(atoi(oldSeqNumber));
				_gcvt_s(resPressure,10,reservatoryPressure, 5);
				_gcvt_s(resLevel, 10, reservatoryLevel, 5);
				
				int i;
				while ((i=strlen(resPressure))<6) {
					resPressure[i] = '0';
					resPressure[i+1] = '\0';
				}

				while ((i = strlen(resLevel)) < 6) {
					resLevel[i] = '0';
					resLevel[i + 1] = '\0';
				}
				sprintf_s(buffer, "%s/%06i/%05i/%05i/%s/%s", DATAMSG,sequenceNumber, tubePressure, tubeTemperature, resPressure,resLevel);
				ReleaseMutex(hMutex);

				printf("Mensagem com dados enviada: %s\n", buffer);
				status = send(clientSocket, buffer, strlen(buffer), 0);
				if (status == SOCKET_ERROR) {
					printf("Error no envio: %d\n", WSAGetLastError());
					closesocket(clientSocket);
					WSACleanup();
					return EXITERROR;
				}
				

			}
			else if (strcmp(code, PARAMETERSMSG) == 0) {
				printf("Mensagem com parametros de controle recebida: %s\n", buffer);
				std::string str = buffer;

				WaitForSingleObject(hMutex, INFINITE);
				sequenceNumber = increaseSequenceNumber(std::stoi(str.substr(3, 8)));
				pressureSetPoint =  std::stoi(str.substr(10,14));
				temperatureSetPoint = std::stoi(str.substr(16, 21));
				gasVolume = std::stoi(str.substr(23, 27));
				sprintf_s(buffer, "%s/%06i", ACKCODE, sequenceNumber);
				ReleaseMutex(hMutex);

				printf("Mensagem ACK enviada: %s\n", buffer);
				status = send(clientSocket, buffer, strlen(buffer), 0);
				if (status == SOCKET_ERROR) {
					printf("Error no envio: %d\n", WSAGetLastError());
					closesocket(clientSocket);
					WSACleanup();
					return EXITERROR;
				}

			}
			else {
				printf("Strcmp deu errado");
			}

		}
		else if (response == 0) {
			printf("Encerrando a conexao\n");
		}
		else {
			printf("Error no recebimento dos dados: %d\n", WSAGetLastError());
			printf("Esperando Conexao...\n");
			status = listen(serverSocket, SOMAXCONN);
			if (status == SOCKET_ERROR) {
				status = WSAGetLastError();
				printf("Erro no listening: %d\n", status);
				closesocket(serverSocket);
				WSACleanup();
				return EXITERROR;
			}

			clientSocket = accept(serverSocket, NULL, NULL);
			if (clientSocket == INVALID_SOCKET) {
				status = WSAGetLastError();
				printf("Erro na função accept do socket: %d\n", status);
				closesocket(serverSocket);
				WSACleanup();
				return EXITERROR;
			}
			response = 1;
		}
	} while (response >0);

	// Desailtando o Socket
	status = shutdown(clientSocket, SD_SEND);
	if (status == SOCKET_ERROR) {
		printf("Shutdown falhou: %d\n", WSAGetLastError());
		closesocket(clientSocket);
		WSACleanup();
		return EXITERROR;
	}

	// cleanup
	closesocket(clientSocket);
	closesocket(serverSocket);
	WSACleanup();

	return EXITSUCESS;
}

int increaseSequenceNumber(int previousSequenceNumber) {
	if (previousSequenceNumber == 999999)
		return 0;
	else
		return previousSequenceNumber + 1;
}

