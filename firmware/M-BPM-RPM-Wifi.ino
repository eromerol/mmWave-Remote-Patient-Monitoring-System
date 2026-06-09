#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "Seeed_Arduino_mmWave.h"

// ==========================================
// DEFINICIÓN RED Y MQTT
// ==========================================
const char* ssid = "Tenda_6BD1D0";       // Madrid:DIGIFIBRA-PLUS-uQ26 o Tenda_6BD1D0 Antequera: Vodafone778RRT
const char* password = "CC2D216BD1D0";     // Madrid:3PNSydcecHbs o CC2D216BD1D0 Antequera: 25325803C

const char* mqtt_server = "192.168.0.103";      // IP Raspberry Pi
const int mqtt_port = 1883;
const char* mqtt_user = "mqtt_user";          
const char* mqtt_password = "mqtt1234";

// ==========================================
// OBJETOS Y VARIABLES
// ==========================================
WiFiClient espClient;
PubSubClient client(espClient);

HardwareSerial mmWaveSerial(1);
SEEED_MR60BHA2 mmWave;

unsigned long ultimoMensaje = 0;
unsigned long tiempoUltimoEnvio = 0;
unsigned long tiempoUltimaMuestra = 0;

int contadorPulso = 0; // Cuántas lecturas válidas llevamos
int contadorResp = 0;

const int TAMAÑO_VENTANA = 5;
float bufferPulso[TAMAÑO_VENTANA];
float bufferResp[TAMAÑO_VENTANA];
int indiceBuffer = 0;
bool bufferLleno = false;

// ==========================================
// FUNCIÓN PARA CONECTAR AL WIFI
// ==========================================
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Conectando a la red WiFi: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi conectado!");
  Serial.print("Dirección IP de la placa: ");
  Serial.println(WiFi.localIP());
}

// ==========================================
// FUNCIÓN PARA CONECTAR AL BROKER MQTT
// ==========================================
void reconnect() {
  // Bucle de conexión
  while (!client.connected()) {
    Serial.print("Intentando conexión MQTT con Home Assistant...");
    
    String clientId = "SensorRadar-";
    clientId += String(random(0, 0xffff), HEX);
    
    // Usuario y contraseña MQTT
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
      Serial.println("¡Conectado a Home Assistant!");


      // Enviar a HA las instrucciones para que cree los sensores visuales
      client.publish("homeassistant/sensor/radar_pulso/config", "{\"name\": \"Pulso Paciente\", \"state_topic\": \"tfm/radar/pulso\", \"unit_of_measurement\": \"BPM\", \"icon\": \"mdi:heart-pulse\", \"unique_id\": \"radar_pulso_1\"}", true);
      
      client.publish("homeassistant/sensor/radar_resp/config", "{\"name\": \"Respiracion Paciente\", \"state_topic\": \"tfm/radar/respiracion\", \"unit_of_measurement\": \"RPM\", \"icon\": \"mdi:lungs\", \"unique_id\": \"radar_resp_1\"}", true);
      
      client.publish("homeassistant/sensor/radar_dist/config", "{\"name\": \"Distancia Radar\", \"state_topic\": \"tfm/radar/distancia\", \"unit_of_measurement\": \"cm\", \"icon\": \"mdi:ruler\", \"unique_id\": \"radar_dist_1\"}", true);

      
    } else {
      Serial.print("Fallo, rc=");
      Serial.print(client.state());
      Serial.println(" Nuevo intento en 5 segundos...");
      delay(5000);
    }
  }
}
// ==========================================
// FUNCIÓN PARA CALCULAR MEDIANA
// ==========================================
float calcularMediana(float arrayOriginal[], int tamaño) {
  float arrayCopia[tamaño];
  for(int i=0; i<tamaño; i++){
    arrayCopia[i] = arrayOriginal[i];
  }
  
  // Ordenar el array de menor a mayor
  for(int i=0; i<tamaño-1; i++) {
    for(int j=i+1; j<tamaño; j++) {
      if(arrayCopia[j] < arrayCopia[i]) {
        float temp = arrayCopia[i];
        arrayCopia[i] = arrayCopia[j];
        arrayCopia[j] = temp;
      }
    }
  }
  // Devolver el valor central
  return arrayCopia[tamaño / 2]; 
}
// ==========================================
// CONFIGURACIÓN INICIAL (SETUP)
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(2000); // Pausa para abrir el monitor serie

  // 1. Iniciamos WiFi y MQTT
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);

  // 2. Iniciamos el Radar en D7 y D6
  Serial.println("Iniciando radar MR60BHA2...");
  mmWaveSerial.begin(115200, SERIAL_8N1, D7, D6);
  mmWave.begin(&mmWaveSerial);
  
  Serial.println("Sistema 100% operativo y monitorizando.");
}

// ==========================================
// BUCLE PRINCIPAL (LOOP)
// ==========================================
void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop(); // Para conexión con Home Assistant

  // Leemos el radar constantemente
  if (mmWave.update(100)) {
    
    float breath_raw = 0.0;
    float heart_raw = 0.0;
    float distance = 0.0;

    bool leidoResp = mmWave.getBreathRate(breath_raw) && breath_raw >= 8.0 && breath_raw <= 45.0;
    bool leidoPulso = mmWave.getHeartRate(heart_raw) && heart_raw >= 40.0 && heart_raw <= 160.0;
    mmWave.getDistance(distance);

    // Capturamos datos 1 vez por segundo
    if (millis() - tiempoUltimaMuestra > 1000) {

      if (leidoPulso) {
        if (contadorPulso < TAMAÑO_VENTANA){
          bufferPulso[contadorPulso] = heart_raw;
          contadorPulso++;
        }
        
      }

      if (leidoResp) {
        if (contadorResp < TAMAÑO_VENTANA){
          bufferResp[contadorResp] = breath_raw;
          contadorResp++;
        }
      }

      tiempoUltimaMuestra = millis();
    }

    //Procesamos y enviamos cada 5 segundos
    if (millis() - tiempoUltimoEnvio >= 5000){
      if (contadorPulso > 0){
        float medianaP = calcularMediana(bufferPulso, contadorPulso);
        client.publish("tfm/radar/pulso", String(medianaP).c_str());
        Serial.printf("Enviado MQTT -> Pulso: %.2f BPM\n", medianaP);
        contadorPulso = 0;
      }

      if (contadorResp > 0){
        float medianaR = calcularMediana(bufferResp, contadorResp);
        client.publish("tfm/radar/respiracion", String(medianaR).c_str());
        Serial.printf("Enviado MQTT -> Respiración: %.2f RPM\n", medianaR);
        contadorResp = 0;
      }

      if (distance > 0.0) {
        client.publish("tfm/radar/distancia", String(distance).c_str());
      }

      tiempoUltimoEnvio = millis();
    }
  }
}