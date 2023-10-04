/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
 * following copyright and licenses apply:
 *
 * Copyright 2015 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/
/**********************************************************************
   Copyright [2014] [Cisco Systems, Inc.]
 
   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at
 
       http://www.apache.org/licenses/LICENSE-2.0
 
   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
**********************************************************************/

/**********************************************************************

    module: ccsp_management_server_pa_api.c

        For CCSP management server component

    ---------------------------------------------------------------

    description:

        This file implements all api functions of CCSP management server 
        component called by PA.
        Since PA and management server component shares one process, 
        these api functions are different to component public apis.

    ---------------------------------------------------------------

    environment:

        platform independent

    ---------------------------------------------------------------

    author:

        Hui Ma 
        Kang Quan

    ---------------------------------------------------------------

    revision:

        06/15/2011    initial revision.

**********************************************************************/
#include "ansc_platform.h"
//#include "ccsp_base_api.h"
#include "string.h"
#include "stdio.h"
#include <assert.h>
#include "ccsp_tr069pa_psm_keys.h"
#include "ccsp_tr069pa_wrapper_api.h"
#include "ccsp_management_server.h"
#include "ccsp_management_server_pa_api.h"
#include "ccsp_supported_data_model.h"
#include "ccsp_psm_helper.h"
#include "ccsp_cwmp_cpeco_interface.h"
#include "ccsp_cwmp_helper_api.h"
#include "ccsp_cwmp_ifo_sta.h"
#include "ccsp_cwmp_proco_interface.h"
#include "Tr69_Tlv.h"
#include "syscfg/syscfg.h"
#include <sys/stat.h>

#ifdef _ANSC_USE_OPENSSL_
#include <openssl/ssl.h>
#include "linux/user_openssl.h"
#endif /* _ANSC_USE_OPENSSL_ */

#define TR69_TLVDATA_FILE "/nvram/TLVData.bin"
#define ETHWAN_FILE     "/nvram/ETHWAN_ENABLE"
#include "secure_wrapper.h"

#define MAX_UDP_VAL  257
#define MAX_BUF_SIZE 256
// TELEMETRY 2.0 //RDKB-25996
#include <telemetry_busmessage_sender.h>

#define DHCP_ACS_URL_FILE "/var/tmp/acs-url-from-dhcp-option.txt"
#define DHCP_V6_ACS_URL_FILE "/var/tmp/acs-url-from-dhcp-option-v6.txt"
#define TR69_ENABLE_CWMP_VALUE "eRT.com.cisco.spvtg.ccsp.tr069pa.Device.ManagementServer.EnableCWMP.Value"

#define SLEEP_PERIOD 500000
#define DATE_TIME_PARAM_NUMBER 6
enum rebootType_e
{
    NONE = 0,
    SCHEDULE_REBOOT,
    DELAY_REBOOT
};
static enum rebootType_e rebootType = NONE;
static ULONG delayRebootTime = 0;
static pthread_t delayRebootThreadPID = 0;

//Intel Proposed RDKB Generic Bug Fix from XB6 SDK
#define NO_OF_RETRY 90                 /* No of times the management server will wait before giving up*/

#define MAX_URL_LEN 1024 
#define HTTP_STR "http://"
#define HTTPS_STR "https://"

PFN_CCSPMS_VALUECHANGE  CcspManagementServer_ValueChangeCB;
CCSP_HANDLE             CcspManagementServer_cbContext;
CCSP_HANDLE             CcspManagementServer_cpeContext;
extern char             *CcspManagementServer_ComponentName;
extern char             *CcspManagementServer_SubsystemPrefix;
extern msObjectInfo     *objectInfo;
extern void             *bus_handle;
extern char             *pPAMComponentName;
extern char             *pPAMComponentPath;
extern ULONG            g_ulAllocatedSizePeak;
extern ULONG            g_ulAllocatedSizeCurr;
extern INT              g_iTraceLevel;
extern BOOL             RegisterWanInterfaceDone;
extern char             *pFirstUpstreamIpAddress;

extern char             *g_Tr069PaAcsDefAddr;
extern char             *_SupportedDataModelConfigFile;
static CCSP_BOOL        s_MS_Init_Done  = FALSE;

extern void waitUntilSystemReady(   void*   cbContext);

#ifdef DHCP_PROV_ENABLE
#include "sysevent/sysevent.h"
#define MAX_URL_LENGTH 256
#define DHCP_ACS_URL "/tmp/dhcp_acs_url"
static pthread_t ethwanURLThread = 0;
extern token_t se_token;
extern int se_fd;
#endif

/* CcspManagementServer_Init is called by PA to register component and
 * load configuration file.
 * Return value - none.
 */

extern CCSP_VOID 
CcspManagementServer_InitCustom
    (
        CCSP_STRING             ComponentName,
        CCSP_STRING             SubsystemPrefix,
        CCSP_HANDLE             hMBusHandle,
        PFN_CCSPMS_VALUECHANGE	msValueChangeCB,
        PFN_CCSP_SYSREADYSIG_CB sysReadySignalCB,
        PFN_CCSP_DIAGCOMPSIG_CB diagCompleteSignalCB,
        CCSP_HANDLE             cbContext,
        CCSP_HANDLE             cpeContext,
        CCSP_STRING             sdmXmlFilename
    );

#if 0
/* Customizable default password generation, platform specific
 */
extern ANSC_STATUS
CcspManagementServer_GenerateDefaultPassword
    (
        CCSP_STRING             pDftPassword,
        PULONG                  pulLength
    );
#endif

static void _get_shell_output (FILE *fp, char *out, size_t len)
{
    if (len <= 0)
        return;

    *out = 0;

    if (fp)
    {
        fgets(out, len, fp);
        len = strlen(out);
        if ((len > 0) && (out[len - 1] == '\n'))
            out[len - 1] = 0;
    }
}

static void _get_shell_output2 (char *cmd, char *buf, size_t len)
{
    FILE *fp;

    if (len <= 0)
        return;

    buf[0] = 0;
    fp = popen (cmd, "r");
    if (fp == NULL)
        return;
    buf = fgets (buf, len, fp);
    pclose (fp);
    if (buf != NULL) {
        len = strlen (buf);
        if ((len > 0) && (buf[len - 1] == '\n'))
            buf[len - 1] = 0;
    }
}

#ifdef DHCP_PROV_ENABLE

static void SaveIntoPSMHelper (char *dhcp_acs_url)
{
    char psmKeyPrefixed[CCSP_TR069PA_PSM_NODE_NAME_MAX_LEN + 16];

    CcspCwmpPrefixPsmKey(psmKeyPrefixed, CcspManagementServer_SubsystemPrefix, "cwmp.dhcpacsurl");

    if (PSM_Set_Record_Value2(bus_handle, CcspManagementServer_SubsystemPrefix, psmKeyPrefixed, ccsp_string, dhcp_acs_url) != CCSP_SUCCESS)
    {
        CcspTraceInfo(("Failed to set cwmp.dhcpacsurl in PSM\n"));
    }
}

static void GetFromPSMHelper (char *url, size_t url_size)
{
    char psmKeyPrefixed[CCSP_TR069PA_PSM_NODE_NAME_MAX_LEN + 16];
    char *dhcp_acs_url = NULL;

    *url = 0;

    CcspCwmpPrefixPsmKey(psmKeyPrefixed, CcspManagementServer_SubsystemPrefix, "cwmp.dhcpacsurl");

    if (PSM_Get_Record_Value2(bus_handle, CcspManagementServer_SubsystemPrefix, psmKeyPrefixed, ccsp_string, &dhcp_acs_url) != CCSP_SUCCESS)
    {
        CcspTraceInfo(("Failed to get cwmp.dhcpacsurl in PSM\n"));
        return;
    }

    if (dhcp_acs_url)
    {
        size_t len = strlen(dhcp_acs_url);

        if (len < url_size)
        {
            memcpy (url, dhcp_acs_url, len + 1);
        }

        AnscFreeMemory(dhcp_acs_url);
    }
}

#endif

static void updateInitalContact (void)
{
    int nCcspError = 0;
    char psmKeyPrefixed[CCSP_TR069PA_PSM_NODE_NAME_MAX_LEN] = {0};
    char *lastContactUrl = NULL;
    CCSP_BOOL bEnabled = FALSE;

    CcspCwmpPrefixPsmKey(psmKeyPrefixed, CcspManagementServer_SubsystemPrefix, CCSP_TR069PA_PSM_KEY_InitialContact);

    lastContactUrl = ((PCCSP_CWMP_PROCESSOR_OBJECT)CcspManagementServer_cbContext)->GetLastContactUrl((ANSC_HANDLE)CcspManagementServer_cbContext);

#ifdef DHCP_PROV_ENABLE
    char lastSavedAcsURLFromDHCP[CCSP_CWMP_MAX_URL_SIZE];
    GetFromPSMHelper(lastSavedAcsURLFromDHCP, sizeof(lastSavedAcsURLFromDHCP));
    AnscTraceInfo(("Last ACS URL received from DHCP options is: %s and lastContactUrl is: %s\n", lastSavedAcsURLFromDHCP, lastContactUrl));

    if ((!lastContactUrl) ||
        (!AnscEqualString(objectInfo[ManagementServerID].parameters[ManagementServerURLID].value, lastContactUrl, TRUE) &&
         (lastSavedAcsURLFromDHCP[0] != '\0') &&
         !AnscEqualString(lastSavedAcsURLFromDHCP, lastContactUrl, TRUE)))
    {
        bEnabled = TRUE;
    }
#else
    if ((!lastContactUrl) ||
        (!AnscEqualString(objectInfo[ManagementServerID].parameters[ManagementServerURLID].value, lastContactUrl, TRUE)))
    {
        bEnabled = TRUE;
    }
#endif

    nCcspError = PSM_Set_Record_Value2
             (
                  bus_handle,
                  CcspManagementServer_SubsystemPrefix,
                  psmKeyPrefixed,
                  ccsp_string,
                  bEnabled ? "1" : "0"
              );
    if (nCcspError != CCSP_SUCCESS)
    {
        /* Rollback on error. */
        CcspManagementServer_RollBackParameterValues();
    }

    if (lastContactUrl)
    {
        AnscFreeMemory(lastContactUrl);
    }

    return;
}

static CCSP_BOOL isACSChangedURL (void)
{
    CCSP_BOOL bACSChangedURL = FALSE;
    char *pValue = NULL;
    int res = 0;

    res = PSM_Get_Record_Value2(
                bus_handle,
                CcspManagementServer_SubsystemPrefix,
                "dmsb.ManagementServer.ACSChangedURL",
                NULL,
                &pValue);
    if (res == CCSP_SUCCESS)
    {
        if (AnscEqualString(pValue, "0", FALSE) == TRUE)
        {
            bACSChangedURL = FALSE;
        }
        else
        {
            bACSChangedURL = TRUE;
        }
    }
    if (pValue != NULL)
    {
        AnscFreeMemory(pValue);
    }

    return bACSChangedURL;
}

void GetConfigFrom_bbhm (int parameterID)
{
    char pRecordName[1000];
    size_t len1, len2, len3;
    char *pValue = NULL;
    int res;

    len1 = strlen(CcspManagementServer_ComponentName);
    len2 = strlen(objectInfo[ManagementServerID].name);
    len3 = strlen(objectInfo[ManagementServerID].parameters[parameterID].name);

    assert ((len1 + 1 + len2 + len3 + 7) <= sizeof(pRecordName));

    memcpy(pRecordName, CcspManagementServer_ComponentName, len1);
    pRecordName[len1] = '.';
    memcpy(&pRecordName[len1 + 1], objectInfo[ManagementServerID].name, len2);
    memcpy(&pRecordName[len1 + 1 + len2], objectInfo[ManagementServerID].parameters[parameterID].name, len3);
    memcpy(&pRecordName[len1 + 1 + len2 + len3], ".Value", 7);

    res = PSM_Get_Record_Value2(
                        bus_handle,
                        CcspManagementServer_SubsystemPrefix,
                        pRecordName,
                        NULL,
                        &pValue);

    if(res == CCSP_SUCCESS){
        CcspTraceDebug2
            (
                "ms",
                ("PSM_Get_Record_Value2 returns %d, name=<%s>, value=<%s>\n", res, pRecordName, pValue ? pValue : "NULL")
            );
        }

    if(pValue)
    {
        objectInfo[ManagementServerID].parameters[parameterID].value = pValue;
    }
    else
    {
        objectInfo[ManagementServerID].parameters[parameterID].value = AnscCloneString("");
    }
}

