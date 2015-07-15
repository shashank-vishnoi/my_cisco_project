/**
   \file psi.h
   \class psi
*/

#if !defined(PSI_H)
#define PSI_H



// standard linux includes
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <string>
#include<list>

// SAIL includes
#include "sail-mediaplayersession-api.h"
// MSP includes
#include "MspCommon.h"
#include "MSPSource.h"
#include "pmt.h"
#include "psiUtils.h"

// cpe includes
#include <cpe_source.h>
#include <cpe_programhandle.h>
#include <cpe_sectionfilter.h>
#include <cpe_mediamgr.h>
#include <cpe_recmgr.h>

class MSPEventQueue;
class Event;


// Standard Defines
#define kAppPatId 0x00
#define kAppPmtId 0x02
#define kPid      0x1FFF

#define kMaxSecfiltBuffer   512

#define kAppTableCRCSize                  4  ///< Table CRC size in bytes

#define kAppTableHeaderFromSectionLength  5  ///< Table Header Size after section Length field
#define kAppMaxSectFiltReadAttempts 2500

#define kAppMaxProgramDesciptors 10
#define kAppMaxEsCount  10
#define kMaxESDescriptors  10

#define kAppPmtHeaderLength               12
#define kAppPmtTableLengthMinusSectionLength 3



#define kSPTS_SectionLength    13        ///section length of Single Program Transport Stream PAT 
#define kSPTS_PATSize          12        /// size of Single Program Transport Stream PAT (excluding CRC)
#define kSPTS_PATSizeWithCRC   16        /// size of Single Program Transport Stream PAT (including CRC)

#define kPMT_HeaderSize       10        /// size of Fixed header in PMT
#define kPMT_CRCSize           4         /// size of CRC in PMT
#define kPMT_LenSize           2         /// size of length field in PMT
#define kPMT_DesLenSize        1         /// length field size in PMT descriptor
#define kPMT_DesTagSize        1         /// tag size in PMT descriptor
#define kPMT_DesInfoSize       3         /// size of information in PMT descriptor
// enums

/**
   Define all possible external states of PSI for CallBack responses
*/
typedef enum
{
    kPSIIdle = -1,
    kPSIReady,
    kAnalogPSIReady,
    kPSIUpdate,
    kPmtRevUpdate,
    kPSIError,
    kPSITimeOut
} ePsiCallBackState;

/**
   Define all possible internal states for PSI
*/
typedef enum
{
    kPsiStateIdle,
    kPsiStateOpened,
    kPsiStateStarted,
    kPsiWaitForPat,
    kPsiWaitForPmt,
    kPsiProcessingPAT,
    kPsiProcessingPMT,
    kPsiUpdatePMTRevision,
    kPsiWaitForUpdate,
    kPsiProcessingPMTUpdate,
    kPsiStateStopped,
    kPsiStateClosed
} ePsiState;




typedef struct
{
    uint8_t tableId                                         : 8;
    uint8_t sectionSyntaxIndicator                          : 1;
    uint8_t undefined                                       : 1;
    uint8_t reserved1                                       : 2;
    uint8_t sectionLengthHi                                 : 4;
    uint8_t sectionLengthLo                                 : 8;
    uint8_t programNumberHi                                 : 8;
    uint8_t programNumberLo                                 : 8;
    uint8_t reserved2                                       : 2;
    uint8_t versionNumber                                   : 5;
    uint8_t currentNextIndicator                            : 1;
    uint8_t sectionNumber                                   : 8;
    uint8_t lastSectionNumber                               : 8;
    uint8_t reserved3                                       : 3;
    uint8_t PcrPidHi                                        : 5;
    uint8_t PcrPidLo                                        : 8;
    uint8_t reserved4                                       : 4;
    uint8_t programInfoLengthHi                             : 4;
    uint8_t programInfoLengthLo                             : 8;
    uint8_t programInfo[1024]; // not much bytes wasted here
} tPsiPmt;

typedef void (*psiCallbackFunction)(ePsiCallBackState state, void *clientData);


