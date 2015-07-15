#include "MediaPlayerSseEventHandler.h"
#include <dlog.h>
#include "csci-sdv-api.h"

#ifdef LOG
#error  LOG already defined
#endif

#define LOG(level, msg, args...)  dlog(DL_MSP_MPLAYER, level,"MediaPlayerSseEventHandler:%s:%d " msg, __FUNCTION__, __LINE__, ##args);
#define TSB_STREAMING_URI "avfs://item=live/"
#define INMEMORY_STREAMING_URI "avfs://item=vod/"
#define TSB_OFFSET "live/"
#define INMEMORY_OFFSET "vod/"

std::list<tMediaCB *> MediaCallbkList;
pthread_mutex_t m_SSENotifMutex;

/**
    * @param serviceUrl [IN] for which a SSE notification was recieved
	* @param pValue [IN] The playload data recived via web services which holds Information pertaining to ,
	* \n Mediaplayer Error Signals,Status and the error/status description
    * @return None
    * @brief Handles SSE notifications sent from the gateway and dispatches to the mediaplayer queue for processing
*/
void HandleSSENotfication(const char* serviceUrl, eCsciWebSvcsMediaInfoPayloadType payloadType, void *pValueData, void *pClientContext)
{
    FNLOG(DL_MSP_MPLAYER);
    (void)pClientContext;
    if (payloadType == kCsciWebSvcsMediaInfoPayloadType_SdvInfoPayload)
    {
        tCsciWebSvcsSdvInfoPayload *pValue = (tCsciWebSvcsSdvInfoPayload*)pValueData;
        if (pValue != NULL)
        {
            Csci_Sdvm_SetConnectionId(pValue->sdvConnectionId);
            LOG(DLOGL_REALLY_NOISY, "SDV Payload connectionid = %d\n", pValue->sdvConnectionId);
        }
        else
        {
            LOG(DLOGL_ERROR, "SDV payload is NULL");
        }
    }


    if (payloadType == kCsciWebSvcsMediaInfoPayloadType_StreamInfoPayload)
    {

        tCsciWebSvcsMediaStreamInfoPayload *pValue = (tCsciWebSvcsMediaStreamInfoPayload *)pValueData;

        if (strlen(pValue->notificationDesc) > 0)
        {
            LOG(DLOGL_REALLY_NOISY, "Received SSE message is..:%s\n", pValue->notificationDesc);
            TriggerSDVStatusCallback(pValue->mediaStreamSignal);
            char *url = {0};
            if (strncmp(serviceUrl, TSB_STREAMING_URI, strlen(TSB_STREAMING_URI)) == 0)
            {
                url = strstr(serviceUrl, TSB_OFFSET);
                url += 5;//move till avfs://item=live/
            }
            else if (strncmp(serviceUrl, INMEMORY_STREAMING_URI, strlen(INMEMORY_STREAMING_URI)) == 0)
            {
                url = strstr(serviceUrl, INMEMORY_OFFSET);
                url += 4;//move till avfs://item=vod/
            }
            if (url)
            {
                LOG(DLOGL_REALLY_NOISY, "URL passed is %s", url);
                PassCBStatus(url, pValue->mediaStreamStatus, pValue->mediaStreamSignal, pValue->notificationDesc);
            }
            else
            {
                LOG(DLOGL_ERROR, "NULL URL");
            }
        }
        else
        {
            LOG(DLOGL_ERROR, " Unknown SSE event with NULL descriptor");
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "Unknown SSE event");
    }
}

/**
	* @param serviceUrl [IN] The service URL for which a media player event was recieved via webservices from the gateway
	* @param status [IN] Media player status sent by the gateway via SSE for the Media player session specified by the serviceUrl
	* @param signal [IN] Media player signal sent by the gateway via SSE for the Media player session specified by the serviceUrl
	* @param status [IN] Description message sent by gateway associated with a media player signal
	* @return None
    * @brief Dispatches the Status and the message to the registered Media player callback function
*/

