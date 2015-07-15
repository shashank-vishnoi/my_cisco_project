/**
   \file psi_ic.h
   \class psi
*/

#if !defined(PSI_IC_H)
#define PSI_IC_H

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
#include "pmt_ic.h"

// Standard Defines
#define kPid      						  0x1FFF
#define ISO_639_LANG_TAG 				  10

#define kAppTableHeaderFromSectionLength  		5  ///< Table Header Size after section Length field
#define kAppPmtHeaderLength               		12 ///< Table length prior to program level descriptors
#define kAppPmtTableLengthMinusSectionLength	3  ///< PMT Table length - Section Length 
///< 3 bytes = (table_id + section_syntax_indicator + section length)>
#define kAppPmtElmStreamInfoLength        		5  ///< Elementary stream info length <type, PID and ES Info Length>
#define kAppTableCRCSize                  		4  ///< Table CRC size in bytes

#define kAppMaxProgramDesciptors 10
#define kAppMaxEsCount  10
#define kMaxESDescriptors  10

/// MPEG stream type definitions <In-line with SDK platform>
#define kElementaryStreamType_Reserved        		0x00          ///< ITU-T | ISO/IEC Reserved
#define kElementaryStreamType_MPEG1_Video     		0x01          ///< ISO/IEC 11172 Video
#define kElementaryStreamType_MPEG2_Video     		0x02          ///< ITU-T Rec. H.262 | ISO/IEC 13818-2 Video or ISO/IEC 11172-2
#define kElementaryStreamType_H263_Video      		0x1A          ///< H.263 Video
#define kElementaryStreamType_H264_Video      		0x1B          ///< H.264 MPEG4 Part 10 Video
#define kElementaryStreamType_H264_SVC_Video  		0x1F          ///< SVC - ISO/IEC 14496-10
#define kElementaryStreamType_H264_MVC_Video  		0x20          ///< MVC - ISO/IEC 14496-10
#define kElementaryStreamType_WVC1_Video      		0x88          ///< Windows Media 9 Video
#define kElementaryStreamType_VC1_Video       		0xEA		  ///< VC-1 Advanced Profile 
#define kElementaryStreamType_VC1_SM_Video    		0xEB		  ///< VC-1 Simple&Main Profile 
#define kElementaryStreamType_DIVX_311_Video  		0x311		  ///< DivX 3.11 coded video 
#define kElementaryStreamType_AVS_Video       		0x42		  ///< AVS video 
#define kElementaryStreamType_Sorenson_h263_Video 	0xF0
#define kElementaryStreamType_VP6_Video       		0xF1
#define kElementaryStreamType_RV40_Video      		0xF2
#define kElementaryStreamType_GI_Video        		0x80          ///< GI Private Video 


#define kElementaryStreamType_MP3_Audio       		0x01           ///< MPEG1/2, layer 3
#define kElementaryStreamType_MPEG1_Audio     		0x03           ///< ISO/IEC 11172 Audio
#define kElementaryStreamType_MPEG2_Audio     		0x04           ///< ISO/IEC 13818-3 Audio
#define kElementaryStreamType_AC3_plus_Audio  		0x06		   ///< Dolby Digital Plus (AC3+ or DDP) audio 
#define kElementaryStreamType_AAC_Audio       		0x0F           ///< ISO/IEC 13818-7 Audio with ADTS transport syntax
#define kElementaryStreamType_AACplus_Audio   		0x11           ///< ISO/IEC 14496-3 Audio with the LATM transport syntax 
#define kElementaryStreamType_AACplus_ADTS_Audio 	0x12	       ///< AAC plus SBR. aka MPEG-4 High Efficiency (AAC-HE), with ADTS (Audio Data Transport Format) */
#define kElementaryStreamType_AVS_Audio       		0x43		   ///< AVS Audio 
#define kElementaryStreamType_AC3_Audio       		0x81		   ///< Dolby Digital AC3 audio 
#define kElementaryStreamType_GI_Audio        		0x81           ///< GI Private Audio - AC3 
#define kElementaryStreamType_DTS_Audio       		0x82		   ///< Digital Surround sound. 
#define kElementaryStreamType_LPCM_HdDvd_Audio  	0x83		   ///< LPCM, HD-DVD mode 
#define kElementaryStreamType_LPCM_BluRay_Audio 	0x84		   ///< LPCM, Blu-Ray mode 
#define kElementaryStreamType_DTS_HD_Audio    		0x85		   ///< Digital Digital Surround sound, HD 
#define kElementaryStreamType_WMA_STD_Audio   		0x86		   ///< WMA Standard   
#define kElementaryStreamType_WMA_PRO_Audio   		0x87		   ///< WMA Professional   
#define kElementaryStreamType_LPCM_DVD_Audio  		0x88		   ///< LPCM, DVD mode 
#define kElementaryStreamType_G726_Audio      		0x89		   ///< G.726 ITU-T ADPCM  
#define kElementaryStreamType_ADPCM_Audio     		0x8A		   ///< MS ADPCM Format 
#define kElementaryStreamType_DVI_ADPCM_Audio 		0x8B		   ///< DVI ADPCM Format 
#define kElementaryStreamType_PCM_Audio       		0x8C		   ///< PCM Audio 
#define kElementaryStreamType_AMR_Audio       		0x8D		   ///< AMR Audio 
#define kElementaryStreamType_DRA_Audio       		0xda
#define kElementaryStreamType_Cook_Audio      		0xF0
#define kElementaryStreamType_A52_VLS_Audio   		0x91

