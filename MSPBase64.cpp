/**

@{
  \file MSPBase64.cpp

  \brief Implementation for Encoding/Decoding base-64 data between HTTP Client and 3rd Party Server.

  \n Original author: Nilesh Dixit
  \n Version 0.1 2011-01-25 (Nilesh Dixit)

  \n Copyright 2009-2010 Cisco Systems, Inc.
  \defgroup Signaling
@}*/

#include <stdio.h>
#include "dlog.h"
#include "MSPBase64.h"


static inline bool isMspBase(unsigned char c)
{
    return (isalnum(c) || (c == '*') || (c == '@'));
}

std::string mspEncode(unsigned char const* encodeStr, unsigned int strLength)
{
    std::string retVal;
    int i = 0;
    int j = 0;
    unsigned char firstCharArray[3];
    unsigned char secondCharArray[4];
    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "gslEncode");
    while (strLength--)
    {
        firstCharArray[i++] = *(encodeStr++);
        if (i == 3)
        {
            secondCharArray[0] = (firstCharArray[0] & 0xfc) >> 2;
            secondCharArray[1] = ((firstCharArray[0] & 0x03) << 4) + ((firstCharArray[1] & 0xf0) >> 4);
            secondCharArray[2] = ((firstCharArray[1] & 0x0f) << 2) + ((firstCharArray[2] & 0xc0) >> 6);
            secondCharArray[3] = firstCharArray[2] & 0x3f;

            for (i = 0; (i < 4) ; i++)
            {
                retVal += mspBase[secondCharArray[i]];
            }
            i = 0;
        }
    }

    if (i)
    {
        for (j = i; j < 3; j++)
        {
            firstCharArray[j] = '\0';
        }
        secondCharArray[0] = (firstCharArray[0] & 0xfc) >> 2;
        secondCharArray[1] = ((firstCharArray[0] & 0x03) << 4) + ((firstCharArray[1] & 0xf0) >> 4);
        secondCharArray[2] = ((firstCharArray[1] & 0x0f) << 2) + ((firstCharArray[2] & 0xc0) >> 6);
        secondCharArray[3] = firstCharArray[2] & 0x3f;

        for (j = 0; (j < i + 1); j++)
        {
            retVal += mspBase[secondCharArray[j]];
        }
        while ((i++ < 3))
        {
            retVal += '=';
        }
    }

    return retVal;

}

std::string mspDecode(std::string const& decodeString)
{
    int strLength = decodeString.size();
    int i = 0;
    int j = 0;
    int k = 0;
    unsigned char secondCharArray[4];
    unsigned char firstCharArray[3];
    std::string retVal;
    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "mspDecode");
    while (strLength-- && (decodeString[k] != '=') && isMspBase(decodeString[k]))
    {
        secondCharArray[i++] = decodeString[k];
        k++;
        if (i == 4)
        {
            for (i = 0; i < 4; i++)
            {
                secondCharArray[i] = mspBase.find(secondCharArray[i]);
            }
            firstCharArray[0] = (secondCharArray[0] << 2) + ((secondCharArray[1] & 0x30) >> 4);
            firstCharArray[1] = ((secondCharArray[1] & 0xf) << 4) + ((secondCharArray[2] & 0x3c) >> 2);
            firstCharArray[2] = ((secondCharArray[2] & 0x3) << 6) + secondCharArray[3];

            for (i = 0; (i < 3); i++)
            {
                retVal += firstCharArray[i];
            }
            i = 0;
        }
    }

    if (i)
    {
        for (j = i; j < 4; j++)
        {
            secondCharArray[j] = 0;
        }
        for (j = 0; j < 4; j++)
        {
            secondCharArray[j] = mspBase.find(secondCharArray[j]);
        }
        firstCharArray[0] = (secondCharArray[0] << 2) + ((secondCharArray[1] & 0x30) >> 4);
        firstCharArray[1] = ((secondCharArray[1] & 0xf) << 4) + ((secondCharArray[2] & 0x3c) >> 2);
        firstCharArray[2] = ((secondCharArray[2] & 0x3) << 6) + secondCharArray[3];

        for (j = 0; (j < i - 1); j++)
        {
            retVal += firstCharArray[j];
        }
    }

    return retVal;
}


