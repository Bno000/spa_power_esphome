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

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_SPA_POWER_ID): cv.use_id(SpaPowerComponent),
        cv.Optional(CONF_PUMP): button.button_schema(SpaPowerKeyButton, icon="mdi:pump"),
        cv.Optional(CONF_PUMP2): button.button_schema(SpaPowerKeyButton, icon="mdi:pump"),
        cv.Optional(CONF_AUX): button.button_schema(
            SpaPowerKeyButton, icon="mdi:toggle-switch"
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_SPA_POWER_ID])
    # key numbers match SpaKey in spa_power_parser.h
    for key, number in ((CONF_PUMP, 0), (CONF_PUMP2, 1), (CONF_AUX, 2)):
        if conf := config.get(key):
            btn = await button.new_button(conf)
            await cg.register_parented(btn, hub)
            cg.add(btn.set_key(number))