#define kElementaryStreamType_Private_Secs    0x05          ///< ITU-T Rec. H.222.0 | ISO/IEC 13818-1 private sections
#define kElementaryStreamType_PES_Packets     0x06          ///< ITU-T Rec. H.222.0 | ISO/IEC 13818-1 PES packets
#define kElementaryStreamType_MHEG            0x07          ///< ISO/IEC 13522 MHEG 
#define kElementaryStreamType_DSM_CC          0x08          ///< ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Annex A DSM-CC 
#define kElementaryStreamType_DSM_CC_EXT      0x09          ///< ITU-T Rec. H.222.1
#define kElementaryStreamType_DSM_CC_EXT_A    0x0A          ///< ISO/IEC 13818-6 type A
#define kElementaryStreamType_DSM_CC_EXT_B    0x0B          ///< ISO/IEC 13818-6 type B, DSM-CC data message
#define kElementaryStreamType_DSM_CC_EXT_C    0x0C          ///< ISO/IEC 13818-6 type C
#define kElementaryStreamType_DSM_CC_EXT_D    0x0D          ///< ISO/IEC 13818-6 type D
#define kElementaryStreamType_MPEG2_Aux       0x0E          ///< ISO/IEC 13818-1 auxiliary
#define kElementaryStreamType_Visual          0x10          ///< ISO/IEC 14496-2 Visual 
#define kElementaryStreamType_SL_FlexMux1     0x12          ///< ISO/IEC 14496-1 SL-packetized stream or FlexMux in PES packets
#define kElementaryStreamType_SL_FlexMux2     0x13          ///< ISO/IEC 14496-1 SL-packetized stream or FlexMux in ISO/IEC14496 sections 
#define kElementaryStreamType_SyncDownload    0x14          ///< ISO/IEC 13818-6 Synchronized Download Protocol


#define kElementaryStreamType_LPCM            0x83          ///< LPCM audio

#define kElementaryStreamType_SCTE35_DPI      0x86          ///< SCTE35 DPI 
#define kElementaryStreamType_DDPlus_Audio    0x87          ///< Dolby Digital Plus Audio

/**
   \class Psi
   \brief PSI component for media controller to get program specific info
   Implements the PSI interface
*/

class Psi
{

private:

    Pmt              *mPmt;         /**< creating object for storing PMT info */
    pthread_mutex_t  mPsiMutex;

    /*!  \fn   freePsi()
     \brief cleans up the memory during PSI object destruction
     @param None
     @return None
     */
    void freePsi();

    /*!  \fn   lockMutex(void)
     \brief to acquire the mutex
     @param None
     @return None
     */
    void lockMutex(void);

    /*!  \fn   unlockMutex(void)
     \brief to release the mutex
     @param None
     @return None
     */
    void unlockMutex(void);

    /*!  \fn   isAudioStreamType(uint16_t aStreamType)
     \brief returns whether the stream is of type AUDIO or not
     @param uint16_t aStreamType - stream type
     @return TRUE if AUDIO; Otherwise false
     */
    bool isAudioStreamType(uint16_t aStreamType);

public:

    /*!  \fn   Psi()
     \brief Constructor
     @param None
     @return None
     */
    Psi();

    /*!  \fn   ~Psi()
     \brief Destructor
     @param None
     @return None
     */
    ~Psi();

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

    /*!  \fn   eMspStatus  psiStart(uint16_t pgmNo,tCpeSrcHandle srcHandle )
     \brief To start the Psi Processing, called by meadia controller
     @param const tCpeSrcHandle handle: Cpe Source Handle
     @param const uint16_t pgmNo: Requested program number
     @return eMspStatus
     */
    eMspStatus psiStart(MSPSource *aSource);

    /*!  \fn   eMspStatus  BuildPmtTableStructure(uint8_t *pPmtPsiPacket, int PMTSize)
     \brief To build the SAIL PMT table structures from the RAW PMT obtained from the stream
     @param uint8_t *pPmtPsiPacket: pointer pointing to RAW PMT obtained from the stream
     @param int PMTSize: size of the RAW PMT
     @return eMspStatus
     */
    eMspStatus BuildPmtTableStructure(uint8_t *pPmtPsiPacket, int PMTSize);
};

#endif

