#include <time.h>
#include <sys/time.h>
#include "MSPHTTPSource_ic.h"
#include "use_threaded.h"
#include "languageSelection.h"
#include "devicesettings/exception.hpp"
#include "IMediaPlayer.h"
#include "csci-dlna-ipclient-gatewayDiscovery-api.h"

#define LOG(level, msg, args...)  dlog(DL_MSP_MRDVR,level,"MSPHTTPSource:%s:%d " msg, __FUNCTION__, __LINE__, ##args);

#define CC_TRICK_ON  1
#define CC_TRICK_OFF 0

using namespace device;

void* MSPHTTPSource::mpSessionId = NULL;
pthread_mutex_t MSPHTTPSource::mrdvr_player_mutex = PTHREAD_MUTEX_INITIALIZER;

///Constructor
MSPHTTPSource::MSPHTTPSource(std::string aSrcUrl)
{
    FNLOG(DL_MSP_MRDVR);
    mCci = DEFAULT_RESTRICTIVE_CCI;
    mSrcUrl = aSrcUrl;
    LOG(DLOGL_REALLY_NOISY, "%s: MSPHTTPSource source URL %s %s\n", __FUNCTION__, mSrcUrl.c_str(), aSrcUrl.c_str());
    mProgramNumber = 0;
    mSourceId = 0;
    mFrequencyHz = 0;
    mSrcStateCB = NULL;
    mClientContext = NULL;
    mHTTPSrcState = kHTTPSrcNotOpened;
    mPendingPause       = false;
    mPendingPlay        = false;
    mPendingSetSpeed    = false;
    mPendingSetPosition = false;
    mCurrentSpeedNum     = 100;
    mCurrentSpeedDen     = 100;
    mParseStatus = kMspStatus_Ok;
    mpSessionId = NULL;
    mClmChannel     = 0;
    mChannelType    = 0;
    mpPMTData = NULL;
    mPMTDataSize = 0;
    mAudioPid = INVALID_PID_VALUE;
    mVideoPid = INVALID_PID_VALUE;
    mSdvSource = false;
    mMusicSource = false;
    mMosaicSource = false;
    mbSfStarted	 = false;
    mpFilterId	= NULL;
    mDataReadyCB = NULL;
    mDataReadyContext = NULL;
    mAppData = NULL;
    pthread_mutex_init(&mAppDataMutex, NULL);
    buildClmChannel();
    buildFileName();
    //For VOD asset content url is not available now
    if (mFileName.find(VOD_SOURCE_URI_PREFIX) == std::string::npos)
    {
        buildCmUUIDfromCDSUrl(mFileName.c_str());
    }
    SetCcChangedCB(NULL, NULL);
    mCgmiFirstPTSReceived = false;
    pthread_mutex_init(&mCgmiDataMutex, NULL);
    //default zoom...
    videoZoomType = tAvpmPictureMode_Normal;

    //these magic numbers are hardcoded to match the zoom results against the benchmark G8.
    mZoom25Params[0] = 240;
    mZoom25Params[1] = 180;
    mZoom25Params[2] = 930;
    mZoom25Params[3] = 475;
    mZoom50Params[0] = 230;
    mZoom50Params[1] = 150;
    mZoom50Params[2] = 750;
    mZoom50Params[3] = 410;
    mHTTPSrcState = kHTTPSrcNotOpened;
}

///Destructor
MSPHTTPSource::~MSPHTTPSource()
{
    FNLOG(DL_MSP_MRDVR);

    SetCcChangedCB(NULL, NULL);

    if (mpPMTData)
    {
        free(mpPMTData);
        mpPMTData = NULL;
    }
    mPMTDataSize = 0;

    pthread_mutex_destroy(&mCgmiDataMutex);
    pthread_mutex_destroy(&mAppDataMutex);
    SetVideoZoomCB(NULL, NULL);
    mpSessionId = NULL;
}


int MSPHTTPSource::mStreamResolutionWidth = 0;
int MSPHTTPSource::mStreamResolutionHeight = 0;
tAvRect MSPHTTPSource::mVidScreenRect;

///Loads a CGMI playback session
eMspStatus MSPHTTPSource::load(SourceStateCallback aPlaybackCB, void* aClientContext)
{
    FNLOG(DL_MSP_MRDVR);

    eMspStatus retMspStatus = kMspStatus_Ok;

    if (mSrcStateCB == NULL)
        mSrcStateCB = aPlaybackCB;

    if (mClientContext == NULL)
        mClientContext = aClientContext;

    LOG(DLOGL_REALLY_NOISY, "%s:Existing FileName is: %s . HTTP URL Case", __FUNCTION__, mFileName.c_str());
    std::size_t found = mFileName.find("http");
    std::size_t isVodUrl = mFileName.find("lscp");

    LOG(DLOGL_REALLY_NOISY, "%s:http-found:%d lscp-found:%d \n", __FUNCTION__, found, isVodUrl);
    if ((found != std::string::npos) || (isVodUrl != std::string::npos))
    {
        pthread_mutex_lock(&mrdvr_player_mutex);
        cgmi_Status stat = CGMI_ERROR_SUCCESS;
        stat = cgmi_CreateSession(CgmiPlayBkCallbackFunction, (void *) this, &mpSessionId);
        if (stat != CGMI_ERROR_SUCCESS)
        {
            LOG(DLOGL_ERROR, "%s: Tuning to source %s failed\n", __FUNCTION__, mSrcUrl.c_str());
            LOG(DLOGL_ERROR, "%s:%d - %s :: cgmi_CreateSession Failed with %s \n", __FILE__, __LINE__, __FUNCTION__, (char *) cgmi_ErrorString(stat).c_str());
            retMspStatus = kMspStatus_BadSource;
        }
        else
        {
            LOG(DLOGL_EMERGENCY, "%s: Tuning to source %s using CGMI SESSION ID %p\n", __FUNCTION__, mSrcUrl.c_str(), mpSessionId);
            SetVideoZoomCB(this, videoZoomChangedCB);
        }
        pthread_mutex_unlock(&mrdvr_player_mutex);
    }
    else
    {
        LOG(DLOGL_ERROR, "%s: Invalid source URL for MSPHTTPSource open %s\n", __FUNCTION__, mFileName.c_str());
        retMspStatus = kMspStatus_BadSource;
    }

    LOG(DLOGL_REALLY_NOISY, "%s: MSPHTTPSource load return success\n", __FUNCTION__);

    return retMspStatus;
}

void MSPHTTPSource::setFileName(std::string filename)
{
    FNLOG(DL_MSP_MRDVR);

    if (!filename.empty())
    {
        mFileName = filename;
        //CmUUID forom vod conent url
        buildCmUUIDfromCDSUrl(mFileName.c_str());
        LOG(DLOGL_REALLY_NOISY, "[%s][%s][%d]: Setting mFileName to: %s", __FILE__, __FUNCTION__, __LINE__, mFileName.c_str());
    }
    else
    {
        LOG(DLOGL_ERROR, "[%s][%s][%d]: Received invalid FileName", __FILE__, __FUNCTION__, __LINE__);
    }
}

///Loads a previously created CGMI playback session with a HTTP source URL
eMspStatus MSPHTTPSource::open(eResMonPriority pri)
{
    FNLOG(DL_MSP_MRDVR);

    eMspStatus retMspStatus = kMspStatus_Ok;

    (void) pri;  // not used for HTTP	 source

    pthread_mutex_lock(&mrdvr_player_mutex);

    if (mpSessionId)
    {
        cgmi_Status stat = CGMI_ERROR_SUCCESS;
        stat = cgmi_Load(mpSessionId, (char*) mFileName.c_str());
        if (stat != CGMI_ERROR_SUCCESS)
        {
            LOG(DLOGL_ERROR, "%s:%d - %s :: cgmi_Load Failed with %s \n", __FILE__, __LINE__, __FUNCTION__, (char *) cgmi_ErrorString(stat).c_str());
            retMspStatus = kMspStatus_BadSource;
        }
        else
        {
            LOG(DLOGL_REALLY_NOISY, "%s: Setting RDK attribute CGMI_ATTRIB_DLNA_CONN_STALL", __FUNCTION__);
            bool isSetAttribute = true;
            stat = cgmi_SetAttribute(mpSessionId, CGMI_ATTRIB_DLNA_CONN_STALL, &isSetAttribute);
            if (stat != CGMI_ERROR_SUCCESS)
            {
                LOG(DLOGL_ERROR, "%s:%d - %s :: cgmi_SetAttribute Failed with %s ", __FILE__, __LINE__, __FUNCTION__, (char *) cgmi_ErrorString(stat).c_str());
            }
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "%s: Invalid Session Id\n", __FUNCTION__);
        retMspStatus = kMspStatus_BadSource;
    }

    pthread_mutex_unlock(&mrdvr_player_mutex);

    LOG(DLOGL_REALLY_NOISY, "%s: MSPHTTPSource open return success\n", __FUNCTION__);

    return retMspStatus;
}

///Starts CGMI playback session
eMspStatus MSPHTTPSource::start()
{
    // Start the source
    FNLOG(DL_MSP_MRDVR);

    eMspStatus retMspStatus = kMspStatus_Ok;

    pthread_mutex_lock(&mrdvr_player_mutex);

    if (mpSessionId)
    {
        cgmi_Status stat = CGMI_ERROR_SUCCESS;
        stat = cgmi_Play(mpSessionId, 0);
        if (stat != CGMI_ERROR_SUCCESS)
        {
            LOG(DLOGL_ERROR, "%s:%d - %s :: cgmi_Play Failed with %s \n", __FILE__, __LINE__, __FUNCTION__, (char *) cgmi_ErrorString(stat).c_str());
            retMspStatus = kMspStatus_BadSource;
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "%s: Invalid Session Id\n", __FUNCTION__);
        retMspStatus = kMspStatus_BadSource;
    }

    pthread_mutex_unlock(&mrdvr_player_mutex);

    LOG(DLOGL_REALLY_NOISY, "%s: MSPHTTPSource start return success\n", __FUNCTION__);

    return retMspStatus;
}

///Stops or Unloads and Destroys CGMI playback session
eMspStatus MSPHTTPSource::stop()
{
    // Stop the source
    FNLOG(DL_MSP_MRDVR);

    eMspStatus retMspStatus = kMspStatus_Ok;

    pthread_mutex_lock(&mrdvr_player_mutex);

    if (mpSessionId)
    {
        Avpm *inst = NULL;
        inst = Avpm::getAvpmInstance();
        if (inst)
        {
            bool ccEnabled = false;
            inst->getCc(&ccEnabled);
            if (true == ccEnabled)
            {
                ccChangedCB(this, false);
            }
        }

        stopAppDataFilter();
        closeAppDataFilter();

        // Collect session ID in a temp variable
        // cgmi_Unload requires mrdvr_player_mutex, in the callback CgmiPlayBkCallbackFunction,
        // to complete it's action
        // Hence releasing the MUTEX mrdvr_player_mutex
        void *pSessionId = mpSessionId;
        mpSessionId = NULL;

        pthread_mutex_unlock(&mrdvr_player_mutex);

        cgmi_Status connidStat = CGMI_ERROR_SUCCESS;
        cgmi_Status stat = CGMI_ERROR_SUCCESS;
        uint32_t connid = 0;
        connidStat = cgmi_GetAttribute(pSessionId, CGMI_ATTRIB_DLNA_CONNID, &connid);
        if (connidStat != CGMI_ERROR_SUCCESS)
        {
            LOG(DLOGL_ERROR, "%s:%d - %s :: cgmi_GetAttribute Failed with %s \n", __FILE__, __LINE__, __FUNCTION__, (char *) cgmi_ErrorString(connidStat).c_str());
        }
        stat = cgmi_Unload(pSessionId);
        if (stat != CGMI_ERROR_SUCCESS)
        {
            LOG(DLOGL_ERROR, "%s:%d - %s :: cgmi_Unload Failed with %s", __FILE__, __LINE__, __FUNCTION__, (char *) cgmi_ErrorString(stat).c_str());
        }
        if (connidStat == CGMI_ERROR_SUCCESS)
        {
            int32_t upnpret = kHN_NoErr;
            upnpret = upnp_CmsConnectionComplete((char *)mcmUuid.c_str(), connid);
            if (upnpret != kHN_NoErr)
            {
                LOG(DLOGL_ERROR, "%s:%d - %s :: upnp_CmsConnectionComplete Failed with %d", __FILE__, __LINE__, __FUNCTION__, upnpret);
            }
        }
        stat = cgmi_DestroySession(pSessionId);
        if (stat != CGMI_ERROR_SUCCESS)
        {
            LOG(DLOGL_ERROR, "%s:%d - %s :: cgmi_DestroySession Failed with %s", __FILE__, __LINE__, __FUNCTION__, (char *) cgmi_ErrorString(stat).c_str());
        }
        else
        {
            LOG(DLOGL_REALLY_NOISY, "Session destroyed successfully; Now reset the Session Id");
            pSessionId = NULL;
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "%s: Invalid Session Id", __FUNCTION__);
        retMspStatus = kMspStatus_BadSource;
        pthread_mutex_unlock(&mrdvr_player_mutex);
    }

    LOG(DLOGL_REALLY_NOISY, "%s: MSPHTTPSource stop return success", __FUNCTION__);

    return retMspStatus;
}