static void ReadTr69TlvData (int ethwan_enable)
{
	int                             res;
	char                            recordName[MAX_BUF_SIZE];
	errno_t                         rc               = -1;
    	int                             ind              = -1;

	FILE *file = NULL;
	Tr69TlvData *object2 = NULL;
	char url[256] = "";
	FILE *fp_dhcp_v6 = NULL;
	FILE *fp_dhcp = NULL;
	CCSP_BOOL bACSChangedURL = FALSE;

	if (!ethwan_enable) //RDKB-40531: As T69_TLVDATA_FILE should not be considered for ETHWAN mode
	{
		/* Intel Proposed RDKB Generic Bug Fix from XB6 SDK
		Wait for TLV202 parsing made Generic as CcspTr069Process is triggerd for start at the early stage of boot up.
		T69_TLVDATA_FILE should not be considered for ETHWAN mode
		Inorder to support ACS URL from DHCP options in DOCSIS mode added wait for wan-status to come up.
		*/
		char out[16];
		char buf[16];
		int watchdog = NO_OF_RETRY;
		FILE *fp = NULL;
		int ret = 0;

		do
		{
			fp = v_secure_popen("r", "sysevent get TLV202-status");
			if (!fp)
			{
				AnscTraceWarning(("%s Error in opening pipe! \n",__FUNCTION__));
			}
			else
			{
				_get_shell_output(fp, out, sizeof(out));
				ret = v_secure_pclose(fp);
				if (ret !=0) {
					AnscTraceWarning(("%s Error in closing command pipe! [%d] \n",__FUNCTION__,ret));
				}
			}
			fp = v_secure_popen("r", "sysevent get wan-status");
			if (!fp)
			{
				AnscTraceWarning(("%s Error in opening pipe! \n",__FUNCTION__));
			}
			else
			{
				_get_shell_output(fp, buf, sizeof(buf));
				ret = v_secure_pclose(fp);
				if (ret !=0) {
					AnscTraceWarning(("%s Error in closing command pipe! [%d] \n",__FUNCTION__,ret));
				}
			}
			sleep(1);
			watchdog--;
		} while ((!strstr(out,"success")) && (!strstr(buf,"started")) && (watchdog != 0));

		if ( watchdog == 0 )
		{
			AnscTraceVerbose(("%s(): Ccsp_GwProvApp haven't been able to initialize TLV Data.\n", __FUNCTION__));
		}

		if ((file = fopen(TR69_TLVDATA_FILE, "rb")) != NULL)
		{
			object2 = malloc(sizeof(Tr69TlvData));
		}
	}

	/* Change the behavior to use TLV202 as first priority. */
	if ((file != NULL) && (object2))
	{
                /* CID 135272 String not null terminated fix */
                memset(object2->URL , '\0', sizeof(object2->URL));
		size_t nm = fread(object2, sizeof(Tr69TlvData), 1, file);
                object2->URL[255] = '\0';
		fclose(file);
		file = NULL;

		if (nm != 1)
		{
			/*
			   File is corrupted or couldn't be read.
			   Fixme: do we need to handle this case better?
			*/
			memset (object2, 0, sizeof(Tr69TlvData));
		}

		AnscTraceInfo(("%s %s File is available and processing by Tr069\n", __FUNCTION__, TR69_TLVDATA_FILE ));
		AnscTraceInfo(("**********************************************************\n"));
		AnscTraceInfo(("%s -#- FreshBootUp: %d\n", __FUNCTION__, object2->FreshBootUp));
		AnscTraceInfo(("%s -#- , Tr69Enable: %d\n", __FUNCTION__, object2->Tr69Enable));
		AnscTraceInfo(("%s -#- , AcsOverRide: %d\n", __FUNCTION__, object2->AcsOverRide));
		AnscTraceInfo(("%s -#- , EnableCWMP: %d\n", __FUNCTION__, object2->EnableCWMP));
		AnscTraceInfo(("%s -#- , URLchanged: %d\n", __FUNCTION__, object2->URLchanged));
		AnscTraceInfo(("%s -#- , Username: %s\n", __FUNCTION__, object2->Username));
		AnscTraceInfo(("%s -#- , Password: %s\n", __FUNCTION__, object2->Password));
		AnscTraceInfo(("%s -#- , URL: %s\n", __FUNCTION__, ( object2->URL[ 0 ] != '\0' ) ? object2->URL : "NULL"));
		AnscTraceInfo(("**********************************************************\n"));

                res = PSM_Set_Record_Value2
                (
                        bus_handle,
                        CcspManagementServer_SubsystemPrefix,
                        TR69_ENABLE_CWMP_VALUE,
                        ccsp_string,
                        object2->EnableCWMP ? "1" : "0"
                );
                fprintf(stderr, "Set EnableCWMP in PSM: val: %s, res: %d\n", object2->EnableCWMP ? "1" : "0", res);
                if(res != CCSP_SUCCESS)
                {
                        /* Rollback on error. */
                        CcspManagementServer_RollBackParameterValues();
                }
                else
                {
                       // restart firewall when EnableCWMP flag is written to PSM
                       system("firewall restart");
                }

                bACSChangedURL = isACSChangedURL();

        // Always update ACS Override flag(TLV202.7) recieved from the boot cfg file
        // Always fetch the ACSOverride flag from the boot config file.
        // Revert to default value "0" if AcsOverRide flag is not present.
        if (object2->AcsOverRide==1){
            objectInfo[ManagementServerID].parameters[ManagementServerACSOverrideID].value=AnscCloneString("true");
        }
        else {
            objectInfo[ManagementServerID].parameters[ManagementServerACSOverrideID].value=AnscCloneString("false");
        }

                /*
                 * B.4.3.7 ACSOverride in CM-SP-eRouter-I10-130808:
                 * If enabled, the CPE MUST accept the ACS URL from the CM configuration file, even if the ACS has overwritten the values.
                 * If disabled, the CPE accepts the CM configuration file values only if the ACS has not overwritten the ACS URL.
                 *   Type   Length   Value
                 *   2.7    N        0: disabled
                 *                   1: enabled
                 */
                if ((object2->AcsOverRide == 1) || ((object2->AcsOverRide == 0) && (bACSChangedURL == FALSE)))
                {
                        /*
                         * If ACS URL is changed by TLV202, we need to set initialContact = 1 in order to indicates
                         * that the Session was established due to a change to the ACS URL.
                         * Then the event code "0 BOOTSTRAP" will be contained in the first Inform.
                        */
                        if(object2->URL && strlen(object2->URL)>0 )
                        {
                                objectInfo[ManagementServerID].parameters[ManagementServerURLID].value = AnscCloneString(object2->URL);
                        }
                        else
                        {
                                char out[256];
                                int i;

                                AnscTraceInfo(("%s %d. ManagementServer URL is not defined in config file. Check if  ACS URL is available from DHCP options\n",__func__,__LINE__));
                                //Check DHCPv6 Options for ACS URL
                                for (i = 0; i < 5; i++)
                                {
                                    _get_shell_output2("sysevent get DHCPv6_ACS_URL", out, sizeof(out));
                                    if (strlen(out) > 0)
                                    {
                                        break;
                                    }
                                    sleep(3);
                                }

                                if (strlen(out) > 0)
                                {
                                    objectInfo[ManagementServerID].parameters[ManagementServerURLID].value = AnscCloneString(out);
                                    AnscTraceInfo(("%s %d. ManagementServer URL from DHCP option(DHCPv6_ACS_URL):%s\n",__func__,__LINE__,out));
                                }
                                else
                                {
                                    //Check DHCPv4 Optiions for ACS URL
                                    for (i = 0; i < 5; i++)
                                    {
                                        _get_shell_output2("sysevent get DHCPv4_ACS_URL", out, sizeof(out));
                                        if (strlen(out) > 0)
                                        {
                                           break;
                                        }
                                        sleep(3);
                                    }

                                    if (strlen(out) > 0)
                                    {
                                        objectInfo[ManagementServerID].parameters[ManagementServerURLID].value = AnscCloneString(out);
                                        AnscTraceInfo(("%s %d. ManagementServer URL from DHCP option(DHCPv4_ACS_URL):%s\n",__func__,__LINE__,out));                                        
                                    }
                                    else
                                    {
                                        GetConfigFrom_bbhm(ManagementServerURLID);                                        
                                    }
                                }
                        }

                        // Here, we need to check what is the value that we got through boot config file and update TR69 PA
                        if(object2->EnableCWMP == 1)
                        {
                                objectInfo[ManagementServerID].parameters[ManagementServerEnableCWMPID].value = AnscCloneString("true");
                        }
                        else
                        {
                                objectInfo[ManagementServerID].parameters[ManagementServerEnableCWMPID].value = AnscCloneString("false");
                        }

                        /* Update all the other TLV-202.2 parameters. If TLV 202 were not set, get it from bbhm config */
                        if(object2->Username && strlen(object2->Username) > 0)
                        {
                                objectInfo[ManagementServerID].parameters[ManagementServerUsernameID].value = AnscCloneString(object2->Username);
                        }
                        else
                        {
                                GetConfigFrom_bbhm(ManagementServerUsernameID);
                        }

                        if(object2->Password&& strlen(object2->Password) > 0)
                        {
                                objectInfo[ManagementServerID].parameters[ManagementServerPasswordID].value = AnscCloneString(object2->Password);
                        }
                        else
                        {
                                GetConfigFrom_bbhm(ManagementServerPasswordID);
                        }

                        if (object2->ConnectionRequestUsername && strlen(object2->ConnectionRequestUsername))
                        {
                               objectInfo[ManagementServerID].parameters[ManagementServerConnectionRequestUsernameID].value = AnscCloneString(object2->ConnectionRequestUsername);
                        }
                        else
                        {
                               GetConfigFrom_bbhm(ManagementServerConnectionRequestUsernameID);
                        }

                        if(object2->ConnectionRequestPassword && strlen(object2->ConnectionRequestPassword))
                        {
                               objectInfo[ManagementServerID].parameters[ManagementServerConnectionRequestPasswordID].value = AnscCloneString(object2->ConnectionRequestPassword);
                        }
                        else
                        {
                               GetConfigFrom_bbhm(ManagementServerConnectionRequestPasswordID);
                        }

                       // Free up all resources before exiting the function
                       if(object2)
                       {
                              free(object2);
                       }
                       updateInitalContact();
                       return;
                }

		// Check if it's a fresh bootup / boot after factory reset / TR69 was never enabled
		// If TR69 was never enabled, then we will always take URL from boot config file.
          	AnscTraceWarning(("%s -#- FreshBootUp: %d, Tr69Enable: %d\n", __FUNCTION__, object2->FreshBootUp, object2->Tr69Enable));
		if((object2->FreshBootUp == 1) || (object2->Tr69Enable == 0))
		{
			AnscTraceWarning(("%s -#- Inside FreshBootUp=1 OR Tr69Enable=0 \n", __FUNCTION__));
			AnscTraceWarning(("%s -#- ACS URL from PSM DB- %s\n", __FUNCTION__, objectInfo[ManagementServerID].parameters[ManagementServerURLID].value));
                        /* CID 335592 Resource leak fix */
                        AnscTraceWarning(("%s -#- ACS URL from cmconfig - %s\n", __FUNCTION__, object2->URL));
			object2->FreshBootUp = 0;
                        /*
                         * If ACS URL is changed by TLV202, we need to set initialContact = 1 in order to indicates
                         * that the Session was established due to a change to the ACS URL.
                         * Then the event code "0 BOOTSTRAP" will be contained in the first Inform.
                        */
			if(objectInfo[ManagementServerID].parameters[ManagementServerURLID].value == NULL)
			{
				objectInfo[ManagementServerID].parameters[ManagementServerURLID].value = AnscCloneString(object2->URL);
			}
			//on Fresh bootup / boot after factory reset, if the URL is empty, set default URL value
                        if (object2->URL[0] == '\0')
			{
				char out[256];
				int i;

				AnscTraceInfo(("%s %d. ManagementServer URL is not defined in config file. Check if  ACS URL is available from DHCP options\n",__func__,__LINE__));

				for (i = 0; i < 5; i++)
				{
					_get_shell_output2("sysevent get DHCPv6_ACS_URL", out, sizeof(out));
					if (strlen(out) > 0)
					{
						break;
					}
					sleep(3);
				}
				if (strlen(out) > 0)
				{
					objectInfo[ManagementServerID].parameters[ManagementServerURLID].value = AnscCloneString(out);
					AnscTraceInfo(("%s %d. ManagementServer URL from DHCP option(DHCPv6_ACS_URL):%s\n",__func__,__LINE__,out));                    
				}
				else
				{
					// Check DHCPv4 Optiions for ACS URL
					for (i = 0; i < 5; i++)
					{
						_get_shell_output2("sysevent get DHCPv4_ACS_URL", out, sizeof(out));
						if (strlen(out) > 0)
						{
							break;
						}
						sleep(3);
					}
					if (strlen(out) > 0)
					{
						objectInfo[ManagementServerID].parameters[ManagementServerURLID].value = AnscCloneString(out);
						AnscTraceInfo(("%s %d. ManagementServer URL from DHCP option(DHCPv4_ACS_URL):%s\n",__func__,__LINE__,out));		    
					}
					else if (g_Tr069PaAcsDefAddr!= NULL)
					{
						AnscTraceWarning(("ACS URL = %s\n",g_Tr069PaAcsDefAddr));
						objectInfo[ManagementServerID].parameters[ManagementServerURLID].value = AnscCloneString(g_Tr069PaAcsDefAddr);
					}
					else
					{
						AnscTraceWarning(("Unable to retrieve ACS URL\n"));
					}
				}
			}
			else
			{
				if(objectInfo[ManagementServerID].parameters[ManagementServerURLID].value == NULL)
				{
					objectInfo[ManagementServerID].parameters[ManagementServerURLID].value = AnscCloneString(object2->URL);
				}
			}
			// Here, we need to check what is the value that we got through boot config file and update TR69 PA
			if(object2->EnableCWMP == 1)
				objectInfo[ManagementServerID].parameters[ManagementServerEnableCWMPID].value = AnscCloneString("true");
			else
				objectInfo[ManagementServerID].parameters[ManagementServerEnableCWMPID].value = AnscCloneString("false");
		}
		// During normal boot-up check if TR69 was enabled in device anytime.
		// If TR69 was enabled at least once URL will be already updated. 
		// But we need to get the latest flag value from boot-config file.
		if ((object2->FreshBootUp == 0) && (object2->Tr69Enable == 1))
		{
                        CCSP_STRING acsURL = AnscCloneString(object2->URL);
			AnscTraceWarning(("%s -#-  Inside FreshBootUp=0 AND Tr69Enable=1 \n", __FUNCTION__));
			AnscTraceWarning(("%s -#-  ACS URL from PSM DB- %s\n", __FUNCTION__, objectInfo[ManagementServerID].parameters[ManagementServerURLID].value));
			AnscTraceWarning(("%s -#-  ACS URL from cmconfig - %s\n", __FUNCTION__, acsURL));
                        AnscFreeMemory(acsURL);

			if (access(CCSP_MGMT_CRPWD_FILE,F_OK)!=0)
				{
				AnscTraceWarning(("%s -#-  %s file is missing in normal boot scenario\n", __FUNCTION__, CCSP_MGMT_CRPWD_FILE));
                        	t2_event_d("SYS_ERROR_MissingMgmtCRPwdID", 1);
				}

            // Check AcsOverRide flag status
            rc = strcasecmp_s("1",strlen("1"),objectInfo[ManagementServerID].parameters[ManagementServerACSOverrideID].value,&ind);
            ERR_CHK(rc);
            if ( rc != EOK || ind )
            {
                rc = strcasecmp_s("true",strlen("true"),objectInfo[ManagementServerID].parameters[ManagementServerACSOverrideID].value, &ind);
                ERR_CHK(rc);
            }
            if ( rc == EOK && !ind )
            {
                /* If AcsOverRide enabled, EnableCWMP and URL is updated from boot config
                 * Any configuration missing in boot config would continue to use existing value
                 */
                if(object2->EnableCWMP){
                    if(object2->EnableCWMP == 1)
                        objectInfo[ManagementServerID].parameters[ManagementServerEnableCWMPID].value = AnscCloneString("true");
                    else
                        objectInfo[ManagementServerID].parameters[ManagementServerEnableCWMPID].value = AnscCloneString("false");
                }
                if(object2->URL && objectInfo[ManagementServerID].parameters[ManagementServerURLID].value){
                    rc = strcasecmp_s(object2->URL,strlen(object2->URL),objectInfo[ManagementServerID].parameters[ManagementServerURLID].value,&ind);
                    ERR_CHK(rc);
                    if ( rc != EOK || ind ){
                        AnscTraceInfo(("%s -#- ACS URL in TLV file different from ACS URL in PSM DB. \n", __FUNCTION__));
                        objectInfo[ManagementServerID].parameters[ManagementServerURLID].value = AnscCloneString(object2->URL);
                        _ansc_sprintf(recordName, "%s.%sURL.Value", CcspManagementServer_ComponentName, objectInfo[ManagementServerID].name);
                        res = PSM_Set_Record_Value2(bus_handle, CcspManagementServer_SubsystemPrefix, recordName, ccsp_string, object2->URL);
                        if(res != CCSP_SUCCESS){
                            AnscTraceWarning(("%s -#- Failed to write object2->URL <%s> into PSM!\n", __FUNCTION__, object2->URL));
                        }
                        /* Since we are switching to new ACS server, existing key would not work.
                         * Removing CCSP_MGMT_CRPWD_FILE and marking object2->Tr69Enable=0 to mimic fresh boot scenario
                         */
                        object2->Tr69Enable=0;
                        if (remove(CCSP_MGMT_CRPWD_FILE))
                        {
                            AnscTraceWarning(("%s -#- Failed to remove CCSP_MGMT_CRPWD_FILE file <%s>\n", __FUNCTION__, CCSP_MGMT_CRPWD_FILE));
                        }
                        if (remove(CCSP_MGMT_PWD_FILE))
                        {
                            AnscTraceWarning(("%s -#- Failed to remove CCSP_MGMT_PWD_FILE file <%s>\n", __FUNCTION__, CCSP_MGMT_PWD_FILE));
                        }
                    }
                }
            }
            else
            {
                /* If AcsOverRide is true, EnableCWMP and URL values would be updated from bootfile
                 * No need to check again.
                 */

                /* If TR69Enabled is already enabled, then no need to read URL.
                   Update only EnableCWMP value to bbhm. */
                if(object2->EnableCWMP == 1)
                {
                    objectInfo[ManagementServerID].parameters[ManagementServerEnableCWMPID].value = AnscCloneString("true");
                }
                else if(object2->EnableCWMP == 0)
                {
                    /* There are possibilities that SNMP can enable TR69. In that case, bbhm will have updated value.
                       We will make the TLV file in sync with the bbhm values.
                       In next boot-up EnableCWMP will again update value from boot-config file*/
                    rc = strcasecmp_s("1",strlen("1"),objectInfo[ManagementServerID].parameters[ManagementServerEnableCWMPID].value,&ind);
                    if ( rc != EOK || ind )
                    {
                        rc = strcasecmp_s("true",strlen("true"),objectInfo[ManagementServerID].parameters[ManagementServerEnableCWMPID].value, &ind);
                    }
                    if ( rc == EOK && !ind )
                    {
                        object2->EnableCWMP = 1;
                    }
                }
            }
        }

		//Below check is needed to make sure PSM has correct ACS URL. This is required for clients that continue to use ACS url from cmconfig.
		if(objectInfo[ManagementServerID].parameters[ManagementServerURLID].value == NULL)
		{
			if(object2->URL != NULL )
			{
				//We are here because, PSM DB doesnt have a valid ACS url but cmconfig has. In this case, setting value from cmconfig to PSM DB
				AnscTraceWarning(("%s -#- PSM DB reported NULL ACS URL.... Setting URL from cmconfig and continue..\n", __FUNCTION__));
				objectInfo[ManagementServerID].parameters[ManagementServerURLID].value = AnscCloneString(object2->URL);
				_ansc_sprintf(recordName, "%s.%sURL.Value", CcspManagementServer_ComponentName, objectInfo[ManagementServerID].name);
				res = PSM_Set_Record_Value2(bus_handle, CcspManagementServer_SubsystemPrefix, recordName, ccsp_string, object2->URL);

				if(res != CCSP_SUCCESS){
					AnscTraceWarning(("%s -#- Failed to write object2->URL <%s> into PSM!\n", __FUNCTION__, object2->URL));
				}
			}
		}

		/* setting cursor at begining of the file & open file in write mode */
		file= fopen(TR69_TLVDATA_FILE, "wb");
		if (file != NULL) 
		{
			fseek(file, 0, SEEK_SET);
			/* Write the updated object2 to the file*/
			fwrite(object2, sizeof(Tr69TlvData), 1, file);
			free(object2);
			fclose(file);
			file = NULL;
			object2 = NULL;
		}
	}
	else
	{
        char *pValue 								 = NULL;
		int   IsNeed2ApplySyndicationPartnerCFGValue = 1;
		int fd                                       = 0;
		AnscTraceWarning(("%s TLV data file is missing!!!\n", __FUNCTION__));
		AnscTraceInfo(("%s %s File is not available so unable to process by Tr069\n", __FUNCTION__, TR69_TLVDATA_FILE ));
		if ((fd = creat("/tmp/.TLVmissedtoparsebytr069",S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) >= 0)
        {
            close(fd);
        }

		//Check whether PSM entry is there or not
                rc = memset_s( recordName, sizeof( recordName ), 0, sizeof( recordName ) );
                ERR_CHK(rc);
		_ansc_sprintf(recordName, "%s.%sEnableCWMP.Value", CcspManagementServer_ComponentName, objectInfo[ManagementServerID].name);

        res = PSM_Get_Record_Value2( bus_handle,
						             CcspManagementServer_SubsystemPrefix,
						             recordName,
						             NULL,
						             &pValue );
		
        if( res == CCSP_SUCCESS )
		{
            AnscTraceInfo(("%s PSM_Get_Record_Value2 success %d, name=<%s> value<%s>\n", __FUNCTION__, res, recordName, ( pValue )  ?  pValue : "NULL" ));

            if( NULL != pValue )
            {
			((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(pValue);
            }
			//No Need to apply since PSM entry is available for EnableCWMP param
            IsNeed2ApplySyndicationPartnerCFGValue = 0;
        }

		//Check whether we need to apply syndication config or not
		if( IsNeed2ApplySyndicationPartnerCFGValue )
		{
			char buf[ 8 ] = { 0 };

			//Get the Syndication_EnableCWMP value and overwrite always during boot-up when cmconfig file not available case
			if( ( 0 == syscfg_get( NULL, "Syndication_EnableCWMP", buf, sizeof( buf )) ) && \
				( '\0' != buf[ 0 ] ) 
			  )
			{
				int Tr69EnableValue = 0;
				
				//Configure EnableCWMP param based on partner's default json config
                                rc = strcmp_s("true", strlen("true"), buf, &ind );
                                ERR_CHK(rc);
                                if((rc == EOK) && (ind == 0))
				{
					objectInfo[ManagementServerID].parameters[ManagementServerEnableCWMPID].value = AnscCloneString("1");
					Tr69EnableValue = 1;
				}
				else
				{
					objectInfo[ManagementServerID].parameters[ManagementServerEnableCWMPID].value = AnscCloneString("0");
					Tr69EnableValue = 0;
				}
			
				AnscTraceInfo(("%s Applying Syndication EnableCWMP:%s\n", __FUNCTION__, buf ));
				
				//Overwrite syndication Enable CWMP value
                                rc = memset_s( recordName, sizeof( recordName ), 0, sizeof( recordName ) );
                                ERR_CHK(rc);
				_ansc_sprintf(recordName, "%s.%sEnableCWMP.Value", CcspManagementServer_ComponentName, objectInfo[ManagementServerID].name);
				res = PSM_Set_Record_Value2(bus_handle, CcspManagementServer_SubsystemPrefix, recordName, ccsp_string, ( 1 == Tr69EnableValue ) ?  "1" : "0" );
				
				if( res != CCSP_SUCCESS )
				{
					AnscTraceWarning(("%s -#- Failed to write EnableCWMP <%s> into PSM!\n", __FUNCTION__, buf ));
				}
			}
		}

		//Get the values from PSM DB in ETHWAN mode at process bootup
		//If PSM values are not available the static variables are initiliased with null strings.
		GetConfigFrom_bbhm(ManagementServerURLID);
		GetConfigFrom_bbhm(ManagementServerUsernameID);
		GetConfigFrom_bbhm(ManagementServerPasswordID);
		GetConfigFrom_bbhm(ManagementServerConnectionRequestUsernameID);
		GetConfigFrom_bbhm(ManagementServerConnectionRequestPasswordID);
	}
        updateInitalContact();
	//To be deleted after testing ConnectionRequest
	//whiteListManagementServerURL();
}

//To be deleted after testing ConnectionRequest
#if 0
/*
 * Stores the ManagementServerURL in PSM to be used while setting firewall rules in case of firewall restart.
 * Sets the iptables rule to allow incoming requests from defined ManagementServer URL.
 * TODO
 * move this function to a common util later to be also used by firewall_interface.c.
 * whitelisting of new ManagementServerURL  has to be handled in "ACS URL changed" case .
 */
void whiteListManagementServerURL()
{
  int                             res;
  char                            recordName[256];
  
  CCSP_STRING pStr = objectInfo[ManagementServerID].parameters[ManagementServerURLID].value;
  fprintf(stderr,"\n  %s %d ManagementServerURL::%s::",__FUNCTION__,__LINE__,pStr);
  if(pStr && AnscSizeOfString(pStr) > 0 )
  {
    memset( recordName, 0, sizeof( recordName ) );
    _ansc_sprintf(recordName, "%s.%sURL.Value", CcspManagementServer_ComponentName, objectInfo[ManagementServerID].name);
    res = PSM_Set_Record_Value2(bus_handle, CcspManagementServer_SubsystemPrefix, recordName, ccsp_string, pStr);
    if(res != CCSP_SUCCESS)
    {
      fprintf(stderr,"\n%s:%d Failed to write object2->URL <%s> into PSM  ",__FUNCTION__,__LINE__, pStr);
      AnscTraceWarning(("%s -#- Failed to write object2->URL <%s> into PSM!\n", __FUNCTION__, pStr));      
    }
    
    char acsAddress[MAX_URL_LEN] = {0};
    strcpy(acsAddress, pStr);	  
    char *str1 = strstr(acsAddress, HTTPS_STR);
    if(str1)
    {
      str1 += strlen(HTTPS_STR);      
    }
    else
    {
      str1 = strstr(acsAddress, HTTP_STR);
      if(str1)
      {
	str1 += strlen(HTTP_STR);	
      }      
    }
    if (str1 == NULL)
    {             
      fprintf(stderr,"\n  %s %d Could not parse URL ",__FUNCTION__,__LINE__);
      return;      
    }
    char * str1end;
    int family = AF_INET;
    if (str1[0] == '[')
    {
      str1++;
      str1end = strchr(str1, ']');
      family = AF_INET6;
    }
    else
    {
      str1end = strchr(str1, ':');
    }
    if (str1end == NULL)
    {
      fprintf(stderr,"\n  %s %d Could not parse URL ",__FUNCTION__,__LINE__);      
      return;      
    }      
    *str1end = 0;	
    fprintf(stderr,"\n  %s %d ACS Server IP Address:%s ",__FUNCTION__,__LINE__,str1); 
          
    char iptable_cmd[1024];
    sprintf(iptable_cmd, "%s -A tr69_filter -s %s -j ACCEPT\n", ((family == AF_INET) ? "iptables" : "ip6tables"), str1);
    fprintf(stderr,"\n  %s %d iptable_cmd:%s ",__FUNCTION__,__LINE__,iptable_cmd);
    system(iptable_cmd);
    fprintf(stderr,"\n Setting ACS URL value for firewall rule setting ");	    
  }
  else
  {
    fprintf(stderr,"\n Could not set ACS URL value for firewall rule ");    
  }
}
#endif 

#ifdef DHCP_PROV_ENABLE
static void* ethwanWaitForMngmntServerURL(void* retry)
{
    int fd = 0;
    int retCode = 0;
    fd_set rfds;
    char msg[1024];

    char dhcpv6_url[MAX_URL_LENGTH];
    char dhcpv4_url[MAX_URL_LENGTH];
    char status[64];

    char* oldValue;
    int psm_len = 0;

    dhcpv6_url[0] = '0';
    dhcpv4_url[0] = '0';

    fd = open(DHCP_ACS_URL, O_RDWR);

    if (fd < 0)
    {
        CcspTraceError(("!!!!!!Failed to open FIFO for DHCP Provisioning!!!!!!!\n"));
        return NULL;
    }

    pthread_detach(ethwanURLThread);

    if (*(int*)retry == 1)
    {
        goto RETRY;
    }
    while(1)
    {
        retCode = 0;

        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);

        memset(status,0,sizeof(status));
        memset(msg,0,sizeof(msg));

        retCode = select(fd+1, &rfds, NULL, NULL, NULL);

        if (retCode < 0)
        {
             if (errno == EINTR)
                 continue;
             CcspTraceError(("!!!!!!Failed to select FIFO for DHCP Provisioning!!!!!!!\n"));
             return NULL;
        }
        else if(retCode == 0)
        {
             continue;
        }

        read(fd, msg, sizeof(msg));

        if (msg[0] == 0)
        {
            continue;
        }

        if (!memcmp(msg,"DHCPv6_ACS_URL",14))
        {
            sscanf(msg+15,"%s",dhcpv6_url);
        }
        else if (!memcmp(msg,"DHCPv4_ACS_URL",14))
        {
            sscanf(msg+15,"%s",dhcpv4_url);
        }
        else if(!memcmp(msg,"CONNECTION_STATUS",17))
        {
            sscanf(msg+18,"%s",status);
        }

RETRY:
        if (strlen(objectInfo[ManagementServerID].parameters[ManagementServerURLID].value) == 0 ||  !memcmp(status,"failed",6))
        {
            if ( dhcpv6_url[0] == '0' )
            {
                 if(sysevent_get(se_fd, se_token, "DHCPv6_ACS_URL", dhcpv6_url, sizeof(dhcpv6_url)))
                 {
                      dhcpv6_url[0] = '0';
                 }
            }

            if ( dhcpv4_url[0] == '0' )
            {
                 if (sysevent_get(se_fd, se_token, "DHCPv4_ACS_URL", dhcpv4_url, sizeof(dhcpv4_url)))
                 {
                      dhcpv4_url[0] = '0';
                 }
            }

            oldValue = objectInfo[ManagementServerID].parameters[ManagementServerURLID].value;
            GetConfigFrom_bbhm(ManagementServerURLID);
            psm_len = strlen(objectInfo[ManagementServerID].parameters[ManagementServerURLID].value);

            if (psm_len && memcmp(objectInfo[ManagementServerID].parameters[ManagementServerURLID].value,oldValue,psm_len))
            {
                 SendValueChangeSignal(ManagementServerID, ManagementServerURLID, oldValue);
                 CcspManagementServer_ValueChangeCB(CcspManagementServer_cbContext, CcspManagementServer_GetPAObjectID(ManagementServerID));
            }
            else if (dhcpv6_url[0] != '0' && memcmp(dhcpv6_url,oldValue,strlen(dhcpv6_url)))
            {
                 objectInfo[ManagementServerID].parameters[ManagementServerURLID].value =  AnscCloneString(dhcpv6_url);
                 SaveIntoPSMHelper(dhcpv6_url);
                 SendValueChangeSignal(ManagementServerID, ManagementServerURLID, oldValue);
                 CcspManagementServer_ValueChangeCB(CcspManagementServer_cbContext, CcspManagementServer_GetPAObjectID(ManagementServerID));
            }
            else if ( dhcpv4_url[0] != '0' && memcmp(dhcpv4_url,oldValue,strlen(dhcpv4_url)))
            {
                 objectInfo[ManagementServerID].parameters[ManagementServerURLID].value =  AnscCloneString(dhcpv4_url);
                 SaveIntoPSMHelper(dhcpv4_url);
                 SendValueChangeSignal(ManagementServerID, ManagementServerURLID, oldValue);
                 CcspManagementServer_ValueChangeCB(CcspManagementServer_cbContext, CcspManagementServer_GetPAObjectID(ManagementServerID));
            }
            else
            {
                 objectInfo[ManagementServerID].parameters[ManagementServerURLID].value =  AnscCloneString(oldValue);
            }
        }

    }
}
#endif

CCSP_VOID
CcspManagementServer_Init
    (
        CCSP_STRING             ComponentName,
        CCSP_STRING             SubsystemPrefix,
        CCSP_HANDLE             hMBusHandle,
        PFN_CCSPMS_VALUECHANGE	msValueChangeCB,
        PFN_CCSP_SYSREADYSIG_CB sysReadySignalCB,
        PFN_CCSP_DIAGCOMPSIG_CB diagCompleteSignalCB,
        CCSP_HANDLE             cbContext,
        CCSP_HANDLE             cpeContext,
        CCSP_STRING             sdmXmlFilename
    )
{
    size_t nameLen = strlen(ComponentName) + 1;
    errno_t rc = -1;
    int res;
    char recordName[MAX_BUF_SIZE];
    int ethwan_enable;

    if ( s_MS_Init_Done ) return;

    CcspManagementServer_ComponentName = AnscAllocateMemory(nameLen);
    if(CcspManagementServer_ComponentName == NULL)
    {
        AnscTraceInfo(("Failed in Allocating Memory: %s %d %s", __FUNCTION__, __LINE__, __FILE__ ));
        exit(1);
    }
    rc = strncpy_s(CcspManagementServer_ComponentName, strlen(ComponentName) + 1, ComponentName, nameLen);
    ERR_CHK(rc);

    if ( !SubsystemPrefix )
    {
        CcspManagementServer_SubsystemPrefix = NULL;
    }
    else
    {
        CcspManagementServer_SubsystemPrefix = AnscCloneString(SubsystemPrefix);
    }

    if ( sdmXmlFilename )
    {
        _SupportedDataModelConfigFile = AnscCloneString(sdmXmlFilename); 
    }
    else
    {
        _SupportedDataModelConfigFile = AnscCloneString(_CCSP_MANAGEMENT_SERVER_DEFAULT_SDM_FILE); 
    }

    bus_handle = hMBusHandle;

    if(msValueChangeCB) CcspManagementServer_ValueChangeCB = msValueChangeCB;
    CcspManagementServer_cbContext  = cbContext;
    CcspManagementServer_cpeContext = cpeContext;
    CcspManagementServer_InitDBus();

    
    /* set callback function on Message Bus to handle systemReadySignal */
    if ( sysReadySignalCB )
    {
        CcspBaseIf_Register_Event(hMBusHandle, NULL, "systemReadySignal");

        CcspBaseIf_SetCallback2
            (
                hMBusHandle,
                "systemReadySignal",
                sysReadySignalCB,
                (void*)cbContext
            );   
    }

    /* set callback function on Message Bus to handle diagCompleteSignal */
    if ( diagCompleteSignalCB )
    {
        CcspBaseIf_Register_Event(hMBusHandle, NULL, "diagCompleteSignal");

        CcspBaseIf_SetCallback2
            (
                hMBusHandle,
                "diagCompleteSignal",
                diagCompleteSignalCB, 
                (void*)cbContext
            );   
    }

    /* TODO: Retrieve IP address/MAC address/Serial Number for later use */
	
    //Custom
    CcspManagementServer_InitCustom(ComponentName,
        SubsystemPrefix,
        hMBusHandle,
        msValueChangeCB,
        sysReadySignalCB,
        diagCompleteSignalCB,
        cbContext,
        cpeContext,
        sdmXmlFilename);

    /* This has to be after InitDBus since PSM needs bus_handle. */
    CcspManagementServer_FillInObjectInfo(); 
    CcspManagementServer_RegisterNameSpace();
    CcspManagementServer_DiscoverComponent();
    if(pPAMComponentName && pPAMComponentPath){
        CcspManagementServer_RegisterWanInterface();
    }

    s_MS_Init_Done = TRUE;

	ethwan_enable = (access(ETHWAN_FILE, F_OK) == 0) ? 1 : 0;

	// Function has to be called if TLV_DATA_FILE is present or not
	ReadTr69TlvData(ethwan_enable);

	if (ethwan_enable) //RDKB-40531: URL updated for EWAN mode
	{
#ifdef DHCP_PROV_ENABLE

        PCCSP_CWMP_PROCESSOR_OBJECT      pMyObject               = (PCCSP_CWMP_PROCESSOR_OBJECT  )cbContext;
        if(pMyObject)
        {
            PCCSP_CWMP_PROCESSOR_PROPERTY    pProperty               = (PCCSP_CWMP_PROCESSOR_PROPERTY)&pMyObject->Property;
            char * pAcsUrl                           = pMyObject->GetAcsUrl((ANSC_HANDLE)pMyObject);
            if ( pAcsUrl )
            {
                size_t len = strlen (pAcsUrl);
                if (len < sizeof(pProperty->AcsUrl))
                {
                    memcpy (pProperty->AcsUrl, pAcsUrl, len + 1);
                }
                AnscFreeMemory(pAcsUrl);
            }
        }

		int file_exists = (access(DHCP_ACS_URL, F_OK) == 0) ? 1 : 0;
		if (file_exists)
		{
			unlink(DHCP_ACS_URL);
		}
		// Start thread to receive ACS URL via DHCP Provisioning in ETHWAN Mode
		if (!mkfifo(DHCP_ACS_URL, 0666))
		{
			pthread_create( &ethwanURLThread, NULL, ethwanWaitForMngmntServerURL,&file_exists);
		}
#endif
		if(objectInfo[ManagementServerID].parameters[ManagementServerURLID].value == NULL)
		{
			//We are here because, PSM DB doesnt have a valid ACS url, device in EWAN mode, setting value from partners_defaults.json to PSM DB
			AnscTraceWarning(("%s -#- PSM DB reported NULL ACS URL.. Device in EWAN mode, setting URL from partners_defaults.json and continue..\n", __FUNCTION__));

			//g_Tr069PaAcsDefAddr has the ACS url from partners_defaults.json, populated via CcspTr069PaSsp_LoadCfgFile() during initialization
			if (g_Tr069PaAcsDefAddr!= NULL)
			{
				AnscTraceWarning(("ACS URL = %s  \n",g_Tr069PaAcsDefAddr));
				objectInfo[ManagementServerID].parameters[ManagementServerURLID].value = AnscCloneString(g_Tr069PaAcsDefAddr);
				rc = memset_s( recordName, sizeof( recordName ), 0, sizeof( recordName ) );                                                                                       
				ERR_CHK(rc);                                                                                                                                                      
				_ansc_sprintf(recordName, "%s.%sURL.Value", CcspManagementServer_ComponentName, objectInfo[ManagementServerID].name);
				res = PSM_Set_Record_Value2(bus_handle, CcspManagementServer_SubsystemPrefix, recordName, ccsp_string, g_Tr069PaAcsDefAddr);
				if(res != CCSP_SUCCESS){
					AnscTraceWarning(("%s -#- Failed to write g_Tr069PaAcsDefAddr <%s> into PSM!\n", __FUNCTION__, g_Tr069PaAcsDefAddr));
				}
			}
			else
			{
				AnscTraceWarning(("Unable to retrieve ACS URL , ACS url retrieved as NULL \n"));
			}

		}
		AnscTraceWarning(("%s -#- ACS URL from PSM DB- %s\n", __FUNCTION__, objectInfo[ManagementServerID].parameters[ManagementServerURLID].value));

	}

    char str[24];
    snprintf(str, sizeof(str), "%lu", g_ulAllocatedSizeCurr);
    objectInfo[MemoryID].parameters[MemoryMinUsageID].value = AnscCloneString(str);

#ifdef USE_WHITELISTED_IP
     //By now we know the ACS URL to be used 
    CCSP_STRING pStr = objectInfo[ManagementServerID].parameters[ManagementServerURLID].value;
    fprintf(stderr,"%s %d ManagementServerURLID:%s\n",__FUNCTION__,__LINE__,pStr);

    char cmd [MAX_URL_LEN + MAX_BUF_SIZE];
    /* Fixme: URL should be single quoted (unless it is already) */
    snprintf(cmd,sizeof(cmd),"sysevent set whitelistedAcsUrl %s",pStr);
    system(cmd);
    fprintf(stderr,"%s: After sysevent set of whitelistedAcsUrl\n", __FUNCTION__ );

    //system("sysevent set firewall-restart");
    system("firewall restart");
    fprintf(stderr,"%s: After firewall restart continue waiting for system ready signal from CR\n", __FUNCTION__ );
#endif

    // To check and wait for system ready signal from CR to proceed further
    waitUntilSystemReady( CcspManagementServer_cbContext );

    //    return  (CCSP_HANDLE)bus_handle;
    return;
}

static CCSP_BOOL CcspManagementServer_GetBooleanValue (char *ParameterValue, CCSP_BOOL DefaultValue)
{
    if (ParameterValue == NULL)
        return DefaultValue;

    if ((strcmp(ParameterValue, "0") == 0) || (strcasecmp(ParameterValue, "false") == 0))
        return FALSE;

    if ((strcmp(ParameterValue, "1") == 0) || (strcasecmp(ParameterValue, "true") == 0))
        return TRUE;

    return DefaultValue;
}

static CCSP_STRING CcspManagementServer_GetBooleanValueStr (char *ParameterValue, CCSP_BOOL DefaultValue)
{
    return AnscCloneString((CcspManagementServer_GetBooleanValue(ParameterValue, DefaultValue) == FALSE) ? "false" : "true");
}

/* CcspManagementServer_GetEnableCWMP is called to get
 * Device.ManagementServer.EnableCWMP.
 * Return value - the parameter value.
 */
CCSP_BOOL
CcspManagementServer_GetEnableCWMP
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);

    return CcspManagementServer_GetBooleanValue(objectInfo[ManagementServerID].parameters[ManagementServerEnableCWMPID].value, TRUE);
}

