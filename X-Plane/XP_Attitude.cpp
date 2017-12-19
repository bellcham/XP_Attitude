
/*
 * XP_Attitude.cpp
 * 
 * 
 */

#undef UNICODE

#define WIN32_LEAN_AND_MEAN

#include <stdio.h>
#include <string.h>

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>

#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMDataAccess.h"

 // Need to link with Ws2_32.lib
#pragma comment (lib, "Ws2_32.lib")
 // #pragma comment (lib, "Mswsock.lib")

#define DEFAULT_BUFLEN 512
#define DEFAULT_TX_PORT "27015"
#define DEFAULT_RX_PORT "27016"
#define DEFAULT_IP "127.0.0.1"

/*
 * Global Variables.  We will store our single window globally.  We also record
 * whether the mouse is down from our mouse handler.  The drawing handler looks
 * at this information and draws the appropriate display.
 * 
 */

static XPLMWindowID	gWindow = NULL;
static int				gClicked = 0;

static void MyDrawWindowCallback(
                                   XPLMWindowID         inWindowID,    
                                   void *               inRefcon);    

static void MyHandleKeyCallback(
                                   XPLMWindowID         inWindowID,    
                                   char                 inKey,    
                                   XPLMKeyFlags         inFlags,    
                                   char                 inVirtualKey,    
                                   void *               inRefcon,    
                                   int                  losingFocus);    

static int MyHandleMouseClickCallback(
                                   XPLMWindowID         inWindowID,    
                                   int                  x,    
                                   int                  y,    
                                   XPLMMouseStatus      inMouse,    
                                   void *               inRefcon);    

SOCKET SendSocket = INVALID_SOCKET;
SOCKET ClientSocket = INVALID_SOCKET;
WSADATA wsaData;

struct addrinfo *txResult = NULL; 
struct addrinfo *rxResult = NULL;
struct addrinfo hints;

int iResult;
int iSendResult;
char recvbuf[DEFAULT_BUFLEN];
char sendbuf[DEFAULT_BUFLEN];
int recvbuflen = DEFAULT_BUFLEN;

char ScreenBuffer[14][80];

XPLMDataRef gZuluTimeDataRef = NULL;
XPLMDataRef gOverrideDataRef = NULL;
XPLMDataRef gPsiDataRef = NULL;
XPLMDataRef gThetaDataRef = NULL;
XPLMDataRef gPhiDataRef = NULL;



/*
 * XPluginStart
 * 
 * Our start routine registers our window and does any other initialization we 
 * must do.
 * 
 */
