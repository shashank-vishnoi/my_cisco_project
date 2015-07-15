#if !defined(MEDIAPLAYERSSEEVENTHANDLER_H)
#define MEDIAPLAYERSSEEVENTHANDLER_H
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <csci-websvcs-common.h>
#include <csci-websvcs-ipclient-streaming-api.h>
#include "dlog.h"
#include <sail-message-api.h>
#include <sail-mediaplayersession-api.h>
#include <csci-msp-event-api.h>
#include <list>

typedef void (*MediaCallback_t)(void *pData, eIMediaPlayerSignal signal, eIMediaPlayerStatus status, char* desc);


/*
Data Structure definition to Keep track of
all active media player session's callback functions
to dispatch error events
*/
typedef struct MediaCB
{
    char Url[256];
    void *pData;
    MediaCallback_t cbFunc;
} tMediaCB;


/**
    * @param serviceUrl [IN] for which a SSE notification was recieved
	* @param pValue [IN] The playload data recived via web services which holds Information pertaining to ,
	* \n Mediaplayer Error Signals,Status and the error/status description
    * @return None
    * @brief Handles SSE notifications sent from the gateway and dispatches to the mediaplayer queue for processing
*/
void HandleSSENotfication(const char* serviceUrl, eCsciWebSvcsMediaInfoPayloadType payloadType, void *pValueData, void *pClientContext);

/**
    * @param pData [IN] Object instance that registers for the service
	* @param srcurl [IN] The URL for which a session was created/loaded by the APP
	* @param cbfunc [IN] Callback Function that has to triggered
    * @return None
    * @brief Registers the callback function that will handle media player error events gracefully
*/
void RegisterMediaPlayerCallbackFunc(void *pData, const char* srcurl, MediaCallback_t cbfunc);

/**
	* @param srcurl [IN] The registered URL for which a session was created/loaded/stopped by the APP
	* @return None
    * @brief Unregisters the callback function
*/
void UnRegisterMediaPlayerCallbackFunc(const char* srcurl);

/**
	* @param serviceUrl [IN] The service URL for which a media player event was recieved via webservices from the gateway
	* @param status [IN] Media player status sent by the gateway via SSE for the Media player session specified by the serviceUrl
	* @param signal [IN] Media player signal sent by the gateway via SSE for the Media player session specified by the serviceUrl
	* @param status [IN] Description message sent by gateway associated with a media player signal
	* @return None
    * @brief Dispatches the Status and the message to the registered Media player callback function
*/
void PassCBStatus(const char* serviceUrl, int8_t status, int8_t signal, char * desc);

void TriggerSDVStatusCallback(int8_t signal);
#endif
