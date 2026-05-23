import time
import sys
from pyftdi.ftdi import Ftdi
from pyftdi.gpio import GpioController

class BitBangSPI:
    def __init__(self, gpio_controller, mode=0):
        self.gpio = gpio_controller
        self.mode = mode
        self.cpol = 1 if mode in (2, 3) else 0
        self.cpha = 1 if mode in (1, 3) else 0

        # CD0 (SCK) = cpol
        # CD1 (MOSI) = 0
        # CD2 (MISO) = Input (High-Z)
        # CD3 (CSN) = 1 (Deasserted)
        # CD4 (RSTN) = Input (released / floating)
        self.dir_mask = 0x0b
        self.out_val = 0x08  # CSN high
        if self.cpol:
            self.out_val |= 0x01 # SCK high
        self.gpio.set_direction(0xff, self.dir_mask)
        self.gpio.write(self.out_val)

    def set_cd4_state(self, state):
        if state == "low":
            self.dir_mask = 0x1b
            self.out_val &= ~0x10
        elif state == "high":
            self.dir_mask = 0x1b
            self.out_val |= 0x10
        else: # float/input
            self.dir_mask = 0x0b
            
        self.gpio.set_direction(0xff, self.dir_mask)
        self.gpio.write(self.out_val)

    def wakeup_via_cs(self):
        # Pull CS low (CD3 = 0)
        self.out_val &= ~0x08
        self.gpio.write(self.out_val)
        time.sleep(0.005)
        
        # Pull CS high (CD3 = 1)
        self.out_val |= 0x08
        self.gpio.write(self.out_val)
        time.sleep(0.002)

    def transfer(self, write_bytes):
        # CSN low
        self.out_val &= ~0x08
        self.gpio.write(self.out_val)
        
        read_bytes = []
        for byte in write_bytes:
            read_byte = 0
            for i in range(8):
                bit = (byte >> (7 - i)) & 1
                
                # CPHA=0: Setup MOSI before edge
                if not self.cpha:
                    if bit:
                        self.out_val |= 0x02
                    else:
                        self.out_val &= ~0x02
                    self.gpio.write(self.out_val)

                # First Edge
                if self.cpol:
                    self.out_val &= ~0x01  # Falling edge
                else:
                    self.out_val |= 0x01   # Rising edge
                self.gpio.write(self.out_val)

                # CPHA=1: Setup MOSI after first edge
                if self.cpha:
                    if bit:
                        self.out_val |= 0x02
                    else:
                        self.out_val &= ~0x02
                    self.gpio.write(self.out_val)

                # Read MISO on appropriate edge
                if not self.cpha:
                    pins = self.gpio.read()
                    miso_bit = 1 if (pins & 0x04) else 0
                    read_byte = (read_byte << 1) | miso_bit

                # Second Edge
                if self.cpol:
                    self.out_val |= 0x01   # Rising edge
                else:
                    self.out_val &= ~0x01  # Falling edge
                self.gpio.write(self.out_val)

                if self.cpha:
                    pins = self.gpio.read()
                    miso_bit = 1 if (pins & 0x04) else 0
                    read_byte = (read_byte << 1) | miso_bit
                
            read_bytes.append(read_byte)
            
        # CSN high
        self.out_val |= 0x08
        if self.cpol:
            self.out_val |= 0x01
        else:
            self.out_val &= ~0x01
        self.gpio.write(self.out_val)
        return bytes(read_bytes)

def run_test():
    devices = Ftdi.list_devices()
    if not devices:
        print("No FTDI devices found!")
        return

    ft4232_devs = []
    for dev, desc in devices:
        if dev.vid == 0x0403 and dev.pid == 0x6011:
            ft4232_devs.append(dev)

    for dev in ft4232_devs:
        if dev.sn:
            url = f"ftdi://ftdi:4232:{dev.sn}/3"
        else:
            url = f"ftdi://ftdi:4232:{dev.bus}:{dev.address:x}/3"

        print(f"\n==========================================")
        print(f"Sweeping SPI Modes on {url}")
        print(f"==========================================")
        gpio_ctrl = GpioController()
        try:
            gpio_ctrl.configure(url, direction=0x0b)
        except Exception as e:
            print(f"Failed to configure {url}: {e}")
            continue

        try:
            # Try with CD4 float, low, and high
            for cd4_state in ["float", "low"]:
                print(f"\n--- CD4 State: {cd4_state.upper()} ---")
                
                for mode in [0, 3]: # Mode 0 and Mode 3 are the standard ones
                    spi = BitBangSPI(gpio_ctrl, mode=mode)
                    spi.set_cd4_state(cd4_state)
                    time.sleep(0.02)
                    spi.wakeup_via_cs()
                    
                    # Test Method 1: EAMRW (2-byte header: 0x40, 0x00)
                    header_eam = [0x40, 0x00, 0x00, 0x00, 0x00, 0x00]
                    res_eam = spi.transfer(header_eam)
                    dev_id_eam = int.from_bytes(res_eam[2:], byteorder='little')
                    
                    # Test Method 2: FACRW (1-byte header: 0x00)
                    header_fac = [0x00, 0x00, 0x00, 0x00, 0x00]
                    res_fac = spi.transfer(header_fac)
                    dev_id_fac = int.from_bytes(res_fac[1:], byteorder='little')
                    
                    print(f"  Mode {mode}:")
                    print(f"    EAMRW (0x40 0x00): ID = {hex(dev_id_eam)} (Bytes: {list(res_eam)})")
                    print(f"    FACRW (0x00):      ID = {hex(dev_id_fac)} (Bytes: {list(res_fac)})")
                    
                    if dev_id_eam in (0xDECA0302, 0xDECA0312) or dev_id_fac in (0xDECA0302, 0xDECA0312):
                        print(f"  >>> SUCCESS: DW3000 detected! <<<")
                        return

        except Exception as e:
            print(f"Error: {e}")
        finally:
            gpio_ctrl.close()

if __name__ == "__main__":
    run_test()
