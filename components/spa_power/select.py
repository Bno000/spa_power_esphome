import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select

from . import CONF_SPA_POWER_ID, SpaPowerComponent, spa_power_ns

SpaPowerPumpSelect = spa_power_ns.class_(
    "SpaPowerPumpSelect", select.Select, cg.Parented.template(SpaPowerComponent)
)

CONF_PUMP = "pump"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_SPA_POWER_ID): cv.use_id(SpaPowerComponent),
        cv.Optional(CONF_PUMP): select.select_schema(
            SpaPowerPumpSelect, icon="mdi:pump"
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_SPA_POWER_ID])
    if conf := config.get(CONF_PUMP):
        sel = await select.new_select(conf, options=["off", "auto", "on"])
        await cg.register_parented(sel, hub)
        cg.add(hub.set_pump_select(sel))
