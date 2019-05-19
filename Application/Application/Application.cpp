// Application.cpp: Este arquivo contém a função 'main'. A execução do programa começa e termina ali.

using namespace std;

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <strsafe.h>
#include <iostream>     // std::cout, std::ostream, std::ios
#include <sstream>
#include <process.h>	// _beginthreadex() e _endthreadex() 
#include <iomanip>      // Necessário para utilizar manipuladores com parâmetros (precision, width)
#include <atlbase.h>    // Necessário para utilizar o macro "_T"
#include <iostream>
#include <ObjIdl.h>
#include "opcda.h"
#include "opcerror.h"
#include "SOCDataCallback.h"
#include "SOCAdviseSink.h"
#include "SOCWrapperFunctions.h"
#include "OPCFunctions.h"


// Casting para terceiro e sexto parâmetros da função _beginthreadex
typedef unsigned (WINAPI *CAST_FUNCTION)(LPVOID);
typedef unsigned *CAST_LPDWORD;

HANDLE hSlot;
LPTSTR SlotName = (LPTSTR)TEXT("\\\\.\\mailslot\\sample_mailslot");

#define	LISTENPORT "3980"
#define TAMBUF  "20"
#define ACKCODE "33"
#define DATAREQUEST "55"
#define DATAMSG "11"
#define PARAMETERSMSG "00"
#define BUFFERLEN 45
#define EXITERROR -1
#define EXITSUCESS 0 

#define WHITE   FOREGROUND_RED   | FOREGROUND_GREEN      | FOREGROUND_BLUE
#define HLGREEN FOREGROUND_GREEN | FOREGROUND_INTENSITY
#define HLRED   FOREGROUND_RED   | FOREGROUND_INTENSITY
#define HLBLUE  FOREGROUND_BLUE  | FOREGROUND_INTENSITY
#define YELLOW  FOREGROUND_RED   | FOREGROUND_GREEN
#define PINK FOREGROUND_RED | FOREGROUND_BLUE
#define CYAN FOREGROUND_GREEN | FOREGROUND_BLUE

#pragma comment(lib, "Ws2_32.lib")

HANDLE hOPCClientThread,
	   hSockerServerThread,
	   hEvent,
	   hMutex,
	   hOut;

unsigned int sequenceNumber, 
			 pressureSetPoint,
	         gasVolume, 
			 tubePressure = 1000,
		     tubeTemperature = 1000;

float temperatureSetPoint,
	  reservatoryPressure= 5.22f,
	  reservatoryLevel = 3.25f;
			
UINT OPC_DATA_TIME = RegisterClipboardFormat(_T("OPCSTMFORMATDATATIME"));

DWORD WINAPI OPCClient();	    // Declaração da função
DWORD WINAPI SocketServer();	// Declaração da função
int increaseSequenceNumber(int previousSequenceNumber);
BOOL ReadSlot();
void parseMessage(char* msg);
bool GenerateVar(VARIANT* var, VARTYPE var_type, void* var_value);

int main(int argc, char **argv) {
	DWORD dwReturn,
		  dwExitCode,
		  dwThreadId;

	hMutex = CreateMutex(NULL, FALSE, (LPCWSTR)"DataMutex");
	hEvent = CreateEvent(NULL, TRUE, FALSE, (LPCWSTR)"ParametersReceiverEvent");
	hSlot = CreateMailslot(SlotName,
		0,                             // Sem tamanho máximo de mensagem 
		MAILSLOT_WAIT_FOREVER,         // Sem timeout para operações 
		NULL);						   // Segurança padrão

	if (hSlot == INVALID_HANDLE_VALUE)
	{
		printf("CreateMailslot falhou com %d\n", GetLastError());
	}

	hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hOut == INVALID_HANDLE_VALUE)
		printf("Erro ao obter handle para a saída do console\n");

	hOPCClientThread = (HANDLE)_beginthreadex(
		NULL,
		0,
		(CAST_FUNCTION)OPCClient,	// Casting necessário
		NULL,
		0,
		(CAST_LPDWORD)&dwThreadId	// Casting necessário
	);

	hSockerServerThread = (HANDLE)_beginthreadex(
		NULL,
		0,
		(CAST_FUNCTION)SocketServer,	// Casting necessário
		NULL,
		0,
		(CAST_LPDWORD)&dwThreadId		// Casting necessário
	);

	const HANDLE pointerHandles[2] = { hOPCClientThread, hSockerServerThread};
	dwReturn = WaitForMultipleObjects(2, pointerHandles, true, INFINITE);

	CloseHandle(hMutex);

	dwReturn = GetExitCodeThread(hOPCClientThread, &dwExitCode);
	CloseHandle(hOPCClientThread);	// Apaga referência ao objeto
	dwReturn = GetExitCodeThread(hSockerServerThread, &dwExitCode);
	CloseHandle(hSockerServerThread);	
	
	return 0;
}

