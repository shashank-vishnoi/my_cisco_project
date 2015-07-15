/**
   \file RecordSession.h
   \class RecordSession
*/

#if !defined(RECORDSESSION_H)
#define RECORDSESSION_H

// standard linux includes
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <string>

// cpe includes
#include <cpe_source.h>
#include <cpe_programhandle.h>
#include <cpe_recmgr.h>

//boost includes
#include <boost/signals2.hpp>
#include <boost/bind.hpp>

// msp includes
#include "MspCommon.h"
#include "psi.h"
#include "AnalogPsi.h"
#include "Cam.h"
/**
   \class RecordSession
   \brief this class will be the gateway for handling requests to set up the A/V
*/

#define TSB_MAX_FILENAME_SIZE 25
//TODO -Needs to maintain an array of two TSB files and Recordsession needs to choose one of them based on availability
#define TSB_FILE_NAME "dvr001"
#define TSB_SIZE_IN_SECS 1000
#define TSB_VIDEO_BITRATE 17000
#define DVR_MNT_PNT_LEN  25

#define REC_SESSION_CHECKSUM_WORD 0xDB

#define REC_METADATA_TAGS  1  //currently fixing as 1


enum eRecordSessionState
{
    kRecordSessionIdle,
    kRecordSessionOpened,
    kRecordSessionConfigured,
    kRecordSessionStarted,
    kRecordSessionConversionStarted,
    kRecordSessionStartedIncomplete,
    kRecordSessionStopped,
    kRecordSessionClosed
} ;


typedef void (*RecordCallbackFunction)(tCpeRecCallbackTypes type, void *clientData);

typedef void (recordSessioncallbacktype)(eIMediaPlayerSignal, eIMediaPlayerStatus);
typedef boost::function<recordSessioncallbacktype> callbackfunctiontype;


class MSPRecordSession
{
public:

    /*!  \fn   eMspStatus  open(const tCpeSrcHandle handle)
     \brief Open the RecordSession to setup the record resources.
     @param const tCpeSrcHandle handle: Cpe Source Handle
     @return eMspStatus
     */
    eMspStatus  open(const tCpeSrcHandle handle,
                     int *tsbHardDrive,
                     unsigned int tsb_number,
                     Psi *psi,
                     int sourceId,
                     std::string first_fragment_file);

    // TODO: Re-visit this method. The interface has changed since 3.1 was started (see the Psi version of open).
    // We will have to re-write this method to conform to the new interface and maintained state of the class
    eMspStatus  open(const tCpeSrcHandle handle, int *tsbHardDrive, unsigned int tsb_number,
                     AnalogPsi *psi, int sourceId, std::string first_fragment_file);


    /*!  \fn   eMspStatus  start(void)
     \brief Start the TSB session.
     @param None
     @return eMspStatus
     */
    eMspStatus  start();

    eMspStatus  start(AnalogPsi *psi);

    /*!  \fn   eMspStatus  stop(void)
     \brief Stop the TSB session.
     @param None
     @return eMspStatus
     */
    eMspStatus  stop(void);

    /*!  \fn   eMspStatus  close(void)
     \brief Close the TSB session.
     @param None
     @return eMspStatus
     */
    eMspStatus  close(void);

    eMspStatus restartForPmtUpdate(eRecordSessionState  RecState);
    eMspStatus stopForPmtUpdate(eRecordSessionState * pRecState);


    /*!  \fn   eMspStatus  startConvert(void)
     \brief Start converting the TSB for persistent recording.
     @param None
     @return eMspStatus
    */
    eMspStatus  startConvert(std::string recfilename, uint32_t nptRecordStartTime, uint32_t nptRecordStopTime, Psi *psi);
    eMspStatus  startConvert(std::string recfilename, uint32_t nptRecordStartTime, uint32_t nptRecordStopTime, AnalogPsi *psi);

    // TODO: Re-visit this method. The interface has changed since 3.1 was started (see the two versions of startConvert above).
    // We will have to re-write this method or add a new version to handle analog.
    eMspStatus  startConvert(std::string recfilename, uint32_t nptRecordStartTime, uint32_t nptRecordStopTime);


    /*!  \fn   eMspStatus  stopConvert(void)
     \brief Stop converting the TSB for persistent recording
     @param None
     @return eMspStatus
    */
    eMspStatus  stopConvert(void);

    eMspStatus GetCurrentNpt(uint32_t* pNpt);

    eMspStatus GetStartNpt(uint32_t* pNpt);

    /*!  \fn   eMspStatus  registerRecordMediaCallback(DisplayMediaCallbackFunction cb)
     \brief Register the callback fn to receive the CPE Media Hal callback status.
     @param RecordMediaCallbackFunction cb: callback function to be registered with DisplaySession
     @return eMspStatus
     */

    eMspStatus  registerRecordCallback(RecordCallbackFunction cb, void *clientdata);


    /*!  \fn   eMspStatus  unregisterRecordMediaCallback(void)
     \brief Unregister the callback fn from the RecordSession.
     @param Psi object
     @return eMspStatus
     */
    void  unregisterRecordCallback(void);

    eMspStatus  InjectPmtData(void);



