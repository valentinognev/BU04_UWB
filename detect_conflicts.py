import time
from pyftdi.ftdi import Ftdi
from pyftdi.gpio import GpioController

def detect_conflicts():
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
        print(f"Running Conflict Detection on {url}")
        print(f"==========================================")
        
        gpio = GpioController()
        try:
            # First, configure all pins CD0-CD7 as INPUTS (0x00)
            gpio.configure(url, direction=0x00)
            pins_idle = gpio.read()
            print(f"Idle state (all inputs): {bin(pins_idle)} (hex: {hex(pins_idle)})")
            
            # Now, try driving each pin CD0-CD4 LOW one by one, and check if it successfully goes LOW
            pin_names = ["CD0 (SCK)", "CD1 (MOSI)", "CD2 (MISO)", "CD3 (CSN)", "CD4 (RSTN)"]
            for pin_idx in range(5):
                mask = 1 << pin_idx
                # Configure ONLY this pin as output (1), others as inputs (0)
                gpio.set_direction(0xff, mask)
                # Drive it LOW (0)
                gpio.write(0x00)
                time.sleep(0.01)
                # Read back all pins
                val = gpio.read()
                bit_val = (val >> pin_idx) & 1
                
                # Check if it successfully went LOW
                if bit_val == 0:
                    print(f"  {pin_names[pin_idx]}: Successfully driven LOW (0)")
                else:
                    print(f"  {pin_names[pin_idx]}: CONFLICT! Attempted to drive LOW, but read back HIGH (1)")
                    
            # Try driving each pin CD0-CD4 HIGH one by one, and check if it successfully goes HIGH
            for pin_idx in range(5):
                mask = 1 << pin_idx
                # Configure ONLY this pin as output (1), others as inputs (0)
                gpio.set_direction(0xff, mask)
                # Drive it HIGH
                gpio.write(mask)
                time.sleep(0.01)
                # Read back all pins
                val = gpio.read()
                bit_val = (val >> pin_idx) & 1
                
                # Check if it successfully went HIGH
                if bit_val == 1:
                    print(f"  {pin_names[pin_idx]}: Successfully driven HIGH (1)")
                else:
                    print(f"  {pin_names[pin_idx]}: CONFLICT! Attempted to drive HIGH, but read back LOW (0)")
                    
        except Exception as e:
            print(f"Error: {e}")
        finally:
            gpio.close()

if __name__ == "__main__":
    detect_conflicts()