void PassCBStatus(const char* serviceUrl, int8_t status, int8_t signal, char * desc)
{
    FNLOG(DL_MSP_MPLAYER);
    LOG(DLOGL_REALLY_NOISY, "Size of Media CB list is:%d", MediaCallbkList.size());

    std::list<tMediaCB *>::iterator it;
    pthread_mutex_lock(&m_SSENotifMutex);
    for (it = MediaCallbkList.begin(); it != MediaCallbkList.end(); it++)
    {
        if (strcmp((*it)->Url, serviceUrl) == 0)
        {
            LOG(DLOGL_REALLY_NOISY, "Found a match.. passing the signals with description");
            if ((*it)->cbFunc)
            {
                (*it)->cbFunc((*it)->pData, (eIMediaPlayerSignal)signal, (eIMediaPlayerStatus)status, desc);
                pthread_mutex_unlock(&m_SSENotifMutex);
                return;
            }
        }
    }
    pthread_mutex_unlock(&m_SSENotifMutex);
}

/**
    * @param pData [IN] Object instance that registers for the service
	* @param srcurl [IN] The URL for which a session was created/loaded by the APP
	* @param cbfunc [IN] Callback Function that has to triggered
    * @return None
    * @brief Registers the callback function that will handle media player error events gracefully
*/
void RegisterMediaPlayerCallbackFunc(void *pData, const char* srcurl, MediaCallback_t cbfunc)
{
    FNLOG(DL_MSP_MPLAYER);
    LOG(DLOGL_REALLY_NOISY, "Size of Media CB list is:%d Object is:%p", MediaCallbkList.size(), pData);

    std::list<tMediaCB *>::iterator it;
    for (it = MediaCallbkList.begin(); it != MediaCallbkList.end(); it++)
    {
        if (strcmp((*it)->Url, srcurl) == 0)
        {
            LOG(DLOGL_REALLY_NOISY, "Already Registered for Url : %s", srcurl);
            return ;
        }
    }

    tMediaCB *pCb = new tMediaCB();
    if (pCb)
    {
        pCb->cbFunc = cbfunc;
        pCb->pData = pData;
        strlcpy(pCb->Url, srcurl, 256);
        pthread_mutex_lock(&m_SSENotifMutex);
        MediaCallbkList.push_back(pCb);
        pthread_mutex_unlock(&m_SSENotifMutex);
        LOG(DLOGL_REALLY_NOISY, "Registered for media error events from gateway for URL: %s", pCb->Url);
    }
}
void TriggerSDVStatusCallback(int8_t signal)
{
    FNLOG(DL_MSP_MPLAYER);
    switch (signal)
    {
    case kMediaPlayerSignal_ServiceLoaded:
        LOG(DLOGL_REALLY_NOISY, "Firing the start lua timer");
        Csci_Sdvm_ReportLUA(kSdvLUAReq_StartTimer);
        break;
    case kMediaPlayerSignal_ServiceNotAvailableDueToSdv:
        LOG(DLOGL_REALLY_NOISY, "Stop the lua timer");
        Csci_Sdvm_ReportLUA(kSdvLUAReq_StopTimer);
        break;
    default:
        break;

    }
}
/**
	* @param srcurl [IN] The registered URL for which a session was created/loaded/stopped by the APP
	* @return None
    * @brief Unregisters the callback function
*/
void UnRegisterMediaPlayerCallbackFunc(const char* srcurl)
{
    FNLOG(DL_MSP_MPLAYER);

    LOG(DLOGL_REALLY_NOISY, "Size of Media CB list is:%d", MediaCallbkList.size());
    std::list<tMediaCB *>::iterator it;
    pthread_mutex_lock(&m_SSENotifMutex);
    for (it = MediaCallbkList.begin(); it != MediaCallbkList.end(); it++)
    {
        if (strcmp((*it)->Url, srcurl) == 0)
        {
            tMediaCB *pCb = *it;
            MediaCallbkList.erase(it);
            LOG(DLOGL_REALLY_NOISY, "UnRegistered for media error events from gateway for URL: %s", pCb->Url);
            delete pCb;
            pCb = NULL;
            pthread_mutex_unlock(&m_SSENotifMutex);
            return ;
        }
    }
    pthread_mutex_unlock(&m_SSENotifMutex);
}

static int g_handle;
extern "C" void Csci_MediaPlayer_RegisterForSSE()
{
    Csci_Websvcs_Ipclient_StreamingInfo_RegisterCallback(HandleSSENotfication, &g_handle);
    pthread_mutex_init(&m_SSENotifMutex, NULL);//init the mutex only when the SSE media service is registered for
}