CCSP_STRING
CcspManagementServer_GetEnableCWMPStr
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);

    return CcspManagementServer_GetBooleanValueStr(objectInfo[ManagementServerID].parameters[ManagementServerEnableCWMPID].value, FALSE);
}

/* CcspManagementServer_GetURL is called to get
 * Device.ManagementServer.URL.
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetURL
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    CCSP_STRING pStr = objectInfo[ManagementServerID].parameters[ManagementServerURLID].value;
    if ( pStr && AnscSizeOfString(pStr) > 0 )
    {
        AnscTraceWarning(("%s -#- ManagementServerURLID_PSM: %s\n", __FUNCTION__, pStr));
	t2_event_s("acs_split", pStr);				
        return  AnscCloneString(pStr);
    }
    else
    {
     //   #if 0
        if(g_Tr069PaAcsDefAddr && AnscSizeOfString(g_Tr069PaAcsDefAddr) > 0)
        {
            if(pStr)
            {
                AnscFreeMemory(pStr);
                objectInfo[ManagementServerID].parameters[ManagementServerURLID].value = NULL;
            }
            objectInfo[ManagementServerID].parameters[ManagementServerURLID].value = AnscCloneString(g_Tr069PaAcsDefAddr);
            return AnscCloneString(g_Tr069PaAcsDefAddr);
        }
        else
      //  #endif
        {
#ifdef DHCP_PROV_ENABLE
            char lastSavedAcsURLFromDHCP[CCSP_CWMP_MAX_URL_SIZE];
            GetFromPSMHelper(lastSavedAcsURLFromDHCP, sizeof(lastSavedAcsURLFromDHCP));

            if (lastSavedAcsURLFromDHCP[0] != '\0')
            {
                AnscTraceWarning(("Last ACS URL received from DHCP options is: %s\n", lastSavedAcsURLFromDHCP));
                return AnscCloneString(lastSavedAcsURLFromDHCP);
            }
#endif
            return  AnscCloneString("");
        }
    }
}

/* CcspManagementServer_GetUsername is called to get
 * Device.ManagementServer.Username.
 * Return value - the parameter value.
 */

