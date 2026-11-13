import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    DEVICE_CLASS_TEMPERATURE,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_PERCENT,
)

from . import CONF_SPA_POWER_ID, SpaPowerComponent

CONF_CURRENT_TEMPERATURE = "current_temperature"
CONF_SET_TEMPERATURE = "set_temperature"
CONF_LIGHT_LEVEL = "light_level"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_SPA_POWER_ID): cv.use_id(SpaPowerComponent),
        cv.Optional(CONF_CURRENT_TEMPERATURE): sensor.sensor_schema(
            unit_of_measurement=UNIT_CELSIUS,
            accuracy_decimals=2,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_SET_TEMPERATURE): sensor.sensor_schema(
            unit_of_measurement=UNIT_CELSIUS,
            accuracy_decimals=2,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT,
            icon="mdi:thermometer-check",
        ),
        cv.Optional(CONF_LIGHT_LEVEL): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            icon="mdi:lightbulb-on-outline",
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_SPA_POWER_ID])
    for key, setter in (
        (CONF_CURRENT_TEMPERATURE, hub.set_current_temperature_sensor),
        (CONF_SET_TEMPERATURE, hub.set_set_temperature_sensor),
        (CONF_LIGHT_LEVEL, hub.set_light_level_sensor),
    ):
        if conf := config.get(key):
            sens = await sensor.new_sensor(conf)
            cg.add(setter(sens))
