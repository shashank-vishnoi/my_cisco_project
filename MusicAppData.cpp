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
/// \file      MusicAppData.cpp
///
/// \brief     Implementation file for Music application data class
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


#include "MusicAppData.h"
#include "csci-meta-charset-api.h"
#include <memory.h>
#include <stdlib.h>
extern unsigned int crctab[256];

#define MAX_LINE_SIZE 256

MusicAppData::MusicAppData(uint32_t maxSize)
    : BaseAppData(maxSize)
{
    FNLOG(DL_MSP_MPLAYER);

    pthread_mutex_init(&accessMutex, NULL);
    seqNum = 0; // coverity fix
}

MusicAppData::~MusicAppData()
{

    FNLOG(DL_MSP_MPLAYER);
}

int MusicAppData::addData(uint8_t *buffer, uint32_t bufferSize)
{
    int datasize = 0;
    FNLOG(DL_MSP_MPLAYER);

#if PLATFORM_NAME == IP_CLIENT
    tTableHeader *header = NULL, _header;
    uint8_t *buffer_backup = buffer;
    memset(&_header, 0, sizeof(_header));
    getSectionHeader(buffer_backup, &_header);
    header = &_header;
    bufferSize = header->SectionLength + 3;
    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s:%d GETADDDATA printing (SectionSize <%d> = SectionLength <%d> + 3 ) \n", __FUNCTION__, __LINE__, bufferSize, header->SectionLength);
#endif

    if (bufferSize  > maxAllowedSize)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s:%d max size exceeded.  drop data\n", __FUNCTION__, __LINE__);
        return datasize;
    }

    pthread_mutex_lock(&accessMutex);
    if (text_IsValidData(buffer))
    {

        if (seqNum != text_GetSequence(buffer)) // new song ID
        {
            uint8_t *songText = &buffer[text_GetMultilingualTextIndex(buffer)];
            char* song = NULL;
            std::string JSON_SONG = "{ \"song\": { \"info\":[";
            uint32_t utflen = 0;
            unsigned char* utf8str = NULL;
            int pos_kText_LeftAdjust = 0, pos_kText_ASCII =  0, ref_pos = 0;
            //The actual data for a songText buffer begins at songText+2.
            //Also, each valid data line is ended by ascii 0x80(kText_LeftAdjust) , 0x0(kText_ASCII) and another ascii character.
            //So, we parse the buffer till 0x80 to find 1 valid line of data.
            //Next Valid data begins after incrementing songText pointer by 2 after 0x0.
            //This implementation has been changed to add support for multi-byte characters (eg. French) CDET# CSCup67380.
            //Please refer the MSP tag vvv4.1-17.00.00 for the original implementation which supported only single byte characters.
            char *s = strchr((char*)songText + 2, kText_LeftAdjust);
            if (s != NULL)
            {
                pos_kText_LeftAdjust = s - (char*)(songText + 2);
            }

            char *p = strchr((char*)songText + 2, kText_ASCII);

            if (p != NULL)
            {
                pos_kText_ASCII = p - (char*)(songText + 2);
            }
            char buf[MAX_LINE_SIZE];
            memset(buf, '\0', MAX_LINE_SIZE);
            char buf1[MAX_LINE_SIZE];
            if (pos_kText_ASCII - pos_kText_LeftAdjust == 1)
            {
                memcpy(buf, songText + 2, pos_kText_LeftAdjust);
            }
            ref_pos = pos_kText_ASCII;

            utf8str = w1252_to_utf8_encoding((uint8_t*)buf, MAX_LINE_SIZE, utflen);
            asprintf(&song, "\"%s\",", utf8str);
            JSON_SONG = JSON_SONG + song;
            while (1)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, " Looping till the valid terminating character in the buffer is found");
                s = strchr((char*)songText + 2 + ref_pos + 2, kText_LeftAdjust);
                if (s != NULL)
                {
                    pos_kText_LeftAdjust = s - (char*)(songText + 2);
                }
                p = strchr((char*)songText + 2 + ref_pos + 2, kText_ASCII);
                if (p != NULL)
                {
                    pos_kText_ASCII = p - (char*)(songText + 2);
                }

                memset(buf1, '\0', MAX_LINE_SIZE);
                if (pos_kText_ASCII - pos_kText_LeftAdjust == 1)
                {
                    memcpy(buf1, songText + 2 + ref_pos + 2, pos_kText_LeftAdjust - (ref_pos + 2));
                    ref_pos = pos_kText_ASCII;
                    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s : %d :  buffer data is %s", __FUNCTION__, __LINE__, buf1);
                }
                else
                {
                    memcpy(buf1, songText + 2 + ref_pos + 2 , pos_kText_LeftAdjust - (ref_pos + 2));
                    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s : %d :  buffer data is %s", __FUNCTION__, __LINE__, buf1);
                    utf8str = w1252_to_utf8_encoding((uint8_t*)buf1, MAX_LINE_SIZE, utflen);
                    asprintf(&song, "\"%s\"", utf8str);
                    JSON_SONG = JSON_SONG + song;
                    break;
                }

                utf8str = w1252_to_utf8_encoding((uint8_t*)buf1, MAX_LINE_SIZE, utflen);
                asprintf(&song, "\"%s\",", utf8str);
                JSON_SONG = JSON_SONG + song;
            }
            JSON_SONG = JSON_SONG + "]}}";
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "FINAL JSON_SONG =  %s   ", JSON_SONG.c_str());
            asprintf(&dataSong, "%s", JSON_SONG.c_str());
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "dataSong =  %s   ", dataSong);
            if (song != NULL)
            {
                free(song);
                song = NULL;
            }
            datasize = JSON_SONG.length();
            seqNum = text_GetSequence(buffer);
        }
        else
        {
            dlog(DL_MSP_MPLAYER, DLOGL_MINOR_DEBUG, "%s: ignore same seq # %d", __FUNCTION__, seqNum);
        }

        dlog(DL_MSP_MPLAYER, DLOGL_MINOR_DEBUG, "%s: datasize %d seq # %d", __FUNCTION__, datasize, seqNum);
    } // end if text_IsValidData()
    pthread_mutex_unlock(&accessMutex);

    return datasize;
}

