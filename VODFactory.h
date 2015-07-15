/** @file VODFactory.h
 *
 * @brief Factory method Header.
 *
 * @author Rosaiah Naidu Mulluri
 *
 * @version 1.0
 *
 * @date 05.10.2012
 *
 * Copyright: Scientific Atlanta, Inc., A Cisco Company
 */
#ifndef MSP_VOD_FACTORY_H
#define MSP_VOD_FACTORY_H

#include "VOD_SessionControl.h"
#include "VOD_StreamControl.h"


class VODFactory
{
public:
    static VOD_SessionControl* getVODSessionControlInstance(OnDemand* ptrOnDemand, const char *aSrcUrl);
    static VOD_StreamControl*  getVODStreamControlInstance(OnDemand* ptrOnDemand, const char *aSrcUrl);

};

#endif // #ifndef VOD_SOURCE_FACTORY_H
