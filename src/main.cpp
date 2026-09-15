#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <cstring>

// Entradas analogicas ADC1 do ESP32 (resolucao de 12 bits: 0 a 4095).
const int pinoLdr = 34;
const int pinoPotenciometro = 35;
const int pinoLed = 2;
const unsigned long intervaloLeitura = 15000;
const unsigned long intervaloAmostragem = 250;
const unsigned long intervaloReconexaoMqtt = 5000;
const int variacaoAltaLuminosidade = 15;
const int variacaoAltaTemperatura = 5;
const int larguraOled = 128;
const int alturaOled = 64;

WiFiClient wifi;
PubSubClient mqtt(wifi);
Adafruit_SSD1306 oled(larguraOled, alturaOled, &Wire, -1);
bool oledDisponivel = false;
const char *statusAnterior = "";
unsigned long ultimaLeitura = 0;
unsigned long ultimaAmostragem = 0;
unsigned long inicioPulsoLed = 0;
unsigned long ultimaTentativaMqtt = 0;
bool pulsoLedAtivo = false;
int ultimaLuminosidadeExibida = -1;
int ultimaTemperaturaExibida = -1;

void conectarWifi() {
  // WiFi.begin inicia a conexao de forma assincrona. Nao esperamos aqui,
  // pois o painel precisa continuar funcionando mesmo sem internet.
  WiFi.mode(WIFI_STA);
  WiFi.begin("Wokwi-GUEST", "");
}

bool conectarMqtt() {
  char clientId[32];
  uint64_t chipId = ESP.getEfuseMac();
  snprintf(clientId, sizeof(clientId), "AmbientIQ-%08X",
           static_cast<uint32_t>(chipId));

  // Uma unica tentativa evita travar o monitor caso o broker esteja fora.
  return mqtt.connect(clientId);
}

void formatarBarra(char *destino, int valor, int maximo) {
  const int largura = 16;
  int preenchimento = constrain(map(valor, 0, maximo, 0, largura), 0, largura);

  destino[0] = '[';
  for (int i = 0; i < largura; i++) {
    destino[i + 1] = i < preenchimento ? '#' : '-';
  }
  destino[largura + 1] = ']';
  destino[largura + 2] = '\0';
}

void imprimirLinhaPainel(const char *texto) {
  const int larguraConteudo = 56;
  int tamanho = strlen(texto);

  Serial.print("| ");
  Serial.print(texto);
  for (int i = tamanho; i < larguraConteudo; i++) {
    Serial.print(' ');
  }
  Serial.println(" |");
}

void imprimirLinhaCentralizada(const char *texto) {
  const int larguraConteudo = 56;
  int tamanho = strlen(texto);
  int espacosEsquerda = (larguraConteudo - tamanho) / 2;
  int espacosDireita = larguraConteudo - tamanho - espacosEsquerda;

  Serial.print("| ");
  for (int i = 0; i < espacosEsquerda; i++) {
    Serial.print(' ');
  }
  Serial.print(texto);
  for (int i = 0; i < espacosDireita; i++) {
    Serial.print(' ');
  }
  Serial.println(" |");
}

void atualizarOled(int luminosidade, int temperatura, int indiceRisco,
                   const char *statusAmbiente) {
  if (!oledDisponivel) {
    return;
  }

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println("AMBIENTIQ   GALPAO 01");
  oled.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  oled.setCursor(0, 14);
  oled.printf("LUZ  %3d%%  TEMP %2dC\n", luminosidade, temperatura);
  oled.printf("RISCO %3d / 100\n", indiceRisco);
  oled.printf("MQTT  %s", mqtt.connected() ? "ONLINE" : "OFFLINE");
  oled.fillRect(0, 48, 128, 16, SSD1306_WHITE);
  oled.setTextSize(2);
  oled.setTextColor(SSD1306_BLACK);
  int posicaoStatus = (128 - (strlen(statusAmbiente) * 12)) / 2;
  oled.setCursor(posicaoStatus, 49);
  oled.print(statusAmbiente);
  oled.display();
}

