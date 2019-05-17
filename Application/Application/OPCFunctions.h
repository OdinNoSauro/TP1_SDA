#pragma once

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

#define VT VT_R4

#ifndef _OPCFUNCTIONS_H
#define _OPCFUNCTIONS_H



IOPCServer *InstantiateServer();
void AddTheGroup(IOPCServer* pIOPCServer, IOPCItemMgt* &pDataIOPCItemMgt, IOPCItemMgt* &pParametersIOPCItemMgt, OPCHANDLE* hServerGroup);
void AddAllItems(IOPCItemMgt* pDataIOPCItemMgt, IOPCItemMgt* pParametersIOPCItemMgt, OPCHANDLE* hServerItem);
void RemoveItem(IOPCItemMgt* pIOPCItemMgt, OPCHANDLE hServerItem);
void RemoveGroup(IOPCServer* pIOPCServer, OPCHANDLE hServerGroup);
void writeItem(IOPCItemMgt* pIOPCItemMgt, OPCHANDLE hServerItem, VARIANT tempVariant);


#endif