/*DH  Customizable default username generation, platform specific*/
ANSC_STATUS
CcspManagementServer_GenerateDefaultUsername
    (
        CCSP_STRING                 pDftUsername,
        PULONG                      pulLength
    );

CCSP_STRING
CcspManagementServer_GetUsername
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    
    CCSP_STRING pUsername = objectInfo[ManagementServerID].parameters[ManagementServerUsernameID].value;

    if ( pUsername && AnscSizeOfString(pUsername) > 0 )
    {
        return  AnscCloneString(pUsername);
    }
    else  
    {
        char        DftUsername[72] = {0};
        ULONG       ulLength        = sizeof(DftUsername) - 1;
        ANSC_STATUS returnStatus    = CcspManagementServer_GenerateDefaultUsername(DftUsername, &ulLength);

        if ( returnStatus != ANSC_STATUS_SUCCESS )
        {
            AnscTraceWarning(("%s -- default username generation failed\n", __FUNCTION__));
            return  AnscCloneString("");
        }
        else
        {
            // Save Username -- TBD  save it to PSM
            if ( pUsername )
            {
                AnscFreeMemory(pUsername);
                objectInfo[ManagementServerID].parameters[ManagementServerUsernameID].value = NULL;
            }
            objectInfo[ManagementServerID].parameters[ManagementServerUsernameID].value = AnscCloneString(DftUsername);
            AnscTraceWarning(("%s -- default username generation Success %s\n", __FUNCTION__, objectInfo[ManagementServerID].parameters[ManagementServerUsernameID].value));
            return  AnscCloneString(DftUsername);
        }
    }
}

