#include <stdlib.h>
#include "dvr_metadata_reader.h"
#include "dlog.h"
#include <cstring>
#include <cstdio>
#define LOG(level, msg, args...)  dlog(DL_MSP_DVR, level,"dvr:%s:%d " msg, __FUNCTION__, __LINE__, ##args);




eMspStatus ReadMrdvrMetaData(char * filename, tCpeRecDataBase **metabuf)
{
    FNLOG(DL_MSP_MRDVR);
    int metasize;
    FILE *fp;
    size_t result = 0;
    uint8_t *dbBuf;
    tCpeRecDataBase *tempDbBuf;

    *metabuf = NULL;

    //TODO: Remove the filename Hack,  get it from Source URl
    fp = fopen(filename, "rb");
    if (fp == NULL)
    {
        LOG(DLOGL_ERROR, " Metadata file opening error for DVR file %s", filename);
        return kMspStatus_Error;
    }


    // obtain file size:
    fseek(fp, 0, SEEK_END);

    metasize = ftell(fp);
    LOG(DLOGL_NOISE, " Metadata fsize:%d sizeof struct:%d\n", metasize, sizeof(tCpeRecDataBase));

    if (metasize <= 0)
    {
        LOG(DLOGL_ERROR, " Metadata file has no data or its corrupted\n");
        fclose(fp);
        return kMspStatus_Error;
    }

    dbBuf = new uint8_t [metasize + 1024]; // TODO: check this
    if (dbBuf == NULL)
    {
        LOG(DLOGL_ERROR, " %s:%d Getting PmtInfo from Stored file failed \n", __FUNCTION__, __LINE__);
        fclose(fp);
        return kMspStatus_Error;
    }

    rewind(fp);

    result = fread(dbBuf, 1, metasize, fp);
    if (result != (unsigned int)metasize)
    {
        LOG(DLOGL_ERROR, " Unable to read the metadata from the file correctly\n");
        LOG(DLOGL_ERROR, " Expected metadata -- %d, read metadata -- %d\n", metasize, result);
        delete [] dbBuf;
        dbBuf = NULL;
        fclose(fp);
        return kMspStatus_Error;
    }
    fclose(fp);

    tempDbBuf = (tCpeRecDataBase *) dbBuf;

    if (tempDbBuf->dbHdr.dbCounts > kCpeRec_DataBaseEntries)
    {
        LOG(DLOGL_ERROR, "  Number of database entries in metadata file is %d", tempDbBuf->dbHdr.dbCounts);
        LOG(DLOGL_ERROR, " Number of entries in Metadata database is more than allowed entries count of %d", kCpeRec_DataBaseEntries);
        delete [] dbBuf;
        dbBuf = NULL;
        return kMspStatus_Error;
    }
    else
    {
        LOG(DLOGL_NOISE, " Number of database entries in metadata file is %d", tempDbBuf->dbHdr.dbCounts);
        *metabuf = (tCpeRecDataBase *)dbBuf;

    }

    return kMspStatus_Ok;
}

tCpeRecDataBaseType* GetDecryptCABlob(tCpeRecDataBase *dataBase , eBlobType *dvr_blob)
{
    tCpeRecDataBaseType *dbTypeCABlob = NULL;
    uint8_t *sectionAddress;
    int i;

    if (dvr_blob == NULL)
    {
        LOG(DLOGL_ERROR, "Invalid parameter.NULL pointer passed in for dvr_blob type variable");
        return NULL;
    }

    dbTypeCABlob = new tCpeRecDataBaseType;
    if (dbTypeCABlob == NULL)
    {
        LOG(DLOGL_ERROR, "Out of memory for CA blob storage");
        return dbTypeCABlob;
    }
    LOG(DLOGL_NOISE, "DB section count is %d", dataBase->dbHdr.dbCounts);
    for (i = 0; i < dataBase->dbHdr.dbCounts; i++)
    {
        sectionAddress = (uint8_t *)(dataBase) + dataBase->dbEntry[i].offset;
        // tag 0x1FAC is for RTN and 0x101 is for sara
        if ((dataBase->dbEntry[i].tag == 0x1FAC) || (dataBase->dbEntry[i].tag == 0x101))
        {
            //processCaMetaDataBlob(sectionAddress,metabuf->dbEntry[i].size);
            dbTypeCABlob->size = dataBase->dbEntry[i].size;
            dbTypeCABlob->dataBuf = (uint8_t *)malloc(dataBase->dbEntry[i].size);
            if (dbTypeCABlob->dataBuf == NULL)
            {
                LOG(DLOGL_ERROR, "Could not allocate memory for CA blob metadata storage");
                dbTypeCABlob->size = 0;
                delete dbTypeCABlob;
                dbTypeCABlob = NULL;
                return dbTypeCABlob;
            }
            memcpy(dbTypeCABlob->dataBuf, sectionAddress, dbTypeCABlob->size);
            break;
        }
        else
        {
            LOG(DLOGL_REALLY_NOISY, "tag 0x%x is not a CAM blob", dataBase->dbEntry[i].tag);
        }
    }

    if (dataBase->dbEntry[i].tag == 0x1FAC)
    {
        *dvr_blob = kRTN_Blob;
    }
    else if (dataBase->dbEntry[i].tag == 0x101)
    {
        *dvr_blob = kSARA_Blob;
    }
    else
    {
        *dvr_blob = kInvalid_Blob;
    }

    return dbTypeCABlob;
}


