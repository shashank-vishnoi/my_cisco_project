/**
 \file MspCommon.h
 */

#if !defined(MSPCOMMON_H)
#define MSPCOMMON_H

#include <ulog.h>
#include <dlog.h>
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
#include "cpe_source.h"
#endif
//#include <glib.h>

enum eMspStatus
{
    kMspStatus_Ok,
    kMspStatus_Error,
    kMspStatus_OutofMemory,
    kMspStatus_BadParameters,
    kMspStatus_StateError,
    kMspStatus_NotSupported,
    kMspStatus_AvpmError,
    kMspStatus_CpeSrcError,
    kMspStatus_CpeMediaError,
    kMspStatus_CpeRecordError,
    kMspStatus_PsiError,
    kMspStatus_SdvError,
    kMspStatus_PsiThreadError,
    kMspStatus_CpeSecionFilterError,
    kMspStatus_CpeProgramHandleError,
    kMspStatus_BadSource,
    kMspStatus_Loading,
    kMspStatus_WaitForTuner,
    kMspStatus_ResMonUnavailable,
    kMspStatus_ResourceDenied,
    kMspStatus_TimeOut
};

const char* const eMspStatusString[] =
{
    "kMspStatus_Ok",
    "kMspStatus_Error",
    "kMspStatus_OutofMemory",
    "kMspStatus_BadParameters",
    "kMspStatus_StateError",
    "kMspStatus_NotSupported",
    "kMspStatus_AvpmError",
    "kMspStatus_CpeSrcError",
    "kMspStatus_CpeMediaError",
    "kMspStatus_CpeRecordError",
    "kMspStatus_PsiError",
    "kMspStatus_SdvError",
    "kMspStatus_PsiThreadError",
    "kMspStatus_CpeSecionFilterError",
    "kMspStatus_CpeProgramHandleError",
    "kMspStatus_BadSource",
    "kMspStatus_Loading",
    "kMspStatus_WaitForTuner",
    "kMspStatus_ResMonUnavailable",
    "kMspStatus_ResourceDenied",
    "kMspStatus_TimeOut"
};

const  char * const cciEmiString[] =
{
    " Copy Free ",
    " Copy No MOre ",
    " Copy Once ",
    " Copy Never "
};
const  char * const cciApsString[] =
{
    " Encoding Off ",
    " AGC ON, Split Burst Off " ,
    " AGC ON, 2 Line Split Burst On ",
    " AGC ON, 2 Line Split Burst On "
};

const  char * const cciCitString[] =
{
    " No Image Constraint ",
    " Image Constraint required"
};

#define  DEFAULT_RESTRICTIVE_CCI  0x0B  // The byte is 00001011 in binary: CIT = 0; APS = 2 line, 10; EMI=11, copy never
#define  RF_SOURCE_URI_PREFIX    "sctetv://"
#define  FILE1_SOURCE_URI_PREFIX "avfs://"
#define  FILE2_SOURCE_URI_PREFIX "sadvr://"
#define  PPV_SOURCE_URI_PREFIX   "sappv://"
#define  NETWORK_URI_PREFIX      "rf:"
#define  MRDVR_SOURCE_URI_PREFIX "mrdvr://"
#define  VOD_SOURCE_URI_PREFIX   "lscp://"
#define  AUDIO_SOURCE_URI_PREFIX "audio://"
#define  QAMRF_SOURCE_URI_PREFIX "qamrf://"
#define  RTSP_SOURCE_URI_PREFIX  "rtsp://"

/* The local recording playback and Mrdvr recording streaming both have the
   streaming URL starting with "avfs://". Hence to distinguish between the 2
   the URL received in HN serve request (i.e, avfs:\\) will be converted to
   svfs:\\, before entering into MSP framework (i.e., MSP session load) */
#define  MRDVR_REC_STREAMING_URL "svfs://"

#define  MRDVR_TSB_STREAMING_URL "avfs://item=live/sctetv://"
#define  MRDVR_TSB_PPV_STREAMING_URL "avfs://item=live/sappv://"
#define  HN_ONDEMAND_STREAMING_URL	"avfs://item=vod/"
#define  MRDVR_TUNER_STREAMING_URL "avfs://item=vod/sctetv://"
#define  MRDVR_TUNER_PPV_STREAMING_URL "avfs://item=vod/sappv://"

// enum for modulation technique used for video
typedef enum
{
    Mode_Reserved   = 0x00,
    Mode_QAM16      = 0x06,
    Mode_QAM32      = 0x07,
    Mode_QAM64      = 0x08,
    Mode_QAM128_sea = 0x0C,
    Mode_QAM128     = 0x0A,
    Mode_QAM256     = 0x10
} ModulationType;

// This class will be utilized to add common functionality which can be used across MSP modules.
class MspCommon
{
public:
    /**
    * \param mode rf modulation type
    * \param cpeMode store RF modulation type in tCpeSrcRFMode format.
    * \return true if input value is mapped successfully with tCpeSrcRFMode enum type otherwise returns false.
    * \brief This function convert RF modulation type in to tCpeSrcRFMode format.
    */
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
    static bool getCpeModeFormat(uint8_t mode, tCpeSrcRFMode& cpeMode);
#endif
};


#endif