/* CcspManagementServer_GetPassword is called to get
 * Device.ManagementServer.Password.
 * Return value - always empty.
 */
CCSP_STRING
CcspManagementServer_GetPassword
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    CCSP_STRING  pStr = objectInfo[ManagementServerID].parameters[ManagementServerPasswordID].value;


    // setting pStr to empty string "" will get the default password back
    if ( pStr && AnscSizeOfString(pStr) > 0 )
    {
        return  AnscCloneString(pStr);
    }
    else 
    {
#if 0
        char          DftPassword[72] = {0};
        ULONG         ulLength        = sizeof(DftPassword) - 1;
        ANSC_STATUS   returnStatus    = CcspManagementServer_GenerateDefaultPassword(DftPassword, &ulLength);

        if ( returnStatus != ANSC_STATUS_SUCCESS )
        {
            AnscTraceWarning(("%s-- default password generation failed, return the empty one!\n", __FUNCTION__));
            return  AnscCloneString("");
        }
        else
        {
            //  Save the password -- TBD  save it to PSM
            if ( pStr )
            {
                AnscFreeMemory(pStr);
                objectInfo[ManagementServerID].parameters[ManagementServerPasswordID].value = NULL;
            }
            objectInfo[ManagementServerID].parameters[ManagementServerPasswordID].value = AnscCloneString(DftPassword);

            return  AnscCloneString(DftPassword);
        }
#else
        return  AnscCloneString("");
#endif
    }
}

/* CcspManagementServer_GetPeriodicInformEnable is called to get
 * Device.ManagementServer.PeriodicInformEnable.
 * Return value - the parameter value.
 */
CCSP_BOOL
CcspManagementServer_GetPeriodicInformEnable
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);

    return CcspManagementServer_GetBooleanValue(objectInfo[ManagementServerID].parameters[ManagementServerPeriodicInformEnableID].value, TRUE);
}

CCSP_STRING
CcspManagementServer_GetPeriodicInformEnableStr
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);

    return CcspManagementServer_GetBooleanValueStr(objectInfo[ManagementServerID].parameters[ManagementServerPeriodicInformEnableID].value, FALSE);
}

/* CcspManagementServer_GetPeriodicInformTime is called to get
 * Device.ManagementServer.PeriodicInformInterval.
 * Return value - the parameter value.
 */
CCSP_UINT
CcspManagementServer_GetPeriodicInformInterval
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    char*   val = objectInfo[ManagementServerID].parameters[ManagementServerPeriodicInformIntervalID].value;
    return  val ? _ansc_atoi(val) : 3600;
}
CCSP_STRING
CcspManagementServer_GetPeriodicInformIntervalStr
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[ManagementServerID].parameters[ManagementServerPeriodicInformIntervalID].value);
}

/* CcspManagementServer_GetPeriodicInformTime is called to get
 * Device.ManagementServer.PeriodicInformTime.
 * Return value - the parameter value.
 */
/*CCSP_UINT
CcspManagementServer_GetPeriodicInformTime
    (
        CCSP_STRING                 ComponentName
    )
{
    char*   val = objectInfo[ManagementServerID].parameters[ManagementServerPeriodicInformTimeID].value;
    return  val ? _ansc_atoi(val) : 0;
}*/


extern CCSP_STRING
CcspManagementServer_GetPeriodicInformTimeStrCustom
    (
        CCSP_STRING                 ComponentName
    );
    
CCSP_STRING
CcspManagementServer_GetPeriodicInformTimeStr
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return CcspManagementServer_GetPeriodicInformTimeStrCustom(ComponentName);
}
/* CcspManagementServer_GetParameterKey is called to get
 * Device.ManagementServer.ParameterKey.
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetParameterKey
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[ManagementServerID].parameters[ManagementServerParameterKeyID].value);
}

/* CcspManagementServer_SetParameterKey is called by PA to set
 * Device.ManagementServer.ParameterKey.
 * This parameter is read-only. So it can only be written by PA.
 * Return value - 0 if success.
 * 
 */
CCSP_INT
CcspManagementServer_SetParameterKey
    (
        CCSP_STRING                 ComponentName,
        CCSP_STRING                 pParameterKey                    
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    /* If it is called by PA, set it directly to PSM. */
    int								res;
    char							recordName[MAX_BUF_SIZE];

    if ( objectInfo[ManagementServerID].parameters[ManagementServerParameterKeyID].value ) {
        AnscFreeMemory((void*)objectInfo[ManagementServerID].parameters[ManagementServerParameterKeyID].value);
    }

    objectInfo[ManagementServerID].parameters[ManagementServerParameterKeyID].value = 
        AnscCloneString(pParameterKey);
    
    _ansc_sprintf(recordName, "%s.%sParameterKey.Value", CcspManagementServer_ComponentName, objectInfo[ManagementServerID].name);    
    
    CcspTraceInfo2("ms", ("Writing ParameterKey <%s> into PSM key <%s> ...\n", pParameterKey, recordName));
    
    res = PSM_Set_Record_Value2 
             (
                  bus_handle,
                  CcspManagementServer_SubsystemPrefix,
                  recordName,
                  ccsp_string,
                  pParameterKey
              );
    
    if(res != CCSP_SUCCESS){
        CcspTraceWarning2("ms", ("Failed to write ParameterKey <%s> into PSM!\n", pParameterKey));
    }
    
    return res;
}