void imprimirPainel(int leituraLdr, int leituraPotenciometro, int luminosidade,
                    int temperatura, int indiceRisco,
                    const char *statusAmbiente, bool publicacaoOk,
                    bool statusMudou) {
  // Bloco convencional: funciona tanto no terminal do Wokwi quanto no
  // monitor do PlatformIO no Windows, sem depender de comandos ANSI.
  char linha[80];
  char barraLuminosidade[19];
  char barraTemperatura[19];
  formatarBarra(barraLuminosidade, luminosidade, 100);
  formatarBarra(barraTemperatura, temperatura, 50);

  Serial.println();
  Serial.println("+----------------------------------------------------------+");
  imprimirLinhaCentralizada("AMBIENTIQ | GALPAO 01");
  imprimirLinhaCentralizada("PAINEL DE MONITORAMENTO AMBIENTAL");
  Serial.println("+----------------------------------------------------------+");
  snprintf(linha, sizeof(linha), "REDE     Wi-Fi: %-10s   MQTT: %-10s",
           WiFi.status() == WL_CONNECTED ? "CONECTADO" : "OFFLINE",
           mqtt.connected() ? "CONECTADO" : "OFFLINE");
  imprimirLinhaPainel(linha);

  snprintf(linha, sizeof(linha), "LUZ      %s  %3d%%   ADC: %-4d",
           barraLuminosidade, luminosidade, leituraLdr);
  imprimirLinhaPainel(linha);

  snprintf(linha, sizeof(linha), "TEMP     %s  %3d C  ADC: %-4d",
           barraTemperatura, temperatura, leituraPotenciometro);
  imprimirLinhaPainel(linha);

  snprintf(linha, sizeof(linha), "RISCO    %3d / 100", indiceRisco);
  imprimirLinhaPainel(linha);

  snprintf(linha, sizeof(linha), "STATUS   [ %-8s ]       LED: %-8s",
           statusAmbiente,
           strcmp(statusAmbiente, "IDEAL") == 0 ? "APAGADO" : "ATIVO");
  imprimirLinhaPainel(linha);

  snprintf(linha, sizeof(linha),
           "ENVIO    MQTT: %-8s       PROXIMA LEITURA: 15 s",
           publicacaoOk ? "SUCESSO" : "FALHOU");
  imprimirLinhaPainel(linha);
  Serial.println("+----------------------------------------------------------+");

  if (statusMudou) {
    Serial.println("  ALERTA: a classificacao do ambiente mudou!");
  }
}

int calcularIndiceRisco(int temperatura, int luminosidade) {
  int indice = 0;

  if (temperatura < 10 || temperatura > 35) {
    indice = indice + 70;
  } else if (temperatura < 15 || temperatura > 30) {
    indice = indice + 50;
  }

  if (luminosidade < 10) {
    indice = indice + 30;
  } else if (luminosidade < 20) {
    indice = indice + 20;
  }

  return indice;
}

const char *classificarAmbiente(int indiceRisco) {
  if (indiceRisco >= 70) {
    return "CRITICO";
  }

  if (indiceRisco > 0) {
    return "ATENCAO";
  }

  return "IDEAL";
}