int MSPHTTPSource::getProgramNumber()const
{
    return mProgramNumber;
}

int MSPHTTPSource::getSourceId()const
{
    return mSourceId;
}

std::string MSPHTTPSource::getSourceUrl()const
{
    return mSrcUrl;
}

std::string MSPHTTPSource::getFileName()const
{
    return mFileName;
}

bool MSPHTTPSource::isDvrSource()const
{
    return true;
}

bool MSPHTTPSource::canRecord()const
{
    return false;
}

//Sets the playback speed
eMspStatus MSPHTTPSource::setSpeed(int numerator, unsigned int denominator, uint32_t aNpt)
{
    FNLOG(DL_MSP_MRDVR);

    (void) aNpt;

    LOG(DLOGL_REALLY_NOISY, "%s: Requested speed is %d/%d \n", __FUNCTION__, numerator, denominator);
    LOG(DLOGL_REALLY_NOISY, "%s: Requested position is %d \n", __FUNCTION__, aNpt);

    pthread_mutex_lock(&mrdvr_player_mutex);

    if (mpSessionId)
    {
        cgmi_Status stat = CGMI_ERROR_SUCCESS;

        if ((mCurrentSpeedNum == numerator) && (mCurrentSpeedDen == denominator))
        {
            dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, " Request to set speed, when player is already in that state. Ignoring it");
            pthread_mutex_unlock(&mrdvr_player_mutex);
            return kMspStatus_Ok;
        }

        if ((mPendingPause == true) || (mPendingPlay == true))
        {
            dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "\"Paused\" or \"Playing \" callback from platform is pending. Delaying SetSpeed settings");
            mPendingSetSpeed = true;
            pthread_mutex_unlock(&mrdvr_player_mutex);
            return kMspStatus_Loading;
        }

        /* Backup the requested speed parameters */
        mCurrentSpeedNum = numerator;
        mCurrentSpeedDen = denominator;

        stat = cgmi_SetRate(mpSessionId, (float)(mCurrentSpeedNum / (float) mCurrentSpeedDen));
        if (stat != CGMI_ERROR_SUCCESS)
        {
            LOG(DLOGL_ERROR, "%s:%d - %s :: cgmi_SetRate Failed with %s \n", __FILE__, __LINE__, __FUNCTION__, (char *) cgmi_ErrorString(stat).c_str());
            mPendingPause = false;
            mPendingPlay = false;
            mPendingSetSpeed = false;
            pthread_mutex_unlock(&mrdvr_player_mutex);
            return kMspStatus_BadSource;
        }

        /* Verify the request is playback or pause */
        if (numerator == 0)
        {
            mPendingPause = true;
            dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "++++++++SetSpeed \"Paused\" Request");
        }
        else
        {
            mPendingPlay = true;
            dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "++++++++SetSpeed \"Play  \" Request %d/%d \n", numerator, denominator);
        }

        pthread_mutex_unlock(&mrdvr_player_mutex);

        /* CC is valid only after receiving First PTS decoded alarm from cgmi. So blocking CC control */
        handleClosedCaptions((float)(mCurrentSpeedNum / (float) mCurrentSpeedDen));
    }
    else
    {
        LOG(DLOGL_ERROR, "%s: Invalid Session Id\n", __FUNCTION__);
        pthread_mutex_unlock(&mrdvr_player_mutex);
        return kMspStatus_BadSource;
    }

    return kMspStatus_Ok;
}

void  MSPHTTPSource::handleClosedCaptions(float trickSpeed)
{
    pthread_mutex_lock(&mCgmiDataMutex);
    if (false == mCgmiFirstPTSReceived)
    {
        pthread_mutex_unlock(&mCgmiDataMutex);
        LOG(DLOGL_ERROR, "CC is not valid without First PTS decoded call from cgmi");
        return;
    }
    pthread_mutex_unlock(&mCgmiDataMutex);

    Avpm *inst = NULL;
    inst = Avpm::getAvpmInstance();

    if (inst)
    {
        bool ccEnabled = false;
        inst->getCc(&ccEnabled);
        if (true == ccEnabled)
        {
            int ccTrickStatus = CC_TRICK_ON;    //CC is OFF when trick mode is ON
            if (trickSpeed == 1.0)
            {
                //Show CC when playing at normal speed
                ccTrickStatus = CC_TRICK_OFF;
            }

            LOG(DLOGL_REALLY_NOISY, "%s: ccSetTrickPlayStatus with arg - %d", __FUNCTION__, ccTrickStatus);
            //Hide CC when doing trick modes
            ccSetTrickPlayStatus(ccTrickStatus);
        }
    }
}

void  MSPHTTPSource::buildCmUUIDfromCDSUrl(const char * pcdsUrl)
{
    if (pcdsUrl != NULL)
    {
        LOG(DLOGL_REALLY_NOISY, "Value of mcmUuid is %s in function %s", (char *)mcmUuid.c_str(), __FUNCTION__);
        mcmUuid = "";
        tDAHnUpnp_DeviceUUID *rootDevList = NULL;
        tDAHnUpnp_DeviceUUID *subDevList = NULL;
        tDAHnUpnp_ServiceUUID *serviceList = NULL;
        uint32_t numRootDevices, numSubDevices, numServices = 0;

        char ipBuff[20] = "";
        char * url_ip = NULL;
        char *cdsUrl = strdup(pcdsUrl);
        char *strtokPtr;

        url_ip = strtok_r(cdsUrl, "/", &strtokPtr);
        url_ip = strtok_r(strtok_r(NULL, "/", &strtokPtr), ":", &strtokPtr);
        LOG(DLOGL_REALLY_NOISY, "Value of mcmUuid outside k loop %s", (char *)mcmUuid.c_str());
        if (url_ip != NULL)
        {
            eDAStatus status = DiscoveryAdapter::getInstance()->DAHnUpnp_GetDevices(&rootDevList, &numRootDevices);
            LOG(DLOGL_REALLY_NOISY, " %s:%d numRootDevices:%d", __FUNCTION__, __LINE__, numRootDevices);

            if (status == kDAHnUpnp_Ok)
            {
                for (uint32_t i = 0; i < numRootDevices; i++)
                {
                    LOG(DLOGL_REALLY_NOISY, "Value of i %d", i);
                    DiscoveryAdapter::getInstance()->DAHnUpnpDevice_GetString(rootDevList[i], kDAHnUpnpDeviceString_PropertyIpv4address, ipBuff, sizeof(ipBuff));
                    if (strcmp(url_ip, ipBuff) == 0)
                    {
                        status = DiscoveryAdapter::getInstance()->DAHnUpnpDevice_GetSubDevices(rootDevList[i], &subDevList, &numSubDevices);
                        LOG(DLOGL_REALLY_NOISY, " %s:%d numSubDevices:%d", __FUNCTION__, __LINE__, numSubDevices);
                        if (status == kDAHnUpnp_Ok)
                        {
                            for (uint32_t j = 0; j < numSubDevices; j++)
                            {
                                LOG(DLOGL_REALLY_NOISY, "Value of j %d", j);
                                status = DiscoveryAdapter::getInstance()->DAHnUpnpDevice_GetDeviceServices(subDevList[j], &serviceList, &numServices);
                                LOG(DLOGL_REALLY_NOISY, " %s:%d numServices:%d", __FUNCTION__, __LINE__, numServices);
                                if (status == kDAHnUpnp_Ok)
                                {
                                    for (uint32_t k = 0; k < numServices; k++)
                                    {
                                        LOG(DLOGL_REALLY_NOISY, "Value of k %d", k);
                                        if (DiscoveryAdapter::getInstance()->isCMService(serviceList[k]))
                                        {
                                            mcmUuid = serviceList[k];
                                            LOG(DLOGL_REALLY_NOISY, "Value of mcmUuid in k loop %s", (char *)mcmUuid.c_str());
                                            break;
                                        }
                                    }
                                }
                                else
                                {
                                    LOG(DLOGL_REALLY_NOISY, "%s :%d failed to get DAHnUpnpDevice_GetDeviceServices list", __FUNCTION__, __LINE__);
                                }
                                DiscoveryAdapter::getInstance()->DAHnUpnpServiceUuidList_Finalize((const tDAHnUpnp_ServiceUUID **)&serviceList);
                            }
                        }
                        else
                        {
                            LOG(DLOGL_REALLY_NOISY, "%s :%d failed to get DAHnUpnpDevice_GetSubDevices list", __FUNCTION__, __LINE__);
                        }
                        DiscoveryAdapter::getInstance()->DAHnUpnpDeviceUuidList_Finalize((const tDAHnUpnp_DeviceUUID **)&subDevList);
                    }
                }
            }
            else
            {
                LOG(DLOGL_ERROR, "%s :%d failed to get DAHnUpnp_GetDevices list", __FUNCTION__, __LINE__);
            }
            DiscoveryAdapter::getInstance()->DAHnUpnpDeviceUuidList_Finalize((const tDAHnUpnp_DeviceUUID **)&rootDevList);
        }
        else
        {
            LOG(DLOGL_ERROR, "url_ip is NULL");
        }
        if (cdsUrl)
        {
            free(cdsUrl);
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "pcdsUrl is NULL");
    }
}


///Gets the current playback position
void MSPHTTPSource::buildFileName()
{
    FNLOG(DL_MSP_MRDVR);

    int pos;
    std::string temp;

    LOG(DLOGL_REALLY_NOISY, "TNR: %s mSrcUrl %s\n", __PRETTY_FUNCTION__, mSrcUrl.c_str());

    mFileName = "";

    if ((pos = mSrcUrl.find("mrdvr://")) == 0)
    {
        LOG(DLOGL_REALLY_NOISY, "TNR: %s mSrcUrl %s pos %d\n", __PRETTY_FUNCTION__, mSrcUrl.c_str(), pos);

        temp = mSrcUrl.substr(strlen("mrdvr://"));
        mFileName = temp;

        LOG(DLOGL_REALLY_NOISY, "%s: MRDVR  file name  %s\n", __PRETTY_FUNCTION__, mFileName.c_str());

        mParseStatus = kMspStatus_Ok;
    }
    else if ((pos = mSrcUrl.find("sctetv://")) == 0)
    {
        char *httpURL = NULL;

        if (CDvrLiveStreamAdapter::getHandle().GetUrlMapCache((char *) mSrcUrl.c_str(), &httpURL) == 0)
        {

            LOG(DLOGL_REALLY_NOISY, "%s: CDvrLiveStreamAdapter::getHandle().GetUrlMapCache returned %s for %s \n", __PRETTY_FUNCTION__, mSrcUrl.c_str(), httpURL);
            mFileName = httpURL;

            LOG(DLOGL_NORMAL, "%s: MRDVR  file name for SCTE URL is %s\n", __PRETTY_FUNCTION__, mFileName.c_str());

            mParseStatus = kMspStatus_Ok;
        }
        else
        {
            httpURL = Csci_Dlna_UPnPgetCDSURL(mSrcUrl.c_str());
            if (httpURL)
            {
                mFileName = httpURL;

                free(httpURL);
                httpURL = NULL;
                LOG(DLOGL_NORMAL, "%s: CDvrLiveStreamAdapter::getHandle().GetUrlMapCache failed to return.  getResUrlfromCDS returns  %s for %s \n", __PRETTY_FUNCTION__, mFileName.c_str(), mSrcUrl.c_str());
            }
        }
    }
    else if ((pos = mSrcUrl.find("sappv://")) == 0)
    {
        LOG(DLOGL_REALLY_NOISY, "****sappv://*******\n");
        char *httpURL = NULL;

        if (CDvrLiveStreamAdapter::getHandle().GetUrlMapCache((char *) mSrcUrl.c_str(), &httpURL) == 0)
        {
            LOG(DLOGL_NORMAL, "%s: CDvrLiveStreamAdapter::getHandle().GetUrlMapCache returned %s for %s \n", __PRETTY_FUNCTION__, mSrcUrl.c_str(), httpURL);

            mFileName = httpURL;

            LOG(DLOGL_REALLY_NOISY, "%s: MRDVR  file name for PPV URL is %s\n", __PRETTY_FUNCTION__, mFileName.c_str());

            mParseStatus = kMspStatus_Ok;
        }
        else
        {
            httpURL = Csci_Dlna_UPnPgetCDSURL(mSrcUrl.c_str());
            if (httpURL)
            {
                mFileName = httpURL;

                free(httpURL);
                httpURL = NULL;
                LOG(DLOGL_NORMAL, "%s: CDvrLiveStreamAdapter::getHandle().GetUrlMapCache failed to return.  getResUrlfromCDS returns  %s for %s \n", __PRETTY_FUNCTION__, mFileName.c_str(), mSrcUrl.c_str());
            }
        }
    }
    else if ((pos = mSrcUrl.find(VOD_SOURCE_URI_PREFIX)) == 0)
    {
        mFileName =  mSrcUrl;
        LOG(DLOGL_NORMAL, "This is a VOD playback url ****lscp://*******\n");
    }
    else
    {
        LOG(DLOGL_ERROR, "%s: %s is Unknown source URL type  \n", __PRETTY_FUNCTION__, mSrcUrl.c_str());
        mFileName = "";
    }
}

