/** @file MspCommon.cpp
 *
 * @brief MspCommon source file.
 *
 * @author Amit Patel
 *
 * @version 1.0
 *
 * @date 04.13.2013
 *
 * Copyright: Scientific Atlanta, Inc., A Cisco Company
 */

#include "MspCommon.h"
#if PLATFORM_NAME == G6 || PLATFORM_NAME == G8


//########################################3


bool MspCommon::getCpeModeFormat(uint8_t mode, tCpeSrcRFMode& cpeMode)
{
    bool status = true;

    switch (mode)
    {
    case Mode_QAM16:
        cpeMode = eCpeSrcRFMode_QAM16;
        break;
    case Mode_QAM32:
        cpeMode = eCpeSrcRFMode_QAM32;
        break;
    case Mode_QAM64:
        cpeMode = eCpeSrcRFMode_QAM64;
        break;
    case Mode_QAM128:
    case Mode_QAM128_sea:
        cpeMode = eCpeSrcRFMode_QAM128;
        break;
    case Mode_QAM256:
        cpeMode = eCpeSrcRFMode_QAM256;
        break;
    default:
        status = false;
        break;
    }
    return status;
}

#endif