DWORD WINAPI OPCClient() {

	IOPCServer* pIOPCServer = NULL;					 // Ponteiro para interface IOPServer
	IOPCItemMgt* pDataIOPCItemMgt = NULL;		  	 // Ponteiro para interface IOPCItemMgt
	IOPCItemMgt* pParametersIOPCItemMgt = NULL;		 // Ponteiro para interface IOPCItemMgt

	OPCHANDLE hServerGroup[2];			// Handle do servidor para o grupo
	OPCHANDLE hServerItem[7];			// Handle do servidor para o item

	DWORD dwReturn,
		  dwExitCode,
		  dwThreadId;

	char buf[100];
	VARIANT tempVariant;
	VariantInit(&tempVariant);

	// Necessário antes de utilizar a biblioteca COM da Microsoft
	SetConsoleTextAttribute(hOut, WHITE);
	printf("Iniciando ambiente COM...\n");
	CoInitialize(NULL);

	// Instancia a interface IOPCServer e pega o apontador para a mesma
	printf("Instanciando Servidor OPC da Matrikon para simulacao...\n");
	pIOPCServer = InstantiateServer();

	// Adiciona o grupo e pega o handle para interface IOPCItemMgt
	printf("Adicionando grupo com estado INACTIVE...\n");
	AddTheGroup(pIOPCServer, pDataIOPCItemMgt,  pParametersIOPCItemMgt, hServerGroup);

	// Adiciona o item
	AddAllItems(pDataIOPCItemMgt, pParametersIOPCItemMgt, hServerItem);
	
	int bRet;
	MSG msg;
	int ticks1, ticks2;
	
	// Utiliza o método IOPCDataCallback para iniciar uma leitura assíncrona via callback
	// Primeiro instanciamos um objeto SOCDataCallback e ajustamos seu contador de referências.
	// Depois chamamos uma função wrapper para preparar o callback
	IConnectionPoint* pIConnectionPoint = NULL;			// Apontador para interface IConnectionPoint
	DWORD dwCookie = 0;
	SOCDataCallback* pSOCDataCallback = new SOCDataCallback();
	pSOCDataCallback->AddRef();

	printf("Preparando a conexao callback IConnectionPoint...\n");
	SetDataCallback(pDataIOPCItemMgt, pSOCDataCallback, pIConnectionPoint, &dwCookie);

	// Altera o grupo para o estado ACTIVE para que possa receber as notificações de callback do servidor
	printf("Alterando o estado do grupo para ACTIVE...\n");
	SetGroupActive(pDataIOPCItemMgt);

	// Loop para processar as notificações de callback do servidor
	ticks1 = GetTickCount();
	do {
		
		bRet = GetMessage(&msg, NULL, 0, 0);
		if (!bRet) {
			SetConsoleTextAttribute(hOut, HLRED);
			printf("Falha ao recuperar mensagem! Codigo de erro = %d\n", GetLastError());
			exit(0);
		}
		TranslateMessage(&msg);
		DispatchMessage(&msg);
		ReadSlot();
		int st = WaitForSingleObject(hEvent, 100);
		if (st == WAIT_OBJECT_0) {
			WaitForSingleObject(hMutex, INFINITE);
			SetConsoleTextAttribute(hOut, CYAN);
			printf("Cliente OPC - Mensagem com parametros eviada: %05i/%.2f/%05i\n\n", pressureSetPoint, temperatureSetPoint, gasVolume);
			GenerateVar(&tempVariant, VT_UI2, (void*)&pressureSetPoint);
			writeItem(pParametersIOPCItemMgt, hServerItem[4], tempVariant);
			GenerateVar(&tempVariant, VT_R4, (void*)&temperatureSetPoint);
			writeItem(pParametersIOPCItemMgt, hServerItem[5], tempVariant);
			GenerateVar(&tempVariant, VT_UI4, (void*)&gasVolume);
			writeItem(pParametersIOPCItemMgt, hServerItem[6], tempVariant);
			ReleaseMutex(hMutex);
		}
		ResetEvent(hEvent);
	} while (true);

	// Cancela o callback e libera a referência
	SetConsoleTextAttribute(hOut, WHITE);
	printf("Cancelando notificacoes IOPCDataCallback...\n");
	CancelDataCallback(pIConnectionPoint, dwCookie);
	//pIConnectionPoint->Release();
	pSOCDataCallback->Release();

	// Remove o item
	printf("Removendo item...\n");
	for (int i = 0; i < 4; i++)	{
		RemoveItem(pDataIOPCItemMgt, hServerItem[i]);
	}
	for (int i = 0; i < 3; i++) {
		RemoveItem(pParametersIOPCItemMgt, hServerItem[i]);
	}

	// Remove o grupo:
	SetConsoleTextAttribute(hOut, WHITE);
	printf("Removendo grupo...\n");
	pDataIOPCItemMgt->Release();
	pParametersIOPCItemMgt->Release();
	RemoveGroup(pIOPCServer, hServerGroup[0]);
	RemoveGroup(pIOPCServer, hServerGroup[1]);

	// Libera a referência da interface
	SetConsoleTextAttribute(hOut, WHITE);
	printf("Removendo servidor OPC...\n");
	pIOPCServer->Release();

	// Encerra o ambiente COM
	SetConsoleTextAttribute(hOut, WHITE);
	printf("Encerrando ambiente COM...\n");
	CoUninitialize();

	return 0;
}

