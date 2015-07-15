/**
   \file shim-AppDataUtils-cperpv1.cpp
   Implementation file for shim layer for App Data specific implementations for G6

*/

#include "ApplicationData.h"
#include "DisplaySession.h"
#include "MSPBase64.h"


#ifdef LOG
#error  LOG already defined
#endif

static int pd_cnt = 0;
int ApplicationData::getData(uint8_t *buffer, uint32_t bufferSize)
{
    FNLOG(DL_MSP_MPLAYER);

    pd_cnt++;
    uint32_t header_len = sizeof(tTableHeader);
    uint8_t *buffer_backup = buffer;
    std::string rv;

    pthread_mutex_lock(&accessMutex);
    uint32_t section_length   = 0;
    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s:%d  GETADDDATA pd_cnt=%d header_len=%d bufferSize=%d, dataQueue.size=%d", __FUNCTION__, __LINE__, pd_cnt, header_len, bufferSize, dataQueue.size());
    if ((dataQueue.size() >= 19) && (bufferSize >= 19))
    {
        for (uint32_t i = 0; i < 19; i++)
        {

            *buffer++ = dataQueue.front();
            dataQueue.pop_front();
        }

        tTableHeader *header  = (tTableHeader *)buffer_backup;
        section_length   = header->SectionLength + 3 - 19 - 4 ;



        for (uint32_t i = 0; i < section_length; i++)
        {
            *buffer++ = dataQueue.front();
            dataQueue.pop_front();
        }

        for (uint32_t i = 0; i < 4; i++)
        {
            *buffer++ = dataQueue.front();
            dataQueue.pop_front();
        }

        section_length   += (19 + 4) ;
    }
    else
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s:%d  GETADDDATA data underrun or buffer too small - header_len=%d bufferSize=%d, dataQueue.size=%d", __FUNCTION__, __LINE__, header_len, bufferSize, dataQueue.size());


    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s:%d  GETADDDATA now encoding64 sec_len=%d, dataQueue.size=%d", __FUNCTION__, __LINE__, section_length, dataQueue.size());
    //Do the Base 64 encoding of length - section_length, buffer_backup
    rv = mspEncode(buffer_backup, section_length);
    if (rv.size() < bufferSize)
        strcpy((char *) buffer_backup, rv.c_str());
    else
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s:%d  Dest buffer is too small (%d vs %d)", __FUNCTION__, __LINE__, rv.size(), bufferSize);

    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s:%d  rv len=%d rv=%-20.20s", __FUNCTION__, __LINE__, rv.size(), rv.c_str());

    pthread_mutex_unlock(&accessMutex);

    dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s:%d GETADDDATA returning %d bytes currentsize left: %d\n", __FUNCTION__, __LINE__, rv.size(), dataQueue.size());
    return (rv.size());
}



