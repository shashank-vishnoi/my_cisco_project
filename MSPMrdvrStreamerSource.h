#ifndef MSP_MRDVR_STREAMER_SOURCE_H
#define MSP_MRDVR_STREAMER_SOURCE_H

#include <string>
#include <stdio.h>
#include "MSPSource.h"
#include "MspCommon.h"
#include "cpe_programhandle.h"
#include "cpe_cam_dvr.h"
#include "cpe_recmgr.h"
#include "Cam.h"
#include "cpe_hncontentmgr_cds.h"
#include "cpe_hnservermgr.h"
#include "dvr_metadata_reader.h"
#include "MSPSourceFactory.h"

using namespace std;

#define CA_DESCRIPTOR_LENGTH      4
#define CA_DESCRIPTOR_DEFAULT     0x0e

class MSPMrdvrStreamerSource: public MSPSource
{
public:
    MSPMrdvrStreamerSource(std::string aSrcUrl);

    ~MSPMrdvrStreamerSource();

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

    static int FileChangeCallback(tCpeHnSrvMgrCallbackTypes type, void *userdata, void *pCallbackSpecific);
    eMspStatus CamDecryptionStop(IPlaySession *ptrPlaySession);
    eMspStatus CamDecryptionStart(uint32_t drmSystemID, tCpePgrmHandle pgrmPlayHandle,
                                  IPlaySession **dPtrPlaySession,
                                  tCpeRecDataBaseType *pDvrBlob, tCpeRecDataBaseType *pDvrCaDesc, uint8_t  scramblingMode);
    tCpePgrmHandle getCpeProgHandle()const;
    void SetCpeStreamingSessionID(uint32_t sessionId);
    IPlaySession* getCAMPlaySession(void);
    void setCAMPlaySession(IPlaySession* pPlaysession);

    IPlaySession 	*ptrPlaySession;
    tCpeHnMsm_CallbackID fileChangeCallbackId;
    pthread_mutex_t  mMutex;
    eMspStatus InjectCCI(uint8_t CCIbyte);
private:
    tCpeSrcHandle mCpeSrcHandle;
    tCpePgrmHandle mPgrmHandle;
    std::string mSrcUrl;
    int32_t mSourceId;
    uint32_t mSessionId;
    int32_t mProgramNumber;
    SourceStateCallback mSrcStateCB;
    void *mClientContext;
    std::string mFileName;
    uint8_t m_CCIbyte;

};

#endif // #ifndef MSP_MRDVR_STREAMER_SOURCE_H

