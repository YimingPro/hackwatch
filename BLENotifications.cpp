#include <Arduino.h>
#include "BLEDevice.h"
#include "BLEServer.h"
#include "BLEClient.h"
#include "BLEUtils.h"
#include "BLE2902.h"
#include <esp_log.h>
#include <esp_bt_main.h>
#include <string>
#include "Task.h"
#include <sys/time.h>
#include <time.h>
#include "sdkconfig.h"

#include "BLENotifications.h"

// JAMES this is working as of 27/2/2019
// Just grab the fresh ESP32 BLE libraries from Arduino library manager

// BLE libraries are really large, so you need to increase partition sizes on your ESP32

// You need to patch the BLE library with setServiceSolicitationData as shown in:
//https://github.com/nkolban/esp32-snippets/issues/430#issuecomment-391491904

// Based largely on the ANCS work of https://github.com/S-March

static char LOG_TAG[] = "SampleServer";

static BLEUUID ancsServiceUUID("7905F431-B5CE-4E99-A40F-4B1E122D00D0");
static BLEUUID notificationSourceCharacteristicUUID("9FBF120D-6301-42D9-8C58-25E699A21DBD");
static BLEUUID controlPointCharacteristicUUID("69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9");
static BLEUUID dataSourceCharacteristicUUID("22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB");

uint8_t latestMessageID[4];
boolean pendingNotification = false;
void (*onMsgReceived)(char*);

class MySecurity : public BLESecurityCallbacks {

    uint32_t onPassKeyRequest(){
        ESP_LOGI(LOG_TAG, "PassKeyRequest");
        return 123456;
    }

    void onPassKeyNotify(uint32_t pass_key){
        ESP_LOGI(LOG_TAG, "On passkey Notify number:%d", pass_key);
    }

    bool onSecurityRequest(){
        ESP_LOGI(LOG_TAG, "On Security Request");
        return true;
    }
    
    bool onConfirmPIN(unsigned int){
        ESP_LOGI(LOG_TAG, "On Confrimed Pin Request");
        return true;
    }

    void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl){
        ESP_LOGI(LOG_TAG, "Starting BLE work!");
        if(cmpl.success){
            uint16_t length;
            esp_ble_gap_get_whitelist_size(&length);
            ESP_LOGD(LOG_TAG, "size: %d", length);
        }
    }
};

#define MESSAGE_BODY 3

static void dataSourceNotifyCallback(
  BLERemoteCharacteristic* pDataSourceCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
    Serial.print("Notify callback for characteristic ");
    Serial.print(pDataSourceCharacteristic->getUUID().toString().c_str());
    Serial.print(" of data length ");
    Serial.println(length);
    for(int i = 0; i < length; i++){
        if(i > 7){
            Serial.write(pData[i]);
        }
        else{
            Serial.print(pData[i], HEX);
            Serial.print(" ");
        }
    }
    Serial.println();
	if (pData[5] == MESSAGE_BODY && onMsgReceived) {
		pData[length]=0;
		onMsgReceived((char*)pData+8);
	}
}

static void NotificationSourceNotifyCallback(
  BLERemoteCharacteristic* pNotificationSourceCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify)
{
    if(pData[0]==0)
    {
      
        Serial.println("New notification!");
        //Serial.println(pNotificationSourceCharacteristic->getUUID().toString().c_str());
        latestMessageID[0] = pData[4];
        latestMessageID[1] = pData[5];
        latestMessageID[2] = pData[6];
        latestMessageID[3] = pData[7];
        
        switch(pData[2])
        {
            case 0:
                Serial.println("Category: Other");
            break;
            case 1:
                Serial.println("Category: Incoming call");
            break;
            case 2:
                Serial.println("Category: Missed call");
            break;
            case 3:
                Serial.println("Category: Voicemail");
            break;
            case 4:
                Serial.println("Category: Social");
            break;
            case 5:
                Serial.println("Category: Schedule");
            break;
            case 6:
                Serial.println("Category: Email");
            break;
            case 7:
                Serial.println("Category: News");
            break;
            case 8:
                Serial.println("Category: Health");
            break;
            case 9:
                Serial.println("Category: Business");
            break;
            case 10:
                Serial.println("Category: Location");
            break;
            case 11:
                Serial.println("Category: Entertainment");
            break;
            default:
            break;
        }
    }
    pendingNotification = true;
}

/**
 * Become a BLE client to a remote BLE server.  We are passed in the address of the BLE server
 * as the input parameter when the task is created.
 */
