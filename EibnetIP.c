#include "EibnetIP.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "string.h"
#include "lwip/sockets.h"
#include "lwip/api.h"
#include "esp_system.h"
#include <esp_log.h>
#include <stdbool.h>
#include "math.h"

#include "globals.h"


QueueHandle_t sendQ;
QueueHandle_t recvQ;


extern ip4_addr_t ip;
ip_addr_t rmip; //IP of the tunneling server e.g. eibd
uint16_t rmport; //Port of the tunneling server e.g. eibd

struct netconn *ctrlconn, *dataconn;

bool connected=false;
uint8_t channelid;
uint8_t sendSeqCnt = 0;

TaskHandle_t sendTask;
TaskHandle_t heartbeatTask;
TaskHandle_t ctrlTask;
TaskHandle_t dataTask;



void EibnetIPDataSock();
void EibnetIPCtrlSock();
void sendDiscoveryRequest();


void sendData(uint16_t dest, uint8_t data_len, uint8_t *data) {
	datapacket_t* packet = malloc(sizeof(datapacket_t));
	packet->target.value = dest;
	packet->len = data_len;
	memset(packet->data, 0, 14);
	memcpy(packet->data, data, data_len);
	xQueueSend(sendQ, &packet, 0);
}

void sendFloat(uint16_t dest, float data){
	int nSign = (data < 0 ? 0x8000 : 0);
	int nExpo = 0;
	int nMant = 0;
	nMant = (int)(data * 100.0);
	while (abs(nMant) > 2047)
	{
		nMant = nMant >> 1;
		nExpo++;
	}
	uint16_t dummy= ( nSign | (nExpo << 11) | (nMant & 0x07FF) );
	uint8_t buf[] = {0x00, (uint8_t)(dummy>>8), (uint8_t)dummy&0xFF};
	sendData(dest, 3, buf);
}

void initTunneling(ip_addr_t ip, uint16_t port){
	rmip=ip;
	rmport=port;
	xTaskCreate(&EibnetIPCtrlSock, "ctrl_sock", 2048, NULL, 4, &ctrlTask);
	xTaskCreate(&EibnetIPDataSock, "data_sock", 2048, NULL, 5, &dataTask);
	vTaskSuspend(dataTask);
}

void initTunnelingWithDiscovery(){
	sendDiscoveryRequest();
	xTaskCreate(&EibnetIPCtrlSock, "ctrl_sock", 2048, NULL, 4, &ctrlTask);
	xTaskCreate(&EibnetIPDataSock, "data_sock", 2048, NULL, 5, &dataTask);
	vTaskSuspend(dataTask);
}

void sendTunnelingAck(struct netconn *newconn, uint8_t channelid, uint8_t sequencecounter, uint8_t ec) {
	struct netbuf * netbuf;
	char buffer[6 + sizeof(EIBNETIP_COMMON_CONNECTION_HEADER)];
	netbuf = netbuf_new();
	netbuf_ref(netbuf, buffer, 6 + sizeof(EIBNETIP_COMMON_CONNECTION_HEADER));
	EIBNETIP_PACKET *ack = (EIBNETIP_PACKET*) buffer;
	ack->head.headersize = 0x06;
	ack->head.version = 0x10;
	ack->head.servicetype = ntohs(KNX_ST_TUNNELING_ACK);
	ack->head.totalsize = ntohs(6 + sizeof(EIBNETIP_COMMON_CONNECTION_HEADER));
	EIBNETIP_COMMON_CONNECTION_HEADER* cch = (EIBNETIP_COMMON_CONNECTION_HEADER *) ((uint8_t *) ack + 6);
	cch->structlength = 0x04;
	cch->channelid = channelid;
	cch->sequencecounter = sequencecounter;
	cch->typespecific = ec;
	netconn_send(newconn, netbuf);
	netbuf_delete(netbuf);
}

