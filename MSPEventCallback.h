
#ifndef MSP_EVENT_CALLBACK_H_
#define MSP_EVENT_CALLBACK_H_


#include <list>
#include <string>

#include "csci-msp-event-api.h"


using namespace std;



class MSPEventCallback
{
private:


    struct EventCallbackInfo
    {
        tCsciMspCallbackFunction mCallbackFn;
        tCsciMspEvent mType;
        void *mClientContext;
    };

    std::list<EventCallbackInfo> mCallbackList;

    void DoCallbacks(tCsciMspEvent type, const void* pCallbackSpecific);

    std::string prevAudioFocusUrl;

public:
    int RegisterCallback(tCsciMspCallbackFunction callback,
                         tCsciMspEvent type,
                         void *clientContext);


    int UnregisterCallback(tCsciMspCallbackFunction callback);

    void ProcessEvents();

    void CheckAudioFocusChange();
};

#endif