PLUGIN_API int XPluginStart(
						char *		outName,
						char *		outSig,
						char *		outDesc)
{
	/* First we must fill in the passed in buffers to describe our
	 * plugin to the plugin-system. */

	strcpy(outName, "LV_ATT");
	strcpy(outSig, "xplanesdk.abellchambers.lvattitude");
	strcpy(outDesc, "A plugin for overiding aircraft attitude from LabVIEW ");

	memset(ScreenBuffer, 0, sizeof(ScreenBuffer));
	memset(recvbuf, 0, DEFAULT_BUFLEN);
	memset(sendbuf, 0, DEFAULT_BUFLEN);
	
	gZuluTimeDataRef = XPLMFindDataRef("sim/time/zulu_time_sec");
	gOverrideDataRef = XPLMFindDataRef("sim/operation/override/override_planepath");
	gPsiDataRef = XPLMFindDataRef("sim/flightmodel/position/psi");
	gThetaDataRef = XPLMFindDataRef("sim/flightmodel/position/theta");
	gPhiDataRef = XPLMFindDataRef("sim/flightmodel/position/phi");

	// Initialize Winsock
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		sprintf(ScreenBuffer[0], "WSAStartup failed with error: %d", iResult);
		return 1;
	}
	else {
		sprintf(ScreenBuffer[0], "WSAStartup startup successful");
	}

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;

	// Resolve the server address and port
	iResult = getaddrinfo(DEFAULT_IP, DEFAULT_TX_PORT, &hints, &txResult);
	if (iResult != 0) {
		sprintf(ScreenBuffer[1], "getaddrinfo for tx socket failed with error: %d", iResult);
		WSACleanup();
		return 1;
	}
	else {
		sprintf(ScreenBuffer[1], "getaddrinfo for tx socket success");
	}

	// Create a SOCKET
	SendSocket = socket(txResult->ai_family, txResult->ai_socktype, txResult->ai_protocol);
	if (SendSocket == INVALID_SOCKET) {
		sprintf(ScreenBuffer[2], "tx socket creation failed with error: %ld", WSAGetLastError());
		freeaddrinfo(txResult);
		WSACleanup();
		return 1;
	}
	else {
		sprintf(ScreenBuffer[2], "tx socket creation success");
	}

	hints.ai_family = AF_INET;

	// Resolve the server address and port
	iResult = getaddrinfo(DEFAULT_IP, DEFAULT_RX_PORT, &hints, &rxResult);
	if (iResult != 0) {
		sprintf(ScreenBuffer[3], "getaddrinfo for rx socket failed with error: %d", iResult);
		WSACleanup();
		return 1;
	}
	else {
		sprintf(ScreenBuffer[3], "getaddrinfo for rx socket success");
	}

	// Create a SOCKET
	ClientSocket = socket(rxResult->ai_family, rxResult->ai_socktype, rxResult->ai_protocol);
	if (SendSocket == INVALID_SOCKET) {
		sprintf(ScreenBuffer[4], "rx socket creation failed with error: %ld", WSAGetLastError());
		freeaddrinfo(txResult);
		WSACleanup();
		return 1;
	}
	else {
		sprintf(ScreenBuffer[4], "rx socket creation success");
		const int optVal = 1;
		setsockopt(ClientSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&optVal, rxResult->ai_addrlen);
	}

	iResult = bind(ClientSocket, rxResult->ai_addr, rxResult->ai_addrlen);
	if (iResult != 0) {
		sprintf(ScreenBuffer[5], "bind for rx socket failed with error: %d", iResult);
		WSACleanup();
		//return 1;
	}
	else {
		sprintf(ScreenBuffer[5], "bind for rx socket success");
	}

	sendto(SendSocket, "tx Socket Initialised", 25, 0, txResult->ai_addr, txResult->ai_addrlen);

	/* Now we create a window.  We pass in a rectangle in left, top,
	 * right, bottom screen coordinates.  We pass in three callbacks. */

	gWindow = XPLMCreateWindow(
					50, 600, 400, 200,			/* Area of the window. */
					1,							/* Start visible. */
					MyDrawWindowCallback,		/* Callbacks */
					MyHandleKeyCallback,
					MyHandleMouseClickCallback,
					NULL);						/* Refcon - not used. */
					
	

	/* We must return 1 to indicate successful initialization, otherwise we
	 * will not be called back again. */
	 
	return 1;
}

/*
 * XPluginStop
 * 
 * Our cleanup routine deallocates our window.
 * 
 */
PLUGIN_API void	XPluginStop(void)
{
	XPLMDestroyWindow(gWindow);
	closesocket(SendSocket);
	closesocket(ClientSocket);
	freeaddrinfo(txResult);
	freeaddrinfo(rxResult);
	WSACleanup();
}

/*
 * XPluginDisable
 * 
 * We do not need to do anything when we are disabled, but we must provide the handler.
 * 
 */
PLUGIN_API void XPluginDisable(void)
{
}

/*
 * XPluginEnable.
 * 
 * We don't do any enable-specific initialization, but we must return 1 to indicate
 * that we may be enabled at this time.
 * 
 */
PLUGIN_API int XPluginEnable(void)
{
	return 1;
}

/*
 * XPluginReceiveMessage
 * 
 * We don't have to do anything in our receive message handler, but we must provide one.
 * 
 */
PLUGIN_API void XPluginReceiveMessage(
					XPLMPluginID	inFromWho,
					int				inMessage,
					void *			inParam)
{
}

/*
 * MyDrawingWindowCallback
 * 
 * This callback does the work of drawing our window once per sim cycle each time
 * it is needed.
 * 
 */