void atualizarLed(const char *statusAmbiente) {
  // IDEAL: apagado | ATENCAO: pulso de 300 ms | CRITICO: aceso.
  if (strcmp(statusAmbiente, "IDEAL") == 0) {
    digitalWrite(pinoLed, LOW);
    pulsoLedAtivo = false;
  } else if (strcmp(statusAmbiente, "ATENCAO") == 0) {
    digitalWrite(pinoLed, HIGH);
    inicioPulsoLed = millis();
    pulsoLedAtivo = true;
  } else {
    digitalWrite(pinoLed, HIGH);
    pulsoLedAtivo = false;
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== AmbientIQ iniciado ===");
  Serial.println("Serial funcionando em 115200 baud.");

  pinMode(pinoLed, OUTPUT);
  analogReadResolution(12);
  Wire.begin(21, 22);
  oledDisponivel = oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  if (oledDisponivel) {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setCursor(18, 25);
    oled.println("AMBIENTIQ");
    oled.display();
  }

  conectarWifi();
  mqtt.setServer("broker.hivemq.com", 1883);
  // Faz a primeira leitura assim que o loop iniciar.
  ultimaLeitura = millis() - intervaloLeitura;
}

void loop() {
  if (mqtt.connected()) {
    mqtt.loop();
  }

  unsigned long agora = millis();
  if (pulsoLedAtivo && agora - inicioPulsoLed >= 300) {
    digitalWrite(pinoLed, LOW);
    pulsoLedAtivo = false;
  }

  // Mantem as tentativas de conexao independentes da atualizacao do painel.
  if (WiFi.status() == WL_CONNECTED && !mqtt.connected() &&
      agora - ultimaTentativaMqtt >= intervaloReconexaoMqtt) {
    ultimaTentativaMqtt = agora;
    conectarMqtt();
  }

  // Amostra rapidamente para perceber mudancas grandes sem esperar 15 s.
  if (agora - ultimaAmostragem < intervaloAmostragem) {
    return;
  }
  ultimaAmostragem = agora;

  int leituraLdr = analogRead(pinoLdr);
  int leituraPotenciometro = analogRead(pinoPotenciometro);

  // No modulo LDR do Wokwi, mais luz produz uma leitura ADC menor.
  int luminosidade = map(leituraLdr, 0, 4095, 100, 0);
  int temperatura = map(leituraPotenciometro, 0, 4095, 0, 50);

  // Temperatura representa 70 pontos e luminosidade representa 30 pontos.
  int indiceRisco = calcularIndiceRisco(temperatura, luminosidade);
  const char *statusAmbiente = classificarAmbiente(indiceRisco);

  bool primeiraLeitura = ultimaLuminosidadeExibida < 0;
  bool tempoDeAtualizar = agora - ultimaLeitura >= intervaloLeitura;
  bool variacaoAlta = !primeiraLeitura &&
                      (abs(luminosidade - ultimaLuminosidadeExibida) >=
                           variacaoAltaLuminosidade ||
                       abs(temperatura - ultimaTemperaturaExibida) >=
                           variacaoAltaTemperatura);
  bool statusMudou = strcmp(statusAnterior, "") != 0 &&
                     strcmp(statusAmbiente, statusAnterior) != 0;

  if (!primeiraLeitura && !tempoDeAtualizar && !variacaoAlta &&
      !statusMudou) {
    return;
  }
  ultimaLeitura = agora;

  // Vetores de caracteres evitam fragmentacao de memoria causada por String.
  char textoLuminosidade[8];
  char textoTemperatura[8];
  char textoIndice[8];

  dtostrf(luminosidade, 1, 0, textoLuminosidade);
  dtostrf(temperatura, 1, 0, textoTemperatura);
  dtostrf(indiceRisco, 1, 0, textoIndice);

  bool publicacaoOk = mqtt.connected();
  publicacaoOk &= mqtt.publish("ambientiq/galpao01/luminosidade", textoLuminosidade);
  publicacaoOk &= mqtt.publish("ambientiq/galpao01/temperatura", textoTemperatura);
  publicacaoOk &= mqtt.publish("ambientiq/galpao01/indice-risco", textoIndice);
  publicacaoOk &= mqtt.publish("ambientiq/galpao01/status", statusAmbiente);

  // Publica um alerta somente quando a classificacao mudar.
  if (statusMudou && mqtt.connected()) {
    publicacaoOk &= mqtt.publish("ambientiq/galpao01/alerta", statusAmbiente);
  }

  statusAnterior = statusAmbiente;
  ultimaLuminosidadeExibida = luminosidade;
  ultimaTemperaturaExibida = temperatura;

  atualizarLed(statusAmbiente);
  atualizarOled(luminosidade, temperatura, indiceRisco, statusAmbiente);
  imprimirPainel(leituraLdr, leituraPotenciometro, luminosidade, temperatura,
                 indiceRisco, statusAmbiente, publicacaoOk, statusMudou);
  Serial.flush();

}
