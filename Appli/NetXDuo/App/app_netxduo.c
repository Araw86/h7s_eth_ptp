/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_netxduo.c
  * @author  MCD Application Team
  * @brief   NetXDuo applicative file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2020-2021 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "app_netxduo.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "main.h"
 #include "nxd_ptp_client.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define PTP_THREAD_PRIORITY 2

#define IPV4_ADDRESS            IP_ADDRESS(192, 168, 1, 2)
#define IPV4_NETWORK_MASK       IP_ADDRESS(255, 255, 255, 0)
#define IPV4_GATEWAY_ADDR       IP_ADDRESS(192, 168, 1, 1)
#define DNS_SERVER_ADDRESS      IP_ADDRESS(192, 168, 1, 1)
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TX_THREAD      NxAppThread;
NX_PACKET_POOL NxAppPool;
NX_IP          NetXDuoEthIpInstance;
/* USER CODE BEGIN PV */
static NX_PTP_CLIENT    ptp_client={0};
/* Define the main thread.  */
static ULONG            ptp_stack[2048 / sizeof(ULONG)];
static SHORT            ptp_utc_offset = 0;

extern ETH_HandleTypeDef heth;

TX_THREAD AppMainThread;
TX_THREAD AppTCPThread;

TX_SEMAPHORE Semaphore;

NX_PACKET_POOL AppPool;

ULONG IpAddress;
ULONG NetMask;
NX_IP IpInstance;
NX_TCP_SOCKET TCPSocket;

UCHAR *pointer;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
static VOID nx_app_thread_entry (ULONG thread_input);
/* USER CODE BEGIN PFP */
/* PTP handler.  */
static UINT ptp_event_callback(NX_PTP_CLIENT *ptp_client_ptr, UINT event, VOID *event_data, VOID *callback_data);
// #define SW_CLOCK
#define DRIVER_PTP
#ifdef SW_CLOCK
#define CLOCK_CALLBACK nx_ptp_client_soft_clock_callback
extern UINT CLOCK_CALLBACK(NX_PTP_CLIENT *client_ptr, UINT operation,
  NX_PTP_TIME *time_ptr, NX_PACKET *packet_ptr,
  VOID *callback_data);
#elif defined DRIVER_PTP
#define CLOCK_CALLBACK nx_driver_ptp_clock_callback

#else
#define CLOCK_CALLBACK nx_ptp_client_hw_clock_callback

UINT CLOCK_CALLBACK(NX_PTP_CLIENT *client_ptr, UINT operation,
  NX_PTP_TIME *time_ptr, NX_PACKET *packet_ptr,
  VOID *callback_data);
#endif

/* USER CODE BEGIN PFP */
static VOID App_Main_Thread_Entry(ULONG thread_input);
/* USER CODE END PFP */

/**
  * @brief  Application NetXDuo Initialization.
  * @param memory_ptr: memory pointer
  * @retval int
  */