/* CcspManagementServer_GetConnectionRequestURL is called to get
 * Device.ManagementServer.ConnectionRequestURL.
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetConnectionRequestURL
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);

#ifdef _COSA_SIM_

    if( !objectInfo[ManagementServerID].parameters[ManagementServerConnectionRequestURLID].value) 
    {

        #include <sys/types.h>
        #include <ifaddrs.h>
        #include <netinet/in.h> 
        #include <arpa/inet.h>

        char ipaddr[INET_ADDRSTRLEN] = {0}, buf[128] ={0};
        struct ifaddrs *ifAddrStruct=NULL, *ifa=NULL;

        getifaddrs(&ifAddrStruct);
        for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa ->ifa_addr->sa_family==AF_INET) { // IPv4
                if (strstr(ifa->ifa_name, "eth")) { // get first ethernet interface
                    if(inet_ntop(AF_INET, &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr, ipaddr, INET_ADDRSTRLEN) != NULL) {
                        CcspTraceDebug(("FirstUpstreamIpInterfaceIPv4Address is %s\n", ipaddr));
                        break;
                    }
                    else ipaddr[0]='\0';
                }
            }
        }
        if (ifAddrStruct!=NULL) freeifaddrs(ifAddrStruct);
        
        char *ptr_url = objectInfo[ManagementServerID].parameters[ManagementServerConnectionRequestURLID].value;
        char *ptr_port = objectInfo[ManagementServerID].parameters[ManagementServerX_LGI_COM_ConnectionRequestPortID].value;
        char *ptr_path = objectInfo[ManagementServerID].parameters[ManagementServerX_CISCO_COM_ConnectionRequestURLPathID].value;
        
        if(ptr_port) sprintf(buf, "http://%s:%s/", ipaddr, ptr_port);
        else         sprintf(buf, "http://%s:%d/", ipaddr, CWMP_PORT);
        if(ptr_path)
        {
	    rc = strcat_s(buf, sizeof(buf), ptr_path);
            ERR_CHK(rc);
        }    
        if(ptr_url) AnscFreeMemory(ptr_url);

        objectInfo[ManagementServerID].parameters[ManagementServerConnectionRequestURLID].value = AnscCloneString(buf);
    }

    return AnscCloneString(objectInfo[ManagementServerID].parameters[ManagementServerConnectionRequestURLID].value);
                                
#else

    if(!pPAMComponentName || !pPAMComponentPath){
        CcspManagementServer_DiscoverComponent();
    }
    if(!RegisterWanInterfaceDone){
        CcspManagementServer_RegisterWanInterface();
    }
    CcspManagementServer_GenerateConnectionRequestURL(FALSE, NULL);
    return AnscCloneString(objectInfo[ManagementServerID].parameters[ManagementServerConnectionRequestURLID].value);

#endif
}

/* CcspManagementServer_GetFirstUpstreamIpInterface is called by PA to get value of
 * com.cisco.spvtg.ccsp.pam.Helper.FirstUpstreamIpInterface or psm stored value
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetFirstUpstreamIpAddress
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    if(!pPAMComponentName || !pPAMComponentPath){
        CcspManagementServer_DiscoverComponent();
    }
    if(!RegisterWanInterfaceDone){
        CcspManagementServer_RegisterWanInterface();
    }
    if(!pFirstUpstreamIpAddress){
        /* Have to read from psm */
        char pRecordName[1000] = {0};
        char *pValue = NULL;
        errno_t rc  = -1;
        
        rc = strcpy_s(pRecordName, sizeof(pRecordName), CcspManagementServer_ComponentName);
        ERR_CHK(rc);
        rc = strcat_s(pRecordName, sizeof(pRecordName), ".FirstUpstreamIpAddress.Value");
        ERR_CHK(rc);
        int res = PSM_Get_Record_Value2(
            bus_handle,
            CcspManagementServer_SubsystemPrefix,
            pRecordName,
            NULL,
            &pValue);
        if(res != CCSP_SUCCESS){
            CcspTraceWarning2("ms", ("CcspManagementServer_GetFirstUpstreamIpAddress PSM_Get_Record_Value2 failed %d, name=<%s>, value=<%s>\n", res, pRecordName, pValue ? pValue : "NULL"));
            if(pValue) AnscFreeMemory(pValue);
            return NULL;
        }
        if(pValue) {
            pFirstUpstreamIpAddress = AnscCloneString(pValue);
            AnscFreeMemory(pValue);
        }
    }
    return AnscCloneString(pFirstUpstreamIpAddress);
}

/* CcspManagementServer_GetConnectionRequestURLPort is called to get
 * Device.ManagementServer.X_LGI-COM_ConnectionRequestPort
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetConnectionRequestURLPort
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[ManagementServerID].parameters[ManagementServerX_LGI_COM_ConnectionRequestPortID].value);
}
/* CcspManagementServer_GetConnectionRequestURLPath is called to get
 * Device.ManagementServer.X_CISCO_COM_ConnectionRequestURLPath.
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetConnectionRequestURLPath
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[ManagementServerID].parameters[ManagementServerX_CISCO_COM_ConnectionRequestURLPathID].value);
}

CCSP_STRING
CcspManagementServer_GetConnectionRequestIf
    (
        CCSP_STRING                 ComponentName
    )
{
    return AnscCloneString(objectInfo[ManagementServerID].parameters[ManagementServerX_LGI_COM_ConnectionRequestIfID].value);
}

static void waitUntilSessionIsNotActive()
{
    ULONG ulActiveSessions = 0;

    do
    {
        usleep(SLEEP_PERIOD);
        ulActiveSessions =
            ((PCCSP_CWMP_PROCESSOR_OBJECT)CcspManagementServer_cbContext)->GetActiveWmpSessionCount((ANSC_HANDLE)CcspManagementServer_cbContext, FALSE);
    } while (ulActiveSessions != 0);
}

static void* delayRebootThread()
{
    ULONG ticks = 0;

    pthread_detach(pthread_self());

    while (TRUE)
    {
        if (rebootType != NONE)
        {
            ticks = delayRebootTime*2;  // 1 second = 2 ticks
            /*
             * For Device.ManagementServer.DelayReboot, the CPE have to wait until
             * the CWMP session in which this parameter value is set has ended
             * before starting a countdown for reboot.
             */
            if (rebootType == DELAY_REBOOT)
            {
                waitUntilSessionIsNotActive();
            }
            rebootType = NONE;
        }
        else
        {
            usleep(SLEEP_PERIOD);  // 1 second = 2 ticks
            ticks--;
        }

        if (ticks == 0)
        {
            /*
             * If a CWMP session is in progress at the specified time,
             * the CPE MUST wait until the session has ended before performing the reboot.
             */
            waitUntilSessionIsNotActive();
            system("reboot");
        }
    }
}

int
CcspManagementServer_SetScheduleRebootStr
    (
        CCSP_STRING                scheduleRebootStr
    )
{
    time_t setTime = 0;
    time_t currentTime = 0;
    time_t diffTime = 0;
    struct tm tm = {0};

    if (scheduleRebootStr == NULL)
    {
        return TR69_INVALID_PARAMETER_VALUE;
    }

    if (sscanf(scheduleRebootStr, "%d-%d-%dT%02d:%02d:%02dZ",
                &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                &tm.tm_hour, &tm.tm_min, &tm.tm_sec
                )!= DATE_TIME_PARAM_NUMBER)
    {
        return TR69_INVALID_PARAMETER_VALUE;
    }

    /* The time t is the number of seconds since 00:00 (midnight) 1 January 1900 GMT. */
    tm.tm_year -= 1900;
    tm.tm_mon-- ;
    setTime = mktime(&tm);

    currentTime = time(NULL);
    diffTime = setTime-currentTime;

    if (diffTime >= 0)
    {
        delayRebootTime = diffTime;
        rebootType = SCHEDULE_REBOOT;
        if (delayRebootThreadPID == 0)
        {
            pthread_create(&delayRebootThreadPID, NULL, delayRebootThread, NULL);
        }
    }

    return 0;
}

CCSP_STRING
CcspManagementServer_GetScheduleRebootStr
    (
        CCSP_STRING                 ComponentName
    )
{
    return AnscCloneString(objectInfo[ManagementServerID].parameters[ManagementServerScheduleRebootID].value);
}

int
CcspManagementServer_SetDelayRebootStr
    (
        CCSP_STRING                delayRebootStr
    )
{
    int delayReboot = 0;

    delayReboot = _ansc_atoi(delayRebootStr);
    if (delayReboot >= 0)
    {
        delayRebootTime = delayReboot;
        rebootType = DELAY_REBOOT;
        if (delayRebootThreadPID == 0)
        {
            pthread_create(&delayRebootThreadPID, NULL, delayRebootThread, NULL);
        }
    }

    return 0;
}

CCSP_STRING
CcspManagementServer_GetDelayRebootStr
    (
        CCSP_STRING                 ComponentName
    )
{
    return AnscCloneString(objectInfo[ManagementServerID].parameters[ManagementServerDelayRebootID].value);
}

CCSP_BOOL
CcspManagementServer_GetHTTPConnectionRequestEnable

    (
        CCSP_STRING                 ComponentName
    )
{
    return CcspManagementServer_GetBooleanValue(objectInfo[ManagementServerID].parameters[ManagementServerHTTPConnectionRequestEnableID].value, TRUE);
}

CCSP_STRING
CcspManagementServer_GetHTTPConnectionRequestEnableStr

    (
        CCSP_STRING                 ComponentName
    )
{
    return CcspManagementServer_GetBooleanValueStr(objectInfo[ManagementServerID].parameters[ManagementServerHTTPConnectionRequestEnableID].value, FALSE);
}

#ifdef _ANSC_USE_OPENSSL_
CCSP_VOID CcspManagementServer_UpdateOpenSSLVerifyMode(){
    CCSP_BOOL bValdMgmtCertEnabled = FALSE;
    bValdMgmtCertEnabled = CcspManagementServer_GetX_LGI_COM_ValidateManagementServerCertificate(NULL);
    if ( bValdMgmtCertEnabled == TRUE )
    {
        openssl_set_verify_mode(SSL_VERIFY_PEER);
    }
    else
    {
        openssl_set_verify_mode(SSL_VERIFY_NONE);
    }
}
#endif /* _ANSC_USE_OPENSSL_ */

CCSP_BOOL
CcspManagementServer_GetX_LGI_COM_ValidateManagementServerCertificate

    (
        CCSP_STRING                 ComponentName
    )
{
    return CcspManagementServer_GetBooleanValue(objectInfo[ManagementServerID].parameters[ManagementServerX_LGI_COM_ValidateManagementServerCertificateID].value, TRUE);
}

CCSP_STRING
CcspManagementServer_GetX_LGI_COM_ValidateManagementServerCertificateStr

    (
        CCSP_STRING                 ComponentName
    )
{
    return CcspManagementServer_GetBooleanValueStr(objectInfo[ManagementServerID].parameters[ManagementServerX_LGI_COM_ValidateManagementServerCertificateID].value, FALSE);
}

/* CcspManagementServer_GetConnectionRequestUsername is called to get
 * Device.ManagementServer.ConnectionRequestUsername.
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetConnectionRequestUsername
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    // return AnscCloneString(objectInfo[ManagementServerID].parameters[ManagementServerConnectionRequestUsernameID].value);
    CCSP_STRING pStr = objectInfo[ManagementServerID].parameters[ManagementServerConnectionRequestUsernameID].value;

    //    AnscTraceWarning(("%s -- ComponentName = %s...\n", __FUNCTION__, ComponentName));

    // setting pStr to empty string "" will get the default username back
    if ( pStr && AnscSizeOfString(pStr) > 0 )
    {
        return  AnscCloneString(pStr);
    }
    else  
    {
        char        DftUsername[72] = {0};
        ULONG       ulLength        = sizeof(DftUsername) - 1;
        ANSC_STATUS returnStatus    = CcspManagementServer_GenerateDefaultUsername(DftUsername, &ulLength);

        if ( returnStatus != ANSC_STATUS_SUCCESS )
        {
            AnscTraceWarning(("%s -- default username generation failed, return the empty one!\n", __FUNCTION__));
            return  AnscCloneString("");
        }
        else
        {
            // Save Username -- TBD  save it to PSM
            if ( pStr )
            {
                AnscFreeMemory(pStr);
                objectInfo[ManagementServerID].parameters[ManagementServerConnectionRequestUsernameID].value = NULL;
            }
            objectInfo[ManagementServerID].parameters[ManagementServerConnectionRequestUsernameID].value = AnscCloneString(DftUsername);

            return  AnscCloneString(DftUsername);
        }
    }
}

/* CcspManagementServer_GetConnectionRequestPassword is called to get
 * Device.ManagementServer.ConnectionRequestPassword.
 * Return empty value.
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetConnectionRequestPassword
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[ManagementServerID].parameters[ManagementServerConnectionRequestPasswordID].value);
}

/* CcspManagementServer_GetACSOverride is called to get
 * Device.ManagementServer.ACSOverride.
 * Return value - the parameter value.
 */
CCSP_BOOL
CcspManagementServer_GetACSOverride
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);

    return CcspManagementServer_GetBooleanValue(objectInfo[ManagementServerID].parameters[ManagementServerACSOverrideID].value, TRUE);
}

CCSP_STRING
CcspManagementServer_GetACSOverrideStr
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);

    return CcspManagementServer_GetBooleanValueStr(objectInfo[ManagementServerID].parameters[ManagementServerACSOverrideID].value, FALSE);
}

