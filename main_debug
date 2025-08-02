# Receptor IR basado en el código funcional de luisfelipe18
# Mejorado con debug, auto-aprendizaje y configuración flexible
# Hardware: TSOP4838 -> GPIO configurable

from machine import Pin
import time

class IRRemoteDecoder:
    def __init__(self, ir_pin=26, output_pins=None):
        self.ir_pin = Pin(ir_pin, Pin.IN)
        
        # Configuración por defecto de pines de salida
        if output_pins is None:
            output_pins = {
                "POWER": 0, "UP": 1, "DOWN": 2, 
                "LEFT": 3, "RIGHT": 4, "MENU": 5
            }
        
        # Configurar pines de salida
        self.outputs = {}
        for name, pin_num in output_pins.items():
            self.outputs[name] = Pin(pin_num, Pin.OUT)
            self.outputs[name].off()  # Asegurar que inicie en LOW
        
        # Diccionario de códigos conocidos (inicialmente vacío para auto-aprendizaje)
        self.button_codes = {
            # Códigos del control original de luisfelipe18 como ejemplo
            (0x61, 0xD6, 0x48, 0xB7): "POWER",
            (0x61, 0xD6, 0xD8, 0x27): "UP", 
            (0x61, 0xD6, 0x58, 0xA7): "DOWN",
            (0x61, 0xD6, 0x20, 0xDF): "LEFT",
            (0x61, 0xD6, 0x60, 0x9F): "RIGHT",
            (0x61, 0xD6, 0xA0, 0x5F): "MENU",
        }
        
        # Variables para modo debug y aprendizaje
        self.debug_mode = False
        self.learning_mode = False
        self.learning_button = ""
        
        print(f"📡 IR Decoder inicializado - Pin IR: {ir_pin}")
        print(f"🔌 Pines de salida configurados: {list(output_pins.keys())}")
        
    def read_pulses(self, timeout_ms=100):
        """Captura pulsos IR - método probado que funciona"""
        pulses = []
        start_time = time.ticks_ms()
        
        # Esperar señal LOW (inicio de transmisión IR)
        while self.ir_pin.value() == 1:
            if time.ticks_diff(time.ticks_ms(), start_time) > timeout_ms:
                return []
        
        # Capturar pulsos (máximo 100 cambios)
        for _ in range(100):
            t0 = time.ticks_us()
            level = self.ir_pin.value()
            
            # Esperar cambio de nivel
            while self.ir_pin.value() == level:
                if time.ticks_diff(time.ticks_us(), t0) > 10000:  # Timeout 10ms
                    return pulses
                    
            duration = time.ticks_diff(time.ticks_us(), t0)
            pulses.append(duration)
            
        return pulses
    
    def decode_nec(self, pulses):
        """Decodificación NEC - método probado que funciona"""
        if len(pulses) < 66:
            if self.debug_mode:
                print(f"❌ Pocos pulsos: {len(pulses)} (necesario: 66+)")
            return None
        
        # Verificar cabecera NEC: 9ms pulso + 4.5ms espacio
        if not (8500 < pulses[0] < 9500 and 4000 < pulses[1] < 5000):
            if self.debug_mode:
                print(f"❌ Cabecera inválida: {pulses[0]}µs, {pulses[1]}µs")
            return None
        
        # Decodificar bits
        bits = []
        for i in range(2, 66, 2):  # Saltar cabecera, procesar en pares
            if i+1 >= len(pulses):
                break
                
            space_duration = pulses[i+1]  # La duración del espacio determina el bit
            
            if 400 < space_duration < 700:
                bits.append(0)  # Bit 0
            elif 1500 < space_duration < 1800:
                bits.append(1)  # Bit 1
            else:
                if self.debug_mode:
                    print(f"❌ Bit inválido en posición {i}: {space_duration}µs")
                return None
        
        if len(bits) < 32:
            if self.debug_mode:
                print(f"❌ Bits insuficientes: {len(bits)}")
            return None
        
        # Convertir bits a bytes
        hex_code = []
        for i in range(0, 32, 8):
            byte = 0
            for bit in bits[i:i+8]:
                byte = (byte << 1) | bit
            hex_code.append(byte)
        
        return hex_code
    
    def print_debug_info(self, pulses, code):
        """Mostrar información detallada para debug"""
        print(f"\n🔍 DEBUG INFO:")
        print(f"Pulsos capturados: {len(pulses)}")
        if len(pulses) >= 2:
            print(f"Cabecera: {pulses[0]}µs, {pulses[1]}µs")
        
        if code:
            print(f"Código decodificado: {[hex(b) for b in code]}")
            print(f"Código como tupla: {tuple(code)}")
        else:
            print("❌ No se pudo decodificar")
            
        # Mostrar algunos pulsos para análisis
        if len(pulses) > 10:
            print("Primeros pulsos:", pulses[:10])
    
    def learn_button(self, button_name):
        """Activar modo aprendizaje para un botón específico"""
        if button_name not in self.outputs:
            print(f"❌ Botón desconocido: {button_name}")
            return False
            
        print(f"\n🎯 MODO APRENDIZAJE: {button_name}")
        print("Presiona el botón en el control remoto...")
        
        self.learning_mode = True
        self.learning_button = button_name
        
        # Esperar hasta 10 segundos por una señal
        start_time = time.ticks_ms()
        while self.learning_mode and time.ticks_diff(time.ticks_ms(), start_time) < 10000:
            self.process_ir_signal()
            time.sleep_ms(10)
        
        if self.learning_mode:  # Si todavía está en modo aprendizaje, falló
            print("⏰ Timeout - no se recibió señal")
            self.learning_mode = False
            return False
        
        return True
    
    def process_ir_signal(self):
        """Procesar una señal IR recibida"""
        pulses = self.read_pulses()
        if not pulses:
            return None
            
        code = self.decode_nec(pulses)
        
        if self.debug_mode:
            self.print_debug_info(pulses, code)
        
        if code:
            key = tuple(code)
            
            if self.learning_mode:
                # Guardar nuevo código
                self.button_codes[key] = self.learning_button
                print(f"✅ Código {[hex(b) for b in code]} guardado para {self.learning_button}")
                self.learning_mode = False
                return self.learning_button
                
            elif key in self.button_codes:
                # Ejecutar acción conocida
                button_name = self.button_codes[key]
                print(f"🔘 Botón detectado: {button_name}")
                self.trigger_output(button_name)
                return button_name
            else:
                # Código desconocido
                print(f"❓ Código desconocido: {[hex(b) for b in code]}")
                return "UNKNOWN"
        
        return None
    
    def trigger_output(self, button_name, duration_ms=400):
        """Activar salida por tiempo determinado"""
        if button_name in self.outputs:
            pin = self.outputs[button_name]
            pin.on()
            time.sleep_ms(duration_ms)
            pin.off()
            print(f"⚡ Pin {button_name} activado por {duration_ms}ms")
    
    def show_learned_codes(self):
        """Mostrar todos los códigos aprendidos"""
        print(f"\n📋 CÓDIGOS GUARDADOS ({len(self.button_codes)}):")
        print("-" * 50)
        for code, button in self.button_codes.items():
            hex_code = [f"0x{b:02X}" for b in code]
            print(f"{button:8} → {', '.join(hex_code)}")
    
    def run_continuous(self):
        """Ejecutar en modo continuo"""
        print(f"\n🚀 MODO CONTINUO ACTIVADO")
        print("Presiona botones del control remoto...")
        print("Ctrl+C para salir")
        
        try:
            while True:
                self.process_ir_signal()
                time.sleep_ms(50)  # Pequeña pausa
        except KeyboardInterrupt:
            print("\n⏹️ Modo continuo detenido")

