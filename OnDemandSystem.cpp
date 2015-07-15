/** @file OnDemandSystem.cpp
 *
 * @brief OnDemand Network System topology implementation.
 *
 * @author Amit Patel
 *
 * @version 1.0
 *
 * @date 12.27.2010
 *
 * Copyright: Scientific Atlanta, Inc., A Cisco Company
 */

#include <ctime>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

//#include "../../sldk_include/conflictapi.h"


#include "IOnDemandSystem.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <dns_resolve.h>

//#include <dnslookup.h>
#include <dlog.h>
#include <sess_client_data.h>
#include <cpe_networking_un.h>
#include <cpe_networking_main.h>
#include "tini.h"
#include "nps.h"

#define _LINUX_BUILD_

#include <cm_appl.h>
#include <Cam.h>


#ifdef LOG
#error  LOG already defined
#endif
#define LOG(level, msg, args...)  dlog(DL_MSP_ONDEMAND, level,"OnDemandSystem:%s:%d " msg, __FUNCTION__, __LINE__, ##args);

#define LOCAL_CONFIG_SERVERS_PATH      "/home/hd/opt/valinor/standalone"
#define NAVIGATION_SERVER_PORT_NUMBER  "80"
#define SRM_SERVER_PORT_NUMBER         "13819"

//#define LONG_MAX  ((long)(~0UL>>1))
//#define LONG_MIN  (-LONG_MAX - 1)
#define ERANGE      34  /* Math result not representable */
#define MAX_SERVERS 3
#define MAX_LINE 254
#define EXPECTEDTAGS 8
//This data is based on the User-to-Network Configuration document revision 3.6.1
#define HEADEND_NAVIGATION_SERVER_CISCO         1
#define HEADEND_NAVIGATION_SERVER_SEACHANGE     2
#define HEADEND_NAVIGATION_SERVER_TTV        3

#define HEADEND_VOD_STREAMER_CISCO           1
#define HEADEND_VOD_STREAMER_SEACHANGE       2

#define HEADEND_SRM_SA_USRM                  1
#define  HEADEND_SRM_TTV_SRM                 2

uint32_t OnDemandSystemClient::mSgId = 0;
OnDemandSystemClient *OnDemandSystemClient::instance = NULL;

OnDemandSystemClient * OnDemandSystemClient::getInstance()
{
    if (instance == NULL)
    {
        instance = new OnDemandSystemClient();
    }
    return instance;
}

eIOnDemandStatus OnDemandSystemClient::GetSrmIpAddress(uint32_t *srmIpAdd)
{
    eIOnDemandStatus status = ON_DEMAND_ERROR;
    dlog(DL_MSP_ONDEMAND, DLOGL_NORMAL, " GetSrmIpAddress :%x  ", srmIpAddress);
    if (srmIpAddress)
    {
        *srmIpAdd = srmIpAddress;
        status = ON_DEMAND_OK;
    }
    return status;
}

eIOnDemandStatus OnDemandSystemClient::GetSrmPort(uint16_t *port)
{
    eIOnDemandStatus status = ON_DEMAND_ERROR;
    dlog(DL_MSP_ONDEMAND, DLOGL_NORMAL, " GetSrmPort :%x", srmPort);
    if (srmPort)
    {
        *port = srmPort;
        status = ON_DEMAND_OK;
    }
    return status;
}
eIOnDemandStatus OnDemandSystemClient::GetmessageTimer(uint32_t *msgTimer)
{
    eIOnDemandStatus status = ON_DEMAND_ERROR;
    dlog(DL_MSP_ONDEMAND, DLOGL_NORMAL, " GetmessageTimer :%x", messageTimer);
    if (messageTimer)
    {
        *msgTimer = messageTimer / 1000;
        status = ON_DEMAND_OK;
    }
    return status;
}
eIOnDemandStatus OnDemandSystemClient::GetSessionInProgressTimer(uint32_t *sipTimer)
{
    eIOnDemandStatus status = ON_DEMAND_ERROR;
    dlog(DL_MSP_ONDEMAND, DLOGL_NORMAL, " GetSessionInProgressTimer :%x", sessionInProgressTimer);
    if (sessionInProgressTimer)
    {
        *sipTimer = sessionInProgressTimer / 1000;
        status = ON_DEMAND_OK;
    }
    return status;
}
eIOnDemandStatus OnDemandSystemClient::GetMessageRetryCount(uint8_t *retryCount)
{
    eIOnDemandStatus status = ON_DEMAND_ERROR;
    dlog(DL_MSP_ONDEMAND, DLOGL_NORMAL, " GetMessageRetryCount :%x", messageRetryCount);
    if (messageRetryCount)
    {
        *retryCount = messageRetryCount;
        status = ON_DEMAND_OK;
    }
    return status;
}
eIOnDemandStatus OnDemandSystemClient::GetSessionIdAssignor(uint8_t *sessIdAssignor)
{
    eIOnDemandStatus status = ON_DEMAND_ERROR;
    if (sessionIdAssignor)
    {
        *sessIdAssignor = sessionIdAssignor;
        status = ON_DEMAND_OK;
    }
    return status;
}
eIOnDemandStatus OnDemandSystemClient::GetResourceIdAssignor(uint8_t *resIdAssignor)
{
    eIOnDemandStatus status = ON_DEMAND_ERROR;
    if (resourceIdAssignor)
    {
        *resIdAssignor = resourceIdAssignor;
        status = ON_DEMAND_OK;
    }
    return status;
}
eIOnDemandStatus OnDemandSystemClient::GetMaxForwardCount(uint8_t *maxForwardCount)
{
    eIOnDemandStatus status = ON_DEMAND_ERROR;
    dlog(DL_MSP_ONDEMAND, DLOGL_NORMAL, " GetMaxForwardCount :%x", maximumForwardCount);
    if (maximumForwardCount)
    {
        *maxForwardCount = maximumForwardCount;
        status = ON_DEMAND_OK;
    }
    return status;
}

