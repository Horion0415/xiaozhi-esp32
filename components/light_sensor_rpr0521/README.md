# RPR-0521 Light Sensor Component

Component for RPR-0521 ambient light and proximity sensor.

## Features
- Ambient light sensing (DATA0/DATA1)
- Proximity detection
- I2C interface (0x38)
- Configurable gain and timing

## Usage

```c
#include "light_sensor_rpr0521.h"

light_sensor_rpr0521_config_t config = {
    .i2c_bus_handle = i2c_bus,
    .device_address = RPR0521_I2C_ADDR,
    .scl_speed_hz = 400000,
    .interrupt_pin = GPIO_NUM_NC,
};

light_sensor_rpr0521_init(&config);

uint16_t light_value;
light_sensor_rpr0521_read_als_data0(&light_value);

light_sensor_rpr0521_deinit();
```

## License
Apache-2.0 