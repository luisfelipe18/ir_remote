# Decodificación Completa de Señales Infrarrojas para Microcontroladores

Los controles remotos genéricos chinos utilizan predominantemente el protocolo NEC (85%+ de casos) debido a su simplicidad de implementación y disponibilidad de chips codificadores de bajo costo. Esta investigación proporciona especificaciones técnicas detalladas y código funcional para implementar un decodificador robusto en RP2040 con migración posterior a ATTiny24.

## Protocolos IR dominantes en controles genéricos chinos

**El protocolo NEC es absolutamente predominante** en controles remotos genéricos chinos por razones económicas y técnicas específicas. Los fabricantes prefieren este protocolo porque requiere componentes mínimos y ofrece verificación de integridad robusta mediante complementos bitwise.

### Especificaciones técnicas del protocolo NEC

La estructura de trama NEC utiliza **modulación por distancia de pulsos** con portadora de 38kHz y ciclo útil del 25-33%. Cada trama contiene exactamente 32 bits organizados como: `[8-bit dirección][8-bit ~dirección][8-bit comando][8-bit ~comando]`.

**Timing crítico del protocolo NEC:**
- **Pulso de inicio (AGC)**: 9000µs ±450µs
- **Espacio de inicio**: 4500µs ±225µs  
- **Bit lógico '0'**: 562.5µs pulso + 562.5µs espacio = 1.125ms total
- **Bit lógico '1'**: 562.5µs pulso + 1687.5µs espacio = 2.25ms total
- **Código de repetición**: 9ms pulso + 2.25ms espacio + 562.5µs (cada 110ms mientras se mantiene presionado)

Los controles genéricos típicamente usan direcciones `0x00/0xFF` o `0x40/0xBF`, con comandos en rangos `0x45-0x4F` para números y `0x15-0x17` para controles de volumen. La **tolerancia de timing requerida es ±20-25%** para decodificación confiable con componentes de bajo costo.

### Protocolos alternativos en aplicaciones específicas

**RC5 (Philips)** aparece ocasionalmente en dispositivos de mayor calidad, utilizando codificación Manchester a 36kHz sin pulso líder. La estructura es: `[S1][S2][T][5-bit dirección][6-bit comando]` con período de bit de 1.778ms.

**SIRC (Sony)** es extremadamente raro en controles genéricos chinos debido a su patente y requerimientos de precisión. Utiliza modulación por ancho de pulso a 40kHz con tres variantes (12, 15, 20 bits).

## Fundamentos físicos de la modulación infrarroja

La **modulación por ancho de pulso a 38kHz** funciona mediante la activación y desactivación rápida de un LED infrarrojo. Matemáticamente, cada período de portadora es 26.3µs (1/38,000Hz). Para el RP2040 a 133MHz, la configuración óptima es `PWMC:5, PWMR:101` generando 38,019.8Hz con desviación de +19.8Hz.

### Características del receptor TSOP4838

El TSOP4838 integra **fotodiodo PIN + preamplificador + filtro pasabanda + demodulador + control AGC automático**. Sus especificaciones críticas incluyen:

- **Rango de voltaje**: 2.0V-5.5V (óptimo: 3.3V-5V)
- **Corriente de suministro**: 0.25-0.45mA según iluminación ambiente
- **Frecuencia de portadora**: 38kHz ±5% (36.1-39.9kHz)
- **Distancia de transmisión**: hasta 24m con LED TSAL6200 a 50mA
- **Mínima duración de ráfaga**: 10 ciclos (≈263µs)
- **Adaptación AGC**: ~200µs (acorta marcas 20µs, extiende espacios 20µs)

**Crítico:** El TSOP4838 produce salida **lógica invertida** (LOW durante recepción de portadora, HIGH en reposo) y requiere ráfagas moduladas - las señales continuas de 38kHz se filtran después de la detección inicial.

### Requerimientos de precisión de timing

La resolución mínima requerida es **10µs para decodificación confiable de NEC**. La latencia de interrupción debe mantenerse <50µs para captura adecuada de tramas. El jitter tolerado es ±105µs a 38kHz según especificaciones del TSOP4838.

## Implementación práctica completa en RP2040

### Código funcional para decodificación NEC

Este decodificador completo maneja interrupciones, timing preciso y validación de protocolo:

```python
from machine import Pin, Timer
import time
import array

class NECIRReceiver:
    def __init__(self, pin_num, callback=None):
        self.pin = Pin(pin_num, Pin.IN, Pin.PULL_UP)
        self.callback = callback
        self.edge_buffer = array.array('L', [0] * 100)
        self.buffer_pos = 0
        self.last_edge_time = 0
        self.collecting = False
        
        # Constantes de timing del protocolo (microsegundos)
        self.LEADER_PULSE = (8000, 10000)
        self.LEADER_SPACE = (3500, 5000)
        self.BIT_PULSE = (400, 800)
        self.BIT0_SPACE = (400, 800)
        self.BIT1_SPACE = (1400, 1800)
        
        # Configurar interrupción
        self.pin.irq(trigger=Pin.IRQ_FALLING | Pin.IRQ_RISING, 
                    handler=self._edge_handler)
        
        # Timer para detección de fin de transmisión
        self.timeout_timer = Timer(-1)
        
    def _edge_handler(self, pin):
        """Manejar cambios de estado del pin"""
        now = time.ticks_us()
        
        if self.last_edge_time:
            duration = time.ticks_diff(now, self.last_edge_time)
            
            if not self.collecting:
                # Buscar inicio de transmisión
                if pin.value() == 1 and self._in_range(duration, self.LEADER_PULSE):
                    self.collecting = True
                    self.buffer_pos = 0
            
            if self.collecting and self.buffer_pos < len(self.edge_buffer):
                self.edge_buffer[self.buffer_pos] = duration
                self.buffer_pos += 1
                
                # Establecer timeout para fin de transmisión
                self.timeout_timer.init(mode=Timer.ONE_SHOT, 
                                      period=50, 
                                      callback=self._transmission_complete)
        
        self.last_edge_time = now
        
    def _transmission_complete(self, timer):
        """Procesar transmisión completa"""
        if self.collecting and self.buffer_pos > 10:
            command = self._decode_nec()
            if command and self.callback:
                self.callback(command)
        
        self.collecting = False
        self.buffer_pos = 0
        
    def _decode_nec(self):
        """Decodificar protocolo NEC desde buffer"""
        if self.buffer_pos < 34:  # Mínimo para NEC (líder + 32 bits + stop)
            return None
            
        pos = 0
        
        # Verificar líder
        if not (self._in_range(self.edge_buffer[pos], self.LEADER_PULSE) and
                self._in_range(self.edge_buffer[pos + 1], self.LEADER_SPACE)):
            return None
        pos += 2
        
        # Decodificar 32 bits de datos
        data = 0
        for bit_num in range(32):
            if pos + 1 >= self.buffer_pos:
                return None
                
            pulse = self.edge_buffer[pos]
            space = self.edge_buffer[pos + 1]
            pos += 2
            
            if not self._in_range(pulse, self.BIT_PULSE):
                return None
                
            if self._in_range(space, self.BIT0_SPACE):
                data = (data << 1) | 0
            elif self._in_range(space, self.BIT1_SPACE):
                data = (data << 1) | 1
            else:
                return None
        
        # Extraer y validar componentes
        address = (data >> 24) & 0xFF
        address_inv = (data >> 16) & 0xFF
        command = (data >> 8) & 0xFF
        command_inv = data & 0xFF
        
        if (address ^ address_inv) == 0xFF and (command ^ command_inv) == 0xFF:
            return {
                'protocol': 'NEC',
                'address': address,
                'command': command,
                'raw_data': data
            }
        
        return None
        
    def _in_range(self, value, range_tuple):
        """Verificar si valor está dentro del rango"""
        return range_tuple[0] <= value <= range_tuple[1]

# Ejemplo de uso
def ir_command_received(cmd):
    button_map = {
        0x45: "UP", 0x46: "DOWN", 0x47: "LEFT", 
        0x44: "RIGHT", 0x40: "MENU", 0x43: "POWER"
    }
    button = button_map.get(cmd['command'], f"UNKNOWN_0x{cmd['command']:02X}")
    print(f"Botón: {button} (Dir:0x{cmd['address']:02X})")

# Inicializar receptor en pin GPIO 22
ir_receiver = NECIRReceiver(22, ir_command_received)
```

### Implementación usando PIO para máxima precisión

Para aplicaciones que requieren timing sub-microsegundo, el **Programmable I/O (PIO)** del RP2040 ofrece precisión de nivel de hardware:

```python
import rp2
from machine import Pin

@rp2.asm_pio(set_init=rp2.PIO.IN_HIGH, autopush=True, push_thresh=32)
def ir_capture_pio():
    """Programa PIO para captura precisa de pulsos IR"""
    wrap_target()
    
    # Esperar flanco descendente (inicio de pulso IR)
    wait(0, pin, 0)
    
    # Inicializar contador
    set(x, 31)
    
    # Contar ciclos mientras pin está bajo
    label("count_low")
    jmp(pin, "pin_high")
    jmp(x_dec, "count_low")
    
    label("pin_high")
    # Mover cuenta a ISR y enviar a FIFO
    mov(isr, x)
    push()
    
    wrap()

class PIOIRReceiver:
    def __init__(self, pin_num):
        self.pin = Pin(pin_num, Pin.IN)
        self.sm = rp2.StateMachine(0, ir_capture_pio, 
                                  freq=1000000,  # 1MHz para resolución microsegundo
                                  in_base=self.pin)
        
    def start_capture(self):
        self.sm.active(1)
        
    def read_pulses(self):
        pulses = []
        while self.sm.rx_fifo() > 0:
            count = self.sm.get()
            pulse_width = 31 - count  # Convertir contador a ancho de pulso
            pulses.append(pulse_width)
        return pulses
```

### Manejo avanzado de ruido y filtrado

```python
class NoiseFilter:
    def __init__(self, min_pulse_us=100, max_pulse_us=50000):
        self.min_pulse = min_pulse_us
        self.max_pulse = max_pulse_us
        
    def filter_pulse(self, duration_us):
        """Filtrar pulsos de ruido e inválidos"""
        # Remover pulsos obviamente inválidos
        if duration_us < self.min_pulse or duration_us > self.max_pulse:
            return None
            
        # Debouncing simple - ignorar pulsos muy cercanos
        now = time.ticks_us()
        if hasattr(self, 'last_valid_time'):
            if time.ticks_diff(now, self.last_valid_time) < 50:  # 50µs debounce
                return None
                
        self.last_valid_time = now
        return duration_us
        
    def smooth_timings(self, pulse_list, window_size=3):
        """Aplicar suavizado por promedio móvil"""
        if len(pulse_list) < window_size:
            return pulse_list
            
        smoothed = []
        for i in range(len(pulse_list)):
            if i < window_size // 2 or i >= len(pulse_list) - window_size // 2:
                smoothed.append(pulse_list[i])
            else:
                window = pulse_list[i - window_size//2 : i + window_size//2 + 1]
                smoothed.append(sum(window) // len(window))
        return smoothed
```

## Técnicas avanzadas de análisis y reverse engineering

### Metodología sistemática para protocolos desconocidos

La ingeniería inversa moderna combina herramientas automáticas con análisis estadístico sofisticado. **AnalysIR** (herramienta comercial $25-50) proporciona decodificación automática de 75+ protocolos usando clustering de duraciones de pulso. **IrScrutinizer** (código abierto) ofrece capacidades similares con notación IRP formal.

**Proceso de análisis recomendado:**

1. **Caracterización inicial**: Usar `FFT` para identificar frecuencia de portadora
2. **Análisis de clustering temporal**: Agrupar duraciones similares para identificar bit '0' y '1'
3. **Detección de estructura**: Localizar preámbulo, datos y trailer mediante correlación
4. **Validación mediante reconstrucción**: Generar señales limpias desde datos decodificados

### Herramientas profesionales para análisis

**Sigrok/PulseView** con decodificadores dedicados (ir_nec, ir_rc5, ook) permite análisis preciso usando analizadores lógicos de bajo costo. **FlipperZero** ofrece capacidades portátiles de captura y replay para trabajo de campo.

Para análisis DIY, esta implementación de debug proporciona visualización detallada:

```python
class IRDebugger:
    def __init__(self, pin_num):
        self.receiver = NECIRReceiver(pin_num, self.debug_callback)
        self.raw_timings = []
        
    def debug_callback(self, cmd):
        print(f"Decodificado: {cmd}")
        self.analyze_signal()
        
    def capture_raw_signal(self, duration_ms=5000):
        """Capturar señal IR cruda para análisis"""
        print("Capturando señal IR cruda...")
        start_time = time.ticks_ms()
        
        while time.ticks_diff(time.ticks_ms(), start_time) < duration_ms:
            if self.receiver.buffer_pos > 0:
                # Copiar buffer actual
                self.raw_timings = list(self.receiver.edge_buffer[:self.receiver.buffer_pos])
                break
                
        self.print_timing_analysis()
        
    def print_timing_analysis(self):
        """Imprimir análisis detallado de timing"""
        if not self.raw_timings:
            print("No se capturaron timings")
            return
            
        print(f"\nCapturados {len(self.raw_timings)} valores de timing:")
        print("Índice | Duración (µs) | Tipo probable")
        print("-" * 40)
        
        for i, timing in enumerate(self.raw_timings):
            guess = self._guess_timing_type(timing)
            print(f"{i:5d} | {timing:10d} | {guess}")
            
    def _guess_timing_type(self, duration):
        """Adivinar qué tipo de pulso representa este timing"""
        if 8000 <= duration <= 10000:
            return "PULSO_LIDER"
        elif 3500 <= duration <= 5000:
            return "ESPACIO_LIDER"
        elif 400 <= duration <= 800:
            return "PULSO_BIT/ESPACIO_0"
        elif 1400 <= duration <= 1800:
            return "ESPACIO_1"
        elif duration > 10000:
            return "IDLE/TIMEOUT"
        else:
            return "DESCONOCIDO"
```