DWORD WINAPI SocketServer() {
	WSADATA     wsaData;

	SOCKET serverSocket,
		   clientSocket;

	PADDRINFOA result = NULL;
	fd_set set;
	struct timeval timeout;

	std::ostringstream ss;

	ADDRINFOA hints;
	int status, response;
	char code[3],
		 oldSeqNumber[7],
		 resPressure[10],
		 resLevel[10];
	char buffer[BUFFERLEN];
	
	status = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (status < 0) {
		SetConsoleTextAttribute(hOut, HLRED);
		printf("Erro na inicizalizacao do ambiente WSA: %d\n", status);
		return EXITERROR;
	}

	// Inicializa a estrutura do socket
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	status = getaddrinfo(NULL,LISTENPORT, &hints, &result);

	printf("Criando socket TCP\n");
	serverSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (serverSocket == INVALID_SOCKET) {
		status = WSAGetLastError();
		SetConsoleTextAttribute(hOut, HLRED);
		printf("Erro na criacao do socket: %d\n", status);
		WSACleanup();
		return EXITERROR;
	}

	printf("Vinculando o socket a porta\n");
	status = bind(serverSocket, result->ai_addr, (int)result->ai_addrlen);
	if (status == SOCKET_ERROR) {
		status = WSAGetLastError();
		SetConsoleTextAttribute(hOut, HLRED);
		printf("Erro na vinculacao do socket: %d\n", status);
		WSACleanup();
		return EXITERROR;
	}

	freeaddrinfo(result);

	printf("Socket comeca a escutar\n");
	status = listen(serverSocket, 3);
	if (status == SOCKET_ERROR) {
		status = WSAGetLastError();
		SetConsoleTextAttribute(hOut, HLRED);
		printf("Erro no listening: %d\n", status);
		closesocket(serverSocket);
		WSACleanup();
		return EXITERROR;
	}

	printf("Servidor esperando conexao\n");
	clientSocket = accept(serverSocket,NULL,NULL);
	if (clientSocket == INVALID_SOCKET) {
		status = WSAGetLastError();
		SetConsoleTextAttribute(hOut, HLRED);
		printf("Erro na funcao accept do socket: %d\n", status);
		closesocket(serverSocket);
		WSACleanup();
		return EXITERROR;
	}

	
	do {
		FD_ZERO(&set);						// Limpa o set
		FD_SET(clientSocket, &set);			// Adiciona o descritor de arquivo ao set
		timeout.tv_sec = 3000;
		timeout.tv_usec = 0;
		int rv = select(clientSocket, &set, NULL, NULL, &timeout); 
		if (rv == SOCKET_ERROR) {
			printf("Socket_Error\n");
			response = 1;
		}
		else if (rv == 0) {
			printf("Timeout\n");
			response = 1;
		}
		else {
			response = recv(clientSocket, buffer, BUFFERLEN, 0);

			// Checa se recebeu alguma mensagem
			if (response > 0) { 
				if (response < BUFFERLEN)
					buffer[response] = '\0';
				strncpy_s(code, buffer, 2);
				code[2] = '\0';
				if (strcmp(code, DATAREQUEST) == 0) {
					SetConsoleTextAttribute(hOut, HLBLUE);
					printf("Servidor Socket - Mensagem de requisicao de dados recebida: %s\n\n", buffer);
					strncpy_s(oldSeqNumber, &buffer[3], 6);
					WaitForSingleObject(hMutex, INFINITE);
					sequenceNumber = increaseSequenceNumber(atoi(oldSeqNumber));
					_gcvt_s(resPressure, 10, reservatoryPressure, 5);
					_gcvt_s(resLevel, 10, reservatoryLevel, 5);

					int i;
					while ((i = strlen(resPressure)) < 6) {
						resPressure[i] = '0';
						resPressure[i + 1] = '\0';
					}

					while ((i = strlen(resLevel)) < 6) {
						resLevel[i] = '0';
						resLevel[i + 1] = '\0';
					}
					sprintf_s(buffer, "%s/%06i/%05i/%05i/%s/%s", DATAMSG, sequenceNumber, tubePressure, tubeTemperature, resPressure, resLevel);
					ReleaseMutex(hMutex);

					SetConsoleTextAttribute(hOut, HLGREEN);
					printf("Servidor Socket - Mensagem com dados enviada: %s\n\n", buffer);
					status = send(clientSocket, buffer, strlen(buffer), 0);
					if (status == SOCKET_ERROR) {
						SetConsoleTextAttribute(hOut, HLRED);
						printf("Error no envio: %d\n", WSAGetLastError());
						closesocket(clientSocket);
						WSACleanup();
						return EXITERROR;
					}
				}
				else if (strcmp(code, PARAMETERSMSG) == 0) {

					WaitForSingleObject(hMutex, INFINITE);

					SetConsoleTextAttribute(hOut, YELLOW);
					printf("Servidor Socket - Mensagem com parametros de controle recebida: %s\n\n", buffer);
					std::string str = buffer;
					sequenceNumber = increaseSequenceNumber(std::stoi(str.substr(3, 8)));
					pressureSetPoint = std::stoi(str.substr(10, 14));
					temperatureSetPoint = std::stof(str.substr(16, 21));
					gasVolume = std::stoi(str.substr(23, 27));
					sprintf_s(buffer, "%s/%06i", ACKCODE, sequenceNumber);
					SetConsoleTextAttribute(hOut, HLGREEN);
					printf("Servidor Socket - Mensagem ACK enviada: %s\n\n", buffer);

					ReleaseMutex(hMutex);
					SetEvent(hEvent);
					status = send(clientSocket, buffer, strlen(buffer), 0);
					if (status == SOCKET_ERROR) {
						SetConsoleTextAttribute(hOut, HLRED);
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
				SetConsoleTextAttribute(hOut, WHITE);
				printf("Encerrando a conexao\n");
			}
			else {
				SetConsoleTextAttribute(hOut, HLRED);
				printf("Error no recebimento dos dados: %d\n", WSAGetLastError());
				SetConsoleTextAttribute(hOut, WHITE);
				printf("Esperando conexao...\n");
				status = listen(serverSocket, 3);
				if (status == SOCKET_ERROR) {
					status = WSAGetLastError();
					SetConsoleTextAttribute(hOut, HLRED);
					printf("Erro no listening: %d\n", status);
					closesocket(serverSocket);
					WSACleanup();
					return EXITERROR;
				}
				printf("Esperando conexao...\n");
				clientSocket = accept(serverSocket, NULL, NULL);
				if (clientSocket == INVALID_SOCKET) {
					status = WSAGetLastError();
					printf("Erro na funcao accept do socket: %d\n", status);
					closesocket(serverSocket);
					WSACleanup();
					return EXITERROR;
				}
				response = 1;
			}

		}
		
	} while (response > 0);

	// Desabilita o socket
	status = shutdown(clientSocket, SD_SEND);
	if (status == SOCKET_ERROR) {
		printf("Shutdown falhou: %d\n", WSAGetLastError());
		closesocket(clientSocket);
		WSACleanup();
		return EXITERROR;
	}

	// Fecha os sockets 
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

BOOL ReadSlot()
{
	DWORD cbMessage,
		  cMessage,
		  cbRead,
		  cAllMessages;
	BOOL fResult;
	LPTSTR lpszBuffer;
	TCHAR achID[80];
	HANDLE hEvent;
	OVERLAPPED ov;

	cbMessage = cMessage = cbRead = 0;

	hEvent = CreateEvent(NULL, FALSE, FALSE, TEXT("ExampleSlot"));
	if (NULL == hEvent)
		return FALSE;
	ov.Offset = 0;
	ov.OffsetHigh = 0;
	ov.hEvent = hEvent;

	fResult = GetMailslotInfo(hSlot,		 // Handle para o mailslot 
			  (LPDWORD)NULL,                 // Sem tamanho máximo de mensagem 
		  	  &cbMessage,                    // Tamanho da próxima mensagem 
			  &cMessage,                     // Número de mensagens 
			  (LPDWORD)NULL);                // Sem timeout de leitura 

	if (!fResult)
	{
		printf("GetMailslotInfo falhou com %d.\n", GetLastError());
		return FALSE;
	}

	if (cbMessage == MAILSLOT_NO_MESSAGE)
	{
		printf("Aguardando mensagem...\n");
		return TRUE;
	}

	cAllMessages = cMessage;

	// Loop para recuperar todas as mensagens
	while (cMessage != 0)
	{
		// Cria string com o número de sequência da mensagem 
		StringCchPrintf((LPTSTR)achID,
			80,
			TEXT("\Mensagem #%d de %d\n"),
			cAllMessages - cMessage + 1,
			cAllMessages);

		// Aloca memória para a mensagem
		lpszBuffer = (LPTSTR)GlobalAlloc(GPTR,
			lstrlen((LPTSTR)achID) * sizeof(TCHAR) + cbMessage);

		if (NULL == lpszBuffer)
			return FALSE;

		lpszBuffer[0] = '\0';

		fResult = ReadFile(hSlot,
				  lpszBuffer,
				  cbMessage,
				  &cbRead,
				  &ov);

		if (!fResult)
		{
			printf("ReadFile falhou com %d.\n", GetLastError());
			GlobalFree((HGLOBAL)lpszBuffer);
			return FALSE;
		}
		WaitForSingleObject(hMutex, INFINITE);
		SetConsoleTextAttribute(hOut, PINK);
		printf("Cliente OPC - Mensagem de dados recebida: %s\n\n", lpszBuffer);
		char* msg = (char*)lpszBuffer;
		parseMessage(msg);
		ReleaseMutex(hMutex);
	
		GlobalFree((HGLOBAL)lpszBuffer);

		fResult = GetMailslotInfo(hSlot,		// Handle para o mailslot  
				  (LPDWORD)NULL,                // Sem tamanho máximo de mensagem  
				  &cbMessage,                   // Tamanho da próxima mensagem 
				  &cMessage,                    // Número de mensagens 
				  (LPDWORD)NULL);               // Sem timeout de leitura 

		if (!fResult)
		{
			printf("GetMailslotInfo falhou com (%d)\n", GetLastError());
			return FALSE;
		}
	}
	CloseHandle(hEvent);
	return TRUE;
}

void parseMessage(char* msg) {
	char* part = strtok(msg, "/");
	
	tubePressure = atoi(part);

	part = strtok(NULL, "/");
	tubeTemperature = atoi(part);

	part = strtok(NULL, "/");
	reservatoryPressure = atof(part);

	part = strtok(NULL, "/");
	reservatoryLevel = atoi(part);
	
}

bool GenerateVar(VARIANT* var, VARTYPE var_type, void* var_value){
	// Atribui var_type
	var->vt = var_type;                
	// Typecast de void* pra ponteiro do tipo específico e depois desreferencia
	switch (var_type & ~VT_ARRAY){
		case VT_I1:
			var->iVal = *static_cast<char*>(var_value);	break;
		case VT_I2:
			var->intVal = *static_cast<short*>(var_value);	break;
		case VT_I4:
			var->intVal = *static_cast<long*>(var_value);	break;
		case VT_UI1:
			var->uiVal = *static_cast<unsigned char*>(var_value);	break;
		case VT_UI2:
			var->ulVal = *static_cast<unsigned short*>(var_value);	break;
		case VT_UI4:
			var->ulVal = *static_cast<unsigned long*>(var_value);	break;
		case VT_R4:
			var->fltVal = *static_cast<float*>(var_value);	break;
		case VT_R8:
			var->dblVal = *static_cast<double*>(var_value);	break;
	}
	return(true);
}