int MusicAppData::getData(uint8_t *buffer, uint32_t bufferSize)
{

    FNLOG(DL_MSP_MPLAYER);
    uint32_t sizeToCopy = 0;

    (void)bufferSize;

    pthread_mutex_lock(&accessMutex);
    sizeToCopy = strlen(dataSong);
    strlcpy((char*)buffer, dataSong, sizeToCopy + 1);

    if (dataSong != NULL)
    {
        free(dataSong);
        dataSong = NULL;
    }

    pthread_mutex_unlock(&accessMutex);

    dlog(DL_MSP_MPLAYER, DLOGL_NORMAL, "%s: got %d bytes \n", __FUNCTION__, sizeToCopy);

    return sizeToCopy;
}

uint32_t MusicAppData::getTotalSize()
{
    uint32_t total = 0;

    return total;
}
////////////////////////////////////////
// PRIVATE FUNCTION: text_IsValidData //
////////////////////////////////////////

bool MusicAppData::text_IsValidData(uint8_t* buf)
// This module checks data info byte-by-byte to see if it's valid for display.
{
    bool result = false;

    uint8_t msgType = text_GetMessageType(buf);
    bool bValidCrc = text_IsValidCRC(buf);
    uint8_t ver = text_GetProtocolVersion(buf);
    uint8_t segState = text_GetSegmentState(buf);
    uint8_t page = text_GetPage(buf);
    uint8_t textCode = text_GetPage(buf);
    uint8_t serviceNo = text_GetServiceNumber(buf);

    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s, %d, mspType 0x%x, crcValid %d, version %d, segSTate %d, page %d, text code 0x%x, service No %d",
         __FUNCTION__, __LINE__, msgType, bValidCrc, ver, segState, page, textCode, serviceNo);


    if ((msgType == TEXT_MSG_TYPE)
            && (bValidCrc)
            && (ver == 0x00)
            && (segState == 0x00)
            && (text_GetPage(buf) == 0x00)
            && (text_GetTextCode(buf) == SVC_TXT_CODE)
            && (text_GetServiceNumber(buf) == 0x00))
    {
        result = true;
    }

    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s,result %d", __FUNCTION__, result);
    return result;
}


///////////////////////////////////////
// PRIVATE FUNCTION: text_IsValidCRC //
///////////////////////////////////////

bool MusicAppData::text_IsValidCRC(uint8_t* buf)

// This module checks if data has valid CRC.
{

    int length = text_GetMessageLength(buf) + 3;
    unsigned int crc;

    // compute CRC32 over the whole buffer, including the CRC
    for (crc = TEXT_CRC_SEED; length > 0; length--, buf++)
    {
        crc = (crc << 8) ^ (crctab[0xff & ((crc >> 24) ^ (*buf))]);
    }
    // the computed CRC32 should be zero
    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s: crc %d", __FUNCTION__, crc);
    if (0 == crc)
    {
        return true;
    }

    return false;
}

void MusicAppData::getSectionHeader(const uint8_t *buf, tTableHeader *p_header)
{
    if (NULL != buf && NULL != p_header)
    {
        p_header->TableID = buf[TS_PSI_TABLE_ID_OFFSET];
        p_header->SectionSyntaxIndicator = (buf[TS_PSI_SECTION_LENGTH_OFFSET] >> 7) & 1;
        p_header->dummy = (buf[TS_PSI_SECTION_LENGTH_OFFSET] >> 6) & 1;
        p_header->SectionLength = TS_PSI_GET_SECTION_LENGTH(buf);
        p_header->TransportStreamID = (uint16_t)(TS_READ_16(&(buf)[TS_PSI_TABLE_ID_EXT_OFFSET]) & 0xFFFF);
        p_header->VersionNumber = (uint8_t)((buf[TS_PSI_CNI_OFFSET] >> 1) & 0x1F);
        p_header->CurrentNextIndicator = buf[TS_PSI_CNI_OFFSET] & 1;
        p_header->SectionNumber = buf[TS_PSI_SECTION_NUMBER_OFFSET];
        p_header->LastSectionNumber = buf[TS_PSI_LAST_SECTION_NUMBER_OFFSET];
    }
    else
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s:%d Error: Null PAT data ", __FILE__, __LINE__);
        return;
    }
}

