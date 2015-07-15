/** @file vodUtils.cpp
 *
 * @brief vodUtils source file.
 *
 * @author Amit Patel
 *
 * @version 1.0
 *
 * @date 12.27.2010
 *
 * Copyright: Scientific Atlanta, Inc., A Cisco Company
 */

#include "vodUtils.h"

//########################################3

ui8* Utils::InsertBuffer(ui8 *insertPosition, const ui8 *buffer,
                         ui32 lengthInBytes)
{
    if ((insertPosition) && (buffer))
    {
        memcpy(insertPosition, buffer, lengthInBytes);
        insertPosition += lengthInBytes;
    }
    return insertPosition;

}

ui8* Utils::GetBuffer(ui8 *getPosition, ui8 *buffer, ui32 lengthInBytes)
{
    if ((getPosition) && (buffer))
    {
        memcpy(buffer, getPosition, lengthInBytes);
        getPosition += lengthInBytes;
    }
    return getPosition;
}

ui8 * Utils::Put1Byte(ui8 * pos, ui8 n)
{
    *(pos + 0) = ((n >> 0) & 0xFF);
    return pos + 1;
}

ui8 * Utils::Put2Byte(ui8 * pos, ui16 n)
{
    *(pos + 0) = ((n >> 8) & 0xFF);
    *(pos + 1) = ((n >> 0) & 0xFF);
    return pos + 2;
}

ui8 * Utils::Put4Byte(ui8 * pos, ui32 n)
{
    *(pos + 0) = ((n >> 24) & 0xFF);
    *(pos + 1) = ((n >> 16) & 0xFF);
    *(pos + 2) = ((n >> 8) & 0xFF);
    *(pos + 3) = ((n >> 0) & 0xFF);
    return pos + 4;
}

ui8 * Utils::Get1Byte(const ui8 * pos, ui8 * n)
{
    *n = *pos;
    return (ui8*) pos + 1;
}

ui8 * Utils::Get2Byte(const ui8 * pos, ui16 * n)
{
    *n = ((*(pos + 0) << 8) | (*(pos + 1) << 0));
    return (ui8*) pos + 2;
}

ui8 * Utils::Get4Byte(const ui8 * pos, ui32 * n)
{
    *n = ((*(pos + 0) << 24) | (*(pos + 1) << 16) | (*(pos + 2) << 8) | (*(pos
            + 3) << 0));
    return (ui8*) pos + 4;
}