/* CcspManagementServer_GetUpgradesManaged is called to get
 * Device.ManagementServer.UpgradesManaged.
 * Return value - the parameter value.
 */
CCSP_BOOL
CcspManagementServer_GetUpgradesManaged
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);

    return CcspManagementServer_GetBooleanValue(objectInfo[ManagementServerID].parameters[ManagementServerUpgradesManagedID].value, TRUE);
}

CCSP_STRING
CcspManagementServer_GetUpgradesManagedStr
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);

    return CcspManagementServer_GetBooleanValueStr(objectInfo[ManagementServerID].parameters[ManagementServerUpgradesManagedID].value, FALSE);
}
/* CcspManagementServer_GetKickURL is called to get
 * Device.ManagementServer.KickURL.
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetKickURL
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[ManagementServerID].parameters[ManagementServerKickURLID].value);
}

/* CcspManagementServer_GetDownloadProgressURL is called to get
 * Device.ManagementServer.DownloadProgressURL.
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetDownloadProgressURL
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[ManagementServerID].parameters[ManagementServerDownloadProgressURLID].value);
}

/* CcspManagementServer_GetDefaultActiveNotificationThrottle is called to get
 * Device.ManagementServer.DefaultActiveNotificationThrottle.
 * Return value - the parameter value.
 */
CCSP_UINT
CcspManagementServer_GetDefaultActiveNotificationThrottle
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    char*   val = objectInfo[ManagementServerID].parameters[ManagementServerDefaultActiveNotificationThrottleID].value;
    return  val ? _ansc_atoi(val) : 0;
}
CCSP_STRING
CcspManagementServer_GetDefaultActiveNotificationThrottleStr
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
     if (objectInfo[ManagementServerID].parameters[ManagementServerDefaultActiveNotificationThrottleID].value == NULL)
	  return AnscCloneString("1");
     
    return AnscCloneString(objectInfo[ManagementServerID].parameters[ManagementServerDefaultActiveNotificationThrottleID].value);
}

/* CcspManagementServer_GetCWMPRetryMinimumWaitInterval is called to get
 * Device.ManagementServer.CWMPRetryMinimumWaitInterval.
 * Return value - the parameter value.
 */
CCSP_UINT
CcspManagementServer_GetCWMPRetryMinimumWaitInterval
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    char*   val = objectInfo[ManagementServerID].parameters[ManagementServerCWMPRetryMinimumWaitIntervalID].value;
    return  val ? _ansc_atoi(val) : 0;
}
CCSP_STRING
CcspManagementServer_GetCWMPRetryMinimumWaitIntervalStr
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[ManagementServerID].parameters[ManagementServerCWMPRetryMinimumWaitIntervalID].value);
}
/* CcspManagementServer_GetCWMPRetryIntervalMultiplier is called to get
 * Device.ManagementServer.CWMPRetryIntervalMultiplier.
 * Return value - the parameter value.
 */
CCSP_UINT
CcspManagementServer_GetCWMPRetryIntervalMultiplier
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    char*   val = objectInfo[ManagementServerID].parameters[ManagementServerCWMPRetryIntervalMultiplierID].value;
    return  val ? _ansc_atoi(val) : 0;
}
CCSP_STRING
CcspManagementServer_GetCWMPRetryIntervalMultiplierStr
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[ManagementServerID].parameters[ManagementServerCWMPRetryIntervalMultiplierID].value);
}
/* CcspManagementServer_GetUDPConnectionRequestAddress is called to get
 * Device.ManagementServer.UDPConnectionRequestAddress.
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetUDPConnectionRequestAddress
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[ManagementServerID].parameters[ManagementServerUDPConnectionRequestAddressID].value);
}

/* CcspManagementServer_GetUDPConnectionRequestAddressNotificationLimit is called to get
 * Device.ManagementServer.UDPConnectionRequestAddressNotificationLimit.
 * Return value - the parameter value.
 */
//CCSP_UINT
CCSP_STRING
CcspManagementServer_GetUDPConnectionRequestAddressNotificationLimit
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[ManagementServerID].parameters[ManagementServerUDPConnectionRequestAddressNotificationLimitID].value);
}

/* CcspManagementServer_GetSTUNEnable is called to get
 * Device.ManagementServer.STUNEnable.
 * Return value - the parameter value.
 */
CCSP_BOOL
CcspManagementServer_GetSTUNEnable
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);

    return CcspManagementServer_GetBooleanValue(objectInfo[ManagementServerID].parameters[ManagementServerSTUNEnableID].value, TRUE);
}

CCSP_STRING
CcspManagementServer_GetSTUNEnableStr
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);

    return CcspManagementServer_GetBooleanValueStr(objectInfo[ManagementServerID].parameters[ManagementServerSTUNEnableID].value, FALSE);
}
/* CcspManagementServer_GetSTUNServerAddress is called to get
 * Device.ManagementServer.STUNServerAddress.
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetSTUNServerAddress
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[ManagementServerID].parameters[ManagementServerSTUNServerAddressID].value);
}

/* CcspManagementServer_GetSTUNServerPort is called to get
 * Device.ManagementServer.STUNServerPort.
 * Return value - the parameter value.
 */
CCSP_UINT
CcspManagementServer_GetSTUNServerPort
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    char*   val = objectInfo[ManagementServerID].parameters[ManagementServerSTUNServerPortID].value;
    return  val ? _ansc_atoi(val) : 0;
}
CCSP_STRING
CcspManagementServer_GetSTUNServerPortStr
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[ManagementServerID].parameters[ManagementServerSTUNServerPortID].value);
}
/* CcspManagementServer_GetSTUNUsername is called to get
 * Device.ManagementServer.STUNUsername.
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetSTUNUsername
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[ManagementServerID].parameters[ManagementServerSTUNUsernameID].value);
}

/* CcspManagementServer_GetSTUNPassword is called to get
 * Device.ManagementServer.STUNPassword.
 * Return empty value.
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetSTUNPassword
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[ManagementServerID].parameters[ManagementServerSTUNPasswordID].value);
}

/* CcspManagementServer_GetSTUNMaximumKeepAlivePeriod is called to get
 * Device.ManagementServer.STUNMaximumKeepAlivePeriod.
 * Return value - the parameter value.
 */
CCSP_INT
CcspManagementServer_GetSTUNMaximumKeepAlivePeriod
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    char*   val = objectInfo[ManagementServerID].parameters[ManagementServerSTUNMaximumKeepAlivePeriodID].value;
    return  val ? _ansc_atoi(val) : 0;
}
CCSP_STRING
CcspManagementServer_GetSTUNMaximumKeepAlivePeriodStr
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[ManagementServerID].parameters[ManagementServerSTUNMaximumKeepAlivePeriodID].value);
}
/* CcspManagementServer_GetSTUNMinimumKeepAlivePeriod is called to get
 * Device.ManagementServer.STUNMinimumKeepAlivePeriod.
 * Return value - the parameter value.
 */
CCSP_UINT
CcspManagementServer_GetSTUNMinimumKeepAlivePeriod
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    char*   val = objectInfo[ManagementServerID].parameters[ManagementServerSTUNMinimumKeepAlivePeriodID].value;
    return  val ? _ansc_atoi(val) : 0;
}
CCSP_STRING
CcspManagementServer_GetSTUNMinimumKeepAlivePeriodStr
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[ManagementServerID].parameters[ManagementServerSTUNMinimumKeepAlivePeriodID].value);
}
/* CcspManagementServer_GetNATDetected is called to get
 * Device.ManagementServer.NATDetected.
 * Return value - the parameter value.
 */
CCSP_BOOL
CcspManagementServer_GetNATDetected
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);

    return CcspManagementServer_GetBooleanValue(objectInfo[ManagementServerID].parameters[ManagementServerNATDetectedID].value, TRUE);
}

CCSP_STRING
CcspManagementServer_GetNATDetectedStr
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);

    return CcspManagementServer_GetBooleanValueStr(objectInfo[ManagementServerID].parameters[ManagementServerNATDetectedID].value, FALSE);
}

/* CcspManagementServer_GetAliasBasedAddressing is called to get
 * Device.ManagementServer.AliasBasedAddressing
 * Return value - the parameter value
 */
// Currently set to a permanent value of FALSE.  
CCSP_BOOL
CcspManagementServer_GetAliasBasedAddressing
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);

    return CcspManagementServer_GetBooleanValue(objectInfo[ManagementServerID].parameters[ManagementServerAliasBasedAddressingID].value, TRUE);
}

CCSP_STRING
CcspManagementServer_GetAliasBasedAddressingStr
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);

    return CcspManagementServer_GetBooleanValueStr(objectInfo[ManagementServerID].parameters[ManagementServerAliasBasedAddressingID].value, FALSE);
}

/* 
 * Device.ManagementServer.ManageableDevice.{i}.
 * is not supported. So Device.ManagementServer.ManageableDeviceNumberOfEntries 
 * is not supported here.
 * 
 */

/* CcspManagementServer_GetAutonomousTransferCompletePolicy_Enable is called to get
 * Device.ManagementServer.AutonomousTransferCompletePolicy.Enable
 * Return value - the parameter value.
 */
CCSP_BOOL
CcspManagementServer_GetAutonomousTransferCompletePolicy_Enable
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);

    return CcspManagementServer_GetBooleanValue(objectInfo[AutonomousTransferCompletePolicyID].parameters[AutonomousTransferCompletePolicyEnableID].value, TRUE);
}

CCSP_STRING
CcspManagementServer_GetAutonomousTransferCompletePolicy_EnableStr
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);

    return CcspManagementServer_GetBooleanValueStr(objectInfo[AutonomousTransferCompletePolicyID].parameters[AutonomousTransferCompletePolicyEnableID].value, FALSE);
}

/* CcspManagementServer_GetAutonomousTransferCompletePolicy_TransferTypeFilter is called to get
 * Device.ManagementServer.AutonomousTransferCompletePolicy.TransferTypeFilter
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetAutonomousTransferCompletePolicy_TransferTypeFilter
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[AutonomousTransferCompletePolicyID].parameters[AutonomousTransferCompletePolicyTransferTypeFilterID].value);
}

/* CcspManagementServer_GetAutonomousTransferCompletePolicy_ResultTypeFilter is called to get
 * Device.ManagementServer.AutonomousTransferCompletePolicy.ResultTypeFilter
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetAutonomousTransferCompletePolicy_ResultTypeFilter
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[AutonomousTransferCompletePolicyID].parameters[AutonomousTransferCompletePolicyResultTypeFilterID].value);
}

/* CcspManagementServer_GetAutonomousTransferCompletePolicy_FileTypeFilter is called to get
 * Device.ManagementServer.AutonomousTransferCompletePolicy.FileTypeFilter
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetAutonomousTransferCompletePolicy_FileTypeFilter
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[AutonomousTransferCompletePolicyID].parameters[AutonomousTransferCompletePolicyFileTypeFilterID].value);
}

/* CcspManagementServer_GetDUStateChangeComplPolicy_Enable is called to get
 * Device.ManagementServer.DUStateChangeComplPolicy.Enable
 * Return value - the parameter value.
 */
CCSP_BOOL
CcspManagementServer_GetDUStateChangeComplPolicy_Enable
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);

    return CcspManagementServer_GetBooleanValue(objectInfo[DUStateChangeComplPolicyID].parameters[DUStateChangeComplPolicyEnableID].value, TRUE);
}

CCSP_STRING
CcspManagementServer_GetDUStateChangeComplPolicy_EnableStr
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);

    return CcspManagementServer_GetBooleanValueStr(objectInfo[DUStateChangeComplPolicyID].parameters[DUStateChangeComplPolicyEnableID].value, FALSE);
}

