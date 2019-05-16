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

#pragma comment(lib, "Ws2_32.lib")

HANDLE hOPCClientThread,
	   hSockerServerThread,
	   hEvent,
	   hMutex;

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
	IOPCItemMgt* pIOPCItemMgt = NULL; //pointer to IOPCItemMgt interface

	OPCHANDLE hServerGroup;    // server handle to the group
	OPCHANDLE hServerItem[7];  // server handle to the item

	DWORD dwReturn,
		dwExitCode,
		dwThreadId;

	char buf[100];
	VARIANT tempVariant;
	VariantInit(&tempVariant);

	// Have to be done before using microsoft COM library:
	printf("Initializing the COM environment...\n");
	CoInitialize(NULL);

	// Let's instantiante the IOPCServer interface and get a pointer of it:
	printf("Instantiating the MATRIKON OPC Server for Simulation...\n");
	pIOPCServer = InstantiateServer();

	// Add the OPC group the OPC server and get an handle to the IOPCItemMgt
	//interface:
	printf("Adding a group in the INACTIVE state for the moment...\n");
	AddTheGroup(pIOPCServer, pIOPCItemMgt, hServerGroup);

	// Add the OPC item. First we have to convert from wchar_t* to char*
	// in order to print the item name in the console.
	AddAllItems(pIOPCItemMgt, hServerItem);
	
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
	SetDataCallback(pIOPCItemMgt, pSOCDataCallback, pIConnectionPoint, &dwCookie);

	// Change the group to the ACTIVE state so that we can receive the
	// server´s callback notification
	printf("Changing the group state to ACTIVE...\n");
	SetGroupActive(pIOPCItemMgt);

	// Enter again a message pump in order to process the server´s callback
	// notifications, for the same reason explained before.
	ticks1 = GetTickCount();
	do {
		
		bRet = GetMessage(&msg, NULL, 0, 0);
		if (!bRet) {
			printf("Failed to get windows message! Error code = %d\n", GetLastError());
			exit(0);
		}
		TranslateMessage(&msg); // This call is not really needed ...
		DispatchMessage(&msg);  // ... but this one is!
		ReadSlot();
		int st = WaitForSingleObject(hEvent, 100);
		if (st == WAIT_OBJECT_0) {
			printf("Event received.\n");
			WaitForSingleObject(hMutex, INFINITE);
			
			ReleaseMutex(hMutex);
		}
		ResetEvent(hEvent);
		
		
	} while (true);

	// Cancel the callback and release its reference
	printf("Cancelling the IOPCDataCallback notifications...\n");
	CancelDataCallback(pIConnectionPoint, dwCookie);
	//pIConnectionPoint->Release();
	pSOCDataCallback->Release();

	// Remove the OPC item:
	printf("Removing the OPC item...\n");
	for (int i = 0; i < 7; i++)	{
		RemoveItem(pIOPCItemMgt, hServerItem[i]);
	}

	// Remove the OPC group:
	printf("Removing the OPC group object...\n");
	pIOPCItemMgt->Release();
	RemoveGroup(pIOPCServer, hServerGroup);

	// release the interface references:
	printf("Removing the OPC server object...\n");
	pIOPCServer->Release();

	//close the COM library:
	printf("Releasing the COM environment...\n");
	CoUninitialize();

	return 0;
}

DWORD WINAPI SocketServer() {
	WSADATA     wsaData;
	SOCKET serverSocket;
	SOCKET clientSocket;
	PADDRINFOA result = NULL;

	std::ostringstream ss;

	ADDRINFOA hints;
	int status, response;
	char code[3], oldSeqNumber[7], resPressure[10], resLevel[10];
	char buffer[BUFFERLEN];
	
	status = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (status < 0) {
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
		printf("Erro na criação do socket: %d\n", status);
		WSACleanup();
		return EXITERROR;
	}

	printf("Vinculando o socket a porta\n");
	status = bind(serverSocket, result->ai_addr, (int)result->ai_addrlen);
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
				while ((i = strlen(resPressure)) < 6) {
					resPressure[i] = '0';
					resPressure[i+1] = '\0';
				}

				while ((i = strlen(resLevel)) < 6) {
					resLevel[i] = '0';
					resLevel[i + 1] = '\0';
				}
				sprintf_s(buffer, "%s/%06i/%05i/%05i/%s/%s", DATAMSG, sequenceNumber, tubePressure, tubeTemperature, resPressure, resLevel);
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
				pressureSetPoint =  std::stoi(str.substr(10, 14));
				temperatureSetPoint = std::stoi(str.substr(16, 21));
				gasVolume = std::stoi(str.substr(23, 27));
				sprintf_s(buffer, "%s/%06i", ACKCODE, sequenceNumber);
				ReleaseMutex(hMutex);
				SetEvent(hEvent);

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

		printf("Mensagem recebida: %s\n", lpszBuffer);

		// Concatenate the message and the message-number string. 

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

bool GenerateVar(VARIANT* var, VARTYPE var_type, void* var_value)
{
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