///Gets the playback position
eMspStatus MSPHTTPSource::getPosition(float *pNptTime)
{
    FNLOG(DL_MSP_MRDVR);

    eMspStatus retMspStatus = kMspStatus_Ok;

    pthread_mutex_lock(&mrdvr_player_mutex);

    if (pNptTime)
    {
        if (mpSessionId)
        {
            cgmi_Status stat = CGMI_ERROR_SUCCESS;
            stat = cgmi_GetPosition(mpSessionId, pNptTime);
            if (stat != CGMI_ERROR_SUCCESS)
            {
                LOG(DLOGL_ERROR, "%s:%d - %s :: cgmi_GetPosition Failed with %s \n", __FILE__, __LINE__, __FUNCTION__, (char *) cgmi_ErrorString(stat).c_str());
                retMspStatus = kMspStatus_BadSource;
            }
            else
            {
                LOG(DLOGL_REALLY_NOISY, "%s: cgmi_GetPosition : curPosition = (%f)\n", __FUNCTION__, *pNptTime);
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "%s: Invalid Session Id\n", __FUNCTION__);
            retMspStatus = kMspStatus_BadSource;
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "%s: Invalid pNptTime parameter\n", __FUNCTION__);
        retMspStatus = kMspStatus_Error;
    }

    pthread_mutex_unlock(&mrdvr_player_mutex);

    return  retMspStatus;
}

///Sets the playback position
eMspStatus MSPHTTPSource::setPosition(float aNptTime)
{
    FNLOG(DL_MSP_MRDVR);

    eMspStatus retMspStatus = kMspStatus_Ok;

    LOG(DLOGL_REALLY_NOISY, "%s: Requested position is %f \n", __FUNCTION__, aNptTime);

    pthread_mutex_lock(&mrdvr_player_mutex);

    if (mpSessionId)
    {

        if ((mPendingPause == true) || (mPendingPlay == true))
        {
            dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "\"Paused\" or \"Playing \" callback from platform is pending. Delaying SetPosition settings");
            mPendingSetPosition = true;
            pthread_mutex_unlock(&mrdvr_player_mutex);
            return kMspStatus_Loading;
        }

        cgmi_Status stat = CGMI_ERROR_SUCCESS;
        stat = cgmi_SetPosition(mpSessionId, aNptTime);
        if (stat != CGMI_ERROR_SUCCESS)
        {
            mPendingSetPosition = false;
            LOG(DLOGL_ERROR, "%s:%d - %s :: cgmi_SetPosition Failed with %s \n", __FILE__, __LINE__, __FUNCTION__, (char *) cgmi_ErrorString(stat).c_str());
            retMspStatus = kMspStatus_BadSource;
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "%s: Invalid Session Id\n", __FUNCTION__);
        retMspStatus = kMspStatus_BadSource;
    }

    pthread_mutex_unlock(&mrdvr_player_mutex);

    return retMspStatus;
}

bool MSPHTTPSource::isPPV(void)
{
    return false;
}

bool MSPHTTPSource::isQamrf(void)
{
    return false;
}

bool MSPHTTPSource::isSDV(void)
{
    return mSdvSource;
}

bool MSPHTTPSource::isMusic(void)
{
    return mMusicSource;
}

eMspStatus MSPHTTPSource::release()
{
    FNLOG(DL_MSP_MRDVR);
    // release the source
    return kMspStatus_Ok;
}

///Sets the presentation parameters
eMspStatus MSPHTTPSource::setPresentationParams(tAvRect *vidScreenRect, bool enablePictureModeSetting, bool enableAudioFocus)
{
    FNLOG(DL_MSP_MRDVR);

    (void) enablePictureModeSetting;

    if (vidScreenRect == NULL)
    {
        LOG(DLOGL_ERROR, "%s: Error null vidScreenRect", __FUNCTION__);
        return kMspStatus_BadParameters;
    }

    LOG(DLOGL_REALLY_NOISY, "rect x: %d  y: %d  w: %d  h: %d  audio: %d",
        vidScreenRect->x, vidScreenRect->y, vidScreenRect->width, vidScreenRect->height, enableAudioFocus);

    //store the rect for zoom callback purpose...
    MSPHTTPSource::mVidScreenRect.x = vidScreenRect->x;
    MSPHTTPSource::mVidScreenRect.y = vidScreenRect->y;
    MSPHTTPSource::mVidScreenRect.width = vidScreenRect->width;
    MSPHTTPSource::mVidScreenRect.height = vidScreenRect->height;

    eMspStatus retMspStatus = kMspStatus_Ok;

    pthread_mutex_lock(&mrdvr_player_mutex);

    if (mpSessionId)
    {
        if (kMspStatus_Ok != (retMspStatus = ApplyZoom(vidScreenRect)))
        {
            LOG(DLOGL_ERROR, "%s : %d : %s ApplyZoom Failed \n", __FILE__, __LINE__, __FUNCTION__);
        }
        else
        {
            LOG(DLOGL_REALLY_NOISY, "%s : %d : %s ApplyZoom Successful \n", __FILE__, __LINE__, __FUNCTION__);
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "%s: Invalid Session Id\n", __FUNCTION__);
        retMspStatus = kMspStatus_BadSource;
    }

    pthread_mutex_unlock(&mrdvr_player_mutex);

    return retMspStatus;
}

///Interface to Register the Audio Language Callback with AVPM
void MSPHTTPSource::SetAudioLangCB(void *aCbData, AudioLangChangedCB aLangChangedCB)
{
    dlog(DL_MSP_MRDVR,  DLOGL_REALLY_NOISY, "%s: In SetAudioLangCB\n", __FUNCTION__);

    Avpm *inst = NULL;
    inst = Avpm::getAvpmInstance();
    if (inst)
    {
        inst->SetAudioLangCB(aCbData, aLangChangedCB);
    }
}

///Get the information about the AV components in the stream
///> pid number
///> Elementary stream type
///> ISO language code
eMspStatus MSPHTTPSource::GetComponents(tComponentInfo *info, uint32_t infoSize, uint32_t *count, uint32_t offset, Psi *ptrPsi)
{
    FNLOG(DL_MSP_MRDVR);

    *count = 0;

    if (infoSize == 0 || info == NULL)
    {
        LOG(DLOGL_ERROR, "%s: Invalid parameters passed in\n", __FUNCTION__);
        return kMspStatus_BadParameters;
    }

    if (ptrPsi == NULL)
    {
        LOG(DLOGL_ERROR, "%s: Invalid state of PSI - Not yet started.\n", __FUNCTION__);
        return kMspStatus_StateError;
    }

    eMspStatus retMspStatus = kMspStatus_Ok;

    pthread_mutex_lock(&mrdvr_player_mutex);

    if (mpSessionId)
    {
        /* Get the list of components available from PSI module */
        retMspStatus = ptrPsi->getComponents(info, infoSize, count, offset);

        /* Update the selected status of the PIDs/ Components */
        if (kMspStatus_Ok == retMspStatus)
        {
            for (uint32_t i = 0; i < *count; i++)
            {
                info[i].selected = false;
                if ((info[i].pid == mVideoPid) || (info[i].pid == mAudioPid))
                {
                    info[i].selected = true;
                }
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "%s : %d : %s GetComponents Failed \n", __FILE__, __LINE__, __FUNCTION__);
        }
    }
    else
    {
        dlog(DL_MSP_MRDVR,  DLOGL_ERROR, "%s: Invalid Session Id\n", __FUNCTION__);
        retMspStatus = kMspStatus_BadSource;
    }

    pthread_mutex_unlock(&mrdvr_player_mutex);

    return retMspStatus;
}

///Sets or Enables the requested audio PID
eMspStatus MSPHTTPSource::SetAudioPid(uint32_t aPid)
{
    FNLOG(DL_MSP_MRDVR);

    eMspStatus retMspStatus = kMspStatus_Ok;

    pthread_mutex_lock(&mrdvr_player_mutex);

    if (mpSessionId)
    {
        cgmi_Status stat = CGMI_ERROR_SUCCESS;
        stat = cgmi_SetPid(mpSessionId, aPid, STREAM_TYPE_AUDIO, true);
        if (stat != CGMI_ERROR_SUCCESS)
        {
            LOG(DLOGL_ERROR, "%s:%d - %s :: cgmi_SetPid Failed with %s \n", __FILE__, __LINE__, __FUNCTION__, (char *) cgmi_ErrorString(stat).c_str());
            retMspStatus = kMspStatus_BadSource;
        }

        mAudioPid = aPid;
    }
    else
    {
        LOG(DLOGL_ERROR, "%s: Invalid Session Id\n", __FUNCTION__);
        retMspStatus = kMspStatus_BadSource;
    }

    pthread_mutex_unlock(&mrdvr_player_mutex);

    return retMspStatus;
}

///Selects the AUDIO and VIDEO components to start the play with
eMspStatus MSPHTTPSource::SelectAudioVideoComponents(Psi *ptrPsi, bool isEasAudioActive)
{
    FNLOG(DL_MSP_MRDVR);

    cgmi_Status 		stat = CGMI_ERROR_SUCCESS;

    tcgmi_PidInfo 		pidTable[2];
    uint8_t             pidTableIndex = 0;

    memset(&pidTable, 0, sizeof(pidTable));

    eMspStatus retMspStatus = kMspStatus_Ok;

    pthread_mutex_lock(&mrdvr_player_mutex);

    if (mpSessionId)
    {
        /* Get the VIDEO pid to be decoded */
        LanguageSelection VidLangSelect(LANG_SELECT_VIDEO, ptrPsi);
        tPid vPid = VidLangSelect.videoPidSelected();
        if (vPid.pid != INVALID_PID_VALUE)
        {
            dlog(DL_MSP_MRDVR,  DLOGL_REALLY_NOISY, "%s: VIDEO PID = %d STREAM TYPE = %d", __FUNCTION__, vPid.pid, vPid.streamType);
            mVideoPid = vPid.pid;

            /* Populate the video PID to pass to CGMI */
            pidTable[pidTableIndex].streamType 	= STREAM_TYPE_VIDEO;
            pidTable[pidTableIndex].pid			= mVideoPid;

            pidTableIndex++;
        }
        else
        {
            LOG(DLOGL_ERROR, "%s:%d - %s :: No valid <VIDEO> PID in the stream", __FILE__, __LINE__, __FUNCTION__);
        }

        /* Get the AUDIO pid to be decoded */
        LanguageSelection audLangSelect(LANG_SELECT_AUDIO, ptrPsi);
        tPid aPid = audLangSelect.pidSelected();
        if (aPid.pid != INVALID_PID_VALUE)
        {
            dlog(DL_MSP_MRDVR,  DLOGL_REALLY_NOISY, "%s: AUDIO PID = %d STREAM TYPE = %d", __FUNCTION__, aPid.pid, aPid.streamType);
            mAudioPid = aPid.pid;

            /* Populate the audio PID to pass to CGMI if EAS audio is not active */
            if (isEasAudioActive == false)
            {
                /* Populate the audio PID to pass to CGMI */
                pidTable[pidTableIndex].streamType 	= STREAM_TYPE_AUDIO;
                pidTable[pidTableIndex].pid			= mAudioPid;

                pidTableIndex++;
            }
            else
            {
                LOG(DLOGL_NORMAL, "%s: EAS Audio is playing. so not starting audio now...", __FUNCTION__);
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "%s:%d - %s :: No valid <AUDIO> PID in the stream", __FILE__, __LINE__, __FUNCTION__);
        }

        /* Dump the pid table */
        dlog(DL_MSP_MRDVR,  DLOGL_NORMAL, "%s: PIDTBL SIZE = %d\n", __FUNCTION__, pidTableIndex);
        for (uint8_t i = 0; i < pidTableIndex; i++)
        {
            dlog(DL_MSP_MRDVR, DLOGL_NORMAL, "%s: PIDTBL INDEX = %d PID = %d STREAM TYPE = %d", __FUNCTION__, i, pidTable[i].pid, pidTable[i].streamType);
        }

        /* Trigger the decoding of selected pid table */
        stat = cgmi_SetPidTable(mpSessionId, pidTableIndex, pidTable);
        if (stat != CGMI_ERROR_SUCCESS)
        {
            LOG(DLOGL_ERROR, "%s:%d - %s :: cgmi_SetPidTable Failed with %s", __FILE__, __LINE__, __FUNCTION__, (char *) cgmi_ErrorString(stat).c_str());
            retMspStatus = kMspStatus_BadSource;
        }
        else
        {
            /* Fake the first frame alarm to application if MUSIC service <like DMX and GALAXY> */
            if (true == isMusic() && mAudioPid != INVALID_PID_VALUE)
            {
                /* Fake a FirstFrameAlarm CB and log a dummy statement*/
                LOG(DLOGL_EMERGENCY, "%s: Tuning to source %s using CGMI SESSION ID %p received NOTIFY_FIRST_PTS_DECODED\n", __FUNCTION__, mSrcUrl.c_str(), mpSessionId);
                mSrcStateCB(mClientContext, kSrcFirstFrameEvent);
                mHTTPSrcState = kHTTPSrcPlaying;
            }
        }
    }
    else
    {
        dlog(DL_MSP_MRDVR,  DLOGL_ERROR, "%s: Invalid Session Id", __FUNCTION__);
        retMspStatus = kMspStatus_BadSource;
    }

    pthread_mutex_unlock(&mrdvr_player_mutex);

    return retMspStatus;
}


