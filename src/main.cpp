#include <Arduino.h>
#include <NimBLEDevice.h>
#include <pb_encode.h>
#include "cultivai.pb.h"
#include "esp_wifi.h"

// --- CONFIGURACIÓN ---
#define WL134_EN 7       
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR_UUID    "beb5483e-36e1-4688-b7f5-ea07361b26a8"

NimBLECharacteristic* pCharacteristic = nullptr;
bool toggleAnimal = false; // Para alternar entre Conejo y Ovino

// --- FUNCIÓN PARA ENVIAR DATOS REALES DE TU DB ---
void sendDatabaseSimulatedData() {
    uint8_t buffer[128];
    AnimalData message = AnimalData_init_zero;
    const char* currentTag;

    // Alternar datos basados en tu tabla
    if (!toggleAnimal) {
        // Datos del Conejo (ID de tu DB)
        currentTag = "REG-1774226204490";
        Serial.println("[DEBUG] Simulando lectura: CONEJO (Engorde)");
    } else {
        // Datos del Ovino (ID de tu DB)
        currentTag = "REG-1774226579067";
        Serial.println("[DEBUG] Simulando lectura: OVINO (Reproductor)");
    }

    // Configurar callback de Protobuf para el string
    message.tag_id.funcs.encode = [](pb_ostream_t *stream, const pb_field_t *field, void * const *arg) -> bool {
        const char *str = (const char*)(*arg);
        if (!pb_encode_tag_for_field(stream, field)) return false;
        return pb_encode_string(stream, (const pb_byte_t*)str, strlen(str));
    };
    
    message.tag_id.arg = (void*)currentTag;
    message.battery_level = 95; 
    message.scanning = true;

    pb_ostream_t ostream = pb_ostream_from_buffer(buffer, sizeof(buffer));
    if (pb_encode(&ostream, AnimalData_fields, &message)) {
        if (pCharacteristic) {
            pCharacteristic->setValue(buffer, ostream.bytes_written);
            pCharacteristic->notify();
            Serial.printf("[BLE] Enviado a App: %s\n", currentTag);
        }
    }

    toggleAnimal = !toggleAnimal; // Cambiar para la siguiente lectura
}

// --- CALLBACK DE ESCRITURA (Reacción a wakeReader de Flutter) ---
class MyCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar) {
        if (pChar->getValue().length() > 0) {
            Serial.println("\n[APP] Comando wakeReader detectado.");
            
            // Simulación de hardware
            digitalWrite(WL134_EN, LOW); 
            delay(800); // Tiempo de "escaneo"
            
            sendDatabaseSimulatedData();
            
            digitalWrite(WL134_EN, HIGH);
            Serial.println("[ESP32] Ciclo de lectura terminado.");
        }
    }
};

void setup() {
    Serial.begin(115200);
    pinMode(WL134_EN, OUTPUT);
    digitalWrite(WL134_EN, HIGH); 
    
    esp_wifi_stop(); 

    // Nombre con MAC para el escaneo de Flutter
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
    pServer->getAdvertising()->start();

    Serial.println("--- MODO SIMULACIÓN DB ACTIVO ---");
    Serial.println("Cada vez que presiones leer en la App, el animal cambiará.");
}

void loop() {
    delay(1000);
}