UINT MX_NetXDuo_Init(VOID *memory_ptr)
{
  UINT ret = NX_SUCCESS;
  TX_BYTE_POOL *byte_pool = (TX_BYTE_POOL*)memory_ptr;
  CHAR *pointer;

  /* USER CODE BEGIN MX_NetXDuo_MEM_POOL */
  /* USER CODE END MX_NetXDuo_MEM_POOL */

  /* USER CODE BEGIN 0 */

  printf("Nx_TCP_Echo_Client application started..\n");
  /* Initialize the NetX system.  */
  nx_system_initialize();
  /* Allocate the memory for packet_pool.  */
  if (tx_byte_allocate(byte_pool, (VOID **) &pointer,  NX_PACKET_POOL_SIZE, TX_NO_WAIT) != TX_SUCCESS)
  {
    return TX_POOL_ERROR;
  }

  /* Create the Packet pool to be used for packet allocation */
  ret = nx_packet_pool_create(&AppPool, "Main Packet Pool", PAYLOAD_SIZE, pointer, NX_PACKET_POOL_SIZE);

  if (ret != NX_SUCCESS)
  {
    return NX_NOT_ENABLED;
  }

  /* Allocate the memory for Ip_Instance */
  if (tx_byte_allocate(byte_pool, (VOID **) &pointer,   2 * DEFAULT_MEMORY_SIZE, TX_NO_WAIT) != TX_SUCCESS)
  {
    return TX_POOL_ERROR;
  }

  /* Create the main NX_IP instance */
//  ret = nx_ip_create(&IpInstance, "Main Ip instance", NULL_ADDRESS, NULL_ADDRESS, &AppPool,nx_stm32_eth_driver,
//                     pointer, 2 * DEFAULT_MEMORY_SIZE, DEFAULT_PRIORITY);
  ret = nx_ip_create(&IpInstance, "Main Ip instance", IPV4_ADDRESS, IPV4_NETWORK_MASK, &AppPool,nx_stm32_eth_driver,
                     pointer, 2 * DEFAULT_MEMORY_SIZE, 1);

  if (ret != NX_SUCCESS)
  {
    return NX_NOT_ENABLED;
  }

  /* Allocate the memory for ARP */
  if (tx_byte_allocate(byte_pool, (VOID **) &pointer, DEFAULT_MEMORY_SIZE, TX_NO_WAIT) != TX_SUCCESS)
  {
    return TX_POOL_ERROR;
  }

  /*  Enable the ARP protocol and provide the ARP cache size for the IP instance */
  ret = nx_arp_enable(&IpInstance, (VOID *)pointer, DEFAULT_MEMORY_SIZE);

  if (ret != NX_SUCCESS)
  {
    return NX_NOT_ENABLED;
  }

  /* Enable the ICMP */
  ret = nx_icmp_enable(&IpInstance);

  if (ret != NX_SUCCESS)
  {
    return NX_NOT_ENABLED;
  }

  /* Enable the UDP protocol required for  DHCP communication */
  ret = nx_udp_enable(&IpInstance);

  /* Enable the TCP protocol */
  ret = nx_tcp_enable(&IpInstance);

  if (ret != NX_SUCCESS)
  {
    return NX_NOT_ENABLED;
  }

  /* Allocate the memory for main thread   */
  if (tx_byte_allocate(byte_pool, (VOID **) &pointer,2 *  DEFAULT_MEMORY_SIZE, TX_NO_WAIT) != TX_SUCCESS)
  {
    return TX_POOL_ERROR;
  }

  /* Create the main thread */
  ret = tx_thread_create(&AppMainThread, "App Main thread", App_Main_Thread_Entry, 0, pointer, 2 * DEFAULT_MEMORY_SIZE,
                         DEFAULT_PRIORITY, DEFAULT_PRIORITY, TX_NO_TIME_SLICE, TX_AUTO_START);

  if (ret != TX_SUCCESS)
  {
    return NX_NOT_ENABLED;
  }
  /* USER CODE END 0 */

  /* USER CODE BEGIN MX_NetXDuo_Init */

  /* USER CODE END MX_NetXDuo_Init */

  return ret;
}

/* USER CODE BEGIN 1 */
  NX_PTP_TIME tm;
/**
* @brief  Main thread entry.
* @param thread_input: ULONG user argument used by the thread entry
* @retval none
*/
static VOID App_Main_Thread_Entry(ULONG thread_input)
{
  NX_PTP_DATE_TIME date;

  UINT ret;
    /* Create the PTP client instance */
//    nx_ptp_client_create(&ptp_client, &ip_0, 0, &pool_0,
//                         PTP_THREAD_PRIORITY, (UCHAR *)ptp_stack, sizeof(ptp_stack),
//                         CLOCK_CALLBACK, NX_NULL);
    ret = nx_ptp_client_create(&ptp_client, &IpInstance, 0, &AppPool,
                         PTP_THREAD_PRIORITY, (UCHAR *)ptp_stack, sizeof(ptp_stack),
                         CLOCK_CALLBACK, NX_NULL);

/* init ptp HW on eth */
    {
      ETH_PTP_ConfigTypeDef ptpconfig={
      .Timestamp = ENABLE,                             /*!< Timestamp during init to unlock 1588 */
      .TimestampUpdateMode = DISABLE,                    /*!< Fine Timestamp Update selected */
      .TimestampInitialize = DISABLE,                   /*!< Initialize Timestamp, set when TS update is needed */
      .TimestampUpdate = DISABLE,                        /*!< Timestamp Update */
      .TimestampAddendUpdate = DISABLE,                  /*!< Timestamp Addend Update */
      .TimestampAll = DISABLE,                          /*!< Disable Timestamp for All Packets, see other settings below */
      .TimestampRolloverMode = ENABLE,                  /*!< Binary Rollover Control selected */
      .TimestampV2 = ENABLE,                            /*!< Enable PTP Packet Processing for Version 2 Format */
      .TimestampEthernet = DISABLE,                     /*!< Select Processing of PTP over UDP Packets */
      .TimestampIPv6 = ENABLE,                         /*!< Disable Processing of PTP Packets Sent over IPv6-UDP */
      .TimestampIPv4 = ENABLE,                          /*!< Enable Processing of PTP Packets Sent over IPv4-UDP */
      .TimestampEvent = DISABLE,                        /*!< Disable Timestamp Snapshot for Event Messages */
      .TimestampMaster = DISABLE,                       /*!< Enable snapshot for Event Messages */
      .TimestampSnapshots = 0x1,                          /*!< Select PTP packets for Taking Snapshots, Table 588 RM0399 */
      .TimestampFilter = DISABLE,                       /*!< Disable MAC Address for PTP Packet Filtering */
      .TimestampChecksumCorrection = DISABLE,           /*!< Do not enable checksum correction at the moment */
      .TimestampStatusMode = DISABLE,                   /*!< Transmit Timestamp Status Mode disabled */
      .TimestampAddend = 1,          /*!< Timestamp addend value (1) */
      .TimestampSubsecondInc = 10<<16, /*!< Subsecond Increment for fine mode starting from 50MHz CLK */
      };
      HAL_ETH_PTP_SetConfig(&heth,&ptpconfig);
    }

    /* start the PTP client */
    ret = nx_ptp_client_start(&ptp_client, NX_NULL, 0, 0, 0, ptp_event_callback, NX_NULL);

    while(1)
    {

        /* read the PTP clock */
        ret = nx_ptp_client_time_get(&ptp_client, &tm);

        /* convert PTP time to UTC date and time */
        ret= nx_ptp_client_utility_convert_time_to_date(&tm, -ptp_utc_offset, &date);

        /* display the current time */
       printf("%2u/%02u/%u %02u:%02u:%02u.%09lu\r\n", date.day, date.month, date.year, date.hour, date.minute, date.second, date.nanosecond);

        tx_thread_sleep(NX_IP_PERIODIC_RATE);
    }
}

