import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import (
    CONF_MAX_VALUE,
    CONF_MIN_VALUE,
    DEVICE_CLASS_TEMPERATURE,
    UNIT_CELSIUS,
)

from . import CONF_SPA_POWER_ID, SpaPowerComponent, spa_power_ns

SpaPowerSetpointNumber = spa_power_ns.class_(
    "SpaPowerSetpointNumber", number.Number, cg.Parented.template(SpaPowerComponent)
)

CONF_SET_TEMPERATURE = "set_temperature"


def _validate_limits(config):
    if config[CONF_MIN_VALUE] >= config[CONF_MAX_VALUE]:
        raise cv.Invalid("min_value must be below max_value")
    return config


# min_value and max_value are required on purpose: they are the safety limits for what the control will ever ask for.
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_SPA_POWER_ID): cv.use_id(SpaPowerComponent),
        cv.Optional(CONF_SET_TEMPERATURE): cv.All(
            number.number_schema(
                SpaPowerSetpointNumber,
                unit_of_measurement=UNIT_CELSIUS,
                device_class=DEVICE_CLASS_TEMPERATURE,
                icon="mdi:thermometer-check",
            ).extend(
                {
                    cv.Required(CONF_MIN_VALUE): cv.float_,
                    cv.Required(CONF_MAX_VALUE): cv.float_,
                }
            ),
            _validate_limits,
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_SPA_POWER_ID])
    if conf := config.get(CONF_SET_TEMPERATURE):
        num = await number.new_number(
            conf,
            min_value=conf[CONF_MIN_VALUE],
            max_value=conf[CONF_MAX_VALUE],
            step=0.25,
        )
        await cg.register_parented(num, hub)
        cg.add(hub.set_setpoint_number(num))