    /*!  \fn   void        performCb(tCpeMediaCallbackTypes type)
     \brief Perform callback function. This method is made public because of the static callback registered
            with the cpe media hal.
     @param tCpeMediaCallbackTypes type: media callback type defined in the cpe media hal api.
     @return eMspStatus
     */
    void performCb(tCpeRecCallbackTypes type);

    eMspStatus  writeTSBMetaData();     // called from dvr controller when tsb starts

    ~MSPRecordSession();
    MSPRecordSession();

    char* GetTsbFileName();
    boost::signals2::connection setCallback(callbackfunctiontype);
    void clearCallback(boost::signals2::connection);
    void SetCCICallback(void *mCBData, CCIcallback_t cb);

    void UnSetCCICallback();


    /*! \fn  eMspStatus  tsbPauseResume  (bool isPause)
     \brief Do Pause Resume for the current TSB session to handle TSB error scenarios. When paused the TSB
            will not grow with contents. And when resumed the TSB starts growing with contents again.

     @param isPause : pass the value "true" to pause TSB session and "false" to resume back the session.
     @return eMspStatus
    */
#if PLATFORM_NAME == G8
    eMspStatus tsbPauseResume(bool isPause);
#endif

    boost::signals2::signal<recordSessioncallbacktype> callback;
    bool mCbIsSet;

    tCpePgrmHandle getRecordingHandle()
    {
        return mRecHandle  ;
    }
    void updateCCI(uint8_t ccivalue);
private:
    char mtsb_filename[TSB_MAX_FILENAME_SIZE];
    tCpeSrcPFPidDef *mPids;
    uint32_t mUseAC3;
    tCpeRecCallbackID mRecCbRecStartId, mRecCbDiskFullId, mRecCbConvComplete;
    uint8_t mCCiValue;
    tCpePgrmHandle mRecHandle;
    tCpeSrcHandle mSourceHandle;

    tCpeRecInjectID mInjectPat;
    tCpeRecInjectID mInjectPmt;

    eRecordSessionState mState;
    RecordCallbackFunction mCb;
    void *mRecvdData;   /**< To store the client context data */
    tCpePgrmHandlePmt  mPmtInfo;
    bool mIsCAMetaWritten;
    Psi  *mPsiptr;
    uint16_t             mPgmNo;
    uint8_t *mRawPmtPtr, *mRawPatPtr;
    unsigned int mRawPmtSize;
    uint16_t mPmtPid;

    static void RecCallbackFun(tCpeRecCallbackTypes type, void *userdata, void *tsbspecific);
    eMspStatus register_callbacks();
    eMspStatus unregister_callbacks();
    eMspStatus  openTSB(const tCpeSrcHandle srcHandle);

    eMspStatus  startConvertAnalogTSB(std::string recfilename, uint32_t nptRecordStartTime, uint32_t nptRecordStopTime);

    eMspStatus  setTSB(std::string first_fragment_file, bool bUseCaBlob = false, bool bRemapPids = false);
    eMspStatus  setTSB(AnalogPsi *psi);
    eMspStatus  startTSB();
    eMspStatus  startTSB(AnalogPsi *psi);
    eMspStatus  stopTSB(void);
    eMspStatus  closeTSB(void);
//    eMspStatus  startConvertTSB(std::string recfilename,float nptRecordStartTime, float nptRecordStopTime);

    eMspStatus  stopConvertTSB(void);
    eMspStatus  getAudioPid();
    eMspStatus  getVideoPid(std::string first_fragment_file, bool bUseCaBlob = false, bool bRemapPids = false);
    eMspStatus  getClockPid();
    int16_t calculate_checksum(uint8_t *databuf, size_t size);
    tPid getAudioPidStruc(Pmt *pmt, uint16_t pid, bool & bFound);
// metadata related stuff
    eMspStatus  saveCAMetaData();
    eMspStatus  savePidsMetaData();
    eMspStatus  writeAllMetaData(std::string);
    eMspStatus  writeAllAnalogMetaData(std::string);

    uint32_t mCaMetaDataSize;
    uint8_t *mCaMetaDataPtr;
    uint8_t mCaDescriptorLength;
    uint8_t *mCaDescriptorPtr;
    tCpeRecDataBasePidTable *mDbPids;
    uint32_t mDbPidsSize;
    uint32_t mCaptionDescriptorLength;

// CAM support functions
    static void *secFltCallbackFunction(tCpeSFltCallbackTypes type, void* userdata, void* pCallbackSpecific);
    eMspStatus startCaFilter(uint16_t pid, const tCpeSrcHandle handle);
    eMspStatus stopCaFilter(void);
    void *sfCallback(tCpeSFltCallbackTypes type, void* pCallbackSpecific);

    // CAM support variables
    tCpePgrmHandle mSfHandle;
    tCpeSFltCallbackID mSfCbIdError;
    tCpeSFltCallbackID mFCbIdSectionData;
    tCpeSFltCallbackID mSfCbIdSectionData;
    IRecordSession *mRecordSession;
    unsigned int mCaSystem;
    unsigned int mCaPid;
    unsigned int mScramblingMode;
    static void caDvrMetadataCallback(void *ctx);
    void metadataCallback(void);
    void SetTsbFileName(int *tsbHardDrive, unsigned int tsb_number);
    bool mbIsAnalog;
    int mEntRegId;
    void *mPtrCBData;
    CCIcallback_t mCCICBFn;

};

#endif