///Update or Set the audio language as per the user current preferences
eMspStatus MSPHTTPSource::UpdateAudioLanguage(Psi *ptrPsi)
{
    FNLOG(DL_MSP_MRDVR);

    eMspStatus retMspStatus = kMspStatus_Ok;

    pthread_mutex_lock(&mrdvr_player_mutex);

    if (mpSessionId)
    {
        LanguageSelection langSelect(LANG_SELECT_AUDIO, ptrPsi);
        tPid aPid = langSelect.pidSelected();
        dlog(DL_MSP_MRDVR,  DLOGL_REALLY_NOISY, "%s: AUDIO PID = %d STREAM TYPE = %d\n", __FUNCTION__, aPid.pid, aPid.streamType);

        /* Check whether valid AUDIO component is selected */
        if (aPid.pid == INVALID_PID_VALUE)
        {
            retMspStatus = kMspStatus_PsiError;
        }
        else
        {
            cgmi_Status stat = CGMI_ERROR_SUCCESS;
            stat = cgmi_SetPid(mpSessionId, aPid.pid, STREAM_TYPE_AUDIO, true);
            if (stat != CGMI_ERROR_SUCCESS)
            {
                LOG(DLOGL_ERROR, "%s:%d - %s :: cgmi_SetPid <AUDIO> Failed with %s \n", __FILE__, __LINE__, __FUNCTION__, (char *) cgmi_ErrorString(stat).c_str());
                retMspStatus = kMspStatus_BadSource;
            }
        }

        mAudioPid = aPid.pid;
    }
    else
    {
        dlog(DL_MSP_MRDVR,  DLOGL_ERROR, "%s: Invalid Session Id\n", __FUNCTION__);
        retMspStatus = kMspStatus_BadSource;
    }

    pthread_mutex_unlock(&mrdvr_player_mutex);

    return retMspStatus;
}

