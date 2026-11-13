import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

CODEOWNERS = []
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["binary_sensor", "button", "number", "select", "sensor", "switch", "text_sensor"]

CONF_SPA_POWER_ID = "spa_power_id"
CONF_TRANSMIT = "transmit"
CONF_DRY_RUN = "dry_run"
CONF_KEY_HOLD = "key_hold"
CONF_RESPONSE_TIMEOUT = "response_timeout"
CONF_MAX_REPLY_LATENCY = "max_reply_latency"
CONF_KEY_SPACING = "key_spacing"

spa_power_ns = cg.esphome_ns.namespace("spa_power")
SpaPowerComponent = spa_power_ns.class_(
    "SpaPowerComponent", cg.Component, uart.UARTDevice
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SpaPowerComponent),
            # Sending is off by default. With transmit: true the controls are live, and dry_run: true (the default)
            # only logs the key frames they would send.
            cv.Optional(CONF_TRANSMIT, default=False): cv.boolean,
            cv.Optional(CONF_DRY_RUN, default=True): cv.boolean,
            cv.Optional(CONF_KEY_HOLD, default="150ms"): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_RESPONSE_TIMEOUT, default="800ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_MAX_REPLY_LATENCY, default="1500us"
            ): cv.positive_time_period_microseconds,
            cv.Optional(CONF_KEY_SPACING, default="400ms"): cv.positive_time_period_milliseconds,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


def _final_validate(config):
    uart.final_validate_device_schema(
        "spa_power",
        baud_rate=19200,
        require_rx=True,
        require_tx=config[CONF_TRANSMIT],
    )(config)
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_transmit(config[CONF_TRANSMIT]))
    cg.add(var.set_dry_run(config[CONF_DRY_RUN]))
    cg.add(var.set_key_hold_ms(config[CONF_KEY_HOLD]))
    cg.add(var.set_response_timeout_ms(config[CONF_RESPONSE_TIMEOUT]))
    cg.add(var.set_max_reply_latency_us(config[CONF_MAX_REPLY_LATENCY]))
    cg.add(var.set_key_spacing_ms(config[CONF_KEY_SPACING]))
