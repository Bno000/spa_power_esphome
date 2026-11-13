import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor

from . import CONF_SPA_POWER_ID, SpaPowerComponent

CONF_LINE1 = "line1"
CONF_LINE2 = "line2"
CONF_KEY = "key"
CONF_PUMP_STATE = "pump_state"
CONF_STATUS_RAW = "status_raw"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_SPA_POWER_ID): cv.use_id(SpaPowerComponent),
        cv.Optional(CONF_LINE1): text_sensor.text_sensor_schema(icon="mdi:numeric-1-box-outline"),
        cv.Optional(CONF_LINE2): text_sensor.text_sensor_schema(icon="mdi:numeric-2-box-outline"),
        cv.Optional(CONF_KEY): text_sensor.text_sensor_schema(icon="mdi:keyboard"),
        cv.Optional(CONF_PUMP_STATE): text_sensor.text_sensor_schema(icon="mdi:pump"),
        cv.Optional(CONF_STATUS_RAW): text_sensor.text_sensor_schema(icon="mdi:code-array"),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_SPA_POWER_ID])
    for key, setter in (
        (CONF_LINE1, hub.set_line1_text_sensor),
        (CONF_LINE2, hub.set_line2_text_sensor),
        (CONF_KEY, hub.set_key_text_sensor),
        (CONF_PUMP_STATE, hub.set_pump_state_text_sensor),
        (CONF_STATUS_RAW, hub.set_status_raw_text_sensor),
    ):
        if conf := config.get(key):
            sens = await text_sensor.new_text_sensor(conf)
            cg.add(setter(sens))