void MyDrawWindowCallback(
                                   XPLMWindowID         inWindowID,    
                                   void *               inRefcon)
{
	int		left, top, right, bottom, iOverride;
	float	color[] = { 1.0, 1.0, 1.0 }; 	/* RGB White */
	struct {
		float Override;
		float Psi;
		float Theta;
		float Phi;
	} InputData;
	
	/* First we get the location of the window passed in to us. */
	XPLMGetWindowGeometry(inWindowID, &left, &top, &right, &bottom);
	
	/* We now use an XPLMGraphics routine to draw a translucent dark
	 * rectangle that is our window's shape. */
	XPLMDrawTranslucentDarkBox(left, top, right, bottom);

	//iResult = recv(ClientSocket, recvbuf, recvbuflen, 0);
	//if (iResult > 0) {
	//	printf("Bytes received: %d\n", iResult);

	//	// Echo the buffer back to the sender
		
		float ZuluTime = XPLMGetDataf(gZuluTimeDataRef);
		sprintf(sendbuf, "%f\n", ZuluTime);
		iSendResult = sendto(SendSocket, sendbuf, sizeof(sendbuf), 0, txResult->ai_addr, txResult->ai_addrlen);
		if (iSendResult == SOCKET_ERROR) {
			sprintf(ScreenBuffer[6],"send failed with error: %d\n", WSAGetLastError());
		}
		else {
			sprintf(ScreenBuffer[6], "Bytes sent: %d\n", iSendResult);
		}
		
		iSendResult = recvfrom(ClientSocket, recvbuf, 16, 0, NULL, 0);
		if (iSendResult == SOCKET_ERROR) {
			sprintf(ScreenBuffer[7], "receive failed with error: %d\n", WSAGetLastError());
		}
		else {
			sprintf(ScreenBuffer[7], "Bytes Received: %d\n", iSendResult);
			sprintf(ScreenBuffer[8], "Data Received: %s\n", recvbuf);
			if (iSendResult == 16) {
				memcpy(&InputData, recvbuf, sizeof(InputData));
				sprintf(ScreenBuffer[9], "Override: %f, Psi: %f, Theta: %f, Phi: %f\n", InputData.Override, InputData.Psi, InputData.Theta, InputData.Phi);
				
				if (InputData.Override) {
					iOverride = 1;
					XPLMSetDatavi(gOverrideDataRef, &iOverride, 0, 1);
					XPLMSetDataf(gPsiDataRef, InputData.Psi);
					XPLMSetDataf(gThetaDataRef, InputData.Theta);
					XPLMSetDataf(gPhiDataRef, InputData.Phi);
				}
				else {
					iOverride = 0;
					XPLMSetDatavi(gOverrideDataRef, &iOverride, 0, 1);
				}
			}
			
		}
	
		for (int i = 0; i<14; i++)
			XPLMDrawString(color, left + 10, (top - 20) - (10 * i), ScreenBuffer[i], NULL, xplmFont_Basic);

	//}
	//else if (iResult == 0)
	//	printf("Connection closing...\n");
	//else {
	//	printf("recv failed with error: %d\n", WSAGetLastError());
	//	closesocket(ClientSocket);
	//	WSACleanup();
	//	//return 1;
	//}
}                                   

/*
 * MyHandleKeyCallback
 * 
 * Our key handling callback does nothing in this plugin.  This is ok; 
 * we simply don't use keyboard input.
 * 
 */
void MyHandleKeyCallback(
                                   XPLMWindowID         inWindowID,    
                                   char                 inKey,    
                                   XPLMKeyFlags         inFlags,    
                                   char                 inVirtualKey,    
                                   void *               inRefcon,    
                                   int                  losingFocus)
{
}                                   

/*
 * MyHandleMouseClickCallback
 * 
 * 
 */
int MyHandleMouseClickCallback(
                                   XPLMWindowID         inWindowID,    
                                   int                  x,    
                                   int                  y,    
                                   XPLMMouseStatus      inMouse,    
                                   void *               inRefcon)
{
	return 0;
}                                      
