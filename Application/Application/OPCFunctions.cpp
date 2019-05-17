#include "OPCFunctions.h"

wchar_t OPC_SERVER_NAME[] = L"Matrikon.OPC.Simulation.1";
const wchar_t* ITEMS_ID[] = { L"Random.UInt1", L"Random.UInt2", L"Random.Real4", L"Random.Saw-toothed Waves", L"Bucket Brigade.UInt2", L"Bucket Brigade.Real4", L"Bucket Brigade.UInt4" };

IOPCServer* InstantiateServer()
{
	CLSID CLSID_OPCServer;
	HRESULT hr;

	// get the CLSID from the OPC Server Name:
	hr = CLSIDFromString(OPC_SERVER_NAME, &CLSID_OPCServer);
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
void AddTheGroup(IOPCServer* pIOPCServer, IOPCItemMgt* &pDataIOPCItemMgt, IOPCItemMgt* &pParametersIOPCItemMgt, OPCHANDLE* hServerGroup){
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
		/*phServerGroup*/&hServerGroup[0],
		&dwUpdateRate,
		/*riid*/ IID_IOPCItemMgt,
		/*ppUnk*/ (IUnknown**)&pDataIOPCItemMgt);
	_ASSERT(!FAILED(hr));

	 hr = pIOPCServer->AddGroup(/*szName*/ L"Group2",
		/*bActive*/ FALSE,
		/*dwRequestedUpdateRate*/ 1000,
		/*hClientGroup*/ hClientGroup+1,
		/*pTimeBias*/ 0,
		/*pPercentDeadband*/ 0,
		/*dwLCID*/1,
		/*phServerGroup*/&hServerGroup[1],
		&dwUpdateRate,
		/*riid*/ IID_IOPCItemMgt,
		/*ppUnk*/ (IUnknown**)&pParametersIOPCItemMgt);
	_ASSERT(!FAILED(hr));
}


void AddAllItems(IOPCItemMgt* pDataIOPCItemMgt, IOPCItemMgt* pParametersIOPCItemMgt, OPCHANDLE* hServerItem)
{
	HRESULT hr;
	OPCITEMDEF DataItemArray[4], ParameterItemArray[3];
	// Array of items to add:
	DataItemArray[0] = {
		/*szAccessPath*/(LPWSTR)L"",
		/*szItemID*/ (LPWSTR)ITEMS_ID[0],
		/*bActive*/ TRUE,
		/*hClient*/(OPCHANDLE)1,
		/*dwBlobSize*/ 0,
		/*pBlob*/ NULL,
		/*vtRequestedDataType*/ VT_UI1,
		/*wReserved*/0
		};
	DataItemArray[1] = {
		/*szAccessPath*/(LPWSTR)L"",
		/*szItemID*/ (LPWSTR)ITEMS_ID[1],
		/*bActive*/ TRUE,
		/*hClient*/(OPCHANDLE)2,
		/*dwBlobSize*/ 0,
		/*pBlob*/ NULL,
		/*vtRequestedDataType*/ VT_UI2,
		/*wReserved*/0
	};
	for (int i = 2; i < 4; i++) {
		// Array of items to add:
		DataItemArray[i] = {
			/*szAccessPath*/(LPWSTR)L"",
			/*szItemID*/ (LPWSTR)ITEMS_ID[i],
			/*bActive*/ TRUE,
			/*hClient*/(OPCHANDLE)i + 1,
			/*dwBlobSize*/ 0,
			/*pBlob*/ NULL,
			/*vtRequestedDataType*/ VT,
			/*wReserved*/0
		};
	}
	
	ParameterItemArray[0] = {
		/*szAccessPath*/(LPWSTR)L"",
		/*szItemID*/ (LPWSTR)ITEMS_ID[4],
		/*bActive*/ TRUE,
		/*hClient*/(OPCHANDLE) 4,
		/*dwBlobSize*/ 0,
		/*pBlob*/ NULL,
		/*vtRequestedDataType*/ VT_UI2,
		/*wReserved*/0
	};

	ParameterItemArray[1] = {
		/*szAccessPath*/(LPWSTR)L"",
		/*szItemID*/ (LPWSTR)ITEMS_ID[5],
		/*bActive*/ TRUE,
		/*hClient*/(OPCHANDLE)5,
		/*dwBlobSize*/ 0,
		/*pBlob*/ NULL,
		/*vtRequestedDataType*/ VT,
		/*wReserved*/0
	};

	ParameterItemArray[2] = {
		/*szAccessPath*/(LPWSTR)L"",
		/*szItemID*/ (LPWSTR)ITEMS_ID[6],
		/*bActive*/ TRUE,
		/*hClient*/(OPCHANDLE)6,
		/*dwBlobSize*/ 0,
		/*pBlob*/ NULL,
		/*vtRequestedDataType*/ VT_UI4,
		/*wReserved*/0
	};

	//Add Result:
	OPCITEMRESULT* pAddResult = NULL;
	HRESULT* pErrors = NULL;

	// Add an Item to the previous Group:
	hr = pDataIOPCItemMgt->AddItems(4, DataItemArray, &pAddResult, &pErrors);
	if (hr != S_OK) {
		printf("Failed call to AddItems function. Error code = %x\n", hr);
		exit(0);
	}
	for (int i = 0; i < 4; i++) {
		// Server handle for the added item:
		hServerItem[i] = pAddResult[i].hServer;
	}

	hr = pParametersIOPCItemMgt->AddItems(3, ParameterItemArray, &pAddResult, &pErrors);
	if (hr != S_OK) {
		printf("Failed call to AddItems function. Error code = %x\n", hr);
		exit(0);
	}
	for (int i = 0; i < 3; i++) {
		// Server handle for the added item:
		hServerItem[i+4] = pAddResult[i].hServer;
	}

	// release memory allocated by the server:
	CoTaskMemFree(pAddResult->pBlob);

	CoTaskMemFree(pAddResult);
	pAddResult = NULL;

	CoTaskMemFree(pErrors);
	pErrors = NULL;
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
void RemoveGroup(IOPCServer* pIOPCServer, OPCHANDLE hServerGroup){
	// Remove the group:
	HRESULT hr = pIOPCServer->RemoveGroup(hServerGroup, FALSE);
	if (hr != S_OK) {
		if (hr == OPC_S_INUSE)
			printf("Failed to remove OPC group: object still has references to it.\n");
		else printf("Failed to remove OPC group. Error code = %x\n", hr);
		exit(0);
	}
}

void writeItem(IOPCItemMgt* pIOPCItemMgt, OPCHANDLE hServerItem, VARIANT tempVariant){

	IOPCSyncIO* pIOPCSyncIO;
	pIOPCItemMgt->QueryInterface(__uuidof(pIOPCSyncIO), (void**)&pIOPCSyncIO);

	HRESULT* pErrors = NULL; //to store error code(s)
	HRESULT hr = pIOPCSyncIO->Write(1, &hServerItem, &tempVariant, &pErrors);

	_ASSERT(!hr);

	//Release memeory allocated by the OPC server:
	CoTaskMemFree(pErrors);
	pErrors = NULL;

	// release the reference to the IOPCSyncIO interface:
	pIOPCSyncIO->Release();
}