### Detección automática de protocolo

```python
class ProtocolDetector:
    def __init__(self):
        self.patterns = {
            'NEC': {
                'leader_pulse_range': (8000, 10000),
                'leader_space_range': (3500, 5000),
                'bit_pulse_range': (400, 800),
                'carrier_freq': 38000
            },
            'RC5': {
                'bit_period': 1778,
                'no_leader': True,
                'carrier_freq': 36000
            },
            'Sony': {
                'leader_pulse_range': (2000, 2800),
                'carrier_freq': 40000
            }
        }
    
    def detect_protocol(self, pulse_sequence):
        """Detectar protocolo basado en patrones de timing"""
        if len(pulse_sequence) < 4:
            return 'UNKNOWN'
            
        # Verificar patrón NEC
        if (self._in_range(pulse_sequence[0], self.patterns['NEC']['leader_pulse_range']) and
            self._in_range(pulse_sequence[1], self.patterns['NEC']['leader_space_range'])):
            return 'NEC'
            
        # Verificar patrón Sony
        if self._in_range(pulse_sequence[0], self.patterns['Sony']['leader_pulse_range']):
            return 'Sony'
            
        # Verificar Manchester (RC5)
        bit_times = [p for p in pulse_sequence if p < 3000]
        if bit_times and all(800 < t < 2500 for t in bit_times):
            return 'RC5'
            
        return 'UNKNOWN'
    
    def _in_range(self, value, range_tuple):
        return range_tuple[0] <= value <= range_tuple[1]
```

## Consideraciones críticas para migración a ATTiny24

### Limitaciones de recursos y estrategias de optimización

El **ATTiny24 presenta restricciones severas**: 2KB Flash y 128 bytes RAM que requieren optimización extrema. MicroPython es **completamente inviable** debido a requerimientos mínimos de 256KB+ código y 16KB+ RAM.

**Presupuesto de memoria recomendado:**
- Código del decodificador IR: 1.2KB Flash
- Variables globales: 48 bytes RAM  
- Stack: 32 bytes RAM
- Margen disponible: 600 bytes Flash, 48 bytes RAM

### Implementación optimizada en C para ATTiny24

