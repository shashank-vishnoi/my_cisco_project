/** @file vodUtils.h
 *
 * @brief vodUtils header file.
 *
 * @author Amit Patel
 *
 * @version 1.0
 *
 * @date 12.27.2010
 *
 * Copyright: Scientific Atlanta, Inc., A Cisco Company
 */

#ifndef _VODUTILS_H
#define _VODUTILS_H

#include <cstring>

// enum for status
typedef enum
{
    E_FALSE,
    E_TRUE
} status;


// typedef for data types
#define  ui32 unsigned int
#define  i32  int
#define  ui16 unsigned short
#define  i16  short
#define  ui8  unsigned char
#define  i8   char



// Utility class provide method to get/set buffer values.
class Utils
{
public:
    static ui8* InsertBuffer(ui8 *insertPosition, const ui8 *buffer,
                             ui32 lengthInBytes);
    static ui8* GetBuffer(ui8 *getPosition, ui8 *buffer, ui32 lengthInBytes);
    static ui8* Get1Byte(const ui8 * pos, ui8 * n);
    static ui8* Get2Byte(const ui8 * pos, ui16 * n);
    static ui8* Get4Byte(const ui8 * pos, ui32 * n);
    static ui8* Put1Byte(ui8 * pos, ui8 n);
    static ui8* Put2Byte(ui8 * pos, ui16 n);
    static ui8* Put4Byte(ui8 * pos, ui32 n);

};

#endif