/**
   \class Psi
   \brief PSI component for media controller to get program specific info
   Implements the PSI interface
*/

class Psi
{

private:

    typedef enum {kPsiTimeOutEvent = -1,
                  kPsiStartEvent,
                  kPsiSFCallbackEvent,
                  kPsiPATReadyEvent,
                  kPsiPMTReadyEvent,
                  kPsiUpdateEvent,
                  kPsiRevUpdateEvent,
                  kPsiGetRemoteFilePmt,
                  kPsiFileSrcPMTReady,
                  kPsiExitEvent
                 } ePsiEvent;

    uint16_t             mPgmNo;          /**< program no to look for PMT PID in section filter data */
    uint16_t             mPmtPid;         /**< PMT PID number */
    tCpeSrcHandle        mSrcHandle;      /**< Source Handle to Open section Filter */
    tCpePgrmHandle       mPgrmHandleSf;   /**< Program Handle to Set/Start the Section Filter */
    ePsiState            mState;          /**< to check section filter state internally in psi */
    ePsiCallBackState    mCbState;        /**< To send the state of PSI to the media controller */
    uint8_t              *mPSecFilterBuf; /**< Buffer to store the section filter call back data */
    uint32_t             mSecFiltSize;    /**< To check the size of section filter data buffer */

    tCpeSFltCallbackID   mSfCbIdSectionData; /**< Call back ID for to receive valid data buffer */
    tCpeSFltCallbackID   mSfCbIdError;       /**< Call back ID for to identify Error */
    tCpeSFltCallbackID   mSfCbIdTimeOut;       /**< Call back ID for Timeout */

    uint8_t              mSfStarted;         /**< Boolean type of variable to know section filter started or not */

    tCpeSFltFilterGroup  mSfGroup;           /**< To set the section filter as group for identifying Table ID */
    int                  mSectFileReadAttempts; /**< Max no of times section filter attempted to get the data */

    Pmt                  *mPmt;         /**< creating object for storing PMT info */

    void                 *mCbClientContext;   /**< To store the client context data */
    psiCallbackFunction  mCallbackFn;           /**< To store and call the registered call back function */

    uint8_t   *mRawPmtPtr, *mRawPatPtr;
    unsigned int mRawPmtSize, mRawPatSize;
    MSPEventQueue* psiThreadEventQueue;
    pthread_t psiEventHandlerThread;
    pthread_mutex_t  mPsiMutex;
    psiUtils *m_ppsiUtils; //Creating object to access psi utils

public:

    Psi();
    ~Psi();

    /*!  \fn   eMspStatus  psiStart(uint16_t pgmNo,tCpeSrcHandle srcHandle )
     \brief To start the Psi Processing, called by meadia controller
     @param const tCpeSrcHandle handle: Cpe Source Handle
     @param const uint16_t pgmNo: Requested program number
     @return eMspStatus
     */
    eMspStatus psiStart(const MSPSource *aSource);
    eMspStatus psiStart(std::string recordUrl);


    /**  \fn   eMspStatus  psiStop(void)
     \brief To stop the Psi processing, called by meadia controller
     @param None
     @return eMspStatus
     */
    eMspStatus psiStop(void);

    /*!  \fn   eMspStatus  registerPsiCallback(psiCallbackFunction cb_fun, void* clientData)
     \brief TO Register the PSI Call back to receive PID information once PSI Ready
     @param psiCallbackFunction cb_fun: Name of the function to call once PSI ready
     @param void* clientData: To store the client context data
     @return eMspStatus
     */
    void registerPsiCallback(psiCallbackFunction cb_fun, void* clientData);

    /*!  \fn   eMspStatus  unRegisterPsiCallback(void)
     \brief To UN-Register the PSI Call back
     @param None
     @return eMspStatus
     */
    void unRegisterPsiCallback(void);

    /*!  \fn   Pmt*  getPmtObj(void)
     \brief to get the object of the PMT class
     @param None
     @return Pmt class object
     */
    Pmt* getPmtObj(void);

