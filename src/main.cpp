#include <Arduino.h> 
#include <Rfid134.h> 
#include <NimBLEDevice.h> 
#include <pb_encode.h> 
#include "cultivai.pb.h" 
#include "esp_wifi.h" 
#include "esp_pm.h" 
#include "esp_sleep.h"
#include "esp_bt.h"

// --- CONFIGURACIÓN DE PINES Y UUIDS --- 
#define WL134_RX 20 
#define WL134_TX 21 
#define WL134_EN 7       
#define IDLE_TIMEOUT 10000 

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b" 
#define CHAR_UUID    "beb5483e-36e1-4688-b7f5-ea07361b26a8" 

// --- VARIABLES GLOBALES ---
NimBLECharacteristic* pCharacteristic = nullptr; 
unsigned long lastActivity = 0;
bool isHardwareOn = false;
esp_pm_lock_handle_t cpuLock = nullptr; 

// --- GESTIÓN DE ENERGÍA ---
void setupPowerManagement() {
    esp_pm_config_esp32c3_t pm_config = {
        .max_freq_mhz = 80, 
        .min_freq_mhz = 10, 
        .light_sleep_enable = true 
    };
    esp_pm_configure(&pm_config);
    esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "rfid_active", &cpuLock);
}

void setHardwareState(bool on) {
    if (on) {
        Serial.println("[PWR] >> HARDWARE DESPIERTO");
        digitalWrite(WL134_EN, LOW); 
        isHardwareOn = true;
        lastActivity = millis();
        if (cpuLock) esp_pm_lock_acquire(cpuLock); 
    } else {
        Serial.println("[PWR] >> HARDWARE DORMIDO (Ahorro)");
        digitalWrite(WL134_EN, HIGH); 
        isHardwareOn = false;
        if (cpuLock) esp_pm_lock_release(cpuLock); 
    }
}

// --- CALLBACK PARA PROTOBUF --- 
bool encode_string_fn(pb_ostream_t *stream, const pb_field_t *field, void * const *arg) { 
    const char *str = (const char*)(*arg); 
    if (!pb_encode_tag_for_field(stream, field)) return false; 
    return pb_encode_string(stream, (const pb_byte_t*)str, strlen(str)); 
} 

void sendTagToApp(const char* tagId) {
    uint8_t buffer[128]; 
    AnimalData message = AnimalData_init_zero; 
    message.tag_id.funcs.encode = &encode_string_fn; 
    message.tag_id.arg = (void*)tagId; 
    message.battery_level = 88; 
    message.scanning = false; 

    pb_ostream_t ostream = pb_ostream_from_buffer(buffer, sizeof(buffer)); 
    if (pb_encode(&ostream, AnimalData_fields, &message)) { 
        if (pCharacteristic) { 
            pCharacteristic->setValue(buffer, ostream.bytes_written); 
            pCharacteristic->notify(); 
            Serial.printf("[BLE] >> Enviado a App: %s\n", tagId);
            setHardwareState(false); 
        } 
    }
}

// --- CALLBACKS DE RFID --- 
class RfidNotify { 
public: 
    static void OnError(Rfid134_Error errorCode) { 
        Serial.printf("[RFID] Error detectado: %d\n", errorCode); 
    } 

    static void OnPacketRead(const Rfid134Reading& reading) { 
        char tagBuffer[16]; 
        sprintf(tagBuffer, "%03u%06lu%06lu", reading.country, 
                static_cast<uint32_t>(reading.id / 1000000), 
                static_cast<uint32_t>(reading.id % 1000000)); 

        Serial.printf("[RFID] >> TAG: %s\n", tagBuffer); 
        sendTagToApp(tagBuffer);
    } 
}; 

Rfid134<HardwareSerial, RfidNotify> rfid(Serial1); 

// --- CALLBACKS DE BLE --- 
class MyCallbacks : public NimBLECharacteristicCallbacks { 
    void onWrite(NimBLECharacteristic* pChar) { 
        if (pChar->getValue().length() > 0) {
            uint8_t cmd = pChar->getValue().data()[0];
            if (cmd == 0x01) { 
                setHardwareState(true);
                // Simulación activa para pruebas
                delay(1000); 
                sendTagToApp("REG-1774226579067"); 
            }
        }
    } 
}; 

void setup() { 
    Serial.begin(115200); 
    pinMode(WL134_EN, OUTPUT); 
    setHardwareState(false);
     
    esp_wifi_stop(); 
    esp_wifi_deinit();

    setupPowerManagement();

    Serial1.begin(9600, SERIAL_8N1, WL134_RX, WL134_TX); 
    rfid.begin(); 

    uint8_t mac[6]; 
    esp_read_mac(mac, ESP_MAC_WIFI_STA); 
    char deviceName[20]; 
    sprintf(deviceName, "CultivAI_%02X%02X", mac[4], mac[5]); 

    NimBLEDevice::init(deviceName); 
    NimBLEServer* pServer = NimBLEDevice::createServer(); 
    NimBLEService* pService = pServer->createService(SERVICE_UUID); 
     
    pCharacteristic = pService->createCharacteristic( 
        CHAR_UUID, 
        NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::READ 
    ); 
     
    pCharacteristic->setCallbacks(new MyCallbacks()); 
    pService->start(); 

    NimBLEAdvertising* pAdvertising = pServer->getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setMinInterval(1600); 
    pAdvertising->setMaxInterval(3200); 
    pAdvertising->start(); 

    Serial.printf("[SYS] Lector %s listo.\n", deviceName); 
} 

void loop() { 
    if (isHardwareOn) {
        rfid.loop(); 
        if (millis() - lastActivity > IDLE_TIMEOUT) {
            setHardwareState(false);
        }
    }
    delay(100); 
}
 