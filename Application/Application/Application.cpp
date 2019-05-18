// Application.cpp : Este arquivo contém a função 'main'. A execução do programa começa e termina ali.

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
#include <iomanip>      // needed to use manipulators with parameters (precision, width)
#include <atlbase.h>    // required for using the "_T" macro
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

DWORD WINAPI OPCClient();	    // declaração da função
DWORD WINAPI SocketServer();	// declaração da função
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
		0,                             // no maximum message size 
		MAILSLOT_WAIT_FOREVER,         // no time-out for operations 
		NULL); // default security

	if (hSlot == INVALID_HANDLE_VALUE)
	{
		printf("CreateMailslot failed with %d\n", GetLastError());
	}

	hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hOut == INVALID_HANDLE_VALUE)
		printf("Erro ao obter handle para a saída da console\n");

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

	CloseHandle(hMutex);

	dwReturn = GetExitCodeThread(hOPCClientThread, &dwExitCode);
	CloseHandle(hOPCClientThread);	// apaga referência ao objeto
	dwReturn = GetExitCodeThread(hSockerServerThread, &dwExitCode);
	CloseHandle(hSockerServerThread);	
	
	return 0;
}

DWORD WINAPI OPCClient() {

	IOPCServer* pIOPCServer = NULL;   //pointer to IOPServer interface
	IOPCItemMgt* pDataIOPCItemMgt = NULL; //pointer to IOPCItemMgt interface
	IOPCItemMgt* pParametersIOPCItemMgt = NULL; //pointer to IOPCItemMgt interface

	OPCHANDLE hServerGroup[2];    // server handle to the group
	OPCHANDLE hServerItem[7];  // server handle to the item

	DWORD dwReturn,
		dwExitCode,
		dwThreadId;

	char buf[100];
	VARIANT tempVariant;
	VariantInit(&tempVariant);

	// Have to be done before using microsoft COM library:
	SetConsoleTextAttribute(hOut, WHITE);
	printf("Initializing the COM environment...\n");
	CoInitialize(NULL);

	// Let's instantiante the IOPCServer interface and get a pointer of it:
	printf("Instantiating the MATRIKON OPC Server for Simulation...\n");
	pIOPCServer = InstantiateServer();

	// Add the OPC group the OPC server and get an handle to the IOPCItemMgt
	//interface:
	printf("Adding a group in the INACTIVE state for the moment...\n");
	AddTheGroup(pIOPCServer, pDataIOPCItemMgt,  pParametersIOPCItemMgt, hServerGroup);

	// Add the OPC item. First we have to convert from wchar_t* to char*
	// in order to print the item name in the console.
	AddAllItems(pDataIOPCItemMgt, pParametersIOPCItemMgt, hServerItem);
	
	int bRet;
	MSG msg;
	int ticks1, ticks2;
	
	// Establish a callback asynchronous read by means of the IOPCDataCallback
	// (OPC DA 2.0) method. We first instantiate a new SOCDataCallback object and
	// adjusts its reference count, and then call a wrapper function to
	// setup the callback.
	IConnectionPoint* pIConnectionPoint = NULL; //pointer to IConnectionPoint Interface
	DWORD dwCookie = 0;
	SOCDataCallback* pSOCDataCallback = new SOCDataCallback();
	pSOCDataCallback->AddRef();

	printf("Setting up the IConnectionPoint callback connection...\n");
	SetDataCallback(pDataIOPCItemMgt, pSOCDataCallback, pIConnectionPoint, &dwCookie);

	// Change the group to the ACTIVE state so that we can receive the
	// server´s callback notification
	printf("Changing the group state to ACTIVE...\n");
	SetGroupActive(pDataIOPCItemMgt);

	// Enter again a message pump in order to process the server´s callback
	// notifications, for the same reason explained before.
	ticks1 = GetTickCount();
	do {
		
		bRet = GetMessage(&msg, NULL, 0, 0);
		if (!bRet) {
			SetConsoleTextAttribute(hOut, HLRED);
			printf("Failed to get windows message! Error code = %d\n", GetLastError());
			exit(0);
		}
		TranslateMessage(&msg); // This call is not really needed ...
		DispatchMessage(&msg);  // ... but this one is!
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

	// Cancel the callback and release its reference
	SetConsoleTextAttribute(hOut, WHITE);
	printf("Cancelling the IOPCDataCallback notifications...\n");
	CancelDataCallback(pIConnectionPoint, dwCookie);
	//pIConnectionPoint->Release();
	pSOCDataCallback->Release();

	// Remove the OPC item:
	printf("Removing the OPC item...\n");
	for (int i = 0; i < 4; i++)	{
		RemoveItem(pDataIOPCItemMgt, hServerItem[i]);
	}
	for (int i = 0; i < 3; i++) {
		RemoveItem(pParametersIOPCItemMgt, hServerItem[i]);
	}

	// Remove the OPC group:
	SetConsoleTextAttribute(hOut, WHITE);
	printf("Removing the OPC group object...\n");
	pDataIOPCItemMgt->Release();
	pParametersIOPCItemMgt->Release();
	RemoveGroup(pIOPCServer, hServerGroup[0]);
	RemoveGroup(pIOPCServer, hServerGroup[1]);

	// release the interface references:
	SetConsoleTextAttribute(hOut, WHITE);
	printf("Removing the OPC server object...\n");
	pIOPCServer->Release();

	//close the COM library:
	SetConsoleTextAttribute(hOut, WHITE);
	printf("Releasing the COM environment...\n");
	CoUninitialize();

	return 0;
}

DWORD WINAPI SocketServer() {
	WSADATA     wsaData;
	SOCKET serverSocket;
	SOCKET clientSocket;
	PADDRINFOA result = NULL;
	fd_set set;
	struct timeval timeout;

	

	std::ostringstream ss;

	ADDRINFOA hints;
	int status, response;
	char code[3], oldSeqNumber[7], resPressure[10], resLevel[10];
	char buffer[BUFFERLEN];
	
	status = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (status < 0) {
		SetConsoleTextAttribute(hOut, HLRED);
		printf("Erro na inicizalização do ambiente WSA: %d\n", status);
		return EXITERROR;
	
	}

	//Inicializando Estrutura do socket
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
		printf("Erro na criação do socket: %d\n", status);
		WSACleanup();
		return EXITERROR;
	}

	printf("Vinculando o socket a porta\n");
	status = bind(serverSocket, result->ai_addr, (int)result->ai_addrlen);
	if (status == SOCKET_ERROR) {
		status = WSAGetLastError();
		SetConsoleTextAttribute(hOut, HLRED);
		printf("Erro na vinculação do socket: %d\n", status);
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

	printf("Servidor Esperando conexao\n");
	clientSocket = accept(serverSocket,NULL,NULL);
	if (clientSocket == INVALID_SOCKET) {
		status = WSAGetLastError();
		SetConsoleTextAttribute(hOut, HLRED);
		printf("Erro na função accept do socket: %d\n", status);
		closesocket(serverSocket);
		WSACleanup();
		return EXITERROR;
	}

	
	do {
		FD_ZERO(&set); /* clear the set */
		FD_SET(clientSocket, &set); /* add our file descriptor to the set */
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

			if (response > 0) { // recebeu alguma mensagem
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
				printf("Esperando Conexao...\n");
				status = listen(serverSocket, 3);
				if (status == SOCKET_ERROR) {
					status = WSAGetLastError();
					SetConsoleTextAttribute(hOut, HLRED);
					printf("Erro no listening: %d\n", status);
					closesocket(serverSocket);
					WSACleanup();
					return EXITERROR;
				}
				printf("Esperando Conexao...\n");
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

		}
		
	} while (response > 0);

	// Desabilitando o Socket
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



BOOL ReadSlot()
{
	DWORD cbMessage, cMessage, cbRead;
	BOOL fResult;
	LPTSTR lpszBuffer;
	TCHAR achID[80];
	DWORD cAllMessages;
	HANDLE hEvent;
	OVERLAPPED ov;

	cbMessage = cMessage = cbRead = 0;

	hEvent = CreateEvent(NULL, FALSE, FALSE, TEXT("ExampleSlot"));
	if (NULL == hEvent)
		return FALSE;
	ov.Offset = 0;
	ov.OffsetHigh = 0;
	ov.hEvent = hEvent;

	fResult = GetMailslotInfo(hSlot, // mailslot handle 
		(LPDWORD)NULL,               // no maximum message size 
		&cbMessage,                   // size of next message 
		&cMessage,                    // number of messages 
		(LPDWORD)NULL);              // no read time-out 

	if (!fResult)
	{
		printf("GetMailslotInfo failed with %d.\n", GetLastError());
		return FALSE;
	}

	if (cbMessage == MAILSLOT_NO_MESSAGE)
	{
		printf("Waiting for a message...\n");
		return TRUE;
	}

	cAllMessages = cMessage;

	while (cMessage != 0)  // retrieve all messages
	{
		// Create a message-number string. 

		StringCchPrintf((LPTSTR)achID,
			80,
			TEXT("\nMessage #%d of %d\n"),
			cAllMessages - cMessage + 1,
			cAllMessages);

		// Allocate memory for the message. 

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
			printf("ReadFile failed with %d.\n", GetLastError());
			GlobalFree((HGLOBAL)lpszBuffer);
			return FALSE;
		}
		WaitForSingleObject(hMutex, INFINITE);
		SetConsoleTextAttribute(hOut, PINK);
		printf("Cliente OPC - Mensagem recebida de Dados: %s\n\n", lpszBuffer);
		char* msg = (char*)lpszBuffer;
		parseMessage(msg);
		ReleaseMutex(hMutex);
	
		GlobalFree((HGLOBAL)lpszBuffer);

		fResult = GetMailslotInfo(hSlot,  // mailslot handle 
			(LPDWORD)NULL,               // no maximum message size 
			&cbMessage,                   // size of next message 
			&cMessage,                    // number of messages 
			(LPDWORD)NULL);              // no read time-out 

		if (!fResult)
		{
			printf("GetMailslotInfo failed (%d)\n", GetLastError());
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
	
	var->vt = var_type;                //Assign var_type
	//Typecast from void* to the specified type*, and them dereferenciate.
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