    /*!  \fn   eMspStatus  psiProcessDvrPlayBkPidInfo(tCpeSrcPFPidDef playBkPidInfo )
       \brief To start the Psi Processing, called by meadia controller
       @param const uint16_t pgmNo: Requested program number
       @return eMspStatus
       */
    eMspStatus getComponents(tComponentInfo *info, uint32_t infoSize, uint32_t *count, uint32_t offset);

    /*!  \fn   uint16_t getProgramNo(void)
         \brief to get the Current program Number
         @param None
         @return mPgmNo
         */
    uint16_t getProgramNo(void);

    eMspStatus getRawPmt(uint8_t **PmtPtr, unsigned int *PmtSize, uint16_t *PmtPid);
    eMspStatus getRawPat(uint8_t **PatPtr);
    void freePsi();

    void lockMutex(void);
    void unlockMutex(void);

    uint32_t getMusicPid()
    {
        return musicPid;
    }

private:

    uint32_t musicPid;
    static void* eventthreadFunc(void *data);
    bool  dispatchEvent(Event *evt);
    eMspStatus queueEvent(ePsiEvent evtyp);


    // To open the Section Filter
    eMspStatus open(void);


    eMspStatus setSectionFilterGroup();
    void       callbackToClient(ePsiCallBackState state);
    int        registerSfCallbacks();
    int        registerSectionFilterCallback(tCpeSFltCallbackTypes callbackType,
            tCpeSFltCallbackID callbackId);

    // To Start the Section filter
    eMspStatus start(void);

    eMspStatus startSectionFilter(uint16_t pid, bool aPsiUpdateFlag = false);

    static void sectionFilterCallbackFun(tCpeSFltCallbackTypes type, void *userdata, void *pCallbackSpecific);

    void setCallbackData(tCpeSFltBuffer *pSfltBuff);

    eMspStatus parseSectionFilterCallBackData();

    // For parsing the PAT info received from section filters
    eMspStatus collectPatData(uint32_t sectionLength);

    // For parsing the PMT info received from section filters
    eMspStatus collectPmtData(uint32_t sectionLength);

    // To stop the section filter
    eMspStatus stop(void);
    // To close the section filter
    eMspStatus close(void);

    //dump TS table header
    void logTableHeaderInfo(tTableHeader *tableHdr);

    //get section  header.
    void getSectionHeader(const uint8_t *buf, tTableHeader *p_header);

    //get program number
    void getProgram(const uint8_t *buf, tPat *p_program);


// CA support routines -- some of this is temporary until we refactor PSI and DVR to handle metadata differently
    uint32_t caMetaDataSize;
    uint8_t *caMetaDataPtr;
    uint32_t caDescriptorLength;
    uint8_t *caDescriptorPtr;

    eMspStatus processCaMetaDataDescriptor(uint8_t *buffer, uint32_t size);
    eMspStatus processCaptionServiceMetaDataDescriptor(uint8_t *buffer, uint32_t size);
    eMspStatus processCaMetaDataBlob(uint8_t *buffer, uint32_t size);
    eMspStatus processMSPMetaData(uint8_t *buffer, uint32_t size);
    eMspStatus processSaraMetaData(uint8_t *buffer, uint32_t size);
    eMspStatus createSaraCaMetaDataDescriptor();
    bool isAudioStreamType(uint16_t aStreamType);

    eMspStatus DumpPmtInfo(uint8_t *pPmtPsiPacket, int PMTSize);

// MRDVR support routines
    eMspStatus ParsePsiRemoteSource(const MSPSource *aSource);

    bool mDeletePsiRequested;
    unsigned int mCurrentPMTCRC;


public:
    unsigned int crc32(unsigned int seed, const char *buf, unsigned int len);
    inline uint32_t getCAMetaDataLength(void)
    {
        return caMetaDataSize;
    }
    inline unsigned char* getCAMetaDataPtr(void)
    {
        return caMetaDataPtr;
    }
    inline uint32_t getCADescriptorLength(void)
    {
        return caDescriptorLength;
    }
    inline unsigned char* getCADescriptorPtr(void)
    {
        return caDescriptorPtr;
    }

};

#endif
