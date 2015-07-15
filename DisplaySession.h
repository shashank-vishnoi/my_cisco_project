/**
   \file DisplaySession.h
   \class DisplaySession
*/

#if !defined(DISPLAYSESSION_H)
#define DISPLAYSESSION_H

// standard linux includes
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <string>

// cpe includes
#include <cpe_source.h>
#include <cpe_mediamgr.h>
#include <cpe_common.h>
#include <directfb.h>

// boost includes
#include <boost/signals2.hpp>
#include <boost/bind.hpp>

// msp includes
#include "psi.h"
#include "Cam.h"
#include "sail-mediaplayersession-api.h"
#include "ApplicationData.h"
#include "MSPSource.h"
#include "ApplicationDataExt.h"

#include "avpm.h"

// for boost signal callback to controller
typedef void (displaySessioncallbacktype)(eIMediaPlayerSignal, eIMediaPlayerStatus);
typedef boost::function<displaySessioncallbacktype> callbackfunctiontype;

/**
   \class DisplaySession
   \brief this class will be the gateway for handling requests to set up the A/V
*/

enum eDisplaySessionState
{
    kDisplaySessionIdle,
    kDisplaySessionOpened,
    kDisplaySessionStarted,
    kDisplaySessionStartedIncomplete,
    kDisplaySessionStopped,
    kDisplaySessionClosed,
    kDisplaySessionFreezed
};


typedef enum eSrcType
{
    kMspSrcTypeDigital,
    kMspSrcTypeAnanlog,
    kMSpSrcTypeUnknown
} etMspSrcType;

typedef void (*DataReadyCallback)(void *aClientContext);

typedef void (*DisplayMediaCallbackFunction)(void *aClientContext, tCpeMediaCallbackTypes aSourceState);

class DisplaySession
{
public:
    /*!  \fn   eMspStatus  updatePids(Psi* psi)
     \brief Update the DisplaySession when there is pid change.
     @param Psi* psi: instance of Psi object
     @return eMspStatus
     */
    eMspStatus  updatePids(Psi* psi);
    eMspStatus SapChangedCb();

    /*!  \fn   eMspStatus  setVideoWindow(const DFBRectangle rect, bool audioFocus)
     \brief Set the scale/full window, and specify whether the window is in focus.
     @param const DFBRectangle rect: window coordinates
     @param bool audioFocus: specified if the window is in focus (true).
     @return eMspStatus
     */
    eMspStatus  setVideoWindow(const DFBRectangle rect, bool audioFocus);

    /*!  \fn   eMspStatus  open(const tCpeSrcHandle handle)
     \brief Open the DisplaySession to setup the display resources.
     @param const tCpeSrcHandle handle: Cpe Source Handle
     @return eMspStatus
     */
    eMspStatus  open(const MSPSource *aSource);
    eMspStatus  open(const MSPSource *aSource, int ChannelType);

    /*!  \fn   eMspStatus  start(void)
     \brief Start the video display output to either the mainTV or the PIP.
     @param None
     @return eMspStatus
     */
    eMspStatus  start(bool isEasAudioActive);


    /*!  \fn   eMspStatus  setSpeed
     \brief Play the video in trick modes to either the mainTV or the PIP.
     @param None
     @return eMspStatus
     */
    eMspStatus setMediaSpeed(tCpePlaySpeed pPlaySpd);

    /*!  \fn   eMspStatus  startMedia
     \brief start the media.
     @param None
     @return eMspStatus
     */
    eMspStatus startMedia();

    /*!  \fn   eMspStatus  stopMedia
     \brief stop the media.
     @param None
     @return eMspStatus
     */
    eMspStatus stopMedia();

    /*!  \fn   eMspStatus  ControlAudio
     \brief Enable/Disable the Audio based on which trick modes you are.
     @param None
     @return eMspStatus
     */
    eMspStatus controlMediaAudio(tCpePlaySpeed pPlaySpd);


    /*!  \fn   eMspStatus  stop(void)
     \brief Stop the video display output to either the mainTV or the PIP.
     @param None
     @return eMspStatus
     */
    eMspStatus  stop(void);


    /*!  \fn   eMspStatus  freeze(void)
     \brief Freeze the video display output to either the mainTV or the PIP.
     @param None
     @return eMspStatus
     */
    eMspStatus  freeze(void);


    /*!  \fn   eMspStatus  close(void)
     \brief Close the video display output to either the mainTV or the PIP.
     @param None
     @return eMspStatus
     */
    eMspStatus  close(bool isEasAudioActive);

    /*!  \fn   eMspStatus  registerDisplayMediaCallback(DisplayMediaCallbackFunction cb)
     \brief Register the callback fn to receive the CPE Media Hal callback status.
     @param DisplayMediaCallbackFunction cb: callback function to be registered with DisplaySession
     @return eMspStatus
     */
    eMspStatus  registerDisplayMediaCallback(void *client, DisplayMediaCallbackFunction cb);


