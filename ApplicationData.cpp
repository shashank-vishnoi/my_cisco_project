///////////////////////////////////////////////////////////
//
//
// Implementation file for application data container class
//
// Purpose: Hide the details of application data handling from media player
//
//
/////////////////////////////////////////////////////////
//
//


#include <memory.h>
#include "ApplicationData.h"
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8
#include "DisplaySession.h"
#endif
#include "MSPBase64.h"
#include "csci-base64util-api.h"



static int packets_added = 0;
const int MAX_FILTERING_PACKETS = 20;
ApplicationData::ApplicationData(uint32_t maxSize)
    : BaseAppData(maxSize)
{
    FNLOG(DL_MSP_MPLAYER);

    pthread_mutex_init(&accessMutex, NULL);
    maxAllowedSize = maxSize;
    pthread_mutex_lock(&accessMutex);
    dataQueue.clear();
    dlog(DL_MSP_MPLAYER, DLOGL_NORMAL, "%s:%d Resetting packets added to 0 \n", __FUNCTION__, __LINE__);
    packets_added = 0;
    pthread_mutex_unlock(&accessMutex);
    dlog(DL_MSP_MPLAYER, DLOGL_NORMAL, "%s:%d Maximum data size  = %d \n", __FUNCTION__, __LINE__, maxSize);
}

ApplicationData::~ApplicationData()
{

    FNLOG(DL_MSP_MPLAYER);
    pthread_mutex_lock(&accessMutex);
    dataQueue.clear();
    dlog(DL_MSP_MPLAYER, DLOGL_NORMAL, "%s:%d Resetting packets added to 0 \n", __FUNCTION__, __LINE__);
    packets_added = 0;
    pthread_mutex_unlock(&accessMutex);

}

int ApplicationData::addData(uint8_t *buffer, uint32_t bufferSize)
{

    FNLOG(DL_MSP_MPLAYER);

    if (packets_added > MAX_FILTERING_PACKETS)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_NORMAL, "%s:%d GETADDDATA max packets added,  drop data, packets_added=%d\n", __FUNCTION__, __LINE__, packets_added);
        return 0;
    }

#if PLATFORM_NAME == IP_CLIENT
    tTableHeader *header = NULL, _header;
    uint8_t *buffer_backup = buffer;
    memset(&_header, 0, sizeof(_header));
    getSectionHeader(buffer_backup, &_header);
    header = &_header;
    bufferSize = header->SectionLength + 3;
    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s:%d GETADDDATA printing (SectionSize <%d> = SectionLength <%d> + 3 ) \n", __FUNCTION__, __LINE__, bufferSize, header->SectionLength);
#endif

    if ((bufferSize + getTotalSize()) > maxAllowedSize)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s:%d GETADDDATA max size exceeded.  drop data\n", __FUNCTION__, __LINE__);
        return 0;
    }
    pthread_mutex_lock(&accessMutex);

    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s:%d GETADDDATA printing data added to be queue (buffersize=%d) \n", __FUNCTION__, __LINE__, bufferSize);

    //printHex(buffer, bufferSize);

    for (uint32_t i = 0; i < bufferSize; i++)
    {
        dataQueue.push_back(*buffer++);
    }
    pthread_mutex_unlock(&accessMutex);

    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s:%d GETADDDATA bufferSize=%d getTotalSize=%d packets_added=%d \n", __FUNCTION__, __LINE__, bufferSize, getTotalSize(), packets_added);
    packets_added++;
    return bufferSize;
}
void ApplicationData::logTableHeaderInfo(tTableHeader *tblHdr)
{
    if (tblHdr)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "TableID                : %d", tblHdr->TableID);
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "SectionSyntaxIndicator : %d", tblHdr->SectionSyntaxIndicator);
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Dummy			        : %d", tblHdr->dummy);
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Reserved1     		 	: %d", tblHdr->Reserved1);
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "SectionLength          : %d", tblHdr->SectionLength);
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "TS ID / Program No     : %d", tblHdr->TransportStreamID);
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "VersionNumber          : %d", tblHdr->VersionNumber);
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "CurrentNextIndicator   : %d", tblHdr->CurrentNextIndicator);
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "SectionNumber          : %d", tblHdr->SectionNumber);
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "LastSectionNumber      : %d", tblHdr->LastSectionNumber);
    }
    else
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Error: Null psi data ");
        return ;
    }
}

uint32_t ApplicationData::getTotalSize()
{
    uint32_t total = 0;

    pthread_mutex_lock(&accessMutex);
    total = dataQueue.size();
    pthread_mutex_unlock(&accessMutex);

    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s:%d   total size is     %d  \n", __FUNCTION__, __LINE__, total);
    return total;
}


#if 1
void ApplicationData::printHex(uint8_t* buf, uint16_t len)
{
    if (len <= 0)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "Nothing to print ,the date length is 0 \n");
        return;
    }


    int l;
    int sofar;
    char s[2000];
    char s2[2000];
    s[0] = 0;
    s2[0] = 0;

    sofar = 0;
    int b = 0;
#if 0
    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "Meta data################### HEX PACKET DATA HERE BEGIN (size=%d) #####################\n", len);
    while (sofar < len)
    {
        if (len - sofar < 100)
        {
            sprintf(s2, " %08d", sofar);
            for (l = 0; l < (len - sofar); l++)
            {
                sprintf(s, " %02x", buf[b++]);
                strcat(s2, s);
            }
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s", s2);
            sofar += 100;
        }
        else
        {
            sprintf(s2, " %08d", sofar);
            for (l = 0; l < 100; l++)
            {
                sprintf(s, " %02x", buf[b++]);
                strcat(s2, s);
            }
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s", s2);
            sofar += 100;
        }
    }

    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "Meta data################### HEX PACKET DATA HERE END #####################\n");
#endif

    s[0] = 0;
    s2[0] = 0;

    sofar = 0;
    b = 0;
    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "Meta data################### ASCII PACKET DATA HERE BEGIN (size=%d) #####################\n", len);
    while (sofar < len)
    {
        if (len - sofar < 100)
        {
            sprintf(s2, " %08d ==>", sofar);
            for (l = 0; l < (len - sofar); l++)
            {
                if (isprint(buf[b]))
                    sprintf(s, "%c", buf[b++]);
                else
                {
                    b++;
                    sprintf(s, "_");
                }
                strcat(s2, s);
            }
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s", s2);
            sofar += 100;
        }
        else
        {
            sprintf(s2, " %08d ==>", sofar);
            for (l = 0; l < 100; l++)
            {
                if (isprint(buf[b]))
                    sprintf(s, "%c", buf[b++]);
                else
                {
                    b++;
                    sprintf(s, "_");
                }
                strcat(s2, s);
            }
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s", s2);
            sofar += 100;
        }
    }

    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "Meta data################### ASCII PACKET DATA HERE END #####################\n");
}
#endif