eIOnDemandStatus OnDemandSystemClient::GetStbMacAddressAddress(unsigned char *stbMacAddr)
{
    eIOnDemandStatus status = ON_DEMAND_ERROR;
    if (stbMacAddr != NULL)
    {
        memcpy(stbMacAddr, stbMacAddress, MAC_ID_SIZE);
        status = ON_DEMAND_OK;
    }
    return status;
}

bool OnDemandSystemClient::IsSessionControlServerAddressChanged(string address)
{
    bool status = true;
    address = address;
    /*  if (!sessionControlServerAddress.empty())
      {
          StringUtility util;
          string sessionControlAddressIp = util.GetSubStringStartingFromBeginning(sessionControlServerAddress,":");
          dlog(DL_MSP_ONDEMAND,DLOGL_SIGNIFICANT_EVENT,"%s:%i sessionControlAddressIp %s!!!",__FUNCTION__, __LINE__,
               sessionControlAddressIp.c_str());        status = (sessionControlAddressIp != address);
      }*/

    return status;

}

string OnDemandSystemClient::GetIpAddressFromDNS(char *hostname)
{
    bool gotServerIp = false;
    tIpAddress *servers[MAX_SERVERS];
    int numServer, i;
    string returnString = "localhost";
    (void)hostname;
    for (i = 0; i < MAX_SERVERS; i++)
    {
        servers[i] = NULL;
    }
    //TODO: needs to uncomment later
    //numServer = dns_resolve(hostname, servers);
    numServer = 1;
    if (numServer > 0)
    {
        for (i = 0; i < numServer; i++)
        {
            if (servers[i])
            {
                if (servers[i]->sa_family == AF_INET)
                {
                    if (!gotServerIp)
                    {
                        struct in_addr ip4Addr;
                        ip4Addr.s_addr = servers[i]->ip.sin_addr.s_addr;
                        returnString = inet_ntoa(ip4Addr);
                        //We should save the first IP address, and ignore the rest.
                        gotServerIp = true;
                    }
                }
                else
                {
                    dlog(DL_MSP_ONDEMAND, DLOGL_ERROR, "%s:%d: Error not supporting (%d) AF_INET6", __FUNCTION__, __LINE__,
                         servers[i]->sa_family);
                }
                free(servers[i]);
                servers[i] = NULL;
            }
        }
    }

    return returnString;
}
eIOnDemandStatus OnDemandSystemClient::GetUnConfigServerAddresses(void)
{
    eIOnDemandStatus status = ON_DEMAND_ERROR;
    const char *str;
    tini_handle_t *nps_tini_handle = NULL;
    tini_error_t ts = TINI_OK;

    initSgdmDiscovery();

    do
    {
        nps_tini_handle = tini_create(NPS_CONFIG_FILE_NAME, &ts);
        if (ts != TINI_OK)
        {
            sleep(1);
        }
    }
    while (ts != TINI_OK);


    str = tini_get_var(nps_tini_handle, "unsess", "port");
    if (str)
    {
        srmPort = strtoul(str, 0, 0);
    }
    else
    {
        srmPort = 13819;
    }

    str = tini_get_var(nps_tini_handle, "unsess", "ipaddr");
    if (str)
    {
        srmIpAddress = strtoul(str, 0, 0);
    }
    else
    {
        srmIpAddress = 0;
    }

    str = tini_get_var(nps_tini_handle, "dsmccconfig", "msg_timer");
    if (str)
    {
        messageTimer = strtoul(str, 0, 0);
    }
    else
    {
        messageTimer = 1;
    }

    str = tini_get_var(nps_tini_handle, "dsmccconfig", "sess_prog_timer");
    if (str)
    {
        sessionInProgressTimer = strtoul(str, 0, 0);
    }
    else
    {
        sessionInProgressTimer = 1;
    }

    str = tini_get_var(nps_tini_handle, "dsmccconfig", "retrycount");
    if (str)
    {
        messageRetryCount = strtoul(str, 0, 0);
    }
    else
    {
        messageRetryCount = 1;
    }

    str = tini_get_var(nps_tini_handle, "dsmccconfig", "sess_id_assign");
    if (str)
    {
        sessionIdAssignor = strtoul(str, 0, 0);
    }
    else
    {
        sessionIdAssignor = 1;
    }

    str = tini_get_var(nps_tini_handle, "dsmccconfig", "res_id_assign");
    if (str)
    {
        resourceIdAssignor = strtoul(str, 0, 0);
    }
    else
    {
        resourceIdAssignor = 1;
    }

    str = tini_get_var(nps_tini_handle, "dsmccconfig", "max_fwd_count");
    if (str)
    {
        maximumForwardCount = strtoul(str, 0, 0);
    }
    else
    {
        maximumForwardCount = 1;
    }
    tini_clear(nps_tini_handle);

    // HOST MAC address. This works for M-Card or RF
    Cam *pCam = Cam::getInstance();

    int ret = pCam->getCaAuthorizationMac(stbMacAddress);

    if (ret < 0)
    {
        LOG(DLOGL_NORMAL, "Error getting mac address ret: %d", ret);
    }
    else
    {
        LOG(DLOGL_NORMAL, "stb authorization mac address: %02x:%02x:%02x:%02x:%02x:%02x\n", stbMacAddress[0], stbMacAddress[1], stbMacAddress[2],
            stbMacAddress[3], stbMacAddress[4], stbMacAddress[5]);
    }

    status = ON_DEMAND_OK;
    return status;
}

