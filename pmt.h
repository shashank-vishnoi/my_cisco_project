/**
*  \file pmt.h
*  \class pmt
*/

#if !defined(MSP_PMT_H)
#define MSP_PMT_H


// standard linux includes
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <string>
#include<list>

// cpe includes
#include <cpe_source.h>
#include <cpe_programhandle.h>
#include <cpe_sectionfilter.h>
#include <cpe_mediamgr.h>
#include <cpe_recmgr.h>
#include "MspCommon.h"


// structures

/**
   Structure to create a list of Audio/Video pids along with stream type
*/
typedef struct
{
    uint16_t              streamType;      ///< stream type
    uint16_t              pid;             ///< PID #
} tPid;

/**
   Structure for identifying the PAT/PMT Table ID
*/
typedef struct __attribute__((__packed__))
{
    uint32_t TableID                 : 8;
    uint32_t SectionSyntaxIndicator  : 1;
    uint32_t dummy                   : 1;
    uint32_t Reserved1               : 2;
    uint32_t SectionLength           : 12;
    uint32_t TransportStreamID       : 16;
    uint32_t Reserved2               : 2;
    uint32_t VersionNumber           : 5;
    uint32_t CurrentNextIndicator    : 1;
    uint32_t SectionNumber           : 8;
    uint32_t LastSectionNumber       : 8;
}
tTableHeader;

/**
   Structure for identifying the PMT PID with the program number:
*/
typedef struct __attribute__((__packed__))  //tPat
{
    uint32_t ProgNumber  : 16;
    uint32_t Reserved    : 3;
    uint32_t PID         : 13;
}
tPat;



/**
   \class Pmt
   \brief To store PMT info and send

*/

class Pmt
{

private:

    tCpePgrmHandlePmt  mPmtInfo; // structure to store PMT info
    tCpeMediaTransportStreamParam mTsParams;
    std::list<tPid> mVideoPid;
    std::list<tPid> mAudioPid;
    friend class DisplaySession;
    friend class Psi;
    friend class RecordSession;

public:

    Pmt();
    ~Pmt();

    /*!  \fn   std::list<tPid> * getVideoPidList(void)
     \brief To send the list of video PIDS with along with stream type
     @param None
     @return std::list<tPid>
     */
    std::list<tPid> * getVideoPidList(void); // to send list of video pids


    /*!  \fn   std::list<tPid> * getAudioPidList(void)
     \brief To send the list of Audio PIDS with along with stream type
     @param None
     @return std::list<tPid>
     */
    std::list<tPid> * getAudioPidList(void);  // to send list of Audio Pids


    /*!  \fn   void     getDescriptor(tCpePgrmHandleMpegDesc *descriptor, uint16_t pid)
     \brief to send descriptor data for particular pid and tag
     @param tCpePgrmHandleMpegDesc *descriptor : type of descriptor to fill the data
     @param uint16_t pid : PID number to find out the descriptor
     @return None
     */
    eMspStatus getDescriptor(tCpePgrmHandleMpegDesc *descriptor, uint16_t pid);

    /*!  \fn   void     releaseDescriptor(tCpePgrmHandleMpegDesc *descriptor)
      \brief to release the descriptor data
      @param tCpePgrmHandleMpegDesc *descriptor : type of descriptor data to be released
      @return None
      */
    void releaseDescriptor(tCpePgrmHandleMpegDesc *descriptor);


    /*!  \fn   uint16_t getPcrpid(void)
     \brief to return clock pid
     @param None
     @return uint16_t
     */
    uint16_t getPcrpid(void); // to return clock pid

    eMspStatus getPmtInfo(tCpePgrmHandlePmt* pmtInfo);

    void createAudioVideoListsFromPmtInfo();
    eMspStatus populateFromSaraMetaData(uint8_t *buffer, uint32_t size);
    eMspStatus populateCaptionServiceMetadata(uint8_t *buffer, uint32_t size);

    eMspStatus populateMSPMetaData(uint8_t *buffer, uint32_t size);

private:

    uint32_t   getProgramNumber(void);
    uint32_t   getTransportID(void);
    void       printPmtInfo(void);
    void       freePmtInfo(void);

};


#endif