class MyClient: public Task {
    void run(void* data) {

        BLEAddress* pAddress = (BLEAddress*)data;
        BLEClient*  pClient  = BLEDevice::createClient();
        BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
        BLEDevice::setSecurityCallbacks(new MySecurity());

        BLESecurity *pSecurity = new BLESecurity();
        pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
        pSecurity->setCapability(ESP_IO_CAP_IO);
        pSecurity->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
        // Connect to the remove BLE Server.
        pClient->connect(*pAddress);

        /** BEGIN ANCS SERVICE **/
        // Obtain a reference to the service we are after in the remote BLE server.
        BLERemoteService* pAncsService = pClient->getService(ancsServiceUUID);
        if (pAncsService == nullptr) {
            ESP_LOGD(LOG_TAG, "Failed to find our service UUID: %s", ancsServiceUUID.toString().c_str());
            return;
        }
        // Obtain a reference to the characteristic in the service of the remote BLE server.
        BLERemoteCharacteristic* pNotificationSourceCharacteristic = pAncsService->getCharacteristic(notificationSourceCharacteristicUUID);
        if (pNotificationSourceCharacteristic == nullptr) {
            ESP_LOGD(LOG_TAG, "Failed to find our characteristic UUID: %s", notificationSourceCharacteristicUUID.toString().c_str());
            return;
        }        
        // Obtain a reference to the characteristic in the service of the remote BLE server.
        BLERemoteCharacteristic* pControlPointCharacteristic = pAncsService->getCharacteristic(controlPointCharacteristicUUID);
        if (pControlPointCharacteristic == nullptr) {
            ESP_LOGD(LOG_TAG, "Failed to find our characteristic UUID: %s", controlPointCharacteristicUUID.toString().c_str());
            return;
        }        
        // Obtain a reference to the characteristic in the service of the remote BLE server.
        BLERemoteCharacteristic* pDataSourceCharacteristic = pAncsService->getCharacteristic(dataSourceCharacteristicUUID);
        if (pDataSourceCharacteristic == nullptr) {
            ESP_LOGD(LOG_TAG, "Failed to find our characteristic UUID: %s", dataSourceCharacteristicUUID.toString().c_str());
            return;
        }        
        const uint8_t v[]={0x1,0x0};
        pDataSourceCharacteristic->registerForNotify(dataSourceNotifyCallback);
        pDataSourceCharacteristic->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t*)v,2,true);
        pNotificationSourceCharacteristic->registerForNotify(NotificationSourceNotifyCallback);
        pNotificationSourceCharacteristic->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t*)v,2,true);
        /** END ANCS SERVICE **/
        
        delay(1000);

        while(1){
            if(pendingNotification == true){
                // CommandID: CommandIDGetNotificationAttributes
                // 32bit uid
                // AttributeID
                Serial.println("Requesting details...");
                const uint8_t vIdentifier[]={0x0,   latestMessageID[0],latestMessageID[1],latestMessageID[2],latestMessageID[3],   0x0};
                pControlPointCharacteristic->writeValue((uint8_t*)vIdentifier,6,true);
                const uint8_t vTitle[]={0x0,   latestMessageID[0],latestMessageID[1],latestMessageID[2],latestMessageID[3],   0x1, 0x0, 0x10};
                pControlPointCharacteristic->writeValue((uint8_t*)vTitle,8,true);
                const uint8_t vMessage[]={0x0,   latestMessageID[0],latestMessageID[1],latestMessageID[2],latestMessageID[3],   0x3, 0x0, 0x10};
                pControlPointCharacteristic->writeValue((uint8_t*)vMessage,8,true);
                const uint8_t vDate[]={0x0,   latestMessageID[0],latestMessageID[1],latestMessageID[2],latestMessageID[3],   0x5};
                pControlPointCharacteristic->writeValue((uint8_t*)vDate,6,true);
                pendingNotification = false;
            }
            delay(100); //does not work without small delay
        }
        
        
        
    
    } // run
}; // MyClient

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        Serial.println("********************");
        Serial.println("**Device connected**");
        Serial.println(BLEAddress(BLEDevice::m_remoteBda).toString().c_str());
        Serial.println("********************");
        MyClient* pMyClient = new MyClient();
        pMyClient->setStackSize(18000);
        pMyClient->start(new BLEAddress(BLEDevice::m_remoteBda));
    };

void onDisconnect(BLEServer* pServer) {
        Serial.println("************************");
        Serial.println("**Device  disconnected**");
        Serial.println("************************");
    }
};

class MainBLEServer: public Task {
    void run(void *data) {
        ESP_LOGD(LOG_TAG, "Starting BLE work!");
        esp_log_buffer_char(LOG_TAG, LOG_TAG, sizeof(LOG_TAG));
        esp_log_buffer_hex(LOG_TAG, LOG_TAG, sizeof(LOG_TAG));

        // Initialize device
        BLEDevice::init("Hackwatch");
        BLEServer* pServer = BLEDevice::createServer();
        pServer->setCallbacks(new MyServerCallbacks());
        BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
        BLEDevice::setSecurityCallbacks(new MySecurity());

        // Advertising parameters:
        // Soliciting ANCS
        BLEAdvertising *pAdvertising = pServer->getAdvertising();
        BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();
        oAdvertisementData.setFlags(0x01);
        oAdvertisementData.setServiceSolicitation(BLEUUID("7905F431-B5CE-4E99-A40F-4B1E122D00D0"));
        pAdvertising->setAdvertisementData(oAdvertisementData);        

        // Set security
        BLESecurity *pSecurity = new BLESecurity();
        pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
        pSecurity->setCapability(ESP_IO_CAP_OUT);
        pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

        //Start advertising
        pAdvertising->start();
        
        ESP_LOGD(LOG_TAG, "Advertising started!");
        delay(portMAX_DELAY);
    }
};

void BLENotifications::beginListening(void (*msgCallback)(char*))
{
    MainBLEServer* pMainBleServer = new MainBLEServer();
    pMainBleServer->setStackSize(20000);
    pMainBleServer->start();
	
	onMsgReceived = msgCallback;
}


