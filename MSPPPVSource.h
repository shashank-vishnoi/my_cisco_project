#ifndef  MSP_PPV_SOURCE_H
#define MSP_PPV_SOURCE_H

#include <string>
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
#include <cpe_source.h>
#endif
#include <sail-clm-api.h>
#include "MSPRFSource.h"
#include "MSPSource.h"
#include "MspCommon.h"
#include "IPpvManager.h"


class MSPPPVSource: public MSPRFSource
{

private:

    static std::list<void*> mActiveControllerList;
    static pthread_mutex_t  mControllerListMutex;
    static bool             mControllerListInititialized;

    typedef enum {kPPVIdle, kPPVLoading, kPPVStartVideo, kPPVStopVideo, kPPVContentNotFound, kPPVInterstitialStart, kPPVInterstitialStop, kPPVSubscriptionAuthorized, kPPVSubscriptionExpired} ePPVState;
    void *ppvEvent;
    ePPVState mPPVState;
    static void PPVMgrClassCallback(void *aUserData, void *ppvData, ePpvStateEnum ppvstate);
    void PPVMgrInstanceCallback(void *ppvData, ePpvStateEnum ppvstate);

    eMspStatus load(SourceStateCallback aSrcStateCB, void* aClientContext);

public:
    MSPPPVSource(std::string aSrcUrl, IMediaPlayerSession *pIMediaPlayerSession);
    ~MSPPPVSource();
    eMspStatus open(eResMonPriority tunerPriority);
    void setTunerPriority(eResMonPriority pri);
    bool isPPV(void);
    bool isQamrf(void);

    bool isAnalogSource()const;
};


#endif // #ifndef MSP_PPV_SOURCE_H