    /*!  \fn   eMspStatus  unregisterDisplayMediaCallback(void)
     \brief Unregister the callback fn from the DisplaySession.
     @param Psi object
     @return eMspStatus
     */
    eMspStatus  unregisterDisplayMediaCallback(void);


    /*!  \fn   void        performCb(tCpeMediaCallbackTypes type)
     \brief Perform callback function. This method is made public because of the static callback registered
            with the cpe media hal.
     @param tCpeMediaCallbackTypes type: media callback type defined in the cpe media hal api.
     @return eMspStatus
     */
    void performCb(tCpeMediaCallbackTypes type);

    eMspStatus filterAppDataPid(uint32_t aPid, DataReadyCallback aDataReadyCB, void *aClientContext, bool isMusic);

    eMspStatus updateAudioPid(Psi *aPsi, uint32_t aPid);

    eMspStatus getApplicationData(uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize);

    void SetCCICallback(void *mCBData, CCIcallback_t cb);

    void UnSetCCICallback();
    eMspStatus stopOutput(void);
    eMspStatus startOutput(void);
    ~DisplaySession();
    DisplaySession(bool isVod = false);

    void StopAudio(void);
    void RestartAudio(void);
    pthread_mutex_t mAppDataMutex;

    boost::signals2::connection setCallback(callbackfunctiontype);
    void clearCallback(boost::signals2::connection);
    void SetAudioLangCB(void *aCbData, AudioLangChangedCB aLangChangedCB);
    void SetSapChangedCB(void *aCbData, SapChangedCB cb);
    static const int maxApplicationDataSize = 65536;
    boost::signals2::signal<displaySessioncallbacktype> callback;
    tCpePgrmHandle getMediaHandle()
    {
        return mMediaHandle ;
    }
    bool getAudioFocus()
    {
        return mAudioFocus;
    }
    eMspStatus PmtRevUpdated(Psi* psi);

    //get  ApplicationData Instance Pointer
    BaseAppData* getAppDataInstance()
    {
        return mAppData;
    }


private:
    eMspStatus setUpDfbThroughAvpm(void);
    eMspStatus setUpDemuxDecoder(bool isEasAudioActive);
    eMspStatus formulateAudioPidTable(Psi* psi);
    eMspStatus formulateVideoPidTable(Psi *psi, Pmt* pmt);
    eMspStatus formulateCCLanguageList(Psi* psi);
    void *sfCallback(tCpeSFltCallbackTypes type, void* pCallbackSpecific);
    static void *secFltCallbackFunction(tCpeSFltCallbackTypes type, void* userdata, void* pCallbackSpecific);
    static void *appSecFltCallbackFunction(tCpeSFltCallbackTypes type, void* userdata, void* pCallbackSpecific);
    eMspStatus startCaFilter(uint16_t pid);
    eMspStatus stopCaFilter(void);
    eMspStatus stopAppDataFilter(void);
    eMspStatus closeAppDataFilter(void);
    eMspStatus initializePlaySession();
    void LogMediaTsParams(tCpeMediaTransportStreamParam *tsParams);
    uint32_t getEasAdjustedDecoderFlag(bool isEasAudioActive);
private:
    // CAM support
    tCpePgrmHandle mSfHandle;
    tCpeSFltCallbackID mSfCbIdError;
    tCpeSFltCallbackID mFCbIdSectionData;
    tCpeSFltCallbackID mSfCbIdSectionData;
    tCpeSrcHandle mSrcHandle;
    tCpeSFltFilterGroup mSFltGroup;

    // Mosaic Support
    tCpePgrmHandle mAppSfHandle;
    bool mbCamSfHandleAdded;
    tCpeSFltCallbackID mAppSfCbIdError;
    tCpeSFltCallbackID mAppSfCbIdSectionData;
    tCpeSFltCallbackID mAppSfCbIdTimeOut;
    DataReadyCallback mDataReadyCB;
    void* mDataReadyContext;
    BaseAppData *mAppData;
    uint32_t mDecoderFlag;
    DFBRectangle mRect;
    bool mAudioFocus;
    bool mWindowSetPending;
    tCpePgrmHandle mMediaHandle;
    tCpeMediaTransportStreamParam mTsParams;
    tCpePgrmHandlePmt             mPmtInfo;
    uint16_t             mPgmNo;
    eDisplaySessionState mState;
    DisplayMediaCallbackFunction mCb;
    void *mClientForCb;
    uint32_t    mFirstFrameCbId;
    uint32_t    mAbsoluteFrameCbId;
    IPlaySession *mPtrPlaySession;
    unsigned int mCaSystem;
    unsigned int mCaPid;
    uint8_t mScramblingMode;
    int mOldCaStatus;
    void *mPtrCBData;
    CCIcallback_t mCCICBFn;
    int mCCIRegId;
    int mEntRegId;
    unsigned int mCSDcrc;

// ## Added, temporary Fix
    etMspSrcType mSourcetype;

    // True if display session is for VOD
    bool mIsVod;
};

#endif




