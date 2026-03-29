#include <Arduino.h>
#include <Rfid134.h>
#include <NimBLEDevice.h>
#include <pb_encode.h>
#include "cultivai.pb.h"
#include "esp_wifi.h"
#include "esp_sleep.h"

// --- CONFIGURACIÓN DE HARDWARE ---
#define WL134_RX 20
#define WL134_TX 21
#define WL134_EN 7       // Pin MOSFET (LOW=ON, HIGH=OFF)
#define BAT_ADC 0
#define INACTIVITY_TIMEOUT 20000 

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR_UUID    "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// Variables Globales
unsigned long lastActivity = 0;
bool isScannerActive = true;
NimBLECharacteristic* pCharacteristic = nullptr;

// --- CALLBACK PARA PROTOBUF STRING ---
bool encode_string_fn(pb_ostream_t *stream, const pb_field_t *field, void * const *arg) {
    const char *str = (const char*)(*arg);
    if (!pb_encode_tag_for_field(stream, field)) return false;
    return pb_encode_string(stream, (const pb_byte_t*)str, strlen(str));
}

// --- CLASE DE NOTIFICACIÓN (Ajustada a tu ejemplo) ---
class RfidNotify {
public:
    static void OnError(Rfid134_Error errorCode) {
        Serial.printf("Com Error: %d\n", errorCode);
    }

    // Usamos la estructura EXACTA de tu ejemplo: Rfid134Reading
    static void OnPacketRead(const Rfid134Reading& reading) {
        lastActivity = millis();
        char tagBuffer[16];

        // Reconstruimos el ID FDX-B de 15 dígitos (País + ID)
        // Usamos sprintf para asegurar los ceros a la izquierda
        sprintf(tagBuffer, "%03u%06lu%06lu", 
                reading.country, 
                static_cast<uint32_t>(reading.id / 1000000), 
                static_cast<uint32_t>(reading.id % 1000000));

        Serial.printf("Animal Detectado: %s\n", tagBuffer);

        // --- ENVIAR POR PROTOBUF ---
        uint8_t buffer[128];
        AnimalData message = AnimalData_init_zero;
        
        message.tag_id.funcs.encode = &encode_string_fn;
        message.tag_id.arg = tagBuffer;
        
        int raw = analogRead(BAT_ADC);
        float v = (raw * 3.3 / 4095.0) * 2.0;
        message.battery_level = (uint32_t)constrain(((v - 3.3) / (4.2 - 3.3) * 100), 0, 100);
        message.scanning = isScannerActive;

        pb_ostream_t ostream = pb_ostream_from_buffer(buffer, sizeof(buffer));
        if (pb_encode(&ostream, AnimalData_fields, &message)) {
            if (pCharacteristic) {
                pCharacteristic->setValue(buffer, ostream.bytes_written);
                pCharacteristic->notify();
            }
        }
    }
};

// Instancia usando Hardware Serial 1 (Pines 20, 21)
Rfid134<HardwareSerial, RfidNotify> rfid(Serial1);

// --- GESTIÓN DE ENERGÍA ---
void setPowerMode(bool active) {
    if (active) {
        setCpuFrequencyMhz(160);
        digitalWrite(WL134_EN, LOW); // MOSFET ON (Canal P)

        // --- CRUCIAL: TIEMPO DE ESTABILIZACIÓN ---
        delay(150); // 150ms para que la antena del WL134 se estabilice e.d los capacitores se rellenen

        isScannerActive = true;
        lastActivity = millis();
        Serial.println(">>> Lector despertando...");
    } else {
        digitalWrite(WL134_EN, HIGH); // MOSFET OFF
        setCpuFrequencyMhz(80); 
        isScannerActive = false;
        Serial.println(">>> Entrando en modo ahorro...");
    }
}

class MyCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar) {
        if (!isScannerActive) setPowerMode(true);
    }
};

void setup() {
    Serial.begin(115200);
    
    pinMode(WL134_EN, OUTPUT);
    digitalWrite(WL134_EN, LOW); // Empezar encendido
    
    esp_wifi_stop(); // Apagar WiFi para ahorrar batería

    // Configuración Serial para el WL-134 (8N2 según tu ejemplo)
    Serial1.begin(9600, SERIAL_8N2, WL134_RX, WL134_TX);
    rfid.begin();

    // --- GENERAR NOMBRE ÚNICO ---
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA); // Leemos la MAC física del chip
    char deviceName[20];
    // Usamos los últimos 2 bytes de la MAC para el sufijo (ej. CultivAI_3F2A)
    sprintf(deviceName, "CultivAI_%02X%02X", mac[4], mac[5]);

    // Setup Bluetooth
    NimBLEDevice::init(deviceName);
    Serial.printf(">>> Bluetooth iniciado como: %s\n", deviceName);
    NimBLEServer* pServer = NimBLEDevice::createServer();
    NimBLEService* pService = pServer->createService(SERVICE_UUID);
    
    pCharacteristic = pService->createCharacteristic(
        CHAR_UUID,
        NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::READ
    );
    
    pCharacteristic->setCallbacks(new MyCallbacks());
    pService->start();
    pServer->getAdvertising()->start();

    lastActivity = millis();
    Serial.println("CultivAI listo.");
}

void loop() {
    if (isScannerActive) {
        rfid.loop();
        if (millis() - lastActivity > INACTIVITY_TIMEOUT) {
            setPowerMode(false);
        }
    } else {
        // Light Sleep mantiene el Bluetooth vivo
        esp_light_sleep_start();
    }
    yield();
}