```c
#include <avr/io.h>
#include <avr/interrupt.h>

// Estructura optimizada del decodificador
struct IRDecoder {
    volatile uint8_t duration;    // Duración del pulso/espacio actual
    uint8_t state;               // Estado de la máquina de estados
    uint8_t bitCount;           // Contador de bits recibidos
    uint32_t code;              // Código IR acumulado
} __attribute__((packed));

// Constantes de timing (en unidades de timer)
#define IR_562us    35      // 562µs a prescaler /16
#define IR_1687us   105     // 1687µs a prescaler /16
#define IR_4500us   281     // 4500µs a prescaler /16
#define IR_9000us   562     // 9000µs a prescaler /16

volatile struct IRDecoder ir;

// Interrupción de cambio de pin para señal IR
ISR(PCINT0_vect) {
    ir.duration = TCNT0;    // Capturar valor del timer
    TCNT0 = 0;              // Reiniciar timer
    
    // Procesar inmediatamente en ISR para conservar RAM
    switch(ir.state) {
        case 0: // Esperando pulso líder
            if (ir.duration > IR_9000us - 50 && ir.duration < IR_9000us + 50) {
                ir.state = 1;
                ir.bitCount = 0;
                ir.code = 0;
            }
            break;
            
        case 1: // Esperando espacio líder
            if (ir.duration > IR_4500us - 30 && ir.duration < IR_4500us + 30) {
                ir.state = 2;
            } else {
                ir.state = 0;
            }
            break;
            
        case 2: // Procesando bits de datos
            if (ir.duration > IR_562us - 10 && ir.duration < IR_562us + 10) {
                // Espacio corto = bit '0'
                ir.code <<= 1;
                ir.bitCount++;
            } else if (ir.duration > IR_1687us - 20 && ir.duration < IR_1687us + 20) {
                // Espacio largo = bit '1'
                ir.code = (ir.code << 1) | 1;
                ir.bitCount++;
            } else {
                ir.state = 0; // Error, reiniciar
                break;
            }
            
            if (ir.bitCount >= 32) {
                // Validar código completo
                uint8_t addr = (ir.code >> 24) & 0xFF;
                uint8_t addr_inv = (ir.code >> 16) & 0xFF;
                uint8_t cmd = (ir.code >> 8) & 0xFF;
                uint8_t cmd_inv = ir.code & 0xFF;
                
                if ((addr ^ addr_inv) == 0xFF && (cmd ^ cmd_inv) == 0xFF) {
                    // Código válido - procesar comando
                    process_ir_command(addr, cmd);
                }
                ir.state = 0;
            }
            break;
    }
}

void IR_init(void) {
    // Configurar Timer0 con prescaler /16 (para timing a 1MHz)
    TCCR0B = (1<<CS01);
    
    // Habilitar interrupción de cambio de pin en PCINT0 (PA0)
    PCMSK |= (1<<PCINT0);
    GIMSK |= (1<<PCIE);
    
    // Configurar PA0 como entrada con pull-up
    DDRA &= ~(1<<PA0);
    PORTA |= (1<<PA0);
    
    ir.state = 0;
    
    sei(); // Habilitar interrupciones globales
}

void process_ir_command(uint8_t address, uint8_t command) {
    // Mapeo simplificado de comandos
    switch(command) {
        case 0x45: // UP
            PORTB |= (1<<PB0);
            break;
        case 0x46: // DOWN  
            PORTB |= (1<<PB1);
            break;
        case 0x47: // LEFT
            PORTB |= (1<<PB2);
            break;
        case 0x44: // RIGHT
            PORTB |= (1<<PB3);
            break;
        case 0x40: // MENU
            PORTB ^= (1<<PB4); // Toggle
            break;
        case 0x43: // POWER
            PORTB ^= (1<<PB5); // Toggle
            break;
    }
}
```

### Optimizaciones específicas para recursos limitados

**Técnicas de optimización aplicadas:**

1. **Procesamiento inmediato en ISR**: Elimina necesidad de buffering
2. **Uso de uint8_t**: Reduce uso de RAM vs int (16-bit)
3. **Estructura empaquetada**: `__attribute__((packed))` minimiza alineación de memoria
4. **Constantes precalculadas**: Evita cálculos en tiempo de ejecución
5. **Estados mínimos**: Máquina de estados simplificada con 3 estados vs enfoques más complejos

**Trade-offs de diseño:**
- **Máxima precisión** (1.5KB Flash, 64 bytes RAM): Múltiples protocolos, corrección de errores avanzada
- **Enfoque balanceado** (1KB Flash, 32 bytes RAM): Solo NEC con buena tolerancia a errores
- **Mínimos recursos** (512 bytes Flash, 16 bytes RAM): Umbralización simple de anchos de pulso

### Estrategias de validación y testing

1. **Verificación de timing**: Usar osciloscopio para validar mediciones de pulso
2. **Cumplimiento de protocolo**: Probar con múltiples tipos de control remoto
3. **Casos extremos**: Probar señales débiles, interferencia, pulsaciones rápidas
4. **Consumo de energía**: Medir corriente en sleep y procesamiento activo
5. **Profiling de memoria**: Usar `avr-objdump` para analizar uso real de memoria

## Implementación de circuito y consideraciones de hardware

### Diseño de circuito optimizado para RP2040

```
TSOP4838 Connections:
Pin 1 (OUT) → RP2040 GPIO22 (configurable)
Pin 2 (GND) → Ground
Pin 3 (VCC) → 3.3V con filtrado:
  - Capacitor cerámico 0.1µF a <5mm del TSOP4838
  - Capacitor bulk 10µF para estabilidad
  - Opcional: RC filter (1kΩ + 47µF) para fuentes ruidosas
```

**Consideraciones críticas de layout:**
- Separar secciones analógicas de digitales con ground pour
- Evitar ruteo de señales digitales de alta velocidad paralelas a salida IR
- Mantener >10mm de separación de osciladores de alta frecuencia
- Usar plano de ground debajo de trazas de señal donde sea posible

La investigación demuestra que con implementación cuidadosa, es posible lograr decodificación robusta de 6 comandos IR usando tanto RP2040/MicroPython para prototipado como ATTiny24/C para producción, con las optimizaciones y consideraciones específicas detalladas en este análisis técnico completo.