void sendDisconnectRequest(struct netconn *newconn, uint8_t channelid) {
	struct netbuf * netbuf;
	char buffer[6 + sizeof(EIBNETIP_DISCONNECT_REQUEST)];
	netbuf = netbuf_new();
	netbuf_ref(netbuf, buffer, 6 + sizeof(EIBNETIP_DISCONNECT_REQUEST));
	EIBNETIP_PACKET *request = (EIBNETIP_PACKET*) buffer;
	request->head.headersize = 0x06;
	request->head.version = 0x10;
	request->head.servicetype = ntohs(KNX_ST_DISCONNECT_REQUEST);
	request->head.totalsize = ntohs(6 + sizeof(EIBNETIP_DISCONNECT_REQUEST));
	EIBNETIP_DISCONNECT_REQUEST* discreq = (EIBNETIP_DISCONNECT_REQUEST *) ((uint8_t *) request + 6);
	discreq->channelid = channelid;
	discreq->reserved = 0;
	EIBNETIP_HPAI *hpai = (EIBNETIP_HPAI *) &(discreq->controlendpoint);
	hpai->structlength = sizeof(EIBNETIP_HPAI);
	hpai->hostprotocol = 0x01;
	hpai->port = htons(1690);
	*((uint32_t *) &(hpai->ip1)) = ip.addr;
	netconn_send(newconn, netbuf);
	netbuf_delete(netbuf);
}

void sendDiscoveryRequest() {
	ip_addr_t mcip;
	IP_ADDR4(&mcip, 224, 0, 23, 12);
	struct netbuf *inbuf;
	char *buf;
	u16_t buflen;
	struct netconn* discoveryconn = netconn_new(NETCONN_UDP);
	err_t err = netconn_bind(discoveryconn, NULL, 1234);
	if (err != ERR_OK) {
		printf("Binding failed %d!!\n", err);
	}
	struct netbuf * netbuf;
	char buffer[6 + sizeof(EIBNETIP_HPAI)];
	netbuf = netbuf_new();
	netbuf_ref(netbuf, buffer, 6 + sizeof(EIBNETIP_HPAI));
	EIBNETIP_PACKET *request = (EIBNETIP_PACKET*) buffer;
	request->head.headersize = 0x06;
	request->head.version = 0x10;
	request->head.servicetype = ntohs(KNX_ST_SEARCH_REQUEST);
	request->head.totalsize = ntohs(6 + sizeof(EIBNETIP_HPAI));
	EIBNETIP_HPAI *hpai = (EIBNETIP_HPAI *) &(request->data);
	hpai->structlength = sizeof(EIBNETIP_HPAI);
	hpai->hostprotocol = 0x01;
	hpai->port = htons(1234);
	*((uint32_t *) &(hpai->ip1)) = ip.addr;
	netconn_sendto(discoveryconn,netbuf,&mcip,3671);
	netconn_set_recvtimeout(discoveryconn, 10000);
	while (1) {
		vTaskDelay(50 / portTICK_PERIOD_MS);
		err = netconn_recv(discoveryconn, &inbuf);
		if (err == ERR_OK) {
			netbuf_data(inbuf, (void**) &buf, &buflen);
			EIBNETIP_PACKET* received_packet = (EIBNETIP_PACKET *) buf;
			if (received_packet->head.headersize == 0x06 && received_packet->head.version == 0x10) {
				if (received_packet->head.servicetype==KNX_ST_SEARCH_RESPONSE){
					EIBNETIP_HPAI *ctrlep = (EIBNETIP_HPAI *) &(received_packet->data);
					if (received_packet->data[66]==0x04){
						printf("Got search response from IP %d.%d.%d.%d:%d!\n",ctrlep->ip1,ctrlep->ip2,ctrlep->ip3,ctrlep->ip4,htons(ctrlep->port));
						IP_ADDR4(&rmip, ctrlep->ip1, ctrlep->ip2, ctrlep->ip3, ctrlep->ip4);
						rmport=htons(ctrlep->port);
						break;
					}
				}
			}
		}else if (err == ERR_TIMEOUT){
			printf("Repeating the discovery request!\n");
			netconn_sendto(discoveryconn,netbuf,&mcip,3671);
		}
	}
	netbuf_delete(netbuf);
}