///Callback function to receive the asynchronous events from the CGMI
void MSPHTTPSource::CgmiPlayBkCallbackFunction(void *pUserData, void *pSession, tcgmi_Event event, tcgmi_Data *pData)
{
    FNLOG(DL_MSP_MRDVR);

    pthread_mutex_lock(&mrdvr_player_mutex);

    LOG(DLOGL_REALLY_NOISY, "%s: RDK CGMI Session = %p SAIL MW Session = %p\n", __FUNCTION__, pSession, mpSessionId);

    if (mpSessionId != pSession)
    {
        LOG(DLOGL_REALLY_NOISY, "%s: Event %d for an unknown session to Middleware - so not proceeding further\n", __FUNCTION__, event);
        pthread_mutex_unlock(&mrdvr_player_mutex);
        return;
    }

    tcgmi_Resoultion* streamResolution = NULL;
    MSPHTTPSource *inst = (MSPHTTPSource *) pUserData;
    if (NULL == inst)
    {
        LOG(DLOGL_ERROR, "%s: Invalid user data\n", __FUNCTION__);
        pthread_mutex_unlock(&mrdvr_player_mutex);
        return;
    }
    else
    {
        LOG(DLOGL_EMERGENCY, "%s: Received event %s via CGMI callback for CGMI session %p\n", __FUNCTION__, (char *) inst->cgmi_EventString(event).c_str(), pSession);

        switch (event)
        {
        case NOTIFY_STREAMING_OK:                   ///<
            LOG(DLOGL_REALLY_NOISY, "%s: NOTIFY_STREAMING_OK\n", __FUNCTION__);
            break;
        case NOTIFY_FIRST_PTS_DECODED:              ///<The decoding has started for the stream and or after a seek.
        {
            bool isSOC = false;
            LOG(DLOGL_REALLY_NOISY, "%s: Tuning to source %s using CGMI SESSION ID %p received NOTIFY_FIRST_PTS_DECODED\n", __FUNCTION__, inst->mSrcUrl.c_str(), pSession);
            pthread_mutex_lock(&(inst->mCgmiDataMutex));
            if (pData != NULL && pData->data != NULL)
            {
                streamResolution = (tcgmi_Resoultion*)pData->data;

                mStreamResolutionWidth = streamResolution->width;
                mStreamResolutionHeight = streamResolution->height;
            }

            LOG(DLOGL_REALLY_NOISY, "%s: registering ccChanged CB ", __FUNCTION__);

            inst->SetCcChangedCB(inst, inst->ccChangedCB);

            inst->mSrcStateCB(inst->mClientContext, kSrcFirstFrameEvent);
            inst->mCgmiFirstPTSReceived = true;
            inst->mHTTPSrcState = kHTTPSrcPlaying;

            Avpm *avpminst = Avpm::getAvpmInstance();
            if (avpminst)
            {
                uint8_t emi = CGMI_GET_CCI_EMI(inst->mCci);
                if (COPY_NO_MORE == emi)
                {
                    isSOC = true;
                }
                avpminst->SetCCIBits((uint8_t) inst->mCci, isSOC);
            }
            pthread_mutex_unlock(&(inst->mCgmiDataMutex));
        }
        break;
        case NOTIFY_STREAMING_NOT_OK:               ///< A streaming error has occurred.
            LOG(DLOGL_REALLY_NOISY, "%s: NOTIFY_STREAMING_NOT_OK - Streaming error occured\n", __FUNCTION__);
            inst->mSrcStateCB(inst->mClientContext, kSrcProblem);

            break;
        case NOTIFY_SEEK_DONE:                      ///< The seek has completed
            LOG(DLOGL_REALLY_NOISY, "%s: NOTIFY_SEEK_DONE\n", __FUNCTION__);
            break;
        case NOTIFY_START_OF_STREAM:                ///< The Current position is now at the Start of the stream
            LOG(DLOGL_REALLY_NOISY, "%s: NOTIFY_START_OF_STREAM - BOF callback received\n", __FUNCTION__);
            LOG(DLOGL_REALLY_NOISY, "Clearing off pending speed  and position settings");
            inst->mPendingPause  = false;
            inst->mPendingPlay   = false;
            inst->mPendingSetSpeed    = false;
            inst->mPendingSetPosition = false;
            inst->mSrcStateCB(inst->mClientContext, kSrcBOF);
            break;
        case NOTIFY_END_OF_STREAM:                  ///< You are at the end of stream or EOF
            LOG(DLOGL_REALLY_NOISY, "%s: NOTIFY_END_OF_STREAM - EOF callback received\n", __FUNCTION__);
            LOG(DLOGL_REALLY_NOISY, "Clearing off pending speed  and position settings");
            inst->mPendingPause  = false;
            inst->mPendingPlay   = false;
            inst->mPendingSetSpeed    = false;
            inst->mPendingSetPosition = false;
            inst->mSrcStateCB(inst->mClientContext, kSrcEOF);
            break;
        case NOTIFY_DECRYPTION_FAILED:              ///< Not able to decrypt the stream: we don't know how to decrypt
            LOG(DLOGL_REALLY_NOISY, "%s: NOTIFY_DECRYPTION_FAILED\n", __FUNCTION__);
            break;
        case NOTIFY_NO_DECRYPTION_KEY:              ///<No key has been provided to decrypt this content.
            LOG(DLOGL_REALLY_NOISY, "%s: NOTIFY_NO_DECRYPTION_KEY\n", __FUNCTION__);
            break;
        case NOTIFY_VIDEO_ASPECT_RATIO_CHANGED:     ///<The straem has changed it's aspect ratio
            LOG(DLOGL_REALLY_NOISY, "%s: NOTIFY_VIDEO_ASPECT_RATIO_CHANGED\n", __FUNCTION__);
            break;
        case NOTIFY_VIDEO_RESOLUTION_CHANGED:       ///<The resolution of the stream has changed.
            if (pData != NULL && pData->data != NULL)
            {
                streamResolution = (tcgmi_Resoultion*)pData->data;

                mStreamResolutionWidth = streamResolution->width;
                mStreamResolutionHeight = streamResolution->height;

                LOG(DLOGL_REALLY_NOISY, "%s: NOTIFY_VIDEO_RESOLUTION_CHANGED, Resolution : width=%d, height=%d \n", __FUNCTION__, mStreamResolutionWidth, mStreamResolutionHeight);
            }
            break;
        case NOTIFY_CHANGED_LANGUAGE_AUDIO:         ///<The streams Audio language has changed
            LOG(DLOGL_REALLY_NOISY, "%s: NOTIFY_CHANGED_LANGUAGE_AUDIO\n", __FUNCTION__);
            break;
        case NOTIFY_CHANGED_LANGUAGE_SUBTITLE:      ///<The subtitle language has changed.
            LOG(DLOGL_REALLY_NOISY, "%s: NOTIFY_CHANGED_LANGUAGE_SUBTITLE\n", __FUNCTION__);
            break;
        case NOTIFY_CHANGED_LANGUAGE_TELETEXT:      ///<The teletext language has changed.
            LOG(DLOGL_REALLY_NOISY, "%s: NOTIFY_CHANGED_LANGUAGE_TELETEXT\n", __FUNCTION__);
            break;
        case NOTIFY_MEDIAPLAYER_URL_OPEN_FAILURE:   ///<The requested URL could not be opened.
            LOG(DLOGL_REALLY_NOISY, "%s: NOTIFY_MEDIAPLAYER_URL_OPEN_FAILURE\n", __FUNCTION__);
            if (pData && pData->data)
            {
                char *errStr = (char *) pData->data;
                LOG(DLOGL_ERROR, "%s: NOTIFY_MEDIAPLAYER_URL_OPEN_FAILURE Error is %s\n", __FUNCTION__, errStr);
            }
            inst->mHTTPSrcState = kHTTPSrcInvalid;
            break;
        case NOTIFY_MEDIAPLAYER_UNKNOWN:             ///<An unexpected error has occured.
            LOG(DLOGL_REALLY_NOISY, "%s: NOTIFY_MEDIAPLAYER_UNKNOWN\n", __FUNCTION__);
            break;
        case NOTIFY_PMT_READY: ///< PMT section is received. The buffer will be available as tcgmi_Data in cgmi_EventCallback.
            LOG(DLOGL_REALLY_NOISY, "%s: NOTIFY_PMT_READY\n", __FUNCTION__);
            if (pData != NULL && pData->data != NULL && pData->size != 0)
            {
                LOG(DLOGL_REALLY_NOISY, "%s: data ptr = %p data size = %d\n", __FUNCTION__, pData->data, pData->size);
                inst->mPMTDataSize = pData->size;
                inst->mpPMTData = (uint8_t *) malloc((pData->size) * sizeof(uint8_t));
                if (inst->mpPMTData == NULL)
                {
                    LOG(DLOGL_ERROR, "%s: memory allocation failed \n", __FUNCTION__);
                }
                else
                {
                    memset(inst->mpPMTData, 0, pData->size);
                    memcpy(inst->mpPMTData, pData->data, pData->size);
                }
            }
            else
            {
                LOG(DLOGL_ERROR, "%s: Error: Invalid payload received in NOTIFY_PMT_READY\n", __FUNCTION__);
            }

            inst->mSrcStateCB(inst->mClientContext, kSrcPMTReadyEvent);

            break;
        case NOTIFY_PSI_READY:             ///< PSI is detected, ready to decode (valid for TS content only)
            LOG(DLOGL_REALLY_NOISY, "%s: NOTIFY_PSI_READY\n", __FUNCTION__);
            inst->mSrcStateCB(inst->mClientContext, kSrcPSIReadyEvent);
            break;
        case NOTIFY_COPY_CONTROL_INFO_CHANGED:
            if (pData != NULL && pData->data != NULL)
            {
                bool isSOC = false;
                tcgmi_CciData cci = 0;
                uint8_t emi = 0, aps = 0, cit = 0, rct = 0;
                cci = (*((tcgmi_CciData*)pData->data)) ;
                inst->mCci = cci;
                LOG(DLOGL_REALLY_NOISY, "%s : NOTIFY_COPY_CONTROL_INFO_CHANGED CCIByte --> %u", __FUNCTION__, inst->mCci);
                emi = CGMI_GET_CCI_EMI(cci);
                LOG(DLOGL_REALLY_NOISY, "%s %d :::%s", "EMI =  ", emi, cciEmiString[emi]);
                aps = CGMI_GET_CCI_APS(cci);
                LOG(DLOGL_REALLY_NOISY, "%s %d ::: %s", "APS = ", aps, cciApsString[aps]);
                cit = CGMI_GET_CCI_CIT(cci);
                LOG(DLOGL_REALLY_NOISY, "%s %d::: %s" , "CIT = ", cit, cciCitString[cit]);
                rct = CGMI_GET_CCI_RCT(cci);;
                LOG(DLOGL_REALLY_NOISY, "%s %d ::: %s", "RCT =  ", rct, cciCitString[rct]);
                //Triggering back the callback even while doing trick play
                if (inst->mHTTPSrcState == kHTTPSrcPlaying || inst->mHTTPSrcState == kHTTPSrcPaused)
                {
                    LOG(DLOGL_REALLY_NOISY, "%s  %u : NOTIFY_COPY_CONTROL_INFO_CHANGED event is received already when session is in progress ", __FUNCTION__, inst->mCci);
                    Avpm *avpminst = Avpm::getAvpmInstance();
                    if (avpminst)
                    {
                        uint8_t emi = CGMI_GET_CCI_EMI(cci);
                        if (COPY_NO_MORE == emi)
                        {
                            isSOC = true;
                        }
                        avpminst->SetCCIBits((uint8_t) inst->mCci, isSOC);
                    }
                }
                else
                {
                    LOG(DLOGL_ERROR, "%s  %u : NOTIFY_COPY_CONTROL_INFO_CHANGED event is received when session is not in progress ", __FUNCTION__, inst->mCci);
                }
            }
            break;

        case NOTIFY_CHANGED_SRC_STATE:
        {
            if (pData != NULL && pData->data != NULL)
            {
                tcgmi_StateInfo *pSgmiStateInfo = (tcgmi_StateInfo *) pData->data;
                switch (pSgmiStateInfo->state)
                {
                case CGMI_SrcState_Stopped:
                    dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "HTTP Source at stopped state and the reason is %d", pSgmiStateInfo->reason);
                    switch (pSgmiStateInfo->reason)
                    {
                    case CGMI_SrcStateReason_EOF:
                        LOG(DLOGL_REALLY_NOISY, "HTTP Src EOF callback received with HTTP state as Stopped");
                        inst->mSrcStateCB(inst->mClientContext, kSrcEOF);
                        break;

                    case CGMI_SrcStateReason_BOF:
                        LOG(DLOGL_REALLY_NOISY, "HTTP Src  BOF callback received with HTTP state as Stopped... Clearing off pending requests...");
                        inst->mPendingPause  = false;
                        inst->mPendingPlay   = false;
                        inst->mPendingSetSpeed    = false;
                        inst->mPendingSetPosition = false;
                        inst->mSrcStateCB(inst->mClientContext, kSrcBOF);
                        break;

                    default:
                        LOG(DLOGL_NORMAL, " UNKNOWN HTTP reason: %d with HTTP state as Stopped", pSgmiStateInfo->reason);
                        break;

                    }
                    inst->mHTTPSrcState = kHTTPSrcStopped;
                    break;

                case CGMI_SrcState_Playing:

                    dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "++++++++SetSpeed \"Play  \" Request ACK %f Reason %d", pSgmiStateInfo->rate, pSgmiStateInfo->reason);

                    switch (pSgmiStateInfo->reason)
                    {
                    case CGMI_SrcStateReason_EOF:
                        LOG(DLOGL_REALLY_NOISY, "HTTP Src EOF callback received with HTTP state as Playing");
                        inst->mSrcStateCB(inst->mClientContext, kSrcEOF);
                        break;

                    case CGMI_SrcStateReason_BOF:
                        LOG(DLOGL_REALLY_NOISY, "HTTP Src  BOF callback received with HTTP state as Playing... Clearing off pending requests... ");
                        inst->mPendingPause  = false;
                        inst->mPendingPlay   = false;
                        inst->mPendingSetSpeed    = false;
                        inst->mPendingSetPosition = false;
                        inst->mSrcStateCB(inst->mClientContext, kSrcBOF);
                        break;

                    default:
                        LOG(DLOGL_NORMAL, " UNKNOWN HTTP reason: %d with HTTP state as Playing", pSgmiStateInfo->reason);
                        if ((inst->mCurrentSpeedNum > 100) && (inst->mPendingPlay == false))
                        {
                            LOG(DLOGL_NORMAL, " Previous trick was fast forward, hence triggereing EOF");
                            inst->mSrcStateCB(inst->mClientContext, kSrcEOF);
                        }
                        else if ((inst->mCurrentSpeedNum > 0) && (inst->mCurrentSpeedNum < 100) && (inst->mPendingPlay == false))
                        {
                            inst->mPendingPause  = false;
                            inst->mPendingPlay   = false;
                            inst->mPendingSetSpeed    = false;
                            inst->mPendingSetPosition = false;
                            LOG(DLOGL_NORMAL, "Previous trick was slow forward, hence triggereing BOF");
                            inst->mSrcStateCB(inst->mClientContext, kSrcBOF);
                        }
                        else
                        {
                            LOG(DLOGL_NORMAL, "Previous trick was not slow forward or fast forward");
                        }
                        break;
                    }
                    inst->mHTTPSrcState = kHTTPSrcPlaying;

                    if (inst->mPendingSetPosition == true)
                    {
                        inst->mSrcStateCB(inst->mClientContext, kSrcPendingSetPosition);
                        inst->mPendingSetPosition = false;
                    }

                    if (inst->mPendingSetSpeed == true)
                    {
                        inst->mSrcStateCB(inst->mClientContext, kSrcPendingSetSpeed);
                        inst->mPendingSetSpeed = false;
                    }

                    inst->mPendingPlay = false;

                    inst->mCurrentSpeedNum = ((int)(pSgmiStateInfo->rate * 100.0));
                    inst->mCurrentSpeedDen = 100;

                    dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "++++++++SetSpeed resetting num = %d den = %d", inst->mCurrentSpeedNum, inst->mCurrentSpeedDen);

                    break;

                case CGMI_SrcState_Paused:

                    dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "++++++++SetSpeed \"Paused\" Request ACK %f Reason %d", pSgmiStateInfo->rate, pSgmiStateInfo->reason);

                    switch (pSgmiStateInfo->reason)
                    {
                    case CGMI_SrcStateReason_EOF:
                        LOG(DLOGL_REALLY_NOISY, "HTTP Src EOF callback received with HTTP state as Paused");
                        inst->mSrcStateCB(inst->mClientContext, kSrcEOF);
                        break;

                    case CGMI_SrcStateReason_BOF:
                        LOG(DLOGL_REALLY_NOISY, "HTTP Src  BOF callback received with HTTP state as Paused... Clearing off pending requests...");
                        inst->mPendingPause  = false;
                        inst->mPendingPlay   = false;
                        inst->mPendingSetSpeed    = false;
                        inst->mPendingSetPosition = false;
                        inst->mSrcStateCB(inst->mClientContext, kSrcBOF);
                        break;

                    default:
                        LOG(DLOGL_NORMAL, " UNKNOWN HTTP reason: %d with HTTP state as Paused", pSgmiStateInfo->reason);
                        break;
                    }
                    inst->mHTTPSrcState = kHTTPSrcPaused;

                    if (inst->mPendingSetPosition == true)
                    {
                        inst->mSrcStateCB(inst->mClientContext, kSrcPendingSetPosition);
                        inst->mPendingSetPosition = false;
                    }

                    if (inst->mPendingSetSpeed == true)
                    {
                        inst->mSrcStateCB(inst->mClientContext, kSrcPendingSetSpeed);
                        inst->mPendingSetSpeed = false;
                    }

                    inst->mPendingPause = false;

                    inst->mCurrentSpeedNum = ((int)(pSgmiStateInfo->rate * 100.0));
                    inst->mCurrentSpeedDen = 100;

                    dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "++++++++SetSpeed resetting num = %d den = %d", inst->mCurrentSpeedNum, inst->mCurrentSpeedDen);

                    break;

                case CGMI_SrcState_MimeAcquired:
                    LOG(DLOGL_REALLY_NOISY, "%s: Source is in  Mime data read from network %d", __FUNCTION__, event);
                    break;

                case CGMI_SrcState_Invalid:
                    LOG(DLOGL_REALLY_NOISY, "%s: Invalid state %d", __FUNCTION__, event);
                    inst->mHTTPSrcState = kHTTPSrcInvalid;
                    break;

                default:
                    LOG(DLOGL_ERROR, "%s: Unknown callback event %d", __FUNCTION__, event);
                    inst->mHTTPSrcState = kHTTPSrcInvalid;
                    break;
                }
            }
            else
            {
                LOG(DLOGL_ERROR, "%s: Error: Invalid payload received in NOTIFY_CHANGED_SRC_STATE\n", __FUNCTION__);
            }
        }
        break;
        default:
            LOG(DLOGL_ERROR, "%s: Unknown callback event %d", __FUNCTION__, event);
            break;
        }
    }

    pthread_mutex_unlock(&mrdvr_player_mutex);
}


std::string MSPHTTPSource:: cgmi_EventString(tcgmi_Event event)
{
    std::string cgmi_event_string = "UNKNOWN ERROR";
    switch (event)
    {
    case NOTIFY_STREAMING_OK:                   ///<
        cgmi_event_string = "NOTIFY_STREAMING_OK";
        break;
    case NOTIFY_FIRST_PTS_DECODED:              ///<The decoding has started for the stream and or after a seek.
        cgmi_event_string = "NOTIFY_FIRST_PTS_DECODED";
        break;
    case NOTIFY_STREAMING_NOT_OK:               ///< A streaming error has occurred.
        cgmi_event_string = "NOTIFY_STREAMING_NOT_OK";
        break;
    case NOTIFY_SEEK_DONE:                      ///< The seek has completed
        cgmi_event_string = "NOTIFY_SEEK_DONE";
        break;
    case NOTIFY_START_OF_STREAM:                ///< The Current position is now at the Start of the stream
        cgmi_event_string = "NOTIFY_START_OF_STREAM";
        break;
    case NOTIFY_END_OF_STREAM:                  ///< You are at the end of stream or EOF
        cgmi_event_string = "NOTIFY_END_OF_STREAM";
        break;
    case NOTIFY_DECRYPTION_FAILED:              ///< Not able to decrypt the stream: we don't know how to decrypt
        cgmi_event_string = "NOTIFY_DECRYPTION_FAILED";
        break;
    case NOTIFY_NO_DECRYPTION_KEY:              ///<No key has been provided to decrypt this content.
        cgmi_event_string = "NOTIFY_NO_DECRYPTION_KEY";
        break;
    case NOTIFY_VIDEO_ASPECT_RATIO_CHANGED:     ///<The straem has changed it's aspect ratio
        cgmi_event_string = "NOTIFY_VIDEO_ASPECT_RATIO_CHANGED";
        break;
    case NOTIFY_VIDEO_RESOLUTION_CHANGED:       ///<The resolution of the stream has changed.
        cgmi_event_string = "NOTIFY_VIDEO_RESOLUTION_CHANGED";
        break;
    case NOTIFY_CHANGED_LANGUAGE_AUDIO:         ///<The streams Audio language has changed
        cgmi_event_string = "NOTIFY_CHANGED_LANGUAGE_AUDIO";
        break;
    case NOTIFY_CHANGED_LANGUAGE_SUBTITLE:      ///<The subtitle language has changed.
        cgmi_event_string = "NOTIFY_CHANGED_LANGUAGE_SUBTITLE";
        break;
    case NOTIFY_CHANGED_LANGUAGE_TELETEXT:      ///<The teletext language has changed.
        cgmi_event_string = "NOTIFY_CHANGED_LANGUAGE_TELETEXT";
        break;
    case NOTIFY_MEDIAPLAYER_URL_OPEN_FAILURE:   ///<The requested URL could not be opened.
        cgmi_event_string = "NOTIFY_MEDIAPLAYER_URL_OPEN_FAILURE";
        break;
    case NOTIFY_MEDIAPLAYER_UNKNOWN:             ///<An unexpected error has occured.
        cgmi_event_string = "NOTIFY_MEDIAPLAYER_UNKNOWN";
        break;
    case NOTIFY_PSI_READY:             ///< PSI is detected, ready to decode (valid for TS content only)
        cgmi_event_string = "NOTIFY_PSI_READY";
        break;
    case NOTIFY_PMT_READY: ///< PMT section is received. The buffer will be available as tcgmi_Data in cgmi_EventCallback.
        cgmi_event_string = "NOTIFY_PMT_READY";
        break;
    case NOTIFY_COPY_CONTROL_INFO_CHANGED:
        cgmi_event_string = "NOTIFY_COPY_CONTROL_INFO_CHANGED";
        break;
    case NOTIFY_CHANGED_SRC_STATE:
        cgmi_event_string = "NOTIFY_CHANGED_SRC_STATE";
        break;
    default:
        cgmi_event_string = "UNKNOWN EVENT";
        break;
    }

    return cgmi_event_string;
}

