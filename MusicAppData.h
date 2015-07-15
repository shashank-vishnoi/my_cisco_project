//*****************************************************************************
//
// ----------------------------------------------------------------------------
// ------------------------------ CISCO CONFIDENTIAL --------------------------
// ------------------ Copyright (c) 2012, Cisco Systems, Inc.------------------
// ----------------------------------------------------------------------------
//
//*****************************************************************************
//@{
///////////////////////////////////////////////////////////////////////////////
///
/// \file      MusicAppData.h
///
/// \brief     Header file for Music application data class
///
///            Description:  Header containing the resources, definitions,
///            and prototypes needed by the Music Application to provide
///            song text data to the service layer.
///
///            Original Project: Videoscape Voyager Vantage
///
///            Original Author(s): Minh Tu
///
///
///
///
///////////////////////////////////////////////////////////////////////////////
///
//@}

#if !defined(MUSICAPPDATA_H)
#define MUSICAPPDATA_H

#include <stdint.h>
#include <deque>
#include <pthread.h>
#include <string.h>

#include <dlog.h>
#include "BaseAppData.h"

#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
#include "DisplaySession.h"
#endif

#define TEXT_MSG_TYPE       0x9A
#define SVC_TXT_CODE        0x05
#define TEXT_CRC_SEED       0xFFFFFFFF

#ifdef __cplusplus
extern "C" {
#endif

    extern size_t strlcpy(char *, const char *, size_t);

#ifdef __cplusplus
} /* closing brace for extern "C" */
#endif

typedef enum
{
    kText_Orig     = 0x05,
    kText_Snap     = 0x0a,
    kText_Message  = 0x0b,
    kText_StrmDesp = 0x0c,
    kText_Section  = 0x0d,
    kText_DCII     = 0xc0
} StreamType;

typedef enum
{
    kText_ASCII              = 0x00,
    kText_NotUsedBegin       = 0x01,
    kText_NotUsedEnd         = 0x3f,
    kText_FmtEfftorSnglBegin = 0x40,
    kText_FmtEfftorSnglEnd   = 0x9f,
    kText_FmtEfftorMltpBegin = 0xa0,
    kText_FmtEfftorMltpEnd   = 0xff
} Text_ModeByte;

typedef enum
{
    kText_EndOfLine      = 0x3c,
    kText_LeftAdjust     = 0x80,
    kText_RightAdjust    = 0x81,
    kText_CenterAdjust   = 0x82,
    kText_ItalicsOn      = 0x83,
    kText_ItalicsOff     = 0x84,
    kText_UnderlineOn    = 0x85,
    kText_UnderlineOff   = 0x86,
    kText_BoldOn         = 0x87,
    kText_BoldOff        = 0x88
} Text_FormatEffector;



// protocol specific macros
#define text_GetMessageType(d) (d[0])
#define text_GetMessageLength(d) (((d[1] & 0x0f) << 8) | (d[2]))
#define text_GetSegmentState(d) (((d[3] & 0x40) >> 6) & 0x1)
#define text_GetProtocolVersion(d) (d[3] & 0x1f)
#define text_GetTableExtension(d) ((text_GetSegmentState(d) == 1) ? ((d[4] << 8) | d[5]) : 0)
#define text_GetLastSegmentNumber(d) ((text_GetSegmentState(d) == 1) ? ((d[6] << 4) | ((d[7] | 0xf0) >> 4)) : 0)
#define text_GetSegmentNumber(d) ((text_GetSegmentState(d) == 1) ? (((d[7] & 0x0f) << 8) | d[8]) : 0)
#define text_GetLanguageCodeIndex(d) ((text_GetSegmentState(d) == 1) ? 9 : 4)
#define text_GetPage(d) ((text_GetSegmentState(d) == 1) ?  ((d[12] << 8) | d[13]) : ((d[7] << 8) | d[8]))
#define text_GetTextCode(d) ((text_GetSegmentState(d) == 1) ? (d[14] & 0x1f) : (d[9] & 0x1f))
#define text_GetServiceNumber(d) ((text_GetSegmentState(d) == 1) ? ((d[15] << 8) | d[16]) : ((d[10] << 8) | d[11]))
#define text_GetSequence(d) ((text_GetSegmentState(d) == 1) ? d[18] : d[13])
#define text_GetTitleStringState(d) ((text_GetSegmentState(d) == 1) ? (d[19] & 0x04) : (d[14] & 0x4))
#define text_GetSubType(d) ((text_GetSegmentState(d) == 1) ? (d[20] & 0x3f) : (d[15] & 0x3f))
#define text_GetBlockLength(d) ((text_GetSegmentState(d) == 1) ? ((d[21] << 16) | (d[22] << 8) | d[23]) : ((d[16] << 16) | (d[17] << 8) | d[18]))
#define text_GetTitleStringLength(d) ((text_GetTitleStringState(d) == 1) ? ((text_GetSegmentState(d) == 1) ? d[24] : d[19]) : 0)
#define text_GetTitleStringIndex(d) ((text_GetTitleStringState(d) == 1) ? ((text_GetSegmentState(d) == 1) ? 25 : 20) : 0)
#define text_GetMultilingualTextLength(d) ((text_GetTitleStringState(d) == 1) ? (text_GetBlockLength(d) - text_GetTitleStringLength(d) - 1) : (text_GetBlockLength(d)))
#define text_GetMultilingualTextIndex(d) ((text_GetSegmentState(d) == 1) ? ((text_GetTitleStringState(d) == 1) ? (25+text_GetTitleStringLength(d)) : 24) : ((text_GetTitleStringState(d) == 1) ? (20+text_GetTitleStringLength(d)) : 19))
#define text_GetCrcIndex(d) (text_GetMessageLength(d) - 1)
#define text_IsValidModeByte(s) ((((s) >= kText_NotUsedBegin) && ((s) >= kText_NotUsedBegin)) ? FALSE : TRUE)

class MusicAppData : public BaseAppData
{

public:
    MusicAppData(uint32_t maxSize);
    virtual ~MusicAppData();

    int addData(uint8_t *buffer, uint32_t bufferSize);
    int getData(uint8_t *buffer, uint32_t bufferSize);

    uint32_t getTotalSize();
    void getSectionHeader(const uint8_t *buf, tTableHeader *p_header);

private:
    bool text_IsValidData(uint8_t* buf);
    bool text_IsValidCRC(uint8_t* buf);

    pthread_mutex_t  accessMutex;
    char *dataSong;
    uint8_t seqNum;

};

#endif