void sendConnectionStateRequest(struct netconn *newconn) {
	struct netbuf * netbuf;
	char buffer[6 + sizeof(EIBNETIP_CONNECTIONSTATE_REQUEST)];
	netbuf = netbuf_new();
	netbuf_ref(netbuf, buffer, 6 + sizeof(EIBNETIP_CONNECTIONSTATE_REQUEST));
	EIBNETIP_PACKET *request = (EIBNETIP_PACKET*)buffer;
	request->head.headersize = 0x06;
	request->head.version = 0x10;
	request->head.servicetype = ntohs(KNX_ST_CONNECTIONSTATE_REQUEST);
	request->head.totalsize = ntohs(6 + sizeof(EIBNETIP_CONNECTIONSTATE_REQUEST));
	EIBNETIP_CONNECTIONSTATE_REQUEST* constat = (EIBNETIP_CONNECTIONSTATE_REQUEST *) ((uint8_t *) request + 6);
	constat->channelid = channelid;
	constat->reserved = 0;
	EIBNETIP_HPAI *hpai = (EIBNETIP_HPAI *) &(constat->controlendpoint);
	hpai->structlength = sizeof(EIBNETIP_HPAI);
	hpai->hostprotocol = 0x01;
	hpai->port = htons(1690);
	*((uint32_t *) &(hpai->ip1)) = ip.addr;
	netconn_send(newconn, netbuf);
	netbuf_delete(netbuf);
}

void sendConnectRequest(struct netconn *newconn) {
	struct netbuf * netbuf;
	netbuf = netbuf_new();

	uint8_t buf[6 + 2 * sizeof(EIBNETIP_HPAI) + sizeof(EIBNETIP_CRI_CRD)];
	netbuf_ref(netbuf, buf, 6 + 2 * sizeof(EIBNETIP_HPAI) + sizeof(EIBNETIP_CRI_CRD));
	EIBNETIP_PACKET* request=(EIBNETIP_PACKET*) buf;
	request->head.headersize=0x06;
	request->head.version=0x10;
	request->head.servicetype=ntohs(KNX_ST_CONNECT_REQUEST);
	request->head.totalsize=ntohs(6 + 2 * sizeof(EIBNETIP_HPAI) + sizeof(EIBNETIP_CRI_CRD));
	EIBNETIP_HPAI *hpaiCtrl = (EIBNETIP_HPAI *) ((uint8_t *) request->data);
	hpaiCtrl->structlength = sizeof(EIBNETIP_HPAI);
	hpaiCtrl->hostprotocol = 0x01;
	hpaiCtrl->port = ntohs(1690);
	*((uint32_t *) &(hpaiCtrl->ip1)) = ip.addr;

	EIBNETIP_HPAI *hpaiData = (EIBNETIP_HPAI *) ((uint8_t *) request->data+sizeof(EIBNETIP_HPAI));
	hpaiData->structlength = sizeof(EIBNETIP_HPAI);
	hpaiData->hostprotocol = 0x01;
	hpaiData->port = ntohs(1691);
	*((uint32_t *) &(hpaiData->ip1)) = ip.addr;

	EIBNETIP_CRI_CRD *crd = (EIBNETIP_CRI_CRD *) ((uint8_t *) request->data	+ 2 * sizeof(EIBNETIP_HPAI));
	/* attach the connection information */
	crd->structlength = 0x04;
	crd->connectiontypecode = 0x04;
	crd->data1 = 0x02;
	crd->data2 = 0;

	netconn_send(newconn, netbuf);
	netbuf_delete(netbuf);
}


