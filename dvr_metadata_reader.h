/** @file MRDvrServer.h
 *
 * @author Arun Perumal
 * @date 05-22-2011
 *
 * @version 1.0
 *
 * @brief DVR Metadata reader utility
 *
 */

#ifndef _DVR_METADATA_READER_H_
#define _DVR_METADATA_READER_H_

#include <string>
#include <list>
#include <cpe_error.h>
#include <cpe_source.h>
#include "cpe_cam_dvr.h"
#include <cpe_recmgr.h>
#include <cpe_error.h>
#include <cpe_source.h>

#include "eventQueue.h"
#include "MspCommon.h"
#include "pmt.h"


typedef enum
{
    kInvalid_Blob = -1,
    kSARA_Blob,
    kRTN_Blob
} eBlobType;





/*!  \fn   eMspStatus  ReadMrdvrMetadata(char *filename, tCpeRecDataBase **metabuf)
     \brief This function is a utility to read out the DVR metadata from a recorded file
     @param char *filename:file name of the recorded asset.
     @param tCpeRecDataBase *database: buffer which will be filled up with DVR metadata
     @return eMspStatus
*/
eMspStatus ReadMrdvrMetaData(char * filename, tCpeRecDataBase **metabuf);

/*!  \fn tCpeRecDataBaseType*  GetDecryptCABlob(tCpeRecDataBase *dataBase)
     \brief This function is a utility to retrieve CA Blob out of DVR metadata
     @param tCpeRecDataBase *dataBase: DVR metadata database
     @param eBlobType *dvr_blob: enum variable tells about CA blob type
     @return tCpeRecDataBaseType *: CA metadata database pointer.
*/
tCpeRecDataBaseType* GetDecryptCABlob(tCpeRecDataBase *dataBase , eBlobType *dvr_blob);


/*!  \fn tCpeRecDataBaseType*  GetDecryptCADesc(tCpeRecDataBase *dataBase)
     \brief This function is a utility to retrieve CA descriptor out of DVR metadata
     @param tCpeRecDataBase *dataBase: DVR metadata database
     @return tCpeRecDataBaseType *: CA metadata database pointer.
*/
tCpeRecDataBaseType* GetDecryptCADesc(tCpeRecDataBase *dataBase);


/*!  \fn   uint8_t GetScramblingMode(tCpeRecDataBase *dataBase)
     \brief This function is a utility to get the scrambling mode.
     @param tCpeRecDataBase *dataBase: DVR metadata database
     @return uint8_t:scrambling mode
*/
uint8_t GetScramblingMode(tCpeRecDataBase *dataBase);

#endif