static UINT ptp_event_callback(NX_PTP_CLIENT *ptp_client_ptr, UINT event, VOID *event_data, VOID *callback_data)
{
    NX_PARAMETER_NOT_USED(callback_data);

    switch (event)
    {
        case NX_PTP_CLIENT_EVENT_MASTER:
        {
            printf("new MASTER clock!\r\n");
            break;
        }

        case NX_PTP_CLIENT_EVENT_SYNC:
        {
            nx_ptp_client_sync_info_get((NX_PTP_CLIENT_SYNC *)event_data, NX_NULL, &ptp_utc_offset);
            printf("SYNC event: utc offset=%d\r\n", ptp_utc_offset);
            break;
        }

        case NX_PTP_CLIENT_EVENT_TIMEOUT:
        {
            printf("Master clock TIMEOUT!\r\n");
            break;
        }
        default:
        {
            break;
        }
    }

    return 0;
}

#ifndef SW_CLOCK
NX_PTP_CLIENT *tx_client_ptr;
NX_PACKET *tx_buff;
UINT nx_ptp_client_hw_clock_callback(NX_PTP_CLIENT *client_ptr, UINT operation,
  NX_PTP_TIME *time_ptr, NX_PACKET *packet_ptr,
  VOID *callback_data)
{
TX_INTERRUPT_SAVE_AREA

NX_PARAMETER_NOT_USED(callback_data);

switch (operation)
{

/* Nothing to do for soft initialization.  */
case NX_PTP_CLIENT_CLOCK_INIT:
{
  ETH_PTP_ConfigTypeDef ptpconfig={
  .Timestamp = ENABLE,                             /*!< Timestamp during init to unlock 1588 */
  .TimestampUpdateMode = DISABLE,                    /*!< Fine Timestamp Update selected */
  .TimestampInitialize = DISABLE,                   /*!< Initialize Timestamp, set when TS update is needed */
  .TimestampUpdate = DISABLE,                        /*!< Timestamp Update */
  .TimestampAddendUpdate = DISABLE,                  /*!< Timestamp Addend Update */
  .TimestampAll = DISABLE,                          /*!< Disable Timestamp for All Packets, see other settings below */
  .TimestampRolloverMode = ENABLE,                  /*!< Binary Rollover Control selected */
  .TimestampV2 = ENABLE,                            /*!< Enable PTP Packet Processing for Version 2 Format */
  .TimestampEthernet = DISABLE,                     /*!< Select Processing of PTP over UDP Packets */
  .TimestampIPv6 = ENABLE,                         /*!< Disable Processing of PTP Packets Sent over IPv6-UDP */
  .TimestampIPv4 = ENABLE,                          /*!< Enable Processing of PTP Packets Sent over IPv4-UDP */
  .TimestampEvent = DISABLE,                        /*!< Disable Timestamp Snapshot for Event Messages */
  .TimestampMaster = DISABLE,                       /*!< Enable snapshot for Event Messages */
  .TimestampSnapshots = 0x1,                          /*!< Select PTP packets for Taking Snapshots, Table 588 RM0399 */
  .TimestampFilter = DISABLE,                       /*!< Disable MAC Address for PTP Packet Filtering */
  .TimestampChecksumCorrection = DISABLE,           /*!< Do not enable checksum correction at the moment */
  .TimestampStatusMode = DISABLE,                   /*!< Transmit Timestamp Status Mode disabled */
  .TimestampAddend = 1,          /*!< Timestamp addend value (1) */
  .TimestampSubsecondInc = 4<<16, /*!< Subsecond Increment for fine mode starting from 50MHz CLK */
  };
  HAL_ETH_PTP_SetConfig(&heth,&ptpconfig);
}
break;

/* Set clock.  */
case NX_PTP_CLIENT_CLOCK_SET:
TX_DISABLE
// client_ptr -> nx_ptp_client_soft_clock = *time_ptr;
{
  ETH_TimeTypeDef time;
  time.Seconds=time_ptr->second_low;
  time.NanoSeconds=time_ptr->nanosecond;
  HAL_ETH_PTP_SetTime(&heth,&time);
}

TX_RESTORE
break;

/* Extract timestamp from packet.
For soft implementation, simply fallthrough and return current timestamp.  */
case NX_PTP_CLIENT_CLOCK_PACKET_TS_EXTRACT:
TX_DISABLE

{
  ETH_TimeStampTypeDef time;
  HAL_ETH_PTP_GetRxTimestamp(&heth,&time);
  time_ptr->second_high=0;
  time_ptr->second_low=time.TimeStampHigh;
  time_ptr->nanosecond=time.TimeStampLow;
}
TX_RESTORE
break;
/* Get clock.  */
case NX_PTP_CLIENT_CLOCK_GET:
TX_DISABLE
{
  ETH_TimeTypeDef time;
  HAL_ETH_PTP_GetTime(&heth,&time);
  time_ptr->second_high=0;
  time_ptr->second_low=time.Seconds;
  time_ptr->nanosecond=time.NanoSeconds;
}

// *time_ptr = client_ptr -> nx_ptp_client_soft_clock;
TX_RESTORE
break;

/* Adjust clock.  */
case NX_PTP_CLIENT_CLOCK_ADJUST:
// _nx_ptp_client_soft_clock_adjust(client_ptr, time_ptr -> nanosecond);
ETH_PtpUpdateTypeDef offsetSign;
if(time_ptr -> nanosecond>=0){
  offsetSign=HAL_ETH_PTP_POSITIVE_UPDATE;
 
}else{
  offsetSign=HAL_ETH_PTP_NEGATIVE_UPDATE;
}
ETH_TimeTypeDef offset;
offset.Seconds=time_ptr -> second_low;
offset.NanoSeconds=time_ptr -> nanosecond;
HAL_ETH_PTP_AddTimeOffset(&heth,offsetSign, &offset);
break;

/* Prepare timestamp for current packet.
For soft implementation, simply notify current timestamp.  */
case NX_PTP_CLIENT_CLOCK_PACKET_TS_PREPARE:
// _nx_ptp_client_packet_timestamp_notify(client_ptr, packet_ptr, &(client_ptr -> nx_ptp_client_soft_clock));
tx_client_ptr=client_ptr;
tx_buff=packet_ptr;
HAL_ETH_PTP_InsertTxTimestamp(&heth);
break;

/* Update soft timer.  */
case NX_PTP_CLIENT_CLOCK_SOFT_TIMER_UPDATE:
TX_DISABLE

// /* increment the nanosecond field of the software clock */
// time_ptr -> nanosecond +=
// (LONG)(NX_PTP_NANOSECONDS_PER_SEC / NX_PTP_CLIENT_TIMER_TICKS_PER_SECOND);

// /* update the second field */
// if (time_ptr -> nanosecond >= NX_PTP_NANOSECONDS_PER_SEC)
// {
// time_ptr -> nanosecond -= NX_PTP_NANOSECONDS_PER_SEC;
// _nx_ptp_client_utility_inc64(&(time_ptr -> second_high),
//    &(time_ptr -> second_low));
// }
TX_RESTORE
break;

default:
return(NX_PTP_PARAM_ERROR);
}

return(NX_SUCCESS);
}

#ifndef DRIVER_PTP && SW_CLOCK

void HAL_ETH_TxPtpCallback(uint32_t *buff, ETH_TimeStampTypeDef *timestamp){

  // if(buff==(uint32_t*)(tx_buff->nx_packet_data_start)){
    {
      NX_PTP_TIME timestamp_ptr;
      timestamp_ptr.second_high=0;
      timestamp_ptr.second_low=timestamp->TimeStampHigh;
      timestamp_ptr.nanosecond=timestamp->TimeStampLow;
      _nx_ptp_client_packet_timestamp_notify(tx_client_ptr, tx_buff,&timestamp_ptr);
    }

  // }
}

#endif
#endif
/* USER CODE END 1 */