void heartBeat(struct netconn *newconn) {
	uint32_t ulNotifiedValue;
	while (1) {
		printf("Heartbeat\n");
		sendConnectionStateRequest(newconn);
		if (xTaskNotifyWait(0x00,ULONG_MAX,&ulNotifiedValue, 2000/portTICK_PERIOD_MS)) {
		}
		else {
			sendConnectionStateRequest(newconn);
			if (xTaskNotifyWait(0x00,ULONG_MAX,&ulNotifiedValue, 2000/portTICK_PERIOD_MS)) {
			}else{
				printf("*****************Connection lost!\n");
				connected=false;
				channelid=0;
				vTaskSuspend(dataTask);
			}
		}
		vTaskDelay(100000 / portTICK_PERIOD_MS);
	}
}

void sendDataTask() {
	ip_addr_t mcip;
    IP_ADDR4(&mcip, 192, 168, 0, 238);
	struct netbuf * netbuf;
	datapacket_t* packet;
	uint8_t buf[6 + 4 + 10 + 14];
	EIBNETIP_PACKET* request= (EIBNETIP_PACKET*) buf;
	request->head.headersize=0x06;
	request->head.version=0x10;
	request->head.servicetype=ntohs(KNX_ST_TUNNELING_REQUEST);
	EIBNETIP_COMMON_CONNECTION_HEADER* cch =(EIBNETIP_COMMON_CONNECTION_HEADER *) ((uint8_t *) buf + 6);
	cch->structlength = 0x04;
	cch->typespecific = 0;
	cemi_msg_t *cemi_msg = (cemi_msg_t *) (request->data+4);//(knx_pkt->pkt_data + 4);
	cemi_msg->message_code = L_DATA_req;
	cemi_msg->additional_info_len = 0;
	cemi_service_t *cemi_data = &cemi_msg->data.service_information;
	cemi_data->control_1.bits.confirm = 0;
	cemi_data->control_1.bits.ack = 0;
	cemi_data->control_1.bits.priority = 3;
	cemi_data->control_1.bits.system_broadcast = 0x01;
	cemi_data->control_1.bits.repeat = 0x01;
	cemi_data->control_1.bits.reserved = 0;
	cemi_data->control_1.bits.frame_type = 0x01;
	cemi_data->control_2.bits.extended_frame_format = 0x00;
	cemi_data->control_2.bits.hop_count = 0x06;
	cemi_data->control_2.bits.dest_addr_type = 0x01;
	cemi_data->source.value = ntohs(OWNADD);
	cemi_data->pci.apci = (2 & 0x0C) >> 2;
	cemi_data->pci.tpci_seq_number = 0x00; // ???
	cemi_data->pci.tpci_comm_type = 0; // ???
	printf("Starting sendTask\n");
	while (1) {
		if (xQueueReceive(sendQ, &packet, 5000 / portTICK_PERIOD_MS)) {
			uint32_t ulNotifiedValue;
			netbuf = netbuf_new();
			uint16_t len = 6 + 4 + 10 + packet->len; // knx_pkt + cemi_msg + cemi_service + data + checksum
			request->head.totalsize=ntohs(len);

			cemi_data->destination.value = ntohs(packet->target.value);
			netbuf_ref(netbuf, buf, len);
			cch->channelid = channelid;
			cemi_data->data_len = packet->len;
			cch->sequencecounter = sendSeqCnt;
			memcpy(cemi_data->data, packet->data, packet->len);
			cemi_data->data[0] = (cemi_data->data[0] & 0x3F) | ((2 & 0x03) << 6);
			netconn_send(dataconn, netbuf);
			netconn_sendto(dataconn,netbuf,&mcip,3671);
			if (xTaskNotifyWait(0x00,ULONG_MAX,&ulNotifiedValue, 2000/portTICK_PERIOD_MS)) {
				if ((ulNotifiedValue) == sendSeqCnt) {
					//printf("Got the right ack!!\n");
					sendSeqCnt++;
				} else
					printf("Got the wrong ack!!\n");
				netbuf_delete(netbuf);
				free(packet);
			} else {
				printf("TIMEOUT!!!\n");
				netconn_send(dataconn, netbuf);
				if (xTaskNotifyWait(0x00, ULONG_MAX, &ulNotifiedValue, 2000/portTICK_PERIOD_MS)) {
					if ((ulNotifiedValue) == sendSeqCnt) {
						printf("Got the right ack in second try!!\n");
						sendSeqCnt++;
					} else
						printf("Got the wrong ack!!\n");
				}else{
					printf("TIMEOUT2!!!\n");
				}
				netbuf_delete(netbuf);
				free(packet);
			}
		}
	}
}