/* CcspManagementServer_GetDUStateChangeComplPolicy_OperationTypeFilter is called to get
 * Device.ManagementServer.DUStateChangeComplPolicy.OperationTypeFilter
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetDUStateChangeComplPolicy_OperationTypeFilter
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[DUStateChangeComplPolicyID].parameters[DUStateChangeComplPolicyOperationTypeFilterID].value);
}

/* CcspManagementServer_GetDUStateChangeComplPolicy_ResultTypeFilter is called to get
 * Device.ManagementServer.DUStateChangeComplPolicy.ResultTypeFilter
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetDUStateChangeComplPolicy_ResultTypeFilter
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[DUStateChangeComplPolicyID].parameters[DUStateChangeComplPolicyResultTypeFilterID].value);
}

/* CcspManagementServer_GetDUStateChangeComplPolicy_FaultCodeFilter is called to get
 * Device.ManagementServer.DUStateChangeComplPolicy.FaultCodeFilter
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetDUStateChangeComplPolicy_FaultCodeFilter
    (
        CCSP_STRING                 ComponentName
        )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[DUStateChangeComplPolicyID].parameters[DUStateChangeComplPolicyFaultCodeFilterID].value);
}

/* CcspManagementServer_GetTr069pa_Name is called to get
 * com.cisco.spvtg.ccsp.tr069pa.Name
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetTr069pa_Name
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[Tr069paID].parameters[Tr069paNameID].value);
}

/* CcspManagementServer_GetTr069pa_Version is called to get
 * com.cisco.spvtg.ccsp.tr069pa.Version
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetTr069pa_Version
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[Tr069paID].parameters[Tr069paVersionID].value);
}

/* CcspManagementServer_GetTr069pa_Author is called to get
 * com.cisco.spvtg.ccsp.tr069pa.Author
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetTr069pa_Author
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[Tr069paID].parameters[Tr069paAuthorID].value);
}

/* CcspManagementServer_GetTr069pa_Health is called to get
 * com.cisco.spvtg.ccsp.tr069pa.Health
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetTr069pa_Health
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[Tr069paID].parameters[Tr069paHealthID].value);
}

/* CcspManagementServer_GetTr069pa_State is called to get
 * com.cisco.spvtg.ccsp.tr069pa.State
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetTr069pa_State
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[Tr069paID].parameters[Tr069paStateID].value);
}

/* CcspManagementServer_GetTr069pa_DTXml is called to get
 * com.cisco.spvtg.ccsp.tr069pa.DTXml
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetTr069pa_DTXml
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[Tr069paID].parameters[Tr069paDTXmlID].value);
}

/* CcspManagementServer_GetMemory_MinUsageStr is called to get
 * com.cisco.spvtg.ccsp.tr069pa.Memory.MinUsage
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetMemory_MinUsageStr
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    /* MinUsage is the memory consumed right after init. It does not change. */
    return AnscCloneString(objectInfo[MemoryID].parameters[MemoryMinUsageID].value);
}

/* CcspManagementServer_GetMemory_MaxUsage is called to get
 * com.cisco.spvtg.ccsp.tr069pa.Memory.MaxUsage
 * Return value - the parameter value.
 */
ULONG
CcspManagementServer_GetMemory_MaxUsage
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return g_ulAllocatedSizePeak;
}

/* CcspManagementServer_GetMemory_MaxUsageStr is called to get
 * com.cisco.spvtg.ccsp.tr069pa.Memory.MaxUsage
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetMemory_MaxUsageStr
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);

    char str[24];
    snprintf(str, sizeof(str), "%lu", g_ulAllocatedSizePeak);
    return AnscCloneString(str);
}

/* CcspManagementServer_GetMemory_Consumed is called to get
 * com.cisco.spvtg.ccsp.tr069pa.Memory.Consumed
 * Return value - the parameter value.
 */
ULONG
CcspManagementServer_GetMemory_Consumed
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return g_ulAllocatedSizeCurr;
}

/* CcspManagementServer_GetMemory_ConsumedStr is called to get
 * com.cisco.spvtg.ccsp.tr069pa.Memory.Consumed
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetMemory_ConsumedStr
    (
        CCSP_STRING                 ComponentName
    )
{
    char str[24];
    snprintf(str, sizeof(str), "%lu", CcspManagementServer_GetMemory_Consumed(ComponentName));
    return AnscCloneString(str);
}

/* CcspManagementServer_GetLogging_EnableStr is called to get
 * com.cisco.spvtg.ccsp.tr069pa.Logging.Enable
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetLogging_EnableStr
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);

    return CcspManagementServer_GetBooleanValueStr(objectInfo[LoggingID].parameters[LoggingEnableID].value, FALSE);
}

/* CcspManagementServer_SetLogging_EnableStr is called to set
 * com.cisco.spvtg.ccsp.tr069pa.Logging.Enable
 * Return value - 0 if success.
 */
CCSP_INT
CcspManagementServer_SetLogging_EnableStr
    (
        CCSP_STRING                 ComponentName,
        CCSP_STRING                 Value
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    errno_t rc  = -1;
    int ind = -1;

    rc = strcasecmp_s(Value, strlen(Value), objectInfo[LoggingID].parameters[LoggingEnableID].value, &ind);
    ERR_CHK(rc);
    if ((!ind) && (rc == EOK))
    {
        return CCSP_SUCCESS;
    }
    if(objectInfo[LoggingID].parameters[LoggingEnableID].value) 
        AnscFreeMemory(objectInfo[LoggingID].parameters[LoggingEnableID].value);
    objectInfo[LoggingID].parameters[LoggingEnableID].value = NULL;

    rc = strcasecmp_s("TRUE",strlen("TRUE"),Value,&ind);
    if ( rc != EOK || ind )
    {
        rc = strcasecmp_s("1",strlen("1"),Value,&ind);
    }
    if ( rc == EOK && !ind )
    {
        objectInfo[LoggingID].parameters[LoggingEnableID].value = AnscCloneString("true");
        if(objectInfo[LoggingID].parameters[LoggingLogLevelID].value)
            AnscSetTraceLevel(_ansc_atoi(objectInfo[LoggingID].parameters[LoggingLogLevelID].value));
    }
    else
    {
        objectInfo[LoggingID].parameters[LoggingEnableID].value = AnscCloneString("false");
        AnscSetTraceLevel(CCSP_TRACE_INVALID_LEVEL);
    }
    return CCSP_SUCCESS;
}

/* CcspManagementServer_GetLogging_LogLevelStr is called to get
 * com.cisco.spvtg.ccsp.tr069pa.Logging.LogLevel
 * Return value - the parameter value.
 */
CCSP_STRING
CcspManagementServer_GetLogging_LogLevelStr
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    if(objectInfo[LoggingID].parameters[LoggingLogLevelID].value) {
        return AnscCloneString(objectInfo[LoggingID].parameters[LoggingLogLevelID].value);
    }
    else
    {
        char str[100] = {0};
        //        _ansc_itoa(g_iTraceLevel, str, 10);
        sprintf(str, "%d", g_iTraceLevel);
        return AnscCloneString(str);
    }
}

/* CcspManagementServer_SetLogging_LogLevelStr is called to set
 * com.cisco.spvtg.ccsp.tr069pa.Logging.LogLevel
 * Return value - 0 if success.
 */
CCSP_STRING
CcspManagementServer_SetLogging_LogLevelStr
    (
        CCSP_STRING                 ComponentName,
        CCSP_STRING                 Value
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    int level = _ansc_atoi(Value);
    errno_t rc  = -1;
    int ind = -1;
    
    rc = strcasecmp_s("TRUE",strlen("TRUE"),objectInfo[LoggingID].parameters[LoggingEnableID].value,&ind);
    ERR_CHK(rc);
    if((level != g_iTraceLevel) && ((!ind) && (rc == EOK)))
    {
        AnscSetTraceLevel(level);
    }
    if(objectInfo[LoggingID].parameters[LoggingLogLevelID].value) {
    rc = strcasecmp_s(Value, strlen(Value), objectInfo[LoggingID].parameters[LoggingLogLevelID].value, &ind);
    ERR_CHK(rc);
    if((ind) && (rc == EOK)){
           AnscFreeMemory(objectInfo[LoggingID].parameters[LoggingLogLevelID].value);
           objectInfo[LoggingID].parameters[LoggingLogLevelID].value = AnscCloneString(Value);
        }
    }
    else {
        objectInfo[LoggingID].parameters[LoggingLogLevelID].value = AnscCloneString(Value);
    }
    return AnscCloneString("0");  // return CCSP_SUCCESS;
}

CCSP_STRING
CcspManagementServer_GetTR069_NotificationStr
    (
        CCSP_STRING                 ComponentName
    )
{
    return AnscCloneString(objectInfo[NotifyID].parameters[TR069Notify_TR069_Notification_ID].value);
}

CCSP_STRING
CcspManagementServer_GetConnected_ClientStr
    (
        CCSP_STRING                 ComponentName
    )
{
    return AnscCloneString(objectInfo[NotifyID].parameters[TR069Notify_Connected_ClientID].value);
}


#ifdef   _CCSP_CWMP_STUN_ENABLED
CCSP_VOID
CcspManagementServer_StunBindingChanged
    (
        CCSP_BOOL                   NATDetected,
        char*                       UdpConnReqURL
    )
{
    CCSP_BOOL                       bPrevNatDetected = FALSE;
    char*                           pOldNatDetected = objectInfo[ManagementServerID].parameters[ManagementServerNATDetectedID].value;
    char*                           pOldUrl = objectInfo[ManagementServerID].parameters[ManagementServerUDPConnectionRequestAddressID].value;
    parameterSigStruct_t            valChanged[2];
    int                             valChangedSize = 0;
    errno_t                         rc             = -1;
    int                             ind               = -1;
    rc = memset_s(valChanged, sizeof(parameterSigStruct_t)*2, 0, sizeof(parameterSigStruct_t)*2);
    ERR_CHK(rc);

    /* NATDetected */
    if ( objectInfo[ManagementServerID].parameters[ManagementServerNATDetectedID].value ) 
    {
        rc = strcmp_s("0",strlen("0"),objectInfo[ManagementServerID].parameters[ManagementServerNATDetectedID].value,&ind);
        ERR_CHK(rc);
        if (rc == EOK)
        {
           bPrevNatDetected = !(!ind);
        }
    }

    objectInfo[ManagementServerID].parameters[ManagementServerNATDetectedID].value = 
        AnscCloneString(NATDetected ? "1" : "0");
        
    if ( objectInfo[ManagementServerID].parameters[ManagementServerNATDetectedID].notification &&
         ((bPrevNatDetected && !NATDetected) || (!bPrevNatDetected && NATDetected) ) )
    {
        valChanged[valChangedSize].parameterName = CcspManagementServer_MergeString(objectInfo[ManagementServerID].name, objectInfo[ManagementServerID].parameters[ManagementServerNATDetectedID].name);
        valChanged[valChangedSize].oldValue = pOldNatDetected;
        pOldNatDetected = NULL;
        valChanged[valChangedSize].newValue = AnscCloneString(objectInfo[ManagementServerID].parameters[ManagementServerNATDetectedID].value);
        valChanged[valChangedSize].type = objectInfo[ManagementServerID].parameters[ManagementServerNATDetectedID].type;
        valChangedSize++;
    }
    
    if ( pOldNatDetected )
    {
        AnscFreeMemory(pOldNatDetected);
    }

    /* UDPConnectionRequestAddress */
    rc = strcmp_s(UdpConnReqURL,MAX_UDP_VAL,pOldUrl,&ind);
    ERR_CHK(rc);
    if ((!pOldUrl && UdpConnReqURL) || ( UdpConnReqURL &&  ((ind) && (rc == EOK))))
    
    {
        if ( objectInfo[ManagementServerID].parameters[ManagementServerUDPConnectionRequestAddressID].notification )
        {
            valChanged[valChangedSize].parameterName = CcspManagementServer_MergeString(objectInfo[ManagementServerID].name, objectInfo[ManagementServerID].parameters[ManagementServerUDPConnectionRequestAddressID].name);
            valChanged[valChangedSize].oldValue = pOldUrl;
            pOldUrl = NULL;
            valChanged[valChangedSize].newValue = AnscCloneString(UdpConnReqURL);
            valChanged[valChangedSize].type = objectInfo[ManagementServerID].parameters[ManagementServerUDPConnectionRequestAddressID].type;
            valChangedSize++;
        }
    }

    if ( pOldUrl )
    {
        objectInfo[ManagementServerID].parameters[ManagementServerUDPConnectionRequestAddressID].value = NULL;
        AnscFreeMemory(pOldUrl);
    }

    if ( UdpConnReqURL )
    {
        objectInfo[ManagementServerID].parameters[ManagementServerUDPConnectionRequestAddressID].value =
            AnscCloneString(UdpConnReqURL);
    }

    if ( valChangedSize > 0 )
    {
        int                         res;
        int                         i;

        res = CcspBaseIf_SendparameterValueChangeSignal (
            bus_handle,
            valChanged,
            valChangedSize);
        if(res != CCSP_SUCCESS){
            CcspTraceWarning2("ms", ( "CcspManagementServer_StunBindingChanged - failed to send value change signal, error = %d.\n", res));
        }

        for(i=0; i<valChangedSize; i++)
        {
            if(valChanged[i].parameterName) AnscFreeMemory((void*)valChanged[i].parameterName);
            if(valChanged[i].oldValue) AnscFreeMemory((void*)valChanged[i].oldValue);
            if(valChanged[i].newValue) AnscFreeMemory((void*)valChanged[i].newValue);
        }

    }
}
#endif

/* CcspManagementServer_GetManageableDeviceNotificationLimit is called to get
 * Device.ManagementServer.ManageableDeviceNotificationLimit.
 * Return value - the parameter value.
 */
//CCSP_UINT
CCSP_STRING
CcspManagementServer_GetManageableDeviceNotificationLimit
    (
        CCSP_STRING                 ComponentName
    )
{
    UNREFERENCED_PARAMETER(ComponentName);
    return AnscCloneString(objectInfo[ManagementServerID].parameters[ManagementServerManageableDeviceNotificationLimitID].value);
}

