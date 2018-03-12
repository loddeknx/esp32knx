/*
 * EibnetIP.h
 *
 *  Created on: 20.01.2018
 *      Author: lars
 */

#ifndef MAIN_EIBNETIP_H_
#define MAIN_EIBNETIP_H_
#include <stdio.h>
#include "esp_system.h"

#define OWNADD 0x66

typedef enum __knx_service_type {
	KNX_ST_SEARCH_REQUEST = 0x0201,
	KNX_ST_SEARCH_RESPONSE = 0x0202,
	KNX_ST_DESCRIPTION_REQUEST = 0x0203,
	KNX_ST_DESCRIPTION_RESPONSE = 0x0204,
	KNX_ST_CONNECT_REQUEST = 0x0205,
	KNX_ST_CONNECT_RESPONSE = 0x0206,
	KNX_ST_CONNECTIONSTATE_REQUEST = 0x0207,
	KNX_ST_CONNECTIONSTATE_RESPONSE = 0x0208,
	KNX_ST_DISCONNECT_REQUEST = 0x0209,
	KNX_ST_DISCONNECT_RESPONSE = 0x020A,

	KNX_ST_DEVICE_CONFIGURATION_REQUEST = 0x0310,
	KNX_ST_DEVICE_CONFIGURATION_ACK = 0x0311,

	KNX_ST_TUNNELING_REQUEST = 0x0420,
	KNX_ST_TUNNELING_ACK = 0x0421,

	KNX_ST_ROUTING_INDICATION = 0x0530,
	KNX_ST_ROUTING_LOST_MESSAGE = 0x0531,
	KNX_ST_ROUTING_BUSY = 0x0532,

//  KNX_ST_RLOG_START = 0x0600,

//  KNX_ST_RLOG_END   = 0x06FF,

	KNX_ST_REMOTE_DIAGNOSTIC_REQUEST = 0x0740,
	KNX_ST_REMOTE_DIAGNOSTIC_RESPONSE = 0x0741,
	KNX_ST_REMOTE_BASIC_CONFIGURATION_REQUEST = 0x0742,
	KNX_ST_REMOTE_RESET_REQUEST = 0x0743,

//  KNX_ST_OBJSRV_START = 0x0800,
//  KNX_ST_OBJSRV_END   = 0x08FF,
} knx_service_type_t;

#define L_DATA_req            0x11
#define L_DATA_con            0x2E
#define L_DATA_ind            0x29

#define  EIBNET_VALUE_READ             0x1000
#define  EIBNET_VALUE_RSP              0x1001
#define  EIBNET_VALUE_WRITE            0x1002

typedef union __address {
	uint16_t value;
	struct {
		uint8_t high;
		uint8_t low;
	} bytes;
	uint8_t array[2];
} address_t;


typedef enum __knx_command_type
{
	KNX_CT_READ = 0x00,
	KNX_CT_ANSWER = 0x01,
	KNX_CT_WRITE = 0x02,
	KNX_CT_INDIVIDUAL_ADDR_WRITE = 0x03,
	KNX_CT_INDIVIDUAL_ADDR_REQUEST = 0x04,
	KNX_CT_INDIVIDUAL_ADDR_RESPONSE = 0x05,
	KNX_CT_ADC_READ = 0x06,
	KNX_CT_ADC_ANSWER = 0x07,
	KNX_CT_MEM_READ = 0x08,
	KNX_CT_MEM_ANSWER = 0x09,
	KNX_CT_MEM_WRITE = 0x0A,
//KNX_CT_UNKNOWN                  = 0x0B,
	KNX_CT_MASK_VERSION_READ = 0x0C,
	KNX_CT_MASK_VERSION_RESPONSE = 0x0D,
	KNX_CT_RESTART = 0x0E,
	KNX_CT_ESCAPE = 0x0F,
} knx_command_type_t;

