import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

CODEOWNERS = []
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["binary_sensor", "sensor", "text_sensor"]

CONF_SPA_POWER_ID = "spa_power_id"

spa_power_ns = cg.esphome_ns.namespace("spa_power")
SpaPowerComponent = spa_power_ns.class_(
    "SpaPowerComponent", cg.Component, uart.UARTDevice
)

CONFIG_SCHEMA = (
    cv.Schema({cv.GenerateID(): cv.declare_id(SpaPowerComponent)})
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "spa_power", baud_rate=19200, require_rx=True
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