void EibnetIPCtrlSock() {
	struct netbuf *inbuf;
	char *buf;
	u16_t buflen;
	EIBNETIP_PACKET* received_packet;
	ctrlconn = netconn_new(NETCONN_UDP);
	err_t err = netconn_bind(ctrlconn, NULL, 1690);
	if (err != ERR_OK) {
		printf("Binding for CtrlSock failed %d!!\n", err);
	}

	netconn_connect(ctrlconn, &rmip, rmport);
	netconn_set_recvtimeout(ctrlconn, 10);
	while(1){
		sendConnectRequest(ctrlconn);
		connected=true;
		while (connected) {
			vTaskDelay(50 / portTICK_PERIOD_MS);
			err = netconn_recv(ctrlconn, &inbuf);
			if (err == ERR_OK) {
				netbuf_data(inbuf, (void**) &buf, &buflen);
				received_packet = (EIBNETIP_PACKET *) buf;
				if (received_packet->head.headersize == 0x06 && received_packet->head.version == 0x10) {
					switch (htons(received_packet->head.servicetype)) {
					case (KNX_ST_CONNECT_RESPONSE): {
						EIBNETIP_CONNECT_RESPONSE* resp=(EIBNETIP_CONNECT_RESPONSE*) (received_packet->data);
						printf("Connect response with ID %d\n", resp->channelid);
						channelid = resp->channelid;
						vTaskResume(dataTask);
						xTaskCreate(&heartBeat, "heart_beat", 2048, ctrlconn, 5,&heartbeatTask);
						break;
					}
					case (KNX_ST_CONNECTIONSTATE_RESPONSE): {
						EIBNETIP_CONNECTIONSTATE_RESPONSE* resp=(EIBNETIP_CONNECTIONSTATE_RESPONSE*) received_packet->data;
						printf("Connection state response for ID%d with status %d\n", resp->channelid,resp->status);
						if(resp->status==0)xTaskNotify(heartbeatTask, 0,eSetValueWithOverwrite);
						break;
					}
					case (KNX_ST_DISCONNECT_RESPONSE):{
						EIBNETIP_DISCONNECT_RESPONSE* resp=(EIBNETIP_DISCONNECT_RESPONSE*) received_packet->data;
						printf("Got disconnect response for channel %d with status %d\n",resp->channelid, resp->status);
						break;
					}
					case (KNX_ST_DISCONNECT_REQUEST):{
						EIBNETIP_DISCONNECT_REQUEST* req=(EIBNETIP_DISCONNECT_REQUEST*) received_packet->data;
						printf("Got disconnect request for channel %d\n",req->channelid);
						break;
					}
					default:
						printf("Unknown control service type 0x%x\n",htons(received_packet->head.servicetype));
					}
				}
				netbuf_delete(inbuf);
			}
		}
		vTaskDelete( heartbeatTask );
	}
	netconn_close(ctrlconn);
}