typedef struct __datapacket {
	knx_command_type_t ct;
	address_t target;
	uint8_t len;
	uint8_t data[14];
} datapacket_t;

typedef struct __cemi_service {
	union {
		struct {
			// Struct is reversed due to bit order
			uint8_t confirm :1; // 0 = no error, 1 = error
			uint8_t ack :1; // 0 = no ack, 1 = ack
			uint8_t priority :2; // 0 = system, 1 = high, 2 = urgent/alarm, 3 = normal
			uint8_t system_broadcast :1; // 0 = system broadcast, 1 = broadcast
			uint8_t repeat :1; // 0 = repeat on error, 1 = do not repeat
			uint8_t reserved :1; // always zero
			uint8_t frame_type :1; // 0 = extended, 1 = standard
		} bits;
		uint8_t byte;
	} control_1;
	union {
		struct {
			// Struct is reversed due to bit order
			uint8_t extended_frame_format :4;
			uint8_t hop_count :3;
			uint8_t dest_addr_type :1; // 0 = individual, 1 = group
		} bits;
		uint8_t byte;
	} control_2;
	address_t source;
	address_t destination;
	uint8_t data_len; // length of data, excluding the tpci byte
	struct
	{
		uint8_t apci :2; // If tpci.comm_type == KNX_COT_UCD or KNX_COT_NCD, then this is apparently control data?
		uint8_t tpci_seq_number :4;
		uint8_t tpci_comm_type :2; // See knx_communication_type_t
	} pci;
	uint8_t data[];
} cemi_service_t;

typedef struct __cemi_addi {
	uint8_t type_id;
	uint8_t len;
	uint8_t data[];
} cemi_addi_t;

typedef struct __cemi_msg {
	uint8_t message_code;
	uint8_t additional_info_len;
	union {
		cemi_addi_t additional_info[0];
		cemi_service_t service_information;
	} data;
} cemi_msg_t;


typedef struct {
	uint8_t structlength;
	uint8_t hostprotocol;
	uint8_t ip1;
	uint8_t ip2;
	uint8_t ip3;
	uint8_t ip4;
	uint16_t port;
} EIBNETIP_HPAI;

typedef struct {
	uint8_t structlength;
	uint8_t connectiontypecode;
	uint8_t data1;
	uint8_t data2;
} EIBNETIP_CRI_CRD;

typedef struct {
	uint8_t headersize;
	uint8_t version;
	uint16_t servicetype;
	uint16_t totalsize;
} EIBNETIP_HEADER;

typedef struct {
	EIBNETIP_HEADER head;
	uint8_t data[];
} EIBNETIP_PACKET;

typedef struct {
	uint8_t channelid;
	uint8_t status;
	EIBNETIP_HPAI dataendpoint;
	EIBNETIP_CRI_CRD crd;
} EIBNETIP_CONNECT_RESPONSE;

typedef struct {
	uint8_t channelid;
	uint8_t reserved;
	EIBNETIP_HPAI controlendpoint;
} EIBNETIP_CONNECTIONSTATE_REQUEST;

typedef struct {
	uint8_t channelid;
	uint8_t reserved;
	EIBNETIP_HPAI controlendpoint;
} EIBNETIP_DISCONNECT_REQUEST;

typedef struct {
	EIBNETIP_HPAI discoveryendpoint;
} EIBNETIP_DISCOVERY_REQUEST;

typedef struct {
	uint8_t channelid;
	uint8_t status;
} EIBNETIP_CONNECTIONSTATE_RESPONSE;

typedef struct {
	uint8_t channelid;
	uint8_t status;
} EIBNETIP_DISCONNECT_RESPONSE;

typedef struct {
	uint8_t structlength;
	uint8_t channelid;
	uint8_t sequencecounter;
	uint8_t typespecific;
} EIBNETIP_COMMON_CONNECTION_HEADER;

#endif /* MAIN_EIBNETIP_H_ */
