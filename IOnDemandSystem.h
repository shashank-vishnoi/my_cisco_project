/** @file IOnDemandSystem.h
 *
 * @brief OnDemand System network topology interface header
 *
 * @author Amit Patel
 *
 * @version 1.0
 *
 * @date 12.27.2010
 *
 * \b Copyright: Scientific Atlanta, Inc., A Cisco Company
 */

#ifndef _I_ONDEMAND_SYSTEM_H
#define _I_ONDEMAND_SYSTEM_H

#include <time.h>
#include <string>
#include "vod.h"
#include "csci-sgdm-api.h"

using namespace std;

#define MAC_ID_SIZE  6

class OnDemandSystemClient
{
public:
    eIOnDemandStatus GetSrmIpAddress(uint32_t *srmIpAdd);
    uint32_t GetVodServerIpAddress();
    eIOnDemandStatus GetSrmPort(uint16_t *port);
    eIOnDemandStatus GetmessageTimer(uint32_t *mTimer);
    eIOnDemandStatus GetSessionInProgressTimer(uint32_t *sProgTimer);
    eIOnDemandStatus GetMessageRetryCount(uint8_t *retryCount);
    eIOnDemandStatus GetSessionIdAssignor(uint8_t *sessIdAssignor);
    eIOnDemandStatus GetResourceIdAssignor(uint8_t *resIdAssignor);
    eIOnDemandStatus GetMaxForwardCount(uint8_t *maxForwardCount);
    eIOnDemandStatus GetStbMacAddressAddress(unsigned char *stbMacAddr);

    uint32_t getSgId();

    /**
     * \return pointer to OnDemandSystemClient Class.
     * \brief This function is used to get OnDemandSystemClient instance.
     */
    static OnDemandSystemClient * getInstance();

    virtual ~OnDemandSystemClient();
private:
    OnDemandSystemClient();

    eIOnDemandStatus ReadOnDemandNetworkTopology();

    eIOnDemandStatus GetLocalConfigServerAddresses();
    eIOnDemandStatus GetUnConfigServerAddresses();
    string GetIpAddressFromDNS(char *hostName);
    bool IsSessionControlServerAddressChanged(string address);
    eIOnDemandStatus initSgdmDiscovery();
    static void ProcessSgIdCallBack(eSgdmDiscoveryState state);

    /**
     * static Pointer to DiscoveryAdapter class.
     */
    static OnDemandSystemClient *instance;

    string sessionControlIpAddress; //ie. IP_Address:Port#

    uint32_t srmIpAddress; //ie. IP_Address:Port#
    uint32_t serverIpAddress; //ie. IP_Address:Port#
    uint16_t srmPort; //ie. IP_Address:Port#
    uint32_t messageTimer;
    uint32_t sessionInProgressTimer;
    uint8_t messageRetryCount;
    uint8_t sessionIdAssignor;
    uint8_t resourceIdAssignor;
    uint8_t maximumForwardCount;
    static uint32_t mSgId;

    uint8_t stbMacAddress[MAC_ID_SIZE];

};

#endif
