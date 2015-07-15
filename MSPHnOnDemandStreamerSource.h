#ifndef MSP_HN_ONDEMAND_STREAMER_SOURCE_H
#define MSP_HN_ONDEMAND_STREAMER_SOURCE_H

//MSPHnOnDemandStreamerSource.h

#include <string>
#include <stdio.h>
#include "MSPSource.h"
#include "MspCommon.h"
#include "cpe_programhandle.h"
#include "cpe_cam.h"
#include "cpe_hnservermgr.h"
#include "MSPSourceFactory.h"
#include "InMemoryStream.h"

using namespace std;


class MSPHnOnDemandStreamerSource: public MSPSource
{
public:
    MSPHnOnDemandStreamerSource(std::string aSrcUrl, tCpeSrcHandle srcHandle);

    ~MSPHnOnDemandStreamerSource();

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

    /* */
    //tCpePgrmHandle getCpeProgHandle()const;

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

    //eMspStatus setStreamInfo(void);

    eMspStatus setStreamInfo(Psi *ptrPsi);

    eMspStatus InjectStreamInfo(Psi *ptrPsi);

    eMspStatus SetPatPMT(Psi *ptrPsi);
    //void MSPHnOnDemandStreamerSource::DumpPatInfo(uint8_t *mPtr);

    eMspStatus DumpPmtInfo(uint8_t *pPmtPsiPacket, int PMTSize);

    tCpePgrmHandle getCpeProgHandle()const;
    void SetCpeStreamingSessionID(uint32_t sessionId);
    pthread_mutex_t  mMutex;
    tCpePgrmHandle mPgrmHandle;
    tCpePgrmHandle getPgrmHandle()
    {
        return mPgrmHandle;
    }
    eMspStatus InjectCCI(uint8_t ccibyte);
private:
    eMspStatus getAudioPid(Psi *ptrPsi, tCpeSrcPFPidDef *mPids);
    eMspStatus getVideoPid(Psi *ptrPsi, tCpeSrcPFPidDef *mPids);
    eMspStatus getClockPid(Psi *ptrPsi, tCpeSrcPFPidDef *mPids);
    uint8_t m_CCIbyte;

    int getNumOfPids(Psi *ptrPsi);
    tCpeSrcHandle mCpeSrcHandle;
    std::string mSrcUrl;
    int32_t mSourceId;
    uint32_t mSessionId;
    int32_t mProgramNumber;
    SourceStateCallback mSrcStateCB;
    void *mClientContext;
    std::string mFileName;
    uint8_t *mRawPmtPtr, *mRawPatPtr;
    unsigned int mRawPmtSize;
    uint16_t mPmtPid;
#if PLATFORM_NAME == G8
    tCpeHnSrvInjectID mInjectPat;
    tCpeHnSrvInjectID mInjectPmt;
#endif
    tCpePgrmHandlePmt mPmtInfo;
};

#endif // #ifndef MSP_HN_ONDEMAND_STREAMER_SOURCE_H

