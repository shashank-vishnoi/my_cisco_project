#ifndef MSP_HTTP_SOURCE_H
#define MSP_HTTP_SOURCE_H

#include <string>
#include <list>
#include "MSPSource.h"
#include "MspCommon.h"
#include <pthread.h>


typedef enum
{
    kHTTPSrcNotOpened,
    kHTTPSrcPlaying,
    kHTTPSrcPaused,
    kHTTPSrcStopped,
    kHTTPSrcInvalid
} eHTTPSrcState;



class MSPHTTPSource: public MSPSource
{
    tCpeSrcHandle mCpeSrcHandle;
    std::string mSrcUrl;
    int mSourceId;
    int mProgramNumber;
    tCpeSrcCallbackID mSrcCbIdStreamError;
    tCpeSrcCallbackID mSrcCbIdNetBuffError;
    tCpeSrcCallbackID mSrcCbIdHttpState;
    SourceStateCallback mSrcStateCB;
    void *mClientContext;
    eMspStatus mParseStatus;
    std::string mFileName;
    eHTTPSrcState mHTTPSrcState;
    int mCurrentSpeedMode;
    pthread_mutex_t mrdvr_player_mutex;
    pthread_cond_t mrdvr_player_status;
    bool mPendingPause;
    bool mPendingPlay;
    bool mPendingSetSpeed;
    bool mPendingSetPosition;
    int currentSpeedNum, currentSpeedDen;

public:
    MSPHTTPSource(std::string aSrcUrl);

    ~MSPHTTPSource();

    eMspStatus load(SourceStateCallback aSrcStateCB, void* aClientContext);

    /* This function open source handle & registers Callback for EOF & BOF */
    eMspStatus open(eResMonPriority pri);

    /* Start the source */
    eMspStatus start();

    /* Stop source handle & unregisters File Playback Callbacks */
    eMspStatus stop();

    /* Release source */
    eMspStatus release();

    /* Getter Methods for Media Controller, Display Session & PSI */
    tCpeSrcHandle getCpeSrcHandle()const;

    int getSourceId()const;

    int getProgramNumber()const;

    /* Required for PIP Swap */
    std::string getSourceUrl()const;

    /* Get File Name */
    std::string getFileName()const;

    bool isDvrSource()const;

    bool canRecord()const;

    eMspStatus setSpeed(tCpePlaySpeed aPlaySpeed, uint32_t aNpt);

    eMspStatus getPosition(float *aNptTime);

    eMspStatus setPosition(float aNptTime);

    bool isPPV(void);

    bool isQamrf(void);

    bool isAnalogSource()const
    {
        return false;
    };

    /* Play back callback function  */
    static int PlayBkCallbackFunction(tCpeSrcCallbackTypes aType, void *aUserData, void *pCallbackSpecific);

    tCpePgrmHandle getCpeProgHandle()const;
    void SetCpeStreamingSessionID(uint32_t sessionId);

private:
    void buildFileName();

};

#endif // #ifndef MSP_HTTP_SOURCE_H

