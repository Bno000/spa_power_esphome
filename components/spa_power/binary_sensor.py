import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import DEVICE_CLASS_HEAT, DEVICE_CLASS_RUNNING

from . import CONF_SPA_POWER_ID, SpaPowerComponent

CONF_PUMP_RUNNING = "pump_running"
CONF_HEATER = "heater"
CONF_OZONE = "ozone"
CONF_PUMP2_RUNNING = "pump2_running"
CONF_AUX = "aux"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_SPA_POWER_ID): cv.use_id(SpaPowerComponent),
        cv.Optional(CONF_PUMP_RUNNING): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_RUNNING, icon="mdi:pump"
        ),
        cv.Optional(CONF_HEATER): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_HEAT, icon="mdi:radiator"
        ),
        cv.Optional(CONF_OZONE): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_RUNNING, icon="mdi:air-filter"
        ),
        cv.Optional(CONF_PUMP2_RUNNING): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_RUNNING, icon="mdi:pump"
        ),
        cv.Optional(CONF_AUX): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_RUNNING, icon="mdi:toggle-switch"
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_SPA_POWER_ID])
    for key, setter in (
        (CONF_PUMP_RUNNING, hub.set_pump_running_binary_sensor),
        (CONF_HEATER, hub.set_heater_binary_sensor),
        (CONF_OZONE, hub.set_ozone_binary_sensor),
        (CONF_PUMP2_RUNNING, hub.set_pump2_running_binary_sensor),
        (CONF_AUX, hub.set_aux_binary_sensor),
    ):
        if conf := config.get(key):
            sens = await binary_sensor.new_binary_sensor(conf)
            cg.add(setter(sens))
