/////////////////////////////////////////////////////////
//
//
// Header file for the base class of application data
//
// Purpose: Hide the details of application data handling from media player
//
//
/////////////////////////////////////////////////////////

#if !defined(BASEAPPDATA_H)
#define BASEAPPDATA_H

#include <stdint.h>

#if PLATFORM_NAME == IP_CLIENT

#include <pmt_ic.h>
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>

#define TS_PSI_TABLE_ID_OFFSET                          0
#define TS_PSI_SECTION_LENGTH_OFFSET                    1
#define TS_PSI_TABLE_ID_EXT_OFFSET                      3
#define TS_PSI_CNI_OFFSET                               5
#define TS_PSI_SECTION_NUMBER_OFFSET                    6
#define TS_PSI_LAST_SECTION_NUMBER_OFFSET               7

#define TS_READ_16( buf ) ((uint16_t)(ntohs(*(uint16_t*)buf)))
#define TS_PSI_GET_SECTION_LENGTH( buf )  (uint16_t)(TS_READ_16( &(buf)[TS_PSI_SECTION_LENGTH_OFFSET] ) & 0x0FFF)

#endif

class BaseAppData
{

public:
    BaseAppData(uint32_t maxSize)
    {
        maxAllowedSize = maxSize;
    }
    virtual ~BaseAppData() {}

    virtual int addData(uint8_t *buffer, uint32_t bufferSize) = 0;
    virtual int getData(uint8_t *buffer, uint32_t bufferSize) = 0;

    virtual uint32_t getTotalSize() = 0;

protected:
    uint32_t maxAllowedSize;
};

#endif
