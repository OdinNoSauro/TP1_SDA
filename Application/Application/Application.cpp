// Application.cpp : Este arquivo contém a função 'main'. A execução do programa começa e termina ali.

using namespace std;

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <stdio.h>
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

// Casting para terceiro e sexto parâmetros da função _beginthreadex
typedef unsigned (WINAPI *CAST_FUNCTION)(LPVOID);
typedef unsigned *CAST_LPDWORD;

wchar_t OPC_SERVER_NAME[] = L"Matrikon.OPC.Simulation.1";
wchar_t ITEM_ID[] = L"Saw-toothed Waves.Real4";
const wchar_t* ITEMS_ID[] = { L"Random.UInt1", L"Random.UInt2", L"Random.Real4", L"Random.Saw-toothed Waves", L"Bucket Brigade.UInt2", L"Bucket Brigade.Real4", L"Bucket Brigade.UInt4" };

#define VT VT_R4
#define	LISTENPORT "3980"
#define TAMBUF  "20"
#define ACKCODE "33"
#define DATAREQUEST "55"
#define DATAMSG "11"
#define PARAMETERSMSG "00"
#define BUFFERLEN 45
#define VT VT_R4
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
	  reservatoryPressure= 5.22f,
	  reservatoryLevel = 3.25f;
			
UINT OPC_DATA_TIME = RegisterClipboardFormat(_T("OPCSTMFORMATDATATIME"));

DWORD WINAPI OPCClient();	    // declaração da função
DWORD WINAPI SocketServer();	// declaração da função
int increaseSequenceNumber(int previousSequenceNumber);
IOPCServer *InstantiateServer(wchar_t ServerName[]);
void AddTheGroup(IOPCServer* pIOPCServer, IOPCItemMgt* &pIOPCItemMgt, OPCHANDLE& hServerGroup);
void AddTheItem(IOPCItemMgt* pIOPCItemMgt, OPCHANDLE& hServerItem);
void AddAllItems(IOPCItemMgt* pIOPCItemMgt, OPCHANDLE* hServerItem);
void ReadItem(IUnknown* pGroupIUnknown, OPCHANDLE hServerItem, VARIANT& varValue);
void RemoveItem(IOPCItemMgt* pIOPCItemMgt, OPCHANDLE hServerItem);
void RemoveGroup(IOPCServer* pIOPCServer, OPCHANDLE hServerGroup);

