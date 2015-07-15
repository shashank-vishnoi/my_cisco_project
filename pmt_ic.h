/**
*  \file pmt_ic.h
*  \class pmt
*/

#if !defined(MSP_PMT_IC_H)
#define MSP_PMT_IC_H


// standard linux includes
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <string>
#include<list>

#include "MspCommon.h"


// structures

/**
 *  Structure to create a list of Audio/Video pids along with stream type
 */
typedef struct
{
    uint16_t              streamType;      ///< stream type
    uint16_t              pid;             ///< PID #
} tPid;

/**
 *  Structure to hold MPEG descriptor <tag, length and data>
 */
typedef struct
{
    uint8_t                     tag;             ///< descriptor tag
    uint32_t                    dataLen;         ///< descriptor data length
    uint8_t                     *data;           ///< descriptor data
} tMpegDesc;

/**
 *  Structure to hold elementary stream information
 *  > stream type, PID number, number of descriptors present for the stream
 *  > Also a pointer that holds the descriptor information <tag, length and data>
 */
typedef struct
{
    uint16_t                    streamType;      ///< stream type
    uint16_t                    pid;             ///< PID #
    uint8_t                     descCount;       ///< es level descriptor loop count
    uint8_t                     reserved[3];     ///< Added for byte padding.
    tMpegDesc                   **ppEsDesc;      ///< es level descriptor array
} tEsData;

/**
 *  Structure to hold PMT structure information
 *  > version number
 *  > PCR PID
 *  > number of program level descriptors
 *  >>>> Program level descriptor array
 *  > number of elementary streams
 *  >>>> Elementary stream loop
 */
typedef struct
{
    uint32_t                   versionNumber;   ///< Version Number
    uint16_t                   clockPid;        ///< Clock PID.
    uint8_t                    pgmDescCount;    ///< Program level descriptor loop count
    uint8_t                    esCount;         ///< Number of elementary stream
    tMpegDesc                  **ppPgmDesc;     ///< Program level descriptor array
    tEsData                    **ppEsData;      ///< Elementary stream loop
} tPmt;

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
   \class Pmt
   \brief To store PMT info and send
**/
class Pmt
{

private:

    tPmt  mPmtInfo; // structure to store PMT info
    std::list<tPid> mVideoPid;
    std::list<tPid> mAudioPid;
    friend class Psi;

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
     @param tMpegDesc *descriptor : type of descriptor to fill the data
     @param uint16_t pid : PID number to find out the descriptor
     @return None
     */
    eMspStatus getDescriptor(tMpegDesc *descriptor, uint16_t pid);

    /*!  \fn   void     releaseDescriptor(tMpegDesc *descriptor)
      \brief to release the descriptor data
      @param tMpegDesc *descriptor : type of descriptor data to be released
      @return None
      */
    void releaseDescriptor(tMpegDesc *descriptor);


    /*!  \fn   uint16_t getPcrpid(void)
     \brief to return clock pid
     @param None
     @return uint16_t
     */
    uint16_t getPcrpid(void); // to return clock pid

    /*!  \fn   uint16_t getPmtInfo(tPmt* pmtInfo)
     \brief to return PMT structure information
     @param None
     @return eMspStatus
     */
    eMspStatus getPmtInfo(tPmt* pmtInfo);

    /*!  \fn   void createAudioVideoListsFromPmtInfo()
     \brief creates Audio and Video lists from PMT structure information
     @param None
     @return None
     */
    void createAudioVideoListsFromPmtInfo();

private:

    /*!  \fn   void       printPmtInfo(void);
     \brief dumps (for debugging purpose) the PMT structure information
     @param None
     @return None
     */
    void       printPmtInfo(void);

    /*!  \fn   void       freePmtInfo(void)
     \brief frees the memory allocated to descriptors in PMT structure
     @param None
     @return None
     */
    void       freePmtInfo(void);
};

#endif