std::string MSPHTTPSource:: cgmi_ErrorString(cgmi_Status stat)
{
    std::string cgmi_err_string = "UNKNOWN ERROR";
    switch (stat)
    {
    case CGMI_ERROR_SUCCESS:           ///<Success
        cgmi_err_string = "CGMI_ERROR_SUCCESS";
        break;

    case CGMI_ERROR_FAILED:            ///<General Error
        cgmi_err_string = "CGMI_ERROR_FAILED";
        break;

    case CGMI_ERROR_NOT_IMPLEMENTED:   ///<This feature or function has not yet been implmented
        cgmi_err_string = "CGMI_ERROR_NOT_IMPLEMENTED";
        break;

    case CGMI_ERROR_NOT_SUPPORTED:     ///<This feature or funtion is not supported
        cgmi_err_string = "CGMI_ERROR_NOT_SUPPORTED";
        break;

    case CGMI_ERROR_BAD_PARAM:         ///<One of the parameters passed in is invalid
        cgmi_err_string = "CGMI_ERROR_BAD_PARAM";
        break;

    case CGMI_ERROR_OUT_OF_MEMORY:     ///<An allocation of memory has failed.
        cgmi_err_string = "CGMI_ERROR_OUT_OF_MEMORY";
        break;

    case CGMI_ERROR_TIMEOUT:           ///<A time out on a filter has occured.
        cgmi_err_string = "CGMI_ERROR_TIMEOUT";
        break;

    case CGMI_ERROR_INVALID_HANDLE:    ///<A session handle or filter handle passed in is not correct.
        cgmi_err_string = "CGMI_ERROR_INVALID_HANDLE";
        break;

    case CGMI_ERROR_NOT_INITIALIZED:   ///<A function is being called when the system is not ready.
        cgmi_err_string = "CGMI_ERROR_NOT_INITIALIZED";
        break;

    case CGMI_ERROR_NOT_OPEN:          ///<The Interface has yet to be opened.
        cgmi_err_string = "CGMI_ERROR_NOT_OPEN";
        break;

    case CGMI_ERROR_NOT_ACTIVE:        ///<This feature is not currently active
        cgmi_err_string = "CGMI_ERROR_NOT_ACTIVE";
        break;

    case CGMI_ERROR_NOT_READY:         ///<The feature requested can not be provided at this time.
        cgmi_err_string = "CGMI_ERROR_NOT_READY";
        break;

    case CGMI_ERROR_NOT_CONNECTED:     ///<The pipeline is currently not connected for the request.
        cgmi_err_string = "CGMI_ERROR_NOT_CONNECTED";
        break;

    case CGMI_ERROR_URI_NOTFOUND:      ///<The URL passed in could not be resolved.
        cgmi_err_string = "CGMI_ERROR_URI_NOTFOUND";
        break;

    case CGMI_ERROR_WRONG_STATE:       ///<The Requested state could not be set
        cgmi_err_string = "CGMI_ERROR_WRONG_STATE";
        break;

    case CGMI_ERROR_NUM_ERRORS:         ///<Place Holder to know how many errors there are in the struct enum.
        cgmi_err_string = "CGMI_ERROR_NUM_ERRORS";
        break;

    default:
        cgmi_err_string = "UNKNOWN ERROR";
        break;
    }

    return cgmi_err_string;
}

///Getting the msp network info (channel, source id, frequency and modulation mode)
eCsciMspDiagStatus MSPHTTPSource::GetMspNetworkInfo(DiagMspNetworkInfo *msgInfo)
{
    if (msgInfo)
    {
        msgInfo->ChanNo     = mClmChannel;
        msgInfo->SourceId   = mSourceId;
        msgInfo->frequency  = mFrequencyHz;
        msgInfo->mode       = mMode;
    }
    else
    {
        LOG(DLOGL_ERROR, "Error: Null msgInfo");
        return kCsciMspDiagStat_NoData;
    }

    return kCsciMspDiagStat_OK;
}

///Building the properties of the tuned live channel
void MSPHTTPSource::buildClmChannel()
{
    int pos = -1;

    mClmChannel = 0;

    if ((pos = mSrcUrl.find("sctetv")) == 0)
    {
        std::string channelStr = mSrcUrl.substr(strlen("sctetv://"));
        if (channelStr.length())
        {
            mClmChannel = atoi(channelStr.c_str());
        }
    }
    else if ((pos = mSrcUrl.find("sappv")) == 0)
    {
        std::string channelStr = mSrcUrl.substr(strlen("sappv://"));
        if (channelStr.length())
        {
            mClmChannel = atoi(channelStr.c_str());
        }
    }
    else
    {
        //Not a live channel...
        //so no need to build the channel properties
        return;
    }

    mChannelList = CLM_GetChannelList("rf:");

    LOG(DLOGL_REALLY_NOISY, "%s: Tuning to source (%s) with channel number %d\n", __PRETTY_FUNCTION__, mSrcUrl.c_str(), mClmChannel);

    //Populate the tuning parameters
    getTuningParamsFromChannelList();
}

///Getting the properties of the tuned live channel from CLM
eMspStatus MSPHTTPSource::getTuningParamsFromChannelList()
{
    eMspStatus status = kMspStatus_Ok;  // status for return value
    time_t now;

    FNLOG(DL_MSP_MPLAYER);
    time(&now);

    // Get MPEG-TS program number - note: mChannelList and mClmChannel set in constructor
    eChannel_Status channelStatus = Channel_GetInt(mChannelList, mClmChannel, now, kMPEGTSProgramNumberInt, &mProgramNumber);
    if (channelStatus != kChannel_OK)
    {
        LOG(DLOGL_REALLY_NOISY, "error getting kMPEGTSProgramNumberInt");
    }

    // Get source ID        TODO:  move this to load since we do it for both RF and SDV source
    channelStatus = Channel_GetInt(mChannelList, mClmChannel, now, kServiceParameterInt, &mSourceId);
    if (channelStatus != kChannel_OK)
    {
        LOG(DLOGL_REALLY_NOISY, "error getting kServiceParameterInt");
    }

    // Get frequency
    channelStatus = Channel_GetInt(mChannelList, mClmChannel, now, kFrequencyInt, (int32_t *)&mFrequencyHz);
    if (channelStatus == kChannel_OK)
    {
        mFrequencyHz = mFrequencyHz * (1000 * 1000);
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "error getting kFrequencyInt");
    }

    // Get symbol rate
    channelStatus = Channel_GetInt(mChannelList, mClmChannel, now, kSymbolRateInt, (int32_t *)&mSymbolRate);
    if (channelStatus != kChannel_OK)
    {
        LOG(DLOGL_REALLY_NOISY, "error getting kSymbolRateInt");
    }

    // Get mode(modulation type) for ananlog chan
    if (mSymbolRate == 0)
    {
        channelStatus = Channel_GetInt(mChannelList, mClmChannel, now, kModulationTypeInt, (int32_t *)&mMode);
        if (channelStatus != kChannel_OK)
        {
            LOG(DLOGL_REALLY_NOISY, "error kModulationTypeInt");
        }
    }
    else
        mMode = eSrcRFMode_QAM256;

    // Get channel type for the channel (video/music/ppv/mosaic/sdv etc)
    channelStatus = Channel_GetInt(mChannelList, mClmChannel, now, kChannelType, &mChannelType);
    if (channelStatus != kChannel_OK)
    {
        LOG(DLOGL_REALLY_NOISY, "error kChannelType");
    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "%s:%d CLM Channel type returns %d", __FUNCTION__, __LINE__, mChannelType);

        mSdvSource = false;
        mMusicSource = false;
        mMosaicSource = false;

        switch (mChannelType)
        {
        case kChannelType_SDV:
            mSdvSource = true;
            break;
        case kChannelType_Music:
            mMusicSource = true;
            break;
        case kChannelType_Mosaic:
            mMosaicSource = true;
            break;
        }
    }

    return status;
}

///Returns the active streaming information
///For live returned information is <channel number, type, source and content URL>
///For  rec returned information is <source and content URL>
eCsciMspDiagStatus MSPHTTPSource::GetMspStreamingInfo(DiagMspStreamingInfo *msgInfo)
{
    int pos = -1;

    if (msgInfo)
    {
        //For recording playback no need to populate channel number and type
        if ((pos = mSrcUrl.find("mrdvr://")) == 0)
        {
            msgInfo->ChanNo     	= INVALID_CHANNEL_NUMBER;
            msgInfo->ChannelType   	= INVALID_CHANNEL_TYPE;
        }
        else
        {
            msgInfo->ChanNo     	= mClmChannel;
            msgInfo->ChannelType   	= mChannelType;
        }

        //source URL population
        strncpy(msgInfo->SourceUrl, mSrcUrl.c_str(), (strlen(mSrcUrl.c_str()) + 1));

        //http content URL population
        strncpy(msgInfo->ContentUrl, mFileName.c_str(), (strlen(mFileName.c_str()) + 1));

        LOG(DLOGL_REALLY_NOISY, "channel no  : %d\n", msgInfo->ChanNo);
        LOG(DLOGL_REALLY_NOISY, "channel type: %d\n", msgInfo->ChannelType);
        LOG(DLOGL_REALLY_NOISY, "source  url : %s\n", msgInfo->SourceUrl);
        LOG(DLOGL_REALLY_NOISY, "content url : %s\n", msgInfo->ContentUrl);
    }
    else
    {
        LOG(DLOGL_ERROR, "Error: Null msgInfo");
        return kCsciMspDiagStat_NoData;
    }

    return kCsciMspDiagStat_OK;
}


///Interface to Register the CC changed Callback with AVPM
void MSPHTTPSource::SetCcChangedCB(void *aCbData, CcChangedCB ccChangedCB)
{
    FNLOG(DL_MSP_MRDVR);

    Avpm *inst = NULL;
    inst = Avpm::getAvpmInstance();
    if (inst)
    {
        inst->SetCcChangedCB(aCbData, ccChangedCB);
    }
}


/**
 *   @param aCbData 	       - callback user data (this)
 *   @param VideoZoomCB        - pointer to the Bridge function videoZoomChangedCB
 *
 *   @return 					Nothing
 *   @brief 	This function serves twin purposes of:
 *				1. Registering the callback with AVPM -> in order to propagate the latest User Selected zoom value from avpm to MSP.
 *				2. Since called at the start of every playback,getting VideoZoomType from AVPM ensures MSP has latest ZOOM setting.
 *				Flows: zoom changed by User AND Channel Change/Startup Playback...
 *				Function to enable and disable the CC.
 */

void MSPHTTPSource::SetVideoZoomCB(void *aCbData, VideoZoomCB videoZoomCB)
{
    FNLOG(DL_MSP_MPLAYER);
    dlog(DL_MSP_MRDVR,  DLOGL_REALLY_NOISY, "%s: Registering SetVideoZoomCB", __FUNCTION__);

    Avpm *inst = NULL;
    inst = Avpm::getAvpmInstance();
    if (inst)
    {
        //Setup the callback...
        inst->SetVideoZoomCB(aCbData, videoZoomCB);

        //Retrieve the zoom setting from avpm...
        this->videoZoomType = inst->getZoomType();
    }
}
/// Returns the RAW PMT data pointer and data size to the caller
eMspStatus MSPHTTPSource:: getRawPmt(uint8_t **pPMTData, int *pPMTSize)
{
    if (pPMTData == NULL || pPMTSize == NULL)
    {
        LOG(DLOGL_ERROR, "%s: Error null input parameters passed", __FUNCTION__);
        return kMspStatus_BadParameters;
    }

    *pPMTData = mpPMTData;
    *pPMTSize = mPMTDataSize;

    return kMspStatus_Ok;
}


/**
 *   @param cbData 	       - callback user data
 *   @param ccSet          - cc enabled status (true=ON, false =OFF)
 *
 *   @return kMspStatus_Ok
 *   @brief Function to enable and disable the CC.
 */
