
#include <assert.h>


#include "MSPEventCallback.h"

#include <dlog.h>

#ifdef LOG
#error  LOG already defined
#endif
#define LOG(level, msg, args...)  dlog(DL_MSP_MPLAYER, level,"MSPEventCallback(%p):%s:%d " msg, this, __FUNCTION__, __LINE__, ##args);


int MSPEventCallback::RegisterCallback(tCsciMspCallbackFunction callback,
                                       tCsciMspEvent type,
                                       void *clientContext)
{
    // verify event is a valid event
    if (type != eCsciMediaPlayerEvent_AudioFocusChanged)
    {
        LOG(DLOGL_ERROR, "Error: unknown event type: %d", type);
        return -1;
    }

    if (!callback)
    {
        LOG(DLOGL_ERROR, "Error: Null callback");
        return -2;
    }


    EventCallbackInfo cbInfo;

    cbInfo.mCallbackFn = callback;
    cbInfo.mType = type;
    cbInfo.mClientContext = clientContext;

    LOG(DLOGL_REALLY_NOISY, "store callback: %p clientContext: %p", callback, clientContext);

    mCallbackList.push_back(cbInfo);
    return 0;
}


int MSPEventCallback::UnregisterCallback(tCsciMspCallbackFunction callback)
{
    bool found = false;

    std::list<EventCallbackInfo>::iterator iter;

    iter = mCallbackList.begin();

    while (iter != mCallbackList.end())
    {
        EventCallbackInfo cbInfo = *iter;

        if (cbInfo.mCallbackFn == callback)
        {
            iter = mCallbackList.erase(iter);
            found = true;
        }
        else
        {
            iter++;
        }
    }

    if (found)
    {
        return 0;
    }
    else
    {
        return -1;
    }
}


void MSPEventCallback::DoCallbacks(tCsciMspEvent type, const void* pCallbackSpecific)
{
    std::list<EventCallbackInfo>::iterator iter;

    iter = mCallbackList.begin();

    for (iter = mCallbackList.begin(); iter != mCallbackList.end(); iter++)
    {
        EventCallbackInfo cbInfo = *iter;

        if (cbInfo.mType == type)
        {
            tCsciMspCallbackFunction cb = cbInfo.mCallbackFn;
            LOG(DLOGL_REALLY_NOISY, "call cb: %p  clientContext: %p ", cb, cbInfo.mClientContext);
            cb(type, cbInfo.mClientContext, pCallbackSpecific);
            LOG(DLOGL_REALLY_NOISY, "return from callback");
        }
    }
}


void MSPEventCallback::ProcessEvents()
{
    // quick return if no registered clients

    if (mCallbackList.size() == 0)
    {
        LOG(DLOGL_REALLY_NOISY, "no registered clients - returning");
        return;
    }
    // check for each type of event

    // 1. process eCsciMediaPlayerEvent_AudioFocusChanged
    CheckAudioFocusChange();

}


void MSPEventCallback::CheckAudioFocusChange()
{
    const char *currentAudioFocusUrl = Csci_MediaPlayer_GetServiceUrlWithAudioFocus();

    string audioFocusUrl;

    if (currentAudioFocusUrl != NULL)
    {
        audioFocusUrl = currentAudioFocusUrl;
        LOG(DLOGL_NOISE, "audioFocusUrl: %s", audioFocusUrl.c_str());
    }
    else
    {
        LOG(DLOGL_NOISE, "null url");
    }

    if (audioFocusUrl != prevAudioFocusUrl)
    {
        prevAudioFocusUrl = audioFocusUrl;
        LOG(DLOGL_MINOR_EVENT, "audioFocusUrl has changed to: %s", audioFocusUrl.c_str());
        DoCallbacks(eCsciMediaPlayerEvent_AudioFocusChanged, audioFocusUrl.c_str());
    }
}


