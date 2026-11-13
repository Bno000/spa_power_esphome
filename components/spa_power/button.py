import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button

from . import CONF_SPA_POWER_ID, SpaPowerComponent, spa_power_ns

SpaPowerKeyButton = spa_power_ns.class_(
    "SpaPowerKeyButton", button.Button, cg.Parented.template(SpaPowerComponent)
)

CONF_PUMP = "pump"
CONF_PUMP2 = "pump2"
CONF_AUX = "aux"
CONF_UP = "up"
CONF_DOWN = "down"
CONF_SELECT = "select"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_SPA_POWER_ID): cv.use_id(SpaPowerComponent),
        cv.Optional(CONF_PUMP): button.button_schema(SpaPowerKeyButton, icon="mdi:pump"),
        cv.Optional(CONF_PUMP2): button.button_schema(SpaPowerKeyButton, icon="mdi:pump"),
        cv.Optional(CONF_AUX): button.button_schema(
            SpaPowerKeyButton, icon="mdi:toggle-switch"
        ),
        cv.Optional(CONF_UP): button.button_schema(
            SpaPowerKeyButton, icon="mdi:arrow-up-bold"
        ),
        cv.Optional(CONF_DOWN): button.button_schema(
            SpaPowerKeyButton, icon="mdi:arrow-down-bold"
        ),
        cv.Optional(CONF_SELECT): button.button_schema(
            SpaPowerKeyButton, icon="mdi:keyboard-return"
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_SPA_POWER_ID])
    # key numbers match SpaKey in spa_power_parser.h
    for key, number in (
        (CONF_PUMP, 0),
        (CONF_PUMP2, 1),
        (CONF_AUX, 2),
        (CONF_UP, 3),
        (CONF_DOWN, 4),
        (CONF_SELECT, 5),
    ):
        if conf := config.get(key):
            btn = await button.new_button(conf)
            await cg.register_parented(btn, hub)
            cg.add(btn.set_key(number))
