/** @file MSPDaigPages.h
 *
 * @brief MSP Diag Pages internal header.
 *
 * @author Rosaiah Naidu Mulluri
 *
 * @version 1.0
 *
 * @date 08-02-2011
 *
 * Copyright: Scientific Atlanta, Inc., A Cisco Company
 */

#ifndef _DIAG_H
#define _DIAG_H
#include <time.h>
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
#include <cpe_source.h>
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#define CAKSTAT_FAIL 0x0
#define CAKSTAT_SUCCESS 0x81
#define MAX_SOURCE_URL 257
#define MAX_CONTENT_URL 257
#define INVALID_CHANNEL_NUMBER -1
#define INVALID_CHANNEL_TYPE   -1

    /*
     * @brief Enum containing return statuses returned by MSP diagnostics
     */
    typedef enum
    {
        kCsciMspDiagStat_OK,  ///< Success.
        kCsciMspDiagStat_InvalidInput, ///< Invalid input
        kCsciMspDiagStat_NoData ///< No applicable data
    }
    eCsciMspDiagStatus;

    /**
    *  This provides information for MSP diagnostics VOD Info
    *  message.
     */
    typedef struct
    {
        uint32_t       sgId; // @brief Service Groud Id
        uint32_t       sessionId; // @brief ondemand session id
        uint32_t       state; // @brief ondemand internal state
        uint32_t       Cakresp; // @brief Response code from SecureMicro (STAT field)
        // entitlementId valid for encrypted VOD asset, N/A for clear
        uint8_t *      entitlementId; // @brief CA entitlement id
        tm             *activatedTime; // @brief session activation time
    } DiagMspVodInfo;

/// enumerator defining RF tuning modes
#if PLATFORM_NAME == IP_CLIENT
    typedef enum
    {
        eSrcRFMode_BaseBandInput,
        eSrcRFMode_Analog,
        eSrcRFMode_QAM16,
        eSrcRFMode_QAM32,
        eSrcRFMode_QAM64,
        eSrcRFMode_QAM128,
        eSrcRFMode_QAM256,
    } tSrcRFMode;
#endif

    /**
    *  This provides information for MSP diagnostics Network Info
    *  message.
     */
    typedef struct
    {
        uint32_t         ChanNo;     // @brief Display Channel Number
        int              SourceId;   // @brief Source ID #
        uint32_t         frequency;

#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
        tCpeSrcRFMode    mode;
#endif
#if PLATFORM_NAME == IP_CLIENT
        tSrcRFMode mode;
#endif
    } DiagMspNetworkInfo;

#if PLATFORM_NAME == IP_CLIENT
    /**
     *  This provides information for MSP diagnostics streaming Info
     *  message.
     */
    typedef struct
    {
        uint32_t ChanNo;     					// @brief Display Channel Number
        int32_t  ChannelType;					// @brief Display Channel Number
        char     SourceUrl[MAX_SOURCE_URL];		// @brief Display Channel Number
        char     ContentUrl[MAX_CONTENT_URL];	// @brief Display HTTP resource url
    } DiagMspStreamingInfo;
#endif

    /**
    *This provides CCI (Copy Control Info)information to Diag pages
    *
    */
    typedef struct
    {
        int cit;
        int aps;
        int emi;
        int rct;  //commonly known as BF Broadcast Flag
        int epn;
        char SrcStr[20] ;
        char DestStr[20] ;
    } DiagCCIData;
    /**
    *  This provides protection type of outputs
    */
    typedef enum
    {
        eDisabled = 0,
        eEnabled,
        eNoneType // As conflicting happens when we include CCH_INC in config.mk. so  changed to eNoneType from eNone
    } eProtType_t;

    typedef enum
    {
        DVI_HDMI,
        YPrPb,
        Port_1394,
        Composite,
        VOD
    } Outputs_t;
    /**
    * This provides information for various outputs
    */
    typedef struct
    {
        eProtType_t ProtType;
        bool bEnabled;
        bool bConstraind;
        char Policy[20];
    } DiagOutputsInfo_t;

    /**
    * This provides information about the components of the playing stream
    */
    typedef struct
    {
        unsigned int    pid;   ///< The PID used for this ES.
        unsigned int    streamType;  ///< MPEG stream type for this ES.
        unsigned char   langCode[3];    ///< ISO-639 language code if the streamType represents audio and the
        bool   selected; 		///< true implied the pid is selected for tuning.
        ///< descriptor is present.
    } DiagComponentsInfo_t;

    eCsciMspDiagStatus Csci_Diag_GetMspVodInfo(DiagMspVodInfo *diagInfo);

    eCsciMspDiagStatus Csci_Diag_GetMspNetworkInfo(DiagMspNetworkInfo *diagInfo);

    eCsciMspDiagStatus Csci_Diag_GetMspCopyProtectionInfo(DiagCCIData *diagCCIInfo);   //added newly

    eCsciMspDiagStatus Csci_Diag_GetMspOutputsInfo(DiagOutputsInfo_t *diagOutputsInfo);   //added newly

    eCsciMspDiagStatus Csci_Diag_GetComponentsInfo(uint32_t *numOfComponents, DiagComponentsInfo_t **diagComponentsInfo);   //added newly

#if PLATFORM_NAME == IP_CLIENT
    eCsciMspDiagStatus Csci_Diag_GetMspStreamingInfo(DiagMspStreamingInfo *streamingInfo);
#endif
}
#endif