tCpeRecDataBaseType* GetDecryptCADesc(tCpeRecDataBase *dataBase)
{
    tCpeRecDataBaseType *dbTypeCADesc = NULL;
    uint8_t *sectionAddress;
    dbTypeCADesc = new tCpeRecDataBaseType;
    if (dbTypeCADesc == NULL)
    {
        return dbTypeCADesc;
    }

    LOG(DLOGL_NOISE, "DB section count is %d", dataBase->dbHdr.dbCounts);
    for (int i = 0; i < dataBase->dbHdr.dbCounts; i++)
    {
        sectionAddress = (uint8_t *)(dataBase) + dataBase->dbEntry[i].offset;
        if (dataBase->dbEntry[i].tag == 0x1FAD)
        {
            //processCaMetaDataDescriptor(sectionAddress,metabuf->dbEntry[i].size);
            dbTypeCADesc->size = dataBase->dbEntry[i].size;
            dbTypeCADesc->dataBuf = (uint8_t *)malloc(dataBase->dbEntry[i].size);
            if (dbTypeCADesc->dataBuf == NULL)
            {
                LOG(DLOGL_ERROR, " %s:%d Could not allocate memory for CA descriptor metadata storage\n", __FUNCTION__, __LINE__);
                dbTypeCADesc->size = 0;
                delete dbTypeCADesc;
                dbTypeCADesc = NULL;
                return dbTypeCADesc;
            }
            memcpy(dbTypeCADesc->dataBuf, sectionAddress, dbTypeCADesc->size);
        }
        else
        {
            LOG(DLOGL_REALLY_NOISY, "tag 0x%x is not a CA descriptor blob", dataBase->dbEntry[i].tag);
        }
    }
    return dbTypeCADesc;
}

uint8_t GetScramblingMode(tCpeRecDataBase *dataBase)
{
    tCpeRecDataBaseType *dbTypeCADesc = NULL;
    uint8_t *sectionAddress;
    dbTypeCADesc = new tCpeRecDataBaseType;
    uint8_t  scramblingMode = 0;

    if (dbTypeCADesc == NULL)
    {
        return scramblingMode;
    }

    LOG(DLOGL_NOISE, " %s:%d DB section count is %d", __FUNCTION__, __LINE__, dataBase->dbHdr.dbCounts);
    for (int i = 0; i < dataBase->dbHdr.dbCounts; i++)
    {
        sectionAddress = (uint8_t *)(dataBase) + dataBase->dbEntry[i].offset;
        // tag 0x1FAB is for RTN and 0x102 is for sara
        if ((dataBase->dbEntry[i].tag == 0x1FAB) || (dataBase->dbEntry[i].tag == 0x102))
        {
            tCpePgrmHandleMpegDesc cakDescriptor;
            tCpePgrmHandleMpegDesc cakSystemDescriptor;
            eMspStatus status = kMspStatus_Ok;
            Pmt *ptrPmt = new Pmt();
            if (ptrPmt)
            {
                if (dataBase->dbEntry[i].tag == 0x1FAB)
                {
                    ptrPmt->populateMSPMetaData(sectionAddress, dataBase->dbEntry[i].size);
                }
                else
                {
                    ptrPmt->populateFromSaraMetaData(sectionAddress, dataBase->dbEntry[i].size);
                }
                std::list<tPid>* videoList = ptrPmt->getVideoPidList();
                if (videoList->size())
                {
                    std::list<tPid>::iterator iter = videoList->begin();
                    cakDescriptor.tag = 0x9;
                    cakDescriptor.dataLen = 0;
                    cakDescriptor.data = NULL;
                    status = ptrPmt->getDescriptor(&cakDescriptor, (*iter).pid);
                    if (status == kMspStatus_Ok)
                    {
                        // now get the CA system descriptor
                        cakSystemDescriptor.tag = 0x65;
                        cakSystemDescriptor.dataLen = 0;
                        cakSystemDescriptor.data = NULL;

                        status = ptrPmt->getDescriptor(&cakSystemDescriptor, (*iter).pid);
                        LOG(DLOGL_NOISE, "ptrPmt->getDescriptor pid:%d status: %d\n", (*iter).pid, status);
                        if (status == kMspStatus_Ok)
                        {
                            scramblingMode = cakSystemDescriptor.data[0];
                            ptrPmt->releaseDescriptor(&cakSystemDescriptor);
                        }
                    }
                    ptrPmt->releaseDescriptor(&cakDescriptor);
                }
            }
            else
            {
                LOG(DLOGL_ERROR, "Unable to allocate memory for Pmt structure");
                delete dbTypeCADesc;
                dbTypeCADesc = NULL;
                return scramblingMode;

            }

            delete ptrPmt;
        }
        else
        {
            LOG(DLOGL_NOISE, " %s:%d Meta data not handled here tag 0x%x \n", __FUNCTION__, __LINE__, dataBase->dbEntry[i].tag);
        }

    }

    //freeing up the dynamically allocated memories.
    delete dbTypeCADesc;
    dbTypeCADesc = NULL;

    return scramblingMode;
}