void MSPHTTPSource::ccChangedCB(void *cbData, bool ccSet)
{
    FNLOG(DL_MSP_MRDVR);

    int error = 0;
    mrcc_Error ccError = CC_EINVAL;

    MSPHTTPSource *inst = (MSPHTTPSource *) cbData;

    if (inst == NULL)
    {
        LOG(DLOGL_ERROR, "Invalid user data\n");
        return;
    }
    else
    {
        pthread_mutex_lock(&(inst->mCgmiDataMutex));
        if (false == inst->mCgmiFirstPTSReceived)
        {
            pthread_mutex_unlock(&(inst->mCgmiDataMutex));
            LOG(DLOGL_ERROR, "CC is not valid without First PTS decoded call from cgmi");
            return;
        }
        pthread_mutex_unlock(&(inst->mCgmiDataMutex));

        if (mpSessionId)
        {
            LOG(DLOGL_REALLY_NOISY, "In ccChangedCB, ccSet=%d", ccSet);
            if (ccSet == true)
            {
                LOG(DLOGL_REALLY_NOISY, "In ccChangedCB, enabling CC");

                startUserDataFilterCallBack startCallbk = (startUserDataFilterCallBack)cgmi_startUserDataFilter;

                error = media_closeCaptionStart(mpSessionId, startCallbk); // pSessionId points the cgmi session
                if (error != 0)
                {
                    LOG(DLOGL_ERROR, "media_closeCaptionStart, error = %d", error);
                }
                else
                {
                    ccError = ccSetCCState(CCStatus_ON, 0); // Enables CC UI
                    if (ccError != CC_SUCCESS)
                    {
                        LOG(DLOGL_ERROR, "ccSetCCState ON Failed with %d", ccError);
                    }

                    ccSetTrickPlayStatus(CC_TRICK_OFF); // resetting trick status for CC
                }
            }
            else
            {
                LOG(DLOGL_REALLY_NOISY, "In ccChangedCB, disabling CC and reseting CC trick status");

                ccSetTrickPlayStatus(CC_TRICK_OFF); // resetting trick status for CC

                stopUserDataFilterCallBack stopCallbk = (stopUserDataFilterCallBack)cgmi_stopUserDataFilter;

                ccError = ccSetCCState(CCStatus_OFF, 0); // Disables CC UI

                if (ccError != CC_SUCCESS)
                {
                    LOG(DLOGL_ERROR, "ccSetCCState OFF Failed with %d", ccError);
                }

                error = media_closeCaptionStop(mpSessionId, stopCallbk);
                if (error != 0)
                {
                    LOG(DLOGL_ERROR, "media_closeCaptionStop, error = %d", error);
                }
            }
        }
        else
        {
            LOG(DLOGL_ERROR, "Invalid Session Id");
        }
    }
}

/**
 *
 *   @param cbData			 - Callback data (pointer to MSP object in this case)
 *   @param tAvpmPictureMode - Type of Zoom Selected
 *
 *   @return Nothing
 *
 *   @brief Bridge function between AVPM_IC and doer for ApplyZoom. Would be called from AVPM_IC in event of
 *			zoom changed by the user. Further, calls ApplyZoom using the callback pointer.
 *
 */
void MSPHTTPSource::videoZoomChangedCB(void *cbData, tAvpmPictureMode ZoomType)
{
    FNLOG(DL_MSP_MPLAYER);
    dlog(DL_MSP_MPLAYER,  DLOGL_REALLY_NOISY, "%s: videoZoomChangedCB Callback: zoomtype=%d", __FUNCTION__, ZoomType);
    MSPHTTPSource *inst = (MSPHTTPSource *) cbData;
    if (inst)
    {
        inst->videoZoomType = ZoomType;
        pthread_mutex_lock(&(inst->mrdvr_player_mutex));
        inst->ApplyZoom(&(MSPHTTPSource::mVidScreenRect));
        pthread_mutex_unlock(&(inst->mrdvr_player_mutex));
    }
    else
    {
        dlog(DL_MSP_MPLAYER,  DLOGL_ERROR, "%s: videoZoomChangedCB Callback: FAILED, NULL instance of MSPHTTP", __FUNCTION__);
    }
}



/**
 *
 *   @param const int 			- value for setDFC (stretch,normal)
 *   @param char* msg			- Message to be printed in exception
 *
 *   @return cgmi_Status	- Success/Failure
 *
 *   @brief Doer Function which actually applies zoom ONLY for stretch and normal mode.
 *
 */
cgmi_Status MSPHTTPSource::iterateAndSetDfc(int dfcType, char* msg)
{
    cgmi_Status stat = CGMI_ERROR_FAILED;
    try
    {
        int numDevices = device::Host::getInstance().getVideoDevices().size();
        for (int i = 0; i < numDevices; i++)
        {
            device::VideoDevice& decoder = device::Host::getInstance().getVideoDevices().at(i);
            decoder.setDFC(dfcType);
        }
        dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s applied %s", __FUNCTION__, msg);
        stat = CGMI_ERROR_SUCCESS;
    }
    catch (const device::Exception e)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s Exception occured during %s- Message: %s, Code: %d", __FUNCTION__, msg, e.getMessage().c_str() , e.getCode());
        stat = CGMI_ERROR_FAILED;
    }
    return stat;
}

/**
 *
 *   @param tAvRect			- TV Resolution Rect
 *
 *   @return cgmi_Status	- Success/Failure
 *
 *   @brief Wrapper Function which identifies the APIs to be called for selected zoom.
 *			Would be called in 2 flows - Channel Change - via set presentationparameter - to
 *			ensure the zoom value persists across reboot/Channel change.
 *			And User Changing the zoom value - to apply new zoom value. Callback via AVPM.
 *
 */
eMspStatus MSPHTTPSource::ApplyZoom(tAvRect *rect)
{
    FNLOG(DL_MSP_MPLAYER);

    dlog(DL_MSP_MPLAYER,  DLOGL_REALLY_NOISY, "%s: VALUES: ZoomType=%d, X=%d, Y=%d, Width=%d, Height=%d", __FUNCTION__, this->videoZoomType, rect->x, rect->y, rect->width, rect->height);

    cgmi_Status stat = CGMI_ERROR_FAILED;
    int x = rect->x;
    int y = rect->y;
    int width = rect->width;
    int height = rect->height;

    if (width < 1280 && height < 720)
    {
        //no zoom for video scaling - just fit the frame into rectangle...
        dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s This is video scaling for Guide - must be stretched irrespective of zoom selection", __FUNCTION__);
        stat = cgmi_SetVideoRectangle(mpSessionId, 0, 0, 1280, 720 , x, y, width, height);
        //stretch fit the stream into the rect - hence calling setdfc...
        iterateAndSetDfc(VideoDFC::kFull, "video scaling for Guide");
        dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s video scaling for Guide - setDFC applied", __FUNCTION__);
    }
    else
    {
        //zoom applicable for full screen mode...
        dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s Applying Zoom on fullscreen", __FUNCTION__);
        switch (videoZoomType)
        {
        case tAvpmPictureMode_Normal :
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s projecting normal ZOOM ", __FUNCTION__);
            stat = cgmi_SetVideoRectangle(mpSessionId, 0, 0, 1280, 720 , x, y, width, height);
            iterateAndSetDfc(VideoDFC::kPlatform, "Normal Zoom");
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s projected Normal ZOOM ", __FUNCTION__);
            break;


        case tAvpmPictureMode_Stretch :
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s projecting Stretch ZOOM ", __FUNCTION__);
            //set the rect again - needed in case of video scaling to stretch flow...
            stat = cgmi_SetVideoRectangle(mpSessionId, 0, 0, 1280, 720 , x, y, width, height);
            iterateAndSetDfc(VideoDFC::kFull, "Stretch Zoom");
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s projected Stretch ZOOM ", __FUNCTION__);
            break;


        case tAvpmPictureMode_Zoom25 :
            stat = cgmi_SetVideoRectangle(mpSessionId, mZoom25Params[0], mZoom25Params[1], mZoom25Params[2], mZoom25Params[3], 0, 0, 1280, 720);
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s projected ZOOM25 ", __FUNCTION__);
            break;

        case tAvpmPictureMode_Zoom50 :
            stat = cgmi_SetVideoRectangle(mpSessionId, mZoom50Params[0], mZoom50Params[1], mZoom50Params[2], mZoom50Params[3], 0, 0, 1280, 720);
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s projected ZOOM50 ", __FUNCTION__);
            break;

        default:
            dlog(DL_MSP_MRDVR, DLOGL_ERROR, "%s: Invalid Zoom Type, not applied!", __FUNCTION__);
            break;
        }
    }

    if (stat != CGMI_ERROR_SUCCESS)
    {
        LOG(DLOGL_ERROR, "%s:%d - %s :: cgmi_SetVideoRectangle Failed with %s \n", __FILE__, __LINE__, __FUNCTION__, (char *) cgmi_ErrorString(stat).c_str());
        return kMspStatus_Error;
    }
    else
    {
        dlog(DL_MSP_MRDVR, DLOGL_REALLY_NOISY, "%s: cgmi_SetVideoRectangle successful", __FUNCTION__);
        return kMspStatus_Ok;
    }
}




eMspStatus MSPHTTPSource::filterAppDataPid(uint32_t aPid, DataReadyCallback aDataReadyCB, void *aClientContext, bool isMusic)
{
    FNLOG(DL_MSP_MPLAYER);

    cgmi_Status retCode = CGMI_ERROR_SUCCESS;
    tcgmi_FilterData filterdata;

    uint16_t pid = (uint16_t) aPid;

    pthread_mutex_lock(&mrdvr_player_mutex);

    if (mpSessionId == NULL)
    {
        dlog(DL_MSP_MRDVR,  DLOGL_ERROR, "%s: Invalid Session Id\n", __FUNCTION__);
        pthread_mutex_unlock(&mrdvr_player_mutex);
        return kMspStatus_BadSource;
    }

    if (aPid == INVALID_PID_VALUE)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "Invalid Pid -- stop filtering on earlier PID\n");
        eMspStatus status = stopAppDataFilter();
        closeAppDataFilter();
        pthread_mutex_unlock(&mrdvr_player_mutex);
        return status;
    }
    else
    {
        pthread_mutex_lock(&mAppDataMutex);

        if (mDataReadyCB == NULL)
        {
            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "Starting Section Filter for App Data Pid %d", aPid);

            mDataReadyCB = aDataReadyCB;
            mDataReadyContext = aClientContext;

            if (mAppData != NULL)       // shouldn't happen, but clear old data if it does
            {
                delete mAppData;
                mAppData = NULL;
            }

            if (true == isMusic)
            {
                mAppData = new MusicAppData(maxApplicationDataSize);
            }
            else
            {
                mAppData = new ApplicationData(maxApplicationDataSize);
            }

            retCode = cgmi_CreateSectionFilter(mpSessionId, pid, mpSessionId, &mpFilterId);
            if (retCode != CGMI_ERROR_SUCCESS)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s:CGMI CreateSectionFilter Failed\n", __FUNCTION__);
                pthread_mutex_unlock(&mAppDataMutex);
                pthread_mutex_unlock(&mrdvr_player_mutex);
                return kMspStatus_Error;
            }

            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s: filter %p created for cgmi session %p for source %p\n", __FUNCTION__, mpFilterId, mpSessionId, this);

            filterdata.value = NULL;
            filterdata.mask = NULL;
            filterdata.length = 0;
            filterdata.comparitor = FILTER_COMP_EQUAL;

            retCode = cgmi_SetSectionFilter(mpSessionId, mpFilterId, &filterdata);
            if (retCode != CGMI_ERROR_SUCCESS)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: CGMI SetSectionFilter Failed\n", __FUNCTION__);
                pthread_mutex_unlock(&mAppDataMutex);
                pthread_mutex_unlock(&mrdvr_player_mutex);
                return kMspStatus_Error;
            }

            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s: filter %p set for cgmi session %p for source %p\n", __FUNCTION__, mpFilterId, mpSessionId, this);

            retCode = cgmi_StartSectionFilter(mpSessionId, mpFilterId, 10, 0, 0, cgmi_QueryBufferCallback, cgmi_SectionBufferCallback);
            if (retCode != CGMI_ERROR_SUCCESS)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: CGMI StartSectionFilter Failed\n", __FUNCTION__);
                pthread_mutex_unlock(&mAppDataMutex);
                pthread_mutex_unlock(&mrdvr_player_mutex);
                return kMspStatus_Error;
            }

            mbSfStarted = true;

            pthread_mutex_unlock(&mAppDataMutex);
            pthread_mutex_unlock(&mrdvr_player_mutex);

            dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s: filter %p started for cgmi session %p for source %p\n", __FUNCTION__, mpFilterId, mpSessionId, this);

            return kMspStatus_Ok;
        }
        else
        {
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "App data filter is already Set\n");
            pthread_mutex_unlock(&mAppDataMutex);
            pthread_mutex_unlock(&mrdvr_player_mutex);
            return kMspStatus_Error;
        }
    }
}