# ===== FUNCIONES DE UTILIDAD =====

def quick_test():
    """Test rápido con configuración estándar"""
    print("🧪 QUICK TEST - Control remoto estándar")
    
    # Configuración estándar
    decoder = IRRemoteDecoder(ir_pin=26)
    decoder.debug_mode = True
    
    # Menú simple
    while True:
        print(f"\n{'='*40}")
        print("OPCIONES:")
        print("1 - Escuchar señales (continuo)")
        print("2 - Aprender botón específico") 
        print("3 - Aprender todos los botones")
        print("4 - Ver códigos guardados")
        print("5 - Toggle debug mode")
        print("0 - Salir")
        
        choice = input("Opción: ").strip()
        
        if choice == "1":
            decoder.run_continuous()
            
        elif choice == "2":
            button = input("Nombre del botón: ").strip().upper()
            decoder.learn_button(button)
            
        elif choice == "3":
            buttons = ["POWER", "UP", "DOWN", "LEFT", "RIGHT", "MENU"]
            for button in buttons:
                print(f"\n➡️ Aprendiendo {button}...")
                if not decoder.learn_button(button):
                    break
            decoder.show_learned_codes()
            
        elif choice == "4":
            decoder.show_learned_codes()
            
        elif choice == "5":
            decoder.debug_mode = not decoder.debug_mode
            print(f"Debug mode: {'ON' if decoder.debug_mode else 'OFF'}")
            
        elif choice == "0":
            break
            
        else:
            print("❌ Opción inválida")

def simple_listener():
    """Versión super simple para solo escuchar"""
    print("👂 ESCUCHADOR SIMPLE")
    decoder = IRRemoteDecoder()
    decoder.run_continuous()

# EJECUTAR
if __name__ == "__main__":
    quick_test()  # Cambia por simple_listener() si prefieres algo básico