int main(int argc, char **argv) {
	DWORD dwReturn,
		dwExitCode,
		dwThreadId;
	hMutex = CreateMutex(NULL, FALSE, (LPCWSTR)"DataMutex");

	hOPCClientThread = (HANDLE)_beginthreadex(
		NULL,
		0,
		(CAST_FUNCTION)OPCClient,	// casting necessário
		NULL,
		0,
		(CAST_LPDWORD)&dwThreadId	// casting necessário
	);

	/*hSockerServerThread = (HANDLE)_beginthreadex(
		NULL,
		0,
		(CAST_FUNCTION)SocketServer,	// casting necessário
		NULL,
		0,
		(CAST_LPDWORD)&dwThreadId	// casting necessário
	);*/

	//const HANDLE pointerHandles[2] = { hOPCClientThread, hSockerServerThread};
	//dwReturn = WaitForMultipleObjects(2, pointerHandles, true, INFINITE);
	dwReturn = WaitForSingleObject(hOPCClientThread, INFINITE);

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

	char buf[100];

	// Have to be done before using microsoft COM library:
	printf("Initializing the COM environment...\n");
	CoInitialize(NULL);

	// Let's instantiante the IOPCServer interface and get a pointer of it:
	printf("Instantiating the MATRIKON OPC Server for Simulation...\n");
	pIOPCServer = InstantiateServer(OPC_SERVER_NAME);

	// Add the OPC group the OPC server and get an handle to the IOPCItemMgt
	//interface:
	printf("Adding a group in the INACTIVE state for the moment...\n");
	AddTheGroup(pIOPCServer, pIOPCItemMgt, hServerGroup);

	// Add the OPC item. First we have to convert from wchar_t* to char*
	// in order to print the item name in the console.
	size_t m;
	wcstombs_s(&m, buf, 100, ITEM_ID, _TRUNCATE);
	printf("Adding the item %s to the group...\n", buf);
	AddAllItems(pIOPCItemMgt, hServerItem);
	
	int bRet;
	MSG msg;
	DWORD ticks1, ticks2;
	ticks1 = GetTickCount();
	
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

	printf("Waiting for IOPCDataCallback notifications during 10 seconds...\n");
	do {
		bRet = GetMessage(&msg, NULL, 0, 0);
		if (!bRet) {
			printf("Failed to get windows message! Error code = %d\n", GetLastError());
			exit(0);
		}
		WaitForSingleObject(hMutex, INFINITE);
		pSOCDataCallback->updateData(&tubePressure, &tubeTemperature, &reservatoryLevel, &reservatoryPressure);
		printf("Mensagem da dados do servidor OPC: %05i/%05i/%.2f/%.2f", tubePressure, tubeTemperature, reservatoryPressure, reservatoryLevel);
		ReleaseMutex(hMutex);
		TranslateMessage(&msg); // This call is not really needed ...
		DispatchMessage(&msg);  // ... but this one is!
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

IOPCServer* InstantiateServer(wchar_t ServerName[])
{
	CLSID CLSID_OPCServer;
	HRESULT hr;

	// get the CLSID from the OPC Server Name:
	hr = CLSIDFromString(ServerName, &CLSID_OPCServer);
	_ASSERT(!FAILED(hr));

	// queue of the class instances to create
	LONG cmq = 1; // nbr of class instance to create.
	MULTI_QI queue[1] =
	{ {&IID_IOPCServer,
	NULL,
	0} };

	//Server info:
	//COSERVERINFO CoServerInfo =
	//{
	//	/*dwReserved1*/ 0,
	//	/*pwszName*/ REMOTE_SERVER_NAME,
	//	/*COAUTHINFO*/  NULL,
	//	/*dwReserved2*/ 0
	//}; 

	// create an instance of the IOPCServer
	hr = CoCreateInstanceEx(CLSID_OPCServer, NULL, CLSCTX_SERVER,
		/*&CoServerInfo*/NULL, cmq, queue);
	_ASSERT(!hr);

	// return a pointer to the IOPCServer interface:
	return(IOPCServer*)queue[0].pItf;
}

/////////////////////////////////////////////////////////////////////
// Add group "Group1" to the Server whose IOPCServer interface
// is pointed by pIOPCServer. 
// Returns a pointer to the IOPCItemMgt interface of the added group
// and a server opc handle to the added group.
//
void AddTheGroup(IOPCServer* pIOPCServer, IOPCItemMgt* &pIOPCItemMgt,
	OPCHANDLE& hServerGroup)
{
	DWORD dwUpdateRate = 0;
	OPCHANDLE hClientGroup = 0;

	// Add an OPC group and get a pointer to the IUnknown I/F:
	HRESULT hr = pIOPCServer->AddGroup(/*szName*/ L"Group1",
		/*bActive*/ FALSE,
		/*dwRequestedUpdateRate*/ 1000,
		/*hClientGroup*/ hClientGroup,
		/*pTimeBias*/ 0,
		/*pPercentDeadband*/ 0,
		/*dwLCID*/0,
		/*phServerGroup*/&hServerGroup,
		&dwUpdateRate,
		/*riid*/ IID_IOPCItemMgt,
		/*ppUnk*/ (IUnknown**)&pIOPCItemMgt);
	_ASSERT(!FAILED(hr));
}


//////////////////////////////////////////////////////////////////
// Add the Item ITEM_ID to the group whose IOPCItemMgt interface
// is pointed by pIOPCItemMgt pointer. Return a server opc handle
// to the item.

void AddTheItem(IOPCItemMgt* pIOPCItemMgt, OPCHANDLE& hServerItem)
{
	HRESULT hr;

	// Array of items to add:
	OPCITEMDEF ItemArray[1] =
	{ {
			/*szAccessPath*/(LPWSTR) L"",
			/*szItemID*/ ITEM_ID,
			/*bActive*/ TRUE,
			/*hClient*/ 1,
			/*dwBlobSize*/ 0,
			/*pBlob*/ NULL,
			/*vtRequestedDataType*/ VT,
			/*wReserved*/0
			} };

	//Add Result:
	OPCITEMRESULT* pAddResult = NULL;
	HRESULT* pErrors = NULL;

	// Add an Item to the previous Group:
	hr = pIOPCItemMgt->AddItems(1, ItemArray, &pAddResult, &pErrors);
	if (hr != S_OK) {
		printf("Failed call to AddItems function. Error code = %x\n", hr);
		exit(0);
	}

	// Server handle for the added item:
	hServerItem = pAddResult[0].hServer;

	// release memory allocated by the server:
	CoTaskMemFree(pAddResult->pBlob);

	CoTaskMemFree(pAddResult);
	pAddResult = NULL;

	CoTaskMemFree(pErrors);
	pErrors = NULL;
}

void AddAllItems(IOPCItemMgt* pIOPCItemMgt, OPCHANDLE* hServerItem)
{
	HRESULT hr; 
	OPCITEMDEF ItemArray[7];
	for (int i = 0; i < 7; i++) {
		// Array of items to add:
		ItemArray[i] = {
			/*szAccessPath*/(LPWSTR)L"",
			/*szItemID*/ (LPWSTR)ITEMS_ID[i],
			/*bActive*/ TRUE,
			/*hClient*/ i + 1,
			/*dwBlobSize*/ 0,
			/*pBlob*/ NULL,
			/*vtRequestedDataType*/ VT,
			/*wReserved*/0
		};
	}
	//Add Result:
	OPCITEMRESULT* pAddResult = NULL;
	HRESULT* pErrors = NULL;

	// Add an Item to the previous Group:
	hr = pIOPCItemMgt->AddItems(7, ItemArray, &pAddResult, &pErrors);
	if (hr != S_OK) {
		printf("Failed call to AddItems function. Error code = %x\n", hr);
		exit(0);
	}
	for (int i = 0; i < 7; i++) {
		// Server handle for the added item:
		hServerItem[i] = pAddResult[i].hServer;
	}

	// release memory allocated by the server:
	CoTaskMemFree(pAddResult->pBlob);

	CoTaskMemFree(pAddResult);
	pAddResult = NULL;

	CoTaskMemFree(pErrors);
	pErrors = NULL;	
}


///////////////////////////////////////////////////////////////////////////////
// Read from device the value of the item having the "hServerItem" server 
// handle and belonging to the group whose one interface is pointed by
// pGroupIUnknown. The value is put in varValue. 
//
void ReadItem(IUnknown* pGroupIUnknown, OPCHANDLE hServerItem, VARIANT& varValue)
{
	// value of the item:
	OPCITEMSTATE* pValue = NULL;

	//get a pointer to the IOPCSyncIOInterface:
	IOPCSyncIO* pIOPCSyncIO;
	pGroupIUnknown->QueryInterface(__uuidof(pIOPCSyncIO), (void**)&pIOPCSyncIO);

	// read the item value from the device:
	HRESULT* pErrors = NULL; //to store error code(s)
	HRESULT hr = pIOPCSyncIO->Read(OPC_DS_DEVICE, 1, &hServerItem, &pValue, &pErrors);
	_ASSERT(!hr);
	_ASSERT(pValue != NULL);

	varValue = pValue[0].vDataValue;

	//Release memeory allocated by the OPC server:
	CoTaskMemFree(pErrors);
	pErrors = NULL;

	CoTaskMemFree(pValue);
	pValue = NULL;

	// release the reference to the IOPCSyncIO interface:
	pIOPCSyncIO->Release();
}

///////////////////////////////////////////////////////////////////////////
// Remove the item whose server handle is hServerItem from the group
// whose IOPCItemMgt interface is pointed by pIOPCItemMgt
//
void RemoveItem(IOPCItemMgt* pIOPCItemMgt, OPCHANDLE hServerItem)
{
	// server handle of items to remove:
	OPCHANDLE hServerArray[1];
	hServerArray[0] = hServerItem;

	//Remove the item:
	HRESULT* pErrors; // to store error code(s)
	HRESULT hr = pIOPCItemMgt->RemoveItems(1, hServerArray, &pErrors);
	_ASSERT(!hr);

	//release memory allocated by the server:
	CoTaskMemFree(pErrors);
	pErrors = NULL;
}

////////////////////////////////////////////////////////////////////////
// Remove the Group whose server handle is hServerGroup from the server
// whose IOPCServer interface is pointed by pIOPCServer
//
void RemoveGroup(IOPCServer* pIOPCServer, OPCHANDLE hServerGroup)
{
	// Remove the group:
	HRESULT hr = pIOPCServer->RemoveGroup(hServerGroup, FALSE);
	if (hr != S_OK) {
		if (hr == OPC_S_INUSE)
			printf("Failed to remove OPC group: object still has references to it.\n");
		else printf("Failed to remove OPC group. Error code = %x\n", hr);
		exit(0);
	}
}