eMspStatus MSPHTTPSource::stopAppDataFilter()
{
    FNLOG(DL_MSP_MPLAYER);

    cgmi_Status retCode = CGMI_ERROR_SUCCESS;

    if (mpSessionId)
    {
        pthread_mutex_lock(&mAppDataMutex);

        if (mbSfStarted == true)
        {
            retCode = cgmi_StopSectionFilter(mpSessionId, mpFilterId);
            if (retCode != CGMI_ERROR_SUCCESS)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: CGMI StopSectionFilter Failed\n", __FUNCTION__);
            }

            mbSfStarted = false;
        }

        if (mAppData != NULL)
        {
            delete mAppData;
            mAppData = NULL;
        }

        mDataReadyCB = NULL;
        mDataReadyContext = NULL;

        pthread_mutex_unlock(&mAppDataMutex);
    }
    else
    {
        dlog(DL_MSP_MRDVR,  DLOGL_ERROR, "%s: Invalid Session Id\n", __FUNCTION__);
        return kMspStatus_BadSource;
    }

    return kMspStatus_Ok;
}

eMspStatus MSPHTTPSource::closeAppDataFilter()
{
    FNLOG(DL_MSP_MPLAYER);

    cgmi_Status retCode = CGMI_ERROR_SUCCESS;

    if (mpSessionId)
    {
        if (mpFilterId != NULL)
        {
            retCode = cgmi_DestroySectionFilter(mpSessionId, mpFilterId);
            if (retCode != CGMI_ERROR_SUCCESS)
            {
                dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s: CGMI cgmi_DestroySectionFilter Failed\n", __FUNCTION__);
            }

            mpFilterId = NULL;
        }
        else
        {
            LOG(DLOGL_NOISE, "Null mpFilterId");
        }
    }
    else
    {
        dlog(DL_MSP_MRDVR,  DLOGL_ERROR, "%s: Invalid Session Id\n", __FUNCTION__);
        return kMspStatus_BadSource;
    }

    return kMspStatus_Ok;
}

eMspStatus MSPHTTPSource::getApplicationData(uint32_t bufferSize, uint8_t *buffer, uint32_t *dataSize)
{
    FNLOG(DL_MSP_MPLAYER);

    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "getApplicationData\n");

    eMspStatus status = kMspStatus_Ok;

    if ((dataSize == NULL) || (mAppData == NULL))
    {
        status = kMspStatus_BadParameters;
    }
    else if ((bufferSize == 0) || (buffer == NULL)) // just requesting size with this call
    {
        *dataSize = mAppData->getTotalSize();
    }
    else
    {
        *dataSize = mAppData->getData(buffer, bufferSize);
    }

    return status;
}

/****************************  Section Filter Callbacks  *******************************/
cgmi_Status MSPHTTPSource::cgmi_QueryBufferCallback(void *pUserData, void *pFilterPriv, void *pFilterId, char **ppBuffer, int *pBufferSize)
{
    FNLOG(DL_MSP_MPLAYER);

    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "cgmi_QueryBufferCallback requested buffer of size %d \n", *pBufferSize);

    MSPHTTPSource *inst = (MSPHTTPSource *) pUserData;
    if (inst == NULL)
    {
        LOG(DLOGL_ERROR, "Invalid user data\n");
        return CGMI_ERROR_BAD_PARAM;
    }

    if (pUserData != inst || pFilterPriv != inst->mpSessionId || pFilterId != inst->mpFilterId)
    {
        LOG(DLOGL_ERROR, "Invalid user data returned from CGMI\n");
        LOG(DLOGL_ERROR, "Received: pUserData = %p pFilterPriv = %p pFilterId = %p\n", pUserData, pFilterPriv, pFilterId);
        LOG(DLOGL_ERROR, "Expected: pUserData = %p pFilterPriv = %p pFilterId = %p\n", inst, inst->mpSessionId, inst->mpFilterId);
        return CGMI_ERROR_BAD_PARAM;
    }

    // Check if a size of greater than zero was provided, use default if not
    if (*pBufferSize <= 0)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "Making the size to Minimum \n");
        *pBufferSize = 256;
    }

    // Allocate buffer of size *pBufferSize
    *ppBuffer = (char *) malloc(*pBufferSize);
    if (NULL == *ppBuffer)
    {
        *pBufferSize = 0;
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "Memory allocation failed...\n");
        return CGMI_ERROR_OUT_OF_MEMORY;
    }

    return CGMI_ERROR_SUCCESS;
}

cgmi_Status MSPHTTPSource::cgmi_SectionBufferCallback(void *pUserData, void *pFilterPriv, void *pFilterId, cgmi_Status sectionStatus, char *pSection, int sectionSize)
{
    FNLOG(DL_MSP_MPLAYER);

    (void) sectionStatus;

    MSPHTTPSource *inst = (MSPHTTPSource *) pUserData;
    if (inst == NULL)
    {
        LOG(DLOGL_ERROR, "Invalid user data\n");
        return CGMI_ERROR_BAD_PARAM;
    }

    if (NULL == pSection)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "NULL section passed to cgmiSectionBufferCallback.\n");
        return CGMI_ERROR_BAD_PARAM;
    }

    if (pUserData != inst || pFilterPriv != inst->mpSessionId || pFilterId != inst->mpFilterId)
    {
        LOG(DLOGL_ERROR, "Invalid user data returned from CGMI\n");
        LOG(DLOGL_ERROR, "Received: pUserData = %p pFilterPriv = %p pFilterId = %p\n", pUserData, pFilterPriv, pFilterId);
        LOG(DLOGL_ERROR, "Expected: pUserData = %p pFilterPriv = %p pFilterId = %p\n", inst, inst->mpSessionId, inst->mpFilterId);
        return CGMI_ERROR_BAD_PARAM;
    }

    dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "Received section pFilterId: %p, data: %p, sectionSize: %d\n\n", pFilterId, pSection, sectionSize);

    pthread_mutex_lock(&(inst->mAppDataMutex));

    if (sectionSize != 0 && inst->mbSfStarted == true)
    {
        int databytes = 0;

        if (inst->mAppData)
        {
            databytes = inst->mAppData->addData((uint8_t *)pSection, sectionSize);
        }

        dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "addData data bytes = %d to %p of %p", databytes, inst->mDataReadyCB, inst->mDataReadyContext);

        if ((databytes) && (inst->mDataReadyCB))
        {
            inst->mDataReadyCB(inst->mDataReadyContext);
        }

        if (pSection)
        {
            free(pSection);
            pSection = NULL;
        }
    }

    pthread_mutex_unlock(&(inst->mAppDataMutex));

    return CGMI_ERROR_SUCCESS;
}

/* Get method to retrieve cgmi session handle */
void* MSPHTTPSource::getCgmiSessHandle()
{
    return mpSessionId;
}

bool MSPHTTPSource::isMosaic(void)
{
    return mMosaicSource;
}

//Restarts the audio for the session
void MSPHTTPSource::RestartAudio(void)
{
    FNLOG(DL_MSP_MPLAYER);

    pthread_mutex_lock(&mrdvr_player_mutex);

    if (mpSessionId)
    {
        if (mAudioPid == INVALID_PID_VALUE)
        {
            LOG(DLOGL_ERROR, "%s: Yet to set Audio Pid. Cannot restart Audio now", __FUNCTION__);
        }
        else
        {
            cgmi_Status stat = CGMI_ERROR_SUCCESS;
            stat = cgmi_SetPid(mpSessionId, mAudioPid, STREAM_TYPE_AUDIO, true);
            if (stat != CGMI_ERROR_SUCCESS)
            {
                LOG(DLOGL_ERROR, "%s:%d - %s :: cgmi_SetPid Failed with %s \n", __FILE__, __LINE__, __FUNCTION__, (char *) cgmi_ErrorString(stat).c_str());
            }
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "%s:Invalid Cgmi SessionId", __FUNCTION__);
    }

    pthread_mutex_unlock(&mrdvr_player_mutex);
}

//Stops the Audio alone for the session
void MSPHTTPSource::StopAudio(void)
{
    FNLOG(DL_MSP_MPLAYER);

    pthread_mutex_lock(&mrdvr_player_mutex);

    if (mpSessionId)
    {
        if (mAudioPid == INVALID_PID_VALUE)
        {
            LOG(DLOGL_ERROR, "%s: Yet to set Audio Pid. Cannot stop audio now", __FUNCTION__);
        }
        else
        {
            cgmi_Status stat = CGMI_ERROR_SUCCESS;
            stat = cgmi_SetPid(mpSessionId, mAudioPid, STREAM_TYPE_AUDIO, false);
            if (stat != CGMI_ERROR_SUCCESS)
            {
                LOG(DLOGL_ERROR, "%s:%d - %s :: cgmi_SetPid Failed with %s \n", __FILE__, __LINE__, __FUNCTION__, (char *) cgmi_ErrorString(stat).c_str());
            }
        }
    }
    else
    {
        LOG(DLOGL_ERROR, "%s:Invalid Cgmi SessionId", __FUNCTION__);
    }

    pthread_mutex_unlock(&mrdvr_player_mutex);
}

/* Method selects the CC language components and update CC Lang stream map for the media player session */
eMspStatus MSPHTTPSource::formulateCCLanguageList(Psi *psi)
{
    FNLOG(DL_MSP_MPLAYER);

    eMspStatus status = kMspStatus_Ok;
    tMpegDesc ccDescr;

    Avpm *inst = Avpm::getAvpmInstance();

    status = getCCServiceDescriptor(psi, &ccDescr);
    if (status == kMspStatus_Ok && ccDescr.dataLen >= 3)
    {
        inst->updateCCLangStreamMap(&ccDescr);
    }
    else
    {
        dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s :::: Language Descriptor length %d\n", __func__, ccDescr.dataLen);
    }
    psi->getPmtObj()->releaseDescriptor(&ccDescr);

    return status;
}

/*Method to parse psi pmt pids to collect CC Lang descriptors*/
eMspStatus MSPHTTPSource::getCCServiceDescriptor(Psi* psi, tMpegDesc * pCcDescr)
{
    FNLOG(DL_MSP_MPLAYER);
    eMspStatus status = kMspStatus_Ok;
    tPid videoPidStruct = {0, 0};

    if (pCcDescr == NULL)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s(%d): pCcDescr is NULL \n", __FUNCTION__, __LINE__);
        return kMspStatus_Error;
    }

    Pmt* pmtObj = psi->getPmtObj();
    if (!pmtObj)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s(%d): pmt obj is NULL \n", __FUNCTION__, __LINE__);
        return kMspStatus_Error;
    }

    std::list<tPid>* videoList = pmtObj->getVideoPidList();
    if (!videoList)
    {
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s(%d): VideoList is NULL \n", __FUNCTION__, __LINE__);
        return kMspStatus_Error;
    }

    int n = videoList->size();
    pCcDescr->tag = CAPTION_SERVICE_DESCR_TAG;
    pCcDescr->dataLen = 0;
    pCcDescr->data = NULL;


    if (n == 0)
    {
        LOG(DLOGL_ERROR, "%s: No video pid", __func__);
        status = pmtObj->getDescriptor(pCcDescr, kPid);
        dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s(%d): KPid: has CC ccDescr.dataLen %d \n", __FUNCTION__, __LINE__, pCcDescr->dataLen);

    }
    else
    {
        LOG(DLOGL_REALLY_NOISY, "%s :::: number of video pid in the list is %d", __func__, n);
        std::list<tPid>::iterator iter = videoList->begin();
        //Choose the first one, don't know what to do if we have more than one video pid
        videoPidStruct = (tPid) * iter;

        eMspStatus status = pmtObj->getDescriptor(pCcDescr, videoPidStruct.pid);

        dlog(DL_MSP_MPLAYER, DLOGL_REALLY_NOISY, "%s(%d): has CC ccDescr.dataLen %d ", __FUNCTION__, __LINE__, pCcDescr->dataLen);
        if (status != kMspStatus_Ok || pCcDescr->dataLen <= 3)
        {
            //No descriptor found with Video PID. Try 0x1fff
            pmtObj->releaseDescriptor(pCcDescr);
            pCcDescr->dataLen = 0;
            pCcDescr->data = NULL;
            status = pmtObj->getDescriptor(pCcDescr, kPid);
            dlog(DL_MSP_MPLAYER, DLOGL_ERROR, "%s(%d): KPid: has CC ccDescr.dataLen %d \n", __FUNCTION__, __LINE__, pCcDescr->dataLen);
        }

        if (status == kMspStatus_Ok && pCcDescr->dataLen < 3)
        {
            status = kMspStatus_Error;
        }
    }
    return status;
}


/******************************************************************************
 * Returns the CCI byte value
 *****************************************************************************/
uint8_t MSPHTTPSource::GetCciBits(void)
{
    return mCci;
}
