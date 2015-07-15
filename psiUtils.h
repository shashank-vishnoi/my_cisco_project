/** @file psiUtils.h
 *
 * author Gnanesha Thimmegowda
 *
 * date 31Oct2012
 *
 * version 1.0
 *
 * @brief The psiUtils.h header file.
 *
 * psiUtils.h is a class definition for a publicly exposed psiUtils API's.
 \n Copyright 2012 Cisco Systems, Inc.
 */

#if !defined(PSIUTILS_H)
#define PSIUTILS_H



// standard linux includes
#include <stdint.h>

// MSP includes
#include "MspCommon.h"
#include "pmt.h"
#include "psiUtilsInterface.h"
// cpe includes
#include <cpe_sectionfilter.h>




#define TS_PSI_TABLE_ID_OFFSET                          0
#define TS_PSI_SECTION_LENGTH_OFFSET                    1
#define TS_PSI_TABLE_ID_EXT_OFFSET                      3
#define TS_PSI_CNI_OFFSET                               5
#define TS_PSI_SECTION_NUMBER_OFFSET                    6
#define TS_PSI_LAST_SECTION_NUMBER_OFFSET               7

#define TS_READ_16( buf ) ((uint16_t)(ntohs(*(uint16_t*)buf)))
#define TS_PSI_GET_SECTION_LENGTH( buf )  (uint16_t)(TS_READ_16( &(buf)[TS_PSI_SECTION_LENGTH_OFFSET] ) & 0x0FFF)





class psiUtils: public psiUtilsInterface
{
private:
    static psiUtils *m_pInstance;
    // Constructor
    psiUtils() {};
public:
    ePSFDState parseSectionFilterGroupData(uint32_t version, tCpeSFltFilterGroup  *SfGroup);
    tPat* getPatData(uint8_t* pSecFilterBuf);
    static psiUtils * getpsiUtilsInstance();
    ~psiUtils() {};
};


#endif //End of #if !defined(PSIUTILS_H)
