
---

# CultivAI: Lector de Ganado FDX-B Inteligente

Sistema de monitoreo de ganado basado en **ESP32-C3** y tecnología **RFID 134.2 kHz**, optimizado para operaciones de campo de larga duración (12h+) mediante gestión de energía agresiva y comunicación eficiente con **Protocol Buffers (Protobuf)**.

## 🚀 Características Principales
* **Identificación Única:** Cada lector genera su nombre Bluetooth (`CultivAI_XXXX`) basado en su dirección MAC física.
* **Gestión de Energía:** Corte total de alimentación al sensor mediante MOSFET y modo `Light Sleep` en el CPU.
* **Protocolo de Datos:** Serialización con **Nanopb** para minimizar el ancho de banda y asegurar la integridad de los datos.
* **Filtro de App:** Publicación de **UUID de Servicio** específico para vinculación rápida en Flutter.

---

## 🛠️ Lista de Componentes (BOM)

| Componente | Modelo Específico | Función |
| :--- | :--- | :--- |
| **Microcontrolador** | **ESP32-C3 Super Mini** | Cerebro del sistema, manejo de BLE y ADC. |
| **Lector RFID** | **WL-134 (134.2 kHz)** | Módulo de lectura FDX-B (ISO11784/85). |
| **Interruptor de Potencia** | **MOSFET SI2301 o AO3401** | MOSFET Canal P de nivel lógico (SOT-23). |
| **Resistencias (Divisor)** | **2x 100kΩ (1% tolerancia)** | Medición del nivel de batería (4.2V a 2.1V). |
| **Capacitor Filtro** | **Electrolítico 10µF - 16V** | Estabilización de lectura de batería y picos de corriente. |
| **Batería** | **Li-Po 3.7V (1000mAh+)** | Fuente de alimentación principal. |

---

## 📐 Arquitectura del Sistema

La arquitectura se divide en tres capas operativas para maximizar la eficiencia:

1.  **Capa de Adquisición (Hardware):** El sensor WL-134 se comunica vía UART (9600, 8N2) con el ESP32. La antena se activa solo cuando el usuario lo requiere desde la App.
2.  **Capa de Procesamiento (Firmware):** * **Estado Activo:** CPU a 160MHz, MOSFET conduciendo.
    * **Estado Idle:** Tras 20s de inactividad, el MOSFET corta el VCC del sensor y el CPU baja a 80MHz.
    * **Estado Sleep:** `esp_light_sleep_start()` suspende el CPU pero mantiene los timers de Bluetooth vivos.
3.  **Capa de Transporte (BLE):** Los datos se envían en paquetes binarios estructurados mediante la característica de notificación.



---

## 🔌 Conexiones Detalladas (Pinout)

### 1. Sensor WL-134 a ESP32-C3
* **TX (WL-134)** -> **GPIO 20 (RX1)**
* **RX (WL-134)** -> **GPIO 21 (TX1)**
* **VCC (WL-134)** -> Conectado al **DRENAJE (Drain)** del MOSFET.

### 2. Control de Energía (MOSFET Canal P)
* **FUENTE (Source)** -> **VCC Batería (3.7V - 4.2V)**
* **COMPUERTA (Gate)** -> **GPIO 7** (A través de una resistencia de 1kΩ opcional).
* **DRENAJE (Drain)** -> **VCC del WL-134**.
> *Nota: El GPIO 7 en LOW activa el paso de corriente; en HIGH lo corta.*

### 3. Divisor de Tensión (Batería)
1.  **VCC Batería** -> Resistencia 100kΩ -> **GPIO 0 (ADC)**.
2.  **GPIO 0 (ADC)** -> Resistencia 100kΩ -> **GND**.
3.  **Capacitor 10µF** -> En paralelo con la resistencia que va a GND (entre GPIO 0 y GND).



---

## 💻 Configuración de Software

### Dependencias (PlatformIO)
```ini
lib_deps = 
    h2zero/NimBLE-Arduino
    nanopb/Nanopb
    Makuna/Rfid134
```



## 🛠️ Instalación y Configuración de Protobuf (Nanopb)

Nanopb es una implementación ligera de Protocol Buffers para microcontroladores. No usaremos el `protoc` estándar de Google solo, sino el generador de Nanopb que crea archivos optimizados para el ESP32.

### 1. Instalar dependencias del sistema
Primero, asegúrate de tener el compilador base y Python instalados:
```bash
sudo apt update
sudo apt install -i protobuf-compiler python3-pip
```

### 2. Instalar Nanopb vía Pip
La forma más limpia de tener el generador es mediante un entorno virtual o directamente con pip:
```bash
pip install nanopb
```

### 3. Compilar tu archivo `cultivai.proto`
Ubícate en la terminal dentro de la carpeta donde tienes tu archivo `.proto` (normalmente la raíz de tu proyecto o una carpeta llamada `proto`). Ejecuta:

```bash
nanopb_generator cultivai.proto
```

**Si el comando anterior no funciona (error de PATH), usa este:**
```bash
python3 -m nanopb.generator cultivai.proto
```

### 4. ¿Qué archivos se generan?
Tras ejecutarlo, aparecerán dos archivos nuevos:
1.  **`cultivai.pb.h`**: Contiene las estructuras de C y las definiciones de los campos.
2.  **`cultivai.pb.c`**: Contiene la lógica de codificación/decodificación.

**⚠️ IMPORTANTE:** Debes mover estos dos archivos (`.h` y `.c`) a la carpeta **`src/`** de tu proyecto en PlatformIO para que el compilador los encuentre al subir el código al ESP32.

---

## 📋 Consideraciones de Diseño
* **Seguridad de Batería:** El divisor de tensión de 200kΩ total garantiza una fuga despreciable de ~21µA, protegiendo la vida útil de la celda incluso en desuso.
* **Estabilidad:** El código incluye un `delay(150)` después de encender el MOSFET para permitir que la antena del WL-134 estabilice su campo magnético antes de procesar datos.
* **Escalabilidad:** Al usar el UUID de servicio `4fafc201-1fb5-459e-8fcc-c5c9c331914b`, la aplicación móvil ignora cualquier otro dispositivo Bluetooth que no pertenezca al ecosistema CultivAI.

---
*CultivAI - Ingeniería aplicada a la ganadería de precisión.*