eIOnDemandStatus OnDemandSystemClient::ReadOnDemandNetworkTopology(void)
{
    eIOnDemandStatus status = ON_DEMAND_ERROR;
    char standalone[100];
    sprintf(standalone, "%s", LOCAL_CONFIG_SERVERS_PATH);

    if (access(standalone, F_OK) >= 0)
    {
        //We have standalone setup
        status = GetLocalConfigServerAddresses();
    }
    else
    {
        //We need to get it from the Unconfig
        status = GetUnConfigServerAddresses();
    }

    return status;
}
eIOnDemandStatus OnDemandSystemClient::GetLocalConfigServerAddresses(void)
{
    char dataFileName[200];
    char line[MAX_LINE];
    char tag[MAX_LINE], value[MAX_LINE];
    int count = 0;
    eIOnDemandStatus status = ON_DEMAND_ERROR;

    FILE  *fp;    // File descriptor
    sprintf(dataFileName, "%s/%s", "/rtn", "odConfig.txt");

    if ((fp =  fopen(dataFileName, "r")) != (FILE *)NULL)
    {

        // First get number of valid lines in file to determine how many Tbl_ServiceParam structs to allocate
        while (fgets(line, MAX_LINE, fp) != NULL)
        {
            if (line[0] == '#')
            {
                continue;
            }

            if (sscanf(line, "%s = %s", tag, value) == 2)
            {
                string tagString(tag);
                string valueString(value);
                string SrmIpAddress = "SrmIpAddr";
                string SrmPort = "SrmPort";
                string MessageTimer = "msgTimer";
                string SipTimer = "sipTimer";
                string RetryCount = "retryCount";
                string SessIdAssigner = "sessIdAssign";
                string ResIdAssigner = "resIdAssign";
                string MaxFwdCount = "maxFwdCount";


                if (tagString == SrmIpAddress)
                {
                    srmIpAddress = strtoul(value, 0, 0);
                    dlog(DL_MSP_ONDEMAND, DLOGL_NORMAL, " srmIpAddress:%x", srmIpAddress);
                }
                else if (tagString == SrmPort)
                {
                    srmPort = strtoul(value, 0, 0);
                    dlog(DL_MSP_ONDEMAND, DLOGL_NORMAL, "  srmPort:%x", srmPort);
                }
                else if (tagString == MessageTimer)
                {
                    messageTimer = strtoul(value, 0, 0);
                    dlog(DL_MSP_ONDEMAND, DLOGL_NORMAL, "  messageTimer:%x", messageTimer);
                }
                else if (tagString == SipTimer)
                {
                    sessionInProgressTimer = strtoul(value, 0, 0);
                    dlog(DL_MSP_ONDEMAND, DLOGL_NORMAL, "  sessionInProgressTimer:%x", sessionInProgressTimer);
                }
                else if (tagString == RetryCount)
                {
                    messageRetryCount = strtoul(value, 0, 0);
                    dlog(DL_MSP_ONDEMAND, DLOGL_NORMAL, "  messageRetryCount:%x", messageRetryCount);
                }
                else if (tagString == SessIdAssigner)
                {
                    sessionIdAssignor = strtoul(value, 0, 0);
                    dlog(DL_MSP_ONDEMAND, DLOGL_NORMAL, "  sessionIdAssignor:%x", sessionIdAssignor);
                }
                else if (tagString == ResIdAssigner)
                {
                    resourceIdAssignor = strtoul(value, 0, 0);
                    dlog(DL_MSP_ONDEMAND, DLOGL_NORMAL, "  resourceIdAssignor:%x", resourceIdAssignor);
                }
                else if (tagString == MaxFwdCount)
                {
                    maximumForwardCount = strtoul(value, 0, 0);
                    dlog(DL_MSP_ONDEMAND, DLOGL_NORMAL, "   maximumForwardCount:%x", maximumForwardCount);
                }
                count++;
            }
        }

        fclose(fp);
        fp = NULL;
    }


    if (count == EXPECTEDTAGS)
    {
        status = ON_DEMAND_OK;
    }

    return status;
}

