import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch

from . import CONF_SPA_POWER_ID, SpaPowerComponent, spa_power_ns

SpaPowerSwitch = spa_power_ns.class_(
    "SpaPowerSwitch", switch.Switch, cg.Parented.template(SpaPowerComponent)
)

CONF_PUMP2 = "pump2"
CONF_AUX = "aux"

# No state restore: the switch must never press a key by itself at boot.
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_SPA_POWER_ID): cv.use_id(SpaPowerComponent),
        cv.Optional(CONF_PUMP2): switch.switch_schema(
            SpaPowerSwitch, icon="mdi:pump", default_restore_mode="DISABLED"
        ),
        cv.Optional(CONF_AUX): switch.switch_schema(
            SpaPowerSwitch, icon="mdi:toggle-switch", default_restore_mode="DISABLED"
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_SPA_POWER_ID])
    for key, number, setter in (
        (CONF_PUMP2, 1, hub.set_pump2_switch),
        (CONF_AUX, 2, hub.set_aux_switch),
    ):
        if conf := config.get(key):
            sw = await switch.new_switch(conf)
            await cg.register_parented(sw, hub)
            cg.add(sw.set_key(number))
            cg.add(setter(sw))