void EibnetIPDataSock() {

	sendQ = xQueueCreate(15, sizeof(datapacket_t*));
	recvQ = xQueueCreate(15, sizeof(datapacket_t*));
	EIBNETIP_PACKET* received_packet;
	EIBNETIP_COMMON_CONNECTION_HEADER* cch;
	dataconn = netconn_new(NETCONN_UDP);
	err_t err = netconn_bind(dataconn, NULL, 1691);
	if (err != ERR_OK) {
		printf("Binding for DataSock failed %d!!\n", err);
	}
	netconn_connect(dataconn, &rmip, rmport);
	struct netbuf *inbuf;
	char *pBuffer;

	u16_t buflen;
	netconn_set_recvtimeout(dataconn, 10);
	xTaskCreate(&sendDataTask, "sendTask", 2048, NULL, 5, &sendTask);

	while (1) {
		err = netconn_recv(dataconn, &inbuf);
		if (err == ERR_OK) {
			netbuf_data(inbuf, (void**) &pBuffer, &buflen);
			received_packet = (EIBNETIP_PACKET *) pBuffer;

			switch (htons(received_packet->head.servicetype)){
			case (KNX_ST_TUNNELING_REQUEST):
				//printf("Got tunneling request:");

				cch = (EIBNETIP_COMMON_CONNECTION_HEADER *) received_packet->data;
				cemi_msg_t *cemi_msg = (cemi_msg_t *) ((received_packet->data) + 4);
				cemi_service_t *cemi_data = &cemi_msg->data.service_information;

				if (cemi_msg->additional_info_len > 0)
					cemi_data = (cemi_service_t *) (((uint8_t *) cemi_data)+ cemi_msg->additional_info_len);
				if (cch->channelid == channelid) {
					sendTunnelingAck(dataconn, cch->channelid,cch->sequencecounter, 0);
						knx_command_type_t ct = (knx_command_type_t) (((cemi_data->data[0]& 0xC0) >> 6)	| ((cemi_data->pci.apci & 0x03) << 2));

							if(cemi_msg->message_code==L_DATA_ind&&cemi_data->source.value != ntohs(OWNADD)){
								//printf("Message code is %x\n",cemi_msg->message_code);

								switch (ct) {
								case KNX_CT_READ: //Read
									printf("Ask for value of %s\n",	GAasString(htons(cemi_data->destination.value)));
									break;
								case KNX_CT_ANSWER: //Response
									printf("Response Data for %s is 0x%x 0x%x\n",GAasString(htons(cemi_data->destination.value)),cemi_data->data[0], cemi_data->data[1]);
									break;
								case KNX_CT_WRITE: //Write
									//printf("Write Data of %s is 0x%x 0x%x\n",GAasString(htons(cemi_data->destination.value)),cemi_data->data[0], cemi_data->data[1]);
									break;
								default:
									printf("Cemi type %x\n", cemi_data->pci.apci);
									break;
								}
								datapacket_t* packet = malloc(sizeof(datapacket_t));
								packet->ct=ct;
								packet->target.value = htons(cemi_data->destination.value);
								packet->len = cemi_data->data_len;
								memset(packet->data, 0, 14);
								memcpy(packet->data, cemi_data->data,cemi_data->data_len);
								if(xQueueSend(recvQ,&packet,0)!=pdPASS){
									printf("Receive queue is full!\n");free(packet);
								}

							}


				} else {
					printf("Wrong channel id %d!Should be %d\n",cch->channelid,channelid);
					vTaskSuspend( ctrlTask);
					sendDisconnectRequest(ctrlconn, cch->channelid);
					vTaskResume( ctrlTask );
				}
				break;

			case (KNX_ST_TUNNELING_ACK):

				cch =(EIBNETIP_COMMON_CONNECTION_HEADER *) received_packet->data;
				//printf("Got ack for seq# %d\n", cch->sequencecounter);
				xTaskNotify(sendTask, cch->sequencecounter,eSetValueWithOverwrite);
				break;
			default:
				printf("Unknown data service type 0x%x\n",htons(received_packet->head.servicetype));
				break;
			}
		}
		netbuf_delete(inbuf);
		vTaskDelay(10 / portTICK_PERIOD_MS);
	}
}