///! A function to discover the SG Id
eIOnDemandStatus OnDemandSystemClient::initSgdmDiscovery()
{
    int32_t connectionId = -1;
    eSgdmDiscoveryState state;
    uint32_t sgId;

    if ((Csci_Sgdm_GetGroupId(&sgId, &state)) == kSgdm_Ok)
    {
        if (state == kSgdm_Known)
        {
            mSgId = sgId;

            LOG(DLOGL_SIGNIFICANT_EVENT, "mSgId: %d", mSgId);
        }
    }

    if (!mSgId)
    {
        dlog(DL_MSP_ONDEMAND, DLOGL_SIGNIFICANT_EVENT, "warning: service group id not set yet state: %d", state);
    }

    // register call back
    eSgdmStatus retVal = Csci_Sgdm_RegisterCallback(OnDemandSystemClient::ProcessSgIdCallBack,
                         true, &connectionId);

    if (retVal != kSgdm_Ok)
    {
        dlog(DL_MSP_ONDEMAND, DLOGL_ERROR, "Sgdm_RegisterCallback error %d", retVal);
        return ON_DEMAND_ERROR;
    }


    return ON_DEMAND_OK;
}

/// being changed to Known
void OnDemandSystemClient::ProcessSgIdCallBack(eSgdmDiscoveryState state)
{
    LOG(DLOGL_FUNCTION_CALLS, "state: %d", state);

    if (state == kSgdm_Known)
    {
        uint32_t sgId = 0;
        eSgdmStatus sgdmStatus = Csci_Sgdm_GetGroupId(&sgId, &state);

        if (sgdmStatus == kSgdm_Ok)
        {
            mSgId = sgId;
            LOG(DLOGL_NORMAL, "Sgdm_GetGroupId mSgId: %d", mSgId);
        }
        else
        {
            LOG(DLOGL_ERROR, "Sgdm_GetGroupId error: %d", sgdmStatus);
        }
    }
    else
    {
        mSgId = 0;
        LOG(DLOGL_SIGNIFICANT_EVENT, "warning state: %d (SGID not known - set to zero)", state);
    }
}

uint32_t OnDemandSystemClient::getSgId()
{
    return mSgId;
}

OnDemandSystemClient::OnDemandSystemClient()
{
    mSgId = 0;
    serverIpAddress = 0;
    ReadOnDemandNetworkTopology();
}

OnDemandSystemClient::~OnDemandSystemClient()
{
}
